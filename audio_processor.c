#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>   
#include <pthread.h>  
#include <semaphore.h>
#include <sndfile.h> 

#include "audio_processor.h"
#include "stack.h"

#define MAX_AUDIO_URL_LEN 200
#define MAX_AUDIO_MODIFIER_STR_LEN 20
#define MAX_INPUT_LINE_LEN 250

int NUM_THREADS = 1;
int BOUNDED_BUFFER_SIZE = 5;

int total_tasks;
int producers_tasks_completed = 0;
int consumers_tasks_completed = 0;

stack *producer_consumer_audio_buf;
stack *tasks;

pthread_mutex_t tasks_mutex;
pthread_mutex_t producer_consumer_buf_mutex;
pthread_mutex_t producers_tasks_completed_mutex;
pthread_mutex_t consumers_tasks_completed_mutex;
sem_t *empty_slots_available;
sem_t *items_available;


size_t write_cb(void *ptr, size_t size, size_t nmemb, ResponseData *response) {
    size_t total_size = size * nmemb;
    response->data = realloc(response->data, response->size + total_size + 1);
    if (response->data == NULL) {
        perror("Memory reallocation for response data failed\n");
        return 0;
    }
    memcpy(&(response->data[response->size]), ptr, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';
    return total_size;
}

void* fetch_audio() {
    while (1) {
        // Check if all tasks are completed
        pthread_mutex_lock(&producers_tasks_completed_mutex);
        if(producers_tasks_completed == total_tasks){
            pthread_mutex_unlock(&producers_tasks_completed_mutex);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&producers_tasks_completed_mutex);

        // Wait for an empty slot in the buffer before making a cURL request for a new item
        sem_wait(empty_slots_available);
        audio_info *task = NULL;

        // Lock the mutex to access the shared tasks stack
        pthread_mutex_lock(&tasks_mutex);
        task = pop(tasks);
        if(task == NULL){
            sem_post(empty_slots_available); // Post semaphore again since empty slot is still available (no new item was added)
            pthread_mutex_unlock(&tasks_mutex);
            continue;
        }

        pthread_mutex_lock(&producers_tasks_completed_mutex);
        producers_tasks_completed++;
        pthread_mutex_unlock(&producers_tasks_completed_mutex);

        pthread_mutex_unlock(&tasks_mutex);

        // Set up curl
        CURL *curl = curl_easy_init();
        if (!curl) {
            perror("Failed to initialize curl handle.\n");
            free(task->url);
            free(task->modification);
            free(task);
            continue; // Move to the next task
        }

        // Set the URL for the request
        curl_easy_setopt(curl, CURLOPT_URL, task->url);

        // Set the callback function for processing the response data
        task->response = (ResponseData*)malloc(sizeof(ResponseData));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, task->response);

        // Set Freesound API access token in request headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Authorization: Bearer l9mwpKohMYC2dbfTvj9nMQtSHXhAXS");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Perform the curl request
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            perror("Failed to perform curl request\n");
        }

        // // Cleanup curl
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        // Push audio data into shared buffer for processing
        pthread_mutex_lock(&producer_consumer_buf_mutex);
        int result = push(producer_consumer_audio_buf, task);
        pthread_mutex_unlock(&producer_consumer_buf_mutex);

        if (result == -1)
        {
            perror("Buffer is currently full, producer cannot add to it\n");
        }

        // Indicate to consumers that a new item is available in buffer
        sem_post(items_available);
    }
    return NULL;
}

void* process_audio() {
    while(1){
        audio_info *task = NULL;

        pthread_mutex_lock(&consumers_tasks_completed_mutex);
        if (consumers_tasks_completed < total_tasks){
            // Wait until an item is available in buffer to process
            sem_wait(items_available);

            pthread_mutex_lock(&producer_consumer_buf_mutex);
            task = pop(producer_consumer_audio_buf);
            pthread_mutex_unlock(&producer_consumer_buf_mutex);

            consumers_tasks_completed++;

            // Increment empty slots semaphore to tell producers that there is empty space in buffer
            sem_post(empty_slots_available);

            if (task == NULL){
                pthread_mutex_unlock(&consumers_tasks_completed_mutex);
                continue;
            }
        }else{
            // Exit if all tasks are processed
            pthread_mutex_unlock(&consumers_tasks_completed_mutex);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&consumers_tasks_completed_mutex);

        char *name = get_audio_name(task->url);

        // Process the audio according to input
        if(strcmp(task->modification, "REVERSE") == 0){
            if (task->response != NULL && task->response->data != NULL) {
                reverse_audio_samples(name, task->response->data, task->response->size);
            } else {
                perror("No audio data found in response\n");
            }
        }else if(strcmp(task->modification, "HALF_SPEED") == 0){
            if (task->response != NULL && task->response->data != NULL) {
                half_speed_audio(name, task->response->data, task->response->size);
            } else {
                perror("No audio data found in response\n");
            }
        }else if(strcmp(task->modification, "DOUBLE_SPEED") == 0){
            if (task->response != NULL && task->response->data != NULL) {
                double_speed_audio(name, task->response->data, task->response->size);
            } else {
                perror("No audio data found in response\n");
            }
        }

        // Free allocated memory for response data and task
        free(task->response->data);
        free(task->response);
        free(task->url);
        free(task->modification);
        free(task);
    }

    return NULL;
}

char *get_audio_name(char *url) {
    // Collect first token
    char* token = strtok(url, "/");
 
    // Keep printing tokens while "/" present in url
    while (token != NULL) {
        if (strcmp(token, "sounds") == 0) {
            // The next token after the "sounds" token is the audio name
            token = strtok(NULL, "/");
            if (token != NULL) {
                return token;
            }
        }
        token = strtok(NULL, "/");
    }

    return NULL;
}

void reverse_audio_samples(char *name, char *data, size_t size) {
    // Wav files downloaded are 16-bit Bit Depth using stereo channel with sample rate of 44100.0 Hz
    size_t sample_size = 2; // 16 bits = 2 bytes
    size_t num_samples = size / sample_size; // Number of samples in original data

    // Swap the order of samples
    for (size_t i = 0; i < num_samples / 2; ++i) {
        size_t j = num_samples - i - 1;

        for (size_t k = 0; k < sample_size; ++k) {
            char temp = data[i * sample_size + k];
            data[i * sample_size + k] = data[j * sample_size + k];
            data[j * sample_size + k] = temp;
        }
    }

    // Save the new data as a WAV file
    char filename[50];
    snprintf(filename, 50, "audio_files/reversed_%s.wav", name);
    save_audio_to_wav(filename, data, size);
}


void double_speed_audio(char *name, char *data, size_t size) {
    // Wav files downloaded are 16-bit Bit Depth using stereo channel with sample rate of 44100.0 Hz
    size_t sample_size = 2; // 16 bits = 2 bytes

    char *new_data = (char *)malloc(size/2);
    if (new_data == NULL) {
        perror("Memory allocation for new audio data failed\n");
        return;
    }

    // Keep every second sample to double the speed
    for (size_t i = 0, j = 0; i < size; i += 2 * sample_size, ++j) {
        new_data[j * sample_size] = data[i];
        new_data[j * sample_size + 1] = data[i + 1];
    }

    // Save the new data as a WAV file
    char filename[50];
    snprintf(filename, 50, "audio_files/double_%s.wav", name);
    save_audio_to_wav(filename, new_data, size);

    free(new_data);
}

void half_speed_audio(char *name, char *data, size_t size) {
    // Wav files downloaded are 16-bit Bit Depth using stereo channel with sample rate of 44100.0 Hz
    size_t sample_size = 2; // 16 bits = 2 bytes
    size_t num_samples = size / sample_size; // Number of samples in original data

    char *new_data = (char *)malloc(size * 2);
    if (new_data == NULL) {
        perror("Memory allocation for new audio data failed\n");
        return;
    }

    // Duplicate samples to slow down audio
    for (size_t i = 0, j = 0; i < size; i += sample_size, j += 2 * sample_size) {
        // Copy original sample
        new_data[j] = data[i];
        new_data[j + 1] = data[i + 1];
        
        // Insert duplicate sample
        if (i + sample_size < size) {
            new_data[j + 2] = data[i];
            new_data[j + 3] = data[i + 1];
        }
    }

    // Save the new data as a WAV file
    char filename[50];
    snprintf(filename, 50, "audio_files/halved_%s.wav", name);
    save_audio_to_wav(filename, new_data, size * 2);

    free(new_data);
}

int save_audio_to_wav(const char *filename, char *data, size_t size) {
    // Wav files downloaded are 16-bit Bit Depth using stereo (2) channel with sample rate of 44100.0 Hz
    SF_INFO sfinfo;
    memset(&sfinfo, 0, sizeof(SF_INFO));
    sfinfo.samplerate = 44100;
    sfinfo.channels = 2;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE *sndfile = sf_open(filename, SFM_WRITE, &sfinfo);
    if (!sndfile) {
        perror("Error opening audio output file\n");
        return -1;
    }

    sf_write_raw(sndfile, data, size);
    sf_close(sndfile);

    return 0;
}

int main(int argc, char *argv[]) {
    // If option was passed, set them in program (-t THREAD_NUM -b BOUNDED_BUFFER_SIZE)
    if (argc > 2)
    {
        int i = 1;
        while (i < argc)
        {
            if (!strcmp(argv[i], "-t"))
            {
                NUM_THREADS = atoi(argv[i + 1]);
                i += 2;
            }
            else if (!strcmp(argv[i], "-b"))
            {
                BOUNDED_BUFFER_SIZE = atoi(argv[i + 1]);
                i += 2;
            }

            if (i == argc - 1)
            {
                break;
            }
        }
    }
    
    tasks = create_stack(10);
    total_tasks = 0;

    FILE *file = fopen("input.in", "r");
    if (file == NULL) {
        perror("Failed to open input file.\n");
        return 1;
    }

    // Collect all producer tasks from input file
    char line[MAX_INPUT_LINE_LEN];
    while (fgets(line, sizeof(line), file) != NULL) {
        audio_info *curr_audio = (audio_info*)malloc(sizeof(audio_info));
        if (curr_audio == NULL) {
            perror("Failed to allocate memory for curr_audio.\n");
            return 1;
        }

        // Allocate memory for url and modification strings
        curr_audio->url = (char*)malloc(MAX_AUDIO_URL_LEN * sizeof(char));
        curr_audio->modification = (char*)malloc(MAX_AUDIO_MODIFIER_STR_LEN * sizeof(char));
        if (curr_audio->url == NULL || curr_audio->modification == NULL) {
            perror("Failed to allocate memory for url or modification.\n");
            free(curr_audio);
            return 1;
        }

        // Collect task url and modification type
        sscanf(line, "%s %s", curr_audio->url, curr_audio->modification);

        if (strcmp(curr_audio->url, "END") == 0) {
            free(curr_audio->modification);
            free(curr_audio->url);
            free(curr_audio);
            break;
        }

        push(tasks, curr_audio);
        total_tasks++;
    }

    fclose(file);

    // Initialize semaphores and mutexes
    empty_slots_available = sem_open("/empty_sem", O_CREAT | O_EXCL, 0644, BOUNDED_BUFFER_SIZE);
    if (empty_slots_available == SEM_FAILED) {
        perror("Failed to open semphore for empty_slots_available");
        return 1;
    }

    items_available = sem_open("/items_sem", O_CREAT | O_EXCL, 0644, 0);
    if (items_available == SEM_FAILED) {
        sem_close(empty_slots_available);
        sem_unlink("/empty_sem");
        perror("Failed to open semphore for items_available");
        return 1;
    }

    pthread_mutex_init(&tasks_mutex, NULL);
    pthread_mutex_init(&producer_consumer_buf_mutex, NULL);
    pthread_mutex_init(&producers_tasks_completed_mutex, NULL);
    pthread_mutex_init(&consumers_tasks_completed_mutex, NULL);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    producer_consumer_audio_buf = create_stack(BOUNDED_BUFFER_SIZE);
    pthread_t *p_tids = (pthread_t *)malloc(NUM_THREADS * 2 * sizeof(pthread_t));

    // // Create threads for fetching (producers) and processing (consumers) audio data
    for (int i = 0; i < NUM_THREADS; i++){
        pthread_create(&p_tids[i], NULL, fetch_audio, NULL);
    }

    for (int i = NUM_THREADS; i < NUM_THREADS*2; i++){
        pthread_create(&p_tids[i], NULL, process_audio, NULL);
    }

    for (int i = 0; i < NUM_THREADS*2; i++){
        pthread_join(p_tids[i], NULL);
    }

    // Clean up
    free(p_tids);
    free(tasks->array);
    free(tasks);
    free(producer_consumer_audio_buf->array);
    free(producer_consumer_audio_buf);

    pthread_mutex_destroy(&tasks_mutex);
    pthread_mutex_destroy(&producer_consumer_buf_mutex);
    pthread_mutex_destroy(&producers_tasks_completed_mutex);
    pthread_mutex_destroy(&consumers_tasks_completed_mutex);

    sem_close(items_available);
    sem_close(empty_slots_available);
    sem_unlink("/items_sem");
    sem_unlink("/empty_sem");

    curl_global_cleanup();

    return 0;
}
#pragma once

// Structure to hold response data from curl request
typedef struct  {
    char *data;
    size_t size;
} ResponseData;

// Structure to hold audio data
typedef struct {
    char *url;
    char *modification;
    ResponseData *response;
} audio_info;

size_t write_cb(void *ptr, size_t size, size_t nmemb, ResponseData *response);
void* fetch_audio();
void* process_audio();
char *get_audio_name(char *url);
void reverse_audio_samples(char *name, char *data, size_t size);
void double_speed_audio(char *name, char *data, size_t size);
void half_speed_audio(char *name, char *data, size_t size);
int save_audio_to_wav(const char *filename, char *data, size_t size);
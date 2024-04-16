# Audio-Morpher

This project is a multithreaded audio processing application written in C. It utilizes libcurl for fetching audio data from URLs provided by the Freesound API, libsndfile for writing new .wav audio files, and pthreads for concurrency. The application reads tasks from an input file, fetches audio data from the specified URLs, and processes the audio according to the provided modification type (e.g., reverse, half speed, double speed).

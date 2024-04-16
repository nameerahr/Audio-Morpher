COMPILER = gcc
CFLAGS = -I/opt/homebrew/Cellar/libsndfile/1.2.2/include
LDFLAGS = -L/opt/homebrew/Cellar/libsndfile/1.2.2/lib
LIBS = -lcurl -lsndfile

TARGETS = audio_processor

all: audio_processor

audio_processor: audio_processor.o stack.o
	$(COMPILER) $(LDFLAGS) -o audio_processor       audio_processor.o stack.o $(LIBS)

audio_processor.o: audio_processor.c
	$(COMPILER) $(CFLAGS) -c -o audio_processor.o       audio_processor.c

stack.o: stack.c
	$(COMPILER) $(CFLAGS) -c -o stack.o       stack.c

clean:
	rm -f *.o *.out audio_processor
#pragma once

#include "audio_processor.h"

typedef struct {
    audio_info **array;
    int top;
    int capacity;
} stack;

int is_empty(stack *stack);
stack* create_stack(size_t capacity);
int push(stack *stack, audio_info *value);
audio_info* pop(stack *stack);

#include <stdio.h>
#include <stdlib.h>

#include "stack.h"


stack* create_stack(size_t capacity) {
    stack *curr_stack = (stack*)malloc(sizeof(stack));
    if (curr_stack == NULL) { 
        perror("Memory allocation for stack failed\n");
        exit(EXIT_FAILURE); 
    }
    curr_stack->array = (audio_info**)malloc(capacity * sizeof(audio_info*));
    if (curr_stack->array == NULL) {
        perror("Memory allocation for stack array failed\n");
        exit(EXIT_FAILURE);
    }
    curr_stack->top = -1;
    curr_stack->capacity = capacity;
    return curr_stack;
}

int push(stack *stack, audio_info *value) {
    if (stack->top == stack->capacity - 1) {
        stack->capacity *= 2;
        stack->array = (audio_info**)realloc(stack->array, stack->capacity * sizeof(audio_info*));
        if (stack->array == NULL) {
            perror("Memory reallocation for stack array failed\n");
            exit(EXIT_FAILURE);
        }
    }
    stack->array[++stack->top] = value;
    return 0;
}

audio_info* pop(stack *stack) {
    if (stack == NULL) return 0;

    if (!is_empty(stack))
    {
        audio_info *value = (stack->array[stack->top]);
        (stack->top)--;
        return value;
    }
    else
    {
        return NULL;
    }
}

int is_empty(stack *stack) {
    if (stack == NULL) return 0;
    return (stack->top == -1);
}

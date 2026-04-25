#include <stdlib.h>
#include <string.h>
#include "input.h"

// define the input struct -> should include input string, string length, mutator state
struct input {
    char *str;
    size_t len;
    MUTATOR_STATE state;
};

INPUT make_input(const char *input_str) {
    // error if input is invalid
    if(input_str == NULL) {
        return NULL;
    }

    // allocate memory + check for allocation erorr
    INPUT input = malloc(sizeof(struct input));
    if(input == NULL) {
        return NULL;
    }

    input->len = strlen(input_str);
    input->str = malloc(input->len+1);
    if(input->str == NULL) {
        free(input);
        return NULL;
    }

    // copy string + set mutator state to 0 since not mutated yet
    strcpy(input->str, input_str);
    input->state = 0;

    // return newly created input
    return input;
}

void free_input(INPUT input) {
    if(input == NULL) {
        return;
    }
    
    // free all allocated memory
    free(input->str);
    free(input);
}

size_t input_len(INPUT input) {
    if(input == NULL) {
        return 0;
    }

    // return length of input string
    return input->len;
}

const char *input_str(INPUT input) {
    if(input == NULL) {
        return NULL;
    }

    // return input string
    return input->str;
}

MUTATOR_STATE input_mutator_state(INPUT input) {
    if(input == NULL) {
        return 0;
    }

    // return current mutator state of input
    return input->state;
}

MUTATOR_STATE input_set_state(INPUT input, MUTATOR_STATE state) {
    if(input == NULL) {
        return 0;
    }
    // save original state to return, then set new state
    MUTATOR_STATE original_state = input->state;
    input->state = state;
    return original_state;
}

MUTATOR_STATE input_state_step(INPUT input) {
    if(input == NULL) {
        return 0;
    }

    // increment by one THEN return
    return ++input->state;
}

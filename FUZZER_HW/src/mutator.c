#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include "input.h"
#include "mutator.h"
#include "global.h"

#define MAX_INPUT_LENGTH 1024

// helper method: calculates hash function H_{n,m}
static uint64_t H_sequence(uint64_t n, uint64_t m, uint64_t S) {
    if(n == 0) {
        return 0;
    }
    if(m==0) {
        return S % n;
    }
    uint64_t previous = H_sequence(n, m-1, S);
    return hash(previous % n) % n;
}

// strategy 1 -> fill with single character to length
// indexes: 18
static INPUT s1_fill_to_length(INPUT input, uint64_t K) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if string length is 0, return empty string
    if(N == 0){
        return make_input("");
    }

    uint64_t target_len = 1ULL << K;
    if (target_len > MAX_INPUT_LENGTH) {
        target_len = MAX_INPUT_LENGTH;
    }

    // allocate memory
    char *new_str = malloc(target_len + 1);
    if(new_str == NULL) {
        return NULL;
    }

    // if target length is shorter than original -> truncate
    if(target_len <= N) {
        strncpy(new_str, str, target_len);
    // if longer -> fill extra space with character 'a'
    } else {
        strncpy(new_str, str, target_len);
        memset(new_str + N, 'a', target_len - N);
    }
    new_str[target_len] = '\0';

    // make new input using this + return
    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 2 -> duplicate input K+1 times
// indexes: 9
static INPUT s2_duplicate(INPUT input, uint64_t K) {
    // get string to be duplicated and length of string
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // get new length after K+1 duplicates -> max is max_input_length
    uint64_t duplicate_times = K + 1;
    uint64_t new_len = N * duplicate_times;
    if(new_len > MAX_INPUT_LENGTH) {
        new_len = MAX_INPUT_LENGTH;
    }

    // allocate enough memory new string 
    char *new_str = malloc(new_len + 1);
    if(new_str == NULL) {
        return NULL;
    }

    // copy over string K+1 times
    for(uint64_t i=0; i< new_len; i++) {
        new_str[i] = str[i % N];
    }
    new_str[new_len] = '\0';

    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 3 -> lengthen string by L chararacters
// indexes: 3, 6, 10, 12, 16, 21, 27, 35
static INPUT s3_length_by(INPUT input, uint64_t L, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // new length is length of orignal + inject -> max is max input length
    uint64_t new_len = N + L;
    if(new_len > MAX_INPUT_LENGTH){
        new_len = MAX_INPUT_LENGTH;
    }
    uint64_t actual_L = new_len - N;

    // allocate memory
    char *new_str = malloc(new_len + 1);
    if(new_str == NULL) {
        return NULL;
    }

    // input string is in first N characters
    strcpy(new_str, str);
    // from i to L-1, A[N+1] = A[H_{N, i+1} (S)]
    for(uint64_t i=0; i < actual_L; i++) {
        uint64_t index = H_sequence(N, i+1, S);
        new_str[N + i] = str[index];
    }
    new_str[new_len] = '\0';

    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 4 -> truncate input
// indexes: 23
static INPUT s4_truncate(INPUT input, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // truncates input to length H_{N, 1} (S)
    uint64_t truncate_len = H_sequence(N, 1, S);
    if(truncate_len > N) {
        truncate_len = N;
    }

    // allocate memory
    char *new_str = malloc(truncate_len + 1);
    if(new_str == NULL) {
        return NULL;
    }

    // copy over string
    strncpy(new_str, str, truncate_len);
    new_str[truncate_len] = '\0';

    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 5 -> inject string "val"
// indexes: 2, 4, 11, 13, 15, 17, 22, 25, 28, 30, 33
static INPUT s5_inject_string(INPUT input, const char *A, uint64_t K, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // selects p locations in input to inject string A
    uint64_t p = (K+1) / 2;

    // string does NOT increase length of string -> allocate same length
    char *new_str = malloc(N + 1);
    if(new_str == NULL) {
        return NULL;
    }
    strcpy(new_str, str);

    // starting location to inject string are at H_{N,i}(S) for i in range 1,p inclusive
    size_t A_len = strlen(A);
    for(uint64_t i=1; i<=p; i++) { 
        uint64_t pos = H_sequence(N, i, S);
        if(pos < N) {
            size_t inject_len = A_len;
            // truncate injected string so length does not change
            if(pos + inject_len > N) {
                inject_len = N - pos;
            }
            strncpy(new_str + pos, A, inject_len);
        }
    }

    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 6 -> inject random integer string
// indexes: 24
static INPUT s6_inject_random_int(INPUT input, uint64_t K, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // chooses p locations to inject string
    uint64_t p = (K + 1) / 2;

    // string of random 32-bit signed integer Z -> chosen to be HASH(S) casted to be 32bit signed integer
    int32_t Z = (int32_t)hash(S);
    char int_str[32];
    snprintf(int_str, sizeof(int_str), "%d", Z);
    
    // does not increase length of string
    char *new_str = malloc(N + 1);
    if (new_str == NULL) {
        return NULL;
    }
    strcpy(new_str, str);
    
    size_t int_len = strlen(int_str);
    
    // starting location to inject string are at H_{N,i}(S) for i in range 1,p inclusive
    for (uint64_t i = 1; i <= p; i++) {
        uint64_t pos = H_sequence(N, i, S);
        if (pos < N) {
            size_t inject_len = int_len;
            if (pos + inject_len > N) {
                inject_len = N - pos;
            }
            strncpy(new_str + pos, int_str, inject_len);
        }
    }

    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 7 -> inject random substring of length L
// indexes: 1, 19, 31, 36
static INPUT s7_inject_random_substring(INPUT input, uint64_t L, int64_t K, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // chooses p locations to inject substring
    uint64_t p = (K + 1) / 2;
    
    // does not incrrease length of string
    char *new_str = malloc(N + 1);
    if (new_str == NULL) {
        return NULL;
    }
    strcpy(new_str, str);
    
    // starting location to inject string are at H_{N,i}(S) for i in range 1,p inclusive
    for (uint64_t i = 1; i <= p; i++) {
        // substring starts at H_{N,1}(S)
        uint64_t start_pos = H_sequence(N, 1, S);
        uint64_t inject_pos = H_sequence(N, i + 1, S);
        
        if (inject_pos < N) {
            // if H_{N,1}(S) + L is greather than N => substring terminates when input string terminates
            uint64_t substr_end = start_pos + L;
            if (substr_end > N) {
                substr_end = N;
            }
            
            // mutation should not decrease the size of the input -> truncates injected string to fit
            uint64_t substr_len = substr_end - start_pos;
            size_t actual_inject_len = substr_len;
            
            if (inject_pos + actual_inject_len > N) {
                actual_inject_len = N - inject_pos;
            }
            
            strncpy(new_str + inject_pos, str + start_pos, actual_inject_len);
        }
    }
    
    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 8 -> inject char 'c'
// indexes: 5, 20, 26, 34
static INPUT s8_inject_char(INPUT input, char C, int64_t K, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // choose p locations to inject character C
    uint64_t p = (K + 1) / 2;
    
    char *new_str = malloc(N + 1);
    if (new_str == NULL) {
        return NULL;
    }
    strcpy(new_str, str);
    
    // locations to substitue with character is H_{N,i}(S) for i in range 1 to p, inclusive
    for (uint64_t i = 1; i <= p; i++) {
        uint64_t pos = H_sequence(N, i, S);
        if (pos < N) {
            new_str[pos] = C;
        }
    }
    
    INPUT result = make_input(new_str);
    free(new_str);
    return result;
}

// strategy 9 -> flip L bits
// indexes: 7. 14, 32, 37
static INPUT s9_flip_bits(INPUT input, uint64_t L, int64_t K, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // choose p locations to flop L bit sequences of 
    uint64_t p = (K + 1) / 2;
    // bit location = position of single bit in string -> total of 8*N bits in input string
    uint64_t total_bits = 8 * N;
    
    char *new_str = malloc(N + 1);
    if (new_str == NULL) {
        return NULL;
    }
    strcpy(new_str, str);
    
    // flip sequences of L bits at H_{8*N, i} (S) in range 1 to p, inclusive
    for (uint64_t i = 1; i <= p; i++) {
        uint64_t bit_pos = H_sequence(total_bits, i, S);
        // do NOT flip bits outside the range of bit locations
        if (bit_pos >= total_bits) {
            continue;
        }
        
        uint64_t byte_idx = bit_pos / 8;
        uint64_t bit_idx = bit_pos % 8;
        
        for (uint64_t j = 0; j < L && bit_idx + j < 8; j++) {
            new_str[byte_idx] ^= (1 << (bit_idx + j));
        }
    }
    
    INPUT result = make_input(new_str);
    free(new_str);
    return result;

}

// strategy 10 -> increment bytes
// indexes: 29
static INPUT s10_increment_bytes(INPUT input, int64_t K, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // choose p locations to increment byte by 1
    uint64_t p = (K + 1) / 2;
    
    char *new_str = malloc(N + 1);
    if (new_str == NULL) {
        return NULL;
    }
    strcpy(new_str, str);
    
    // increment at H_{N, i}(S) for i in range i to p, inclusive
    for (uint64_t i = 1; i <= p; i++) {
        uint64_t pos = H_sequence(N, i, S);
        if (pos < N) {
            new_str[pos]++;
        }
    }
    
    INPUT result = make_input(new_str);
    free(new_str);
    return result;

}

// strategy 11 -> decrement bytes
// indexes: 8
static INPUT s11_decrement_bytes(INPUT input, int64_t K, uint64_t S) {
    size_t N = input_len(input);
    const char *str = input_str(input);

    // if empty string, return new input with empty string
    if(N == 0) {
        return make_input("");
    }

    // choose p locations to decrement byte by 1
    uint64_t p = (K + 1) / 2;
    
    char *new_str = malloc(N + 1);
    if (new_str == NULL) {
        return NULL;
    }
    strcpy(new_str, str);
    
    // decrement at H_{N, i}(S) for i in range i to p, inclusive
    for (uint64_t i = 1; i <= p; i++) {
        uint64_t pos = H_sequence(N, i, S);
        if (pos < N) {
            new_str[pos]--;
        }
    }
    
    INPUT result = make_input(new_str);
    free(new_str);
    return result;

}

INPUT mutate(INPUT input) {
    if(input == NULL) {
        return NULL;
    }
    // get input's mutator state + calculate value for chosen strategy
    MUTATOR_STATE S = input_mutator_state(input);
    uint64_t K = (S/37) + 1;
    int mutation_strategy = S % 37;

    INPUT result = NULL;

    switch(mutation_strategy) {
        // (1) strategy 7 with L=1
        case 0:
            result = s7_inject_random_substring(input, 1, K, S);
            break;

        // (2) strategy 5 with A="0"
        case 1:
            result = s5_inject_string(input, "0", K, S);
            break;

        // (3) strategy 3 with L=4
        case 2:
            result = s3_length_by(input, 4, S);
            break;

        // (4) strategy 5 with A="1"
        case 3:
            result = s5_inject_string(input, "1", K, S);
            break;

        // (5) strategy 8 with C='/'
        case 4:
            result = s8_inject_char(input, '/', K, S);
            break;

        // (6) strategy 3 with L=7
        case 5:
            result = s3_length_by(input, 7, S);
            break;

        // (7) strategy 9 with L=1
        case 6:
            result = s9_flip_bits(input, 1, K, S);
            break;

        // (8) strategy 11
        case 7:
            result = s11_decrement_bytes(input, K, S);
            break;

        // (9) strategy 2
        case 8:
            result = s2_duplicate(input, K);
            break;

        // (10) strategy 3 with L=1
        case 9:
            result = s3_length_by(input, 1, S);
            break;

        // (11) strategy 5 with A="-128"
        case 10:
            result = s5_inject_string(input, "-128", K, S);
            break;

        // (12) strategy 3 with L=8
        case 11:
            result = s3_length_by(input, 8, S);
            break;

        // (13) strategy 5 with A="2147483647"
        case 12:
            result = s5_inject_string(input, "2147483647", K, S);
            break;

        // (14) strategy 9 with L=4
        case 13:
            result = s9_flip_bits(input, 4, K, S);
            break;

        // (15) strategy 5 with A="-1"
        case 14:
            result = s5_inject_string(input, "-1", K, S);
            break;

        // (16) strategy 3 with L=2
        case 15:
            result = s3_length_by(input, 2, S);
            break;

        // (17) strategy 5 with A="32767"
        case 16:
            result = s5_inject_string(input, "32767", K, S);
            break;

        // (18) strategy 1
        case 17:
            result = s1_fill_to_length(input, K);
            break;

        // (19) strategy 7 with L=2
        case 18:
            result = s7_inject_random_substring(input, 2, K, S);
            break;

        // (20) strategy 8 with C='.'
        case 19:
            result = s8_inject_char(input, '.', K, S);
            break;

        // (21) strategy 3 with L=3
        case 20:
            result = s3_length_by(input, 3, S);
            break;

        // (22) strategy 5 with A="%p"
        case 21:
            result = s5_inject_string(input, "%p", K, S);
            break;

        // (23) strategy 4
        case 22:
            result = s4_truncate(input, S);
            break;

        // (24) strategy 6
        case 23:
            result = s6_inject_random_int(input, K, S);
            break;

        // (25) strategy 5 with A="127"
        case 24:
            result = s5_inject_string(input, "127", K, S);
            break;

        // (26) strategy 8 with C=';'
        case 25:
            result = s8_inject_char(input, ';', K, S);
            break;

        // (27) strategy 3 with L=5
        case 26:
            result = s3_length_by(input, 5, S);
            break;

        // (28) strategy 5 with A="-32768"
        case 27:
            result = s5_inject_string(input, "-32768", K, S);
            break;

        // (29) strategy 10
        case 28:
            result = s10_increment_bytes(input, K, S);
            break;

        // (30) strategy 5 with A="%s"
        case 29:
            result = s5_inject_string(input, "%s", K, S);
            break;

        // (31) strategy 7 with L=8
        case 30:
            result = s7_inject_random_substring(input, 8, K, S);
            break;

        // (32) strategy 9 with L=8
        case 31:
            result = s9_flip_bits(input, 8, K, S);
            break;

        // (33) strategy 5 with A="-2147483648"
        case 32:
            result = s5_inject_string(input, "-2147483648", K, S);
            break;

        // (34) strategy 8 with C=','
        case 33:
            result = s8_inject_char(input, ',', K, S);
            break;

        // (35) strategy 3 with L=6
        case 34:
            result = s3_length_by(input, 6, S);
            break;

        // (36) strategy 7 with L=4
        case 35:
            result = s7_inject_random_substring(input, 4, K, S);
            break;

        // (37) strategy 9 with L=2
        case 36:
            result = s9_flip_bits(input, 2, K, S);
            break;

        // default return original input, no mutation
        default: 
            make_input(input_str(input));
            break;
    }

    // increment mutator state after success
    input_state_step(input);
    return result;
}
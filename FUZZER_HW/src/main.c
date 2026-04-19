#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fuzzer.h"
#include "global.h"

int main(int argc, char *argv[]) {
    int job_count = DEFAULT_RUNNER_COUNT;
    int input_count = DEFAULT_INPUT_TOTAL;
    int time_limit = DEFAULT_TIMEOUT_SEC;
    const char *seed_file = NULL;
    
    int h_flag = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            break;
        }
        // if -h is found, ignore all other arguments
        if (strcmp(argv[i], "-h") == 0) {
            h_flag = 1;
            break;

        // else, -s seed_file is required, followed by commands
        } else if (strcmp(argv[i], "-s") == 0) {
            // if -i is not followed by filename, exit fail
            if (i + 1 >= argc) {
                PRINT_USAGE(stderr, argv[0]);
                return EXIT_FAILURE;
            }
            seed_file = argv[++i];
        // -j specifies number of jobs that the fuzzers will use 
        } else if (strcmp(argv[i], "-j") == 0) {
            if (i + 1 >= argc) {
                PRINT_USAGE(stderr, argv[0]);
                return EXIT_FAILURE;
            }

            job_count = atoi(argv[++i]);
            if(job_count <= 0) {
                PRINT_USAGE(stderr, argv[0]);
                return EXIT_FAILURE;
            }

        } else if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                PRINT_USAGE(stderr, argv[0]);
                return EXIT_FAILURE;
            }

            input_count = atoi(argv[++i]);
            if(input_count <= 0) {
                PRINT_USAGE(stderr, argv[0]);
                return EXIT_FAILURE;
            }

        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                PRINT_USAGE(stderr, argv[0]);
                return EXIT_FAILURE;
            }

            time_limit = atoi(argv[++i]);
            if(time_limit <= 0) {
                PRINT_USAGE(stderr, argv[0]);
                return EXIT_FAILURE;
            }
        } else {
            PRINT_USAGE(stderr, argv[0]);
            return EXIT_FAILURE;
        }
    }
    if(h_flag) {
        PRINT_USAGE(stdout, argv[0]);
        return EXIT_SUCCESS;
    }
    if(seed_file == NULL) {
        PRINT_USAGE(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    int program_start = 1;
    for(int i=1; i<argc; i++) {
        if(argv[i][0] != '-') {
            program_start = i;
            break;
        }

        if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "-t") == 0) {
            i++;
        }
    }

    if (program_start >= argc) {
        PRINT_USAGE(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    cmd = argv[program_start];
    args = &argv[program_start];
    program_argc = argc - program_start;
    timeout = time_limit;

    int found_at = 0;
    for(size_t j=0; j<program_argc; j++) {
        if (strcmp(args[j], PROGRAM_ARGUMENT_PLACEHOLDER) == 0) {
            found_at = 1;
            break;
        }
    }
    if(!found_at) {
        PRINT_USAGE(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    FILE *seed_fp = fopen(seed_file, "r");
    if (seed_fp == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    //int result = run_fuzzer(seed_fp, job_count, input_count, time_limit, args);
    
    fclose(seed_fp);
    return EXIT_SUCCESS;
}
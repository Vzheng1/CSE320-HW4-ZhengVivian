#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "fuzzer.h"
#include "input.h"
#include "input_queue.h"
#include "mutator.h"
#include "coverage_map.h"
#include "runner.h"
#include "global.h"

// signal flags
static volatile sig_atomic_t sigusr1_received = 0;
static volatile sig_atomic_t sigchld_received = 0;
static volatile sig_atomic_t terminate_fuzzer = 0;

// signal handlers
static void sigusr1_handler(int sig) {
    (void)sig;
    sigusr1_received = 1;
}

static void sigchld_handler(int sig) {
    (void)sig;
    sigchld_received = 1;
}

static void terminate_handler(int sig) {
    (void)sig;
    terminate_fuzzer = 1;
}

int run_fuzzer(FILE *seed_file, int job_count, int input_count, int time_limit, char *target_program[]) {
    fzl_init(NULL);

    // install signal handlers
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, sigusr1_handler);
    signal(SIGCHLD, sigchld_handler);
    //SIGINT, SIGTERM, SIGHUP -> terminate 
    signal(SIGINT, terminate_handler);
    signal(SIGTERM, terminate_handler);
    signal(SIGHUP, terminate_handler);

    // initialize input queue
    INPUT_QUEUE queue = input_queue_init();
    if(queue == NULL) {
        fzl_fini(NULL);
        return EXIT_FAILURE;
    }

    // initialize coverage map
    COVERAGE_MAP map = coverage_map_init();
    if(map == NULL) {
        input_queue_fini(queue);
        fzl_fini(NULL);
        return EXIT_FAILURE;
    }

    // initialize runners
    RUNNERS runners = runners_init(job_count);
    if(runners == NULL) {
        input_queue_fini(queue);
        coverage_map_fini(map);
        fzl_fini(NULL);
        return EXIT_FAILURE;
    }

    /*There is also the input_count parameter which specifies the total number of inputs to test with the fuzzer excluding the inputs from the seed file. This function must make sure that once this upper limit has been reached and the results has been returned to the main process, that the program cleans up before exiting with EXIT_SUCCESS.
        .*/
    
    // read lines from the seed file -> create input -> enqueue
    char line[4096];
    while(fgets(line, sizeof(line), seed_file) != NULL) {
        size_t len = strlen(line);

        // remove trailing newline if exists
        if(len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        // create input from the line read + enqueue into high priority queue
        INPUT input = make_input(line);
        if(input != NULL) {
            enqueue_high_prio_input(queue, input);
        }
    }

    // loop -> checks if any runner is done
    //      -for done runners: fuzzer extracts results from running the target program 
    //          -> if exited normally, fuzzer determiners if if high/low priority input + inserts input into appropriate queue
    //              - if neither priority, discard input
    //          -> if crashed, print out message describing input + crash
    //          -> if timeout, input is ignored + may also print out message of timeout (optional)
    int inputs_tested = 0;
    while(inputs_tested < input_count && !terminate_fuzzer) {
        // assign any ready jobs with inputs -> dequeue from queue + pass to mutation engine
        //      initial seed inputs should not be mutated on first pass through fuzzer -> fuzzer submits mutated input to runner
        if(runners_has_ready_jobs(runners)) {
            INPUT input = dequeue_input(queue);
            if(input != NULL) {
                INPUT mutated_input = mutate(input);
                if(mutated_input != NULL) {
                    if(runners_submit_input(runners, mutated_input) == 0) {
                        inputs_tested++;
                    }
                }
            }
        }

        // SIGUSR1 -> runner has completed
        if(sigusr1_received) {
            // reset signal + check again to make sure runners are done
            sigusr1_received = 0;
            runners_check_if_jobs_done(runners);

            while(runners_has_done_jobs(runners)) {
                RUNNER_STATE state;
                int exit_code = 0;

                RUNNER runner = runners_process_result(runners, &state, &exit_code);
                if(runner != NULL) {
                    int runner_id = runner_get_id(runner);
                    fzl_received_status(runner_id, state, exit_code, NULL);
                    
                    // if received status is VALID (exited normally) -> determine if high/low priority + insert into correct queue
                    if(state==VALID) {
                        char *coverage_data = runner_coverage_map(runner);
                        if(coverage_data != NULL) {
                            // get priority based on coverage map data
                            COVERAGE_PRIORITY priority = coverage_map_add(map, coverage_data);
                            INPUT active_input = runner_get_active_input(runner);

                            // high priority -> enqueue into high priority queue
                            if(priority == COV_HIGH_PRIO && active_input != NULL) {
                                INPUT duplicate_input = make_input(input_str(active_input));
                                if(duplicate_input != NULL) {
                                    enqueue_high_prio_input(queue, duplicate_input);
                                }
                            // low priority -> enqueue into low priority queue
                            } else if(priority == COV_LOW_PRIO  && active_input != NULL) {
                                INPUT duplicate_input = make_input(input_str(active_input));
                                if(duplicate_input != NULL) {
                                    enqueue_low_prio_input(queue, duplicate_input);
                                }
                            }
                        }
                    // if CRASHED -> print out message describing input and crash
                    } else if (state == CRASH) {
                        INPUT active_input = runner_get_active_input(runner);
                        if(active_input != NULL) {
                            fprintf(stdout, "CRASH: Input: %s (Signal: %d)\n", input_str(active_input), exit_code);
                        }
                    } else if (state == TIMEOUT) {
                        // optional
                    }
                }
            }
        }
        // if received sigchld signal -> reap terminated process
        if(sigchld_received) {
            sigchld_received = 0;
            runners_reap(runners);
        }
    }

    // clean up and return success
    runners_fini(runners);
    coverage_map_fini(map);
    input_queue_fini(queue);
    fzl_fini(NULL);
    return EXIT_SUCCESS;
}
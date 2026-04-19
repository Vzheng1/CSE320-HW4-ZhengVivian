#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include "runner.h"
#include "input.h"
#include "global.h"
#include "fuzzer.h"

// global variable for initializing runner id
static int runner_id = 0;

/**
implement runner struct:
    id = unique runner id
    input pipe = for receieving inputs from main process -> [0] = read end , [1] = write end (runner read/writes)
    status pipe = for sending results to main -> [0] read end, [1] write end (main reads/writes)
    shm = name of shared memory object + file descriptor
    runner_pid = pid of runner child
    active_input = currently being executed by this runner
 */

struct runner {
    int id;
    int input_pipe[2];
    int status_pipe[2];
    char *shm_name;
    int shm_fd;
    char *coverage_map;
    pid_t runner_pid;
    INPUT active_input;
};

RUNNER runner_init() {
    // allocate memory for runner struct
    RUNNER runner = malloc(sizeof(struct runner));
    if(runner == NULL) {
        return NULL;
    }

    // initialize values
    runner->id = runner_id++;
    runner->runner_pid = -1;
    runner->active_input = NULL;
    runner->coverage_map = NULL;

    // initialize pipes
    if(pipe(runner->input_pipe) == -1) {
        free(runner);
        return NULL;
    }
    if(pipe(runner->status_pipe) == -1) {
        close(runner->input_pipe[0]);
        close(runner->input_pipe[1]);
        free(runner);
        return NULL;
    }

    // the pipe that the main process reads from -> status pipe
    // end should be set as nonblocking using the O_NONBLOCK flag in the fcntl system call.
    //      allows main process to check for results without blocking
    if(fcntl(runner->status_pipe[0], F_SETFL, O_NONBLOCK) == -1) {
        close(runner->input_pipe[0]);
        close(runner->input_pipe[1]);
        close(runner->status_pipe[0]);
        close(runner->status_pipe[1]);
        free(runner);
        return NULL;
    }

    // create unique shared memory
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/fuzzer_runner_%d_%ld", runner->id, (long)getpid());
    
    // allocate memory for shared name
    runner->shm_name = malloc(strlen(shm_name) + 1);
    if (runner->shm_name == NULL) {
        close(runner->input_pipe[0]);
        close(runner->input_pipe[1]);
        close(runner->status_pipe[0]);
        close(runner->status_pipe[1]);
        free(runner);
        return NULL;
    }
    strcpy(runner->shm_name, shm_name);
    
    // remove any existing shared memory with the same name, then create new one + check for error
    shm_unlink(runner->shm_name);
    runner->shm_fd = shm_open(runner->shm_name, O_CREAT | O_RDWR, 0600);
    if (runner->shm_fd == -1) {
        close(runner->input_pipe[0]);
        close(runner->input_pipe[1]);
        close(runner->status_pipe[0]);
        close(runner->status_pipe[1]);
        free(runner->shm_name);
        free(runner);
        return NULL;
    }
    
    /* size of shared memory should be COVERAGE_MAP_SIZE bits -> (COVERAGE_MAP_SIZE bits + 7) / 8 bytes
     *      -makes sure to have enough space for ALL bits
     * ftruncate() resizes the shared memory object to calculated size then handle erorr if occur
     */
    size_t bitmap_bytes = (COVERAGE_MAP_SIZE + 7) / 8;
    if (ftruncate(runner->shm_fd, bitmap_bytes) == -1) {
        close(runner->input_pipe[0]);
        close(runner->input_pipe[1]);
        close(runner->status_pipe[0]);
        close(runner->status_pipe[1]);
        close(runner->shm_fd);
        shm_unlink(runner->shm_name);
        free(runner->shm_name);
        free(runner);
        return NULL;
    }
    
    // map the shared memory into the processes's address space -> coverage map
    runner->coverage_map = mmap(NULL, bitmap_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, runner->shm_fd, 0);
    if (runner->coverage_map == MAP_FAILED) {
        close(runner->input_pipe[0]);
        close(runner->input_pipe[1]);
        close(runner->status_pipe[0]);
        close(runner->status_pipe[1]);
        close(runner->shm_fd);
        shm_unlink(runner->shm_name);
        free(runner->shm_name);
        free(runner);
        return NULL;
    }
    
    /* initialize coverage map to all zeros since there is no coverage data yet */
    memset(runner->coverage_map, 0, bitmap_bytes);
    
    /* log that runner was initialized) */
    fzl_runner_init(runner->id, NULL);
    
    return runner;
}

void runner_fini(RUNNER runner) {
    // invalid 
    if(runner == NULL) {
        return;
    }

    // terminate runner if still running
    if(runner->runner_pid > 0) {
        kill(runner->runner_pid, SIGTERM);
        waitpid(runner->runner_pid, NULL, 0);
    }
    // free active input if there is
    if(runner->active_input != NULL) {
        free_input(runner->active_input);
    }

    // close pipes
    close(runner->input_pipe[0]);
    close(runner->input_pipe[1]);
    close(runner->status_pipe[0]);
    close(runner->status_pipe[1]);

    // unmsp shared memory from address space -> must be same size as mmap
    size_t bitmap_bytes = (COVERAGE_MAP_SIZE + 7)/8;
    if(runner->coverage_map != NULL) {
        munmap(runner->coverage_map, bitmap_bytes);
    }
    // close shared memory fd
    if(runner->shm_fd >= 0) {
        close(runner->shm_fd);
    }
    // delete shared memory from system 
    if(runner->shm_name != NULL) {
        shm_unlink(runner->shm_name);
        free(runner->shm_name);
    }

    fzl_runner_fini(runner->id, NULL);
    free(runner);
}

int runner_get_id(RUNNER runner) {
    if (runner == NULL) {
        return -1;
    }
    return runner->id;
}

char *runner_coverage_map(RUNNER runner) {
    if(runner == NULL) {
        return NULL;
    }

    // return address storing coverage-feedback data written to by the target process.
    return runner->coverage_map;
}

INPUT runner_get_active_input(RUNNER runner) {
    if(runner == NULL) {
        return NULL;
    }

    // return INPUT currently handling
    return runner->active_input;
}

// should be called only by the main process -> fuzzer main sends INPUT to runner process via pipe set up
//      this function should only be called if the runner has been launched via runner_launch
// called to give a runner a new input to test
int fuzzer_send_runner_input(RUNNER runner, INPUT input) {
    // invalid
    if(runner == NULL || input == NULL) {
        return -1;
    }

    // get input string and length
    const char *str = input_str(input);
    size_t len = input_len(input);

    // send the length through input pipe -> so runner knows how much to read
    if(write(runner->input_pipe[1], &len, sizeof(len)) == -1) {
        return -1;
    }
    // then send the input string
    if(write(runner->input_pipe[1], str, len) == -1) {
        return -1;
    }

    // if runner currently has active input, free it THEN store new input to update 
    if(runner->active_input != NULL) {
        free_input(runner->active_input);
    }
    runner->active_input = make_input(str);

    // log for testing
    fzl_sending_input(runner->id, str, NULL);
    return 0;
}

// called only by the runner process -> runner attempting to receive an INPUT from the main fuzzer process through the pipe
//      this process blocks until input has been received from the main fuzzer process or a signal has been received
//
// called by runner to get next input to run -> blocks until main process sends input/signal arrives
char * runner_receive_fuzzer_input(RUNNER runner) {
    if(runner == NULL) {
        return NULL;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(runner->input_pipe[0], &read_fds);

    sigset_t sigmask, oldmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGTERM);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGHUP);
    sigaddset(&sigmask, SIGPIPE);

    sigprocmask(SIG_BLOCK, &sigmask, &oldmask);

    int ret = pselect(runner->input_pipe[0] + 1, &read_fds, NULL, NULL, NULL, &oldmask);
    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    if(ret == -1) {
        if(errno == EINTR) {
            return NULL;
        }
        return NULL;
    }
    if(ret == 0 || !FD_ISSET(runner->input_pipe[0], &read_fds)) {
        return NULL;
    }

    // read length of input from main 
    size_t len;
    ssize_t bytes_read = read(runner->input_pipe[0], &len, sizeof(len));
    if(bytes_read == -1) {
        if(errno == EINTR) {
            return NULL;
        }
        return NULL;
    }
    if(bytes_read == 0) {
        // Pipe closed
        return NULL;
    }
    if(bytes_read != sizeof(len)) {
        // Incomplete read
        return NULL;
    }

    if(len > 1024 * 1024) {  // Max 1MB input size
        return NULL;
    }

    // allocate buffer for the input string + null at end
    char *input_buffer = malloc(len + 1);
    if(input_buffer == NULL) {
        return NULL;
    }

    // read input string + add null at end
    size_t total_read = 0;
    while(total_read < len) {
        bytes_read = read(runner->input_pipe[0], input_buffer + total_read, len - total_read);
        if(bytes_read == -1) {
            if(errno == EINTR) {
                free(input_buffer);
                return NULL;
            }
            free(input_buffer);
            return NULL;
        }
        if(bytes_read == 0) {
            // Premature EOF
            free(input_buffer);
            return NULL;
        }
        total_read += bytes_read;
    }
    input_buffer[len] = '\0';

    fzl_runner_received_input(runner->id, input_buffer, NULL);
    return input_buffer;

}

// called only by the runner process -> called when the runner's child process has completed
//      purpose = for the runner to send the status of the program back to the main fuzzer process
// done by (1) sending the main process the status of the target program through the pipe
//         (2) To alert the main fuzzer process that this runner is ready, it will send the main process a SIGUSR1 signal after writing to the pipe.
int runner_alert_fuzzer(RUNNER runner, RUNNER_STATE state, int data) {
    if(runner == NULL) {
        return -1;
    }

    // writes exit status to status pipe for main to access
    if(write(runner->status_pipe[1], &state, sizeof(state)) == -1) {
        return -1;
    }
    // write data to pipe
    if(write(runner->status_pipe[1], &data, sizeof(data)) == -1) {
        return -1;
    }
    fzl_runner_sending_status(runner->id, state, data, NULL);

    // send signal to main process (use kill) -> send SIGUSR1
    if(kill(getppid(), SIGUSR1) == -1) {
        return -1;
    }

    return 0;
}

// only be called if SIGUSRI has been receieved by the main fuzzer process -> attempt to receive status written to in pipe by runner process if there
//      If not there, the function will return NO_STATE
//      Otherwise, it will store the state and update data if it is non-null and the state is not TIMEOUT
// This function should NOT block if O_NONBLOCK was set correctly in runner_init.
RUNNER_STATE fuzzer_attempt_receive_status(RUNNER runner, int *data) {
    if(runner == NULL) {
        return NO_STATE;
    }

    // try to read state from status pipe (non block)
    RUNNER_STATE state;
    if(read(runner->status_pipe[0], &state, sizeof(state)) == -1) {
        return NO_STATE;
    } 
    // read data
    int sent_data;
    if(read(runner->status_pipe[0], &sent_data, sizeof(sent_data)) == -1) {
        return NO_STATE;
    } 

    // if state + data is non null AND state is not TIMEOUT -> store both
    if(state != TIMEOUT && data != NULL) {
        *data = sent_data;
    }
    return state;
}

static volatile sig_atomic_t sigchld_received = 0;
static volatile sig_atomic_t sigalrm_received = 0;
static volatile sig_atomic_t runner_terminate = 0;

static void sigchld_handler(int sig) {
    (void)sig;
    sigchld_received = 1;
}
static void sigalrm_handler(int sig) {
    (void)sig;
    sigalrm_received = 1;
}
static void terminate_handler(int sig) {
    (void)sig;
    runner_terminate = 1;
}

// only be called by the main fuzzer process -> forking and launching the runner job
// (1) runner starts by installing signal handlers for the aforementioned signals and closing its ends to pipes
// (2) redirects the file descriptor of the shared memory to COVERAGE_MAP_FD
// (3) runner enters a loop: 
//      (a)if runner does NOT have input to run -> wait for parent to send an input via runner_receieve_fuzzer_input
//          -once input is receieve, will fork + execute target program with input
//      (b) before exec call, open /dev/null + redirect standard output/input to this file descriptor using dup2
//          -ensures output of the target program does not clutter the terminal
//      (c) process sets alarm to set time limit for target process
//           -if runner receives SIGALRM + process is running -> forward SIGALRM to child process to terminate
//           -if runner receives SIGCHLD -> program reap its child process + obtain status of program to send back to main using runner_alert_fuzzer + reset alarm so it is no longer received
//      -loop repeats until runner receives termination signal -> should handle cleaning process before exit
// (4) on parent side, make sure to close pipes too
int runner_launch(RUNNER runner) {
    if(runner == NULL) {
        return -1;
    }
    
    // for a new child process
    pid_t pid = fork();
    if(pid == -1) {
        return -1;
    }

    if(pid == 0) {
        
        // install signal handlers for this process
        signal(SIGPIPE, SIG_IGN);                       
        signal(SIGCHLD, sigchld_handler);        
        signal(SIGALRM, sigalrm_handler);       
        signal(SIGTERM, terminate_handler);       
        signal(SIGINT, terminate_handler);         
        signal(SIGHUP, terminate_handler);

        // block signals during critical sections
        sigset_t mask, oldmask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGALRM);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGHUP);

        // close pipes not used by runner -> only reads from input_pipe[0], write to status_pipe[1]
        // close input_pipe[1], status_pipe[0]
        close(runner->input_pipe[1]);
        close(runner->status_pipe[0]);

        // redirect shared memory FD to COVERAGE_MAP_FD
        if(dup2(runner->shm_fd, COVERAGE_MAP_FD) == -1) {
            exit(EXIT_FAILURE);
        }
        fzl_runner_init(runner->id, NULL);

        // wait for input from main fuzzer -> blocks until main sends input/signal arrives
        while (!runner_terminate) {
            // read input -> fail 
            char *input_buffer = runner_receive_fuzzer_input(runner);
            if (input_buffer == NULL) {
                if (runner_terminate) {
                    break;
                }
                continue;
            }
            
            // reset signal flags for this
            sigchld_received = 0;
            sigalrm_received = 0;
            
            // fork child process to run target program
            pid_t child_pid = fork();
            if (child_pid == -1) {
                free(input_buffer);
                continue;
            }
            
            if (child_pid == 0) {
                // redirect standard output and input to /dev/null -> prevent output from cluttering terminal
                //int dev_null_fd = open("/dev/null", O_WRONLY);
                int dev_null_fd = open("/dev/null", O_RDWR);
                if (dev_null_fd != -1) {
                    dup2(dev_null_fd, STDOUT_FILENO);
                    dup2(dev_null_fd, STDIN_FILENO);
                    dup2(dev_null_fd, STDERR_FILENO);
                    close(dev_null_fd);
                }
                
                // close pipes that are no longer needed
                close(runner->input_pipe[0]);
                close(runner->status_pipe[1]);
                close(runner->shm_fd);
                
                //build argument vector with @@ replaced by input
                /*
                extern char **args;
                extern size_t program_argc;

                char **argv = malloc(sizeof(char*) * (program_argc + 1));
                if (argv == NULL) {
                    free(input_buffer);
                    exit(EXIT_FAILURE);
                }
                
                size_t argc = 0;
                for (size_t i = 0; i < program_argc; i++) {
                    if (strcmp(args[i], PROGRAM_ARGUMENT_PLACEHOLDER) == 0) {
                        // replace @@ with input string
                        argv[argc++] = input_buffer;
                    } else {
                        argv[argc++] = args[i];
                    }
                }
                argv[argc] = NULL;
                */

                extern char **args;
                extern size_t program_argc;
                
                // build argv
                int argc_count = 0;
                for (size_t i = 0; i < program_argc; i++) {
                    argc_count++;
                }
                
                char **argv = malloc(sizeof(char*) * (argc_count + 1));
                if (argv == NULL) {
                    free(input_buffer);
                    exit(EXIT_FAILURE);
                }
                
                size_t argc = 0;
                for (size_t i = 0; i < program_argc; i++) {
                    if (strcmp(args[i], PROGRAM_ARGUMENT_PLACEHOLDER) == 0) {
                        argv[argc++] = input_buffer;
                    } else {
                        argv[argc++] = args[i];
                    }
                }
                argv[argc] = NULL;
                
                // log before execution + execute
                fzl_runner_launch(runner->id, argv, NULL);
                execvp(argv[0], argv);
                
                // only if execution fails
                free(argv);
                free(input_buffer);
                exit(EXIT_FAILURE);
                
            } else {
                // for parent of target process
                free(input_buffer);

                // block signals before setting alarm
                sigprocmask(SIG_BLOCK, &mask, &oldmask);
        
                // set alarm to enforce timeout -> SIGALRM sgignal sent if run out of time
                extern int timeout;
                alarm(timeout);
                
                // wait for child to exit/timeout -> use sigsuspend()
                int child_status = 0;
                while (!sigchld_received && !sigalrm_received && !runner_terminate) {
                    sigsuspend(&oldmask);
                }
                // cancel alarm + restore signal mask
                alarm(0);
                sigprocmask(SIG_SETMASK, &oldmask, NULL);
                
                // timeout occurred -> kill target + send TIMEOUT status back to main fuzzer
                if (sigalrm_received) {
                    kill(child_pid, SIGKILL);
                    waitpid(child_pid, &child_status, 0);
                    
                    runner_alert_fuzzer(runner, TIMEOUT, 0);
                
                // child terminates -> use wait to reap child, cancel alarm + send signal to main
                } else if (sigchld_received) {
                    waitpid(child_pid, &child_status, 0);
                    alarm(0);
                    
                    // if child terminated normally, it is valid so get exit code
                    if (WIFEXITED(child_status)) {
                        runner_alert_fuzzer(runner, VALID, WEXITSTATUS(child_status));
                    // if was killed by signal, get the signal number -> CRASH
                    } else if (WIFSIGNALED(child_status)) {
                        runner_alert_fuzzer(runner, CRASH, WTERMSIG(child_status));
                    }
                }
            }
        }
        
        // log exit process
        fzl_runner_fini(runner->id, NULL);
        exit(EXIT_SUCCESS);
        
    } else {
        // cleanup for parent process -> store pid and close pipes
        runner->runner_pid = pid;
        
        // close pipes not used by main
        close(runner->input_pipe[0]);  
        close(runner->status_pipe[1]); 
        
        return 0;
    }
}


// implement runnerS struct
struct runners {
    RUNNER *ready_queue;
    int ready_count;
    RUNNER *active_queue;
    int active_count;
    RUNNER *done_queue;
    int done_count;
    int capacity;
};

RUNNERS runners_init(int job_count) {
    // allocate memory for runners struct
    RUNNERS runners = malloc(sizeof(struct runners));
    if(runners == NULL) {
        return NULL;
    }

    // initialize variable
    runners->capacity = job_count;
    runners->ready_count = 0;
    runners->active_count = 0;
    runners->done_count = 0;

    // allocate memory for each queue + check for memory leak for each
    runners->ready_queue = malloc(sizeof(RUNNER) * job_count);
    if(runners->ready_queue == NULL) {
        free(runners->ready_queue);
        free(runners);
        return NULL;
    }

    runners->active_queue = malloc(sizeof(RUNNER) * job_count);
    if(runners->active_queue == NULL) {
        free(runners->ready_queue);
        free(runners->active_queue);
        free(runners);
        return NULL;
    }

    runners->done_queue = malloc(sizeof(RUNNER) * job_count);
    if(runners->done_queue == NULL) {
        free(runners->ready_queue);
        free(runners->active_queue);
        free(runners->done_queue);
        free(runners);
        return NULL;
    }

    // create and launch every runner based on job_count given
    for(int i=0; i<job_count; i++) {
        // initialize runner + cleanup if error occurs
        RUNNER runner = runner_init();
        if(runner == NULL) {
            for(int j=0; j<i; j++) {
                runner_fini(runners->ready_queue[j]);
            }

            free(runners->ready_queue);
            free(runners->active_queue);
            free(runners->done_queue);
            free(runners);
            return NULL;
        }

        // launch runner + error handle for it
        if(runner_launch(runner) == -1) {
            runner_fini(runner);
            for(int j=0; j<i; j++) {
                runner_fini(runners->ready_queue[j]);
            }
            free(runners->ready_queue);
            free(runners->active_queue);
            free(runners->done_queue);
            free(runners);
            return NULL;
        }

        // add to successful created + launch runner to ready queue
        runners->ready_queue[runners->ready_count++] = runner;
    }

    return runners;
}

void runners_fini(RUNNERS runners) {
    // invalid
    if(runners == NULL) {
        return;
    }

    // iterate through each queue to free each runner 
    for(int i=0; i<runners->ready_count; i++) {
        runner_fini(runners->ready_queue[i]);
    }
    for(int i=0; i<runners->active_count; i++) {
        runner_fini(runners->active_queue[i]);
    }
    for(int i=0; i<runners->done_count; i++) {
        runner_fini(runners->done_queue[i]);
    }

    // free all queues and runners
    free(runners->ready_queue);
    free(runners->active_queue);
    free(runners->done_queue);
    free(runners);
}

int runners_submit_input(RUNNERS runners, INPUT input) {
    // check for invalid case
    if(runners == NULL || input == NULL || runners->ready_count == 0) {
        return -1;
    }

    // select first runner from ready queue + dequeue it
    RUNNER runner = runners->ready_queue[0];
    memmove(&runners->ready_queue[0], &runners->ready_queue[1], sizeof(RUNNER)*(runners->ready_count - 1));
    runners->ready_count--;

    // send input to the runner
    if(fuzzer_send_runner_input(runner, input) == -1) {
        return -1;
    }

    // move runner to active queue
    runners->active_queue[runners->active_count++] = runner;
    return 0;
}

int runners_has_jobs(RUNNERS runners) {
    if(runners == NULL) {
        return 0;
    }
    // checks if there are any runners in any queues
    return runners->ready_count > 0 || runners->active_count > 0 || runners->done_count > 0;
}

int runners_has_active_jobs(RUNNERS runners) {
    if(runners == NULL) {
        return 0;
    }
    // checks if there are any runners in active queue
    return runners->active_count > 0;
}

int runners_has_done_jobs(RUNNERS runners) {
    if(runners == NULL) {
        return 0;
    }
    // checks if there are any runners in done queue
    return runners->done_count > 0;
}

int runners_has_ready_jobs(RUNNERS runners) {
    if(runners == NULL) {
        return 0;
    }
    // checks if there are any runners in ready queue
    return runners->ready_count > 0;
}

void runners_check_if_jobs_done(RUNNERS runners) {
    if(runners == NULL) {
        return;
    }

    // check each runner in active queue to see if it has send data in the pipe
    //      -only be called if the main process has received a SIGUSR1
    // any active runners which have written in its pipe for the main process to read is:
    //      removed from the active queue and added to the ready queue 
    int i =0;
    while(i < runners->active_count) {
        RUNNER runner = runners->active_queue[i];
        RUNNER_STATE state = fuzzer_attempt_receive_status(runner, NULL);

        // if runner is complete, move to ready queue -> should this not move to done queue?
        if(state != NO_STATE) {
            memmove(&runners->active_queue[i], &runners->active_queue[i+1], sizeof(RUNNER)*(runners->active_count-i-1));
            runners->active_count--;
            runners->done_queue[runners->done_count++] = runner;
        } else{
            i++;
        }
    }
}

RUNNER runners_process_result(RUNNERS runners, RUNNER_STATE *state, int *data) {
    // invalid
    if(runners == NULL || runners->done_count == 0) {
        return NULL;
    }

    // selects a runner in the done queue and remove it from done queue
    // returns any information the exit status of the target program given the input assigned to the runner
    RUNNER runner = runners->done_queue[0];
    memmove(&runners->done_queue[0], &runners->done_queue[1], sizeof(RUNNER)*(runners->done_count-1));
    runners->done_count--;

    // get information on exit status + add it to the ready queue.
    *state = fuzzer_attempt_receive_status(runner, data);
    runners->ready_queue[runners->ready_count++] = runner;

    return runner;
}

int runners_reap(RUNNERS runners) {
    // invalid
    if(runners == NULL) {
        return -1;
    }

    // attempt to reap any terminated runner processes and remove said runner from the queue it currently is in
    //      Then, the function will call runner_fini on the runner
    int reaped = 0;
    pid_t pid;
    // if there is dead children, search for dead runner in each queue
    // if none, returns immediately
    while((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        int found = 0;

        // check ready queue
        for(int i=0; i<runners->ready_count; i++) {
            if(runners->ready_queue[i]->runner_pid == pid) {
                RUNNER runner = runners->ready_queue[i];
                memmove(&runners->ready_queue[i], &runners->ready_queue[i+1], sizeof(RUNNER)*(runners->ready_count-i-1));
                runners->ready_count--;
                
                runner_fini(runner);
                found = 1;
                reaped++;
                break;
            }
        }

        if(found) {
            continue;
        }

        // check active queue
        for(int i=0; i<runners->active_count; i++) {
            if(runners->active_queue[i]->runner_pid == pid) {
                RUNNER runner = runners->active_queue[i];
                memmove(&runners->active_queue[i], &runners->active_queue[i+1], sizeof(RUNNER)*(runners->active_count-i-1));
                runners->active_count--;
                
                runner_fini(runner);
                found = 1;
                reaped++;
                break;
            }
        }

        if(found) {
            continue;
        }

        // check done queue
        for(int i=0; i<runners->done_count; i++) {
            if(runners->done_queue[i]->runner_pid == pid) {
                RUNNER runner = runners->done_queue[i];
                memmove(&runners->done_queue[i], &runners->done_queue[i+1], sizeof(RUNNER)*(runners->done_count-i-1));
                runners->done_count--;
                
                runner_fini(runner);
                reaped++;
                break;
            }
        }
    }

    return reaped;
}
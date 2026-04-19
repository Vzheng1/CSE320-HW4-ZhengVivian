#include "input.h"
#include "input_queue.h"

// implement queue_node, queue, input_queue structs
typedef struct queue_node {
    INPUT input;
    struct queue_node *next;
} QUEUE_NODE;

typedef struct {
    QUEUE_NODE *head;
    QUEUE_NODE *tail;

} QUEUE;

// input_queue is composed of 2 queues -> high, low priority + counter to keep track of dequeue pattern
struct input_queue {
    QUEUE high_priority;
    QUEUE low_priority;
    int dequeue_counter;
};

INPUT_QUEUE input_queue_init() {
    // allocate memory of input queue component + check for error
    INPUT_QUEUE queue = malloc(sizeof(struct input_queue));
    if(queue == NULL) {
        return NULL;
    }

    // initialize high priority queue
    queue->high_priority.head = NULL;
    queue->high_priority.tail = NULL;

    // initialize low priority queue
    queue->low_priority.head = NULL;
    queue->low_priority.tail = NULL;

    // initialize counter
    queue->dequeue_counter = 0;
    
    // return after initialization is complete
    return queue;
}

void input_queue_fini(INPUT_QUEUE queue) {
    // if invalid queue, nothing to free -> return
    if(queue == NULL) {
        return;
    }

    // free high priority queue -> include all nodes + their inputs
    QUEUE_NODE *h_node = queue->high_priority.head;
    while(h_node != NULL) {
        QUEUE_NODE *next = h_node->next;
        free_input(h_node->input);
        free(h_node);
        h_node = next;
    }

    // free low priority queue -> include all nodes + their inputs
    QUEUE_NODE *l_node = queue->low_priority.head;
    while(l_node != NULL) {
        QUEUE_NODE *next = l_node->next;
        free_input(l_node->input);
        free(l_node);
        l_node = next;
    }

    // free allocated memory input_queue 
    free(queue);
}

void enqueue_high_prio_input(INPUT_QUEUE queue, INPUT input) {
    if(queue == NULL || input == NULL) {
        return;
    }

    // bitmap determines if the coverage-feedback data obtained by some input has caused the program to take a new control-flow edge that has never been observed prior
    // If so, the input is considered a high priority input.
}

void enqueue_low_prio_input(INPUT_QUEUE queue, INPUT input) {
    (void) queue;
    (void) input;

    // hashset determines if the coverage-feedback data represents a new path that has not been observed before. 
    // If so, then the input is considered a low priority input.

    // no priority = discard
}

INPUT dequeue_input(INPUT_QUEUE queue) {
    // invalid input queue
    if(queue == NULL) {
        return NULL;
    }

    INPUT dequeue_input = NULL;

    // 9 high priority dequeues then 1 low priority queue -> repeat pattern
    if(queue->dequeue_counter % 10 != 0) {
        // check for if queue head is available
        if(queue->high_priority.head != NULL) {
            // start from head of queue + save the head node's input to dequeue/return at end
            QUEUE_NODE *current = queue->high_priority.head;
            dequeue_input = current->input;

            // move to next -> if at end queue, set tail to NULL also
            queue->high_priority.head = current->next;
            if(queue->high_priority.head == NULL) {
                queue->high_priority.tail = NULL;
            }

            // free the dequeued node, increment dequeue counter to continue pattern
            free(current);
            queue->dequeue_counter++;
        }
    } else {
        if(queue->low_priority.head != NULL) {
            QUEUE_NODE *current = queue->low_priority.head;
            dequeue_input = current->input;

            queue->low_priority.head = current->next;
            if(queue->low_priority.head == NULL) {
                queue->low_priority.tail = NULL;
            }
            free(current);
            queue->dequeue_counter++;
        }
    }

    // requeue the dequeued the input -> should differentiate between high/low queue?
    if(dequeue_input != NULL) {
        enqueue_high_prio_input(queue, dequeue_input);
    }

    // return reference to the input
    return dequeue_input;
}

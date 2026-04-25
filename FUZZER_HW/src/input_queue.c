#include <stdlib.h>
#include <string.h>
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

static int input_in_queue(QUEUE *queue, INPUT input) {
    if(queue == NULL || input == NULL) {
        return 0;
    }

    const char *str = input_str(input);
    size_t len = input_len(input);

    QUEUE_NODE *current = queue->head;
    while(current != NULL) {
        QUEUE_NODE *next = current->next;

        // iterate through queue, if already in queue -> return 1 
        if(input_len(current->input) == len && strcmp(input_str(current->input), str) == 0) {
            return 1;
        }
        
        current = next;
    }

    // if not in queue, return 0
    return 0;
}

void enqueue_high_prio_input(INPUT_QUEUE queue, INPUT input) {
    // invalid 
    if(queue == NULL || input == NULL) {
        return;
    }

    // if input is already in a queue, return
    if(input_in_queue(&queue->high_priority, input) || input_in_queue(&queue->low_priority, input)) {
        return;
    }

    // else, create new node to add to high priority queue
    QUEUE_NODE *node = malloc(sizeof(QUEUE_NODE));
    if(node == NULL) {
        return;
    }
    node->input = input;
    node->next = NULL;

    // if nothing in high priority, node becomes head
    if(queue->high_priority.head == NULL) {
        queue->high_priority.head = node;
    // else, add to end of queue so link to current tail then make it the new tail
    } else {
        queue->high_priority.tail->next = node;
    }
    queue->high_priority.tail = node;
}

void enqueue_low_prio_input(INPUT_QUEUE queue, INPUT input) {
    // invalid 
    if(queue == NULL || input == NULL) {
        return;
    }

    // if input is already in a queue, return
    if(input_in_queue(&queue->high_priority, input) || input_in_queue(&queue->low_priority, input)) {
        return;
    }

    // else, create new node to add to low priority queue
    QUEUE_NODE *node = malloc(sizeof(QUEUE_NODE));
    if(node == NULL) {
        return;
    }
    node->input = input;
    node->next = NULL;

    // if nothing in low priority, node becomes head
    if(queue->low_priority.head == NULL) {
        queue->low_priority.head = node;
    // else, add to end of queue so link to current tail then make it the new tail
    } else {
        queue->low_priority.tail->next = node;
    }
    queue->low_priority.tail = node;
}

INPUT dequeue_input(INPUT_QUEUE queue) {
    // invalid input queue
    if(queue == NULL) {
        return NULL;
    }

    INPUT dequeue_input = NULL;
    int from_high_queue = 0;

    // dequeue pattern -> 9 hihg priority then 1 low priority -> repeat
    int dequeue_pattern = (queue->dequeue_counter < 9);

    // get references to two queueus based on pattern
    QUEUE *first = dequeue_pattern ? &queue->high_priority : &queue->low_priority;
    QUEUE *second = dequeue_pattern ? &queue->low_priority : &queue->high_priority;

    // try deqeue from first queue -> if empty, try second queue -> if both empty, return NULL
    QUEUE *selected = NULL;
    if(first->head != NULL) {
        selected = first;
    } else if(second->head != NULL) {
        selected = second;
    } else {
        return NULL;
    }

    // dequeue from selected queue -> save input to return + update head/tail + free the freed node
    QUEUE_NODE *current = selected->head;
    dequeue_input = current->input;
    selected->head = current->next;
    if(selected->head == NULL) {
        selected->tail = NULL;
    }
    free(current);

    // check if input is from high priority queue so know where to enqueue back into
    from_high_queue = (selected == &queue->high_priority);

    // increment counter after dequeue succeeds
    queue->dequeue_counter = (queue->dequeue_counter + 1) % 10;

    // enqueue the dequeue_input back into correct queue based on where it came from
    if(from_high_queue) {
        enqueue_high_prio_input(queue, dequeue_input);
    } else {
        enqueue_low_prio_input(queue, dequeue_input);
    }

    // return reference to input
    return dequeue_input;
}

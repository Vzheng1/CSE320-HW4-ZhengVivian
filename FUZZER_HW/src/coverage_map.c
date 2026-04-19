#include <stdlib.h>
#include <string.h>
#include "coverage_map.h"
#include "global.h"

// implement nodes for hashset
typedef struct hash_node{
    char *data;
    size_t len;
    struct hash_node *next;
} HASH_NODE;

// implement coverage map struct containing bitmap and hashset + their sizes
struct coverage_map {
    unsigned char *bitmap;
    size_t bitmap_bytes;
    HASH_NODE **hashset;
    size_t hash_size;
};

#define HASHSET_SIZE 1024

COVERAGE_MAP coverage_map_init() {
    // allocate memory for entire coverage_map component
    COVERAGE_MAP map = malloc(sizeof(struct coverage_map));
    if(map == NULL) {
        return NULL;
    }

    // initialize bitmap -> has size of COVERAGE_MAP_SIZE bits
    map->bitmap_bytes = (COVERAGE_MAP_SIZE + 7)/8;
    map->bitmap = calloc(map->bitmap_bytes, 1);
    if(map->bitmap == NULL) {
        free(map);
        return NULL;
    } 

    // initialize hashset -> allocate memory for all nodes, size = max input length?
    map->hashset = calloc(HASHSET_SIZE, sizeof(HASH_NODE *));
    if(map->hashset == NULL) {
        free(map->bitmap);
        free(map);
        return NULL;
    }
    map->hash_size = HASHSET_SIZE;

    return map;
}

void coverage_map_fini(COVERAGE_MAP map) {
    // invalid input
    if(map == NULL) {
        return;
    }

    // iterate through entire hashset to free nodes
    for(size_t i = 0; i < map->hash_size; i++) {
        HASH_NODE *current = map->hashset[i];

        while(current != NULL) {
            HASH_NODE *next = current->next;
            free(current->data);
            free(current);
            current = next;
        }
    }

    // free hashset, bitmap and coverage_map
    free(map->hashset);
    free(map->bitmap);
    free(map);
}

static unsigned long hash_data(const char *data, size_t len) {
    unsigned long hash = 5381;

    for(size_t i=0; i<len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)data[i];
    }
    return hash % HASHSET_SIZE;
}

COVERAGE_PRIORITY coverage_map_add(COVERAGE_MAP map, char *cov_data) {
    // invalid inputs
    if(map == NULL || cov_data == NULL) {
        return COV_NO_PRIO;
    }

    // check for HIGH priority FIRST
    // bitmap ->each bit represents if its corresponding edge in the control flow graph (CFG) of the program has been reached by some program execution by the fuzzer
    //      bitmap's value = logical-or of all the coverage-feedback data obtained by prior target program executions
    // purpose -> determine if the coverage-feedback data obtained by some input has caused the program to take a new control-flow edge that has never been observed prior. 
    //      if new edge taken -> input = HIGH priority
    size_t cov_len = map->bitmap_bytes;
    unsigned char *cov_bytes = (unsigned char *)cov_data;

    int is_new_edge = 0;
    for(size_t i=0; i< cov_len; i++) {
        // for each byte in coverage data, calculate edges/bits that have NOT been taken before
        //      bytes ON in cov and OFF in bitmap = new edges 
        unsigned char new_bits = cov_bytes[i] & ~map->bitmap[i];

        // if new bits are found, update bitmap with logical OR with new cov_data
        if(new_bits != 0) {
            is_new_edge = 1;
            map->bitmap[i] |= cov_bytes[i];
        }
    }
    // if has taken new edge, it is high priority -> RETURN
    if(is_new_edge) {
        return COV_HIGH_PRIO;
    }

    // if NOT high priority, check for LOW priority using hashset
    // hashset holds coverage-feedback data.
    //      goal = determine if the coverage-feedback data represents a new path that has not been observed before. 
    //      -> new path observed = low priority input

    // calculate hash of coverage data
    unsigned long hash = hash_data(cov_data, cov_len);
    HASH_NODE *current = map->hashset[hash];

    // look for matching path in the that hash bucket -> not found = new path
    while(current != NULL) {
        // if matching path is found, this input is NO priority -> return
        if(current->len == cov_len && memcmp(current->data, cov_data, cov_len) == 0) {
            return COV_NO_PRIO;
        }
        current = current->next;
    }

    // else, not found then has new path so add new node to hashset and return low priority
    // if run into error in any case, return no priority (-1)
    HASH_NODE *new_node = malloc(sizeof(HASH_NODE));
    if(new_node == NULL) {
        return COV_NO_PRIO;
    }

    new_node->data = malloc(cov_len);
    if(new_node->data == NULL) {
        free(new_node);
        return COV_NO_PRIO;
    }

    memcpy(new_node->data, cov_data, cov_len);
    new_node->len = cov_len;
    new_node->next = map->hashset[hash];
    map->hashset[hash] = new_node;

    return COV_LOW_PRIO;
}

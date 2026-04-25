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

// helper method to mask for last byte of bitmap
// COVERAGE_MAP_SIZE is in bits BUT bitmap is byte addressed 
//  -> if bit count not divisible by 8, last byte has unused bits -> must be ignored
static unsigned char last_byte_mask(size_t bit_count) {
    size_t remainder = bit_count % 8;
    if(remainder == 0) {
        return 0xFF;
    }

    return (unsigned char)((1u << remainder) - 1u);
}

COVERAGE_PRIORITY coverage_map_add(COVERAGE_MAP map, char *cov_data) {
    // invalid inputs
    if(map == NULL || cov_data == NULL) {
        return COV_NO_PRIO;
    }

    size_t cov_len = map->bitmap_bytes;
    unsigned char *cov_bytes = (unsigned char *)cov_data;

    // calculate hash of coverage data + get hash bucket 
    // iterate through bucket to check if it exists in hashset -> if not, new path; else, nt new
    unsigned long hash = hash_data(cov_data, cov_len);
    HASH_NODE *current = map->hashset[hash];
    int is_new_path = 1;

    while(current != NULL) {
        if(current->len == cov_len && memcmp(current->data, cov_data, cov_len) == 0) {
            is_new_path = 0;
            break;
        }
        current = current->next;
    }

    int is_new_edge = 0;
    unsigned char tail_mask = last_byte_mask(COVERAGE_MAP_SIZE);
    for(size_t i=0; i< cov_len; i++) {
        unsigned char cov = cov_bytes[i];
        if(i == cov_len - 1) {
            // clear unused bits so padding bits do NOT count as new edges -> ignore them
            cov &= tail_mask;
        }

        // any bit set in the incoming coverage + NOT yet seen in bitmap = new bit 
        // if new bit exists -> new edfe -> HIGH priority
        unsigned char new_bits = cov & (unsigned char)~map->bitmap[i];

        if(new_bits != 0) {
            is_new_edge = 1;
        }

        // always add the coverage data into bitmap by doing logical OR to keep track
        map->bitmap[i] |= cov;
    }

    // if new path exists, add to hashset
    if(is_new_path) {
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
    }
    
    // return priority based on new edge/path
    // new edge -> HIGH priority, new path -> LOW priority
    // return NO priority if neither
    if(is_new_edge) {
        return COV_HIGH_PRIO;
    }
    if(is_new_path) {
        return COV_LOW_PRIO;
    }
    return COV_NO_PRIO;
}

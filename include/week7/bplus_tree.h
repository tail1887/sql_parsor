/* WEEK7: B+ tree / primary-key index (see docs/weeks/WEEK7/) */
#ifndef BPLUS_TREE_H
#define BPLUS_TREE_H

#include <stddef.h>
#include <stdint.h>

typedef struct BPlusTree BPlusTree;

BPlusTree *bplus_create(void);
void bplus_destroy(BPlusTree *t);

/* Insert key -> payload (0-based data row index). Duplicate key returns -1. OOM returns -2. */
int bplus_insert(BPlusTree *t, int64_t key, size_t payload);

/* 0 if found and *payload set; -1 if not found. */
int bplus_search(const BPlusTree *t, int64_t key, size_t *payload);

/* Load from CSV: same key updates payload (last row wins). */
int bplus_insert_or_replace(BPlusTree *t, int64_t key, size_t payload);

#endif

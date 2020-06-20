// Automatically generated header.

#pragma once
#include "threads.h"
#include <stdlib.h>
#include "vector.h"
typedef struct {
  vector_t mtxs;
} locktable_t;
locktable_t locktable_new(unsigned long size);
void locktable_lock(locktable_t* locktable, uint64_t id);
void locktable_unlock(locktable_t* locktable, uint64_t id);
void locktable_lock_key(locktable_t* locktable, char* key, size_t key_size);
void locktable_unlock_key(locktable_t* locktable, char* key, size_t key_size);
void locktable_free(locktable_t* locktable);

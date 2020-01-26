#include <stdlib.h>

#include "vector.h"
#include "siphash.h"
#include "threads.h"

typedef struct {
  vector_t mtxs;
} locktable_t;

locktable_t locktable_new(unsigned long size) {
  locktable_t locktable;
  locktable.mtxs = vector_new(sizeof(mtx_t));
  vector_init(&locktable.mtxs, size);
  
  vector_iterator mtx_iter = vector_iterate(&locktable.mtxs);
  while (vector_next(&mtx_iter)) {
    mtx_init(mtx_iter.x, mtx_plain);
  }

  return locktable;
}

void locktable_lock(locktable_t* locktable, char* key, size_t key_size) {
  mtx_t* mtx = vector_get(&locktable->mtxs, siphash24_keyed(key, key_size) % locktable->mtxs.length);
  mtx_lock(mtx);
}

void locktable_unlock(locktable_t* locktable, char* key, size_t key_size) {
  mtx_t* mtx = vector_get(&locktable->mtxs, siphash24_keyed(key, key_size) % locktable->mtxs.length);
  mtx_unlock(mtx);
}

void locktable_free(locktable_t* locktable) {
  vector_iterator mtx_iter = vector_iterate(&locktable->mtxs);
  while (vector_next(&mtx_iter)) {
    mtx_destroy(mtx_iter.x);
  }

  vector_free(&locktable->mtxs);
}

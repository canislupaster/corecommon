#include <stdlib.h>

#include "threads.h"

typedef struct {
  mtx_t prop; //lock on rwlock properties (ex. readers must wait for writer to see write)
  unsigned read; //amount of readers
  int write; //whether lock is being used for writing

  cnd_t cnd; //notifies when write is 0 or read is 0; notifications are mutually exclusive with prop: prop must be unlocked for a notification to occur
} rwlock_t;

rwlock_t rwlock_new() {
  rwlock_t rwlock;
  
  mtx_init(&rwlock.prop, mtx_plain);
  cnd_init(&rwlock.cnd);
  
  rwlock.read = 0;
  rwlock.write = 0;
  
  return rwlock;
}

void rwlock_read(rwlock_t* rwlock) {
  mtx_lock(&rwlock->prop);
  if (rwlock->write) {
    cnd_wait(&rwlock->cnd, &rwlock->prop);
  }

  rwlock->read++;
  mtx_unlock(&rwlock->prop);
}

void rwlock_write(rwlock_t* rwlock) {
  mtx_lock(&rwlock->prop);
  if (rwlock->read>0 || rwlock->write) {
    cnd_wait(&rwlock->cnd, &rwlock->prop);
  }

  rwlock->write=1;
  mtx_unlock(&rwlock->prop);
}

void rwlock_unread(rwlock_t* rwlock) {
  mtx_lock(&rwlock->prop);
  int notify = --rwlock->read == 0;
  mtx_unlock(&rwlock->prop);

  if (notify) cnd_broadcast(&rwlock->cnd);
}

void rwlock_unwrite(rwlock_t* rwlock) {
  mtx_lock(&rwlock->prop);
  rwlock->write=0;
  mtx_unlock(&rwlock->prop);

  cnd_broadcast(&rwlock->cnd);
}

void rwlock_free(rwlock_t* rwlock) {
  mtx_destroy(&rwlock->prop);
  cnd_destroy(&rwlock->cnd);
}
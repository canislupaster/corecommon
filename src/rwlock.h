// Automatically generated header.

#pragma once
#include <stdlib.h>
#include "threads.h"
typedef struct {
  mtx_t prop; //lock on rwlock properties (ex. readers must wait for writer to see write)
  unsigned read; //amount of readers
  int write; //whether lock is being used for writing

  cnd_t cnd; //notifies when write is 0 or read is 0; notifications are mutually exclusive with prop: prop must be unlocked for a notification to occur
} rwlock_t;
rwlock_t rwlock_new();
void rwlock_read(rwlock_t* rwlock);
void rwlock_write(rwlock_t* rwlock);
void rwlock_unread(rwlock_t* rwlock);
void rwlock_unwrite(rwlock_t* rwlock);

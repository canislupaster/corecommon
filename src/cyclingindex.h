// Automatically generated header.

#pragma once
#include "vector.h"
typedef struct {
	vector_t bounds;
	unsigned i;
	unsigned new_bound;
} cycling_index_t;
cycling_index_t cindex_new(unsigned l);
void cindex_cycle(cycling_index_t* cin);
unsigned cindex_next(cycling_index_t* cin, unsigned* i);
unsigned cindex_get(cycling_index_t* cin);
unsigned cindex_start(cycling_index_t* cin);

#include <stdio.h>
#include "vector.h"

typedef struct {
	vector_t bounds;
	unsigned i;
	unsigned new_bound;
} cycling_index_t;

cycling_index_t cindex_new(unsigned l) {
	cycling_index_t cin = {.bounds=vector_new(sizeof(unsigned)), .i=0, .new_bound=0};
	vector_populate(&cin.bounds, l, &(unsigned){0});
	return cin;
}

void cindex_cycle(cycling_index_t* cin) {
	*(unsigned*)vector_get(&cin->bounds, cin->i) = cin->new_bound;
	cin->i = (cin->i+1)%cin->bounds.length;
	if (cin->i==0) cin->new_bound=0;
}

unsigned cindex_next(cycling_index_t* cin, unsigned* i) {
	unsigned* prevlen = (unsigned*)vector_get(&cin->bounds, cin->i);
	if (*i==*prevlen && cin->i<cin->bounds.length-1) {
		*i = *(unsigned*)vector_get(&cin->bounds, cin->bounds.length-1);
	}

	return (*i)++;
}

unsigned cindex_get(cycling_index_t* cin) {
	return cindex_next(cin, &cin->new_bound);
}

unsigned cindex_start(cycling_index_t* cin) {
	return cin->i==0?0:*(unsigned*)vector_get(&cin->bounds, cin->i-1);
}

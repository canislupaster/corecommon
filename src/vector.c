#include <stdlib.h>
#include "util.h"

typedef struct {
	unsigned long size;

	unsigned long length;
	char* data;
} vector_t;

typedef struct {
	vector_t* vec;

	unsigned long i;
	char rev;
	void* x;
} vector_iterator;

vector_t vector_new(unsigned long size) {
	vector_t vec = {size, .length=0, .data=NULL};

	return vec;
}

void vector_init(vector_t* vec, unsigned long length) {
	vec->data = malloc(vec->size * length);
	vec->length = length;
}

/// returns ptr to insertion point
void* vector_push(vector_t* vec) {
	vec->length++;

	//allocate or resize
	if (!vec->data)
		vec->data = heap(vec->size);
	else
		vec->data = resize(vec->data, vec->size * vec->length);

	return vec->data + (vec->length - 1) * vec->size;
}

void* vector_pushcpy(vector_t* vec, void* x) {
	void* pos = vector_push(vec);
	memcpy(pos, x, vec->size);
	return pos;
}

int vector_pop(vector_t* vec) {
	if (vec->length == 0)
		return 0;

	vec->length--;
	vec->data = resize(vec->data, vec->size * vec->length);

	return 1;
}

void* vector_popcpy(vector_t* vec) {
	if (vec->length == 0)
		return NULL;

	void* x = heapcpy(vec->size, vec->data + vec->size * vec->length - 1);

	vec->length--;
	vec->data = resize(vec->data, vec->size * vec->length);

	return x;
}

/// returns 1 if removed successfully
int vector_remove(vector_t* vec, unsigned long i) {
	if (i == vec->length - 1) {
		vec->length--;

		vec->data = resize(vec->data, vec->size * vec->length);
	}

	//sanity checks
	if (!vec->data || i >= vec->length)
		return 0;

	memcpy(vec->data + i * vec->size,
				 vec->data + (i + 1) * vec->size,
				 (vec->length - 1 - i) * vec->size);
	return 1;
}

void* vector_get(vector_t* vec, unsigned long i) {
	if (i >= vec->length) {
		return NULL;
	}

	return vec->data + i * vec->size;
}

void* vector_insert(vector_t* vec, unsigned long i) {
	vec->length++;

	if (!vec->data)
		vec->data = heap(vec->size);
	else
		vec->data = resize(vec->data, vec->size * vec->length);

	if (vec->length > i)
		memcpy(vec->data + vec->size * (i + 1),
					 vec->data + vec->size * i,
					 (vec->length - i) * vec->size);

	return vec->data + vec->size * i;
}

void* vector_insertcpy(vector_t* vec, unsigned long i, void* x) {
	void* pos = vector_insert(vec, i);
	memcpy(pos, x, vec->size);

	return pos;
}

void* vector_set(vector_t* vec, unsigned long i) {
	if (i >= vec->length) {
		vec->length = i + 1;
		vec->data = resize(vec->data, vec->size * vec->length);
	}

	return vec->data + i * vec->size;
}

void* vector_setcpy(vector_t* vec, unsigned long i, void* x) {
	void* pos = vector_set(vec, i);
	memcpy(pos, x, vec->size);

	return pos;
}

vector_iterator vector_iterate(vector_t* vec) {
	vector_iterator iter = {
			vec, .i=0, .rev=0
	};

	return iter;
}

int vector_next(vector_iterator* iter) {
	iter->x = iter->vec->data + (iter->rev ? iter->vec->length - 1 - iter->i : iter->i) * iter->vec->size;
	iter->i++;

	if (iter->i > iter->vec->length)
		return 0;
	else
		return 1;
}

void vector_cpy(vector_t* from, vector_t* to) {
	*to = *from;

	to->data = heapcpy(from->size * from->length, from->data);
}

void vector_clear(vector_t* vec) {
	if (vec->data) {
		drop(vec->data);
		vec->data = NULL;
	}

	vec->length = 0;
}

void vector_free(vector_t* vec) {
	if (vec->data)
		drop(vec->data);
}
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "util.h"

typedef struct {
	unsigned long size;

	unsigned long length;
	char* data;
} vector_t;

typedef struct {
	vector_t* vec;

	long i;
	char rev;
	void* x;
} vector_iterator;

vector_t vector_new(unsigned long size) {
	vector_t vec = {size, .length=0, .data=NULL};

	return vec;
}

//requires heap allocated str
vector_t vector_from_string(char* str) {
	return (vector_t){.size=1, .length=strlen(str)+1, .data=str};
}

void vector_downsize(vector_t* vec) {
	if (vec->length == 0) {
		drop(vec->data);
		vec->data = NULL;
	} else {
		vec->data = resize(vec->data, vec->length * vec->size);
	}
}

void vector_upsize(vector_t* vec, unsigned long length) {
	if (vec->length == 0)
		vec->data = heap(length * vec->size);
	else
		vec->data = resize(vec->data, vec->size*(vec->length+length));
	
	vec->length += length;
}

/// returns ptr to insertion point
void* vector_push(vector_t* vec) {
	//allocate or resize
	vector_upsize(vec, 1);

	return vec->data + (vec->length - 1) * vec->size;
}

void* vector_pushcpy(vector_t* vec, void* x) {
	void* pos = vector_push(vec);
	memcpy(pos, x, vec->size);
	return pos;
}

void* vector_stock(vector_t* vec, unsigned long length) {
	vector_upsize(vec, length);

	return vec->data + (vec->length-length)*vec->size;
}

void* vector_stockcpy(vector_t* vec, unsigned long length, void* data) {
	void* pos = vector_stock(vec, length);
	memcpy(pos, data, length*vec->size);
	return pos;
}

void* vector_stockstr(vector_t* vec, char* str) {
  return vector_stockcpy(vec, strlen(str), str);
}

unsigned long vector_elem(vector_t* vec, char* ptr) {
  return (ptr - vec->data)/vec->size;
}

void* vector_get(vector_t* vec, unsigned long i) {
	if (i >= vec->length) {
		return NULL;
	}

	return vec->data + i * vec->size;
}

char* vector_getstr(vector_t* vec, unsigned long i) {
	char** x = vector_get(vec, i);
  return x ? *x : NULL;
}

void vector_truncate(vector_t* vec, unsigned long length) {
	if (length == 0) {
		drop(vec->data);
	} else if (vec->length > length) {
		vec->data = resize(vec->data, vec->size*length);
	}
	
	vec->length = length;
}

int vector_pop(vector_t* vec) {
	if (vec->length == 0)
		return 0;

	vec->length--;
	vector_downsize(vec);

	return 1;
}

char* vector_popptr(vector_t* vec) {
	if (vec->length == 0)
		return NULL;

  char* ptr = *(char**)(vec->data + vec->size*(vec->length-1));

	vec->length--;
	vector_downsize(vec);

	return ptr;
}

void* vector_popcpy(vector_t* vec) {
	if (vec->length == 0)
		return NULL;

	void* x = heapcpy(vec->size, vec->data + vec->size * (vec->length - 1));

	vec->length--;
	vector_downsize(vec);

	return x;
}

/// returns 1 if removed successfully
int vector_remove(vector_t* vec, unsigned long i) {
	//sanity checks
	if (i >= vec->length)
		return 0;

	vec->length--;

	memcpy(vec->data + i * vec->size,
				 vec->data + (i + 1) * vec->size,
				 (vec->length - i) * vec->size);

	vector_downsize(vec);
	return 1;
}

char* vector_removeptr(vector_t* vec, unsigned long i) {
	if (i >= vec->length)
		return NULL;

  char* ptr = *(char**)(vec->data + vec->size*i);

	vec->length--;

	memcpy(vec->data + i * vec->size,
				 vec->data + (i + 1) * vec->size,
				 (vec->length - i) * vec->size);

	vector_downsize(vec);
	return ptr;
}

int vector_removemany(vector_t* vec, unsigned long i, unsigned long len) {
	if (i+len > vec->length)
		return 0;

	vec->length -= len;

	memcpy(vec->data + i * vec->size,
				 vec->data + (i + len) * vec->size,
				 (vec->length - i) * vec->size);

	vector_downsize(vec);
	return 1;
}

int vector_remove_element(vector_t* vec, char* x) {
	if (x >= vec->size * vec->length + vec->data)
		return 0;

	vec->length--;

	memcpy(x,
				 x + vec->size,
				 (vec->data + vec->length * vec->size) - x);

	vector_downsize(vec);
	return 1;
}

//inserts element into index i
void* vector_insert(vector_t* vec, unsigned long i) {
	vector_upsize(vec, 1);

	if (vec->length > i)
		memcpy(vec->data + vec->size * (i + 1),
					 vec->data + vec->size * i,
					 (vec->length-1 - i) * vec->size);

	return vec->data + vec->size * i;
}

void* vector_insertcpy(vector_t* vec, unsigned long i, void* x) {
	void* pos = vector_insert(vec, i);
	memcpy(pos, x, vec->size);

	return pos;
}

void* vector_insert_many(vector_t* vec, unsigned long i, unsigned long length) {
	vector_upsize(vec, length);

	if (vec->length-length >= i)
		memcpy(vec->data + vec->size * (i + length),
					 vec->data + vec->size * i,
					 (vec->length-length - i) * vec->size);

	return vec->data + vec->size * i;
}

void* vector_insert_manycpy(vector_t* vec, unsigned long i, unsigned long length, void* x) {
	void* pos = vector_insert_many(vec, i, length);
	memcpy(pos, x, vec->size*length);

	return pos;
}

void* vector_insertstr(vector_t* vec, unsigned long i, char* str) {
  return vector_insert_manycpy(vec, i, strlen(str), str);
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

vector_iterator vector_iterate_rev(vector_t* vec) {
	return (vector_iterator){vec, .i=vec->length-1, .rev=1};
}

int vector_next(vector_iterator* iter) {
	iter->x = iter->vec->data + iter->i * iter->vec->size;
	if (iter->rev) iter->i--; else iter->i++;

	if ((iter->rev && iter->i < -1)
		|| (!iter->rev && iter->i > iter->vec->length))

		return 0;
	else
		return 1;
}

//same thing without increment
void* vector_peek(vector_iterator* iter) {
	if (iter->i > iter->vec->length) {
		return 0;
	} else {
		return iter->vec->data + (iter->rev ? iter->vec->length - 1 - iter->i : iter->i) * iter->vec->size;
	}
}

void vector_cpy(vector_t* from, vector_t* to) {
	*to = *from;

	to->data = heapcpy(from->size * from->length, from->data);
}

//why
//    - author
void vector_add(vector_t* from, vector_t* to) {
	vector_stockcpy(to, from->length, from->data);
}

unsigned long vector_search(vector_t* vec, void* elem) {
	vector_iterator iter = vector_iterate(vec);
	while (vector_next(&iter)) {
		if (memcmp(iter.x, elem, vec->size)==0)
			return iter.i;
	}

	return 0;
}

unsigned long vector_cmp(vector_t* vec1, vector_t* vec2) {
  if (vec1->length != vec2->length) return vec1->length;

  vector_iterator iter = vector_iterate(vec1);
  while (vector_next(&iter)) {
    char* elem2 = vector_get(vec2, iter.i-1);
    if (memcmp(iter.x, elem2, vec1->size)!=0) return iter.i;
  }

  return 0;
}

unsigned long vector_cmpstr(vector_t* vec1, vector_t* vec2) {
  if (vec1->length != vec2->length) return vec1->length;

  vector_iterator iter = vector_iterate(vec1);
  while (vector_next(&iter)) {
    char* elem1 = *(char**)iter.x;
    char* elem2 = vector_getstr(vec2, iter.i-1);
    if (strcmp(elem1, elem2)!=0) return iter.i;
  }

  return 0;
}

//shell-selection sort
//max size 64
void vector_sort_inplace(vector_t* vec, size_t offset, size_t size) {
	vector_iterator iter = vector_iterate(vec);

	uint64_t x=0;
	uint64_t next=0;
	char temp[vec->size];

	unsigned long prev_pos;

	while (vector_next(&iter)) {
		memcpy(&x, (char*)iter.x+offset, size);
		if (!iter.rev) memcpy(&next, (char*)iter.x+vec->size+offset, size);

		if (x>next) {
			iter.rev=1;
			prev_pos = iter.i;

			iter.i -= 2;

			continue;
		} else if (iter.rev) {
			//insert next before x
			memcpy((char*)iter.x+vec->size, (char*)iter.x, vector_get(vec, prev_pos-1)-iter.x);
			memcpy(iter.x, temp, vec->size);

			iter.i = prev_pos;
			iter.rev=0;
		}
	}
}

//mutates string and does not allocate (except for vector)
vector_t vector_split_str(char* str, const char* delim) {
	vector_t vec = vector_new(sizeof(char*));
	
  char* split;
	while ((split=strsep(&str, delim)) && split) {
    vector_pushcpy(&vec, &split);
  }

	return vec;
}

vector_t vector_from_strings(char* start, unsigned long num) {
	vector_t vec = vector_new(sizeof(char*));
	char** data = vector_stock(&vec, num);
	
	for (unsigned long i=0; i<num; i++) {
		data[i] = start;
		start += strlen(start)+1;
	}

	return vec;
}

void vector_flatten_strings(vector_t* vec, vector_t* out, char* delim, unsigned long len) {
	vector_iterator iter = vector_iterate(vec);
	while (vector_next(&iter)) {
		char* s = *(char**)iter.x;

		vector_stockstr(out, s);

		if (delim && iter.i < vec->length) {
			vector_stockcpy(out, len, delim);
		}
	}
}

void vector_clear(vector_t* vec) {
	if (vec->length>0)
		drop(vec->data);

	vec->length = 0;
}

void vector_free(vector_t* vec) {
	if (vec->length>0)
		drop(vec->data);
}

void vector_expand_strings(vector_t* vec, vector_t* out, char* begin, char* delim, char* end) {
  char** start = vector_stock(out, vec->length);

  vector_t str = vector_new(1);
  vector_stockcpy(&str, strlen(begin), begin);
  
	unsigned long end_len = strlen(end)+1;
	unsigned long delim_len = strlen(delim);

  vector_iterator iter = vector_iterate(vec);
  while (vector_next(&iter)) {
    char* segment = *(char**)iter.x;
		vector_stockcpy(&str, strlen(segment), segment);
		vector_stockcpy(&str, delim_len, delim);

    vector_stockcpy(&str, end_len, end);

    start[iter.i-1] = heapcpystr(str.data);
    vector_truncate(&str, str.length-end_len);
  }

	vector_free(&str);
}

void vector_free_strings(vector_t* vec) {
	vector_iterator iter = vector_iterate(vec);
	while (vector_next(&iter)) {
		drop(*(char**)iter.x);
	}

	if (vec->length>0)
		drop(vec->data);
}

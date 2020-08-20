// Automatically generated header.

#pragma once
#include <err.h>
#include <stdint.h>	 //ints for compatibility, since we are writing to files
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "siphash.h"
#include "threads.h"
#include "util.h"
#include "vector.h"
typedef struct {
	mtx_t lock;
	FILE* file;

	uint64_t length;
} filemap_list_t;
typedef struct {
	FILE* data;

	mtx_t lock;	 // lock for file io

	filemap_list_t* alias;	// optional list layer to provide index consistency

	uint64_t free;

	//total size for bounds checking
	//because some reads are inconsistent to save locks
	// ;-)
	uint64_t size;

	unsigned fields;
} filemap_t;
typedef struct {
	filemap_t* data;

	mtx_t lock;
	FILE* file;

	uint32_t hash_seed[4];

	uint64_t length;

	uint64_t slots;
	// slots being resized out of all slots
	// theoretically this could be packed into slots
	// and slots will be rounded down to the nearest power of two
	// so bits switched beforehand would be the resize_slots
	uint64_t resize_slots;
	// extra slots for probing, only used for lookup until resize finishes
	uint64_t resize_lookup_slots;

	unsigned field;
} filemap_index_t;
typedef struct {
	mtx_t lock;
	FILE* file;

	uint64_t first;
	uint64_t min;
	uint64_t max;
	uint64_t pages;

	uint64_t page_size;
} filemap_ordered_list_t;
typedef struct {
	uint64_t index;
	uint64_t data_pos;

	char exists;
} filemap_partial_object;
typedef struct {
	filemap_partial_object partial;
	uint64_t item_order;
	uint64_t page;
} filemap_ord_partial_object;
typedef struct {
	uint64_t data_pos;
	uint64_t data_size;

	char** fields;
	uint64_t* lengths;

	char exists;
} filemap_object;
typedef struct {
	FILE* file;
	mtx_t* lock;

	uint64_t pos;	 // file position

	filemap_partial_object obj;
} filemap_iterator;
filemap_t filemap_new(char* data, unsigned fields, int overwrite);
filemap_index_t filemap_index_new(filemap_t* fmap, char* index, unsigned field, int overwrite);
filemap_list_t filemap_list_new(char* list, int overwrite);
filemap_partial_object filemap_partialize(filemap_partial_object* partial, filemap_object* obj);
filemap_object filemap_index_obj(filemap_object* obj,
																 filemap_partial_object* partial);
filemap_partial_object filemap_deref(filemap_list_t* list,
																		 filemap_partial_object* partial);
filemap_ordered_list_t filemap_ordered_list_new(char* order_list, int page_size, int overwrite);
filemap_partial_object filemap_find(filemap_index_t* index, char* key, uint64_t key_size);
filemap_object filemap_cpy(filemap_t* filemap, filemap_partial_object* res);
filemap_object filemap_cpyref(filemap_t* filemap, filemap_partial_object* obj);
typedef struct {
	char exists;
	vector_t val;
} filemap_field;
filemap_field filemap_cpyfield(filemap_t* filemap,
															 filemap_partial_object* partial,
															 unsigned field);
filemap_field filemap_cpyfieldref(filemap_t* filemap, filemap_partial_object* partial, unsigned field);
filemap_partial_object filemap_insert(filemap_index_t* index,
																			filemap_object* obj);
int filemap_remove(filemap_index_t* index, char* key, uint64_t key_size);
filemap_partial_object filemap_add(filemap_list_t* list, filemap_object* obj);
filemap_ord_partial_object filemap_ordered_insert(filemap_ordered_list_t* list,
																									uint64_t item_order,
																									filemap_object* obj);
int filemap_ordered_remove_id(filemap_ordered_list_t* list, uint64_t item_order, filemap_partial_object* obj);
filemap_partial_object filemap_get_idx(filemap_list_t* list, uint64_t i);
void filemap_list_remove(filemap_list_t* list, filemap_partial_object* obj);
void filemap_list_update(filemap_list_t* list, filemap_partial_object* partial, filemap_object* obj);
filemap_iterator filemap_list_iterate(filemap_list_t* list);
uint64_t filemap_list_pos(uint64_t idx);
uint64_t filemap_list_idx(uint64_t pos);
int filemap_next(filemap_iterator* iter);
vector_t filemap_readmany(filemap_iterator* iter, int* more, unsigned max);
filemap_object filemap_push(filemap_t* filemap, char** fields, uint64_t* lengths);
typedef struct {
	uint64_t field;
	char* new;
	uint64_t len;
} update_t;
filemap_object filemap_push_updated(filemap_t* filemap, filemap_object* base, update_t* updates, unsigned count);
void filemap_push_field(filemap_object* obj, uint64_t field, uint64_t size, void* x);
void filemap_set(filemap_t* filemap, filemap_partial_object* partial, update_t* updates, unsigned count);
filemap_object filemap_findcpy(filemap_index_t* index, char* key, uint64_t key_size);
void filemap_delete_object(filemap_t* filemap, filemap_object* obj);
void filemap_updated_free(filemap_object* obj);
void filemap_object_free(filemap_t* fmap, filemap_object* obj);
void filemap_index_wcached(filemap_index_t* index);
void filemap_index_free(filemap_index_t* index);
void filemap_ordered_free(filemap_ordered_list_t* order);
void filemap_list_wcached(filemap_list_t* list);
void filemap_list_free(filemap_list_t* list);
void filemap_wcached(filemap_t* filemap);
void filemap_free(filemap_t* filemap);

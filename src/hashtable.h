// Automatically generated header.

#pragma once
#include <string.h>
#include <stdint.h>
#include <emmintrin.h>
#include "siphash.h"
#define CONTROL_BYTES 16
#define DEFAULT_BUCKETS 2
typedef struct {
	uint8_t control_bytes[CONTROL_BYTES];
} bucket;
typedef struct {
	unsigned long key_size;
	unsigned long size;

	/// hash and compare
	uint64_t (* hash)(void*);

	/// compare(&left, &right)
	int (* compare)(void*, void*);

	unsigned long length;
	unsigned long num_buckets;
	char* buckets;
} map_t;
typedef struct {
	map_t* map;

	char c;
	unsigned long bucket;

	void* key;
	void* x;
	char current_c;
	bucket* bucket_ref;
} map_iterator;
typedef struct {
	void* val;
	char exists;
} map_insert_result;
typedef struct {
	map_t* map;

	void* key;
	uint64_t h1;
	uint8_t h2;

	unsigned long probes;

	bucket* current;
	/// temporary storage for c when matching
	char c;
} map_probe_iterator;
extern uint8_t MAP_SENTINEL_H2;
uint64_t hash_string(char** x);
uint64_t hash_ulong(unsigned long* x);
uint64_t hash_ptr(void** x);
int compare_ulong(unsigned long* left, unsigned long* right);
int compare_ptr(void** left, void** right);
unsigned long map_bucket_size(map_t* map);
map_t map_new();
void map_configure(map_t* map, unsigned long size);
void map_configure_string_key(map_t* map, unsigned long size);
void map_configure_ulong_key(map_t* map, unsigned long size);
void map_configure_ptr_key(map_t* map, unsigned long size);
int map_load_factor(map_t* map);
uint16_t mask(bucket* bucket, uint8_t h2);
map_iterator map_iterate(map_t* map);
int map_next(map_iterator* iterator);
extern uint16_t MAP_PROBE_EMPTY;
void* map_find(map_t* map, void* key);
typedef struct {
	char* pos;
	/// 1 if already existent
	char exists;
} map_probe_insert_result;
void map_resize(map_t* map);
map_insert_result map_insert(map_t* map, void* key);
map_insert_result map_insertcpy(map_t* map, void* key, void* v);
void map_cpy(map_t* from, map_t* to);
int map_remove(map_t* map, void* key);
void map_free(map_t* map);

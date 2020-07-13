// Automatically generated header.

#pragma once
#include <string.h>
#include <stdint.h>
#if __arm__
#include <arm_neon.h>
#endif
#if __x86_64
#include <emmintrin.h>
#endif
#define CONTROL_BYTES 16
#define DEFAULT_BUCKETS 2
typedef struct {
	uint8_t control_bytes[CONTROL_BYTES];
} bucket;
#include "rwlock.h"
typedef struct {
	rwlock_t* lock;

	unsigned long key_size;
	unsigned long size;

	/// hash and compare
	uint64_t (* hash)(void*);

	/// compare(&left, &right)
	int (* compare)(void*, void*);

  //free data in map_remove, before references are removed
	void (*free)(void*);

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
	map_t* map;

	void* key;
	uint64_t h1;
	uint8_t h2;

	unsigned long probes;

	bucket* current;
	/// temporary storage for c when matching
	unsigned char c;
} map_probe_iterator;
typedef struct {
	void* val;
	char exists;
} map_insert_result;
uint64_t make_h1(uint64_t hash);
extern uint8_t MAP_SENTINEL_H2;
uint64_t hash_string(char** x);
typedef struct {
  char* bin;
  unsigned long size;
} map_sized_t;
uint64_t hash_sized(map_sized_t* x);
uint64_t hash_uint64(uint64_t* x);
uint64_t hash_ptr(void** x);
int compare_string(char** left, char** right);
int compare_sized(map_sized_t* left, map_sized_t* right);
int compare_uint64(uint64_t* left, uint64_t* right);
int compare_ptr(void** left, void** right);
void free_string(void* x);
void free_sized(void* x);
unsigned long map_bucket_size(map_t* map);
map_t map_new();
void map_distribute(map_t* map);
void map_configure(map_t* map, unsigned long size);
void map_configure_string_key(map_t* map, unsigned long size);
void map_configure_sized_key(map_t* map, unsigned long size);
void map_configure_uint64_key(map_t* map, unsigned long size);
void map_configure_ptr_key(map_t* map, unsigned long size);
int map_load_factor(map_t* map);
map_iterator map_iterate(map_t* map);
int map_next_unlocked(map_iterator* iterator);
int map_next(map_iterator* iterator);
void map_next_delete(map_iterator* iterator);
void* map_findkey_unlocked(map_t* map, void* key);
void* map_find_unlocked(map_t* map, void* key);
void* map_find(map_t* map, void* key);
typedef struct {
	char* pos;
	/// 1 if already existent
	char exists;
} map_probe_insert_result;
void map_resize(map_t* map);
map_insert_result map_insert_locked(map_t* map, void* key);
map_insert_result map_insert(map_t* map, void* key);
map_insert_result map_insertcpy(map_t* map, void* key, void* v);
map_insert_result map_insertcpy_noexist(map_t* map, void* key, void* v);
void map_cpy(map_t* from, map_t* to);
void* map_remove_unlocked(map_t* map, void* key);
void* map_removeptr(map_t* map, void* key);
int map_remove(map_t* map, void* key);
void map_free(map_t* map);

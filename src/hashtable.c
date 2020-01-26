#include <string.h>
#include <stdint.h>
#include <emmintrin.h>

#include "util.h"
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

const char DEFAULT_BUCKET[CONTROL_BYTES] = {0};

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

static uint64_t make_h1(uint64_t hash) {
	return (hash << 7) >> 7;
}

const uint8_t MAP_SENTINEL_H2 = 0x80;

static uint8_t make_h2(uint64_t hash) {
	uint8_t h2 = (hash >> 57);
	if (h2 == 0)
		h2 = 0xFFu; //if h2 is zero, its control byte will be marked empty, so just invert it

	return h2;
}

/// uses siphash
uint64_t hash_string(char** x) {
	return siphash24_keyed((uint8_t*) *x, strlen(*x));
}

uint64_t hash_ulong(unsigned long* x) {
	return siphash24_keyed((uint8_t*) x, sizeof(unsigned long));
}

uint64_t hash_ptr(void** x) {
	return siphash24_keyed((uint8_t*) x, sizeof(void*));
}

int compare_string(char** left, char** right) {
	return strcmp(*left, *right) == 0;
}

int compare_ulong(unsigned long* left, unsigned long* right) {
	return *left == *right;
}

int compare_ptr(void** left, void** right) {
	return *left == *right;
}

unsigned long map_bucket_size(map_t* map) {
	return CONTROL_BYTES + CONTROL_BYTES * map->size;
}

map_t map_new() {
	map_t map = {.length=0, .num_buckets=DEFAULT_BUCKETS};

	return map;
}

void map_configure(map_t* map, unsigned long size) {
	map->size = size + map->key_size;

	unsigned long x = DEFAULT_BUCKETS * map_bucket_size(map);
	map->buckets = heap(x);

	for (unsigned long i = 0; i < map->num_buckets; i++) {
		memcpy(map->buckets + i * map_bucket_size(map), DEFAULT_BUCKET, CONTROL_BYTES);
	}
}

void map_configure_string_key(map_t* map, unsigned long size) {
	map->key_size = sizeof(char*); //string reference is default key

	map->hash = (uint64_t(*)(void*)) hash_string;
	map->compare = (int (*)(void*, void*)) compare_string;

	map_configure(map, size);
}

void map_configure_ulong_key(map_t* map, unsigned long size) {
	map->key_size = sizeof(unsigned long); //string reference is default key

	map->hash = (uint64_t(*)(void*)) hash_ulong;
	map->compare = (int (*)(void*, void*)) compare_ulong;

	map_configure(map, size);
}

void map_configure_ptr_key(map_t* map, unsigned long size) {
	map->key_size = sizeof(unsigned long); //string reference is default key

	map->hash = (uint64_t(*)(void*)) hash_ulong;
	map->compare = (int (*)(void*, void*)) compare_ulong;

	map_configure(map, size);
}

int map_load_factor(map_t* map) {
	return ((double) (map->length) / (double) (map->num_buckets * CONTROL_BYTES)) > 0.5;
}

uint16_t mask(bucket* bucket, uint8_t h2) {
	__m128i control_byte_vec = _mm_loadu_si128((const __m128i*)bucket->control_bytes);

	__m128i result = _mm_cmpeq_epi8(_mm_set1_epi8(h2), control_byte_vec);
	return _mm_movemask_epi8(result);
}

map_iterator map_iterate(map_t* map) {
	map_iterator iterator = {
			map,
			.bucket=0, .c=0,
			.bucket_ref=NULL, .key=NULL
	};

	return iterator;
}

//todo: sse
int map_next(map_iterator* iterator) {
	//while bucket is less than the last bucket in memory
	while (iterator->bucket < iterator->map->num_buckets) {
		iterator->bucket_ref = (bucket*) (iterator->map->buckets + map_bucket_size(iterator->map) * iterator->bucket);

		//if filled, update key
		unsigned char filled = iterator->bucket_ref->control_bytes[iterator->c] != 0
				&& iterator->bucket_ref->control_bytes[iterator->c] != MAP_SENTINEL_H2;

		if (filled) {
			iterator->current_c = iterator->c;
			iterator->key = (char*) iterator->bucket_ref + CONTROL_BYTES + (iterator->map->size * iterator->c);
			iterator->x = iterator->key + iterator->map->key_size;
		}

		//increment byte or bucket
		if (++iterator->c >= CONTROL_BYTES) {
			iterator->bucket++;
			iterator->c = 0;
		}

		//if filled (we've already updated key, return
		if (filled) {
			return 1;
		}
	}

	return 0;
}

static map_probe_iterator map_probe_hashed(map_t* map, void* key, uint64_t h1, uint8_t h2) {
	map_probe_iterator probe_iter = {
			map,
			key, h1, h2,
			.probes=0, .current=NULL
	};

	return probe_iter;
}

static map_probe_iterator map_probe(map_t* map, void* key) {
	uint64_t hash = map->hash(key);
	return map_probe_hashed(map, key, make_h1(hash), make_h2(hash));
}

static int map_probe_next(map_probe_iterator* probe_iter) {
	if (probe_iter->probes >= probe_iter->map->num_buckets)
		return 0;

	uint64_t idx =
			(probe_iter->h1
					+ (uint64_t) ((0.5 * (double)probe_iter->probes)
					+ (0.5 * (double)probe_iter->probes * (double)probe_iter->probes)))

					% probe_iter->map->num_buckets;

	probe_iter->current = (bucket*) (probe_iter->map->buckets + idx * map_bucket_size(probe_iter->map));
	probe_iter->probes++;

	return 1;
}

static uint16_t map_probe_empty(map_probe_iterator* probe_iter) {
	return mask(probe_iter->current, 0);
}

const uint16_t MAP_PROBE_EMPTY = UINT16_MAX;

static void* map_probe_match(map_probe_iterator* probe_iter) {
	uint16_t masked = mask(probe_iter->current, probe_iter->h2);

	//there is a matched byte, find which one
	if (masked > 0) {
		for (probe_iter->c = 0; probe_iter->c < CONTROL_BYTES; probe_iter->c++) {
			if ((masked >> probe_iter->c) & 0x01) {
				void* compare_key =
						(char*) probe_iter->current + CONTROL_BYTES + (probe_iter->map->size * probe_iter->c);

				if (probe_iter->map->compare(probe_iter->key, compare_key))
					return compare_key;
			}
		}
	}

	return NULL;
}

/// returns ptr to key alongside value
void* map_find(map_t* map, void* key) { //TODO: if length == stuff seen stop searching
	map_probe_iterator probe = map_probe(map, key);
	while (map_probe_next(&probe) && map_probe_empty(&probe) != MAP_PROBE_EMPTY) {
		void* x = map_probe_match(&probe);
		if (x)
			return x + map->key_size;
	}

	return NULL;
}

typedef struct {
	char* pos;
	/// 1 if already existent
	char exists;
} map_probe_insert_result;

static map_probe_insert_result map_probe_insert(map_probe_iterator* probe) {
	map_probe_insert_result res = {.exists=0};

	unsigned char c;
	bucket* bucket_ref = NULL;

	while (map_probe_next(probe)) {
		//already exists, overwrite
		char* probe_match = map_probe_match(probe);
		if (probe_match) {
			res.exists = 1;
			res.pos = probe_match;
			return res;
		}

		uint16_t empty = map_probe_empty(probe);

		//look for empty slot and set bucket
		//dont use sse for flexibility to check for sentinels and stuff
		if (!bucket_ref) {
			for (unsigned char c2 = 0; c2 < CONTROL_BYTES; c2++) {
				//empty or sentinel
				if (probe->current->control_bytes[c2] == 0
						|| probe->current->control_bytes[c2] == MAP_SENTINEL_H2) {

					bucket_ref = probe->current;
					c = c2;
					break;
				}
			}
		}

		//empty bucket, stop probing
		if (empty == MAP_PROBE_EMPTY)
			break;
	}

	if (!bucket_ref) {
		res.pos = NULL;
		return res;
	}

	//set h2
	bucket_ref->control_bytes[c] = probe->h2;
	//return insertion point
	res.pos = (char*) bucket_ref + CONTROL_BYTES + (probe->map->size * c);
	return res;
}

//returns the item which can be used/copied if the hashmap is not being used in parallel
static void* map_probe_remove(map_probe_iterator* probe) {
	while (map_probe_next(probe) && map_probe_empty(probe) != MAP_PROBE_EMPTY) {
		void* x = map_probe_match(probe);
		//found, set h2 to sentinel
		if (x) {
			probe->current->control_bytes[probe->c] = MAP_SENTINEL_H2;
			return x;
		}
	}

	return NULL;
}

void map_resize(map_t* map) {
	while (map_load_factor(map)) {
		//double
		unsigned long old_num_buckets = map->num_buckets;
		map->num_buckets = map->num_buckets * 2;

		map->buckets = resize(map->buckets, map->num_buckets * map_bucket_size(map));

		for (unsigned long i = old_num_buckets; i < map->num_buckets; i++) {
			memcpy(map->buckets + i * map_bucket_size(map), DEFAULT_BUCKET, CONTROL_BYTES);
		}

		//rehash
		map_iterator iter = map_iterate(map);
		while (map_next(&iter) && iter.bucket < old_num_buckets) {
			uint64_t hash = map->hash(iter.key);
			uint64_t h1 = make_h1(hash);

			//if it has moved buckets, remove and insert into new bucket
			if (h1 % map->num_buckets != iter.bucket) {
				iter.bucket_ref->control_bytes[iter.current_c] = 0x80;

				map_probe_iterator probe = map_probe_hashed(map, iter.key, h1, make_h2(hash));
				//copy things over
				map_probe_insert_result insertion = map_probe_insert(&probe);
				memcpy(insertion.pos, iter.key, map->size);
			}
		}
	}
}

map_insert_result map_insert(map_t* map, void* key) {
	map_resize(map); //resize before insert to preserve reference integrity

	map_probe_iterator probe = map_probe(map, key);

	map_probe_insert_result insertion = map_probe_insert(&probe);

	//key already exists, skip insertion
	if (!insertion.exists) {
		//store key
		memcpy(insertion.pos, key, map->key_size);
	}

	insertion.pos += map->key_size;

	map->length++;

	map_insert_result res = {.val=insertion.pos, .exists=insertion.exists};
	return res;
}

/// replaces old value, one if already existed
map_insert_result map_insertcpy(map_t* map, void* key, void* v) {
	map_insert_result res = map_insert(map, key);
	//store value
	memcpy(res.val, v, map->size - map->key_size);

	return res;
}

void map_cpy(map_t* from, map_t* to) {
	*to = *from;
	to->buckets = heapcpy(from->num_buckets * map_bucket_size(from), from->buckets);
}

int map_remove(map_t* map, void* key) {
	map_probe_iterator probe = map_probe(map, key);

	if (map_probe_remove(&probe)) {
		map->length--;
		map_resize(map);
		return 1;
	} else {
		return 0;
	}
}

void map_free(map_t* map) {
	drop(map->buckets);
}
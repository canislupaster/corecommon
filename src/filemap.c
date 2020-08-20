#include <err.h>
#include <stdint.h>	 //ints for compatibility, since we are writing to files
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "siphash.h"
#include "threads.h"
#include "util.h"
#include "vector.h"

#define LOAD_FACTOR \
	0.5	 // RESIZE_SLOTS/LOAD_FACTOR must be greater than one, lest probes break
#define SENTINEL 1
#define RESIZE_SLOTS 1
#define MUTEXES 10

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

// generalized iterator for indexes with 64-bit addresses
typedef struct {
	FILE* file;
	mtx_t* lock;

	uint64_t pos;	 // file position

	filemap_partial_object obj;
} filemap_iterator;

// write slots to index and data
// assumes index is at correct point
void write_slots(filemap_index_t* index, unsigned int slots) {
	for (unsigned i = 0; i < slots; i++) {
		uint64_t pos = 0;	 // NULL pos, uninitialized
		fwrite(&pos, 8, 1, index->file);
	}
}

// seed and lengths, before indexes
const uint64_t INDEX_PREAMBLE = 4 * 4 + (8 * 4);
const uint64_t ORDER_INDEX_PREAMBLE = 8 * 4;

filemap_t filemap_new(char* data, unsigned fields, int overwrite) {
	filemap_t filemap;

	filemap.data = fopen(data, "rb+");
	filemap.alias = NULL;
	filemap.fields = fields;

	if (!filemap.data || overwrite) {
		filemap.data = fopen(data, "wb");
		filemap.free = 0;

		// initialize freelist
		fwrite(&filemap.free, 8, 1, filemap.data);

		filemap.data = freopen(data, "rb+", filemap.data);
	} else {
		fseek(filemap.data, 0, SEEK_SET);
		fread(&filemap.free, 8, 1, filemap.data);
	}

	fseek(filemap.data, 0, SEEK_END);
	filemap.size = ftell(filemap.data);

	mtx_init(&filemap.lock, mtx_plain);

	return filemap;
}

filemap_index_t filemap_index_new(filemap_t* fmap, char* index, unsigned field, int overwrite) {
	filemap_index_t new_index;

	new_index.data = fmap;

	new_index.file = fopen(index, "rb+");
	new_index.field = field;

	if (!new_index.file || overwrite) {
		new_index.file = fopen(index, "wb");

		srand(time(NULL));
		// copied from hashtable
		for (unsigned char i = 0; i < 4; i++) {
			new_index.hash_seed[i] = rand();	// seed 16 bytes at a time
		}

		// go to beginning and write seed
		fseek(new_index.file, 0, SEEK_SET);
		fwrite(new_index.hash_seed, 4, 4, new_index.file);

		new_index.slots = 2;
		new_index.resize_slots = 0;
		new_index.resize_lookup_slots = 0;
		new_index.length = 0;

		// uint64s -> 8 bytes beware of this henceforth
		fwrite(&new_index.slots, 8, 1, new_index.file);
		fwrite(&new_index.resize_slots, 8, 1, new_index.file);
		fwrite(&new_index.resize_lookup_slots, 8, 1, new_index.file);

		fwrite(&new_index.length, 8, 1, new_index.file);

		write_slots(&new_index, 2);	 // write slots afterwards

		freopen(index, "rb+", new_index.file);
	} else {
		// read the seed (4 bytes x 4)
		fread(new_index.hash_seed, 4, 4, new_index.file);

		fread(&new_index.slots, 8, 1, new_index.file);
		fread(&new_index.resize_slots, 8, 1, new_index.file);
		fread(&new_index.resize_lookup_slots, 8, 1, new_index.file);

		fread(&new_index.length, 8, 1, new_index.file);
	}

	mtx_init(&new_index.lock, mtx_plain);

	return new_index;
}

filemap_list_t filemap_list_new(char* list, int overwrite) {
	filemap_list_t new_list;

	new_list.file = fopen(list, "rb+");

	if (!new_list.file || overwrite) {
		new_list.file = fopen(list, "wb");

		new_list.length = 0;

		fwrite(&new_list.length, 8, 1, new_list.file);

		freopen(list, "rb+", new_list.file);
	} else {
		fread(&new_list.length, 8, 1, new_list.file);
	}

	mtx_init(&new_list.lock, mtx_plain);

	return new_list;
}

filemap_partial_object filemap_partialize(filemap_partial_object* partial, filemap_object* obj) {
	return (filemap_partial_object){
			.data_pos = obj->data_pos, .exists = obj->exists, .index = partial ? partial->index : 0};
}

filemap_partial_object filemap_partialize_ref(filemap_partial_object* ref, filemap_object* obj) {
	return (filemap_partial_object){
			.data_pos = obj->data_pos, .exists = obj->exists, .index = ref->data_pos};
}

// uses index as data
filemap_object filemap_index_obj(filemap_object* obj,
																 filemap_partial_object* partial) {
	return (filemap_object){.data_pos = partial->index,
													.data_size = obj->data_size,
													.fields = obj->fields,
													.lengths = obj->lengths,
													.exists = obj->exists};
}

static uint64_t filemap_list_value(filemap_list_t* list, uint64_t idx) {
	uint64_t res;

	mtx_lock(&list->lock);

	fseek(list->file, idx, SEEK_SET);
	fread(&res, 8, 1, list->file);

	mtx_unlock(&list->lock);

	return res;
}

filemap_partial_object filemap_deref(filemap_list_t* list,
																		 filemap_partial_object* partial) {
	if (!partial->exists) return (filemap_partial_object){.exists = 0};

	filemap_partial_object ret;
	ret.index = partial->data_pos;
	ret.data_pos = filemap_list_value(list, partial->data_pos);

	ret.exists = ret.data_pos != 0;

	return ret;
}

void write_pages(filemap_ordered_list_t* list, uint64_t next, uint64_t min, uint64_t length) {
	// next, min, and length
	fwrite(&next, 8, 1, list->file);
	fwrite(&min, 8, 1, list->file);
	fwrite(&length, 8, 1, list->file);

	uint64_t set[2] = {0};
	// ordering -- data_pos -- ...
	for (uint64_t i = 0; i < list->page_size; i++)
		fwrite(set, 8 * 2, 1, list->file);
}

filemap_ordered_list_t filemap_ordered_list_new(char* order_list, int page_size, int overwrite) {
	filemap_ordered_list_t new_list;

	new_list.file = fopen(order_list, "rb+");
	new_list.page_size = page_size;

	if (!new_list.file || overwrite) {
		new_list.file = fopen(order_list, "wb");

		new_list.first = ORDER_INDEX_PREAMBLE;
		new_list.pages = 1;
		new_list.min = UINT64_MAX;
		new_list.max = 0;

		fwrite(&new_list.pages, 8, 1, new_list.file);
		fwrite(&new_list.min, 8, 1, new_list.file);
		fwrite(&new_list.max, 8, 1, new_list.file);
		fwrite(&new_list.first, 8, 1, new_list.file);

		write_pages(&new_list, 0, UINT64_MAX, 0);

		freopen(order_list, "rb+", new_list.file);
	} else {
		fread(&new_list.pages, 8, 1, new_list.file);
		fread(&new_list.min, 8, 1, new_list.file);
		fread(&new_list.max, 8, 1, new_list.file);
		fread(&new_list.first, 8, 1, new_list.file);
	}

	mtx_init(&new_list.lock, mtx_plain);

	return new_list;
}

uint64_t do_hash(filemap_index_t* index, char* key, uint64_t key_size) {
	return siphash24(key, key_size, (char*)&index->hash_seed);
}

// fields are in the format
// skip to one, skip to two, one, two
// so you can index the sizes without having to read them
// returns the size of the field (skipfield-skip(field-1))
// expects index already locked
static uint64_t skip_fields(filemap_t* filemap, uint64_t field) {
	uint64_t skip_field;
	uint64_t skip_field_before = 0;

	if (field > 0) {
		fseek(filemap->data, (field - 1) * 8, SEEK_CUR);
		fread(&skip_field_before, 8, 1, filemap->data);
	}

	fread(&skip_field, 8, 1, filemap->data);

	// 1 less field because we have read one
	fseek(filemap->data, ((filemap->fields - field - 1) * 8) + skip_field_before, SEEK_CUR);
	return skip_field - skip_field_before;
}

// linear hashing
// add another bucket(s)
void filemap_resize(filemap_index_t* index) {
	while ((double)index->length / (double)(index->slots + index->resize_slots) >= LOAD_FACTOR) {
		uint64_t lookup_overlap = index->resize_lookup_slots - index->resize_slots;

		if (lookup_overlap < RESIZE_SLOTS) {
			index->resize_lookup_slots -= lookup_overlap;
			index->resize_lookup_slots += RESIZE_SLOTS;

			// add missing slots
			fseek(index->file, 0, SEEK_END);
			write_slots(index, RESIZE_SLOTS - lookup_overlap);
		}

		index->resize_slots += RESIZE_SLOTS;

		for (uint64_t i = index->resize_slots - RESIZE_SLOTS; i < index->resize_slots; i++) {
			uint64_t hash;

			for (uint64_t probes = 0; probes < index->slots; probes++) {	// extra check in case non-resized slots are full and have
																												// same hash
				uint64_t slot_pos = INDEX_PREAMBLE + 8 * ((i + ((probes + probes * probes) / 2)) % index->slots);
				fseek(index->file, slot_pos, SEEK_SET);

				uint64_t pos = 0;
				fread(&pos, 8, 1, index->file);

				if (!pos) break;

				uint64_t data_pos = pos;
				if (index->data->alias) {
					data_pos = filemap_list_value(index->data->alias, pos);
					if (data_pos == 0) continue;
				}

				mtx_lock(&index->data->lock);

				fseek(index->data->data, data_pos, SEEK_SET);

				uint64_t f_size = skip_fields(index->data, index->field);

				char field[f_size];
				if (f_size > 0) {	 // this is neccesary :)
					if (fread(field, (size_t)f_size, 1, index->data->data) < 1) {
						fprintf(stderr, "corrupted database, field %i does not exist", index->field);
					}
				}

				mtx_unlock(&index->data->lock);

				hash = do_hash(index, field, f_size);
				if (hash % index->slots != i) continue;
				
				uint64_t slots = (index->slots + (i/index->slots)*index->slots) * 2;
 				if (probes>0 || hash % slots != i) {
					// seek back to pos, set to zero (since we are moving item)
					fseek(index->file, slot_pos, SEEK_SET);

					uint64_t set_pos = 0;
					fwrite(&set_pos, 8, 1, index->file);

					long new_probes = 0;
					uint64_t new_pos = 1;
					uint64_t new_index;

					while (new_pos != 0) {
						uint64_t slot =
								hash + (uint64_t)((new_probes + new_probes * new_probes) / 2);

						slot %= slots;

						// add more lookup slots for probing
						// these are only for already-resized slots
						if (slot >= index->slots + index->resize_lookup_slots) {
							uint64_t diff = slot - (index->slots + index->resize_lookup_slots - 1);

							fseek(index->file, 0, SEEK_END);
							write_slots(index, diff);

							index->resize_lookup_slots += diff;
						}

						fseek(index->file, INDEX_PREAMBLE + slot * 8, SEEK_SET);

						new_index = ftell(index->file);
						fread(&new_pos, 8, 1, index->file);

						new_probes++;
					}

					// write position to new index
					fseek(index->file, new_index, SEEK_SET);
					fwrite(&pos, 8, 1, index->file);
				}
			}
		}

		// resize complete, don't think theres much more to do
		if (index->resize_slots >= index->slots) {
			index->resize_slots -= index->slots;
			index->resize_lookup_slots -= index->slots;

			index->slots *= 2;
		}
	}
}

static filemap_partial_object filemap_find_unlocked(filemap_index_t* index,
																										uint64_t hash,
																										char* key,
																										uint64_t key_size,
																										int insert) {
	filemap_partial_object obj;
	obj.exists = 0;

	long probes = 0;

	uint64_t slot2 = hash % (index->slots * 2);
	char resizing = slot2 >= index->slots &&
									slot2 < index->slots + index->resize_slots;	 // whether it starts from a resizing slot

	while (1) {
		uint64_t slot = (hash + (uint64_t)((probes + probes * probes) / 2)) %
										(resizing ? index->slots * 2 : index->slots);

		if ((resizing && slot >= index->slots + index->resize_lookup_slots) ||
				(!resizing && probes >= index->slots)) {
			
			if (insert) {
				uint64_t diff = slot - (index->slots + index->resize_lookup_slots - 1);

				fseek(index->file, 0, SEEK_END);
				write_slots(index, diff);

				index->resize_lookup_slots += diff;
			} else {
				break;	// exceeded extra resize probing slots, wasn't extended during
								// resize and so does not exist
			}
		}

		fseek(index->file, INDEX_PREAMBLE + (slot * 8), SEEK_SET);

		obj.index = ftell(index->file);
		fread(&obj.data_pos, 8, 1, index->file);	// read pos from current slot

		if (obj.data_pos == SENTINEL) {
			if (insert) {
				obj.data_pos = 0;
				return obj;
			}

			probes++;
			continue;
		}

		if (obj.data_pos != 0) {
			uint64_t data_pos = obj.data_pos;

			if (index->data->alias) {
				data_pos = filemap_list_value(index->data->alias, data_pos);
				
				if (data_pos == 0) {
					probes++;
					continue;
				}
			}

			mtx_lock(&index->data->lock);

			fseek(index->data->data, data_pos, SEEK_SET);
			uint64_t cmp_key_size = skip_fields(index->data, index->field);

			if (cmp_key_size == key_size) {
				char cmp_key[key_size];
				fread(cmp_key, (size_t)cmp_key_size, 1, index->data->data);

				if (memcmp(key, cmp_key, key_size) == 0) {
					mtx_unlock(&index->data->lock);
					mtx_unlock(&index->lock);

					obj.exists = 1;
					return obj;
				}
			}

			mtx_unlock(&index->data->lock);
		} else {
			break;
		}

		probes++;
	}

	return obj;
}

filemap_partial_object filemap_find(filemap_index_t* index, char* key, uint64_t key_size) {
	uint64_t hash = do_hash(index, key, key_size);

	mtx_lock(&index->lock);
	filemap_partial_object obj = filemap_find_unlocked(index, hash, key, key_size, 0);

	mtx_unlock(&index->lock);

	return obj;
}

// returns 1 if list needs to be rearranged so that block is at the head
// locks data
int freelist_insert(filemap_t* filemap, uint64_t freelist, uint64_t block, uint64_t size) {
	uint64_t freelist_prev = 0;
	uint64_t cmp_size = UINT64_MAX;

	mtx_lock(&filemap->lock);

	// keep iterating from the front until we find an element that we are greater
	// then
	while (freelist) {
		fseek(filemap->data, freelist, SEEK_SET);
		fread(&cmp_size, 8, 1, filemap->data);	// read size

		if (size > cmp_size) {
			break;
		} else {
			freelist_prev = freelist;
		}

		fread(&freelist, 8, 1, filemap->data);	// read next
	}

	// insert between freelist_prev and freelist

	// write size and next (freelist)
	fseek(filemap->data, block, SEEK_SET);
	fwrite(&size, 8, 1, filemap->data);
	fwrite(&freelist, 8, 1, filemap->data);

	if (freelist_prev) {
		fseek(filemap->data, freelist_prev + 8, SEEK_SET);	// go to next
		fwrite(&block, 8, 1, filemap->data);

		mtx_unlock(&filemap->lock);
		return 0;
	} else {
		mtx_unlock(&filemap->lock);
		return block;
	}
}

// get a free block from freelist or end of data file
// data_size includes key or data lengths
// locks data
uint64_t get_free(filemap_t* filemap, uint64_t data_size) {
	mtx_lock(&filemap->lock);

	if (filemap->free) {
		fseek(filemap->data, filemap->free, SEEK_SET);

		uint64_t size, next;

		fread(&size, 8, 1, filemap->data);
		fread(&next, 8, 1, filemap->data);

		if (size == data_size) {
			uint64_t free = filemap->free;
			filemap->free = next;

			mtx_unlock(&filemap->lock);

			return free;

		} else if (size > data_size && size - data_size >= 8 * 2) {
			// insert residue as free block
			uint64_t block = filemap->free + data_size;
			uint64_t freed = filemap->free;

			// mutex hell
			mtx_unlock(&filemap->lock);

			// set next or block as head
			int res = freelist_insert(filemap, next, block, size - data_size);

			mtx_lock(&filemap->lock);

			if (res) {
				filemap->free = block;
			} else {
				filemap->free = next;
			}

			mtx_unlock(&filemap->lock);

			return freed;
		}
	}

	//no matching frees, extend size
	fseek(filemap->data, 0, SEEK_END);
	uint64_t new_block = ftell(filemap->data);

	filemap->size += data_size;

	mtx_unlock(&filemap->lock);

	return new_block;
}

static void readlens(filemap_t* filemap, uint64_t* lengths, uint64_t* sum) {
	// read lengths
	*sum = 0;	 // use sum to decode skips into lengths

	for (unsigned i = 0; i < filemap->fields; i++) {
		fread(&lengths[i], 8, 1, filemap->data);
		lengths[i] -= *sum;

		*sum += lengths[i];
	}
}

filemap_object filemap_cpy(filemap_t* filemap, filemap_partial_object* res) {
	filemap_object obj;

	if (res->exists) {
		obj.data_pos = res->data_pos;

		obj.data_size = 0;

		obj.lengths = heap(8 * filemap->fields);
		obj.fields = heap(sizeof(char*) * filemap->fields);

		mtx_lock(&filemap->lock);

		fseek(filemap->data, obj.data_pos, SEEK_SET);

		readlens(filemap, obj.lengths, &obj.data_size);
		obj.data_size += 8 * filemap->fields;

		// read data
		for (unsigned i = 0; i < filemap->fields; i++) {

			if (obj.lengths[i] > 0 && ftell(filemap->data)+obj.lengths[i]-1 <= filemap->size) {
				obj.fields[i] = heap(obj.lengths[i]);
				fread(obj.fields[i], obj.lengths[i], 1, filemap->data);
			} else {
				obj.fields[i] = NULL;
			}
		}

		mtx_unlock(&filemap->lock);

		obj.exists = 1;
		return obj;

	} else {
		obj.exists = 0;
		return obj;
	}
}

filemap_object filemap_cpyref(filemap_t* filemap, filemap_partial_object* obj) {
	if (filemap->alias) {
		if (!obj->exists) return (filemap_object){.exists = 0};

		filemap_partial_object temp;

		temp.data_pos = filemap_list_value(filemap->alias, obj->data_pos);
		if (temp.data_pos == 0) return (filemap_object){.exists = 0};

		temp.exists = 1;

		return filemap_cpy(filemap, &temp);
	} else {
		return filemap_cpy(filemap, obj);
	}
}

typedef struct {
	char exists;
	vector_t val;
} filemap_field;

filemap_field filemap_cpyfield(filemap_t* filemap,
															 filemap_partial_object* partial,
															 unsigned field) {
	if (!partial->exists) return (filemap_field){.exists = 0};

	mtx_lock(&filemap->lock);

	fseek(filemap->data, partial->data_pos, SEEK_SET);
	uint64_t len = skip_fields(filemap, field);

	if (ftell(filemap->data) + len-1 > filemap->size)
		return (filemap_field){.exists=0};

	vector_t vec = vector_new(1);

	char* data = vector_stock(&vec, len);
	fread(data, len, 1, filemap->data);

	mtx_unlock(&filemap->lock);

	return (filemap_field){.exists = 1, .val = vec};
}

filemap_field filemap_cpyfieldref(filemap_t* filemap, filemap_partial_object* partial, unsigned field) {
	if (filemap->alias) {
		if (!partial->exists) return (filemap_field){.exists = 0};

		filemap_partial_object temp;
		temp.data_pos = filemap_list_value(filemap->alias, partial->data_pos);
		if (temp.data_pos == 0) return (filemap_field){.exists = 0};

		temp.exists = 1;

		return filemap_cpyfield(filemap, &temp, field);
	} else {
		return filemap_cpyfield(filemap, partial, field);
	}
}

// adds obj to index
filemap_partial_object filemap_insert(filemap_index_t* index,
																			filemap_object* obj) {
	char* key = obj->fields[index->field];
	uint64_t key_size = obj->lengths[index->field];

	uint64_t hash = do_hash(index, key, key_size);

	mtx_lock(&index->lock);

	filemap_resize(index);

	filemap_partial_object partial =
			filemap_find_unlocked(index, hash, key, key_size, 1);

	fseek(index->file, partial.index, SEEK_SET);
	fwrite(&obj->data_pos, 8, 1, index->file);	// write data_pos

	partial.data_pos = obj->data_pos;	 //"file" leaks will occur if previous data is not removed

	if (!partial.exists) index->length++;
	partial.exists = 1;

	mtx_unlock(&index->lock);

	return partial;
}

int filemap_remove(filemap_index_t* index, char* key, uint64_t key_size) {
	uint64_t hash = do_hash(index, key, key_size);

	mtx_lock(&index->lock);

	filemap_partial_object obj = filemap_find_unlocked(index, hash, key, key_size, 0);
	if (!obj.index) {
		mtx_unlock(&index->lock);
		return 0;
	}

	// set original index to zero to indicate slot is empty
	fseek(index->file, obj.index, SEEK_SET);
	uint64_t setptr = SENTINEL;
	fwrite(&setptr, 8, 1, index->file);

	index->length--;

	mtx_unlock(&index->lock);

	return 1;
}

filemap_iterator filemap_index_iterate(filemap_index_t* index) {
	filemap_iterator iter = {
			.pos = INDEX_PREAMBLE, .file = index->file, .lock = &index->lock};
	return iter;
}

filemap_partial_object filemap_add(filemap_list_t* list, filemap_object* obj) {
	filemap_partial_object res = {.data_pos = obj ? obj->data_pos : UINT64_MAX,
																.exists = 1};

	mtx_lock(&list->lock);

	//uncomment (and add a flag to the list struct?) when slots should be reused from the end
//	if (list->length > 0) {
//		fseek(list->file, -8, SEEK_END);
//
//		uint64_t pos = 0;
//		// go backwards through zero entries until preamble
//		while (1) {
//			fread(&pos, 8, 1, list->file);
//
//			if (pos != 0) break;
//			else if (pos == 16) {
//				fseek(list->file, -8, SEEK_CUR);
//				break;
//			} else {
//				fseek(list->file, -16, SEEK_CUR);
//			}
//		}
//	} else {
		fseek(list->file, 0, SEEK_END);
//	}

	res.index = ftell(list->file);

	fwrite(&res.data_pos, 8, 1, list->file);
	list->length++;

	mtx_unlock(&list->lock);

	return res;
}

typedef struct {
	filemap_ordered_list_t* list;
	uint64_t page;
	uint64_t next, min, length;
} filemap_page_iterator;

filemap_page_iterator filemap_page_iterate(filemap_ordered_list_t* list) {
	return (filemap_page_iterator){.list = list, .page = 0};
}

int filemap_page_next(filemap_page_iterator* iter) {
	if (!iter->page) {
		iter->page = iter->list->first;
	} else {
		if (!iter->next) return 0;

		iter->page = iter->next;
	}

	fseek(iter->list->file, iter->page, SEEK_SET);
	fread(&iter->next, 8, 1, iter->list->file);
	fread(&iter->min, 8, 1, iter->list->file);
	fread(&iter->length, 8, 1, iter->list->file);

	return 1;
}

void filemap_page_skip(filemap_ordered_list_t* list, uint64_t length) {
	uint64_t d[2] = {0};
	for (uint64_t i = 0; i < length; i++) {
		do
			fread(d, 8 * 2, 1, list->file);
		while (d[1] == 0);
	}
}

vector_t filemap_page_read(filemap_ordered_list_t* list, uint64_t length) {
	vector_t vec = vector_new(sizeof(uint64_t[2]));
	uint64_t* data = vector_stock(&vec, length);

	for (uint64_t i = 0; i < length; i++) {
		// skip sentinels
		do fread(&data[i * 2], 8 * 2, 1, list->file); while (data[i * 2 + 1] == 0);
	}

	return vec;
}

uint64_t filemap_page_insert(filemap_ordered_list_t* list, uint64_t length, uint64_t item_order, uint64_t data_pos) {
	uint64_t i = 0;
	uint64_t order_pos[2];
	
	for (; i < length; i++) {
		// skip sentinels
		do
			fread(&order_pos, 8 * 2, 1, list->file);
		while (order_pos[1] == 0);

		if (order_pos[0] > item_order) {
			i++; //length - i elements left (read element)
			
			break;
		}
	}

	uint64_t pos;

	if (i == length) {	// length == 0 or end of list
		pos = ftell(list->file);
		fwrite((uint64_t[]){item_order, data_pos}, 8 * 2, 1, list->file);

	} else {
		fseek(list->file, -8 * 2, SEEK_CUR); //insert before if havent reached end
		pos = ftell(list->file);

		vector_t shift = filemap_page_read(list, length - i + 1);

		fseek(list->file, pos, SEEK_SET);
		fwrite((uint64_t[]){item_order, data_pos}, 8 * 2, 1, list->file);

		fwrite(shift.data, 8 * 2, shift.length, list->file);
		vector_free(&shift);
	}

	return pos;
}

filemap_partial_object filemap_pop(filemap_list_t* list) {
	filemap_partial_object res;

	mtx_lock(&list->lock);

	if (list->length == 0) {
		mtx_unlock(&list->lock);

		res.exists = 0;
		return res;
	}

	fseek(list->file, -8, SEEK_END);
	res.index = ftell(list->file);

	fread(&res.data_pos, 8, 1, list->file);

	fseek(list->file, -8, SEEK_CUR);

	uint64_t new = 0;
	fwrite(&new, 8, 1, list->file);

	mtx_unlock(&list->lock);

	res.exists = 1;
	return res;
}

filemap_ord_partial_object filemap_ordered_insert(filemap_ordered_list_t* list,
																									uint64_t item_order,
																									filemap_object* obj) {
	filemap_ord_partial_object res = {
			.partial = {.data_pos = obj->data_pos, .exists = 1},
			.item_order = item_order};

	if (item_order < list->min) list->min = item_order;
	if (item_order > list->max) list->max = item_order;

	mtx_lock(&list->lock);

	// pages:
	// next -- min -- length -- data ........
	filemap_page_iterator iter = filemap_page_iterate(list);

	uint64_t min;
	uint64_t length;
	uint64_t page;

	filemap_page_next(&iter);	 // at least one item

	min = iter.min;
	length = iter.length;
	page = iter.page;

	int cont = 1;
	while (cont) {
		cont = filemap_page_next(&iter);

		// if current page isn't full (and inserting does not cause discontinuity),
		// adjust minimum and write
		if (length < list->page_size && (!cont || item_order <= iter.min)) {
			fseek(list->file, page + 8, SEEK_SET);

			// replace min and increment length
			if (item_order < min) fwrite(&item_order, 8, 1, list->file);
			else
				fseek(list->file, 8, SEEK_CUR);

			length++;
			fwrite(&length, 8, 1, list->file);

			res.page = page;
			res.partial.index =
					filemap_page_insert(list, length - 1, item_order, obj->data_pos);
			break;

			// otherwise write new page (in-between) if both current and next page are
			// full (or next does not exist)
		} else if (item_order >= min &&
							 (!cont ||
								(item_order < iter.min && iter.length >= list->page_size))) {
			// write new page
			fseek(list->file, 0, SEEK_END);
			uint64_t new_page = ftell(list->file);
			write_pages(list, cont ? iter.page : 0, item_order, 1);

			fseek(list->file, new_page + 8 * 3, SEEK_SET);
			fwrite((uint64_t[]){item_order, obj->data_pos}, 8 * 2, 1, list->file);

			// update links
			fseek(list->file, page, SEEK_SET);
			fwrite(&new_page, 8, 1, list->file);

			res.page = new_page;
			res.partial.index = new_page + 8 * 3;
			break;
		} else if (page == list->first && item_order <= iter.min) {
			// write page behind
			fseek(list->file, 0, SEEK_END);
			list->first = ftell(list->file);
			write_pages(list, page, item_order, 1);

			fseek(list->file, list->first + 8 * 3, SEEK_SET);
			fwrite((uint64_t[]){item_order, obj->data_pos}, 8 * 2, 1, list->file);
		}

		// backup properties for comparisons
		min = iter.min;
		length = iter.length;
		page = iter.page;
	}

	mtx_unlock(&list->lock);

	return res;
}

int filemap_ordered_remove_id(filemap_ordered_list_t* list, uint64_t item_order, filemap_partial_object* obj) {
	filemap_page_iterator iter = filemap_page_iterate(list);

	int cont = 1;
	vector_t pages = vector_new(sizeof(uint64_t));

	mtx_lock(&list->lock);
	filemap_page_next(&iter);

	uint64_t min = iter.min;
	vector_pushcpy(&pages, &iter.page);

	while (cont) {
		cont = filemap_page_next(&iter);

		if (item_order >= min && (item_order <= iter.min || !cont)) {
			while (pages.length>0) {
				uint64_t page = *(uint64_t*)vector_get(&pages, pages.length-1);
				vector_pop(&pages);
				
				fseek(list->file, page + 8*2, SEEK_SET);
				
				uint64_t length;
				fread(&length, 8, 1, list->file);

				uint64_t other[2];
				for (unsigned i=0; i<list->page_size; i++) {
					fread(other, 8*2, 1, list->file);

					if (item_order == other[0] && obj->data_pos == other[1]) {
						fseek(list->file, -8*2, SEEK_CUR);

						uint64_t set[2] = {0};
						fwrite(set, 8*2, 1, list->file);

						//decrement length
						length--;
						fseek(list->file, page+8*2, SEEK_SET);
						fwrite(&length, 8, 1, list->file);

						vector_free(&pages);
						mtx_unlock(&list->lock);
						return 1;
					} else if (other[1] == 0) {
						break;
					}
				}
			}
			
			break;
		}

		vector_pushcpy(&pages, &iter.page);
		min = iter.min;
	}
	
	vector_free(&pages);
	mtx_unlock(&list->lock);
	return 0;
}

void filemap_ordered_remove(filemap_ordered_list_t* list,
														filemap_ord_partial_object* ord_partial) {
	mtx_lock(&list->lock);

	fseek(list->file, ord_partial->partial.index, SEEK_SET);

	uint64_t set[2] = {0};
	fwrite(&set, 8 * 2, 1, list->file);

	fseek(list->file, ord_partial->page + 8, SEEK_SET);

	uint64_t min, length;
	fread(&min, 8, 1, list->file);
	fread(&length, 8, 1, list->file);

	length--;
	fseek(list->file, -8, SEEK_CUR);
	fwrite(&length, 8, 1, list->file);

	if (ord_partial->page != list->first && ord_partial->item_order == min) {
		min = UINT64_MAX;	 // fine because mins will be lowered if no items in page
		vector_t page = filemap_page_read(list, length);

		vector_iterator iter = vector_iterate(&page);
		while (vector_next(&iter)) {
			uint64_t* val = iter.x;
			if (val[0] < min) min = val[0];
		}

		fseek(list->file, ord_partial->page + 8, SEEK_SET);
		fwrite(&min, 8, 1, list->file);
	}

	mtx_unlock(&list->lock);
}

filemap_partial_object filemap_get_idx(filemap_list_t* list, uint64_t i) {
	filemap_partial_object res;

	res.index = i;

	mtx_lock(&list->lock);

	if (res.index < 8 || list->length * 8 + 8 < res.index) {
		mtx_unlock(&list->lock);

		res.exists = 0;
		return res;
	}

	fseek(list->file, res.index, SEEK_SET);

	fread(&res.data_pos, 8, 1, list->file);

	mtx_unlock(&list->lock);

	res.exists = res.data_pos != 0;
	return res;
}

filemap_partial_object filemap_get(filemap_list_t* list, uint64_t i) {
	return filemap_get_idx(list, 8 + (i * 8));
}

vector_t filemap_ordered_page(filemap_ordered_list_t* list, uint64_t start_page, unsigned pages) {
	vector_t vec = vector_new(sizeof(filemap_ord_partial_object));

	mtx_lock(&list->lock);

	filemap_page_iterator iter = filemap_page_iterate(list);

	long items = pages * list->page_size;
	long start_items = start_page * list->page_size;

	while (filemap_page_next(&iter) && items > 0) {
		uint64_t len = iter.length;

		if (start_items > 0) {
			if (iter.length > start_items) {
				len = iter.length - start_items;
				filemap_page_skip(list, start_items);

				start_items = 0;
			} else {
				start_items -= iter.length;
				continue;
			}
		}

		filemap_ord_partial_object* data = vector_stock(&vec, len);
		for (uint64_t i = 0; i < items && i < len; i++) {
			// populate data and skip sentinels
			while (data[i].partial.data_pos == 0) {
				data[i].page = iter.page;
				data[i].partial.exists = 1;

				data[i].partial.index = ftell(list->file);
				fread(&data[i].item_order, 8, 1, list->file);
				fread(&data[i].partial.data_pos, 8, 1, list->file);
			}
		}

		items -= len;
	}

	mtx_unlock(&list->lock);

	return vec;
}

void filemap_list_remove(filemap_list_t* list, filemap_partial_object* obj) {
	mtx_lock(&list->lock);

	// set original index to zero to indicate slot is empty
	fseek(list->file, obj->index, SEEK_SET);
	uint64_t setptr = 0;
	fwrite(&setptr, 8, 1, list->file);

	mtx_unlock(&list->lock);

	obj->exists = 0;
}

void filemap_list_update(filemap_list_t* list, filemap_partial_object* partial, filemap_object* obj) {
	mtx_lock(&list->lock);

	fseek(list->file, partial->index, SEEK_SET);
	fwrite(&obj->data_pos, 8, 1, list->file);

	mtx_unlock(&list->lock);

	partial->data_pos = obj->data_pos;
}

//wrappers to bypass deref
void filemap_list_update_ref(filemap_list_t* list, filemap_partial_object* partial, filemap_object* obj) {
	filemap_partial_object temp = {.index = partial->data_pos};
	filemap_list_update(list, &temp, obj);
}

void filemap_list_remove_ref(filemap_t* filemap, filemap_list_t* list, filemap_partial_object* partial) {
	filemap_partial_object temp = {.index = partial->data_pos};
	filemap_list_remove(list, &temp);
}

filemap_iterator filemap_list_iterate(filemap_list_t* list) {
	filemap_iterator iter = {.pos = 8, .file = list->file, .lock = &list->lock};
	return iter;
}

//sorry for using idx interchangeably for file and actual indices
uint64_t filemap_list_pos(uint64_t idx) {
	return idx*8 + 8;
}

uint64_t filemap_list_idx(uint64_t pos) {
	return pos/8 - 1;
}

int filemap_read_unlocked(filemap_iterator* iter) {
	do {
		iter->obj.index = ftell(iter->file);
		if (fread(&iter->obj.data_pos, 8, 1, iter->file)<1) return 0;

	} while ((!iter->obj.data_pos || iter->obj.data_pos == SENTINEL) && !feof(iter->file));

	return 1;
}

int filemap_next(filemap_iterator* iter) {
	mtx_lock(iter->lock);
	
	fseek(iter->file, iter->pos, SEEK_SET);
	iter->pos += 8;

	int ret = filemap_read_unlocked(iter);

	mtx_unlock(iter->lock);

	if (ret) {
		iter->obj.exists = 1;
		return 1;
	} else {
		return 0;
	}
}

vector_t filemap_readmany(filemap_iterator* iter, int* more, unsigned max) {
	vector_t res = vector_new(sizeof(filemap_partial_object));

	mtx_lock(iter->lock);
	fseek(iter->file, iter->pos, SEEK_SET);

	while (res.length<max) {
		*more = filemap_read_unlocked(iter);
		if (!*more) break;

		iter->obj.exists = 1;
		vector_pushcpy(&res, &iter->obj);
	}
	
	iter->pos = ftell(iter->file);

	mtx_unlock(iter->lock);

	return res;
}

filemap_object filemap_push(filemap_t* filemap, char** fields, uint64_t* lengths) {
	filemap_object obj;

	obj.data_size = 8 * filemap->fields;	// one for each skip
	for (unsigned i = 0; i < filemap->fields; i++) {
		obj.data_size += lengths[i];
	}

	if (obj.data_size < 8 * 2) {	// less than freelist requires, print and return
		fprintf(stderr, "object less than 16 bytes, cannot push");
		obj.exists = 0;
		return obj;
	}

	obj.fields = fields;
	obj.lengths = lengths;

	obj.data_pos = get_free(filemap, obj.data_size);

	mtx_lock(&filemap->lock);

	fseek(filemap->data, obj.data_pos, SEEK_SET);

	// write skips -- sum of all previous lengths
	uint64_t skip = 0;
	for (unsigned i = 0; i < filemap->fields; i++) {
		skip += lengths[i];
		fwrite(&skip, 8, 1, filemap->data);
	}

	// write each field
	for (unsigned i = 0; i < filemap->fields; i++) {
		fwrite(fields[i], lengths[i], 1, filemap->data);
	}

	mtx_unlock(&filemap->lock);

	obj.exists = 1;
	return obj;
}

typedef struct {
	uint64_t field;
	char* new;
	uint64_t len;
} update_t;

filemap_object filemap_push_updated(filemap_t* filemap, filemap_object* base, update_t* updates, unsigned count) {
	char** fields = heap(filemap->fields * 8);
	memcpy(fields, base->fields, filemap->fields * 8);

	uint64_t* lengths = heap(filemap->fields * 8);
	memcpy(lengths, base->lengths, filemap->fields * 8);

	for (unsigned i = 0; i < count; i++) {
		update_t up = updates[i];
		fields[up.field] = up.new;
		lengths[up.field] = up.len;
	}

	return filemap_push(filemap, fields, lengths);
}

void filemap_push_field(filemap_object* obj, uint64_t field, uint64_t size, void* x) {
	obj->fields[field] = resize(obj->fields[field], obj->lengths[field] + size);
	memcpy(obj->fields[field] + obj->lengths[field], x, size);

	obj->lengths[field] += size;
}

//requires lengths to be same, overwrites previous data
void filemap_set(filemap_t* filemap, filemap_partial_object* partial, update_t* updates, unsigned count) {
	uint64_t skips[filemap->fields];
	mtx_lock(&filemap->lock);

	fseek(filemap->data, partial->data_pos, SEEK_SET);
	for (unsigned i = 0; i < filemap->fields; i++) {
		fread(&skips[i], 8, 1, filemap->data);
	}

	for (unsigned i = 0; i < count; i++) {
		update_t up = updates[i];
		uint64_t skip = skips[up.field - 1];

		if (skip < 8 || skip+up.len-1 > filemap->size) continue;

		if (up.field == 0) fseek(filemap->data, partial->data_pos + (8 * filemap->fields), SEEK_SET);
		else
			fseek(filemap->data, partial->data_pos + (8 * filemap->fields) + skip, SEEK_SET);

		fwrite(&up.new, up.len, 1, filemap->data);
	}

	mtx_unlock(&filemap->lock);
}

filemap_object filemap_findcpy(filemap_index_t* index, char* key, uint64_t key_size) {
	filemap_partial_object res = filemap_find(index, key, key_size);

	//implicit deref
	return filemap_cpyref(index->data, &res);
}

void filemap_delete(filemap_t* filemap, filemap_partial_object* obj) {
	mtx_lock(&filemap->lock);

	// get sum / last skip
	fseek(filemap->data, obj->data_pos + (filemap->fields - 1) * 8, SEEK_SET);

	uint64_t size;
	fread(&size, 8, 1, filemap->data);

	mtx_unlock(&filemap->lock);

	if (freelist_insert(filemap, filemap->free, obj->data_pos,
											size + filemap->fields * 8)) {
		filemap->free = obj->data_pos;
	}
}

void filemap_delete_object(filemap_t* filemap, filemap_object* obj) {
	if (freelist_insert(filemap, filemap->free, obj->data_pos, obj->data_size)) {
		filemap->free = obj->data_pos;
	}
}

#define COPY_CHUNK 1024
static void filecpy(FILE* f, uint64_t from, uint64_t size, uint64_t to) {
	while (size > 0) {
		uint64_t chunk_size = COPY_CHUNK > size ? size : COPY_CHUNK;
		size -= chunk_size;

		char chunk[chunk_size];

		fseek(f, from, SEEK_SET);
		fread(chunk, chunk_size, 1, f);

		fseek(f, to, SEEK_SET);
		fwrite(chunk, chunk_size, 1, f);

		from += chunk_size;
		to += chunk_size;
	}
}

void filemap_clean(filemap_t* filemap) {
	mtx_lock(&filemap->lock);

	uint64_t next = filemap->free;
	uint64_t offset = 0;	// extra empty space from frees
	uint64_t size, free;

	// get all free zones, shift em backwards
	while (next) {
		fseek(filemap->data, next, SEEK_SET);	 // skip size
		free = next;

		fread(&size, 8, 1, filemap->data);
		fread(&next, 8, 1, filemap->data);

		filecpy(filemap->data, free + size, next - (free + size),
						free - offset);	 // shift everything after size before next back
		offset += size;
	}

	mtx_unlock(&filemap->lock);
}

void filemap_updated_free(filemap_object* obj) {
	drop(obj->fields);
	drop(obj->lengths);
}

void filemap_object_free(filemap_t* fmap, filemap_object* obj) {
	if (!obj->exists) return;

	for (unsigned i = 0; i < fmap->fields; i++) {
		if (obj->fields[i]) drop(obj->fields[i]);
	}

	drop(obj->fields);
	drop(obj->lengths);
}

void filemap_index_wcached(filemap_index_t* index) {
	mtx_lock(&index->lock);
	fseek(index->file, 4 * 4, SEEK_SET);	// skip seed

	fwrite(&index->slots, 8, 1, index->file);
	fwrite(&index->resize_slots, 8, 1, index->file);
	fwrite(&index->resize_lookup_slots, 8, 1, index->file);

	fwrite(&index->length, 8, 1, index->file);

	mtx_unlock(&index->lock);
}

void filemap_index_free(filemap_index_t* index) {
	filemap_index_wcached(index);
	fclose(index->file);
	mtx_destroy(&index->lock);
}

void filemap_ordered_wcache(filemap_ordered_list_t* order) {
	mtx_lock(&order->lock);

	fseek(order->file, 0, SEEK_SET);

	fwrite(&order->pages, 8, 1, order->file);

	fwrite(&order->min, 8, 1, order->file);
	fwrite(&order->max, 8, 1, order->file);

	fwrite(&order->first, 8, 1, order->file);

	mtx_unlock(&order->lock);
}

void filemap_ordered_free(filemap_ordered_list_t* order) {
	filemap_ordered_wcache(order);
	fclose(order->file);
	mtx_destroy(&order->lock);
}

void filemap_list_wcached(filemap_list_t* list) {
	mtx_lock(&list->lock);

	fseek(list->file, 0, SEEK_SET);
	fwrite(&list->length, 8, 1, list->file);

	mtx_unlock(&list->lock);
}

void filemap_list_free(filemap_list_t* list) {
	filemap_list_wcached(list);
	fclose(list->file);
	mtx_destroy(&list->lock);
}

void filemap_wcached(filemap_t* filemap) {
	mtx_lock(&filemap->lock);

	fseek(filemap->data, 0, SEEK_SET);
	fwrite(&filemap->free, 8, 1, filemap->data);

	mtx_unlock(&filemap->lock);
}

void filemap_free(filemap_t* filemap) {
	filemap_wcached(filemap);
	fclose(filemap->data);
	mtx_destroy(&filemap->lock);
}

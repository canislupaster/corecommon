#include <unistd.h>

#include "database.hpp"

Database::Database(char const* fname): file(fopen(fname, "rb+")) {
	if (!file) file=fopen(fname, "rw+");
}

Database::~Database() {
	fsync(fileno(file));
	fclose(file);
}

Database::BlockRef::BlockRef(Database& db, Block* block, uint64_t idx): db(db), block(block), idx(idx) {
	block->prev=ntohll(block->prev);
	block->next=ntohll(block->next);
}

Database::BlockRef::~BlockRef() {
	if (!block) return;

	block->prev=htonll(block->prev);
	block->next=htonll(block->next);
	munmap(block, sizeof(Block));
}

Database::BlockRef::Address Database::BlockRef::insert(uint16_t size, unsigned char after) {
	return Database::BlockRef::Address();
}

Database::BlockRef Database::map_block(uint64_t idx) {
	return Database::BlockRef(*this, reinterpret_cast<Block*>(mmap(nullptr, sizeof(Block), PROT_WRITE | PROT_READ, MAP_SHARED, fileno(file), idx)), idx);
}

Database::BlockRef Database::make_block() {
	fseek(file, SEEK_END, 0);
	return map_block(ftell(file));
}

Database::NodeRef::NodeRef(Database::Node* node): node(node) {
	for (unsigned& cmp: node->cmps) cmp = ntohl(cmp);
	for (unsigned& loc: node->locs) loc = ntohl(loc);
	for (unsigned& weight: node->weights) weight = ntohl(weight);
}

Database::NodeRef::NodeRef(): node(nullptr) {}

Database::NodeRef::~NodeRef() {
	if (!node) return;

	for (unsigned& cmp: node->cmps) cmp = htonl(cmp);
	for (unsigned& loc: node->locs) loc = htonl(loc);
	for (unsigned& weight: node->weights) weight = htonl(weight);

	munmap(node, sizeof(Node));
}

Database::NodeRef Database::map_node(uint64_t idx) {
	return Database::NodeRef(reinterpret_cast<Node*>(mmap(NULL, sizeof(Node), PROT_WRITE | PROT_READ, MAP_SHARED, fileno(file), idx)));
}

Database::NodeRef Database::make_node() {
	NodeRef ref;
	if (free_node.node) {
		ref=free_node.node;
		
		if (free_node.node->next_free_node!=-1) {
			free_node = map_node(free_node.node->next_free_node);
		}
	} else {
		fseek(file, 0, SEEK_END);
		uint64_t idx = ftell(file);

		char new_node[sizeof(Node)] = {0};
		fwrite(new_node, sizeof(Node), 1, file);
		fseek(file, idx, SEEK_SET);

		ref=map_node(idx);
	}
	
	ref.node->flags = 0;

	for (unsigned& x: ref.node->cmps) x=UINT_MAX;
	for (unsigned& x: ref.node->cmp_locs) x=-1;
	for (unsigned& x: ref.node->locs) x=-1;

	return ref;
}

Database::Row::Row(Database& db, Database::BlockRef&& blockref): db(db), ref(blockref), ptr(blockref.block->data) {
	VarIntRef vi(ptr);
	row_sz = vi.value();
	ptr+=vi.size;
	row_i = 0;
}

Database::Row Database::map_row(uint64_t block_idx, unsigned char rowi) {
	Database::Row row = Database::Row(*this, Database::BlockRef(map_block(block_idx)));
	for (unsigned char i=0; i<rowi; i++) if (!row.skip_row()) throw RowNoExists();
	return row;
}

void Database::Row::next(uint64_t amt) {
	ptr+=amt;

	if (ptr>=ref.block->data+Block::BLOCK_SIZE) {
		uint64_t diff = ptr-ref.block->data;

		while (diff>=Block::BLOCK_SIZE) {
			ref = db.map_block(ref.block->next);
			diff-=Block::BLOCK_SIZE;
		}

		row_i = 0;
		ptr=ref.block->data + diff;
	}
}

bool Database::Row::skip_row() {
	row_i++;
	next(row_sz);

	VarIntRef vi(ptr);
	row_sz = vi.value();
	if (row_sz==0) return false;

	ptr+=vi.size;
	return true;
}

bool Database::Row::skip_col() {
	if (row_sz==0){
		return false;
	} else if (col_skipped) {
		col_skipped=false;
	} else {
		next(8+col_sz);
		row_sz -= 8+col_sz;
	}

	VarIntRef vi(ptr);
	col_sz = vi.value();

	ptr+=vi.size;
	return true;
}

bool Database::Row::skip_ncol(unsigned n) {
	for (; n!=-1; n--) if (!skip_col()) return false;
	return true;
}

Slice<MaybeOwnedSlice<char>> Database::Row::col_rawdata_index(bool skip_idx) {
	if (row_sz==0) return Slice<MaybeOwnedSlice<char>>(MaybeOwnedSlice<char>());

	if (ptr+8+col_sz>=ref.block->data+Block::BLOCK_SIZE) {
		col_skipped=true;

		col_sz+=8;
		row_sz-=col_sz;

		uint64_t saved_col_sz = skip_idx ? col_sz-8 : col_sz;
		char* data = new char[saved_col_sz];

		unsigned left = Block::BLOCK_SIZE-(ptr-ref.block->data);

		do {
			if (skip_idx) {
				if (saved_col_sz+left>col_sz) {
					if (saved_col_sz<col_sz) memcpy(data, ptr, left-saved_col_sz+col_sz);
					else memcpy(data+saved_col_sz-col_sz, ptr, left);
				}
			} else {
				memcpy(data+saved_col_sz-col_sz, ptr, left);
			}

			ref = db.map_block(ref.block->next);

			col_sz-=Block::BLOCK_SIZE;
			left=Block::BLOCK_SIZE;
			ptr=ref.block->data;
		} while (col_sz>Block::BLOCK_SIZE);

		memcpy(data+(saved_col_sz-col_sz), ptr, col_sz);
		ptr+=col_sz;
		col_sz=0;

		return Slice<MaybeOwnedSlice<char>>(MaybeOwnedSlice<char>(data, saved_col_sz, true));
	} else {
		return Slice<MaybeOwnedSlice<char>>(MaybeOwnedSlice<char>(skip_idx ? 8+ptr : ptr, skip_idx ? col_sz : 8+col_sz, false));
	}
}

template<class T>
Slice<MaybeOwnedSlice<T>> Database::Row::col_rawdata() {
	Slice<MaybeOwnedSlice<char>> cdata = col_rawdata_index(true);
	return Slice<MaybeOwnedSlice<T>>(MaybeOwnedSlice<T>(reinterpret_cast<T*>(cdata.data()), cdata.size(), cdata.slice_type.owned));
}

template<class T>
Slice<MaybeOwnedSlice<T>> Database::Row::col_data() {
	return col_rawdata<T>();
}

template<>
Slice<MaybeOwnedSlice<unsigned>> Database::Row::col_data<unsigned>() {
	Slice<MaybeOwnedSlice<unsigned>> cdata = col_rawdata<unsigned>();
	cdata.slice_type.to_owned();

	for (unsigned& x: cdata) {
		x = ntohl(x);
	}

	return cdata;
}

void Database::NodeIterator::shift_by() {
	unsigned b_i = v_offset/8;
	if (b_i<v.size()) {
		char buf[8] = {0};

		if (v.size()-b_i<5) {
			end=true;

			for (unsigned char i=0; i<v.size()-b_i; i++) {
				buf[i] = *(v.data() + b_i+i);
			}

		} else {
			*reinterpret_cast<unsigned*>(buf) = *reinterpret_cast<unsigned*>(v.data()+b_i);
			buf[4] = *(v.data()+b_i+4);
		}

		shifted_v = *reinterpret_cast<uint64_t*>(buf)<<(v_offset%8);
	} else {
		shifted_v = 0;
	}
}

void Database::NodeIterator::shift() {
	int amt = __builtin_clz(min ^ max);
	v_offset+=amt;

	shift_by();

	min<<=amt;
	max<<=amt;
	max |= UINT_MAX>>(32-amt);
}

//UNDERCONSTRUCTION!11,'ouonibunhibnteu 🚧 🚧 🚧 🚧
void Database::go(Database::NodeIterator& iter) {
	unsigned i;
	for (; i<NODE_BRANCHES-1; i++) {
		if (iter.shifted_v<=iter.x.node->cmps[i]) break;
	}

	if (i!=NODE_BRANCHES && iter.x.node->cmp_locs[i]==-1) {
		//insert if needed
		return;
	}

	if (i!=NODE_BRANCHES && iter.shifted_v==iter.x.node->cmps[i]) {
		Row r = map_row(iter.x.node->cmp_locs[i], iter.x.node->cmp_loc_rowi[i]);

		if (iter.end) {
			unsigned char flag = iter.x.node->flags >> (2*i);
			bool do_insert_reinsert;
			if (flag & CMP_FLAGS_END) {
				r.skip_ncol(iter.col_i);
				if (r.col_sz==iter.col_sz) {
					if (iter.insert && (iter.nonunique || iter.overwrite)) {
						if (iter.overwrite) {
							r.remove_exclude_index(iter.col_i);
						}

						r.insert_after(iter.rowdata);

						if (iter.overwrite) {
							iter.x.node->cmp_locs[i] = r.ref.idx;
							iter.x.node->cmp_loc_rowi[i] = r.row_i;
						}
					}

					return;
				}

				do_insert_reinsert = iter.col_sz<r.col_sz;
			} else {
				do_insert_reinsert = true;
			}

			if (do_insert_reinsert) {
				//insert our node here (since it terminates earlier) and the other node downward
				//(since the other node is already duplicated downstream)
				return;
			}
		}

		i++; //insert after cmp (GEQ side)

		iter.min = 0;
		iter.max = UINT_MAX;
		iter.v_offset += 32;
		iter.shift_by();
	} else {
		if (i>0 && i<iter.x.node->cmps.size()) {
			iter.min = iter.x.node->cmps[0];
			iter.max = iter.x.node->cmps[NODE_BRANCHES-2];
		} else if (i==0) {
			iter.max=iter.x.node->cmps[0];
		} else if (i==iter.x.node->cmps.size()) {
			iter.min=iter.x.node->cmps[NODE_BRANCHES-2];
		}

		iter.shift();
	}

	if (iter.x.node->locs[i]==-1) {
		if (iter.insert) {
			NodeRef branch = make_node();
			iter.x.node->locs[i] = branch.idx;
			branch.node->cmps[0] = iter.cmp;
//			branch.node->cmp_locs
		}

		return;
	}

	iter.x = map_node(iter.x.node->locs[i]);
	go(iter);
}

//template<template<class> class SliceType>
//Database::NodeRef Database::insert_rec(Database::InsertInfo info, Database::NodeRef base) {
//	Row r = map_row(base.node->cmp_locs[i], base.node->cmp_loc_rowi[i]);
//	r.insert_after(info.rowdata);
//	base.node->cmp_locs[] = r.
//			base.x->flags |= NODE_FLAGS_END << (2*i);
//
//
//	if (info.overwrite) {
//		r.remove_exclude_index(info.col_i);
//		base.node->cmp_locs[i] = info.loc;
//	} else if (info.nonunique) {
//
//	}
//
//
//	base.node->cmp_locs[i] = info.loc;
//	base.node->flags |= CMP_FLAGS_END<<(2*i);
//}

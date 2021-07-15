#ifndef CORECOMMON_SRC_DATABASE_HPP_
#define CORECOMMON_SRC_DATABASE_HPP_

#include <sys/mman.h>
#include <fstream>
#include <array>

#include "util.hpp"

class Database {
 private:
	static const unsigned char NODE_FLAGS_END = 1<<0;
	static const unsigned char CMP_FLAGS_END = 1<<1;
	static const size_t NODE_BRANCHES = 4;

	struct __attribute__((packed)) Node {
		union {
			struct {
				unsigned char flags;

				std::array<unsigned, NODE_BRANCHES-1> cmps;
				std::array<unsigned, NODE_BRANCHES-1> cmp_locs;
				std::array<unsigned char, NODE_BRANCHES-1> cmp_loc_rowi;

				std::array<unsigned, NODE_BRANCHES> locs;
				std::array<unsigned, NODE_BRANCHES> weights;
			};

			uint64_t next_free_node;
		};
	};

	class NodeRef {
	 public:
		Node* node;
		uint64_t idx;

		NodeRef(Node* node);
		NodeRef();
		~NodeRef();
	};

	struct __attribute__((packed)) Block {
		static const size_t BLOCK_SIZE=4096;
		uint64_t prev, next;

		struct Address {
			uint64_t start;
			unsigned char where;
		};

		char data[BLOCK_SIZE];
	};

	struct BlockRef {
		Database& db;

		Block* block;
		uint64_t idx;

		struct Address {
			uint64_t start;
			unsigned char where;
			char* data;

			Block::Address address() const;
		};

		BlockRef::Address insert(uint16_t size, unsigned char after);
		BlockRef::Address push(uint16_t size);
		void remove(unsigned char where);

		BlockRef::Address operator[](unsigned char where);

		BlockRef(Database& db, Block* block, uint64_t idx);
		~BlockRef();
	};

	NodeRef free_node;
	BlockRef free_block;

 public:
	Database(char const* fname);
	~Database();

	enum class ColType {
		Unsigned,
		String
	};

	class Table {
	 private:
		struct Column {
			ColType coltype;
			NodeRef base; //nullable
		};

		std::vector<Column> cols;
		friend class Database;

	 public:
		unsigned const id;
	};

	class Row {
	 private:
		friend class Database;

		Database& db;
		BlockRef ref;

		char* ptr;
		uint64_t row_sz, col_sz;
		unsigned char row_i;
		bool col_skipped;

		Row(Database& db, BlockRef&& blockref);
		void next(uint64_t amt);

		void remove_exclude_index(unsigned exclude_index);

		template<template<class> class SliceType>
		void insert_after(Slice<SliceType<char>> rowdata);

		Slice<MaybeOwnedSlice<char>> col_rawdata_index(bool skip_idx);

	 public:
		bool skip_row();
		bool skip_col();
		bool skip_ncol(unsigned n);

		template<class T>
		Slice<MaybeOwnedSlice<T>> col_rawdata();

		template<class T>
		Slice<MaybeOwnedSlice<T>> col_data();
	};

	struct RowNoExists: public std::exception {
		char const* what() const noexcept {
			return "row specified by an index does not exist";
		}
	};

	struct ColNoExists: public std::exception {
		char const* what() const noexcept {
			return "column needed for indexing or retrieval does not exist";
		}
	};

 private:
	FILE* file;

	BlockRef map_block(uint64_t idx);
	BlockRef make_block();

	Row map_row(uint64_t block_idx, unsigned char rowi);

	NodeRef map_node(uint64_t idx);
	NodeRef make_node();

	struct NodeIterator {
		unsigned col_i, col_sz;

		unsigned min, max;

		Slice<MaybeOwnedSlice<unsigned char>> v;

		unsigned v_offset;
		unsigned shifted_v;
		bool end;

		bool insert, nonunique, overwrite;
		unsigned cmp;
		Slice<DynSlice<char>> rowdata;

		Database::NodeRef x;

		void shift_by();
		void shift();
	};

	void go(NodeIterator& iter);

	void balance(NodeRef x);
//	template<template<class> class SliceType>
//	NodeRef insert_rec(InsertInfo info, NodeRef base);
	template<template<class> class SliceType>
	NodeRef search(NodeRef base, Slice<SliceType<unsigned>> v);
};

#endif //CORECOMMON_SRC_DATABASE_HPP_

#ifndef CORECOMMON_SRC_MAP_HPP_
#define CORECOMMON_SRC_MAP_HPP_

#include <array>
#include <limits>
#include <vector>
#include <memory>
#include <string>
#include <utility>
#include <optional>

#if __arm__
#include <arm_neon.h>
#endif

#if __SSE__
#include <emmintrin.h>
#endif

template<class K, class V, bool multiple=false, template<class> class Allocator = std::allocator>
class Map {
 public:
	size_t count;
	using Bucket = std::pair<K, V>;

	Map() {
		count=0;
		resize(DEFAULT_BUCKETS);
	}

	Map(unsigned cap): count(0) {
		unsigned sz=0;
		for (; (1<<sz)<=cap; sz++);
		resize((1<<(sz+1)));
	}

	Map(std::initializer_list<std::pair<K,V>> init): Map() {
		for (auto [k,v]: init) insert(k,v);
	}

	V* operator[](K const& k) {
		FindIterator x=find_begin(k);
		return x.bucket ? &x.bucket->second : nullptr;
	}

	V const* operator[](K const& k) const {
		return const_cast<Map&>(*this)[k];
	}

	std::optional<V> insert(K k, V v) {
		check_resize();

		size_t h = do_hash(k);
		Probe p = Probe(*this, h);

		Bucket* bucket;
		if (multiple) {
			while (!(bucket = p.insert())) ++p;
			count++;
		} else {
			while (true) {
				MatchResult res = p.match(k);
				if ((bucket=res.match)) {
					V copy = std::move(bucket->second);
					bucket->second = v;
					return std::make_optional<V>(std::move(copy));
				} else if (!res.cont) {
					bucket=p.insert();
					count++;
					break;
				}

				++p;
			}
		}

		bucket->first = k;
		bucket->second = v;
		return std::optional<V>();
	}

	V& upsert(K k) {
		check_resize();

		size_t h = do_hash(k);
		Probe p = Probe(*this, h);

		Bucket* bucket;
		if (multiple) {
			while (!(bucket = p.insert())) ++p;
			count++;
		} else {
			while (true) {
				MatchResult res = p.match(k);
				if ((bucket=res.match)) {
					return bucket->second;
				} else if (!res.cont) {
					bucket=p.insert();
					count++;
					break;
				}

				++p;
			}
		}

		bucket->first = k;
		return bucket->second;
	}

	std::optional<V> remove(K& k) {
		size_t h = do_hash(k);
		Probe p = Probe(*this, h);

		while (true) {
			MatchResult res = p.match(k);

			if (res.match) {
				p.remove();
				return std::optional(res.match->second);
			} else if (!res.cont) {
				break;
			}

			++p;
		}

		return std::optional<V>();
	}

 private:
	static const unsigned char NUM_CONTROL_BYTES=16;
	static const unsigned char SENTINEL=0x80;
	static const unsigned char DEFAULT_BUCKETS=2;

	using ControlBytes = std::array<unsigned char, NUM_CONTROL_BYTES>;

	using BucketAllocator = Allocator<Bucket>;
	using ControlBytesAllocator = Allocator<ControlBytes>;

	//stores partial hashes of bucketed items, 0x80 if item ahead in probe chain
	//0x00 if empty
	std::vector<ControlBytes, ControlBytesAllocator> control_bytes;
	std::vector<Bucket, BucketAllocator> buckets;

	constexpr static float LOAD_FACTOR_CONSTANT = 2;

	bool load_factor() const {
		return LOAD_FACTOR_CONSTANT*count>buckets.size();
	}

	static size_t do_hash(K const& k) {
		size_t h = std::hash<K>()(k);
		if ((h&UCHAR_MAX)==0 || (h&UCHAR_MAX)==SENTINEL) {
			h=~h;
		}

		return h;
	}

	struct MatchResult {
		bool cont;
		Bucket* match;
	};

	class Probe {
	 public:
		Map& map;

		unsigned i;
		unsigned char* current;
		unsigned probe_i;
		unsigned char c;
		unsigned char target;

		bool cont;
#ifdef __SSE__
		uint16_t current_matches;
#elif defined(__arm__)
		uint64_t current_matches[2];
#endif

		Probe(Map& map, size_t hash): map(map), i((hash >> 8) % map.control_bytes.size()),
		                              current(map.control_bytes[i].data()), probe_i(0), c(0), target(hash&UCHAR_MAX) {}

		void operator++() {
			probe_i++;
			i=(i+probe_i)%map.control_bytes.size();
			current=map.control_bytes[i].data();

			c=0;
		}

		MatchResult match(K const& k) {
			//one of the big points about the extra metadata is that this is sse optimized
			//but imma just let the compiler sort that out, hopefully (last time i used sse, it was practically the only hindrance from portability)
			//its still better than just hashing in that i can store sentinels & shizz
			//plus VERY locality, probably
			MatchResult res = {.match=nullptr};

			//does faster than sse experiments kinda, uncomment if u dont believe me (or look at the crappy "sse" code)
#if true//!defined(__SSE__) && !defined(__arm__)
			if (c==0) {
				cont=true;
				for (; c<NUM_CONTROL_BYTES; c++) {
					if (!current[c]) {
						cont=false;
					}
				}

				c=0;
			}

			for (; c<NUM_CONTROL_BYTES; c++) {
				if (current[c]==target) {
					Bucket* bucket = &map.buckets[i*NUM_CONTROL_BYTES+c];
					if (bucket->first==k) {
						res.match=bucket;
						break;
					}
				}
			}
#else
			if (c==0) {
#ifdef __SSE__
				__m128i control_byte_vec = _mm_loadu_si128((const __m128i*)current);

				__m128i result = _mm_cmpeq_epi8(_mm_set1_epi8(target), control_byte_vec);
				current_matches = _mm_movemask_epi8(result);

				cont = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(0), control_byte_vec))==0;

#elif defined(__arm__)
				uint8x16_t control_byte_vec = vld1q_u8(current);
				uint8x16_t result = vceqq_u8(control_byte_vec, vdupq_n_u8(target));
				vst1q_u8((uint8_t*)current_matches, result); //no movemask?

				uint8x16_t empty_res = vceqq_u8(control_byte_vec, vdupq_n_u8(0));
				uint64_t empty_mem[2];
				vst1q_u8((uint8_t*)empty_mem, empty_res);

				cont = empty_mem[0]==0 && empty_mem[1]==0;
#endif
			}

			unsigned offset;
#ifdef __SSE__
			while (current_matches > 0) {
				offset = __builtin_ctz(current_matches);
#elif defined(__arm__)
			while (masked[0] > 0 || masked[1] > 0) {
				offset = masked[0]>0 ? __builtin_ctzll(masked[0])/8 : __builtin_ctzll(masked[1])/8;
#endif

#ifdef __SSE__
				current_matches >>= offset+1;
				c+=offset+1;
#elif defined(__arm__)
				if (masked[0]>0) {
					masked[0] >>= 8*(c+1);
					if (masked[0]==0) c=8;
					else c += offset+1;
				} else {
					masked[1] >>= 8*(offset-7);
					c += offset-7;
				}
#endif

				Bucket* bucket = &map.buckets[i*NUM_CONTROL_BYTES+c-1];
				if (bucket->first==k) {
					res.match=bucket;
					break;
				}
			}

#endif

			res.cont=cont;
			return res;
		}

		Bucket* insert() {
			for (c=0; c<NUM_CONTROL_BYTES; c++) {
				if (current[c]==0 || current[c]==SENTINEL) {
					current[c] = target;
					return &map.buckets[i*NUM_CONTROL_BYTES+c];
				}
			}

			return nullptr;
		}

		void remove() {
			current[c] = SENTINEL;
			map.count--;
		}
	};

	void resize(unsigned to) {
		unsigned long prev_sz = control_bytes.size();

		ControlBytes cbytes;
		cbytes.fill(0);
		control_bytes.resize(to, cbytes);

		buckets.resize(to*NUM_CONTROL_BYTES);

		for (unsigned i=0; i<prev_sz; i++) {
			ControlBytes& control = control_bytes[i];

			for (unsigned char c=0; c<NUM_CONTROL_BYTES; c++) {
				if (control[c]==SENTINEL) continue;

				if (control[c]) {
					Bucket& bucket = buckets[i*NUM_CONTROL_BYTES+c];
					size_t h = do_hash(bucket.first);

					if ((h>>8)%to >= prev_sz) {
						control[c] = SENTINEL;

						Probe p(*this, h);
						Bucket* insertion;

						while ((insertion=p.insert())==nullptr)
							++p;

						*insertion = bucket;
					}
				}
			}
		}
	}

	void check_resize() {
		if (load_factor()) {
			resize(control_bytes.size()*2);
		}
	}

 public:
	class FindIterator {
	 private:
		struct Cursor {
			Probe probe;
			K const& k;
		};

		std::optional<Cursor> cursor;

		FindIterator(Map& map, K const& k, size_t h): cursor({.probe=Probe(map, h), .k=k}) {
			++*this;
		}

	 public:
		using iterator_category = std::input_iterator_tag;
		using difference_type = void; //why the hell would i suggest this is doable?
		using value_type = V;
		using pointer = V*;
		using reference = V&;

		Bucket* bucket=nullptr;

		FindIterator() {}
		FindIterator(Map& map, K const& k): FindIterator(map, k, do_hash(k)) { }

		void operator++() {
			if (bucket) ++cursor->probe;

			MatchResult res;
			do {
				res = cursor->probe.match(cursor->k);

				if (res.match) {
					bucket=res.match;
					return;
				}

				++cursor->probe;
			} while (res.cont);

			bucket=nullptr;
		}

		void remove() {
			cursor->probe.remove();
		}

		bool operator==(FindIterator const& other) const {
			return other.bucket==bucket;
		}

		bool operator!=(FindIterator const& other) const {
			return other.bucket!=bucket;
		}

		V& operator*() const {
			return bucket->second;
		}

		V& operator->() const {
			return bucket->second;
		}
	};

	FindIterator find_begin(K const& k) {
		return FindIterator(*this, k);
	}

	static FindIterator find_end() {
		return FindIterator();
	}

	struct Iterator {
		using iterator_category = std::input_iterator_tag;
		using difference_type = void;
		using value_type = std::pair<K,V>;
		using pointer = std::pair<K,V>*;
		using reference = std::pair<K,V>&;

		size_t c;
		typename std::vector<ControlBytes, ControlBytesAllocator>::iterator iter;
		Map& map;

		void operator++() {
			if (++c % NUM_CONTROL_BYTES == 0 && c!=0) ++iter;

			while (c<map.buckets.size()) {
				do {
					if ((*iter)[c%NUM_CONTROL_BYTES]!=0 && (*iter)[c%NUM_CONTROL_BYTES]!=SENTINEL) {
						return;
					}
				} while (++c % NUM_CONTROL_BYTES != 0);

				++iter;
			}
		}

		bool operator==(Iterator const& other) const {
			return c==other.c;
		}

		bool operator!=(Iterator const& other) const {
			return c!=other.c;
		}

		void remove() {
			(*iter)[c%NUM_CONTROL_BYTES] = SENTINEL;
			map.count--;
		}

		std::pair<K,V>& operator*() {
			return map.buckets[c];
		}

		std::pair<K,V>* operator->() {
			return &map.buckets[c];
		}
	};

	//same thing except const
	struct ConstIterator {
		using iterator_category = std::input_iterator_tag;
		using difference_type = void;
		using value_type = std::pair<K,V>;
		using pointer = std::pair<K,V> const*;
		using reference = std::pair<K,V> const&;

		size_t c;
		typename std::vector<ControlBytes, ControlBytesAllocator>::const_iterator iter;
		Map const& map;

		void operator++() {
			if (++c % NUM_CONTROL_BYTES == 0 && c!=0) ++iter; //ha! c++! geddit?!1

			while (c<map.buckets.size()) {
				do {
					if ((*iter)[c%NUM_CONTROL_BYTES]!=0 && (*iter)[c%NUM_CONTROL_BYTES]!=SENTINEL) {
						return;
					}
				} while (++c % NUM_CONTROL_BYTES != 0);

				++iter;
			}
		}

		bool operator==(ConstIterator const& other) const {
			return c==other.c;
		}

		bool operator!=(ConstIterator const& other) const {
			return c!=other.c;
		}

		std::pair<K,V> const& operator*() {
			return map.buckets[c];
		}

		std::pair<K,V> const* operator->() {
			return &map.buckets[c];
		}
	};

	Iterator begin() {
		Iterator x = {.c=static_cast<size_t>(-1), .iter=control_bytes.begin(), .map=*this};
		++x;
		return x;
	}

	Iterator end() {
		return {.c=buckets.size(), .iter=control_bytes.end(), .map=*this};
	}

	void clear() {
		auto it = begin(), to=end();
		for (; it!=to; ++it) it.remove();
	}

	ConstIterator begin() const {
		ConstIterator x = {.c=static_cast<size_t>(-1), .iter=control_bytes.begin(), .map=*this};
		++x;
		return x;
	}

	ConstIterator end() const {
		return {.c=buckets.size(), .iter=control_bytes.end(), .map=*this};
	}
};

#endif //CORECOMMON_SRC_MAP_HPP_
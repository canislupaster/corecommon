#ifndef CORECOMMON_SRC_SORTEDMAP_HPP_
#define CORECOMMON_SRC_SORTEDMAP_HPP_

#include <vector>
#include <limits>
#include <functional>
#include <list>
#include <variant>
#include <optional>

#include "util.hpp"

template<class K, class V, class Compare=std::less_equal<const K>>
class SortedMap {
 private:
	using KV = std::pair<K,V>;

	struct Skip;
	struct Up;

	using SkipIter = typename std::list<Skip>::iterator;
	using KVIter = typename std::list<Up>::iterator;

	struct Skip {
		KV* kv;
		std::variant<SkipIter, KVIter> down;
		std::optional<SkipIter> up;
	};

	SkipIter center;

	std::vector<std::list<Skip>> skiplists;

	struct Up {
		KV* kv;
		std::optional<SkipIter> up;
	};

	std::list<Up> kvs;

	template<class DK>
	unsigned density(unsigned count, KVIter kv_iter) {
		return __builtin_ctz(count);
	}

	template<typename Iterator>
	bool locate(Iterator begin, Iterator end, Iterator* iter, K const& k) const {
		while (true) {
			if (k==(*iter)->kv->first) {
				return true;
			}

			bool cmp = Compare()((*iter)->kv->first, k);
			Iterator forward = *iter;
			std::advance(forward, 1);
			Iterator backward = *iter;
			std::advance(backward, -1);
			if (cmp && (forward==end || !Compare()(forward->kv->first, k))) {
				*iter = forward;
				return false;
			} else if (!cmp && (*iter==begin || Compare()(backward->kv->first, k))) {
				return false;
			}

			if (cmp) {
				*iter = forward;
			} else {
				*iter = backward;
			}
		}
	}

	struct LocationRec {
		bool match;
		std::vector<SkipIter> path;
		std::list<Skip>* current;
		KVIter kv_iter;
	};

	LocationRec locate_rec(K const& k, bool stop_on_match) {
		SkipIter cur = center;
		bool reset=false;
		std::vector<SkipIter> path;
		for (std::list<Skip>& skipl: skiplists) {
			if (reset) cur = skipl.begin();

			if (cur!=skipl.end()) {
				if (locate(skipl.begin(), skipl.end(), &cur, k) && stop_on_match) {
					return (LocationRec){.match=true, .current=&skipl, .path=path, .kv_iter=kvs.end()};
				}

				if (cur==skipl.end()) cur--;
				path.push_back(cur);
				cur = std::get<SkipIter>(cur->down);
				reset=false;
			} else {
				path.push_back(cur);
				reset=true;
			}
		}

		if (kvs.size()==0)
			return (LocationRec){.match=false, .current=nullptr, .kv_iter=kvs.end(), .path=path};

		KVIter iter = reset ? kvs.begin() : std::get<KVIter>(cur->down);
		bool match = locate(kvs.begin(), kvs.end(), &iter, k);
		return (LocationRec){.match=match, .current=nullptr, .kv_iter=iter, .path=path};
	}

 public:
	SortedMap(): kvs(), skiplists() {
		skiplists.emplace_back();
		center=skiplists[0].begin();
	}

	~SortedMap() {
		for (Up& up: kvs) {
			delete up.kv;
		}
	}

	void insert(K const& k, V const& v) {
		KV* new_kv = new std::pair(k,v);

		LocationRec rec = locate_rec(k, false);
		KVIter kv_iter = kvs.insert(rec.kv_iter, (Up){.kv=new_kv});
		std::variant<SkipIter, KVIter> down = std::variant<SkipIter, KVIter>(kv_iter);
		std::optional<SkipIter>* up = &kv_iter->up;

		unsigned d = density<K>(kvs.size(), kv_iter);

		while (d>skiplists.size()) {
			skiplists.insert(skiplists.begin(), std::list<Skip>());
			center = skiplists[0].begin();
		}

		for (unsigned i=skiplists.size()-1; i!=(unsigned)skiplists.size()-1-d; i--) {
			SkipIter insertion = skiplists[i].insert(rec.path[i], (Skip){.kv=new_kv, .down=down});
			*up = std::optional(insertion);
			down = std::variant<SkipIter, KVIter>(insertion);
			up = &insertion->up;
		}

		*up = std::optional<SkipIter>();
	}

	V* operator[](K const& k) {
		LocationRec rec = locate_rec(k, true);
		if (rec.match) {
			return &rec.kv_iter->kv->second;
		} else {
			return nullptr;
		}
	}

	bool remove(K const& k) {
		LocationRec rec = locate_rec(k, false);
		if (rec.loc.match) {
			do {
				std::optional<SkipIter> up = rec.kv_iter->up;
				auto skipl_iter = skiplists.end();
				while (up) {
					skipl_iter--;
					skipl_iter->erase(*up);
					up = (**up).up;
				}
			} while (rec.kv_iter->kv->first == k);

			return true;
		} else {
			return false;
		}
	}

	//TODO: iterator
};

#endif //CORECOMMON_SRC_SORTEDMAP_HPP_

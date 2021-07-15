#ifndef CORECOMMON_SRC_LOCKTABLE_HPP_
#define CORECOMMON_SRC_LOCKTABLE_HPP_

#include <mutex>
#include <vector>

template<class K>
class LockTable {
 private:
	std::vector<std::mutex> mutexes;

 public:
	using Lock = typename std::lock_guard<std::mutex>;

	Lock lock(K& k) {
		return std::lock_guard(mutexes[std::hash<K>()(k)%mutexes.size()]);
	}

	LockTable(unsigned size): mutexes(size, std::mutex()) {}
};

#endif //CORECOMMON_SRC_LOCKTABLE_HPP_

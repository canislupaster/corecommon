#ifndef CORECOMMON_SRC_SMALLVEC_HPP_
#define CORECOMMON_SRC_SMALLVEC_HPP_

#include <array>
#include <vector>

template<class T, size_t MinCapacity=1, template<class X> class Alloc=std::allocator>
class SmallVecAllocator {
 public:
	using value_type = T;
	using pointer = T*;
	using reference = T&;
	using const_pointer = T const*;
	using const_reference = T const&;
	using size_type = size_t;
	using difference_type = ptrdiff_t;

	std::array<T, MinCapacity> arr;
	Alloc<T> alloc;

	SmallVecAllocator() {}
	pointer allocate(size_type n) {
		return n <= MinCapacity ? arr.data() : alloc.allocate(n);
	}

	void deallocate(pointer p, size_type n) {
		if (p!=arr.data()) {
			alloc.deallocate(p, n);
		}
	}
};

template<class T, size_t MinCapacity=1, template<class X> class Alloc=std::allocator>
using SmallVec = std::vector<T, SmallVecAllocator<T, MinCapacity, Alloc>>;

#endif //CORECOMMON_SRC_SMALLVEC_HPP_

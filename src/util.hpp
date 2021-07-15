#ifndef CORECOMMON_SRC_UTIL_HPP_
#define CORECOMMON_SRC_UTIL_HPP_

#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

template<class Arg, class ...Args>
struct VarArgList {
	static const bool is_last=true;
	using First=Arg;
	template<template<class...> class T>
	using Rest = T<Args...>;
	template<template<class...> class T>
	using X = T<Arg, Args...>;
};

template<class Arg>
struct VarArgList<Arg> {
	static const bool is_last=true;
	using Last=Arg;
	template<template<class...> class T>
	using X = T<Arg>;
};

template<class T>
struct Identity {
	using type=T;
};

template<class T, size_t static_size>
class StaticSlice {
 public:
	using type = T;
	T* data;

	StaticSlice(T* data): data(data) {}

	size_t size() const {
		return static_size;
	}
};

template<class T>
class DynSlice {
 private:
	size_t variable_size;
 public:
	using type = T;
	T* data;

	DynSlice(T* data, size_t sz): data(data), variable_size(sz) {}
	DynSlice(): data(nullptr), variable_size(0) {}

	size_t size() const {
		return variable_size;
	}
};

template<class T>
class MaybeOwnedSlice {
 private:
	size_t variable_size;
 public:
	using type = T;
	T* data;
	bool owned;

	MaybeOwnedSlice(T* data, size_t sz, bool owned): data(data), variable_size(sz), owned(owned) {}
	MaybeOwnedSlice(): data(nullptr), variable_size(0), owned(false) {}

	void to_owned() {
		T* new_data = new T[variable_size];
		std::memcpy(new_data, data, variable_size*sizeof(T));

		data = new_data;
		owned=true;
	}

	size_t size() const {
		return variable_size;
	}

	~MaybeOwnedSlice() {
		if (owned) delete data;
	}
};

template<class SliceType>
class Slice {
 public:
	SliceType slice_type;

	Slice(SliceType slice): slice_type(slice) {}

	typename SliceType::type* data() {
		return slice_type.data;
	}

	typename SliceType::type* begin() {
		return slice_type.data;
	}

	typename SliceType::type* end() {
		return slice_type.data+slice_type.size();
	}

	size_t size() const {
		return slice_type.size();
	}

	DynSlice<typename SliceType::type> subslice(size_t start, size_t end) {
		return DynSlice(begin()+start, end-start);
	}

	typename SliceType::type& operator[](size_t i) {
		return *(begin()+i);
	}
};

class VarIntRef {
 public:
	struct VarInt {
		unsigned char first;
		unsigned char rest[];
	};

	VarInt const* vi;
	char size;

	VarIntRef(VarInt* var_vi, uint64_t x);
	VarIntRef(VarInt const* vi): vi(vi), size((vi->first>>5) & 7) {};
	VarIntRef(char const* vi): VarIntRef(reinterpret_cast<VarInt const*>(vi)) {};
	uint64_t value() const;
};

std::string read_file(const char* path);

unsigned gcd(unsigned a, unsigned b);
unsigned lcm(unsigned a, unsigned b);

#endif //CORECOMMON_SRC_UTIL_HPP_

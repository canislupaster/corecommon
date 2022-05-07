#ifndef CORECOMMON_SRC_UTIL_HPP_
#define CORECOMMON_SRC_UTIL_HPP_

#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>
#include <optional>

template<class T>
struct ConstReverseIterator {
	T const& t;
	using const_iterator = typename T::const_reverse_iterator;
	ConstReverseIterator(T const& t): t(t) {}
	typename T::const_reverse_iterator begin() const { return t.rbegin(); }
	typename T::const_reverse_iterator end() const { return t.rend(); }
};

template<class T>
struct ReverseIterator {
	T& t;
	using iterator = typename T::reverse_iterator;
	ReverseIterator(T& t): t(t) {}
	typename T::reverse_iterator begin() { return t.rbegin(); }
	typename T::reverse_iterator end() { return t.rend(); }
};

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

template<class T>
struct False {
	constexpr static bool value=false;
};

//copies when copied, otherwise holds ptr (assumed to be valid!)
template<class T>
struct Cow { //🐮
	bool owned;
	union {
		T x;
		T* x_ptr;
	};

	Cow(T const& x): x(x), owned(true) {}
	Cow(T&& x): x(std::move(x)), owned(true) {}
	Cow(T* x): x_ptr(x), owned(false) {}

	Cow(Cow const& cow): x(cow.owned ? cow.x : *cow.x_ptr), owned(true) {}

	T& operator*() {
		return owned ? x : *x_ptr;
	}

	T const& operator*() const {
		return owned ? x : *x_ptr;
	}

	T* operator->() {
		return owned ? &x : x_ptr;
	}

	T const* operator->() const {
		return owned ? &x : x_ptr;
	}

	~Cow() {
		if (owned) ~T(x);
	}
};

template<class T>
struct LazyInitialize {
	std::optional<T> t;
	std::function<T()> init;
	explicit LazyInitialize(std::function<T()> initializer): t(), init(initializer) {}

	T& operator*() {
		if (!t) {
			t = std::move(init());
		}

		return *t;
	}

	T* operator->() {
		return &*this;
	}
};

//ok, learned the hard way dont use in classes that arent noexcept constructible!
template<class T>
struct LateInitialize {
	union {
		T data;
		char buf[sizeof(T)];
	};

	T& operator*() {
		return data;
	}

	T* operator->() {
		return &data;
	}

	LateInitialize() {}

	//...
	LateInitialize& operator=(T const& t) {
		new(buf) T(t);
		return *this;
	}

	LateInitialize& operator=(T&& t) {
		new(buf) T(std::move(t));
		return *this;
	}

	template<class ...Args>
	LateInitialize& operator()(Args&&... args) {
		new(buf) T(std::forward<Args>(args)...);
		return *this;
	}

	~LateInitialize() {
		data.~T();
	}
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
size_t binomial(size_t n, size_t k);

char hexchar(char hex);
void charhex(unsigned char chr, char* out);

//for constructing an overloaded lambda
//eg. overloaded {
//            [](auto arg) { std::cout << arg << ' '; },
//            [](double arg) { std::cout << std::fixed << arg << ' '; },
//            [](const std::string& arg) { std::cout << std::quoted(arg) << ' '; }
//        }
//(from https://en.cppreference.com/w/cpp/utility/variant/visit)

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

#endif //CORECOMMON_SRC_UTIL_HPP_

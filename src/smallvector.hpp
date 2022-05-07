#ifndef CORECOMMON_SRC_SMALLVECTOR_HPP_
#define CORECOMMON_SRC_SMALLVECTOR_HPP_

#include <vector>
#include <array>

template<class T, size_t MinCapacity>
class SmallVector {
 private:
	template<class TPtr, class TRef, class SmallVecRef>
	class IteratorTemplate {
	 private:
		IteratorTemplate(TPtr p, SmallVecRef r): ptr(p), ref(r) {}
		friend SmallVector;

	 public:
		using iterator_category = std::random_access_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = T;
		using pointer = TPtr;
		using reference = TRef;

		TPtr ptr;
		SmallVecRef ref;

		template<class OtherTPtr, class OtherTRef, class OtherSmallVecRef>
		IteratorTemplate(IteratorTemplate<OtherTPtr, OtherTRef, OtherSmallVecRef> const& from): ptr(from.ptr), ref(from.ref) {}

		IteratorTemplate& operator=(IteratorTemplate const& other) {
			ptr=other.ptr;
			return *this;
		}

		IteratorTemplate& operator++() {
			ptr++;
			if (ptr==ref.arr.end()) ptr = ref.data;
			return *this;
		}

		IteratorTemplate& operator--() {
			if (ptr==ref.data) ptr = ref.arr.end()-1;
			else ptr--;
			return *this;
		}

		IteratorTemplate operator++(int) {
			if (ptr==ref.arr.end()-1) {
				ptr = ref.data;
				return IteratorTemplate(ref.arr.end()-1, ref);
			} else {
				return IteratorTemplate(ptr++, ref);
			}
		}

		IteratorTemplate operator--(int) {
			if (ptr==ref.data) {
				ptr = ref.arr.end()-1;
				return IteratorTemplate(ref.data, ref);
			} else {
				return IteratorTemplate(ptr--, ref);
			}
		}

		bool in_small_buf() const {
			return ptr>=ref.arr.begin() && ptr<ref.arr.end();
		}

		IteratorTemplate& operator+=(std::ptrdiff_t d) {
			if (in_small_buf() && (ref.arr.end()-ptr<=d)) ptr = ref.data+(d-(ref.arr.end()-ptr));
			else ptr+=d;
			return *this;
		}

		IteratorTemplate operator+(std::ptrdiff_t d) const {
			IteratorTemplate cpy = *this;
			cpy+=d;
			return cpy;
		}

		IteratorTemplate& operator-=(std::ptrdiff_t d) {
			if (!in_small_buf() && (ptr-ref.data<d)) ptr = (ref.arr.end()-d)+(ptr-ref.data);
			else ptr-=d;
			return *this;
		}

		IteratorTemplate operator-(std::ptrdiff_t d) const {
			IteratorTemplate cpy = *this;
			cpy-=d;
			return cpy;
		}

		std::ptrdiff_t operator-(IteratorTemplate const& other) {
			if (in_small_buf()) {
				return other.in_small_buf() ? ptr-other.ptr : -static_cast<std::ptrdiff_t>((ref.arr.end()-ptr)+(other.ptr-ref.data));
			} else {
				return other.in_small_buf() ? (ptr-ref.data)+(ref.arr.end()-other.ptr) : ptr-other.ptr;
			}
		}

		bool operator==(IteratorTemplate const& other) const {
			return ptr==other.ptr;
		}

		bool operator!=(IteratorTemplate const& other) const {
			return ptr!=other.ptr;
		}

		bool operator>=(IteratorTemplate const& other) const {
			bool a=in_small_buf(), b=other.in_small_buf();
			if (a==b) return ptr>=other.ptr;
			else return b;
		}

		bool operator>(IteratorTemplate const& other) const {
			bool a=in_small_buf(), b=other.in_small_buf();
			if (a==b) return ptr>other.ptr;
			else return b;
		}

		bool operator<=(IteratorTemplate const& other) const {
			return !(*this>other);
		}

		bool operator<(IteratorTemplate const& other) const {
			return !(*this>=other);
		}

		TRef operator*() {
			return *ptr;
		}

		TRef operator->() {
			return *ptr;
		}
	};

 public:
	std::array<T, MinCapacity> arr;
	T* data;
	size_t cap, len;

	SmallVector(std::initializer_list<T> il): len(il.size()) {
		if (il.size()<=MinCapacity) {
			std::move(il.begin(), il.end(), arr.begin());
			cap=0;
		} else {
			cap=il.size()-MinCapacity;
			data = new T[cap];
			std::move(il.begin(), il.begin()+MinCapacity, arr.begin());
			std::move(il.begin()+MinCapacity, il.end(), data);
		}
	}

	SmallVector(size_t n, T x): len(n) {
		if (n<=MinCapacity) {
			std::fill(arr.begin(), arr.begin()+n, x);
			cap=0;
		} else {
			cap=n-MinCapacity;
			data = new T[cap];
			std::fill(arr.begin(), arr.end(), x);
			std::fill(data, data+cap, x);
		}
	}

	void reserve(size_t n) {
		if (n<=cap+MinCapacity) return;
		n-=MinCapacity;
		T* new_data = new T[n];
		std::copy(data, data+cap, new_data);

		if (cap>0) delete data;
		data = new_data;
		cap=n;
	}

	SmallVector(size_t n): len(0), cap(0) {
		reserve(n);
	}

	SmallVector(SmallVector const& other): SmallVector(other.len) {
		len=other.len;
		std::copy(other.begin(), other.end(), begin());
	}

	using iterator = IteratorTemplate<T*, T&, SmallVector&>;
	using const_iterator = IteratorTemplate<T const*, T const&, SmallVector const&>;

	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

 private:
	iterator deconst(const_iterator x) {
		return iterator(const_cast<T*>(x.ptr), *this);
	}

	iterator make_space(const_iterator where, size_t count) {
		len+=count;

		if (where.in_small_buf()) {
			reserve(len);
			iterator cpy = deconst(where);
			std::move_backward(cpy, end()-count, end());

			return cpy;
		} else {
			size_t off = where.ptr-data;
			reserve(len);
			if (off>=len-count-MinCapacity) return iterator(data+off, *this);
			std::move(data+off, data+off+count, data+off+count);
			return iterator(data+off, *this);
		}
	}

 public:
	iterator begin() {
		return iterator(arr.begin(), *this);
	}

	iterator end() {
		return iterator(len>=MinCapacity ? data+len-MinCapacity : arr.begin()+len, *this);
	}

	const_iterator begin() const {
		return const_iterator(arr.begin(), *this);
	}

	const_iterator end() const {
		return const_iterator(len>=MinCapacity ? data+len-MinCapacity : arr.begin()+len, *this);
	}

	reverse_iterator rbegin() {
		return std::make_reverse_iterator(end());
	}

	reverse_iterator rend() {
		return std::make_reverse_iterator(begin());
	}

	const_reverse_iterator rbegin() const {
		return std::make_reverse_iterator(end());
	}

	const_reverse_iterator rend() const {
		return std::make_reverse_iterator(begin());
	}

	SmallVector(SmallVector&& other): cap(other.cap), len(other.len) {
		data = other.data;
		other.cap=0;
		other.len=MinCapacity;
		std::move(other.arr.begin(), other.arr.begin()+std::min(other.len, MinCapacity), arr.begin());
	}

	SmallVector& swap(SmallVector& other) {
		std::swap(data, other.data);
		std::swap(cap, other.cap);
		std::swap(other.len, len);
		std::swap(other.arr, arr);
		return *this;
	}

	SmallVector& operator=(SmallVector other) {
		return swap(other);
	}

	T& operator[](size_t i) {
		if (i>=MinCapacity) return *(data+i-MinCapacity);
		else return *(arr.begin()+i);
	}

	T const& operator[](size_t i) const {
		if (i>=MinCapacity) return *(data+i-MinCapacity);
		else return *(arr.begin()+i);
	}

	void clear() {
		len=0;
	}

	iterator insert(const_iterator where, T const& what) {
		iterator x = make_space(where, 1);
		*x = what;
		return x;
	}

	iterator insert(const_iterator where, size_t count, T const& what) {
		iterator x = make_space(where, count);
		std::fill(x, x+count, what);
		return x;
	}

	template<class InputIt>
	typename std::enable_if<!std::is_same<typename std::iterator_traits<InputIt>::value_type, void>::value, iterator>::type
	insert(const_iterator where, InputIt start, InputIt end) {
		iterator x = make_space(where, end-start);
		std::copy(start, end, x);
		return x;
	}

	iterator insert(const_iterator where, std::initializer_list<T> il) {
		iterator x = make_space(where, il.size());
		std::copy(il.begin(), il.end(), x);
		return x;
	}

	template<class... Args>
	iterator emplace(const_iterator where, Args&&... args) {
		iterator x = make_space(where, 1);
		if (!std::is_trivially_destructible<T>::value) (*x).~T();
		::new (x.ptr) T(std::move<Args...>(args...));
		return x;
	}

	iterator erase(const_iterator start, const_iterator end) {
		size_t count = end-start;

		std::move(end, const_iterator(this->end()), deconst(start));

		if (!std::is_trivially_destructible<T>::value) {
			iterator cpy = deconst(start+(const_iterator(this->end())-end));
			for (; cpy!=this->end(); cpy++) (*cpy).~T();
		}

		len -= count;
		return deconst(start);
	}

	iterator erase(const_iterator x) {
		return erase(x, x+1);
	}

	void resize(size_t n) {
		if (n>len) {
			reserve(n);
			len=n; //potentially undefined, bc im too lazy to figure out how to ensure it is default-insertible
		} else {
			erase(begin()+n, end());
		}
	}

	void push_back(T const& v) {
		insert(end(), v);
	}

	template<class ...Args>
	void emplace_back(Args... args) {
		emplace<Args...>(end(), args...);
	}

	void pop_back() {
		erase(end()-1, end());
	}

	size_t size() const {
		return len;
	}

	T& front() {
		return arr.front();
	}

	T& back() {
		return *(end()-1);
	}

	T const& front() const {
		return arr.front();
	}

	T const& back() const {
		return *(end()-1);
	}

	~SmallVector() {
		if (!std::is_trivially_destructible<T>::value) {
			for (T& x: *this) {
				x.~T();
			}
		}

		if (cap>0) delete data;
	}
};

#endif //CORECOMMON_SRC_SMALLVECTOR_HPP_

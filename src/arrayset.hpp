#ifndef CORECOMMON_SRC_ARRAYSET_HPP_
#define CORECOMMON_SRC_ARRAYSET_HPP_

#include <array>

#include "util.hpp"

template<class T>
struct ArraySet {
	std::vector<T> vec;
	size_t elements;
	ArraySet(size_t elements, size_t len): elements(elements), vec(binomial(len, elements)) {}

	typename std::vector<T>::reference operator[](size_t* idx) {
		std::vector<size_t> elem_vec(idx, idx+elements);
		std::sort(elem_vec.begin(), elem_vec.end());
		std::reverse(elem_vec.begin(), elem_vec.end());
		size_t out_i=0;

		for (size_t i=0; i<elements; i++) {
			out_i += binomial(elem_vec[i], elements-i);
		}

		return vec[out_i];
	}

	struct Iterator {
		std::vector<size_t> elem_vec;
		typename std::vector<T>::iterator t;
		ArraySet<T>& ref;

		std::pair<std::vector<size_t>&, typename std::vector<T>::reference> operator*() {
			return std::pair<std::vector<size_t>&, typename std::vector<T>::reference>(elem_vec, *t);
		}

		void operator++() {
			for (size_t i=elem_vec.size()-1; i!=-1; i--) {
				if (i==0 || elem_vec[i]<elem_vec[i-1]-1) {
					elem_vec[i]++;

					for (size_t j = i+1; j < elem_vec.size(); j++) {
						elem_vec[j] = j-i-1;
					}

					break;
				}
			}

			t++;
			if (ref[elem_vec.data()]!=*t) {
				throw std::runtime_error("oops, throw computer in ditch");
			}
		}

		bool operator==(Iterator const& other) {
			return other.t==t;
		}

		bool operator!=(Iterator const& other) {
			return other.t!=t;
		}
	};

	Iterator begin() {
		Iterator iter {.t=vec.begin(), .elem_vec=std::vector<size_t>(elements, 0), .ref=*this};
		for (size_t i = 0; i < elements; i++) {
			iter.elem_vec[i] = elements-i-1;
		}

		return iter;
	}

	Iterator end() {
		return {.t=vec.end(), .ref=*this};
	}
};

#endif //CORECOMMON_SRC_ARRAYSET_HPP_

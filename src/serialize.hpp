#ifndef CORECOMMON_SRC_SERIALIZE_HPP_
#define CORECOMMON_SRC_SERIALIZE_HPP_

#include <iostream>
#include <sstream>
#include "util.hpp"

struct UnserializableType: public std::exception {
	char const* what() const noexcept {
		return "could not serialize type";
	}
};

template<class Output=std::ostream&>
class Serialize {
	Output out;
 public:
	template<class T>
	void write(T const& x) {
		throw UnserializableType();
	}

	template<>
	void write<int>(int const& x) {
		int net = htonl(x);
		out.write(reinterpret_cast<char*>(&net), sizeof(int));
	}

	template<>
	void write<long>(long const& x) {
		long net = htonll(x);
		out.write(reinterpret_cast<char*>(&net), sizeof(long));
	}

	template<>
	void write<short>(short const& x) {
		short net = htons(x);
		out.write(reinterpret_cast<char*>(&net), sizeof(short));
	}

	template<>
	void write<unsigned>(unsigned const& x) {
		unsigned net = htonl(x);
		out.write(reinterpret_cast<char*>(&net), sizeof(unsigned));
	}

	template<>
	void write<unsigned long>(unsigned long const& x) {
		unsigned long net = htonll(x);
		out.write(reinterpret_cast<char*>(&net), sizeof(unsigned long));
	}

	template<>
	void write<char*>(char* const& x) {
		out.write(x, strlen(x)+1);
	}

	template<>
	void write<std::string>(std::string const& x) {
		out.write(x.data(), x.length());
		out.write("\0", 1);
	}

	template<class T>
	void write_vector(std::vector<T> vec, std::function<void(Serialize<Output>&, T&)> f) {
		write<unsigned long>(vec.size());
		for (T& t: vec) {
			f(*this, t);
		}
	}

	explicit Serialize(Output out): out(out) {}
};

template<class Input=std::istream&>
class Deserialize {
 private:
	Input in;
 public:
	template<class T, class ReadInput=Input>
	T read() {
		throw UnserializableType();
	}

	template<>
	int read<int>() {
		int net;
		in.read(reinterpret_cast<char*>(&net), sizeof(int));
		return ntohl(net);
	}

	template<>
	long read<long>() {
		long net;
		in.read(reinterpret_cast<char*>(&net), sizeof(long));
		return ntohll(net);
	}

	template<>
	short read<short>() {
		short net;
		in.read(reinterpret_cast<char*>(&net), sizeof(short));
		return ntohs(net);
	}

	template<>
	unsigned read<unsigned>() {
		unsigned net;
		in.read(reinterpret_cast<char*>(&net), sizeof(unsigned));
		return ntohl(net);
	}

	template<>
	unsigned long read<unsigned long>() {
		unsigned long net;
		in.read(reinterpret_cast<char*>(&net), sizeof(unsigned long));
		return ntohll(net);
	}

	template<>
	std::string read<std::string, std::ostream&>() {
		std::ostringstream stream;
		stream<<in.rdbuf();
		return stream.str();
	}

	template<>
	std::string read<std::string>() {
		return in.read_string();
	}

	template<class T>
	std::vector<T> read_vector(std::function<T(Deserialize<Input>&)> f) {
		unsigned long len = read<unsigned long>();
		std::vector<T> vec(len);
		for (unsigned long i=0; i<len; i++) {
			vec[i] = f(*this);
		}
	}

	explicit Deserialize(Input in): in(in) {}
};

#endif //CORECOMMON_SRC_SERIALIZE_HPP_

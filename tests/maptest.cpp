#include <string>
#include <chrono>
#include <fstream>

#include "map.hpp"

using namespace std::chrono;

std::string rand_string(unsigned size) {
	const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJK...";
	std::string s;

	for (unsigned n=0; n<size; n++) {
		int key = rand() % (int) (strlen(charset) - 1);
		s.push_back(charset[key]);
	}

	return s;
}


int main(int argc, char** argv) {
	srand(47210);

	Map<std::string, int> map;

	std::array<std::string, 20> start;

	for (std::string& i : start) {
		std::string k = rand_string(10);
		int x = rand();
		map.insert(k, x);

		i=k;
	}

	std::ofstream out("./maptest_data.dat", std::ofstream::out);

	for (unsigned n=1; n<10000; n++) {
		std::string k2 = rand_string(10);
		int x2 = rand();
		map.insert(k2, x2);

		int const* ret;

		time_point tp = high_resolution_clock::now();

		for (unsigned i=0; i<100; i++) {
			for (std::string& k: start) {
				ret = map[k];
				if (!ret) return 1;
			}
		}

		unsigned ms = duration_cast<nanoseconds>(high_resolution_clock::now()-tp).count();
		out << n << " " << ms << std::endl;

		int const* ret2 = map[k2];

		if (!ret2 || *ret2!=x2) {
			return 1;
		}
	}

	return 0;
}
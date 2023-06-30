#include <string>
#include <chrono>
#include <fstream>
#include <set>
#include <cassert>

#include "map.hpp"

std::string rand_string(unsigned size) {
	const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJK...";
	std::string s;

	for (unsigned n=0; n<size; n++) {
		int key = rand() % (int) (strlen(charset) - 1);
		s.push_back(charset[key]);
	}

	return s;
}

int main() {
	Map<std::string, std::monostate> m;
	std::vector<std::string> ins;
	std::set<std::string> ins_set;

	while (ins.size()<1000) {
		std::string r = rand_string(15);
		if (m[r]!=nullptr) continue;

		m.upsert(r);
		ins.push_back(r);
		ins_set.insert(r);
	}

	assert(m.count==ins.size());
	for (std::string s: ins) {
		unsigned cnt=0;
		for (auto fit=m.find_begin(s); fit!=m.find_end(); ++fit) {
			cnt++;
			assert(cnt<=1);
		}

		assert(m[s]!=nullptr);
	}

	auto it = m.begin();
	for (; it!=m.end(); ++it) {
		assert(ins_set.find(it->first)!=ins_set.end());
	}

}
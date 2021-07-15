#include "sortedmap.hpp"

int main(int argc, char** argv) {
	SortedMap<int, int> smap;
	smap.insert(1, 2);
	smap.insert(2, 3);
	int* v = smap[1];
	int* v2 = smap[2];

	return 0;
}
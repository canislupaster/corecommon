#include <iostream>

#include "numeric.hpp"

int main(int argc, char** argv) {
	Digital x(100);
	Digital y(3);
	x /= y;
	std::cout << x << std::endl;

	return 0;
}
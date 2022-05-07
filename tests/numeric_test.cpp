#include <iostream>

#include "numeric.hpp"

int main(int argc, char** argv) {
	Rational x(Digital(21849),Digital(182));
	std::cout << x << std::endl;
	x.round(5);

	std::cout << x << std::endl;
	std::cout << Digital(127) << std::endl;

	return 0;
}
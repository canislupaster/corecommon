#include "config.hpp"

int main(int argc, char** argv) {
	Config cfg;
	cfg.parse("x=1\r\ny = 2");
	return 0;
}
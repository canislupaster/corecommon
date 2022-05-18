#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
	std::ifstream i(argv[1]);
	std::string x((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
	std::string delim = ")\"";
	while (x.find(delim)!=std::string::npos) delim.insert(delim.begin()+1, 'x');
	std::cout << "R\"" << std::string(delim.begin()+1, delim.end()-1) << '(' << x << delim;
	i.close();
	return 0;
}
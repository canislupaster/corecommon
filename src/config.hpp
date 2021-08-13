#ifndef SRC_CONFIG_HPP_
#define SRC_CONFIG_HPP_

#include <string>
#include <sstream>
#include <variant>

#include "map.hpp"

struct Config {
	using Variant = std::variant<float, std::string, long, bool>;

	struct Value {
		bool is_default;
		Variant var;
	};

	Map<std::string, Value> map;
	void parse(const std::string& str);
	std::stringstream save() const;

	Config() = default;
};

#endif //SRC_CONFIG_HPP_

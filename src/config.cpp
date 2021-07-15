#include "config.hpp"
#include "parser.hpp"

void Config::parse(std::string const& str) {
	Parser<Unit> parser(str);

	auto newline = Many(0,1,Match("\r") || Match("\n"));
	auto parse_string = (Match("\"") + ParseString(Many((Match("\\") + Any()) || (!Match("\""))))).skip(Match("\"") + newline);
	auto sep = Many(ParseWS()) + Match("=") + Many(ParseWS());

	auto parsed =
					(Multiple(TupleMap(
									Many(ParseWS()) + ParseString(Many(!LookAhead(sep) + Any())).skip(sep),

									((ParseFloat().skip(newline) + ResultMap<float, Value>([](float x){ return Value {.is_default=false, .var=Variant(x)}; }))
									 || (ParseInt().skip(newline) + ResultMap<long, Value>([](long x){ return Value {.is_default=false, .var=Variant(x)}; }))

									 || ((Match("true") + ResultMap<Unit, Value>([](auto x){ return Value {.is_default=false, .var=Variant(true)}; })
									      || Match("false") + ResultMap<Unit, Value>([](auto x){ return Value {.is_default=false, .var=Variant(false)}; })))
									      .skip(newline)

									 || (parse_string + ResultMap<std::string, Value>([](std::string x){ return Value {.is_default=false, .var=Variant(x)}; })))
					)).skip(ParseEOF())).run(parser);

	for (std::tuple<std::string, Value> const& val: parsed.res) {
		map.insert(std::get<0>(val), std::get<1>(val));
	}
}

std::stringstream Config::save() {
	//TODO:
	return std::stringstream(); //TROLLOLOLOL
}

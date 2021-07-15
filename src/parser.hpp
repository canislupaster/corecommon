#ifndef SRC_PARSER_HPP_
#define SRC_PARSER_HPP_

#include <cstring>
#include <cstdlib>
#include <functional>
#include <string>
#include <sstream>
#include <optional>
#include <tuple>
#include <vector>

#include "util.hpp"

//empty type for maps which are stateless/resultless
using Unit = std::tuple<>;

enum class ParseStatus {
	None,
	Expected,
	Unexpected,
	EndOfFile,
	Value
};

struct ParserSpan {
	char const* start;
	char const* text;
	unsigned length;

	ParserSpan(char const* text): start(text), text(text), length(strlen(text)) {}
	ParserSpan(ParserSpan from, unsigned length): start(from.start), text(from.text), length(length) {}

	struct LineCol {
		unsigned line;
		unsigned col;
	};

	LineCol line_col() const;
	void operator+=(unsigned n);
};

std::ostream& operator<<(std::ostream& ostream, ParserSpan const& span);

template<class Result=std::tuple<>>
class Parser {
 public:
	Parser(char const* text): span(text), res(), stat(ParseStatus::None), err(false) {}
	Parser(std::string const& str): Parser(str.c_str()) {}
	Parser(char const* text, Result default_res): span(text), res(default_res), stat(ParseStatus::None), err(false) {}

	template<class OldResult>
	Parser(Parser<OldResult>& parser, Result res): span(parser.span), res(res), stat(ParseStatus::None), err(false) {}

	Parser(Parser<Result>& parser):
					res(parser.res), span(parser.span), stat(parser.stat), err(parser.err), expected(parser.expected) {}

	template<class OldResult>
	Parser(Parser<OldResult>& parser):
		span(parser.span), stat(parser.stat), err(parser.err), expected(parser.expected) {}

	ParserSpan span;
	Result res;

	bool err;
	ParseStatus stat;
	char const* expected;

	std::string err_string() const {
		switch (stat) {
			case ParseStatus::Expected:
				return (std::stringstream() << "expected \"" << expected << "\", got " << this->span << ".").str();
			case ParseStatus::Unexpected:
				return (std::stringstream() << "unexpected \"" << expected << "\" in " << this->span << ".").str();
			case ParseStatus::EndOfFile:
				return (std::stringstream() << "unexpected EOF at " << this->span << ".").str();
			case ParseStatus::Value:
				return (std::stringstream() << "error parsing value at " << this->span << ".").str();
			case ParseStatus::None:
				return std::string("nondescript error");
		}
	}
};

//a curious recurrence!
template<class T, class FromArg, class ToArg>
class ParseMap;

template<class InnerMap>
struct Not;

template<class Left, class Right>
struct Chain;

template<class SkipMap, class Result>
struct Skip;

template<class Left, class Right>
struct Or;

template<class T, class FromArg, class ToArg>
class ParseMap {
public:
	using From = FromArg;
	using To = ToArg;

	Parser<To> operator()(Parser<From>& parser) const {
		return static_cast<T const*>(this)->run(parser);
	}

	Not<T> operator!() const {
		return Not<T> {.inner = *static_cast<T const*>(this)};
	}

	template<class Right>
	Chain<T, Right> operator+(Right rhs) const {
		return Chain<T, Right> {.left = *static_cast<T const*>(this), .right=rhs};
	}

	template<class SkipMap>
	Chain<T, Skip<To, SkipMap>> skip(SkipMap skip) const {
		return *this + Skip<To, SkipMap>(skip);
	}

	template<class Right>
	Or<T, Right> operator||(Right rhs) const {
		return Or<T, Right> {.left = *static_cast<T const*>(this), .right=rhs};
	}
};


template<class From, class To>
struct ResultMap: public ParseMap<ResultMap<From, To>, From, To> {
	const std::function<To(From)> resmap;

	ResultMap(std::function<To(From)> resmap): resmap(resmap) {}

	Parser<To> run(Parser<From>& parser) const {
		return Parser<To>(parser, resmap(parser.res));
	}
};

template<class OldResult>
struct Ignore: public ParseMap<Ignore<OldResult>, OldResult, Unit> {
	Parser<Unit> run(Parser<OldResult>& parser) const {
		return Parser<Unit>(parser);
	}
};

template<class Head, class ...Rest>
struct TupleMap: public ParseMap<TupleMap<Head, Rest...>, typename Head::From, std::tuple<typename Head::To, typename Rest::To...>> {
	const Head head;
	const TupleMap<Rest...> rest;

	TupleMap(Head head, Rest... rest): head(head), rest(rest...) {}

	Parser<typename TupleMap<Head, Rest...>::To> run(Parser<typename Head::From>& parser) const {
		Parser<typename Head::To> head_parser = head(parser);

		if (head_parser.err)
			return Parser<typename TupleMap<Head, Rest...>::To>(head_parser);

		parser.span = head_parser.span;
		Parser<typename TupleMap<Rest...>::To> rest_parser = rest(parser);

		if (rest_parser.err)
			return Parser<typename TupleMap<Head, Rest...>::To>(rest_parser);

		return Parser<typename TupleMap<Head, Rest...>::To>(rest_parser, std::tuple_cat(std::tuple(head_parser.res), rest_parser.res));
	}
};

template<class Head>
struct TupleMap<Head>: public ParseMap<TupleMap<Head>, typename Head::From, std::tuple<typename Head::To>> {
	const Head head;

	TupleMap(Head head): head(head) {}

	Parser<std::tuple<typename Head::To>> run(Parser<typename Head::From>& parser) const {
		Parser<typename Head::To> head_parser = head(parser);
		if (head_parser.err) return Parser<std::tuple<typename Head::To>>(head_parser);
		return Parser(head_parser, std::tuple(head_parser.res));
	}
};

template<class InnerMap>
struct Not: public ParseMap<Not<InnerMap>, Unit, Unit> {
	const InnerMap inner;

	Parser<Unit> run(Parser<Unit>& parser) const {
		Parser<Unit> to_parser = inner(parser);

		parser.span = to_parser.span;

		if (to_parser.stat == ParseStatus::Expected) parser.stat = ParseStatus::Unexpected;
		else if (to_parser.stat == ParseStatus::Unexpected) parser.stat = ParseStatus::Expected;

		parser.err = !to_parser.err;

		return parser;
	}
};

template<class InnerMap>
struct LookAhead: public ParseMap<LookAhead<InnerMap>, typename InnerMap::From, typename InnerMap::To> {
	const InnerMap inner;
	LookAhead(InnerMap inner): inner(inner) {}

	Parser<typename InnerMap::To> run(Parser<typename InnerMap::From>& parser) const {
		Parser<typename InnerMap::To> inner_parser = inner(parser);
		inner_parser.span = parser.span;
		return inner_parser;
	}
};

template<class Left, class Right>
struct Chain: public ParseMap<Chain<Left, Right>, typename Left::From, typename Right::To> {
	const Left left;
	const Right right;

	Parser<typename Right::To> run(Parser<typename Left::From>& parser) const {
		Parser<typename Left::To> left_parser = left(parser);
		if (left_parser.err) return Parser<typename Right::To>(left_parser);

		return right(left_parser);
	}
};

template<class Result, class SkipMap>
struct Skip: public ParseMap<Skip<Result, SkipMap>, Result, Result> {
	const SkipMap skip;

	Skip(SkipMap skip): skip(skip) {}

	Parser<Result> run(Parser<Result>& parser) const {
		Parser<Unit> unit_parser(parser, Unit());
		Parser<Unit> skip_parser = skip(unit_parser);

		if (skip_parser.err) return Parser<Result>(skip_parser);
		parser.span = skip_parser.span;

		return parser;
	}
};

template<class InnerMap>
struct Multiple: public ParseMap<Multiple<InnerMap>, typename InnerMap::From, std::vector<typename InnerMap::To>> {
	const size_t min, max;
	const InnerMap inner;

	Multiple(size_t min, size_t max, InnerMap inner): min(min), max(max), inner(inner) {}
	Multiple(InnerMap inner): min(0), max(std::numeric_limits<size_t>::max()), inner(inner) {}

	Parser<std::vector<typename InnerMap::To>> run(Parser<typename InnerMap::From>& parser) const {
		std::vector<typename InnerMap::To> vec(min);

		for (unsigned i=0; i<max; i++) {
			Parser<typename InnerMap::To> inner_parser = inner(parser);

			if (inner_parser.err) {
				if (i<min) return Parser<std::vector<typename InnerMap::To>>(inner_parser);
				else return Parser<std::vector<typename InnerMap::To>>(parser, vec);
			}

			parser.span = inner_parser.span;
			vec.push_back(inner_parser.res);
		}

		return Parser(parser, vec);
	}
};

template<class InnerMap>
struct Many: public ParseMap<Many<InnerMap>, typename InnerMap::From, Unit> {
	const size_t min, max;
	const InnerMap inner;

	Many(size_t min, size_t max, InnerMap inner): min(min), max(max), inner(inner) {}
	Many(InnerMap inner): min(0), max(std::numeric_limits<size_t>::max()), inner(inner) {}

	Parser<Unit> run(Parser<typename InnerMap::From>& parser) const {
		for (unsigned i=0; i<max; i++) {
			Parser<typename InnerMap::To> inner_parser = inner(parser);

			if (inner_parser.err) {
				if (i<min) return Parser<Unit>(inner_parser);
				else return Parser<Unit>(parser, Unit());
			}

			parser.span = inner_parser.span;
		}

		return Parser<Unit>(parser, Unit());
	}
};

template<class Left, class Right>
struct Or: public ParseMap<Or<Left, Right>, typename Left::From, typename Left::To> {
	const Left left;
	const Right right;

	Parser<typename Left::To> run(Parser<typename Left::From>& parser) const {
		Parser<typename Left::To> left_parser = left(parser);
		if (!left_parser.err) return left_parser;

		return right(parser);
	}
};

template<class InnerMap>
struct ParseString: public ParseMap<ParseString<InnerMap>, typename InnerMap::From, std::string> {
	InnerMap inner;
	ParseString(InnerMap inner): inner(inner) {}

	Parser<std::string> run(Parser<typename InnerMap::From>& parser) const {
		char const* start = parser.span.text;
		Parser<typename InnerMap::To> inner_parser = inner(parser);
		return Parser<std::string>(inner_parser, std::string(start, inner_parser.span.text - start));
	}
};

struct Any: public ParseMap<Any, Unit, Unit> {
	Parser<Unit> run(Parser<Unit>& parser) const {
		parser.stat = ParseStatus::Expected;
		parser.expected = "anything";

		if (parser.span.length==0) {
			parser.err=true;
		} else {
			parser.span+=1;
		}

		return parser;
	}
};

struct ParseEOF: public ParseMap<ParseEOF, Unit, Unit> {
	Parser<Unit> run(Parser<Unit>& parser) const {
		parser.stat = ParseStatus::Expected;
		parser.expected = "end of input";

		if (parser.span.length!=0) {
			parser.err=true;
		}

		return parser;
	}
};

struct ParseWS: public ParseMap<ParseWS, Unit, Unit> {
	Parser<Unit> run(Parser<Unit>& parser) const {
		parser.stat = ParseStatus::Expected;
		parser.expected = "whitespace";

		if (parser.span.length==0 || strchr("\r\n ", *parser.span.text)==nullptr) {
			parser.err=true;
		} else {
			parser.span+=1;
		}

		return parser;
	}
};

struct Match: public ParseMap<Match, Unit, Unit> {
	char const* match;
	Match(char const* match): match(match) {}

	Parser<Unit> run(Parser<Unit>& parser) const {
		parser.stat = ParseStatus::Expected;
		parser.expected = match;

		if (parser.span.length<strlen(match) || strncmp(parser.span.text, match, strlen(match))!=0) {
			parser.err=true;
		} else {
			parser.span += strlen(match);
		}

		return parser;
	}
};

struct ParseInt: public ParseMap<ParseInt, Unit, long> {
	Parser<long> run(Parser<Unit>& parser) const {
		char* end;
		long x = strtol(parser.span.text, &end, 10);

		parser.stat = ParseStatus::Value;

		if (end==parser.span.text) {
			parser.err=true;
			return Parser<long>(parser);
		}

		parser.span.length -= end-parser.span.text;
		parser.span.text = end;

		return Parser(parser, x);
	}
};

struct ParseFloat: public ParseMap<ParseFloat, Unit, float> {
	Parser<float> run(Parser<Unit>& parser) const {
		char* end;
		float x = strtof(parser.span.text, &end);

		parser.stat = ParseStatus::Value;

		if (end==parser.span.text) {
			parser.err=true;
			return Parser<float>(parser);
		}

		parser.span.length -= end-parser.span.text;
		parser.span.text = end;

		return Parser(parser, x);
	}
};

#endif //SRC_PARSER_HPP_

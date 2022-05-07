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
	Error,
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
	Parser(Parser<OldResult> const& parser, Result res): span(parser.span), res(res), stat(ParseStatus::None), err(false) {}

	template<class OldResult>
	explicit Parser(Parser<OldResult> const& parser):
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
struct Many;

struct Match;

template<class Result>
struct Ignore;

template<class InnerMap>
struct Not;

template<class Left, class Right>
struct Chain;

template<class To, class InnerMap>
struct Combine;

template<class SkipMap, class Result>
struct Skip;

template<class Left, class Right>
struct Or;

template<class T, class FromArg, class ToArg>
class ParseMap {
public:
	using From = FromArg;
	using To = ToArg;

	size_t length(Parser<From> const& parser) const {
		Parser<To> to = static_cast<T const*>(this)->run(parser);
		return to.span.start - parser.span.start;
	}

	Parser<To> operator()(Parser<From> const& parser) const {
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

	Many<T> maybe() const {
		return Many(0,1,*static_cast<T const*>(this));
	}

	template<class Sep>
	Chain<Combine<From, T>, Many<Chain<Skip<From, Sep>, T>>> separated(Sep sep) const {
		T const* x = static_cast<T const*>(this);
		return Combine<From, T>(*x) + Many(Skip<From, Sep>(sep) + *x);
	}

	template<class Right>
	Or<T, Right> operator||(Right rhs) const {
		return Or<T, Right> {.left = *static_cast<T const*>(this), .right=rhs};
	}
};

template<class From, class To>
struct LazyMap: public ParseMap<LazyMap<From, To>, From, To> {
	const std::function<Parser<To>(Parser<From> const&)> lzmap;

	LazyMap(std::function<Parser<To>(Parser<From> const&)> lazymap): lzmap(lazymap) {}

	Parser<To> run(Parser<From> const& parser) const {
		//:#
		return lzmap(parser);
	}
};

template<class From, class To>
struct ResultMap: public ParseMap<ResultMap<From, To>, From, To> {
	const std::function<To(From)> resmap;

	ResultMap(std::function<To(From)> resmap): resmap(resmap) {}
	ResultMap(To x): resmap([x](From from){return x;}) {}

	Parser<To> run(Parser<From> const& parser) const {
		return Parser<To>(parser, resmap(parser.res));
	}
};

template<class From>
struct ErrorMap: public ParseMap<ErrorMap<From>, From, From> {
	const std::function<bool(From)> cond;

	ErrorMap(std::function<bool(From)> cond): cond(cond) {}

	Parser<From> run(Parser<From> const& parser) const {
		if (cond(parser.res)) {
			Parser<From> new_parser = parser;
			new_parser.err = true;
			new_parser.stat = ParseStatus::Error;
			return new_parser;
		}

		return parser;
	}
};

template<class OldResult>
struct Ignore: public ParseMap<Ignore<OldResult>, OldResult, Unit> {
	Parser<Unit> run(Parser<OldResult> const& parser) const {
		return Parser<Unit>(parser);
	}
};

template<class Head, class ...Rest>
struct TupleMap: public ParseMap<TupleMap<Head, Rest...>, typename Head::From, std::tuple<typename Head::To, typename Rest::To...>> {
	const Head head;
	const TupleMap<Rest...> rest;

	TupleMap(Head head, Rest... rest): head(head), rest(rest...) {}

	Parser<typename TupleMap<Head, Rest...>::To> run(Parser<typename Head::From> const& parser) const {
		Parser<typename Head::To> head_parser = head(parser);
		if (head_parser.err)
			return Parser<typename TupleMap<Head, Rest...>::To>(head_parser);

		Parser<typename TupleMap<Rest...>::To> rest_parser = rest(head_parser);
		if (rest_parser.err)
			return Parser<typename TupleMap<Head, Rest...>::To>(rest_parser);

		return Parser<typename TupleMap<Head, Rest...>::To>(rest_parser, std::tuple_cat(std::tuple(head_parser.res), rest_parser.res));
	}
};

template<class Head>
struct TupleMap<Head>: public ParseMap<TupleMap<Head>, typename Head::From, std::tuple<typename Head::To>> {
	const Head head;

	TupleMap(Head head): head(head) {}

	Parser<std::tuple<typename Head::To>> run(Parser<typename Head::From> const& parser) const {
		Parser<typename Head::To> head_parser = head(parser);
		if (head_parser.err) return Parser<std::tuple<typename Head::To>>(head_parser);
		return Parser(head_parser, std::tuple(head_parser.res));
	}
};

template<class InnerMap>
struct Not: public ParseMap<Not<InnerMap>, Unit, Unit> {
	const InnerMap inner;

	Parser<Unit> run(Parser<Unit> const& parser) const {
		Parser<Unit> to_parser = inner(parser);

		if (to_parser.stat == ParseStatus::Expected) to_parser.stat = ParseStatus::Unexpected;
		else if (to_parser.stat == ParseStatus::Unexpected) to_parser.stat = ParseStatus::Expected;

		to_parser.err = !to_parser.err;

		return to_parser;
	}
};

template<class InnerMap>
struct LookAhead: public ParseMap<LookAhead<InnerMap>, typename InnerMap::From, typename InnerMap::To> {
	const InnerMap inner;
	LookAhead(InnerMap inner): inner(inner) {}

	Parser<typename InnerMap::To> run(Parser<typename InnerMap::From> const& parser) const {
		Parser<typename InnerMap::To> inner_parser = inner(parser);
		inner_parser.span = parser.span;
		return inner_parser;
	}
};

template<class Left, class Right>
struct Chain: public ParseMap<Chain<Left, Right>, typename Left::From, typename Right::To> {
	const Left left;
	const Right right;

	Parser<typename Right::To> run(Parser<typename Left::From> const& parser) const {
		Parser<typename Left::To> left_parser = left(parser);
		if (left_parser.err) return Parser<typename Right::To>(left_parser);

		return right(left_parser);
	}
};

template<class To, class InnerMap>
struct Combine: public ParseMap<Combine<To, InnerMap>, typename InnerMap::From, To> {
	const InnerMap in;
	const std::function<To(typename InnerMap::From, typename InnerMap::To)> resmap;

	Combine(InnerMap in, std::function<To(typename InnerMap::From, typename InnerMap::To)> resmap): in(in), resmap(resmap) {}
	Combine(InnerMap in): in(in), resmap([](typename InnerMap::From a, typename InnerMap::To b){return a;}) {}

	Parser<To> run(Parser<typename InnerMap::From> const& parser) const {
		Parser<typename InnerMap::To> inner_parsed = in(parser);
		if (inner_parsed.err) return Parser<To>(inner_parsed);

		return Parser<To>(inner_parsed, resmap(parser.res, inner_parsed.res));
	}
};

template<class Result, class SkipMap>
struct Skip: public ParseMap<Skip<Result, SkipMap>, Result, Result> {
	const SkipMap skip;

	Skip(SkipMap skip): skip(skip) {}

	Parser<Result> run(Parser<Result> const& parser) const {
		Parser<Unit> unit_parser = Parser<Unit>(parser, Unit());
		Parser<Unit> skip_parser = skip(unit_parser);

		if (skip_parser.err) return Parser<Result>(skip_parser);
		return Parser<Result>(skip_parser, parser.res);
	}
};

template<class InnerMap>
struct Multiple: public ParseMap<Multiple<InnerMap>, typename InnerMap::From, std::vector<typename InnerMap::To>> {
	const size_t min, max;
	const InnerMap inner;

	Multiple(size_t min, size_t max, InnerMap inner): min(min), max(max), inner(inner) {}
	Multiple(InnerMap inner): min(0), max(std::numeric_limits<size_t>::max()), inner(inner) {}

	Parser<std::vector<typename InnerMap::To>> run(Parser<typename InnerMap::From> const& parser) const {
		Parser<typename InnerMap::From> new_parser = parser;
		std::vector<typename InnerMap::To> vec(min);

		for (unsigned i=0; i<max; i++) {
			Parser<typename InnerMap::To> inner_parser = inner(new_parser);

			if (inner_parser.err) {
				if (i<min) return Parser<std::vector<typename InnerMap::To>>(inner_parser);
				else return Parser(new_parser, vec);
			}

			new_parser.span = inner_parser.span;
			vec.push_back(inner_parser.res);
		}

		return Parser(new_parser, vec);
	}
};

template<size_t n, class InnerMap>
struct ArrayMap: public ParseMap<ArrayMap<n, InnerMap>, typename InnerMap::From, std::array<typename InnerMap::To, n>> {
	const InnerMap inner;
	ArrayMap(InnerMap inner): inner(inner) {}

	Parser<std::array<typename InnerMap::To, n>> run(Parser<typename InnerMap::From> const& parser) const {
		Parser<typename InnerMap::From> new_parser = parser;

		std::array<typename InnerMap::To, n> arr;

		for (unsigned i=0; i<n; i++) {
			Parser<typename InnerMap::To> inner_parser = inner(new_parser);
			new_parser.span = inner_parser.span;
			arr[i] = inner_parser.res;
		}

		return Parser(new_parser, arr);
	}
};

template<class InnerMap>
struct Many: public ParseMap<Many<InnerMap>, typename InnerMap::From, Unit> {
	const size_t min, max;
	const InnerMap inner;

	Many(size_t min, size_t max, InnerMap inner): min(min), max(max), inner(inner) {}
	Many(InnerMap inner): min(0), max(std::numeric_limits<size_t>::max()), inner(inner) {}

	Parser<Unit> run(Parser<typename InnerMap::From> const& parser) const {
		Parser<typename InnerMap::From> new_parser = parser;
		for (unsigned i=0; i<max; i++) {
			Parser<typename InnerMap::To> inner_parser = inner(new_parser);

			if (inner_parser.err) {
				if (i<min) return Parser<Unit>(inner_parser);
				else return Parser<Unit>(new_parser, Unit());
			}

			new_parser.span = inner_parser.span;
		}

		return Parser<Unit>(new_parser, Unit());
	}
};

template<class Left, class Right>
struct Or: public ParseMap<Or<Left, Right>, typename Left::From, typename Left::To> {
	const Left left;
	const Right right;

	Parser<typename Left::To> run(Parser<typename Left::From> const& parser) const {
		Parser<typename Left::To> left_parser = left(parser);
		if (!left_parser.err) return left_parser;

		return right(parser);
	}
};

template<class InnerMap>
struct ParseString: public ParseMap<ParseString<InnerMap>, typename InnerMap::From, std::string> {
	InnerMap inner;
	ParseString(InnerMap inner): inner(inner) {}

	Parser<std::string> run(Parser<typename InnerMap::From> const& parser) const {
		char const* start = parser.span.text;
		Parser<typename InnerMap::To> inner_parser = inner(parser);
		if (inner_parser.err) return Parser<std::string>(inner_parser);
		return Parser<std::string>(inner_parser, std::string(start, inner_parser.span.text - start));
	}
};

struct Any: public ParseMap<Any, Unit, Unit> {
	Parser<Unit> run(Parser<Unit> const& parser) const {
		Parser<Unit> new_parser = parser;
		new_parser.stat = ParseStatus::Expected;
		new_parser.expected = "anything";

		if (parser.span.length==0) {
			new_parser.err=true;
		} else {
			new_parser.span+=1;
		}

		return new_parser;
	}
};

struct Char: public ParseMap<Char, Unit, char> {
	Parser<char> run(Parser<Unit> const& parser) const {
		auto char_parser = Parser<char>(parser);
		char_parser.stat = ParseStatus::Expected;
		char_parser.expected = "a character";

		if (parser.span.length==0) {
			char_parser.err=true;
		} else {
			char_parser.res = *parser.span.start;
			char_parser.span+=1;
		}

		return char_parser;
	}
};

struct ParseEOF: public ParseMap<ParseEOF, Unit, Unit> {
	Parser<Unit> run(Parser<Unit> const& parser) const {
		Parser<Unit> new_parser = parser;
		new_parser.stat = ParseStatus::Expected;
		new_parser.expected = "end of input";

		if (new_parser.span.length!=0) {
			new_parser.err=true;
		}

		return new_parser;
	}
};

struct ParseWS: public ParseMap<ParseWS, Unit, Unit> {
	Parser<Unit> run(Parser<Unit> const& parser) const {
		Parser<Unit> new_parser = parser;
		new_parser.stat = ParseStatus::Expected;
		new_parser.expected = "whitespace";

		if (new_parser.span.length==0 || strchr("\r\n ", *new_parser.span.text)==nullptr) {
			new_parser.err=true;
		} else {
			new_parser.span+=1;
		}

		return new_parser;
	}
};

struct Match: public ParseMap<Match, Unit, Unit> {
	char const* match;
	Match(char const* match): match(match) {}

	Parser<Unit> run(Parser<Unit> const& parser) const {
		Parser<Unit> new_parser = parser;
		new_parser.stat = ParseStatus::Expected;
		new_parser.expected = match;

		if (new_parser.span.length<strlen(match) || strncmp(new_parser.span.text, match, strlen(match))!=0) {
			new_parser.err=true;
		} else {
			new_parser.span += strlen(match);
		}

		return new_parser;
	}
};

struct ParseInt: public ParseMap<ParseInt, Unit, long> {
	Parser<long> run(Parser<Unit> const& parser) const {
		char* end;
		
		auto new_parser = Parser<long>(parser);
		new_parser.res = strtol(parser.span.text, &end, 10);

		new_parser.stat = ParseStatus::Value;

		if (end==new_parser.span.text) {
			new_parser.err=true;
			return new_parser;
		}

		new_parser.span.length -= end-new_parser.span.text;
		new_parser.span.text = end;

		return new_parser;
	}
};

struct ParseFloat: public ParseMap<ParseFloat, Unit, float> {
	Parser<float> run(Parser<Unit> const& parser) const {
		char* end;
		auto new_parser = Parser<float>(parser);

		new_parser.res = strtof(parser.span.text, &end);
		new_parser.stat = ParseStatus::Value;

		if (end==new_parser.span.text) {
			new_parser.err=true;
			return new_parser;
		}

		new_parser.span.length -= end-new_parser.span.text;
		new_parser.span.text = end;

		return new_parser;
	}
};

#endif //SRC_PARSER_HPP_

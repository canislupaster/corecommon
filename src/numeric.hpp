#ifndef CORECOMMON_SRC_NUMERIC_HPP_
#define CORECOMMON_SRC_NUMERIC_HPP_

#include <vector>
#include <variant>
#include <complex>

#include "smallvector.hpp"
#include "util.hpp"

//this PoS (piece of shit) is so slow never use it!
//only a toy until i actually get my hands dirty
class Digital {
 public:
	bool negative;
	int exponent; //highest digit corresponds to 2^(exponent*16)
	SmallVector<uint16_t, 1> digits;

	Digital(uint16_t x, bool neg, int e=0, unsigned len=1);
	Digital(std::initializer_list<uint16_t> x, bool neg, int e=0);
	Digital(uint64_t x, bool neg);
	Digital(int64_t x);
	static Digital UInt(uint16_t x);

	void pad(int e, int end);
	void trim();

	struct DivZero: public std::exception {
		char const* what() const noexcept override {
			return "division by zero";
		}
	};

	Digital& add(uint16_t x, int e, bool do_trim=true);
	Digital& sub(uint16_t x, int e, bool do_trim=true);
	Digital& operator+=(uint16_t x);
	Digital& operator-=(uint16_t x);

	Digital& operator+=(Digital const& other);
	Digital& negate();
	void round(unsigned sigfigs);
	Digital operator-() const;

	Digital operator+(Digital const& other) const;

	Digital& operator<<=(int x);
	Digital& operator>>=(int x);
	Digital operator<<(int x) const;
	Digital operator>>(int x) const;

	Digital operator*(Digital const& other) const;

	struct DivisionResult;

	DivisionResult div(Digital const& other, int prec=10, bool round=true) const;
	Digital operator/(Digital const& other) const;
	Digital operator%=(Digital const& other);

	Digital& operator*=(Digital const& other);
	Digital& operator/=(Digital const& other);

	Digital operator-(Digital const& other) const;
	Digital& operator-=(Digital const& other);

	enum CmpType {
		Greater,
		Geq,
		Eq
	};

	bool cmp(Digital const& other, CmpType cmp_t) const;

	bool operator>(Digital const& other) const;
	bool operator>=(Digital const& other) const;
	bool operator<(Digital const& other) const;
	bool operator<=(Digital const& other) const;
	bool operator==(Digital const& other) const;
	bool operator!=(Digital const& other) const;
};

struct Digital::DivisionResult {
	Digital quotient;
	Digital remainder;
};

std::ostream& operator<<(std::ostream& os, Digital const& x);

struct Rational {
	Digital num;
	Digital den;

	Rational(Digital num, Digital den);
	static Rational approx(Digital x, unsigned prec);

	void round(unsigned prec);
};

std::ostream& operator<<(std::ostream& os, Rational const& x);


#endif //CORECOMMON_SRC_NUMERIC_HPP_

#ifndef CORECOMMON_SRC_NUMERIC_HPP_
#define CORECOMMON_SRC_NUMERIC_HPP_

#include <vector>
#include <variant>
#include <complex>

#include "smallvec.hpp"
#include "util.hpp"

struct Operation;

struct Rational {
	long upper, lower;
};

class Digital {
 public:
	bool negative;
	int exponent; //highest digit corresponds to 2^(exponent*16)
	unsigned repeat; //period of last digits
	SmallVec<uint16_t, 1> digits;

	Digital(uint16_t x, bool neg, int e=0, unsigned len=1);
	Digital(std::initializer_list<uint16_t> x, bool neg, int e=0);
	Digital(int16_t x);
	static Digital UInt(uint16_t x);

	void pad(int e, int end);
	void trim();

	struct DivZero: public std::exception {
		char const* what() const noexcept override {
			return "division by zero";
		}
	};

	Digital& add(uint16_t x, int e);
	Digital& sub(uint16_t x, int e);
	Digital& operator+=(uint16_t x);
	Digital& operator-=(uint16_t x);
	Digital& operator+=(Digital const& other);
	Digital& negate();
	Digital operator-() const;

	Digital operator+(Digital const& other) const;

	Digital& operator<<=(int x);
	Digital& operator>>=(int x);
	Digital operator<<(int x) const;
	Digital operator>>(int x) const;

	Digital operator*(Digital const& other) const;

	struct DivisionResult;

	DivisionResult div(Digital const& other, int end=-INT_MIN, int repeat_max=20) const;
	Digital operator/(Digital const& other) const;
//	Decimal operator%=(Decimal const& other);

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

enum class ExpressionType {
	Rational,
	Decimal,
	Real,
	Complex,
	Operation
};

#endif //CORECOMMON_SRC_NUMERIC_HPP_

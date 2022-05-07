#include "numeric.hpp"

#include <algorithm>

Digital::Digital(std::initializer_list<uint16_t> x, bool neg, int e): digits(x), exponent(e), negative(neg) {}

Digital::Digital(uint16_t x, bool neg, int e, unsigned int len): digits(len, x), exponent(e), negative(neg) {}

Digital::Digital(uint64_t x, bool neg): Digital({static_cast<uint16_t>(x>>48), static_cast<uint16_t>((x>>32)&UINT16_MAX), static_cast<uint16_t>((x>>16)&UINT16_MAX), static_cast<uint16_t>(x&UINT16_MAX)}, x<0, 3) {
	trim();
}

Digital::Digital(int64_t x): Digital(static_cast<uint64_t>(x), x<0) {}

Digital Digital::UInt(uint16_t x) {
	return Digital(x, false);
}

void Digital::pad(int e, int end) {
//	if (digits.size()==0) digits.push_back(0);
//
	if (exponent<e) {
		digits.insert(digits.begin(), e-exponent, (negative && digits.back()>0) ? static_cast<uint16_t>(-1) : 0);
		exponent = e;
	}

	unsigned other_offset = exponent-end+1;
	if (other_offset>digits.size()) {
		digits.insert(digits.end(), other_offset-digits.size(), 0);
	}
}

void Digital::trim() {
	auto iter=digits.begin();
	while (digits.end()-iter>1 && (negative ? *iter==UINT16_MAX : *iter==0)) iter++;
	exponent-=iter-digits.begin();
	digits.erase(digits.begin(), iter);

	iter=digits.end()-1;
	while (iter-digits.begin()>0 && *iter==0) iter--;

	digits.erase(iter+1, digits.end());
}

Digital& Digital::add(uint16_t x, int e, bool do_trim) {
	pad(e, e);

	auto iter = digits.rend()-1 - exponent + e;

	*iter += x;
	bool carry = *iter<x;

	for (iter++; carry && iter!=digits.rend(); iter++) {
		(*iter)++;
		carry = *iter==0;
	}

	if (carry) {
		if (negative) {
			negative = !negative;
		} else {
			exponent++;
			digits.insert(digits.begin(), 1);
		}
	}

	if (do_trim) trim();
	return *this;
}

Digital& Digital::sub(uint16_t x, int e, bool do_trim) {
	pad(e, e);

	auto iter = digits.rend()-1 - exponent + e;
	bool borrow = *iter<x;
	*iter-=x;

	for (iter++; borrow && iter!=digits.rend(); iter++) {
		borrow = *iter==0;
		(*iter)--;
	}

	if (borrow) {
		if (negative) {
			exponent++;
			digits.insert(digits.begin(), static_cast<uint16_t>(-2));
		} else {
			negative = true;
		}
	}

	if (do_trim) trim();
	return *this;
}

Digital& Digital::operator+=(uint16_t x) {
	return this->add(x,0);
}

Digital& Digital::operator-=(uint16_t x) {
	return this->sub(x,0);
}

Digital& Digital::operator+=(Digital const& other) {
	pad(other.exponent, other.exponent-other.digits.size()+1);

	bool carry=false;
	bool all_zero=true;

	auto iter = digits.rend() - (exponent-other.exponent) - other.digits.size();

	for (auto other_iter=other.digits.rbegin(); other_iter!=other.digits.rend(); other_iter++) {
		*iter += *other_iter + carry;
		if (*iter!=0) all_zero=false;
		carry = carry ? *iter<=*other_iter : *iter<*other_iter;

		iter++;
	}

	if (other.negative) {
		for (; iter!=digits.rend(); iter++) {
			(*iter)--;
			if (*iter!=0) all_zero=false;
			carry = carry || *iter<UINT16_MAX;
		}
	} else {
		for (; carry && iter!=digits.rend(); iter++) {
			(*iter)++;
			carry = *iter==0;
		}
	}

	if (carry) {
		if (negative!=other.negative) {
			negative = false;
		} else if (!negative) {
			exponent++;
			digits.insert(digits.begin(), 1);
		} else if (all_zero) {
			digits = decltype(digits)({UINT16_MAX});
			exponent++;
		}
	} else if (other.negative) {
		if (!negative) {
			negative=true;
		} else {
			exponent++;
			digits.insert(digits.begin(), UINT16_MAX-1);
		}
	}

	trim();
	return *this;
}

Digital& Digital::negate() {
	bool carry = true;
	for (uint16_t& digit : ReverseIterator(digits)) {
		if (carry && digit!=0) {
			digit = -digit;
			carry = false;

			negative = !negative;
		} else if (!carry) {
			digit = ~digit;
		}
	}

	return *this;
}

void Digital::round(unsigned sigfigs) {
	if (digits.size()>sigfigs) {
		if (digits[sigfigs]>=UINT16_MAX/2) {
			add(1, exponent-sigfigs+1);
		}

		digits.erase(digits.begin()+sigfigs, digits.end());
	}
}

Digital Digital::operator-() const {
	Digital cpy = *this;
	cpy.negate();
	return cpy;
}

Digital Digital::operator+(Digital const & other) const{
	Digital cpy = *this;
	cpy+=other;
	return cpy;
}

Digital& Digital::operator<<=(int x){
	if (x % 16 == 0) {
		exponent += x/16;
		return *this;
	} else if (x<0) {
		exponent-=x/16 + 1;
		x=16+(x%16);
	} else {
		exponent+=x/16;
		x %= 16;
	}

	uint16_t rest = 0;
	auto iter = digits.rbegin();

	for (; iter<digits.rend(); iter++) {
		uint16_t swap_rest = *iter>>(16-x);
		*iter<<=x;
		*iter+=rest;
		rest = swap_rest;
	}

	if (rest) {
		exponent++;
		digits.insert(digits.begin(), rest);
	}

	trim();

	return *this;
}

Digital& Digital::operator>>=(int x) {
	*this<<=-x;
	return *this;
}

Digital Digital::operator>>(int x) const {
	Digital copy(*this);
	copy<<=-x;
	return copy;
}

Digital Digital::operator<<(int x) const {
	Digital copy(*this);
	copy<<=x;
	return copy;
}

Digital Digital::operator*(Digital const& other) const {
	Digital res(0, false, exponent + other.exponent, 0);

	Digital a_rep(0), b_rep(0), rep(0);

	for (unsigned i=0; i<digits.size(); i++) {
		int e1 = exponent - static_cast<int>(i);
		for (unsigned j=0; j<other.digits.size(); j++) {
			int e2 = e1 + other.exponent - static_cast<int>(j);

			uint32_t mul = digits[i]*other.digits[j];
			res.add(static_cast<uint16_t>(mul >> 16), e2+1, false);
			res.add(static_cast<uint16_t>(mul & UINT16_MAX), e2, false);
		}
	}

	res.trim();
	if (negative && other.negative) {
		res -= other << (16*(exponent+1));
		res -= *this << (16*(other.exponent+1));
		res.sub(1, other.exponent+exponent+2);
	} else if (negative) {
		res -= other << (16*(exponent+1));
	} else if (other.negative) {
		res -= *this << (16*(other.exponent+1));
	}

	return res;
}

Digital::DivisionResult Digital::div(Digital const& other, int prec, bool round) const {
	if (other==0) throw DivZero();
	else if (*this==0) return DivisionResult {.quotient=Digital(0), .remainder=Digital(0)};

	Digital res(0, false, exponent - other.exponent);
	Digital rem = negative ? -*this : *this;

	uint16_t leading = other.digits.front();

	if (other.negative) {
		decltype(digits)::const_reverse_iterator nonzero = std::find_if_not(other.digits.rbegin(), other.digits.rend(), [](uint16_t x){return x==0;});
		if (nonzero>=other.digits.rend()-1) leading = -leading;
		else leading = ~leading;
	}

	uint32_t wide_leading = static_cast<uint32_t>(leading);

	for (size_t i=0; i<=prec; i++) {
		uint32_t wide_div = (static_cast<uint32_t>(rem.digits[0])<<16)/wide_leading;
		uint16_t lo = static_cast<uint16_t>(wide_div & UINT16_MAX);
		uint16_t hi = static_cast<uint16_t>(wide_div>>16);

		if (i==prec) {
			if (round && lo>=UINT16_MAX/2) hi++;
			lo=0;
		}

		if (hi) res.add(hi, exponent-other.exponent-i);
		else if (res.exponent-static_cast<int>(res.digits.size())>exponent-other.exponent-static_cast<int>(i)) {
			res.digits.insert(res.digits.end(), res.exponent-res.digits.size()-exponent+other.exponent+i, 0);
		}

		if (lo) {
			res.digits.insert(res.digits.end(), lo);
			if (other.negative) rem += other*Digital({hi, lo}, false, exponent-other.exponent-i);
			else rem += other*Digital({static_cast<uint16_t>(~hi),
					                          static_cast<uint16_t>(-lo)}, wide_div!=0, exponent-other.exponent-i);
		} else if (hi) {
			if (other.negative) rem += other*Digital({hi}, false, exponent-other.exponent-i);
			else rem += other*Digital({static_cast<uint16_t>(-hi)}, wide_div!=0, exponent-other.exponent-i);
		}

		if (rem==0) {
			break;
		}
	}

	if (other.negative!=negative) {
		res.negate();
		rem.negate();
	}

	res.trim();
	return (DivisionResult){.quotient=res, .remainder=rem};
}

Digital Digital::operator/(Digital const& other) const {
	return div(other).quotient;
}

Digital Digital::operator%=(Digital const& other) {
	return div(other, exponent-other.exponent+1, false).remainder;
}

//	while (true) {
//		if (this->exponent<other.exponent) return *this;
//		else if (this->exponent==other.exponent) {
//			if (digits.front() > other.digits.front()
//				|| (digits.front() == other.digits.front() && *this>other)) *this -= other;
//			else return *this;
//		} else {
//
//		}
//	}
//}
//
Digital& Digital::operator*=(Digital const& other) {
	*this = *this*other;
	return *this;
}

Digital& Digital::operator/=(Digital const& other) {
	*this = *this/other;
	return *this;
}

Digital Digital::operator-(Digital const& other) const {
	return *this + (-other);
}

Digital& Digital::operator-=(Digital const& other) {
	return *this += -other;
}

std::ostream& operator<<(std::ostream& os, Digital const& x){
	//slow and stupid
	Digital copy = x;
	if (copy.negative) {
		copy.negate();
		os << "-";
	}

	bool is_frac = copy.exponent + 1 < copy.digits.size();
	Digital frac = is_frac ? copy : 0;
	if (is_frac) {
		frac.digits.erase(frac.digits.begin(), frac.digits.begin()+frac.exponent+1);
		copy.digits.erase(copy.digits.begin()+copy.exponent+1, copy.digits.end());
	}

	std::stringstream ss;
	while (copy>0) {
		Digital::DivisionResult res = copy.div(10, copy.exponent, false);
		if (res.remainder>=1) ss<<res.remainder.digits.front();
		else ss<<"0";

		copy = res.quotient;
	}

	std::string cpy(ss.str());
	std::reverse(cpy.begin(), cpy.end());
	os << cpy;

	if (is_frac) os<<".";
	while (frac>0) {
		frac*=10;
		if (frac.exponent<0) {
			os<<"0";
		} else {
			os<<frac.digits.front();
			frac.digits.erase(frac.digits.begin());
		}
	}

	return os;
}

bool Digital::cmp(Digital const& other, CmpType cmp_t) const {
//	if (other.digits.size()==0)
//		return digits.size()==0 ? (cmp_t==CmpType::Eq || cmp_t==CmpType::Geq) : (cmp_t!=CmpType::Eq && !negative);
//	else if (digits.size()==0)
//		return cmp_t!=CmpType::Eq && other.negative;

	if (negative!=other.negative) return cmp_t!=CmpType::Eq && other.negative;

	if (exponent>other.exponent) return cmp_t!=CmpType::Eq && !negative;
	else if (other.exponent<exponent) return cmp_t!=CmpType::Eq && negative;

	if (digits.size()>other.digits.size()) return cmp_t!=CmpType::Eq && !negative;
	else if (digits.size()<other.digits.size()) return cmp_t!=CmpType::Eq && negative;

	auto iter1=digits.begin(), iter2=other.digits.begin();

	while (iter1!=digits.end() && iter2!=other.digits.end()) {
		if (*iter2 < *iter1) return cmp_t!=CmpType::Eq && !negative;
		else if (*iter1 < *iter2) return cmp_t!=CmpType::Eq && negative;
		iter1++; iter2++;
	}

	return cmp_t==CmpType::Geq || cmp_t==CmpType::Eq;
}

bool Digital::operator>(Digital const& other) const {
	return cmp(other, CmpType::Greater);
}

bool Digital::operator>=(Digital const& other) const {
	return cmp(other, CmpType::Geq);
}

bool Digital::operator<(Digital const& other) const {
	return !cmp(other, CmpType::Geq);
}

bool Digital::operator<=(Digital const& other) const {
	return !cmp(other, CmpType::Greater);
}

bool Digital::operator==(Digital const& other) const {
	return cmp(other, CmpType::Eq);
}

bool Digital::operator!=(Digital const& other) const {
	return !cmp(other, CmpType::Eq);
}

Rational::Rational(Digital num, Digital den): num(std::move(num)), den(std::move(den)) {}

Rational Rational::approx(Digital x, unsigned int prec) {
	Rational ret(std::move(x),1);
	int off = ret.num.digits.size()-ret.num.exponent-1;
	if (off>0) {
		ret.den<<off;
		ret.num<<off;
	}

	ret.round(prec);
	return ret;
}

void Rational::round(unsigned int prec) {
	Digital a=1,b=0,c=1,d=0;
	for (; prec!=0; prec--) {
		if (num.exponent>=den.exponent) {
			Digital::DivisionResult num_d = num.div(den, num.exponent-den.exponent, prec==1);
			num = num_d.remainder;
			b -= num_d.quotient*c;
			a -= num_d.quotient*d;
		}

		if (num==0) {
			num = a.negative ? b : -b;
			den = a.negative ? -a : a;
			return;
		}

		if (den.exponent>=num.exponent) {
			Digital::DivisionResult den_d = den.div(num, den.exponent-num.exponent, prec==1);
			den = den_d.remainder;
			c -= den_d.quotient*b;
			d -= den_d.quotient*a;
		}

		if (den==0) break;
	}

	num = d.negative ? c : -c;
	den = d.negative ? -d : d;
}

std::ostream& operator<<(std::ostream& os, Rational const& x){
	os << x.num << "/" << x.den;
	return os;
}

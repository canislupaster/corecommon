#include "numeric.hpp"

#include <algorithm>

Digital::Digital(std::initializer_list<uint16_t> x, bool neg, int e): digits(x), exponent(e), repeat(0), negative(neg) {}

Digital::Digital(uint16_t x, bool neg, int e, unsigned int len): digits(len, x), exponent(e), repeat(0), negative(neg) {}

Digital::Digital(int16_t x): Digital(x, x<0, 0, x!=0) {}

Digital Digital::UInt(uint16_t x) {
	return Digital(x, false);
}

void Digital::pad(int e, int end) {
	if (digits.size()==0) digits.push_back(0);

	if (exponent<e) {
		digits.insert(digits.begin(), e-exponent, (negative && digits.back()>0) ? static_cast<uint16_t>(-1) : 0);
		exponent = e;
	}

	unsigned other_offset = exponent-end+1;
	if (other_offset>digits.size()-repeat) {
		if (repeat) {
			unsigned extension = 1 + (other_offset-digits.size()+repeat-1)/repeat;
			for (unsigned i=0; i<extension; i++) {
				digits.insert(digits.end(), digits.end()-repeat, digits.end());
			}
		} else {
			digits.insert(digits.end(), other_offset-digits.size(), 0);
		}
	}
}

void Digital::trim() {
	if (!digits.size()) return;

	auto iter=digits.begin();
	while (iter!=digits.end()-repeat && (negative ? *iter==UINT16_MAX : *iter==0)) iter++;
	exponent-=iter-digits.begin();
	digits.erase(digits.begin(), iter);

	if (repeat) {
		//maybe i should delete this takes way too long for every single goddamn arithmetic op
		//TODO: throw this away
		for (unsigned i=1; i<repeat; i++) {
			if (repeat%i!=0) continue;
			bool works=true;
			for (unsigned k=i; k<repeat; k++) {
				if (*(digits.rbegin()+(k%i))!=*(digits.rbegin()+k)) {
					works=false; break;
				}
			}

			if (works) {
				repeat=i;
				digits.erase(digits.end()-repeat+i, digits.end());
			}
		}
	} else {
		iter=digits.end()-1;
		if (iter>digits.begin()) {
			while (*iter==0) iter--;
		}

		digits.erase(iter+1, digits.end()-repeat);
	}
}

Digital& Digital::add(uint16_t x, int e) {
	pad(e, e);

	auto iter = digits.begin() + exponent - e;

	*iter += x;
	bool carry = *iter<x;

	for (; carry && iter!=digits.begin()-1; iter--) {
		*iter++;
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

	trim();
	return *this;
}

Digital& Digital::sub(uint16_t x, int e) {
	pad(e, e);

	auto iter = digits.begin() + exponent - e;
	bool borrow = *iter<x;
	*iter-=x;

	for (; borrow && iter!=digits.begin()-1; iter--) {
		borrow = *iter==0;
		*iter--;
	}

	if (borrow) {
		if (negative) {
			exponent++;
			digits.insert(digits.begin(), static_cast<uint16_t>(-2));
		} else {
			negative = true;
		}
	}

	trim();
	return *this;
}

Digital& Digital::operator+=(uint16_t x) {
	return this->add(x,0);
}

Digital& Digital::operator-=(uint16_t x) {
	return this->sub(x,0);
}

Digital& Digital::operator+=(Digital const& other) {
	if (other.digits.size()==0) return *this;

	pad(other.exponent, other.exponent-other.digits.size()+1);
	unsigned other_offset = exponent-other.exponent+other.digits.size();

	bool carry=false;

	unsigned other_repeat_offset = other.repeat ? ((digits.size()-1-other_offset) % other.repeat) : 0;

	if (repeat && other.repeat) {
		unsigned newrepeat = lcm(repeat, other.repeat);

		for (unsigned i=0; i<newrepeat-repeat; i++) {
			 digits.insert(digits.end(), *(digits.end()-i-repeat+(i%repeat)));
		}

		for (unsigned i=0; i<newrepeat; i++) {
			uint16_t& digit = *(digits.rbegin()+i);
			uint16_t const& other_digit = *(other.digits.rbegin()+other.repeat-1 - (other_repeat_offset + other.repeat-i)%other.repeat);

			digit += other_digit + carry;
			carry = carry ? digit<=other_digit : digit<other_digit;
		}

		bool repeat_carry = carry;
		for (unsigned i=0; repeat_carry && i<newrepeat; i++) {
			 uint16_t& digit = *(digits.rbegin()+i);
			 digit++;
			 repeat_carry = digit==0;
		}

		repeat=newrepeat;
	}

	auto iter = digits.rend() - (exponent-other.exponent) - other.digits.size();

	if (other.repeat) {
		for (unsigned i=0; i<digits.size()-other_offset; i++) {
			uint16_t const& other_digit = *(other.digits.rbegin()+other.repeat-1-(other_repeat_offset+other.repeat-i) % other.repeat);
			*iter += other_digit + carry;
			carry = carry ? *iter<=other_digit : *iter<other_digit;

			iter++;
		}
	}

	for (auto other_iter=other.digits.rbegin()+other.repeat; other_iter!=other.digits.rend(); other_iter++) {
		*iter += *other_iter + carry;
		carry = carry ? *iter<=*other_iter : *iter<*other_iter;

		iter++;
	}

	if (other.negative) {
		for (; iter!=digits.rend(); iter++) {
			(*iter)--;
			carry = carry || *iter<UINT16_MAX;
		}
	} else {
		for (; carry && iter!=digits.rend(); iter++) {
			(*iter)++;
			carry = *iter==0;
		}
	}

	if (other.repeat && !repeat) {
		for (unsigned i=0; i<other.repeat; i++) {
			digits.insert(digits.end(), *(other.digits.rbegin()+other.repeat-1-(other_repeat_offset+1+i) % other.repeat));
		}

		repeat = other.repeat;
	}

	if (carry) {
		if (negative!=other.negative) {
			negative = false;
		} else {
			exponent++;
			digits.insert(digits.begin(), negative ? static_cast<uint16_t>(-2) : 1);
		}
	} else if (!negative && other.negative) {
		negative=true;
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
	if (repeat) {
		rest=*iter>>(16-x);
		*iter = (*iter<<x) | rest;
	}

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
	Digital summand(0, false, 1, 2);

	Digital a_rep(0), b_rep(0), rep(0);

	if (repeat) {
		a_rep = Digital(0, false, exponent-digits.size()+repeat, repeat);
		std::copy(digits.begin()+digits.size()-repeat, digits.end(), a_rep.digits.begin());

		Digital den(UINT16_MAX, false, repeat-1, repeat);
		rep += (a_rep*other<<(repeat*16))/den;
	}

	if (other.repeat) {
		b_rep = Digital(0, false, other.exponent-other.digits.size()+other.repeat, other.repeat);
		std::copy(other.digits.begin()+other.digits.size()-other.repeat, other.digits.end(), b_rep.digits.begin());

		Digital den(UINT16_MAX, false, other.repeat-1, other.repeat);
		rep += (b_rep*(*this) << (other.repeat*16))/den;
	}

	if (repeat && other.repeat) {
		//👀
		Digital den(UINT16_MAX, repeat+other.repeat-1, repeat+other.repeat);
		den.trim();
		den -= Digital(2) << (16*repeat);
		den -= Digital(2) << (16*other.repeat);

		rep += (a_rep*b_rep << (16*(repeat+other.repeat)))/den;
		res = rep;
	}

	for (unsigned i=0; i<digits.size()-repeat; i++) {
		int e1 = exponent - static_cast<int>(i);
		for (unsigned j=0; j<other.digits.size()-other.repeat; j++) {
			summand.exponent = e1 + other.exponent - static_cast<int>(j) + 1;

			uint32_t mul = digits[i]*other.digits[j];
			summand.digits[0] = static_cast<uint16_t>(mul >> 16);
			summand.digits[1] = static_cast<uint16_t>(mul & UINT16_MAX);

			res += summand;
		}
	}

	res.trim();
	if (negative && other.negative) {
		res += other << (16*(exponent+1));
		res += *this << (16*(other.exponent+1));
		res -= 1 << (16*(other.exponent+exponent+2));
	} else if (negative) {
		res -= other << (16*(exponent+1));
	} else if (other.negative) {
		res -= *this << (16*(other.exponent+1));
	}

//	res.negative = negative ^ other.negative;
	return res;
}

Digital::DivisionResult Digital::div(Digital const& other, int end, int repeat_max) const {
	if (other==0) throw DivZero();
	else if (*this==0) return DivisionResult {.quotient=Digital(0), .remainder=Digital(0)};

	Digital res(0, false, exponent - other.exponent);
	Digital rem = negative ? -*this : *this;

	int rep_min = other.exponent - static_cast<int>(other.digits.size()) - exponent + static_cast<int>(digits.size());

	uint16_t leading = other.digits.front();

	if (other.negative) {
		auto nonzero = std::find_if_not(other.digits.rbegin(), other.digits.rend(), [](uint16_t x){return x==0;});
		if (nonzero>=other.digits.rend()-1) leading = -leading;
		else leading = ~leading;
	}

	uint32_t wide_leading = static_cast<uint32_t>(leading);

	std::optional<Digital> rem_repeat;
	for (size_t i=0; (exponent-other.exponent-static_cast<int>(i))>=end; i++) {
		uint32_t wide_div = (static_cast<uint32_t>(rem.digits[0])<<16)/wide_leading;
		bool end_next = (exponent-other.exponent-static_cast<int>(i))==end;
		uint16_t lo = end_next ? 0 : static_cast<uint16_t>(wide_div & UINT16_MAX);
		uint16_t hi = static_cast<uint16_t>(wide_div>>16);

		if (hi) res.add(hi, exponent-other.exponent-i);
		else if (res.exponent-static_cast<int>(res.digits.size())>exponent-other.exponent-static_cast<int>(i)) {
			res.digits.insert(res.digits.end(), res.exponent-res.digits.size()-exponent+other.exponent+i, 0);
		}

		if (lo) {
			res.digits.insert(res.digits.end(), lo);
			if (other.negative) rem += other*Digital({hi, lo}, false, exponent-other.exponent-i);
			else rem += other*Digital({static_cast<uint16_t>(lo==0 ? -hi : ~hi),
					                          static_cast<uint16_t>(lo==0 ? 0 : -lo)}, wide_div!=0, exponent-other.exponent-i);
		} else if (hi) {
			if (other.negative) rem += other*Digital({hi}, false, exponent-other.exponent-i);
			else rem += other*Digital({static_cast<uint16_t>(-hi)}, wide_div!=0, exponent-other.exponent-i);
		}

		if (rem==0) {
			break;
		} else if (end==INT_MIN && i>rep_min) {
			if (!rem_repeat) {
				rem_repeat.emplace(rem);
				continue;
			} else if (i-rep_min>repeat_max) {
				break;
			}

			rem_repeat->exponent=rem.exponent;
			if (*rem_repeat==rem) {
				res.repeat=i-other.exponent+exponent;
				break;
			}
		}
	}

	if (other.negative!=negative) res.negate();

	res.trim();
	return (DivisionResult){.quotient=res, .remainder=rem};
}

Digital Digital::operator/(Digital const& other) const {
	return div(other).quotient;
}

//Decimal Decimal::operator%=(Decimal const& other) {
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

	int offset=0;
	while (copy.exponent>0) {
		copy /= 10;
		offset--;
	}

	while (copy.exponent>-2 && copy>0) {

		if (res.remainder>=1) ss<<res.remainder.digits.front();
		else ss<<"0";
		copy /= 10;

		offset--;
		if (offset==0) ss << ".";
	}

	if (offset>0) {
		for (; offset>0; offset--) ss << "0";
		ss << ".";
	}

	std::string cpy(ss.str());
	std::reverse(cpy.begin(), cpy.end());
	os << cpy;

	return os;
}

bool Digital::cmp(Digital const& other, CmpType cmp_t) const {
	if (other.digits.size()==0)
		return digits.size()==0 ? (cmp_t==CmpType::Eq || cmp_t==CmpType::Geq) : (cmp_t!=CmpType::Eq && !negative);
	else if (digits.size()==0)
		return cmp_t!=CmpType::Eq && other.negative;

	if (negative!=other.negative) return cmp_t!=CmpType::Eq && other.negative;

	if (exponent>other.exponent) return cmp_t!=CmpType::Eq && !negative;
	else if (other.exponent<exponent) return cmp_t!=CmpType::Eq && negative;

	if (!other.repeat && digits.size()>other.digits.size()) return cmp_t!=CmpType::Eq && !negative;
	else if (!repeat && digits.size()<other.digits.size()) return cmp_t!=CmpType::Eq && negative;

	auto iter1=digits.begin(), iter2=other.digits.begin();
	long d=0;

	do {
		while (iter1!=digits.end() && iter2!=other.digits.end()) {
			if (*iter2 < *iter1) return cmp_t!=CmpType::Eq && !negative;
			else if (*iter1 < *iter2) return cmp_t!=CmpType::Eq && negative;
			iter1++; iter2++;
		}

		if (repeat && iter1==digits.end()) {
			d+=repeat;
			iter1-=repeat;
		}

		if (other.repeat && iter2==other.digits.end()) {
			d-=other.repeat;
			iter2-=other.repeat;
		}
	} while (d!=0);

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

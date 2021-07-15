#include "util.hpp"

#include <fstream>
#include <string>

//kinda copied from https://nachtimwald.com/2017/09/24/hex-encode-and-decode-in-c/
//since im too lazy to type all these ifs
char hexchar(char hex) {
	if (hex >= '0' && hex <= '9') {
		return hex - '0';
	} else if (hex >= 'A' && hex <= 'F') {
		return hex - 'A' + 10;
	} else if (hex >= 'a' && hex <= 'f') {
		return hex - 'a' + 10;
	} else {
		return 0;
	}
}

void charhex(unsigned char chr, char* out) {
	char first = (char)(chr % 16u);
	if (first < 10) out[1] = first+'0';
	else out[1] = (first-10) + 'A';

	chr /= 16;

	if (chr < 10) out[0] = chr+'0';
	else out[0] = (chr-10) + 'A';
}

char const* B64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//designed for inefficiency and complexity
unsigned char* bit_reinterpret(unsigned char* src, unsigned inlen, unsigned* outlen, unsigned char inl, unsigned char outl) {
	*outlen = (inlen*inl)/outl + 1; //if aligned, remove padding
	unsigned char* out = (unsigned char*)malloc(*outlen);

	unsigned char* srccur = src;
	unsigned char* cur = out;
	unsigned pos=0;

	*cur=0;

	while (1) {
		*cur |= (((*srccur<<(pos%inl))&(UCHAR_MAX<<(8-inl)))>>(pos%outl))&(UCHAR_MAX<<(8-outl));

		char incr;
		if (inl-(pos%inl) > outl-(pos%outl)) {
			incr = outl-(pos%outl);
		} else {
			incr = inl-(pos%inl);
		}

		if ((pos%outl) + incr >= outl) {
			cur++;
			*cur = 0;
		}

		if ((pos%inl) + incr >= inl) {
			srccur++;
			if (srccur-src >= inlen) {
				pos += incr;
				break;
			}
		}

		pos += incr;
	}

	*outlen = cur-out + (pos%outl > 0 ? 1 : 0);

	return out;
}

char* base64_encode(char* src, unsigned len) {
	unsigned olen;
	unsigned char* s = bit_reinterpret((unsigned char*)src, len, &olen, 8, 6);
	char* o = (char*)s;

	for (unsigned i=0; i<olen; i++) {
		o[i] = B64_ALPHABET[s[i]>>2];
	}

	o = (char*)realloc(o, ((len+2)/3)*4 + 1);
	while ((olen*6)/24 != (len+2)/3) {
		o[olen++] = '=';
	}

	o[olen] = 0;

	return o;
}

//mods src
char* base64_decode(char* src, unsigned* len) {
	unsigned slen = strlen(src);
	unsigned len_off=0;
	while (slen>0 && src[slen-1]=='=') {
		len_off++;
		slen--;
	}

	for (unsigned i=0; i<slen; i++) {
		src[i] = (strchr(B64_ALPHABET, src[i])-B64_ALPHABET)<<2;
	}

	unsigned char* s = bit_reinterpret((unsigned char*)src, slen, len, 6, 8);
	*len -= len_off;
	return (char*)s;
}

VarIntRef::VarIntRef(VarIntRef::VarInt* var_vi, uint64_t x): vi(var_vi), size(1) {
	var_vi->first = static_cast<unsigned char>(x) & ((1 << 5)-1);
	x>>=5;
	for (; x>0 && size<8; size++) {
		var_vi->rest[size] = static_cast<unsigned char>(x);
		x>>=8;
	}

	var_vi->first |= (size-1) << 5;
}

uint64_t VarIntRef::value() const {
	uint64_t x=vi->first & ((1<<5)-1);

	for (char i=0; i<size-1; i++) {
		x |= vi->rest[i]<<(i*8+5);
	}

	return x;
}

std::string read_file(const char* path) {
	std::ifstream file;
	file.exceptions(std::ios::failbit);
	file.open(path);

	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

unsigned gcd(unsigned a, unsigned b) {
	while (true) {
		a %= b;
		if (a==0) return b;
		b %= a;
		if (b==0) return a;
	}
}

unsigned lcm(unsigned a, unsigned b) {
	unsigned x = gcd(a,b);
	return (a*b)/x;
}

#include "parser.hpp"

ParserSpan::LineCol ParserSpan::line_col() const {
	LineCol lc = {.line=1, .col=0};
	for (char const* x=text; x>=start; x--) {
		if (*x=='\n') lc.line++;
		if (lc.line==1) lc.col++;
	}

	return lc;
}

std::ostream& operator<<(std::ostream& ostream, ParserSpan const& span) {
	ParserSpan::LineCol lc = span.line_col();
	return ostream << "\"" << std::string_view(span.text, span.length) << "\" (" << lc.line << ":" << lc.col << ")";
}

void ParserSpan::operator+=(unsigned int n) {
	length-=n;
	text+=n;
}

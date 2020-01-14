#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "util.h"
#include "str.h"

// by Pavel Šimerda

int vasprintf(char** strp, const char* fmt, va_list ap) {
	va_list ap1;
	size_t size;
	char* buffer;

	va_copy(ap1, ap);
	size = vsnprintf(NULL, 0, fmt, ap1) + 1;
	va_end(ap1);
	buffer = heap(size);

	if (!buffer)
		return -1;

	*strp = buffer;

	return vsnprintf(buffer, size, fmt, ap);
}
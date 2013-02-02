#include "fp.h"

/* fixed point utilities */
#define IS_DIGIT(a) ((a) >= '0' && (a) <= '9')
#define DIGIT(a) ((a) - '0')

fixed_t str_to_fp(const char *str, char **endptr) {
	int sign;
	if(*str == '-') {
		sign = -1;
		str++;
	} else {
		sign = 1;
	}

	/* integer part */
	fixed_t val = 0;
	while(IS_DIGIT(*str)) val = val*10 + DIGIT(*(str++));

	fixed_t div = 1;
	if(*str == '.') {
		/* fractional part */
		str++;
		while(IS_DIGIT(*str)) {
			val = val*10 + DIGIT(*(str++));
			div *= 10;
		}
	}
	val = (fixed_d_t)val * FP_ONE / div;

	if(endptr) *endptr = str;

	return sign < 0 ? -val : val;
}

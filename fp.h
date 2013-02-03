#ifndef _FP_H_
#define _FP_H_

#include <stdint.h>

/* fixed point arithmetics */
typedef int32_t fixed_t;
typedef int64_t fixed_d_t; /* double sized type for multiplication and division */

#define FP_FRACT_BITS 10

/* constants */
#define FP_ONE (1L << FP_FRACT_BITS)

/* operations */
#define FP_TRUNC(a) ((a) >= 0 ? ((a) >> FP_FRACT_BITS) : -((-(a)) >> FP_FRACT_BITS))
#define FP_FRAC(a) ((a) & (FP_ONE - 1))

#define FP_ROUND(a) \
	((a) >= 0 ? \
	(((a) + (FP_ONE >> 1)) >> FP_FRACT_BITS) : \
	-((-(a) + (FP_ONE >> 1)) >> FP_FRACT_BITS))

#define FP_UP_MUL(a, b) ((fixed_d_t)(a) * (b))
#define FP_UP(a) ((a) << FP_FRACT_BITS)
#define FP_DOWN(a) ((a) >> FP_FRACT_BITS) /* usage example: a*b + c*d ==> FP_DOWN(FP_UP_MUL(a, b) + FP_UP_MUL(c, d)) */

#define FP_MUL(a, b) (((fixed_d_t)(a) * (b)) >> FP_FRACT_BITS)
#define FP_DIV(a, b) (((fixed_d_t)(a) << FP_FRACT_BITS) / (b))
#define FP_ABS(a) ((a) < 0 ? -(a) : (a))

/* limits */
#define FP_MIN INT32_MIN
#define FP_MAX INT32_MAX
#define FP_D_MIN INT64_MIN
#define FP_D_MAX INT64_MAX

fixed_t str_to_fp(const char *str, const char **endptr);

#endif /* _FP_H_ */

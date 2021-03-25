#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

/* Our Implemetation */
// We use 17.14 fixed-point representation
typedef int fixed_t;
#define P 17
#define Q 14
#define F 1<<14

#define CONVERT_TO_FP(n) (n) * (F)
#define CONVERT_TO_INTZ(x) (x) / (F)
#define CONVERT_TO_INTN(x) ((x) >= 0 ? ((x) + (F) / 2)/ (F) : ((x) - (F) / 2)/ (F))
#define ADD_Y(x, y) (x) + (y)
#define SUB_Y(x, y) (x) - (y)
#define ADD_N(x, n) (x) + (n) * (F)
#define SUB_N(x, n) (x) - (n) * (F)
#define MULT_BY_Y(x, y) ((int64_t)(x)) * (y) / (F)
#define MULT_BY_N(x, n) (x) * (n)
#define DIVIDE_BY_Y(x, y) ((int64_t)(x)) * (F) / (y)
#define DIVIDE_BY_N(x, n) (x) / (n)
/* END */
#endif /* threads/fixed-point.h */
#ifndef PLATFORM_UTIL_H
#define PLATFORM_UTIL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(a) ((void)(a))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CLAMP(v,lo,hi) MIN(MAX(v,lo),hi)
#endif

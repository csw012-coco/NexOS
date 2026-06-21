#pragma once

typedef unsigned long size_t;
typedef signed long ptrdiff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

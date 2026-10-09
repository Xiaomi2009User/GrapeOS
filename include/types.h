#ifndef GRAPE_TYPES_H
#define GRAPE_TYPES_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int i32;
#ifdef GRAPE_FIRMWARE
typedef unsigned long usize;
#else
typedef unsigned int usize;
#endif

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
typedef _Bool bool;
#define true 1
#define false 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif

// Copyright (c) 2015 YamaArashi

#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static inline void fatal_error_impl(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}

#define FATAL_ERROR(...) fatal_error_impl(__VA_ARGS__)

#ifdef _MSC_VER
#define UNUSED
#else
#define UNUSED __attribute__((__unused__))
#endif // _MSC_VER

#endif // GLOBAL_H

#pragma once
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t n);
void* calloc(size_t n, size_t sz);
void* realloc(void* p, size_t n);
void free(void* p);
void abort(void) __attribute__((noreturn));
void exit(int code) __attribute__((noreturn));
char* getenv(const char* name);
int abs(int v);
long labs(long v);
int atoi(const char* s);
double atof(const char* s);
long strtol(const char* s, char** end, int base);
unsigned long strtoul(const char* s, char** end, int base);
double strtod(const char* s, char** end);
float strtof(const char* s, char** end);
void qsort(void* base, size_t n, size_t sz, int (*cmp)(const void*, const void*));
#ifdef __cplusplus
}
#endif
#define NULL 0
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

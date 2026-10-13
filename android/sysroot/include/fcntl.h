#pragma once
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
int open(const char* path, int flags, ...);
#ifdef __cplusplus
}
#endif
// Linux generic (aarch64) values, identical in bionic
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_CLOEXEC 02000000

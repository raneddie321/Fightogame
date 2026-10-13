#pragma once
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
ssize_t read(int fd, void* buf, size_t n);
ssize_t write(int fd, const void* buf, size_t n);
int close(int fd);
int fsync(int fd);
int unlink(const char* path);
int access(const char* path, int mode);
#ifdef __cplusplus
}
#endif
#define F_OK 0
#define R_OK 4
#define W_OK 2

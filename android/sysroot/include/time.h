#pragma once
#include <sys/types.h>
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t clk, struct timespec* ts);
#ifdef __cplusplus
}
#endif
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

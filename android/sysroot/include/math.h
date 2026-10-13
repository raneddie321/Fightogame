#pragma once
#ifdef __cplusplus
extern "C" {
#endif
float sinf(float); float cosf(float); float tanf(float); float asinf(float); float acosf(float);
float atanf(float); float atan2f(float, float); float sqrtf(float); float powf(float, float);
float expf(float); float exp2f(float); float logf(float); float log2f(float); float log10f(float);
float fabsf(float); float floorf(float); float ceilf(float); float roundf(float); float truncf(float);
float fmodf(float, float); float fminf(float, float); float fmaxf(float, float); float hypotf(float, float);
float copysignf(float, float); float sinhf(float); float coshf(float); float tanhf(float);
double sin(double); double cos(double); double tan(double); double asin(double); double acos(double);
double atan(double); double atan2(double, double); double sqrt(double); double pow(double, double);
double exp(double); double exp2(double); double log(double); double log2(double); double log10(double);
double fabs(double); double floor(double); double ceil(double); double round(double); double trunc(double);
double fmod(double, double); double fmin(double, double); double fmax(double, double); double hypot(double, double);
double ldexp(double, int); double frexp(double, int*); float ldexpf(float, int); float frexpf(float, int*);
double copysign(double, double); double sinh(double); double cosh(double); double tanh(double);
#ifdef __cplusplus
}
#endif
#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define M_PI 3.14159265358979323846
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)

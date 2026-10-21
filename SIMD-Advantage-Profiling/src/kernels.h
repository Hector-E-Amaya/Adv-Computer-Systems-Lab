#ifndef KERNELS_H
#define KERNELS_H

#include <stddef.h>

/* fp32 */
void saxpy_f32(size_t n, float a, const float *restrict x, float *restrict y);
void saxpy_strided_f32(size_t n, size_t stride, float a, const float *restrict x, float *restrict y);
void mul_f32(size_t n, const float *restrict x, const float *restrict y, float *restrict z);
void stencil_f32(size_t n, float c0, float c1, float c2, const float *restrict x, float *restrict y);
float dot_f32(size_t n, const float *restrict x, const float *restrict y);
float dot_simd_f32(size_t n, const float *restrict x, const float *restrict y);

/* fp64 */
void saxpy_f64(size_t n, double a, const double *restrict x, double *restrict y);
void saxpy_strided_f64(size_t n, size_t stride, double a, const double *restrict x, double *restrict y);
void mul_f64(size_t n, const double *restrict x, const double *restrict y, double *restrict z);
void stencil_f64(size_t n, double c0, double c1, double c2, const double *restrict x, double *restrict y);
double dot_f64(size_t n, const double *restrict x, const double *restrict y);
double dot_simd_f64(size_t n, const double *restrict x, const double *restrict y);

#endif

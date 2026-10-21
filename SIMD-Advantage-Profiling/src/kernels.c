/*
 * Benchmark kernels: plain C loops. The build flags (scripts/build.sh) decide
 * whether and how wide they vectorize. Every kernel is written out by hand,
 * with no macros, so -fopt-info messages and the assembly map one-to-one
 * onto source lines.
 */
#include "kernels.h"

/* ---------- SAXPY: y = a*x + y ---------- */

void saxpy_f32(size_t n, float a, const float *restrict x, float *restrict y)
{
    for (size_t i = 0; i < n; i++)
        y[i] = a * x[i] + y[i];
}

void saxpy_f64(size_t n, double a, const double *restrict x, double *restrict y)
{
    for (size_t i = 0; i < n; i++)
        y[i] = a * x[i] + y[i];
}

/* Same update on every stride-th element; n is the number of elements updated. */
void saxpy_strided_f32(size_t n, size_t stride, float a, const float *restrict x, float *restrict y)
{
    for (size_t i = 0; i < n; i++)
        y[i * stride] = a * x[i * stride] + y[i * stride];
}

void saxpy_strided_f64(size_t n, size_t stride, double a, const double *restrict x, double *restrict y)
{
    for (size_t i = 0; i < n; i++)
        y[i * stride] = a * x[i * stride] + y[i * stride];
}

/* ---------- Elementwise multiply: z = x*y ---------- */

void mul_f32(size_t n, const float *restrict x, const float *restrict y, float *restrict z)
{
    for (size_t i = 0; i < n; i++)
        z[i] = x[i] * y[i];
}

void mul_f64(size_t n, const double *restrict x, const double *restrict y, double *restrict z)
{
    for (size_t i = 0; i < n; i++)
        z[i] = x[i] * y[i];
}

/* ---------- 3-point stencil on interior points; y[0] and y[n-1] untouched ---------- */

void stencil_f32(size_t n, float c0, float c1, float c2, const float *restrict x, float *restrict y)
{
    if (n < 3)
        return;
    for (size_t i = 1; i < n - 1; i++)
        y[i] = c0 * x[i - 1] + c1 * x[i] + c2 * x[i + 1];
}

void stencil_f64(size_t n, double c0, double c1, double c2, const double *restrict x, double *restrict y)
{
    if (n < 3)
        return;
    for (size_t i = 1; i < n - 1; i++)
        y[i] = c0 * x[i - 1] + c1 * x[i] + c2 * x[i + 1];
}

/* ---------- Dot product ---------- */

/* Plain reduction: strict left-to-right FP order, so -O3 alone must not vectorize it (H8). */
float dot_f32(size_t n, const float *restrict x, const float *restrict y)
{
    float s = 0.0f;
    for (size_t i = 0; i < n; i++)
        s += x[i] * y[i];
    return s;
}

double dot_f64(size_t n, const double *restrict x, const double *restrict y)
{
    double s = 0.0;
    for (size_t i = 0; i < n; i++)
        s += x[i] * y[i];
    return s;
}

/* OpenMP SIMD permits reassociation; takes effect only with -fopenmp-simd. */
float dot_simd_f32(size_t n, const float *restrict x, const float *restrict y)
{
    float s = 0.0f;
#pragma omp simd reduction(+:s)
    for (size_t i = 0; i < n; i++)
        s += x[i] * y[i];
    return s;
}

double dot_simd_f64(size_t n, const double *restrict x, const double *restrict y)
{
    double s = 0.0;
#pragma omp simd reduction(+:s)
    for (size_t i = 0; i < n; i++)
        s += x[i] * y[i];
    return s;
}

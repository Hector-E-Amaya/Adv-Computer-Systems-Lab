/*
 * Timing and validation harness for the kernels in kernels.c.
 *
 * One process runs one configuration and prints one CSV row, or a PASS/FAIL
 * line with --validate. Every timed run is validated first against a
 * long-double reference, including guard elements past the end of each array,
 * which catch out-of-bounds tail writes.
 *
 * Windows only: QueryPerformanceCounter timing, thread pinned to one logical
 * CPU. With no perf counters available, cycles are estimated from the
 * frequency measured by a chain of dependent adds (1 cycle each).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <float.h>
#include <io.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernels.h"

#ifndef BUILD_NAME
#define BUILD_NAME "unknown"
#endif

enum { K_SAXPY, K_MUL, K_STENCIL, K_DOT, K_DOT_SIMD, K_COUNT };
static const char *const kname[K_COUNT] = {"saxpy", "mul", "stencil", "dot", "dot_simd"};
static const int kflops[K_COUNT]  = {2, 1, 5, 2, 2};  /* FLOPs per element */
static const int ktraffic[K_COUNT] = {3, 3, 2, 2, 2}; /* elements moved per element (useful bytes / sizeof(T)) */
static const int knbuf[K_COUNT]   = {2, 3, 2, 2, 2};  /* arrays allocated */

#define GUARD 16           /* guard elements after each array */
#define GUARD_VAL 12345.0  /* exactly representable in float and double */

/* Coefficients chosen exactly representable in float and double. */
#define A_COEF  0.75
#define C0_COEF 0.25
#define C1_COEF 0.5
#define C2_COEF 0.25

struct opts {
    int kernel, f64, validate_only, cpu, trials;
    size_t n, stride, misalign;
    double min_time;
};

struct bufs {
    void *raw[3], *p[3];
    size_t len; /* elements per array, including guard */
};

static double sink; /* keeps dot results observable */

/* ---------- timing ---------- */

static double qpc_period;

static double now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * qpc_period;
}

/* Effective core frequency in GHz: best of 3 runs of a dependent-add chain. */
static double measure_ghz(void)
{
    const long iters = 5000000; /* 8 adds per iteration, ~40M cycles */
    double best = 0;
    for (int r = 0; r < 3; r++) {
        long long x = 0;
        double t0 = now();
        for (long i = 0; i < iters; i++)
            __asm__ volatile("add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\t"
                             "add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0"
                             : "+r"(x));
        double t = now() - t0;
        double ghz = 8.0 * iters / t / 1e9;
        if (ghz > best)
            best = ghz;
    }
    return best;
}

/* ---------- buffers ---------- */

static size_t esize(const struct opts *o) { return o->f64 ? sizeof(double) : sizeof(float); }

static long double get(const void *p, size_t i, int f64)
{
    return f64 ? ((const double *)p)[i] : ((const float *)p)[i];
}

static void set(void *p, size_t i, double v, int f64)
{
    if (f64)
        ((double *)p)[i] = v;
    else
        ((float *)p)[i] = (float)v;
}

static void alloc_bufs(const struct opts *o, struct bufs *b)
{
    memset(b, 0, sizeof *b);
    b->len = o->n * o->stride + GUARD;
    for (int k = 0; k < knbuf[o->kernel]; k++) {
        b->raw[k] = _aligned_malloc(b->len * esize(o) + o->misalign, 64);
        if (!b->raw[k]) {
            fprintf(stderr, "allocation of %zu bytes failed\n", b->len * esize(o));
            exit(2);
        }
        b->p[k] = (char *)b->raw[k] + o->misalign;
    }
}

static void free_bufs(struct bufs *b)
{
    for (int k = 0; k < 3; k++)
        _aligned_free(b->raw[k]);
}

/* Deterministic values in [0.5, 1.5): no cancellation, no denormals. */
static void init_bufs(const struct opts *o, struct bufs *b)
{
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    size_t live = o->n * o->stride;
    for (int k = 0; k < knbuf[o->kernel]; k++)
        for (size_t i = 0; i < b->len; i++) {
            rng = rng * 6364136223846793005ull + 1442695040888963407ull;
            double v = i < live ? 0.5 + (double)(rng >> 11) * 0x1.0p-53 : GUARD_VAL;
            set(b->p[k], i, v, o->f64);
        }
}

/* ---------- kernel dispatch (switch outside the repetition loop) ---------- */

static double run_reps(const struct opts *o, struct bufs *b, long reps)
{
    void *x = b->p[0], *y = b->p[1], *z = b->p[2];
    size_t n = o->n, st = o->stride;
    double s = 0;
    if (!o->f64) {
        switch (o->kernel) {
        case K_SAXPY:
            if (st == 1)
                for (long r = 0; r < reps; r++) saxpy_f32(n, (float)A_COEF, x, y);
            else
                for (long r = 0; r < reps; r++) saxpy_strided_f32(n, st, (float)A_COEF, x, y);
            break;
        case K_MUL:
            for (long r = 0; r < reps; r++) mul_f32(n, x, y, z);
            break;
        case K_STENCIL:
            for (long r = 0; r < reps; r++)
                stencil_f32(n, (float)C0_COEF, (float)C1_COEF, (float)C2_COEF, x, y);
            break;
        case K_DOT:
            for (long r = 0; r < reps; r++) s += dot_f32(n, x, y);
            break;
        case K_DOT_SIMD:
            for (long r = 0; r < reps; r++) s += dot_simd_f32(n, x, y);
            break;
        }
    } else {
        switch (o->kernel) {
        case K_SAXPY:
            if (st == 1)
                for (long r = 0; r < reps; r++) saxpy_f64(n, A_COEF, x, y);
            else
                for (long r = 0; r < reps; r++) saxpy_strided_f64(n, st, A_COEF, x, y);
            break;
        case K_MUL:
            for (long r = 0; r < reps; r++) mul_f64(n, x, y, z);
            break;
        case K_STENCIL:
            for (long r = 0; r < reps; r++) stencil_f64(n, C0_COEF, C1_COEF, C2_COEF, x, y);
            break;
        case K_DOT:
            for (long r = 0; r < reps; r++) s += dot_f64(n, x, y);
            break;
        case K_DOT_SIMD:
            for (long r = 0; r < reps; r++) s += dot_simd_f64(n, x, y);
            break;
        }
    }
    return s;
}

/* ---------- validation ---------- */

/*
 * Runs the kernel once on fresh data and compares against a long-double
 * reference. Error is measured in units of eps * (sum of |terms|).
 * Tolerance: 8 eps for elementwise kernels; (n + 8) eps for the dot products,
 * the worst-case bound for recursive summation, valid for any summation order.
 * Every element the kernel must not write (guards, gaps between strided
 * elements, stencil endpoints) must come back bit-identical.
 */
static int validate(const struct opts *o, struct bufs *b, double *max_err, double *tol)
{
    int f64 = o->f64, kern = o->kernel, ok = 1;
    size_t n = o->n, st = o->stride, len = b->len, es = esize(o);
    long double eps = f64 ? DBL_EPSILON : FLT_EPSILON;
    void *snap[3] = {0};

    init_bufs(o, b);
    for (int k = 0; k < knbuf[kern]; k++) {
        snap[k] = malloc(len * es);
        if (!snap[k]) {
            fprintf(stderr, "snapshot allocation failed\n");
            exit(2);
        }
        memcpy(snap[k], b->p[k], len * es);
    }
    double s = run_reps(o, b, 1);

    *max_err = 0;
    *tol = (kern == K_DOT || kern == K_DOT_SIMD) ? (double)n + 8 : 8;

#define CHECK(got, ref, mag)                                              \
    do {                                                                  \
        long double m_ = (mag) > 0 ? (mag) : 1;                           \
        double e_ = (double)(fabsl((long double)(got) - (ref)) / (m_ * eps)); \
        if (!(e_ <= *tol)) ok = 0; /* also catches NaN */                 \
        if (e_ > *max_err || e_ != e_) *max_err = e_;                     \
    } while (0)

    switch (kern) {
    case K_SAXPY:
        for (size_t i = 0; i < len; i++) {
            long double x0 = get(snap[0], i, f64), y0 = get(snap[1], i, f64);
            long double got = get(b->p[1], i, f64);
            if (i < n * st && i % st == 0)
                CHECK(got, A_COEF * x0 + y0, fabsl(A_COEF * x0) + fabsl(y0));
            else if (got != y0)
                ok = 0; /* wrote an element it must not touch */
        }
        break;
    case K_MUL:
        for (size_t i = 0; i < len; i++) {
            long double got = get(b->p[2], i, f64);
            if (i < n) {
                long double ref = get(snap[0], i, f64) * get(snap[1], i, f64);
                CHECK(got, ref, fabsl(ref));
            } else if (got != get(snap[2], i, f64))
                ok = 0;
        }
        break;
    case K_STENCIL:
        for (size_t i = 0; i < len; i++) {
            long double got = get(b->p[1], i, f64);
            if (n >= 3 && i >= 1 && i < n - 1) {
                long double a = C0_COEF * get(snap[0], i - 1, f64), c = C1_COEF * get(snap[0], i, f64),
                            d = C2_COEF * get(snap[0], i + 1, f64);
                CHECK(got, a + c + d, fabsl(a) + fabsl(c) + fabsl(d));
            } else if (got != get(snap[1], i, f64))
                ok = 0;
        }
        break;
    case K_DOT:
    case K_DOT_SIMD: {
        long double ref = 0, mag = 0;
        for (size_t i = 0; i < n; i++) {
            long double t = get(snap[0], i, f64) * get(snap[1], i, f64);
            ref += t;
            mag += fabsl(t);
        }
        CHECK(s, ref, mag);
        break;
    }
    }
#undef CHECK

    for (int k = 0; k < 3; k++)
        free(snap[k]);
    return ok;
}

/* ---------- benchmark ---------- */

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void benchmark(const struct opts *o, struct bufs *b, int valid, double max_err)
{
    double ghz0 = measure_ghz();
    init_bufs(o, b);

    /* Grow reps until one trial lasts min_time; this doubles as warm-up. */
    long reps = 1;
    for (;;) {
        double t0 = now();
        sink += run_reps(o, b, reps);
        double t = now() - t0;
        if (t >= o->min_time)
            break;
        double f = t > 1e-6 ? 1.2 * o->min_time / t : 8;
        if (f < 2)
            f = 2;
        reps = (long)(reps * f) + 1;
    }

    double *ts = malloc(o->trials * sizeof *ts);
    double sum = 0, sum2 = 0;
    for (int k = 0; k < o->trials; k++) {
        double t0 = now();
        sink += run_reps(o, b, reps);
        ts[k] = (now() - t0) / reps;
        sum += ts[k];
        sum2 += ts[k] * ts[k];
    }
    double ghz1 = measure_ghz();

    qsort(ts, o->trials, sizeof *ts, cmp_double);
    double med = o->trials % 2 ? ts[o->trials / 2] : 0.5 * (ts[o->trials / 2 - 1] + ts[o->trials / 2]);
    double mean = sum / o->trials;
    double var = sum2 / o->trials - mean * mean;
    double sd = var > 0 ? sqrt(var) : 0;

    size_t elems = o->kernel == K_STENCIL ? (o->n >= 3 ? o->n - 2 : 0) : o->n;
    double e = elems ? (double)elems : 1;
    double ghz = 0.5 * (ghz0 + ghz1);

    printf("%s,%s,%s,%zu,%zu,%zu,%ld,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.4f,%.4f,%.3g,%d\n",
           BUILD_NAME, kname[o->kernel], o->f64 ? "f64" : "f32", o->n, o->stride, o->misalign,
           reps, o->trials, med * 1e9, ts[0] * 1e9, ts[o->trials - 1] * 1e9, sd * 1e9,
           med * 1e9 / e,                                   /* ns per element */
           kflops[o->kernel] * e / med / 1e9,               /* GFLOP/s */
           ktraffic[o->kernel] * esize(o) * e / med / 1e9,  /* GB/s, useful bytes */
           med * ghz * 1e9 / e,                             /* cycles per element */
           ghz0, ghz1, max_err, valid);
    free(ts);
}

/* ---------- main ---------- */

static void usage(void)
{
    fprintf(stderr,
            "usage: bench --kernel saxpy|mul|stencil|dot|dot_simd --dtype f32|f64 --n N\n"
            "             [--stride S] [--misalign BYTES] [--trials T] [--min-time SEC]\n"
            "             [--cpu C] [--validate]\n"
            "       bench --header | --info\n");
    exit(2);
}

static void print_info(void)
{
    printf("build=%s", BUILD_NAME);
#ifdef __SSE2__
    printf(" SSE2");
#endif
#ifdef __AVX__
    printf(" AVX");
#endif
#ifdef __AVX2__
    printf(" AVX2");
#endif
#ifdef __FMA__
    printf(" FMA");
#endif
#ifdef __AVX512F__
    printf(" AVX512F");
#endif
    printf("\n");
}

int main(int argc, char **argv)
{
    struct opts o = {.kernel = -1, .f64 = -1, .cpu = 2, .trials = 7, .stride = 1, .min_time = 0.02};

    _setmode(_fileno(stdout), _O_BINARY); /* LF line endings in CSV output */

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--header")) {
            puts("build,kernel,dtype,n,stride,misalign,reps,trials,t_med_ns,t_min_ns,t_max_ns,t_std_ns,"
                 "ns_per_elem,gflops,gbps,cycles_per_elem,ghz_before,ghz_after,max_err_eps,valid");
            return 0;
        } else if (!strcmp(a, "--info")) {
            print_info();
            return 0;
        } else if (!strcmp(a, "--validate")) {
            o.validate_only = 1;
        } else if (!v) {
            usage();
        } else if (!strcmp(a, "--kernel")) {
            for (int k = 0; k < K_COUNT; k++)
                if (!strcmp(v, kname[k]))
                    o.kernel = k;
            i++;
        } else if (!strcmp(a, "--dtype")) {
            o.f64 = !strcmp(v, "f64") ? 1 : !strcmp(v, "f32") ? 0 : -1;
            i++;
        } else if (!strcmp(a, "--n")) {
            o.n = strtoull(v, NULL, 10), i++;
        } else if (!strcmp(a, "--stride")) {
            o.stride = strtoull(v, NULL, 10), i++;
        } else if (!strcmp(a, "--misalign")) {
            o.misalign = strtoull(v, NULL, 10), i++;
        } else if (!strcmp(a, "--trials")) {
            o.trials = atoi(v), i++;
        } else if (!strcmp(a, "--min-time")) {
            o.min_time = atof(v), i++;
        } else if (!strcmp(a, "--cpu")) {
            o.cpu = atoi(v), i++;
        } else {
            usage();
        }
    }
    if (o.kernel < 0 || o.f64 < 0 || o.n < 1 || o.stride < 1 || o.trials < 1)
        usage();
    if (o.stride != 1 && o.kernel != K_SAXPY) {
        fprintf(stderr, "--stride is only supported for saxpy\n");
        return 2;
    }
    if (o.misalign % esize(&o) != 0 || o.misalign >= 64) {
        fprintf(stderr, "--misalign must be a multiple of the element size and < 64\n");
        return 2;
    }

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpc_period = 1.0 / (double)f.QuadPart;
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    if (o.cpu >= 0)
        SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << o.cpu);

    struct bufs b;
    alloc_bufs(&o, &b);
    double max_err, tol;
    int valid = validate(&o, &b, &max_err, &tol);

    if (o.validate_only) {
        printf("%s %s %s %s n=%zu stride=%zu misalign=%zu err=%.3g tol=%.3g (eps units)\n",
               valid ? "PASS" : "FAIL", BUILD_NAME, kname[o.kernel], o.f64 ? "f64" : "f32",
               o.n, o.stride, o.misalign, max_err, tol);
    } else {
        benchmark(&o, &b, valid, max_err);
    }
    free_bufs(&b);
    return valid ? 0 : 1;
}

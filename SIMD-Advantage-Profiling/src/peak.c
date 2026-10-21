/*
 * Measured machine ceilings for the roofline (single core, pinned).
 *
 *   compute: independent FMA chains at each width (ss/sd, 128, 256, 512 bit),
 *            plus separate mul + add chains for the scalar and SSE2 baselines,
 *            which have no FMA (-march=x86-64)
 *   memory:  read, write and copy bandwidth with 512-bit loads/stores at a
 *            footprint inside L1, L2, L3 and DRAM
 *
 * Output: CSV rows on stdout (kind,variant,dtype,bytes,value,unit,ghz).
 * Same timing and frequency method as bench.c.
 * Build: scripts/build.sh. Needs AVX-512 (-march=tigerlake), -ffp-contract=off
 * and no auto-vectorization, so each intrinsic maps to exactly one instruction.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <immintrin.h>
#include <io.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

static double qpc_period;
static volatile double sink;

static double now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * qpc_period;
}

static double measure_ghz(void)
{
    const long iters = 5000000;
    double best = 0;
    for (int r = 0; r < 3; r++) {
        long long x = 0;
        double t0 = now();
        for (long i = 0; i < iters; i++)
            __asm__ volatile("add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\t"
                             "add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0"
                             : "+r"(x));
        double ghz = 8.0 * iters / (now() - t0) / 1e9;
        if (ghz > best)
            best = ghz;
    }
    return best;
}

/* ---------- compute peaks ---------- */

/* Low 32 bits of a vector as a float: only feeds the sink, keeps results live. */
static float first_lane(const void *v)
{
    float f;
    memcpy(&f, v, sizeof f);
    return f;
}

/* 12 independent FMA chains: covers 4-cycle latency x 2 FMA ports, with margin.
 * a = a*0.5 + 1 converges to 2, so values never overflow or go denormal. */
#define FMA_PEAK(name, T, SET1, FMA, LANES)                         \
    static double name(long iters)                                  \
    {                                                               \
        T a[12], b = SET1(0.5), c = SET1(1.0);                      \
        for (int k = 0; k < 12; k++)                                \
            a[k] = SET1(0.1 * k);                                   \
        for (long i = 0; i < iters; i++)                            \
            for (int k = 0; k < 12; k++)                            \
                a[k] = FMA(a[k], b, c);                             \
        double s = 0;                                               \
        for (int k = 0; k < 12; k++)                                \
            s += first_lane(&a[k]);                                 \
        sink += s;                                                  \
        return 2.0 * 12 * (LANES) * iters;                          \
    }

/* 8 mul chains + 8 add chains, independent, for builds without FMA.
 * mul by (1 + 2^-20) and add 2^-10: bounded growth for the iteration counts used. */
#define MULADD_PEAK(name, T, SET1, MUL, ADD, LANES)                 \
    static double name(long iters)                                  \
    {                                                               \
        T a[8], d[8], m = SET1(1.0 + 0x1.0p-20), c = SET1(0x1.0p-10); \
        for (int k = 0; k < 8; k++)                                 \
            a[k] = d[k] = SET1(1.0 + 0.1 * k);                      \
        for (long i = 0; i < iters; i++)                            \
            for (int k = 0; k < 8; k++) {                           \
                a[k] = MUL(a[k], m);                                \
                d[k] = ADD(d[k], c);                                \
            }                                                       \
        double s = 0;                                               \
        for (int k = 0; k < 8; k++)                                 \
            s += first_lane(&a[k]) + first_lane(&d[k]);             \
        sink += s;                                                  \
        return 16.0 * (LANES) * iters;                              \
    }

#define S_PS(v) _mm_set1_ps((float)(v))
#define S_PD(v) _mm_set1_pd(v)
#define Y_PS(v) _mm256_set1_ps((float)(v))
#define Y_PD(v) _mm256_set1_pd(v)
#define Z_PS(v) _mm512_set1_ps((float)(v))
#define Z_PD(v) _mm512_set1_pd(v)

FMA_PEAK(fma_ss, __m128, S_PS, _mm_fmadd_ss, 1)
FMA_PEAK(fma_sd, __m128d, S_PD, _mm_fmadd_sd, 1)
FMA_PEAK(fma_ps128, __m128, S_PS, _mm_fmadd_ps, 4)
FMA_PEAK(fma_pd128, __m128d, S_PD, _mm_fmadd_pd, 2)
FMA_PEAK(fma_ps256, __m256, Y_PS, _mm256_fmadd_ps, 8)
FMA_PEAK(fma_pd256, __m256d, Y_PD, _mm256_fmadd_pd, 4)
FMA_PEAK(fma_ps512, __m512, Z_PS, _mm512_fmadd_ps, 16)
FMA_PEAK(fma_pd512, __m512d, Z_PD, _mm512_fmadd_pd, 8)
MULADD_PEAK(muladd_ss, __m128, S_PS, _mm_mul_ss, _mm_add_ss, 1)
MULADD_PEAK(muladd_sd, __m128d, S_PD, _mm_mul_sd, _mm_add_sd, 1)
MULADD_PEAK(muladd_ps128, __m128, S_PS, _mm_mul_ps, _mm_add_ps, 4)
MULADD_PEAK(muladd_pd128, __m128d, S_PD, _mm_mul_pd, _mm_add_pd, 2)

struct compute_case {
    const char *variant, *dtype, *used_by;
    double (*fn)(long);
};

static const struct compute_case compute_cases[] = {
    {"muladd_ss", "f32", "scalar", muladd_ss},    {"muladd_sd", "f64", "scalar", muladd_sd},
    {"muladd_ps128", "f32", "sse2", muladd_ps128}, {"muladd_pd128", "f64", "sse2", muladd_pd128},
    {"fma_ss", "f32", "-", fma_ss},               {"fma_sd", "f64", "-", fma_sd},
    {"fma_ps128", "f32", "-", fma_ps128},         {"fma_pd128", "f64", "-", fma_pd128},
    {"fma_ps256", "f32", "avx2", fma_ps256},      {"fma_pd256", "f64", "avx2", fma_pd256},
    {"fma_ps512", "f32", "avx512", fma_ps512},    {"fma_pd512", "f64", "avx512", fma_pd512},
};

/* ---------- memory bandwidth (512-bit, 64-byte aligned) ---------- */

/* Passes over the array per call: about 8 MB of traffic per call, so small
 * (L1) footprints are not dominated by per-call setup. */
static int passes;

/* One FMA consumes two loads (one as a memory operand), like the saxpy kernel:
 * a plain add per load is capped by the single 512-bit ALU port on this core,
 * not by the cache. */
static void bw_read(float *p, size_t n)
{
    __m512 s[8];
    for (int k = 0; k < 8; k++)
        s[k] = _mm512_setzero_ps();
    for (int pass = 0; pass < passes; pass++)
        for (size_t i = 0; i < n; i += 256)
            for (int k = 0; k < 8; k++)
                s[k] = _mm512_fmadd_ps(_mm512_load_ps(p + i + 32 * k),
                                       _mm512_load_ps(p + i + 32 * k + 16), s[k]);
    for (int k = 1; k < 8; k++)
        s[0] = _mm512_add_ps(s[0], s[k]);
    sink += _mm512_reduce_add_ps(s[0]);
}

static void bw_write(float *p, size_t n)
{
    __m512 v = _mm512_set1_ps(1.0f);
    for (int pass = 0; pass < passes; pass++)
    for (size_t i = 0; i < n; i += 64)
        for (int k = 0; k < 4; k++)
            _mm512_store_ps(p + i + 16 * k, v);
}

static float *copy_dst;

static void bw_copy(float *p, size_t n)
{
    for (int pass = 0; pass < passes; pass++)
    for (size_t i = 0; i < n; i += 64)
        for (int k = 0; k < 4; k++)
            _mm512_store_ps(copy_dst + i + 16 * k, _mm512_load_ps(p + i + 16 * k));
}

struct mem_case {
    const char *variant;
    int bytes_per_elem; /* useful bytes moved per float: read 4, write 4, copy 8 */
    void (*fn)(float *, size_t);
};

static const struct mem_case mem_cases[] = {
    {"read", 4, bw_read}, {"write", 4, bw_write}, {"copy", 8, bw_copy},
};

/* Footprints (bytes per array; copy uses two arrays). */
static const struct { const char *level; size_t bytes; } levels[] = {
    {"L1", 16 << 10}, {"L2", 512 << 10}, {"L3", 3 << 20}, {"DRAM", 256 << 20},
};

/* Best-of-5 time per call, with repetitions grown until a trial lasts >= 50 ms. */
#define TIME_BEST(call, reps_out, best_out)                         \
    do {                                                            \
        long reps_ = 1;                                             \
        for (;;) {                                                  \
            double t0_ = now();                                     \
            for (long r_ = 0; r_ < reps_; r_++) call;               \
            if (now() - t0_ >= 0.05) break;                         \
            reps_ *= 2;                                             \
        }                                                           \
        double best_ = 1e30;                                        \
        for (int k_ = 0; k_ < 5; k_++) {                            \
            double t0_ = now();                                     \
            for (long r_ = 0; r_ < reps_; r_++) call;               \
            double t_ = (now() - t0_) / reps_;                      \
            if (t_ < best_) best_ = t_;                             \
        }                                                           \
        reps_out = reps_;                                           \
        best_out = best_;                                           \
    } while (0)

int main(void)
{
    _setmode(_fileno(stdout), _O_BINARY);
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpc_period = 1.0 / (double)f.QuadPart;
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);

    puts("kind,variant,dtype,used_by,bytes,value,unit,flop_per_cycle,ghz");

    for (size_t c = 0; c < sizeof compute_cases / sizeof *compute_cases; c++) {
        const struct compute_case *cc = &compute_cases[c];
        const long iters = 1000000; /* one call: ~12-16M dependent ops */
        double ghz0 = measure_ghz(), flops = 0, t;
        long reps;
        TIME_BEST(flops = cc->fn(iters), reps, t);
        double ghz = 0.5 * (ghz0 + measure_ghz());
        double gflops = flops / t / 1e9;
        printf("compute,%s,%s,%s,0,%.4g,GFLOP/s,%.3f,%.4f\n", cc->variant, cc->dtype, cc->used_by,
               gflops, gflops / ghz, ghz);
        (void)reps;
    }

    for (size_t l = 0; l < sizeof levels / sizeof *levels; l++) {
        size_t bytes = levels[l].bytes, n = bytes / sizeof(float);
        passes = bytes >= (8 << 20) ? 1 : (int)((8 << 20) / bytes);
        float *p = _aligned_malloc(bytes, 64);
        copy_dst = _aligned_malloc(bytes, 64);
        if (!p || !copy_dst) {
            fprintf(stderr, "allocation failed\n");
            return 2;
        }
        for (size_t i = 0; i < n; i++)
            p[i] = 1.0f, copy_dst[i] = 0.0f;
        for (size_t m = 0; m < sizeof mem_cases / sizeof *mem_cases; m++) {
            const struct mem_case *mc = &mem_cases[m];
            double ghz0 = measure_ghz(), t;
            long reps;
            TIME_BEST(mc->fn(p, n), reps, t);
            double ghz = 0.5 * (ghz0 + measure_ghz());
            printf("memory,%s_%s,f32,-,%zu,%.4g,GB/s,,%.4f\n", mc->variant, levels[l].level, bytes,
                   (double)mc->bytes_per_elem * n * passes / t / 1e9, ghz);
            (void)reps;
        }
        _aligned_free(p);
        _aligned_free(copy_dst);
    }
    return 0;
}

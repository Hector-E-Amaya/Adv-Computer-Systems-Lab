/*
 * Bandwidth by access pattern, granularity and read/write mix (Intel MLC
 * bandwidth-matrix equivalent).
 *
 * Each thread owns a buffer of L lines (L a power of two) and visits the lines
 * in a pattern-defined order. Every line is visited once per pass:
 *   seq     line k
 *   stride  stride S bytes: lines 0, S, 2S, ..., then 64, 64+S, ... (all lines covered)
 *   random  a bijective hash of k (xorshift-multiply on log2(L) bits)
 * At each line it touches GRAN bytes (8-byte words), as a read or a write.
 * Line k of the sequence is a write if (k mod 10) < wpct/10, giving exact
 * 100/0, 70/30, 50/50 and 0/100 read/write mixes. --nt uses non-temporal
 * stores (no read-for-ownership).
 *
 * useful GB/s counts bytes actually touched (lines x GRAN). est_dram_gbps
 * counts the 64 B lines moved to/from DRAM: one read per line visited (demand
 * or RFO), plus one write-back per written line (regular stores).
 */
#include "common.h"

#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { P_SEQ, P_STRIDE, P_RANDOM };
static const char *const pat_name[] = {"seq", "stride", "random"};

struct opts {
    size_t size; /* bytes per thread, rounded down to a power of two */
    int pattern, stride, gran, wpct, nt, large, threads, smt, trials, check;
    double dur;
};

static struct opts g;

/* Each step is a bijection on lbits-bit values, so the order covers every line. */
static inline size_t hash_line(uint64_t x, uint64_t mask, int lbits)
{
    x ^= x >> (lbits / 2 + 1);
    x = (x * 0x9E3779B97F4A7C15ull) & mask;
    x ^= x >> (lbits / 3 + 1);
    x = (x * 0xBF58476D1CE4E5B9ull) & mask;
    return (size_t)x;
}

/* Stride order with power-of-two sizes: s = 2^sbits lines per stride,
 * per = 2^pbits strides per sweep; line = (k mod per) * s + k / per. */
#define LINE_SEQ(k) (k)
#define LINE_STRIDE(k) ((((k) & pmask) << sbits) | ((k) >> pbits))
#define LINE_RANDOM(k) hash_line((k), mask, lbits)

static int stride_bits(void)
{
    int sbits = 0;
    while (((size_t)LINE << sbits) < (size_t)g.stride)
        sbits++;
    return sbits;
}

/* Reference mapping (used by the correctness check). */
static size_t line_at(size_t k, size_t L, int lbits)
{
    const size_t mask = L - 1;
    const int sbits = stride_bits(), pbits = lbits - sbits;
    const size_t pmask = ((size_t)1 << pbits) - 1;
    switch (g.pattern) {
    case P_SEQ: return LINE_SEQ(k);
    case P_STRIDE: return LINE_STRIDE(k);
    default: return LINE_RANDOM(k);
    }
}

static uint64_t write_value(size_t line, int j) { return ~(uint64_t)(line * 8 + j); }

/* One function per (pattern, words-per-line), so the inner loop has no
 * runtime word count. Reads of 4/8 words go into 256-bit accumulators that
 * are reduced once at the end. Lines go in groups of 10: the first wpct/10 of
 * each group are writes, so the mix is exact without a per-line modulo. */
#define DEFINE_RUN(NAME, LINE_OF, WORDS)                                              \
    static uint64_t NAME(uint64_t *buf, size_t L, int lbits, size_t k0, size_t count) \
    {                                                                                 \
        const size_t mask = L - 1;                                                    \
        const int wr = g.wpct / 10, nt = g.nt;                                        \
        const int sbits = stride_bits(), pbits = lbits - sbits;                       \
        const size_t pmask = ((size_t)1 << pbits) - 1;                                \
        const __m256i iota0 = _mm256_set_epi64x(3, 2, 1, 0);                          \
        const __m256i iota4 = _mm256_set_epi64x(7, 6, 5, 4);                          \
        const __m256i ones = _mm256_set1_epi64x(-1);                                  \
        __m256i v0 = _mm256_setzero_si256(), v1 = _mm256_setzero_si256();             \
        uint64_t s = 0;                                                               \
        (void)pmask; (void)mask; (void)lbits;                                         \
        for (size_t c = 0; c < count; c += 10) {                                      \
            _Pragma("GCC unroll 10")                                                  \
            for (int i = 0; i < 10; i++) {                                            \
                if (c + i == count)                                                   \
                    goto done;                                                        \
                size_t k = (k0 + c + i) & mask;                                       \
                size_t line = LINE_OF(k);                                             \
                uint64_t *p = buf + line * 8;                                         \
                if (i < wr) {                                                         \
                    if (WORDS >= 4) {                                                 \
                        __m256i b = _mm256_set1_epi64x((long long)(line * 8));        \
                        __m256i w0 = _mm256_xor_si256(_mm256_add_epi64(b, iota0), ones); \
                        if (nt) _mm256_stream_si256((__m256i *)p, w0);                \
                        else _mm256_store_si256((__m256i *)p, w0);                    \
                        if (WORDS == 8) {                                             \
                            __m256i w1 = _mm256_xor_si256(_mm256_add_epi64(b, iota4), ones); \
                            if (nt) _mm256_stream_si256((__m256i *)(p + 4), w1);      \
                            else _mm256_store_si256((__m256i *)(p + 4), w1);          \
                        }                                                             \
                    } else {                                                          \
                        for (int j = 0; j < WORDS; j++) {                             \
                            if (nt) _mm_stream_si64((long long *)&p[j], (long long)write_value(line, j)); \
                            else p[j] = write_value(line, j);                         \
                        }                                                             \
                    }                                                                 \
                } else {                                                              \
                    if (WORDS >= 4) {                                                 \
                        v0 = _mm256_add_epi64(v0, _mm256_load_si256((__m256i *)p));   \
                        if (WORDS == 8)                                               \
                            v1 = _mm256_add_epi64(v1, _mm256_load_si256((__m256i *)(p + 4))); \
                    } else {                                                          \
                        for (int j = 0; j < WORDS; j++)                               \
                            s += p[j];                                                \
                    }                                                                 \
                }                                                                     \
            }                                                                         \
        }                                                                             \
    done:                                                                             \
        if (nt)                                                                       \
            _mm_sfence();                                                             \
        uint64_t t[4];                                                                \
        _mm256_storeu_si256((__m256i *)t, _mm256_add_epi64(v0, v1));                  \
        return s + t[0] + t[1] + t[2] + t[3];                                         \
    }

#define DEFINE_PATTERN(P, LINE_OF)                  \
    DEFINE_RUN(run_##P##_1, LINE_OF, 1)             \
    DEFINE_RUN(run_##P##_2, LINE_OF, 2)             \
    DEFINE_RUN(run_##P##_4, LINE_OF, 4)             \
    DEFINE_RUN(run_##P##_8, LINE_OF, 8)
DEFINE_PATTERN(seq, LINE_SEQ)
DEFINE_PATTERN(stride, LINE_STRIDE)
DEFINE_PATTERN(random, LINE_RANDOM)

typedef uint64_t (*run_fn)(uint64_t *, size_t, int, size_t, size_t);
static const run_fn run_table[3][4] = {
    {run_seq_1, run_seq_2, run_seq_4, run_seq_8},
    {run_stride_1, run_stride_2, run_stride_4, run_stride_8},
    {run_random_1, run_random_2, run_random_4, run_random_8},
};

/* Visit `count` lines starting at sequence position k0. Returns the read sum. */
static uint64_t run_lines(uint64_t *buf, size_t L, int lbits, size_t k0, size_t count)
{
    int w = g.gran / 8, idx = w == 1 ? 0 : w == 2 ? 1 : w == 4 ? 2 : 3;
    return run_table[g.pattern][idx](buf, L, lbits, k0, count);
}

/* ---------- correctness check (one thread, one pass) ---------- */

/* Independent reference: the order must cover every line exactly once; after
 * one pass, read lines are unchanged, their sum matches, and written lines hold
 * write_value() in the first GRAN bytes and the original data after that. */
static int check_pass(uint64_t *buf, size_t L, int lbits)
{
    size_t words = L * 8;
    size_t *k_of = malloc(L * sizeof *k_of);
    uint8_t *cnt = calloc(L, 1);
    if (!k_of || !cnt)
        return 0;
    int ok = 1;
    for (size_t k = 0; k < L; k++) {
        size_t line = line_at(k, L, lbits);
        if (line >= L || cnt[line]++) {
            ok = 0;
            break;
        }
        k_of[line] = k;
    }
    if (ok) {
        for (size_t i = 0; i < words; i++)
            buf[i] = i;
        uint64_t sum = run_lines(buf, L, lbits, 0, L), expect = 0;
        int wr = g.wpct / 10, gw = g.gran / 8;
        for (size_t line = 0; line < L && ok; line++) {
            int is_write = (int)(k_of[line] % 10) < wr;
            for (int j = 0; j < 8; j++) {
                uint64_t orig = line * 8 + j, v = buf[line * 8 + j];
                if (is_write && j < gw)
                    ok &= v == write_value(line, j);
                else
                    ok &= v == orig;
                if (!is_write && j < gw)
                    expect += orig;
            }
        }
        ok &= sum == expect;
    }
    free(k_of);
    free(cnt);
    return ok;
}

/* ---------- threads ---------- */

struct worker {
    int cpu, lbits;
    uint64_t *buf;
    size_t lines;
    int64_t done;
    double t0, t1;
    uint64_t sum;
};

static volatile LONG ready;
static volatile int go;

static DWORD WINAPI work(void *arg)
{
    struct worker *w = arg;
    pin_thread(w->cpu);
    InterlockedIncrement(&ready);
    while (!go)
        _mm_pause();
    double t0 = now(), end = t0 + g.dur;
    size_t k = 0;
    int64_t done = 0;
    uint64_t sum = 0;
    const size_t chunk = 4090; /* multiple of 10 keeps the R/W mix exact across chunks */
    do {
        sum += run_lines(w->buf, w->lines, w->lbits, k, chunk);
        k = (k + chunk) % w->lines;
        done += chunk;
    } while (now() < end);
    w->t0 = t0;
    w->t1 = now();
    w->done = done;
    w->sum = sum;
    return 0;
}

static double run_trial(struct worker *ws, int n)
{
    HANDLE h[64] = {0};
    ready = 0;
    go = 0;
    for (int i = 0; i < n; i++)
        h[i] = CreateThread(NULL, 0, work, &ws[i], 0, NULL);
    while (ready < n)
        _mm_pause();
    go = 1;
    WaitForMultipleObjects(n, h, TRUE, INFINITE);
    double t0 = ws[0].t0, t1 = ws[0].t1, lines = 0;
    for (int i = 0; i < n; i++) {
        CloseHandle(h[i]);
        if (ws[i].t0 < t0) t0 = ws[i].t0;
        if (ws[i].t1 > t1) t1 = ws[i].t1;
        lines += (double)ws[i].done;
    }
    return lines / (t1 - t0); /* lines per second, all threads */
}

static void usage(void)
{
    fprintf(stderr,
            "usage: bw [--size BYTES/thread] [--threads T] [--smt] [--pattern seq|stride|random]\n"
            "          [--stride 64|256|1024|...] [--gran 8|16|32|64] [--wpct 0|30|50|100] [--nt]\n"
            "          [--large] [--dur SEC] [--trials T] [--check]\n"
            "       bw --header\n");
    exit(2);
}

int main(int argc, char **argv)
{
    g = (struct opts){.size = 64 << 20, .pattern = P_SEQ, .stride = 64, .gran = 64,
                      .threads = 1, .trials = 5, .dur = 0.25};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--header")) {
            puts("tool,pattern,stride,gran,wpct,nt,threads,size_per_thread,pages,trials,"
                 "useful_gbps_med,useful_gbps_min,useful_gbps_max,mlines_per_s,est_dram_gbps,"
                 "ghz_before,ghz_after,cpus");
            return 0;
        } else if (!strcmp(a, "--check")) {
            g.check = 1;
        } else if (!strcmp(a, "--nt")) {
            g.nt = 1;
        } else if (!strcmp(a, "--large")) {
            g.large = 1;
        } else if (!strcmp(a, "--smt")) {
            g.smt = 1;
        } else if (!v) {
            usage();
        } else if (!strcmp(a, "--size")) {
            g.size = parse_size(v), i++;
        } else if (!strcmp(a, "--threads")) {
            g.threads = atoi(v), i++;
        } else if (!strcmp(a, "--pattern")) {
            g.pattern = !strcmp(v, "seq") ? P_SEQ : !strcmp(v, "stride") ? P_STRIDE
                        : !strcmp(v, "random") ? P_RANDOM : -1;
            i++;
        } else if (!strcmp(a, "--stride")) {
            g.stride = atoi(v), i++;
        } else if (!strcmp(a, "--gran")) {
            g.gran = atoi(v), i++;
        } else if (!strcmp(a, "--wpct")) {
            g.wpct = atoi(v), i++;
        } else if (!strcmp(a, "--dur")) {
            g.dur = atof(v), i++;
        } else if (!strcmp(a, "--trials")) {
            g.trials = atoi(v), i++;
        } else {
            usage();
        }
    }
    if (g.pattern < 0 || (g.gran != 8 && g.gran != 16 && g.gran != 32 && g.gran != 64) ||
        g.wpct % 10 ||
        g.wpct < 0 || g.wpct > 100 || g.threads < 1 || g.trials < 1 ||
        g.stride < LINE || (g.stride & (g.stride - 1)))
        usage(); /* stride: power of two, >= 64 */

    size_t L = 1;
    int lbits = 0;
    while ((L << 1) * LINE <= g.size)
        L <<= 1, lbits++;
    if (g.pattern == P_STRIDE && (size_t)g.stride / LINE > L) {
        fprintf(stderr, "stride larger than the buffer\n");
        return 2;
    }

    timer_init();

    int cpus[64], ncpu;
    if (g.smt) { /* every logical CPU, siblings adjacent */
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        ncpu = (int)si.dwNumberOfProcessors;
        for (int i = 0; i < ncpu; i++)
            cpus[i] = (i + 2) % ncpu; /* start at core 1, core 0 last */
    } else {
        int cores[64];
        ncpu = core_cpus(cores, 64);
        for (int i = 0; i < ncpu; i++)
            cpus[i] = cores[(i + 1) % ncpu]; /* core 1 first, core 0 (OS interrupts) last */
    }
    if (g.threads > ncpu) {
        fprintf(stderr, "only %d CPUs available for --threads\n", ncpu);
        return 2;
    }

    struct worker *ws = calloc(g.threads, sizeof *ws);
    for (int i = 0; i < g.threads; i++) {
        ws[i].cpu = cpus[i];
        ws[i].lines = L;
        ws[i].lbits = lbits;
        ws[i].buf = alloc_buf(L * LINE, g.large, NULL);
        if (!ws[i].buf)
            return 3;
    }

    if (g.check) {
        pin_thread(cpus[0]);
        int ok = check_pass(ws[0].buf, L, lbits);
        printf("%s bw pattern=%s stride=%d gran=%d wpct=%d nt=%d lines=%zu\n", ok ? "PASS" : "FAIL",
               pat_name[g.pattern], g.stride, g.gran, g.wpct, g.nt, L);
        return ok ? 0 : 1;
    }

    pin_thread(cpus[0]);
    double ghz0 = measure_ghz();
    run_trial(ws, g.threads); /* warm-up, discarded */
    double *gb = malloc(g.trials * sizeof *gb), lps_sum = 0;
    for (int r = 0; r < g.trials; r++) {
        double lps = run_trial(ws, g.threads);
        gb[r] = lps * g.gran / 1e9;
        lps_sum += lps;
    }
    double ghz1 = measure_ghz();
    double lps = lps_sum / g.trials;
    double wfrac = g.wpct / 100.0;
    double dram = lps * LINE * (1 + (g.nt ? 0 : wfrac)) / 1e9;
    double med = median(gb, g.trials);
    printf("bw,%s,%d,%d,%d,%d,%d,%zu,%s,%d,%.4f,%.4f,%.4f,%.3f,%.4f,%.4f,%.4f,", pat_name[g.pattern],
           g.pattern == P_STRIDE ? g.stride : 64, g.gran, g.wpct, g.nt, g.threads, L * LINE,
           g.large ? "2M" : "4K", g.trials, med, gb[0], gb[g.trials - 1], lps / 1e6, dram, ghz0,
           ghz1);
    for (int i = 0; i < g.threads; i++)
        printf("%s%d", i ? " " : "", cpus[i]);
    printf("\n");
    return 0;
}

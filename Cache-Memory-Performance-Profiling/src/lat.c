/*
 * Pointer-chase latency (Intel MLC "idle latency" equivalent).
 *
 * Every node holds the address of the next node. The nodes form ONE cycle
 * (Sattolo's algorithm), so each load's address depends on the previous load:
 * no overlap, no address prediction. Time per hop = load-to-use latency.
 *
 * Modes:
 *   random  one node per 64 B line, random cycle   -> true latency per level
 *   seq     node i -> i+1                          -> prefetcher-friendly (pitfall demo)
 *   page    one node per 4 KB page, random cycle; the node's line offset within
 *           the page varies so nodes spread over cache sets. The data stays
 *           small while the page count (TLB pressure) grows.
 *
 * --k K runs K independent chases along the same cycle (memory-level
 * parallelism); the output reports per-chase latency and total throughput.
 */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { M_RANDOM, M_SEQ, M_PAGE };
static const char *const mode_name[] = {"random", "seq", "page"};

struct opts {
    size_t size;
    int mode, large, k, cpu, trials, check;
    double min_time;
};

static char *base;
static size_t nnodes;
static volatile uintptr_t sink;

static char *node_addr(size_t i, int mode)
{
    if (mode == M_PAGE)
        return base + i * PAGE4K + ((i * 7) & 63) * LINE;
    return base + i * LINE;
}

static size_t node_index(const char *p, int mode)
{
    return (size_t)(p - base) / (mode == M_PAGE ? PAGE4K : LINE);
}

/* Link the nodes into one cycle: node i points to node a[i]. */
static void build(int mode)
{
    uint32_t *a = malloc(nnodes * sizeof *a);
    if (!a) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    if (mode == M_SEQ) {
        for (size_t i = 0; i < nnodes; i++)
            a[i] = (uint32_t)((i + 1) % nnodes);
    } else {
        for (size_t i = 0; i < nnodes; i++)
            a[i] = (uint32_t)i;
        /* Sattolo: j < i (never j == i) makes the permutation a single cycle. */
        uint64_t s = 12345;
        for (size_t i = nnodes - 1; i > 0; i--) {
#ifdef MUTANT_FISHER_YATES
            size_t j = rng_next(&s) % (i + 1); /* plain shuffle: usually several cycles */
#else
            size_t j = rng_next(&s) % i;
#endif
            uint32_t t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
    }
    for (size_t i = 0; i < nnodes; i++)
        *(char **)node_addr(i, mode) = node_addr(a[i], mode);
    free(a);
}

/* Walk from node 0: must visit every node exactly once and return to the
 * start after exactly nnodes hops. */
static int check_cycle(int mode)
{
    uint8_t *seen = calloc(nnodes, 1);
    char *start = node_addr(0, mode), *p = start;
    int ok = seen != NULL;
    for (size_t s = 0; ok && s < nnodes; s++) {
        size_t idx = node_index(p, mode);
        if (idx >= nnodes || seen[idx] || p != node_addr(idx, mode))
            ok = 0;
        else
            seen[idx] = 1;
        p = *(char **)p;
    }
    free(seen);
    return ok && p == start;
}

/* K chases, each a dependent chain; the inner loop is fully unrolled so the K
 * pointers live in registers (K <= 12 fits without spills; see build/asm).
 * The final positions are written back to pos[], so consecutive calls
 * CONTINUE along the cycle. If every trial restarted from the same node, a
 * trial shorter than one lap would re-walk the same subset of the buffer, and
 * the L3 could retain part of it, understating latency for large buffers. */
#define CHASE(K)                                                   \
    static uintptr_t chase##K(char **pos, int64_t steps)           \
    {                                                              \
        char *p[K];                                                \
        for (int k = 0; k < K; k++)                                \
            p[k] = pos[k];                                         \
        for (int64_t s = 0; s < steps; s++) {                      \
            _Pragma("GCC unroll 16")                               \
            for (int k = 0; k < K; k++)                            \
                p[k] = *(char **)p[k];                             \
        }                                                          \
        uintptr_t x = 0;                                           \
        for (int k = 0; k < K; k++) {                              \
            pos[k] = p[k];                                         \
            x ^= (uintptr_t)p[k];                                  \
        }                                                          \
        return x;                                                  \
    }
CHASE(1) CHASE(2) CHASE(3) CHASE(4) CHASE(6) CHASE(8) CHASE(10) CHASE(12) CHASE(14) CHASE(16)

static const struct { int k; uintptr_t (*fn)(char **, int64_t); } chasers[] = {
    {1, chase1}, {2, chase2}, {3, chase3}, {4, chase4}, {6, chase6}, {8, chase8},
    {10, chase10}, {12, chase12}, {14, chase14}, {16, chase16},
};

__attribute__((noreturn)) static void usage(void)
{
    fprintf(stderr,
            "usage: lat --size BYTES[K|M|G] [--mode random|seq|page] [--k 1..16]\n"
            "           [--large] [--cpu C] [--trials T] [--min-time SEC] [--check]\n"
            "       lat --header | --topology\n"
            "K must be one of 1 2 3 4 6 8 10 12 14 16\n");
    exit(2);
}

int main(int argc, char **argv)
{
    struct opts o = {.mode = M_RANDOM, .k = 1, .cpu = -2, .trials = 5, .min_time = 0.05};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--header")) {
            puts("tool,mode,size_bytes,nodes,pages,k,steps,trials,lat_ns_med,lat_ns_min,lat_ns_max,"
                 "lat_cyc_med,bw_gbps_med,ghz_before,ghz_after,cpu");
            return 0;
        } else if (!strcmp(a, "--topology")) {
            print_topology();
            return 0;
        } else if (!strcmp(a, "--check")) {
            o.check = 1;
        } else if (!strcmp(a, "--large")) {
            o.large = 1;
        } else if (!v) {
            usage();
        } else if (!strcmp(a, "--size")) {
            o.size = parse_size(v), i++;
        } else if (!strcmp(a, "--mode")) {
            o.mode = !strcmp(v, "seq") ? M_SEQ : !strcmp(v, "page") ? M_PAGE
                     : !strcmp(v, "random") ? M_RANDOM : -1;
            i++;
        } else if (!strcmp(a, "--k")) {
            o.k = atoi(v), i++;
        } else if (!strcmp(a, "--cpu")) {
            o.cpu = atoi(v), i++;
        } else if (!strcmp(a, "--trials")) {
            o.trials = atoi(v), i++;
        } else if (!strcmp(a, "--min-time")) {
            o.min_time = atof(v), i++;
        } else {
            usage();
        }
    }
    uintptr_t (*chase)(char **, int64_t) = NULL;
    for (size_t c = 0; c < sizeof chasers / sizeof *chasers; c++)
        if (chasers[c].k == o.k)
            chase = chasers[c].fn;
    if (o.mode < 0 || !chase || o.trials < 1)
        usage();
    nnodes = o.size / (o.mode == M_PAGE ? PAGE4K : LINE);
    if (nnodes < 2 || nnodes < (size_t)o.k || nnodes > 0xFFFFFFFFu) {
        fprintf(stderr, "size gives %zu nodes; need 2..2^32 and >= k\n", nnodes);
        return 2;
    }

    timer_init();
    if (o.cpu == -2) { /* default: first logical CPU of physical core 1 */
        int cores[64];
        o.cpu = core_cpus(cores, 64) > 1 ? cores[1] : 0;
    }
    pin_thread(o.cpu);

    size_t actual;
    base = alloc_buf(nnodes * (o.mode == M_PAGE ? PAGE4K : LINE), o.large, &actual);
    if (!base)
        return 3;
    build(o.mode);

    if (o.check) {
        int ok = check_cycle(o.mode);
        /* Continuation: two chase calls of s steps must end where a plain walk
         * of 2s steps ends (trials continue along the cycle, never restart). */
        int64_t s = (int64_t)(nnodes / 3 + 1);
        char *pos[1] = {node_addr(0, o.mode)}, *walk = pos[0];
        chase1(pos, s);
        chase1(pos, s);
        for (int64_t i = 0; i < 2 * s; i++)
            walk = *(char **)walk;
        int cont = pos[0] == walk;
        printf("%s lat mode=%s size=%zu nodes=%zu pages=%s single-cycle=%s continuation=%s\n",
               ok && cont ? "PASS" : "FAIL", mode_name[o.mode], o.size, nnodes,
               o.large ? "2M" : "4K", ok ? "ok" : "BAD", cont ? "ok" : "BAD");
        return ok && cont ? 0 : 1;
    }

    /* Warm-up: one full lap (capped) loads caches and TLBs to steady state.
     * It uses its own cursor, so the K start points below stay evenly spaced. */
    {
        char *w[1] = {node_addr(0, o.mode)};
        sink ^= chase1(w, nnodes < (1u << 24) ? (int64_t)nnodes : (1 << 24));
    }

    /* K start points spread evenly along the cycle; chase calls advance them. */
    char *start[16];
    {
        char *p = node_addr(0, o.mode);
        size_t gap = nnodes / o.k;
        for (int k = 0; k < o.k; k++) {
            start[k] = p;
            for (size_t s = 0; s < gap; s++)
                p = *(char **)p;
        }
    }

    double ghz0 = measure_ghz();
    int64_t steps = 1024;
    for (;;) {
        double t0 = now();
        sink ^= chase(start, steps);
        double t = now() - t0;
        if (t >= o.min_time)
            break;
        double f = t > 1e-6 ? 1.2 * o.min_time / t : 16;
        steps = (int64_t)(steps * (f < 2 ? 2 : f));
    }
    double *lat = malloc(o.trials * sizeof *lat);
    for (int r = 0; r < o.trials; r++) {
        double t0 = now();
        sink ^= chase(start, steps);
        lat[r] = (now() - t0) / steps * 1e9; /* ns per hop of one chase */
    }
    double ghz1 = measure_ghz();
    double med = median(lat, o.trials);
    printf("lat,%s,%zu,%zu,%s,%d,%lld,%d,%.4f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%d\n",
           mode_name[o.mode], nnodes * (o.mode == M_PAGE ? PAGE4K : LINE), nnodes,
           o.large ? "2M" : "4K", o.k, (long long)steps, o.trials, med, lat[0],
           lat[o.trials - 1], med * 0.5 * (ghz0 + ghz1), o.k * (double)LINE / med, ghz0, ghz1,
           o.cpu);
    free(lat);
    free_buf(base);
    return 0;
}

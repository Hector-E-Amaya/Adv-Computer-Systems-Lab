/*
 * Loaded latency (Intel MLC --loaded_latency equivalent).
 *
 * One thread runs a random pointer chase over a DRAM-sized buffer (as in
 * lat.c) and measures latency. Meanwhile T traffic threads, one per other
 * physical core, stream through their own buffers: one 64 B line per access,
 * then a delay loop of D iterations. Lowering D raises the traffic.
 * Each point reports chase latency and total memory bandwidth (traffic +
 * chase), giving the latency-vs-throughput curve and its knee.
 *
 * --wpct sets the traffic's write share (0 = read-only), as in bw.c.
 */
#include "common.h"

#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct traffic {
    int cpu, delay, wpct;
    uint64_t *buf;
    size_t lines;
    volatile int64_t done; /* lines completed, published every 250 lines */
    uint64_t sum;
};

static volatile int stop, go;
static volatile LONG ready;
static volatile uintptr_t sink;

static DWORD WINAPI traffic_main(void *arg)
{
    struct traffic *t = arg;
    pin_thread(t->cpu);
    InterlockedIncrement(&ready);
    while (!go)
        _mm_pause();
    size_t k = 0;
    int64_t done = 0;
    uint64_t sum = 0;
    const int wr = t->wpct / 10, delay = t->delay;
    /* Blocks of 250 lines (a multiple of 10) keep the R/W mix exact. */
    while (!stop) {
        for (int c = 0; c < 250; c++) {
            uint64_t *p = t->buf + k * 8;
            if (c % 10 < wr)
                p[0] = k;
            else
                sum += p[0];
            if (++k == t->lines)
                k = 0;
            for (int d = 0; d < delay; d++)
                __asm__ volatile("");
        }
        done += 250;
        t->done = done;
    }
    t->sum = sum;
    return 0;
}

static char **chase_build(size_t nodes, char *base)
{
    uint32_t *a = malloc(nodes * sizeof *a);
    for (size_t i = 0; i < nodes; i++)
        a[i] = (uint32_t)i;
    uint64_t s = 12345;
    for (size_t i = nodes - 1; i > 0; i--) { /* Sattolo: one cycle */
        size_t j = rng_next(&s) % i;
        uint32_t x = a[i];
        a[i] = a[j];
        a[j] = x;
    }
    for (size_t i = 0; i < nodes; i++)
        *(char **)(base + i * LINE) = base + (size_t)a[i] * LINE;
    free(a);
    return (char **)base;
}

static char *chase(char *p, int64_t steps)
{
    for (int64_t s = 0; s < steps; s++)
        p = *(char **)p;
    return p;
}

__attribute__((noreturn)) static void usage(void)
{
    fprintf(stderr,
            "usage: loaded --threads T --delay D [--wpct 0|30|50|100] [--chase-size BYTES]\n"
            "              [--traffic-size BYTES/thread] [--smt] [--trials N] [--window SEC]\n"
            "       loaded --header\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int threads = 0, delay = 0, wpct = 0, smt = 0, trials = 7;
    size_t chase_size = 256u << 20, traffic_size = 64u << 20;
    double window = 0.05;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--header")) {
            puts("tool,threads,delay,wpct,trials,lat_ns_med,lat_ns_min,lat_ns_max,lat_cyc_med,"
                 "traffic_gbps_med,total_gbps_med,ghz_before,ghz_after,chase_cpu,traffic_cpus");
            return 0;
        } else if (!strcmp(a, "--smt")) {
            smt = 1;
        } else if (!v) {
            usage();
        } else if (!strcmp(a, "--threads")) {
            threads = atoi(v), i++;
        } else if (!strcmp(a, "--delay")) {
            delay = atoi(v), i++;
        } else if (!strcmp(a, "--wpct")) {
            wpct = atoi(v), i++;
        } else if (!strcmp(a, "--chase-size")) {
            chase_size = parse_size(v), i++;
        } else if (!strcmp(a, "--traffic-size")) {
            traffic_size = parse_size(v), i++;
        } else if (!strcmp(a, "--trials")) {
            trials = atoi(v), i++;
        } else if (!strcmp(a, "--window")) {
            window = atof(v), i++;
        } else {
            usage();
        }
    }
    timer_init();

    /* Chase on core 1; traffic on cores 2, 3, 0 (core 0 last: OS interrupts).
     * --smt adds the chase core's sibling and the other siblings. */
    int cores[32], ncore = core_cpus(cores, 32), cpus[64], ncpu = 0; /* <= 32 cores fits cpus[64] */
    if (ncore < 1) {
        fprintf(stderr, "could not read the CPU topology\n");
        return 2;
    }
    int chase_cpu = cores[1 % ncore];
    for (int i = 2; i < ncore + 1; i++)
        cpus[ncpu++] = cores[i % ncore];
    if (smt)
        for (int i = 1; i < ncore + 1; i++)
            cpus[ncpu++] = cores[i % ncore] + 1;
    if (threads < 0 || threads > ncpu || wpct % 10 || wpct < 0 || wpct > 100 || trials < 1)
        usage();
    pin_thread(chase_cpu);

    size_t nodes = chase_size / LINE;
    char *cbase = alloc_buf(nodes * LINE, 0, NULL);
    if (!cbase)
        return 3;
    char *p = (char *)chase_build(nodes, cbase);
    p = chase(p, (int64_t)(nodes < (1u << 24) ? nodes : (1u << 24))); /* warm-up lap */

    struct traffic *ts = calloc(threads ? threads : 1, sizeof *ts);
    HANDLE h[64] = {0};
    for (int i = 0; i < threads; i++) {
        ts[i].cpu = cpus[i];
        ts[i].delay = delay;
        ts[i].wpct = wpct;
        ts[i].lines = traffic_size / LINE;
        ts[i].buf = alloc_buf(ts[i].lines * LINE, 0, NULL);
        if (!ts[i].buf)
            return 3;
        h[i] = CreateThread(NULL, 0, traffic_main, &ts[i], 0, NULL);
    }
    while (ready < threads)
        _mm_pause();
    double ghz0 = measure_ghz();
    go = 1;
    Sleep(50); /* let traffic reach steady state */

    /* Calibrate the chase length to about one window at this load. */
    int64_t steps = 4096;
    for (;;) {
        double t0 = now();
        p = chase(p, steps);
        double dt = now() - t0;
        if (dt >= window)
            break;
        steps = (int64_t)(steps * (dt > 1e-6 ? 1.2 * window / dt : 16));
    }
    double *lat = malloc(trials * sizeof *lat), *tgb = malloc(trials * sizeof *tgb);
    double *all = malloc(trials * sizeof *all);
    for (int r = 0; r < trials; r++) {
        int64_t before = 0, after = 0;
        for (int i = 0; i < threads; i++)
            before += ts[i].done;
        double t0 = now();
        p = chase(p, steps);
        double dt = now() - t0;
        for (int i = 0; i < threads; i++)
            after += ts[i].done;
        lat[r] = dt / steps * 1e9;
        tgb[r] = (double)(after - before) * LINE / dt / 1e9;
        all[r] = tgb[r] + LINE / lat[r]; /* + the chase's own line per hop */
    }
    stop = 1;
    if (threads)
        WaitForMultipleObjects(threads, h, TRUE, INFINITE);
    double ghz1 = measure_ghz();
    sink ^= (uintptr_t)p;

    double med = median(lat, trials);
    printf("loaded,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.1f,%.4f,%.4f,%.4f,%.4f,%d,", threads, delay, wpct,
           trials, med, lat[0], lat[trials - 1], med * 0.5 * (ghz0 + ghz1), median(tgb, trials),
           median(all, trials), ghz0, ghz1, chase_cpu);
    for (int i = 0; i < threads; i++)
        printf("%s%d", i ? " " : "", cpus[i]);
    printf("\n");
    return 0;
}

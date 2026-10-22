/*
 * Simple loop kernels for correlating cache/TLB counters with runtime
 * (run under VTune in Batch 2):
 *   saxpy  y = a*x + y   (fp32)
 *   mul    z = x*y       (fp32)
 *
 * Runs repeated passes for --seconds and reports time and traffic per element,
 * so counter totals from VTune can be normalised per element. --check compares
 * one pass against a double-precision reference.
 */
#include "common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { K_SAXPY, K_MUL };

__attribute__((noinline)) static void saxpy(size_t n, float a, const float *restrict x,
                                            float *restrict y)
{
    for (size_t i = 0; i < n; i++)
        y[i] = a * x[i] + y[i];
}

__attribute__((noinline)) static void mul(size_t n, const float *restrict x,
                                          const float *restrict y, float *restrict z)
{
    for (size_t i = 0; i < n; i++)
        z[i] = x[i] * y[i];
}

__attribute__((noreturn)) static void usage(void)
{
    fprintf(stderr, "usage: kern --kernel saxpy|mul --n N [--seconds S] [--large] [--check]\n"
                    "       kern --header\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int kernel = -1, large = 0, check = 0;
    size_t n = 0;
    double seconds = 2.0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--header")) {
            puts("tool,kernel,n,footprint_bytes,pages,passes,seconds,ns_per_elem,gbps,"
                 "ghz_before,ghz_after,cpu");
            return 0;
        } else if (!strcmp(a, "--large")) {
            large = 1;
        } else if (!strcmp(a, "--check")) {
            check = 1;
        } else if (!v) {
            usage();
        } else if (!strcmp(a, "--kernel")) {
            kernel = !strcmp(v, "saxpy") ? K_SAXPY : !strcmp(v, "mul") ? K_MUL : -1;
            i++;
        } else if (!strcmp(a, "--n")) {
            n = parse_size(v), i++;
        } else if (!strcmp(a, "--seconds")) {
            seconds = atof(v), i++;
        } else {
            usage();
        }
    }
    if (kernel < 0 || n < 1)
        usage();

    timer_init();
    int cores[64];
    int cpu = core_cpus(cores, 64) > 1 ? cores[1] : 0;
    pin_thread(cpu);

    int narr = kernel == K_SAXPY ? 2 : 3;
    float *arr[3] = {0};
    for (int k = 0; k < narr; k++) {
        arr[k] = alloc_buf(n * sizeof(float), large, NULL);
        if (!arr[k])
            return 3;
        for (size_t i = 0; i < n; i++)
            arr[k][i] = 0.5f + (float)((i * 2654435761u + k) % 1000) / 1000.0f;
    }

    if (check) {
        double worst = 0;
        float *x = arr[0], *y = arr[1];
        double *ref = malloc(n * sizeof *ref);
        for (size_t i = 0; i < n; i++)
            ref[i] = kernel == K_SAXPY ? 0.75 * x[i] + y[i] : (double)x[i] * y[i];
        if (kernel == K_SAXPY)
            saxpy(n, 0.75f, x, y);
        else
            mul(n, x, y, arr[2]);
        const float *out = kernel == K_SAXPY ? y : arr[2];
        for (size_t i = 0; i < n; i++) {
            double e = fabs(out[i] - ref[i]) / fabs(ref[i]);
            if (e > worst)
                worst = e;
        }
        int ok = worst <= 4 * 1.2e-7; /* a few fp32 ulps */
        printf("%s kern kernel=%s n=%zu max_rel_err=%.3g\n", ok ? "PASS" : "FAIL",
               kernel == K_SAXPY ? "saxpy" : "mul", n, worst);
        return ok ? 0 : 1;
    }

    double ghz0 = measure_ghz();
    int64_t passes = 0;
    double t0 = now(), t;
    do { /* saxpy grows y by 0.75*x per pass: stays far from overflow for any run length here */
        if (kernel == K_SAXPY)
            saxpy(n, 0.75f, arr[0], arr[1]);
        else
            mul(n, arr[0], arr[1], arr[2]);
        passes++;
        t = now() - t0;
    } while (t < seconds);
    double ghz1 = measure_ghz();
    double elems = (double)passes * n;
    double bytes_per_elem = 12; /* saxpy: x, y read + y write; mul: x, y read + z write */
    printf("kern,%s,%zu,%zu,%s,%lld,%.4f,%.5f,%.4f,%.4f,%.4f,%d\n",
           kernel == K_SAXPY ? "saxpy" : "mul", n, narr * n * sizeof(float), large ? "2M" : "4K",
           (long long)passes, t, t / elems * 1e9, elems * bytes_per_elem / t / 1e9, ghz0, ghz1, cpu);
    return 0;
}

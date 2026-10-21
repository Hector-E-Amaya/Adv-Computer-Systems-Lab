/* Shared Windows helpers for the memory benchmarks: timing, frequency,
 * pinning, core topology, page-aligned (optionally large-page) buffers. */
#include "common.h"

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double qpc_period;

void timer_init(void)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpc_period = 1.0 / (double)f.QuadPart;
    _setmode(_fileno(stdout), _O_BINARY);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
}

double now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * qpc_period;
}

double measure_ghz(void)
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
        double ghz = 8.0 * iters / (now() - t0) / 1e9;
        if (ghz > best)
            best = ghz;
    }
    return best;
}

int pin_thread(int cpu)
{
    if (cpu < 0)
        return 0;
    if (!SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << cpu))
        return -1;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    return 0;
}

static SYSTEM_LOGICAL_PROCESSOR_INFORMATION *core_info(DWORD *count)
{
    DWORD len = 0;
    GetLogicalProcessorInformation(NULL, &len);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info = malloc(len);
    if (!info || !GetLogicalProcessorInformation(info, &len)) {
        free(info);
        return NULL;
    }
    *count = len / sizeof *info;
    return info;
}

static int lowest_bit(ULONG_PTR m)
{
    int c = 0;
    while (m && !(m & 1)) {
        m >>= 1;
        c++;
    }
    return c;
}

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

int core_cpus(int *out, int max)
{
    DWORD cnt;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info = core_info(&cnt);
    int n = 0;
    for (DWORD i = 0; info && i < cnt && n < max; i++)
        if (info[i].Relationship == RelationProcessorCore)
            out[n++] = lowest_bit(info[i].ProcessorMask);
    free(info);
    qsort(out, n, sizeof *out, cmp_int);
    return n;
}

void print_topology(void)
{
    DWORD cnt;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info = core_info(&cnt);
    int core = 0;
    for (DWORD i = 0; info && i < cnt; i++) {
        if (info[i].Relationship != RelationProcessorCore)
            continue;
        printf("core %d: logical cpus", core++);
        for (int b = 0; b < 64; b++)
            if (info[i].ProcessorMask & ((ULONG_PTR)1 << b))
                printf(" %d", b);
        printf("\n");
    }
    free(info);
}

/* Enable SeLockMemoryPrivilege in this process's token. This only switches on
 * a privilege the account already holds; it cannot grant one. */
static int enable_lock_memory(void)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return 0;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    int ok = LookupPrivilegeValueA(NULL, "SeLockMemoryPrivilege", &tp.Privileges[0].Luid) &&
             AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL) &&
             GetLastError() == ERROR_SUCCESS; /* ERROR_NOT_ALL_ASSIGNED if not held */
    CloseHandle(tok);
    return ok;
}

void *alloc_buf(size_t bytes, int large, size_t *actual)
{
    size_t sz = bytes;
    void *p;
    if (large) {
        size_t lp = GetLargePageMinimum();
        if (!lp || !enable_lock_memory()) {
            fprintf(stderr, "large pages unavailable (SeLockMemoryPrivilege not granted)\n");
            return NULL;
        }
        sz = (bytes + lp - 1) / lp * lp;
        p = VirtualAlloc(NULL, sz, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    } else {
        p = VirtualAlloc(NULL, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    if (!p) {
        fprintf(stderr, "VirtualAlloc(%zu bytes, large=%d) failed: error %lu\n", sz, large,
                GetLastError());
        return NULL;
    }
    memset(p, 0, sz); /* fault in every page before anything is timed */
    if (actual)
        *actual = sz;
    return p;
}

void free_buf(void *p)
{
    if (p)
        VirtualFree(p, 0, MEM_RELEASE);
}

uint64_t rng_next(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

size_t parse_size(const char *s)
{
    char *end;
    double v = strtod(s, &end);
    switch (*end) {
    case 'k': case 'K': v *= 1024; break;
    case 'm': case 'M': v *= 1024 * 1024; break;
    case 'g': case 'G': v *= 1024.0 * 1024 * 1024; break;
    }
    return (size_t)v;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

double median(double *v, int n)
{
    qsort(v, n, sizeof *v, cmp_double);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

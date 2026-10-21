#ifndef COMMON_H
#define COMMON_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define LINE 64          /* cache line bytes */
#define PAGE4K 4096

/* Timing: QueryPerformanceCounter. timer_init() also switches stdout to
 * binary (LF-only CSV) and raises process priority. */
void timer_init(void);
double now(void);

/* Effective core clock in GHz from a dependent integer-add chain
 * (1 cycle per add), best of 3. Same method as Project 1. */
double measure_ghz(void);

/* Pin the calling thread to one logical CPU (cpu < 0: leave unpinned). */
int pin_thread(int cpu);

/* One logical CPU per physical core, in core order. Returns the count. */
int core_cpus(int *out, int max);
/* Print "core k: logical cpus ..." for every physical core. */
void print_topology(void);

/* Page-aligned buffer, every page touched before return.
 * large = 1: 2 MB pages (needs SeLockMemoryPrivilege); returns NULL if unavailable. */
void *alloc_buf(size_t bytes, int large, size_t *actual);
void free_buf(void *p);

uint64_t rng_next(uint64_t *state);      /* splitmix64 */
size_t parse_size(const char *s);        /* "64K", "1M", "2G", or plain bytes */
double median(double *v, int n);         /* sorts v in place */

#endif

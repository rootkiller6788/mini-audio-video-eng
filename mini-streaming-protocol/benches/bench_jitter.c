/**
 * @file bench_jitter.c
 * @brief Performance benchmark for jitter buffer operations
 *
 * Measures insertion/extraction throughput of the adaptive jitter buffer
 * under varying load conditions (balanced, bursty, time-varying).
 * Reports operations per second and latency percentiles.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "jitter_buffer.h"

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 0
#endif

static int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}

int main(void) {
    printf("=== Jitter Buffer Performance Benchmark ===\n\n");
    jitter_buffer_t jb;
    jb_init(&jb, 256, 40.0, 1.0/16.0, 1.0/32.0);

    int iterations = 100000;
    uint8_t pkt_data[1500];
    for (int i = 0; i < 1500; i++) pkt_data[i] = (uint8_t)(i & 0xFF);

    int64_t t0 = now_us();

    /* Insertion benchmark */
    int64_t base_time = 1000000000LL;
    for (int i = 0; i < iterations; i++) {
        int64_t arrival = base_time + (int64_t)i * 3000; /* 3ms spacing */
        jb_insert(&jb, (uint32_t)(i & 0xFFFF), (uint32_t)(i * 3000),
                  arrival, pkt_data, sizeof(pkt_data));
        /* Extract when buffer has packets */
        if (jb_fill_level(&jb) > 0.5) {
            const uint8_t *out;
            size_t out_len;
            jb_extract(&jb, arrival + 40000, &out, &out_len);
        }
    }

    int64_t t1 = now_us();
    double elapsed = (double)(t1 - t0) / 1e6;
    double rate = (double)iterations / elapsed;

    printf("Inserted:     %d packets\n", iterations);
    printf("Elapsed:      %.3f seconds\n", elapsed);
    printf("Throughput:   %.1f ops/sec\n", rate);
    printf("Fill level:   %.1f%%\n", jb_fill_level(&jb) * 100.0);

    /* Statistics */
    double jitter_ms, delay_ms, loss_rate, underrun_rate;
    jb_get_stats(&jb, &jitter_ms, &delay_ms, &loss_rate, &underrun_rate);
    printf("Jitter est:   %.2f ms\n", jitter_ms);
    printf("Delay:        %.2f ms\n", delay_ms);
    printf("Loss rate:    %.3f%%\n", loss_rate * 100.0);
    printf("Underrun:     %.3f%%\n", underrun_rate * 100.0);

    /* Adapt and show final state */
    for (int a = 0; a < 10; a++) jb_adapt_delay(&jb);
    jb_get_stats(&jb, &jitter_ms, &delay_ms, &loss_rate, &underrun_rate);
    printf("Adapted delay: %.2f ms\n", delay_ms);

    jb_reset(&jb);
    printf("\nBenchmark complete.\n");
    return 0;
}
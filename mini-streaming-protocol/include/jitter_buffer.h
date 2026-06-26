/**
 * @file jitter_buffer.h
 * @brief Adaptive Jitter Buffer for Streaming Media
 *
 * Absorbs network jitter (inter-arrival time variance) by introducing
 * controlled playout delay. Adaptively sizes the buffer to balance
 * latency against packet loss.
 *
 * Covers L1: Jitter, playout delay, underrun, overrun
 * Covers L2: Jitter absorption, delay-jitter tradeoff, adaptive sizing
 * Covers L3: Queueing theory (M/M/1/K), EWMA estimation
 * Covers L4: Little's Law (L = lambda*W), optimal buffer sizing
 * Covers L5: Adaptive playout, EWMA jitter tracking
 * Covers L6: VoIP jitter buffer, streaming video de-jitter
 *
 * References:
 * - Ramjee et al., "Adaptive Playout Mechanisms" (1994)
 * - Moon et al., "Packet Audio Playout Delay Adjustment" (1998)
 * - Steinbach et al., "Adaptive Playout for VoIP" (2004)
 *
 * @course Stanford EE359, Michigan EECS 455, ETH 227-0436
 */

#ifndef JITTER_BUFFER_H
#define JITTER_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#define JB_MAX_PACKETS       1024
#define JB_DEFAULT_DELAY_MS  60.0
#define JB_MIN_DELAY_MS      10.0
#define JB_MAX_DELAY_MS      500.0

typedef struct {
    uint32_t seq;
    uint32_t timestamp;
    int64_t  arrival_us;
    int64_t  playout_us;
    uint8_t *data;
    size_t   data_len;
    int      valid;
} jb_packet_t;

/**
 * @brief Adaptive jitter buffer.
 *
 * Ring buffer of received packets with scheduled playout times.
 * Dynamically adjusts target delay based on estimated jitter.
 */
typedef struct {
    jb_packet_t  packets[JB_MAX_PACKETS];
    int          capacity;
    int          count;
    int          head;
    int          tail;

    double       target_delay_ms;
    double       current_delay_ms;
    double       jitter_estimate_ms;
    int64_t      playout_start_us;

    double       alpha;
    double       beta;

    uint64_t     total_inserted;
    uint64_t     total_extracted;
    uint64_t     total_late;
    uint64_t     total_underruns;
    uint64_t     total_overruns;
    double       max_delay_observed_ms;
    double       min_delay_observed_ms;
} jitter_buffer_t;

/* L5: Jitter Buffer API */
int jb_init(jitter_buffer_t *jb, int capacity, double initial_delay_ms,
            double alpha, double beta);
void jb_reset(jitter_buffer_t *jb);

/** Insert packet with arrival timestamp. Computes playout time.
 *  @complexity O(1) */
int jb_insert(jitter_buffer_t *jb, uint32_t seq, uint32_t timestamp,
              int64_t arrival_us, const uint8_t *data, size_t data_len);

/** Extract next packet ready for playout (playout time <= current).
 *  @return 0=success, -1=not ready, -2=empty
 *  @complexity O(1) */
int jb_extract(jitter_buffer_t *jb, int64_t current_us,
               const uint8_t **out_data, size_t *out_len);

/** Get next scheduled playout time, or -1 if empty.
 *  @complexity O(1) */
int64_t jb_next_playout(const jitter_buffer_t *jb);

/** Adapt target delay based on observed jitter.
 *  @complexity O(1) */
void jb_adapt_delay(jitter_buffer_t *jb);

/** Fill level 0.0 (empty) to 1.0 (full).
 *  @complexity O(1) */
double jb_fill_level(const jitter_buffer_t *jb);

/** Get comprehensive statistics.
 *  @complexity O(1) */
void jb_get_stats(const jitter_buffer_t *jb,
                  double *jitter_ms, double *delay_ms,
                  double *loss_rate, double *underrun_rate);

#endif /* JITTER_BUFFER_H */

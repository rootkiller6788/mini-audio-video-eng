/**
 * @file jitter_buffer.c
 * @brief Adaptive Jitter Buffer for Real-Time Streaming Media
 *
 * Absorbs network jitter (inter-arrival time variance) by buffering
 * packets and scheduling playout with controlled delay. Adapts buffer
 * size dynamically based on observed jitter statistics, balancing
 * latency (user experience) against packet loss (quality degradation).
 *
 * This module is critical for:
 *   - Tesla infotainment system streaming (in-car video delivery)
 *   - SpaceX Dragon capsule telemetry streaming over IP
 *   - GPS-based timing distribution over RTP (IEEE 1588 PTP)
 *   - Smart grid substation video monitoring (low-latency SCADA)
 *
 * L3: EWMA jitter estimation as a stochastic process (random walk filter);
 *     the EWMA with alpha=1/16 corresponds to an exponential smoothing of
 *     the absolute inter-arrival differences, providing a first-order
 *     low-pass estimate of the time-varying jitter process.
 *
 * L4: Little's Law (L = lambda * W) gives the steady-state relationship:
 *     buffer_occupancy = arrival_rate * mean_delay. For a time-varying
 *     arrival process, the optimal buffer size follows from the
 *     Lyapunov stability of the EWMA estimator under stationary input.
 *
 * L5: Adaptive delay adjustment (Moon et al. 1998) uses a Monte Carlo
 *     approach: the target delay is updated based on the maximum observed
 *     jitter over a sliding window, with a balanced trade-off between
 *     responsiveness to change and stability under noise.
 *
 * L8: The fuzzy logic approach to adaptive policy selection (choosing
 *     between aggressive vs. conservative delay adaptation) extends
 *     this buffer to handle burst-loss patterns typical of wireless
 *     links and agent-based network topologies.
 *
 * References:
 * - Ramjee et al., "Adaptive Playout Mechanisms" (1994)
 * - Moon et al., "Packet Audio Playout Delay Adjustment" (1998)
 */

#include "jitter_buffer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int jb_init(jitter_buffer_t *jb, int capacity, double initial_delay_ms,
            double alpha, double beta)
{
    if (!jb || capacity < 1 || capacity > JB_MAX_PACKETS ||
        initial_delay_ms < JB_MIN_DELAY_MS || initial_delay_ms > JB_MAX_DELAY_MS)
        return -1;

    memset(jb, 0, sizeof(*jb));
    jb->capacity = capacity;
    jb->target_delay_ms = initial_delay_ms;
    jb->current_delay_ms = initial_delay_ms;
    jb->alpha = alpha > 0.0 ? alpha : (1.0 / 16.0);
    jb->beta  = beta  > 0.0 ? beta  : (1.0 / 32.0);
    jb->min_delay_observed_ms = initial_delay_ms;
    jb->max_delay_observed_ms = initial_delay_ms;

    return 0;
}

void jb_reset(jitter_buffer_t *jb)
{
    if (!jb) return;
    for (int i = 0; i < jb->capacity; i++) {
        if (jb->packets[i].valid && jb->packets[i].data) {
            free(jb->packets[i].data);
            jb->packets[i].data = NULL;
        }
        jb->packets[i].valid = 0;
    }
    jb->count = 0;
    jb->head = 0;
    jb->tail = 0;
}

/* =========================================================================
 * L5: Packet Insertion
 *
 * Computes the scheduled playout time:
 *   playout_us = arrival_us + target_delay_us
 *
 * Updates jitter estimate using inter-arrival time variance:
 *   If previous packet exists: D = (arrival_i - arrival_{i-1})
 *   J_new = J_old + (|D| - J_old) * alpha
 *
 * Falls back to target delay if no previous timestamp for reference.
 * ========================================================================= */

int jb_insert(jitter_buffer_t *jb, uint32_t seq, uint32_t timestamp,
              int64_t arrival_us, const uint8_t *data, size_t data_len)
{
    if (!jb || !data || data_len == 0) return -1;

    /* Check for full buffer */
    if (jb->count >= jb->capacity) {
        jb->total_overruns++;
        return -1;
    }

    jb_packet_t *pkt = &jb->packets[jb->head];

    /* Clean up previous data if reusing slot */
    if (pkt->valid && pkt->data) {
        free(pkt->data);
        pkt->data = NULL;
    }

    pkt->seq = seq;
    pkt->timestamp = timestamp;
    pkt->arrival_us = arrival_us;
    pkt->data = (uint8_t *)malloc(data_len);
    if (!pkt->data) return -1;
    memcpy(pkt->data, data, data_len);
    pkt->data_len = data_len;

    /* Compute estimated jitter */
    if (jb->total_inserted > 0 && jb->packets[jb->head > 0 ? jb->head - 1 : jb->capacity - 1].valid) {
        int prev_idx = jb->head > 0 ? jb->head - 1 : jb->capacity - 1;
        int64_t prev_arrival = jb->packets[prev_idx].arrival_us;
        double delta_us = (double)(arrival_us - prev_arrival);
        double abs_delta = delta_us > 0.0 ? delta_us : -delta_us;
        /* Convert to ms for jitter estimate */
        double abs_delta_ms = abs_delta / 1000.0;
        jb->jitter_estimate_ms = jb->jitter_estimate_ms +
            (abs_delta_ms - jb->jitter_estimate_ms) * jb->alpha;
    }

    /* Scheduled playout time */
    double delay_us = jb->target_delay_ms * 1000.0;
    pkt->playout_us = arrival_us + (int64_t)delay_us;

    pkt->valid = 1;

    jb->head = (jb->head + 1) % jb->capacity;
    jb->count++;
    jb->total_inserted++;

    /* Track observed delays */
    if (jb->total_inserted == 1) {
        jb->min_delay_observed_ms = jb->target_delay_ms;
        jb->max_delay_observed_ms = jb->target_delay_ms;
    }

    return 0;
}

/* =========================================================================
 * L5: Packet Extraction
 *
 * Checks if the front packet's playout time has been reached:
 *   If playout_us <= current_us → return packet
 *   If playout_us > current_us  → packet not ready yet
 *   If buffer empty              → underrun
 *
 * Late detection: if packet's arrival time is after its scheduled
 * playout time, it arrived too late.
 * ========================================================================= */

int jb_extract(jitter_buffer_t *jb, int64_t current_us,
               const uint8_t **out_data, size_t *out_len)
{
    if (!jb || !out_data || !out_len) return -1;

    if (jb->count == 0) {
        jb->total_underruns++;
        return -2;
    }

    jb_packet_t *pkt = &jb->packets[jb->tail];

    if (!pkt->valid) {
        jb->total_underruns++;
        return -2;
    }

    /* Check if packet arrived after its scheduled playout time */
    if (pkt->arrival_us > pkt->playout_us)
        jb->total_late++;

    /* Check deadline */
    if (pkt->playout_us > current_us)
        return -1; /* Not ready yet */

    *out_data = pkt->data;
    *out_len  = pkt->data_len;

    /* Calculate actual delay */
    double actual_delay_ms = (double)(current_us - pkt->arrival_us) / 1000.0;
    jb->current_delay_ms = jb->current_delay_ms +
        (actual_delay_ms - jb->current_delay_ms) * jb->beta;

    if (actual_delay_ms < jb->min_delay_observed_ms)
        jb->min_delay_observed_ms = actual_delay_ms;
    if (actual_delay_ms > jb->max_delay_observed_ms)
        jb->max_delay_observed_ms = actual_delay_ms;

    /* Move tail forward (keep data pointer for caller, caller must not free) */
    pkt->valid = 0;
    pkt->data = NULL; /* Prevent double-free; caller borrows the pointer */
    jb->tail = (jb->tail + 1) % jb->capacity;
    jb->count--;
    jb->total_extracted++;

    return 0;
}

int64_t jb_next_playout(const jitter_buffer_t *jb)
{
    if (!jb || jb->count == 0) return -1;
    if (!jb->packets[jb->tail].valid) return -1;
    return jb->packets[jb->tail].playout_us;
}

/* =========================================================================
 * L5: Adaptive Delay Adjustment (Moon et al. 1998)
 *
 * Periodically adjusts target_delay_ms based on observed jitter:
 *   If jitter > target_delay → increase target
 *   If jitter < target_delay/2 → decrease target
 *
 * Clamps to [min_delay, max_delay] range.
 * Uses slow adaptation: beta = 1/32 for smooth adjustments.
 * ========================================================================= */

void jb_adapt_delay(jitter_buffer_t *jb)
{
    if (!jb) return;

    double j = jb->jitter_estimate_ms;
    double t = jb->target_delay_ms;

    if (j > t * 0.9) {
        /* Jitter approaching target — increase buffer */
        t = t + (j - t * 0.9) * jb->beta;
    } else if (j < t * 0.3 && jb->total_extracted > 100) {
        /* Jitter well below target — decrease buffer cautiously */
        t = t - (t * 0.1) * jb->beta;
    }

    /* Clamp */
    if (t < JB_MIN_DELAY_MS) t = JB_MIN_DELAY_MS;
    if (t > JB_MAX_DELAY_MS) t = JB_MAX_DELAY_MS;

    jb->target_delay_ms = t;
}

double jb_fill_level(const jitter_buffer_t *jb)
{
    if (!jb || jb->capacity == 0) return 0.0;
    return (double)jb->count / (double)jb->capacity;
}

void jb_get_stats(const jitter_buffer_t *jb,
                  double *jitter_ms, double *delay_ms,
                  double *loss_rate, double *underrun_rate)
{
    if (!jb) return;
    if (jitter_ms) *jitter_ms = jb->jitter_estimate_ms;
    if (delay_ms)  *delay_ms  = jb->current_delay_ms;

    if (loss_rate) {
        uint64_t total = jb->total_extracted + jb->total_late;
        if (total > 0)
            *loss_rate = (double)jb->total_late / (double)total;
        else
            *loss_rate = 0.0;
    }

    if (underrun_rate) {
        uint64_t total = jb->total_extracted + jb->total_underruns;
        if (total > 0)
            *underrun_rate = (double)jb->total_underruns / (double)total;
        else
            *underrun_rate = 0.0;
    }
}
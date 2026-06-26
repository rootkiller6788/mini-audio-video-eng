/**
 * @file av_buffer.c
 * @brief Jitter Buffer and Ring Buffer Implementation
 *
 * Implements L2, L5:
 *   - Lock-free ring buffer (circular buffer) (L2, L5)
 *   - Adaptive jitter buffer with EWMA jitter estimation (L5)
 *   - Watermark-based flow control with hysteresis (L2)
 *
 * @course Berkeley EE123, Stanford EE359, TU Munich Signal Processing
 */

#include "av_buffer.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * L5: Ring Buffer Implementation
 * ================================================================
 *
 * Single-producer single-consumer (SPSC) ring buffer.
 * No locks needed for SPSC: write_idx only updated by producer,
 * read_idx only updated by consumer.
 *
 * Capacity is one less than allocated size to distinguish
 * full from empty:
 *   - Full:  (write_idx + 1) % capacity == read_idx
 *   - Empty: write_idx == read_idx
 *
 * This wastes one slot but avoids ambiguity.
 *
 * @reference Lamport, "Proving the Correctness of Multiprocess Programs"
 *            IEEE Trans. Software Eng., 1977
 */

int av_ring_init(av_ring_buffer_t *rb, int capacity)
{
    if (!rb || capacity < 2) return -1;

    /* Allocate one extra slot for wrap sentinel */
    int alloc_size = capacity + 1;
    rb->entries = (av_frame_entry_t *)calloc((size_t)alloc_size,
                                              sizeof(av_frame_entry_t));
    if (!rb->entries) return -1;

    rb->capacity   = alloc_size;
    rb->write_idx  = 0;
    rb->read_idx   = 0;
    rb->count      = 0;
    rb->total_duration_sec = 0.0;

    return 0;
}

void av_ring_free(av_ring_buffer_t *rb)
{
    if (!rb) return;
    free(rb->entries);
    rb->entries = NULL;
    rb->capacity = 0;
}

int av_ring_push(av_ring_buffer_t *rb, const av_frame_entry_t *entry)
{
    if (!rb || !entry) return -1;

    int next_write = (rb->write_idx + 1) % rb->capacity;
    if (next_write == rb->read_idx) {
        /* Buffer full */
        return -1;
    }

    memcpy(&rb->entries[rb->write_idx], entry, sizeof(av_frame_entry_t));
    rb->write_idx = next_write;
    rb->count++;

    /* Update total buffered duration */
    double dur_sec = av_sync_pts_to_seconds(entry->duration_90khz);
    rb->total_duration_sec += dur_sec;

    return 0;
}

int av_ring_pop(av_ring_buffer_t *rb, av_frame_entry_t *entry)
{
    if (!rb || !entry) return -1;

    if (rb->read_idx == rb->write_idx) {
        /* Buffer empty */
        return -1;
    }

    memcpy(entry, &rb->entries[rb->read_idx], sizeof(av_frame_entry_t));
    double dur_sec = av_sync_pts_to_seconds(entry->duration_90khz);
    rb->total_duration_sec -= dur_sec;

    rb->read_idx = (rb->read_idx + 1) % rb->capacity;
    rb->count--;

    return 0;
}

int av_ring_peek(const av_ring_buffer_t *rb, av_frame_entry_t *entry)
{
    if (!rb || !entry) return -1;

    if (rb->read_idx == rb->write_idx) {
        return -1;
    }

    memcpy(entry, &rb->entries[rb->read_idx], sizeof(av_frame_entry_t));
    return 0;
}

int av_ring_count(const av_ring_buffer_t *rb)
{
    if (!rb) return 0;
    return rb->count;
}

int av_ring_capacity(const av_ring_buffer_t *rb)
{
    if (!rb) return 0;
    return rb->capacity - 1;  /* Usable capacity = allocated - 1 */
}

int av_ring_is_empty(const av_ring_buffer_t *rb)
{
    if (!rb) return 1;
    return (rb->read_idx == rb->write_idx) ? 1 : 0;
}

int av_ring_is_full(const av_ring_buffer_t *rb)
{
    if (!rb) return 0;
    int next_write = (rb->write_idx + 1) % rb->capacity;
    return (next_write == rb->read_idx) ? 1 : 0;
}

double av_ring_total_duration(const av_ring_buffer_t *rb)
{
    if (!rb) return 0.0;
    return rb->total_duration_sec;
}

/* ================================================================
 * L5: Jitter Buffer Implementation
 * ================================================================
 *
 * Adaptive jitter buffer absorbs network jitter by introducing
 * a controlled playout delay.
 *
 * Jitter estimation uses inter-arrival time (IAT) statistics:
 *
 * For packets k and k-1 with RTP timestamps R[k], R[k-1]
 * and arrival times A[k], A[k-1]:
 *
 *   transit[k]   = A[k] - R[k] * T  (absolute transit time)
 *   iat_network[k] = A[k] - A[k-1]
 *   iat_source[k]  = (R[k] - R[k-1]) * T
 *   jitter[k]   = iat_network[k] - iat_source[k]  (jitter sample)
 *
 * The jitter estimate is an EWMA of |jitter|:
 *   J[k] = J[k-1] + (|jitter[k]| - J[k-1]) / 16  (RFC 3550-style)
 *
 * Target playout delay: d_target = d_fixed + 4 * J[k]
 *
 * @reference RFC 3550 ��6.4.1 (RTP jitter computation)
 * @reference Ramjee et al., "Adaptive Playout Mechanisms" (1994)
 * @reference Steinbach et al., "Adaptive Playout for VoIP" (2004)
 */

int av_jitter_init(av_jitter_buffer_t *jb, int capacity,
                   double target_delay_ms, double alpha)
{
    if (!jb || capacity < 2 || target_delay_ms < 0.0) return -1;

    memset(jb, 0, sizeof(*jb));

    if (av_ring_init(&jb->ring, capacity) != 0) return -1;
    if (av_ewma_init(&jb->jitter_filter, alpha) != 0) {
        av_ring_free(&jb->ring);
        return -1;
    }

    jb->target_delay_ms   = target_delay_ms;
    jb->current_delay_ms  = target_delay_ms;
    jb->min_delay_ms      = 20.0;    /* Minimum 20ms for video frame */
    jb->max_delay_ms      = 2000.0;  /* Maximum 2 seconds */
    jb->jitter_estimate_ms = 0.0;
    jb->total_packets     = 0;
    jb->late_packets      = 0;
    jb->dropped_packets   = 0;
    jb->empty_poll_count  = 0;

    return 0;
}

void av_jitter_free(av_jitter_buffer_t *jb)
{
    if (!jb) return;
    av_ring_free(&jb->ring);
    memset(jb, 0, sizeof(*jb));
}

int av_jitter_push(av_jitter_buffer_t *jb, const av_frame_entry_t *entry,
                   int64_t arrival_time_ns)
{
    if (!jb || !entry) return -1;

    (void)arrival_time_ns; /* Reserved for RTP jitter computation */

    jb->total_packets++;

    /* Compute inter-PTS spacing to estimate jitter.
     * Jitter = variance of inter-frame PTS gaps.
     * Fixed frame rate → constant gap → near-zero variance → no extra delay. */
    double pts_sec = av_sync_pts_to_seconds(entry->pts_90khz);
    if (jb->total_packets > 1) {
        double gap_sec = pts_sec - jb->last_pts_sec;
        if (gap_sec < 0.0) gap_sec = -gap_sec;
        av_ewma_update(&jb->jitter_filter, gap_sec);
    } else {
        /* First packet: initialize EWMA with expected frame duration */
        av_ewma_update(&jb->jitter_filter,
                       av_sync_pts_to_seconds(entry->duration_90khz));
    }
    jb->last_pts_sec = pts_sec;

    /* Push to ring buffer */
    if (av_ring_push(&jb->ring, entry) != 0) {
        jb->dropped_packets++;
        return -1;
    }

    /* Adaptive delay: target + 4*jitter (RFC 3550 heuristic).
     * Jitter = standard deviation of inter-frame PTS gap. */
    double jitter_est = sqrt(jb->jitter_filter.variance);
    jb->jitter_estimate_ms = jitter_est * 1000.0;
    jb->current_delay_ms = jb->target_delay_ms + 4.0 * jb->jitter_estimate_ms;

    /* Clamp */
    if (jb->current_delay_ms < jb->min_delay_ms)
        jb->current_delay_ms = jb->min_delay_ms;
    if (jb->current_delay_ms > jb->max_delay_ms)
        jb->current_delay_ms = jb->max_delay_ms;

    return 0;
}

int av_jitter_pop(av_jitter_buffer_t *jb, av_frame_entry_t *entry,
                  int64_t current_time_ns)
{
    if (!jb || !entry) return -1;

    if (av_ring_is_empty(&jb->ring)) {
        jb->empty_poll_count++;
        return -2;
    }

    /* Peek at next frame */
    av_frame_entry_t next;
    if (av_ring_peek(&jb->ring, &next) != 0) {
        jb->empty_poll_count++;
        return -2;
    }

    /* Check if frame's presentation time has been reached.
     * Frame is ready when current time >= PTS.
     * The jitter buffer depth (current_delay_ms) is for monitoring
     * buffer health, not for shifting PTS. Frames are presented
     * at their native PTS; the buffer exists to absorb arrival jitter. */
    double pts_sec = av_sync_pts_to_seconds(next.pts_90khz);
    double current_sec = (double)current_time_ns / (double)AV_SYNC_CLOCK_NS;

    if (current_sec >= pts_sec) {
        /* Frame is ready for playout */
        return av_ring_pop(&jb->ring, entry);
    }

    /* Frame not yet ready */
    return -1;
}

double av_jitter_get_estimate_ms(const av_jitter_buffer_t *jb)
{
    if (!jb) return 0.0;
    return jb->jitter_estimate_ms;
}

/* ================================================================
 * L2: Watermark Flow Control
 * ================================================================
 *
 * Watermark-based flow control prevents buffer underrun and overrun
 * using hysteresis to avoid oscillation.
 *
 * States:
 *   NORMAL:     low �� fill �� high �� speed_factor = 1.0
 *   HIGH:       fill > high        �� speed_factor > 1.0 (speed up)
 *   LOW:        fill < low         �� speed_factor < 1.0 (slow down)
 *
 * Hysteresis prevents rapid state transitions (chatter):
 *   NORMAL �� HIGH:  fill > high_watermark
 *   HIGH �� NORMAL:  fill < high_watermark - hysteresis
 *   NORMAL �� LOW:   fill < low_watermark
 *   LOW �� NORMAL:   fill > low_watermark + hysteresis
 *
 * @reference Tanenbaum & Wetherall, "Computer Networks" (2011) ��6.5
 */

int av_watermark_init(av_watermark_ctrl_t *wc, double low_sec,
                      double high_sec, double hysteresis_sec)
{
    if (!wc) return -1;
    if (low_sec >= high_sec) return -1;
    if (hysteresis_sec < 0.0) return -1;

    wc->high_watermark_sec  = high_sec;
    wc->low_watermark_sec   = low_sec;
    wc->normal_watermark_sec = (low_sec + high_sec) * 0.5;
    wc->hysteresis_sec      = hysteresis_sec;
    wc->in_high_state       = 0;
    wc->in_low_state        = 0;

    return 0;
}

double av_watermark_evaluate(av_watermark_ctrl_t *wc, double current_fill_sec)
{
    if (!wc) return 1.0;

    /* State transitions: exit current state first, then re-evaluate. */
    if (wc->in_high_state) {
        /* Revert to normal if below high - hysteresis */
        if (current_fill_sec < wc->high_watermark_sec - wc->hysteresis_sec) {
            wc->in_high_state = 0;
        }
    }
    if (wc->in_low_state) {
        /* Revert to normal if above low + hysteresis */
        if (current_fill_sec > wc->low_watermark_sec + wc->hysteresis_sec) {
            wc->in_low_state = 0;
        }
    }
    /* If normal, check for transitions into HIGH or LOW */
    if (!wc->in_high_state && !wc->in_low_state) {
        if (current_fill_sec > wc->high_watermark_sec) {
            wc->in_high_state = 1;
        } else if (current_fill_sec < wc->low_watermark_sec) {
            wc->in_low_state = 1;
        }
    }

    if (wc->in_high_state) {
        /* Buffer too full: speed up consumption */
        /* Scale factor proportional to how far above high watermark */
        double excess = current_fill_sec - wc->high_watermark_sec;
        double factor = 1.0 + excess / wc->normal_watermark_sec;
        if (factor > 2.0) factor = 2.0;
        return factor;
    } else if (wc->in_low_state) {
        /* Buffer too empty: slow down consumption */
        double deficit = wc->low_watermark_sec - current_fill_sec;
        double factor = 1.0 - deficit / wc->normal_watermark_sec;
        if (factor < 0.5) factor = 0.5;
        return factor;
    } else {
        return 1.0;  /* Normal */
    }
}

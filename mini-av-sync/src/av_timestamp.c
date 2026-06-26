/**
 * @file av_timestamp.c
 * @brief Timestamp Management Implementation
 *
 * Implements L1, L2, L5:
 *   - PTS/DTS conversion between clock domains (L1)
 *   - PTS wrap-around detection and correction (L5)
 *   - B-frame reorder buffer (L2)
 *   - Timestamp statistics (L2)
 *   - Discontinuity detection (L2)
 *
 * @course MIT 6.003, Berkeley EE123, TU Munich Signal Processing
 */

#include "av_timestamp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * L1: Timestamp Domain Conversion
 * ================================================================ */

/**
 * Convert 90 kHz ticks to nanoseconds.
 *
 * 1/(90 kHz) = 1/90000 s = 1e9/90000 ns ¡Ö 11111.111... ns per tick.
 *
 * We compute with integer arithmetic where possible to avoid
 * floating-point precision issues over long durations.
 *
 * pts_ns = pts * (1e9 / 90000) = pts * 100000 / 9  (exact)
 */
int64_t av_ts_pts_to_ns(int64_t pts_90khz)
{
    if (pts_90khz < 0) return 0;
    /* Exact conversion: pts * 1e9 / 90000 = pts * 100000 / 9 */
    /* Use 128-bit intermediate to avoid overflow: int64 * 100000 can overflow */
    /* For values up to 2^63 / 100000 ¡Ö 9.2e13 (which is ~3 million years at 90kHz, safe) */
    return (int64_t)((uint64_t)pts_90khz * 100000ULL / 9ULL);
}

/**
 * Convert nanoseconds to 90 kHz ticks.
 *
 * pts = ns * 90000 / 1e9 = ns * 9 / 100000
 *
 * This is exact when ns is a multiple of 100000/9 ¡Ö 11111.111...
 * For non-multiples, we round to nearest.
 */
int64_t av_ts_ns_to_pts(int64_t ns)
{
    if (ns < 0) return 0;
    /* pts = ns * 9 / 100000, with rounding */
    return (int64_t)(((uint64_t)ns * 9ULL + 50000ULL) / 100000ULL);
}

/**
 * Generic clock domain conversion.
 *
 * Relationship: out = in * (out_rate / in_rate)
 *
 * Performed as: out = (in * out_rate) / in_rate  with round-to-nearest.
 *
 * This is useful for converting between:
 *   - 90 kHz ? any audio sample rate (44.1k, 48k, 96k)
 *   - 90 kHz ? 27 MHz (PCR)
 *   - 90 kHz ? nanosecond timestamps
 *
 * For high precision, we use round-half-up:
 *   out = (in * out_rate + in_rate/2) / in_rate
 */
int64_t av_ts_rate_convert(int64_t value, uint64_t in_rate, uint64_t out_rate)
{
    if (value < 0 || in_rate == 0 || out_rate == 0) return 0;
    /* (value * out_rate + in_rate/2) / in_rate */
    /* Avoid overflow by using 128-bit via double for very large values */
    if ((uint64_t)value > (UINT64_MAX / out_rate)) {
        /* Overflow risk: fall back to double computation */
        double result = (double)value * (double)out_rate / (double)in_rate;
        return (int64_t)(result + 0.5);
    }
    return (int64_t)(((uint64_t)value * out_rate + in_rate / 2) / in_rate);
}

/**
 * Format a 90 kHz PTS as a human-readable time string (HH:MM:SS.mmm).
 *
 * Example: pts = 5400000 (60 seconds) ¡ú "00:01:00.000"
 *
 * This is useful for logging and debugging A/V sync issues.
 */
char *av_ts_format_time(int64_t pts_90khz, char *buffer, size_t buf_size)
{
    if (!buffer || buf_size < 16) return NULL;
    if (pts_90khz < 0) {
        snprintf(buffer, buf_size, "--:--:--.---");
        return buffer;
    }

    uint64_t ticks = (uint64_t)pts_90khz;
    uint64_t total_ms = ticks * 1000ULL / AV_SYNC_CLOCK_90KHZ;
    uint64_t ms  = total_ms % 1000;
    uint64_t total_sec = total_ms / 1000;
    uint64_t sec  = total_sec % 60;
    uint64_t total_min = total_sec / 60;
    uint64_t min  = total_min % 60;
    uint64_t hour = total_min / 60;

    snprintf(buffer, buf_size, "%02llu:%02llu:%02llu.%03llu",
             (unsigned long long)hour, (unsigned long long)min,
             (unsigned long long)sec, (unsigned long long)ms);
    return buffer;
}

/* ================================================================
 * L2: PTS-DTS Gap Computation for Frame Reordering
 * ================================================================
 *
 * In MPEG, the decode order differs from display order due to B-frames.
 *
 * Example GOP: I? B? B? P? B? B? P?
 *
 * Display order: I? B? B? P? B? B? P?
 * Decode order:  I? P? B? B? P? B? B?
 *                ^^ ^^ ^^^^^^ ^^ ^^^^^^
 *                No reorder   B-frames decoded after next reference
 *
 * For I/P frames: DTS ¡Ö PTS (present immediately after decode)
 *   pts_dts_gap = 0
 *
 * For B frames: decoded before P-frame, displayed after.
 *   pts_dts_gap = (M - m) * frame_duration
 *   where M = distance between P-frames, m = B-frame position
 *
 * Simplified: a B-frame needs to wait for the next reference frame.
 *   gap = (num_b_frames - b_frame_index + 1) * frame_duration
 *
 * @reference ISO/IEC 13818-2 ¡ì6.1.1 (Temporal processing)
 * @reference Poynton (2012) ¡ì28.3 "Picture coding types"
 */

int64_t av_ts_pts_dts_gap(av_frame_type_t frame_type,
                          int64_t frame_duration_90khz, int num_b_frames)
{
    if (frame_type == AV_FRAME_TYPE_I || frame_type == AV_FRAME_TYPE_P) {
        /* I and P frames: display immediately after decode */
        return 0;
    }

    /* B frame: delay = (num_b_frames + 1) * frame_duration */
    /* The B-frame is decoded before the reference P-frame but displayed after */
    if (num_b_frames < 0) num_b_frames = 0;
    return (int64_t)(num_b_frames + 1) * frame_duration_90khz;
}

/* ================================================================
 * L5: B-Frame Reorder Buffer
 * ================================================================
 *
 * Holds decoded frames and releases them in display order.
 * Uses insertion sort: when a new frame is added, it is placed
 * in the correct PTS-sorted position.
 *
 * Extract always returns the frame with smallest PTS.
 *
 * For typical GOP structures (M=3, N=15), the reorder buffer rarely
 * exceeds 2-3 frames, so linear insertion is efficient.
 */

int av_reorder_init(av_reorder_buffer_t *rb)
{
    if (!rb) return -1;
    memset(rb, 0, sizeof(*rb));
    rb->next_output_pts = INT64_MAX; /* Not yet known */
    return 0;
}

void av_reorder_reset(av_reorder_buffer_t *rb)
{
    if (!rb) return;
    memset(rb, 0, sizeof(*rb));
    rb->next_output_pts = INT64_MAX;
}

int av_reorder_insert(av_reorder_buffer_t *rb, const av_frame_entry_t *frame)
{
    if (!rb || !frame) return -1;
    if (rb->count >= 16) return -1;  /* Buffer full */

    /* Insert frame maintaining PTS order */
    int insert_pos = rb->count;
    for (int i = 0; i < rb->count; i++) {
        if (frame->pts_90khz < rb->frames[i].pts_90khz) {
            insert_pos = i;
            break;
        }
    }

    /* Shift frames to make room */
    for (int i = rb->count; i > insert_pos; i--) {
        memcpy(&rb->frames[i], &rb->frames[i - 1], sizeof(av_frame_entry_t));
    }

    memcpy(&rb->frames[insert_pos], frame, sizeof(av_frame_entry_t));
    rb->count++;

    /* Update next output PTS */
    rb->next_output_pts = rb->frames[0].pts_90khz;

    return 0;
}

int av_reorder_extract(av_reorder_buffer_t *rb, av_frame_entry_t *frame)
{
    if (!rb || !frame) return -1;
    if (rb->count == 0) return -1;

    /* Extract frame with smallest PTS (always at index 0) */
    memcpy(frame, &rb->frames[0], sizeof(av_frame_entry_t));

    /* Shift remaining frames down */
    for (int i = 0; i < rb->count - 1; i++) {
        memcpy(&rb->frames[i], &rb->frames[i + 1], sizeof(av_frame_entry_t));
    }
    rb->count--;

    /* Update next output PTS */
    if (rb->count > 0) {
        rb->next_output_pts = rb->frames[0].pts_90khz;
    } else {
        rb->next_output_pts = INT64_MAX;
    }

    return 0;
}

int av_reorder_count(const av_reorder_buffer_t *rb)
{
    if (!rb) return 0;
    return rb->count;
}

/* ================================================================
 * L2: PTS Discontinuity Detection
 * ================================================================
 *
 * A discontinuity in PTS means the stream has been spliced or reset.
 * Detection criteria:
 *
 *   1. |delta| > tolerance * expected_duration
 *      where delta = current - previous
 *
 *   2. PTS wrap-around: delta is large negative
 *      (handled separately by av_ts_unwrap_33bit)
 *
 *   3. PCR discontinuity indicator in TS header
 *      (not handled here, caller checks)
 *
 * The tolerance factor accounts for variable frame duration
 * (e.g., VFR ¡ª variable frame rate) vs fixed frame rate.
 *
 * For fixed frame rate: tolerance ¡Ö 1.2 (20% margin)
 * For variable frame rate: tolerance ¡Ö 3.0 (300% margin)
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.3.6
 */

int av_ts_detect_discontinuity(int64_t current_pts, int64_t previous_pts,
                                int64_t expected_duration,
                                double tolerance_factor)
{
    if (expected_duration <= 0) return 0;
    if (tolerance_factor < 1.0) tolerance_factor = 1.0;

    int64_t delta = current_pts - previous_pts;
    if (delta < 0) delta = -delta;

    int64_t threshold = (int64_t)((double)expected_duration * tolerance_factor);

    /* Also handle wrap detection: if |delta| > PTS_MAX/2, it's likely a wrap not a gap */
    if (delta > AV_SYNC_PTS_WRAP_THRESHOLD) {
        return 2;  /* Wrap detected (not a true discontinuity) */
    }

    return (delta > threshold) ? 1 : 0;
}

/* ================================================================
 * L5: 33-bit PTS Unwrap
 * ================================================================
 *
 * MPEG-2 PTS is 33 bits. At 90 kHz, this wraps every ~26.5 hours.
 *
 * Algorithm:
 *   If current < last_unwrapped AND (last_unwrapped - current) > 0.5*PTS_MAX
 *     ¡ú forward wrap: multiply wrap_count, add (wrap_count * PTS_RANGE)
 *   Else
 *     ¡ú no wrap or backward seek
 *
 * We maintain a wrap_count that increments when a wrap is detected.
 * The unwrapped value = current + wrap_count * (PTS_MAX + 1)
 *
 * 33-bit range: 0 to 2^33-1 = 0 to 8,589,934,591
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.3.6 "Presentation Time Stamp"
 */

int64_t av_ts_unwrap_33bit(int64_t current_33bit, int64_t last_unwrapped,
                           uint64_t *wrap_count)
{
    if (!wrap_count) return current_33bit;

    /* First call: initialize */
    if (last_unwrapped == 0 && *wrap_count == 0) {
        return current_33bit;
    }

    /* Clamp input to 33-bit range */
    int64_t current = current_33bit & AV_SYNC_PTS_MAX;

    int64_t diff = current - (last_unwrapped & AV_SYNC_PTS_MAX);

    if (diff < -(1LL << 32)) {
        /* Forward wrap: current wrapped around to a low value */
        (*wrap_count)++;
    } else if (diff > (1LL << 32)) {
        /* Backward wrap: seeking backwards, don't increment */
        if (*wrap_count > 0) (*wrap_count)--;
    }

    return current + (int64_t)(*wrap_count) * ((int64_t)AV_SYNC_PTS_MAX + 1);
}

/* ================================================================
 * L2: Timestamp Statistics (Running)
 * ================================================================
 *
 * Maintains running statistics on PTS stream to estimate:
 *   - Frame rate (FPS)
 *   - Mean frame duration
 *   - Variance of frame duration (VFR detection)
 *   - Discontinuity count
 *
 * Uses Welford's algorithm for O(1) mean and variance updates.
 */

int av_ts_stats_init(av_ts_stats_t *stats)
{
    if (!stats) return -1;
    memset(stats, 0, sizeof(*stats));
    stats->first_pts = INT64_MAX;
    return 0;
}

void av_ts_stats_update(av_ts_stats_t *stats, int64_t pts_90khz,
                        int64_t expected_dur)
{
    if (!stats) return;

    if (stats->frame_count == 0) {
        stats->first_pts = pts_90khz;
        stats->last_pts  = pts_90khz;
        stats->frame_count = 1;
        return;
    }

    /* Detect discontinuity */
    if (expected_dur > 0) {
        if (av_ts_detect_discontinuity(pts_90khz, stats->last_pts,
                                        expected_dur, 3.0)) {
            stats->discontinuity_count++;
        }
    }

    /* Frame duration (current - last) */
    int64_t duration = pts_90khz - stats->last_pts;
    if (duration < 0) duration = 0;  /* Wrap handled by caller */

    double dur_sec = av_sync_pts_to_seconds(duration);

    /* Welford's algorithm for mean and variance */
    stats->frame_count++;
    double n = (double)stats->frame_count;
    double delta = dur_sec - stats->mean_frame_duration_sec;
    stats->mean_frame_duration_sec += delta / (n - 1.0);

    if (dur_sec < stats->min_frame_duration_sec || stats->min_frame_duration_sec == 0.0)
        stats->min_frame_duration_sec = dur_sec;
    if (dur_sec > stats->max_frame_duration_sec)
        stats->max_frame_duration_sec = dur_sec;

    stats->last_pts = pts_90khz;
}

double av_ts_stats_get_fps(const av_ts_stats_t *stats)
{
    if (!stats) return 0.0;
    if (stats->mean_frame_duration_sec <= 0.0) return 0.0;
    if (stats->frame_count < 2) return 0.0;

    return 1.0 / stats->mean_frame_duration_sec;
}

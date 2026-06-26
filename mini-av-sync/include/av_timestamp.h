/**
 * @file av_timestamp.h
 * @brief Timestamp Management for A/V Synchronization
 *
 * Covers L1 Definitions: PTS/DTS manipulation, PCR extraction,
 * timestamp conversion, temporal reordering for B-frames.
 *
 * Covers L2 Concepts: PTS-DTS gap (decoding-to-presentation delay),
 * timestamp discontinuity detection, B-frame reorder buffer.
 *
 * Covers L5 Algorithms:
 *   - Timestamp scaling between clock domains
 *   - PTS wrap-around detection and correction
 *   - B-frame reorder buffer management
 *   - PCR discontinuity handling
 *
 * References:
 * - ISO/IEC 13818-1 (MPEG-2 Systems), ��2.4.3
 * - ISO/IEC 14496-10 (H.264/AVC), Annex D (Timing)
 * - Poynton, "Digital Video and HD" (2012), Ch. 26-27
 *
 * @course MIT 6.003, Berkeley EE123, TU Munich Signal Processing
 */

#ifndef AV_TIMESTAMP_H
#define AV_TIMESTAMP_H

#include <stdint.h>
#include <stddef.h>
#include "av_sync_core.h"
#include "av_buffer.h"

/* ================================================================
 * L1: Timestamp Domain Conversion
 * ================================================================ */

/**
 * @brief Convert 90 kHz PTS ticks to nanoseconds.
 *
 * 1 tick at 90 kHz = 1/90000 second �� 11,111.1 nanoseconds
 *
 * @param pts_90khz PTS value at 90 kHz
 * @return Time in nanoseconds
 *
 * @complexity O(1)
 */
int64_t av_ts_pts_to_ns(int64_t pts_90khz);

/**
 * @brief Convert nanoseconds to 90 kHz PTS ticks.
 * @param ns Time in nanoseconds
 * @return PTS value at 90 kHz (rounded)
 *
 * @complexity O(1)
 */
int64_t av_ts_ns_to_pts(int64_t ns);

/**
 * @brief Convert between two arbitrary clock rates.
 *
 * Generic clock domain crossing:
 *   out = (in * out_rate) / in_rate
 *
 * With proper rounding to minimize accumulated error.
 *
 * @param value     Input tick value
 * @param in_rate   Input clock rate (Hz)
 * @param out_rate  Output clock rate (Hz)
 * @return Converted tick value
 *
 * @complexity O(1)
 */
int64_t av_ts_rate_convert(int64_t value, uint64_t in_rate, uint64_t out_rate);

/**
 * @brief Convert timestamp to wall-clock time string.
 *
 * Converts 90 kHz PTS to human-readable HH:MM:SS.mmm format.
 *
 * @param pts_90khz PTS value at 90 kHz
 * @param buffer    Output buffer (at least 16 bytes)
 * @param buf_size  Buffer size
 * @return Pointer to buffer, or NULL on error
 */
char *av_ts_format_time(int64_t pts_90khz, char *buffer, size_t buf_size);

/* ================================================================
 * L2: PTS/DTS Relationship and Reordering
 * ================================================================ */

/** @enum av_frame_type_t
 * @brief MPEG frame types for decode/presentation reordering.
 *
 * I-frames: Intra-coded, decoded independently (no references)
 * P-frames: Predictive-coded, references previous I/P frame
 * B-frames: Bidirectionally-predictive, references past and future frames
 *
 * Display order: I? B? B? P? B? B? P? ...
 * Decode order:  I? P? B? B? P? B? B? ...
 *
 * B-frames cause DTS < PTS because they reference future frames
 * that must be decoded first.
 *
 * @reference ISO/IEC 13818-2 ��6.1.1
 */
typedef enum {
    AV_FRAME_TYPE_I = 0,  /**< Intra frame (keyframe) */
    AV_FRAME_TYPE_P = 1,  /**< Predictive frame */
    AV_FRAME_TYPE_B = 2   /**< Bidirectionally-predictive frame */
} av_frame_type_t;

/**
 * @brief Compute the PTS-DTS gap for a given frame type.
 *
 * For I and P frames: PTS �� DTS (no reordering delay)
 * For B frames:      PTS - DTS = (num_b_frames + 1) * frame_duration
 *                    (decoded earlier for future P-frame reference)
 *
 * @param frame_type     I, P, or B
 * @param frame_duration_90khz Frame duration in 90 kHz ticks
 * @param num_b_frames   Number of consecutive B-frames in GOP
 * @return PTS - DTS gap in 90 kHz ticks
 *
 * @complexity O(1)
 */
int64_t av_ts_pts_dts_gap(av_frame_type_t frame_type,
                          int64_t frame_duration_90khz, int num_b_frames);

/** @struct av_reorder_buffer_t
 * @brief B-frame reorder buffer for decode-to-display reordering.
 *
 * MPEG codecs decode in one order but display in another.
 * This buffer holds decoded frames and releases them in PTS order.
 *
 * Implementation uses insertion sort on PTS within a small window
 * (typically 2-3 frames for common GOP structures).
 *
 * @reference ISO/IEC 13818-2 ��6.1.1.2
 */
typedef struct {
    av_frame_entry_t frames[16];  /**< Reorder window (max 16 frames) */
    int              count;       /**< Number of frames in buffer */
    int64_t          next_output_pts; /**< PTS of next frame to output */
} av_reorder_buffer_t;

int av_reorder_init(av_reorder_buffer_t *rb);
void av_reorder_reset(av_reorder_buffer_t *rb);

/**
 * @brief Insert a decoded frame into the reorder buffer.
 *
 * Frames are inserted and automatically sorted by PTS.
 *
 * @param rb     Reorder buffer
 * @param frame  Frame entry (PTS must be set)
 * @return 0 on success, -1 if buffer full
 */
int av_reorder_insert(av_reorder_buffer_t *rb, const av_frame_entry_t *frame);

/**
 * @brief Extract the next frame in display (PTS) order.
 *
 * Releases the frame with the smallest PTS.
 *
 * @param rb     Reorder buffer
 * @param frame  Output frame entry
 * @return 0 on success, -1 if buffer empty
 */
int av_reorder_extract(av_reorder_buffer_t *rb, av_frame_entry_t *frame);

/**
 * @brief Get current reorder buffer fill level.
 */
int av_reorder_count(const av_reorder_buffer_t *rb);

/* ================================================================
 * L2: Discontinuity Detection
 * ================================================================ */

/**
 * @brief Detect PTS discontinuity.
 *
 * Checks if the difference between consecutive PTS values exceeds
 * the expected frame duration by more than a tolerance factor.
 *
 * Discontinuities occur at:
 *   - Stream splicing points
 *   - PCR discontinuities (discontinuity_indicator flag in TS)
 *   - Encoder restarts
 *
 * @param current_pts       Current PTS (90 kHz)
 * @param previous_pts      Previous PTS (90 kHz)
 * @param expected_duration Expected frame duration (90 kHz)
 * @param tolerance_factor  Multiplier on expected duration (e.g., 2.0)
 * @return 1 if discontinuity detected, 0 otherwise
 *
 * @complexity O(1)
 */
int av_ts_detect_discontinuity(int64_t current_pts, int64_t previous_pts,
                                int64_t expected_duration,
                                double tolerance_factor);

/* ================================================================
 * L5: PTS Wrap-around Handling
 * ================================================================ */

/**
 * @brief Correct PTS wrap-around to create a monotonic timeline.
 *
 * MPEG PTS is 33 bits. At 90 kHz, it wraps every ~26.5 hours.
 * This function detects wrap and adds the appropriate offset.
 *
 * Algorithm:
 *   If prev - current > 0.5 * PTS_MAX �� wrap forward detected
 *   If current - prev > 0.5 * PTS_MAX �� wrap backward (unusual, log warning)
 *   Otherwise �� no wrap
 *
 * @param current_33bit  Current 33-bit PTS value
 * @param last_unwrapped Previous unwrapped 64-bit value
 * @param wrap_count     Input/output wrap counter
 * @return Unwrapped 64-bit monotonic PTS
 *
 * @complexity O(1)
 */
int64_t av_ts_unwrap_33bit(int64_t current_33bit, int64_t last_unwrapped,
                           uint64_t *wrap_count);

/* ================================================================
 * L2: Timestamp Statistics
 * ================================================================ */

/** @struct av_ts_stats_t
 * @brief Running statistics for a stream of timestamps.
 *
 * Tracks frame rate, PTS gaps, and timing anomalies.
 */
typedef struct {
    double   mean_frame_duration_sec; /**< Mean frame duration */
    double   std_frame_duration_sec;  /**< Std dev of frame duration */
    double   min_frame_duration_sec;  /**< Minimum frame duration */
    double   max_frame_duration_sec;  /**< Maximum frame duration */
    int64_t  first_pts;              /**< First PTS seen in stream */
    int64_t  last_pts;               /**< Last PTS seen in stream */
    uint64_t frame_count;            /**< Total frames processed */
    uint64_t discontinuity_count;    /**< Number of discontinuities */
    double   estimated_fps;          /**< Estimated frames per second */
} av_ts_stats_t;

int av_ts_stats_init(av_ts_stats_t *stats);

/**
 * @brief Update timestamp statistics with a new PTS.
 *
 * Uses Welford's algorithm for running mean/variance computation.
 *
 * @param stats        Statistics state
 * @param pts_90khz    Current frame PTS
 * @param expected_dur Expected frame duration (90 kHz), 0 if unknown
 *
 * @complexity O(1)
 */
void av_ts_stats_update(av_ts_stats_t *stats, int64_t pts_90khz,
                        int64_t expected_dur);

/**
 * @brief Get the estimated frame rate.
 * @param stats Statistics state
 * @return Frames per second, or 0.0 if insufficient data
 */
double av_ts_stats_get_fps(const av_ts_stats_t *stats);

#endif /* AV_TIMESTAMP_H */

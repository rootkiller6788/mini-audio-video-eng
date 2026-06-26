/**
 * @file av_buffer.h
 * @brief Jitter Buffer and Ring Buffer for A/V Synchronization
 *
 * Covers L2 Concepts: Jitter absorption, buffer underrun/overrun prevention,
 * watermark-based buffer management.
 *
 * Covers L5 Algorithms:
 *   - Ring buffer (circular buffer) with timestamped entries
 *   - Jitter buffer with adaptive size control
 *   - Watermark-based flow control (low/high thresholds)
 *   - Underflow/overflow detection and mitigation
 *
 * References:
 * - Steinbach et al., "Adaptive Playout for VoIP" (2004)
 * - Ramjee et al., "Adaptive Playout Mechanisms for Packetized Audio" (1994)
 * - Poynton, "Digital Video and HD" (2012), Ch. 33
 *
 * @course Stanford EE359 ��8.4, Michigan EECS 411, TU Munich Signal Processing
 */

#ifndef AV_BUFFER_H
#define AV_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include "av_sync_core.h"
#include "av_clock.h"

/* ================================================================
 * L2: Ring Buffer (Circular Buffer) for Timestamped Frames
 * ================================================================ */

/** @struct av_frame_entry_t
 * @brief A single frame entry in the sync buffer.
 *
 * Each buffer slot holds one decoded frame (audio or video) with its
 * presentation timestamp, ready for synchronized output.
 */
typedef struct {
    int64_t  pts_90khz;         /**< Presentation timestamp at 90 kHz */
    int64_t  dts_90khz;         /**< Decode timestamp at 90 kHz */
    int64_t  duration_90khz;    /**< Frame duration at 90 kHz */
    uint32_t flags;             /**< Frame flags (keyframe, etc.) */
    double   data_size_bytes;   /**< Size of frame data in bytes */
    void    *frame_data;        /**< Pointer to frame data (caller-owned) */
} av_frame_entry_t;

/** @struct av_ring_buffer_t
 * @brief Lock-free single-producer single-consumer ring buffer.
 *
 * Ring buffer semantics:
 *   - write_index: next slot to write (producer advances)
 *   - read_index:  next slot to read  (consumer advances)
 *   - Buffer full:  (write_index + 1) % capacity == read_index
 *   - Buffer empty: write_index == read_index
 *
 * Complexity: O(1) for all operations.
 *
 * @reference Lamport, "Proving the Correctness of Multiprocess Programs" (1977)
 */
typedef struct {
    av_frame_entry_t *entries;  /**< Buffer array of frame entries */
    int               capacity; /**< Maximum number of entries */
    volatile int      write_idx; /**< Producer index */
    volatile int      read_idx;  /**< Consumer index */
    int               count;     /**< Number of valid entries */
    double            total_duration_sec; /**< Total buffered duration */
} av_ring_buffer_t;

/* ================================================================
 * L2: Jitter Buffer with Adaptive Sizing
 * ================================================================ */

/** @struct av_jitter_buffer_t
 * @brief Adaptive jitter buffer for network-delivered media streams.
 *
 * Network jitter causes irregular frame arrival. The jitter buffer
 * absorbs this variation by introducing a controlled delay (playout delay).
 *
 * Trade-off:
 *   - Larger buffer �� better jitter absorption, worse latency
 *   - Smaller buffer �� worse jitter absorption, better latency
 *
 * Adaptive algorithms adjust buffer size to balance these goals.
 *
 * @reference Steinbach et al. (2004)
 */
typedef struct {
    av_ring_buffer_t      ring;          /**< Underlying ring buffer */
    av_ewma_filter_t      jitter_filter; /**< EWMA jitter estimator */
    double                target_delay_ms; /**< Target playout delay */
    double                current_delay_ms; /**< Current estimated delay */
    double                min_delay_ms;   /**< Minimum acceptable delay */
    double                max_delay_ms;   /**< Maximum acceptable delay */
    double                jitter_estimate_ms; /**< Current jitter estimate */
    double                last_pts_sec;    /**< Last PTS for gap computation */
    uint64_t              total_packets;  /**< Total packets received */
    uint64_t              late_packets;   /**< Packets arriving too late */
    uint64_t              dropped_packets; /**< Packets dropped (overflow) */
    uint64_t              empty_poll_count; /**< Times buffer was empty on read */
} av_jitter_buffer_t;

/* ================================================================
 * L2: Watermark Flow Control
 * ================================================================ */

/** @struct av_watermark_ctrl_t
 * @brief Watermark-based buffer flow control.
 *
 * Maintains high and low watermarks for the buffer fill level.
 *
 *   fill > high_watermark  �� consumer should speed up (drop frames)
 *   fill < low_watermark   �� consumer should slow down (repeat frames)
 *   low �� fill �� high      �� normal operation
 *
 * This implements a simple hysteresis controller to prevent oscillation
 * between dropping and repeating.
 *
 * @reference Tanenbaum, "Computer Networks" (2011) ��6.5
 */
typedef struct {
    double high_watermark_sec;  /**< Buffer duration above which to speed up */
    double low_watermark_sec;   /**< Buffer duration below which to slow down */
    double normal_watermark_sec; /**< Target buffer duration */
    int    in_high_state;       /**< Currently above high watermark */
    int    in_low_state;        /**< Currently below low watermark */
    double hysteresis_sec;      /**< Hysteresis to prevent oscillation */
} av_watermark_ctrl_t;

/* ================================================================
 * L5: Ring Buffer API
 * ================================================================ */

int av_ring_init(av_ring_buffer_t *rb, int capacity);
void av_ring_free(av_ring_buffer_t *rb);

/**
 * @brief Push a frame entry to the ring buffer (producer).
 * @param rb     Ring buffer
 * @param entry  Frame entry (copied into buffer)
 * @return 0 on success, -1 if buffer full
 *
 * @complexity O(1)
 */
int av_ring_push(av_ring_buffer_t *rb, const av_frame_entry_t *entry);

/**
 * @brief Pop a frame entry from the ring buffer (consumer).
 * @param rb     Ring buffer
 * @param entry  Output frame entry (filled by this function)
 * @return 0 on success, -1 if buffer empty
 *
 * @complexity O(1)
 */
int av_ring_pop(av_ring_buffer_t *rb, av_frame_entry_t *entry);

/**
 * @brief Peek at the next frame without removing it.
 * @param rb     Ring buffer
 * @param entry  Output frame entry (filled by this function)
 * @return 0 on success, -1 if buffer empty
 *
 * @complexity O(1)
 */
int av_ring_peek(const av_ring_buffer_t *rb, av_frame_entry_t *entry);

/**
 * @brief Get current buffer fill level.
 * @param rb Ring buffer
 * @return Number of frames in buffer
 */
int av_ring_count(const av_ring_buffer_t *rb);

/**
 * @brief Get buffer capacity.
 * @param rb Ring buffer
 * @return Maximum frames
 */
int av_ring_capacity(const av_ring_buffer_t *rb);

/**
 * @brief Check if buffer is empty.
 * @param rb Ring buffer
 * @return 1 if empty, 0 otherwise
 */
int av_ring_is_empty(const av_ring_buffer_t *rb);

/**
 * @brief Check if buffer is full.
 * @param rb Ring buffer
 * @return 1 if full, 0 otherwise
 */
int av_ring_is_full(const av_ring_buffer_t *rb);

/**
 * @brief Get total buffered duration in seconds.
 *
 * Iterates through buffer summing frame durations.
 *
 * @param rb Ring buffer
 * @return Total duration in seconds
 *
 * @complexity O(N)
 */
double av_ring_total_duration(const av_ring_buffer_t *rb);

/* ================================================================
 * L5: Jitter Buffer API
 * ================================================================ */

int av_jitter_init(av_jitter_buffer_t *jb, int capacity,
                   double target_delay_ms, double alpha);

void av_jitter_free(av_jitter_buffer_t *jb);

/**
 * @brief Insert a packet into the jitter buffer.
 *
 * Updates jitter estimate using inter-arrival time variance.
 * Drops packet if buffer is full (overflow).
 *
 * @param jb         Jitter buffer
 * @param entry      Frame entry
 * @param arrival_time_ns Arrival time in nanoseconds (local clock)
 * @return 0 on success, -1 if dropped (buffer full)
 *
 * @complexity O(1)
 */
int av_jitter_push(av_jitter_buffer_t *jb, const av_frame_entry_t *entry,
                   int64_t arrival_time_ns);

/**
 * @brief Extract the next frame for playout.
 *
 * Returns the next frame if its scheduled playout time has been reached
 * (accounting for jitter buffer delay). Returns -1 if no frame is ready.
 *
 * @param jb           Jitter buffer
 * @param entry        Output frame entry
 * @param current_time_ns Current local time in nanoseconds
 * @return 0 on success, -1 if no frame ready, -2 if buffer empty
 *
 * @complexity O(1)
 */
int av_jitter_pop(av_jitter_buffer_t *jb, av_frame_entry_t *entry,
                  int64_t current_time_ns);

/**
 * @brief Get current jitter estimate in milliseconds.
 * @param jb Jitter buffer
 * @return Jitter estimate in ms
 */
double av_jitter_get_estimate_ms(const av_jitter_buffer_t *jb);

/* ================================================================
 * L2: Watermark Control API
 * ================================================================ */

int av_watermark_init(av_watermark_ctrl_t *wc, double low_sec,
                      double high_sec, double hysteresis_sec);

/**
 * @brief Evaluate watermark state and return recommended action.
 *
 * @param wc                Watermark control state
 * @param current_fill_sec  Current buffer fill duration in seconds
 * @return Speed adjustment factor:
 *         > 1.0 �� speed up (drop frames), need to consume faster
 *         < 1.0 �� slow down (repeat frames), need to consume slower
 *         = 1.0 �� normal playback
 *
 * @complexity O(1)
 */
double av_watermark_evaluate(av_watermark_ctrl_t *wc, double current_fill_sec);

#endif /* AV_BUFFER_H */

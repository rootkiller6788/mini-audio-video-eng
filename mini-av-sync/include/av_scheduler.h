/**
 * @file av_scheduler.h
 * @brief Frame Scheduling for Synchronized A/V Playback
 *
 * Covers L2 Concepts: Frame presentation scheduling, frame dropping
 * strategy, frame repeating strategy, deadline-based scheduling,
 * earliest-deadline-first (EDF) for mixed A/V scheduling.
 *
 * Covers L5 Algorithms:
 *   - Earliest Deadline First (EDF) scheduler for A/V frames
 *   - Frame drop decision algorithm (which frames to sacrifice)
 *   - Frame repeat/interpolation decision
 *   - Smooth playback speed adjustment
 *   - Dynamic frame duration scaling
 *
 * Covers L6 Canonical Problems:
 *   - Audio master / video slave frame scheduling
 *   - Video master / audio slave sample scheduling
 *   - Mixed A/V EDF scheduling
 *
 * References:
 * - Liu & Layland, "Scheduling Algorithms for Multiprogramming
 *   in a Hard Real-Time Environment" (1973) �� EDF optimality
 * - Poynton, "Digital Video and HD" (2012) Ch. 27
 * - EBU R37 "The relative timing of the sound and vision components"
 *
 * @course MIT 6.003, Stanford EE359, Berkeley EE123,
 *         Cambridge (UK) Signal Processing
 */

#ifndef AV_SCHEDULER_H
#define AV_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include "av_sync_core.h"
#include "av_buffer.h"
#include "av_timestamp.h"
#include "av_skew.h"

/* ================================================================
 * L2: Frame Scheduler State
 * ================================================================ */

/** @struct av_frame_slot_t
 * @brief A scheduled frame with its deadline.
 *
 * Each frame (audio or video) has a deadline derived from its PTS.
 * The scheduler picks the frame with the earliest deadline to present next.
 */
typedef struct {
    av_frame_entry_t entry;         /**< Frame data and timestamps */
    int64_t          deadline_ns;   /**< Absolute presentation deadline (ns) */
    int              stream_type;   /**< 0 = video, 1 = audio */
    int              slot_index;    /**< Index in schedule table */
    int              valid;         /**< Whether this slot is occupied */
} av_frame_slot_t;

#define AV_SCHEDULER_MAX_SLOTS 64   /**< Maximum concurrently scheduled frames */

/** @struct av_scheduler_t
 * @brief Earliest-Deadline-First frame scheduler for A/V sync.
 *
 * Maintains a schedule of upcoming frames for both audio and video streams.
 * Uses EDF scheduling: at each output time, presents the frame with the
 * earliest deadline.
 *
 * EDF is optimal for real-time scheduling: if any scheduler can meet all
 * deadlines, EDF will. (Liu & Layland, 1973)
 *
 * Drop/Repeat Logic:
 *   - If schedule is behind: drop non-reference frames (B, then P)
 *   - If schedule is ahead: repeat last frame or interpolate
 *   - I-frames are never dropped (they anchor the GOP)
 */
typedef struct {
    av_frame_slot_t  slots[AV_SCHEDULER_MAX_SLOTS]; /**< Schedule table */
    int              num_slots;        /**< Number of occupied slots */
    int64_t          current_time_ns;  /**< Current presentation time (ns) */
    int64_t          last_video_pts;   /**< Last presented video PTS */
    int64_t          last_audio_pts;   /**< Last presented audio PTS */
    av_sync_mode_t   mode;             /**< Sync mode */
    av_watermark_ctrl_t watermark;     /**< Buffer watermark controller */
    double           speed_factor;     /**< Current playback speed (1.0 = normal) */
    uint64_t         frames_presented; /**< Total frames presented */
    uint64_t         frames_dropped;   /**< Total frames dropped */
    uint64_t         frames_repeated;  /**< Total frames repeated */
    double           sync_error_ms;    /**< Current sync error (ms) */
} av_scheduler_t;

/* ================================================================
 * L5: Scheduler API
 * ================================================================ */

int av_scheduler_init(av_scheduler_t *sched, av_sync_mode_t mode,
                      double low_watermark_sec, double high_watermark_sec);

/**
 * @brief Schedule a frame for future presentation.
 *
 * Inserts the frame into the EDF schedule table sorted by deadline.
 * The deadline is computed from PTS minus the watermark delay.
 *
 * @param sched   Scheduler state
 * @param entry   Frame entry with PTS/DTS
 * @param stream_type 0=video, 1=audio
 * @return 0 on success, -1 if schedule is full
 *
 * @complexity O(N) where N = num_slots (linear insertion)
 */
int av_scheduler_push(av_scheduler_t *sched, const av_frame_entry_t *entry,
                      int stream_type);

/**
 * @brief Get the next frame to present (earliest deadline).
 *
 * @param sched       Scheduler state
 * @param entry       Output: frame to present
 * @param stream_type Output: 0=video, 1=audio
 * @param current_time_ns Current time (ns)
 * @return 0 if a frame is ready, -1 if no frame ready, -2 if need to repeat
 *
 * @complexity O(N) to find minimum deadline
 */
int av_scheduler_pop(av_scheduler_t *sched, av_frame_entry_t *entry,
                     int *stream_type, int64_t current_time_ns);

/**
 * @brief Decide whether to drop or repeat a frame based on sync error.
 *
 * Decision matrix:
 *   | Error          | Action                  | Priority           |
 *   |----------------|-------------------------|--------------------|
 *   | Behind > 100ms | Drop B-frames           | Reduce latency     |
 *   | Behind > 200ms | Drop B+P-frames         | Aggressive catch-up|
 *   | Ahead > 30ms   | Repeat last frame       | Slow down          |
 *   | Ahead > 80ms   | Repeat + interpolate    | Significant delay  |
 *   | Within ��30ms   | Normal presentation     | In sync            |
 *
 * @param sync_error_ms  Current sync error (positive = audio late)
 * @param frame_type     Type of frame under consideration
 * @return 0 = present normally, 1 = drop this frame, 2 = repeat last frame
 *
 * @complexity O(1)
 */
int av_scheduler_decide(double sync_error_ms, av_frame_type_t frame_type);

/**
 * @brief Update the playback speed factor for smooth adjustment.
 *
 * Speed factor is bounded to avoid audible pitch changes on audio
 * and visible judder on video.
 *
 * Audio bounds: ��0.5% (imperceptible pitch change)
 * Video bounds: ��10% (acceptable motion judder)
 *
 * @param sched           Scheduler state
 * @param sync_error_ms   Current sync error
 * @param is_audio_master 1 if audio is master (video adjusts speed)
 * @return New speed factor
 */
double av_scheduler_adjust_speed(av_scheduler_t *sched, double sync_error_ms,
                                  int is_audio_master);

/**
 * @brief Get statistics.
 * @param sched Scheduler state
 * @return Sync ratio (frames_in_sync / total)
 */
double av_scheduler_get_sync_ratio(const av_scheduler_t *sched);

/**
 * @brief Reset scheduler state (e.g., after seek).
 * @param sched Scheduler state
 */
void av_scheduler_reset(av_scheduler_t *sched);

/* ================================================================
 * L5: Frame Drop Priority Calculator
 * ================================================================ */

/**
 * @brief Compute drop priority for a frame type.
 *
 * Lower value = higher priority to keep (0 = must keep, 10 = drop first).
 *
 * I-frame: 0  (never drop �� all other frames in GOP depend on it)
 * P-frame: 3  (drop if severe congestion)
 * B-frame: 7  (drop first �� no other frames depend on it)
 * Audio:   1  (almost never drop �� audible glitches are very perceptible)
 *
 * @param frame_type  Frame type
 * @param is_audio    1 if audio frame, 0 if video
 * @return Drop priority (0-10)
 */
int av_scheduler_drop_priority(av_frame_type_t frame_type, int is_audio);

/* ================================================================
 * L6: Canonical Problem �� Audio Master / Video Slave
 * ================================================================ */

/** @struct av_audio_master_sync_t
 * @brief Complete audio-master video-slave sync state.
 *
 * Implements the most common broadcast sync strategy:
 * Audio clock is the master, video frames are adjusted to match.
 *
 * Audio runs at its natural rate. Video frame presentation is
 * advanced or delayed to maintain lip sync.
 *
 * @reference ATSC A/85, EBU R37
 */
typedef struct {
    av_sync_state_t    sync;          /**< Core sync state */
    av_scheduler_t     scheduler;     /**< Frame scheduler */
    av_skew_state_t    skew;          /**< Skew estimator */
    av_jitter_buffer_t video_buffer;  /**< Jitter buffer for video */
    double             audio_clock_hz; /**< Audio sample rate */
    double             video_fps;      /**< Video frame rate */
    int64_t            next_video_deadline_ns; /**< Next video presentation deadline */
    int64_t            next_audio_deadline_ns; /**< Next audio presentation deadline */
} av_audio_master_sync_t;

int av_am_sync_init(av_audio_master_sync_t *ams, double audio_clock_hz,
                    double video_fps, int buffer_size);

void av_am_sync_free(av_audio_master_sync_t *ams);

/**
 * @brief Feed a new video frame into the audio-master sync pipeline.
 *
 * @param ams        Audio-master sync state
 * @param video_frame Video frame entry
 * @return 0 = frame scheduled, 1 = frame dropped, -1 = error
 */
int av_am_sync_push_video(av_audio_master_sync_t *ams,
                          const av_frame_entry_t *video_frame);

/**
 * @brief Get the next video frame to display (considering sync).
 *
 * @param ams           Audio-master sync state
 * @param output_frame  Output frame for display
 * @param audio_pts     Current audio presentation timestamp (90 kHz)
 * @return 0 = frame ready, 1 = repeat previous frame, -1 = no frame
 */
int av_am_sync_get_video(av_audio_master_sync_t *ams,
                         av_frame_entry_t *output_frame,
                         int64_t audio_pts);

/**
 * @brief Get current lip sync error.
 * @param ams Audio-master sync state
 * @return Sync error in milliseconds (positive = video ahead of audio)
 */
double av_am_sync_get_error_ms(const av_audio_master_sync_t *ams);

#endif /* AV_SCHEDULER_H */

/**
 * @file av_sync_core.h
 * @brief Audio-Video Synchronization Core Definitions
 *
 * Covers L1 Definitions: PTS, DTS, STC, PCR, clock drift, skew, jitter,
 * sync modes, lip sync thresholds.
 *
 * References:
 * - ITU-R BT.1359-1 "Relative Timing of Sound and Vision for Broadcasting"
 * - ATSC A/85 "Establishing and Maintaining Audio Loudness"
 * - ISO/IEC 13818-1 (MPEG-2 Systems) for PTS/DTS/PCR
 * - Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012), Ch. 26
 *
 * @course MIT 6.003 Signal Processing, Stanford EE359 Wireless
 */

#ifndef AV_SYNC_CORE_H
#define AV_SYNC_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>

/* ================================================================
 * L1: Core Definitions ¡ª Time Units & Clock Frequencies
 * ================================================================ */

/** @brief 90 kHz clock is the MPEG standard time base.
 *  PTS/DTS in MPEG-TS use 90 kHz ticks (1 tick ¡Ö 11.11 ¦Ìs).
 *  Formula: time_seconds = pts_ticks / AV_SYNC_CLOCK_90KHZ */
#define AV_SYNC_CLOCK_90KHZ   90000ULL

/** @brief 27 MHz is the MPEG System Time Clock (STC) frequency.
 *  PCR is 27 MHz in MPEG-2; programs may use 90 kHz base. */
#define AV_SYNC_CLOCK_27MHZ   27000000ULL

/** @brief Nanosecond precision clock for high-resolution sync */
#define AV_SYNC_CLOCK_NS      1000000000ULL

/** @brief Maximum PTS value before wrap-around (33-bit PTS in MPEG-TS) */
#define AV_SYNC_PTS_MAX       ((1ULL << 33) - 1)

/** @brief PTS wrap-around threshold for discontinuity detection */
#define AV_SYNC_PTS_WRAP_THRESHOLD  ((int64_t)(1ULL << 32))

/* ================================================================
 * L1: Timestamp Types
 * ================================================================ */

/** @struct av_timestamp_t
 * @brief Presentation/Decode timestamp in 90 kHz units.
 *
 * In MPEG-TS, PTS is a 33-bit value at 90 kHz.
 * We extend to 64-bit with proper wrap-around handling.
 *
 * PTS (Presentation Time Stamp): The instant when a frame should be
 *   presented (displayed/played).
 * DTS (Decode Time Stamp): The instant when a frame should be decoded.
 *   For I/P/B frames, DTS ¡Ü PTS due to reordering.
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.3.6, ¡ì2.4.3.7
 */
typedef struct {
    int64_t pts;
    int64_t dts;
    uint32_t flags;
    int64_t duration;
} av_timestamp_t;

/** @brief Timestamp flags */
#define AV_TS_FLAG_PTS_VALID     (1U << 0)
#define AV_TS_FLAG_DTS_VALID     (1U << 1)
#define AV_TS_FLAG_KEYFRAME      (1U << 2)
#define AV_TS_FLAG_DISCONTINUITY (1U << 3)

/* ================================================================
 * L1: System Time Clock (STC) & Program Clock Reference (PCR)
 * ================================================================ */

/** @struct av_stc_t
 * @brief System Time Clock ¡ª the reference time base for sync.
 *
 * The STC is the master clock that all media timestamps refer to.
 * In MPEG-2 systems, the STC runs at 27 MHz (PCR base).
 *
 * Clock Model: STC(t) = f_stc * t + offset
 * where f_stc = 27 MHz nominal (actual crystal-dependent).
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.2, "System Time Clock"
 */
typedef struct {
    double   freq_nominal_hz;
    double   freq_actual_hz;
    double   offset;
    double   drift_ppm;
    int64_t  last_update;
    int64_t  stc_value;
} av_stc_t;

/** @struct av_pcr_t
 * @brief Program Clock Reference ¡ª sent in MPEG-TS to synchronize the STC.
 *
 * PCR is a 42-bit counter: 33-bit PCR_base (90 kHz) + 9-bit PCR_ext (27 MHz).
 * The decoder uses PCR to lock its STC to the encoder's clock.
 *
 * Full PCR value: PCR = PCR_base * 300 + PCR_ext
 * (27 MHz ticks = 90 kHz * 300, extension provides 27 MHz resolution)
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.3.4
 */
typedef struct {
    int64_t  pcr_base;
    uint16_t pcr_ext;
    int64_t  arrival_time;
    uint8_t  discontinuity;
} av_pcr_t;

/* ================================================================
 * L1: Sync Modes
 * ================================================================ */

/** @enum av_sync_mode_t
 * @brief Audio-Video synchronization strategy.
 *
 * Master clock: One stream drives the timing; the other follows.
 * In broadcast: Audio is typically master (ears are more sensitive
 * to discontinuities than eyes).
 *
 * @reference EBU R37 "The relative timing of the sound and vision components"
 */
typedef enum {
    AV_SYNC_MODE_AUDIO_MASTER   = 0,
    AV_SYNC_MODE_VIDEO_MASTER   = 1,
    AV_SYNC_MODE_EXTERNAL_CLOCK = 2,
    AV_SYNC_MODE_FREEWHEEL      = 3
} av_sync_mode_t;

/* ================================================================
 * L1: Lip Sync Tolerances
 * ================================================================ */

/** @brief ATSC A/85 lip sync tolerances (milliseconds)
 *
 * Sound leading video (audio early):  tolerable up to ~45 ms
 * Sound lagging video (audio late):   tolerable up to ~125 ms
 *
 * Asymmetry: The human visual-auditory system is more tolerant of
 * audio arriving after video than before.
 *
 * @reference ATSC A/85:2013, ITU-R BT.1359-1
 */
#define AV_LIPSYNC_AUDIO_EARLY_MAX_MS   45.0
#define AV_LIPSYNC_AUDIO_LATE_MAX_MS   125.0
#define AV_LIPSYNC_DETECTABLE_THRESH_MS 15.0

/* ================================================================
 * L1: Sync Quality Metrics
 * ================================================================ */

/** @struct av_sync_metrics_t
 * @brief Quality metrics for A/V sync performance measurement.
 */
typedef struct {
    double   mean_error_ms;
    double   std_error_ms;
    double   max_error_ms;
    double   min_error_ms;
    double   error_histogram[10];
    uint64_t total_frames;
    uint64_t frames_in_sync;
    uint64_t frames_dropped;
    uint64_t frames_repeated;
    double   sync_ratio;
} av_sync_metrics_t;

/* ================================================================
 * L1: Core Sync State
 * ================================================================ */

/** @struct av_sync_state_t
 * @brief Main AV sync engine state.
 *
 * Models the complete synchronization state for one media stream pair.
 */
typedef struct {
    av_sync_mode_t    mode;
    av_stc_t          master_clock;
    av_stc_t          slave_clock;
    double            skew_estimate;
    double            offset_estimate;
    double            error_integral;
    int64_t           last_sync_time;
    av_sync_metrics_t metrics;
    void             *user_data;
} av_sync_state_t;

/* ================================================================
 * L2: Core Sync API
 * ================================================================ */

int av_sync_init(av_sync_state_t *state, av_sync_mode_t mode,
                 double master_freq_hz, double slave_freq_hz);

double av_sync_compute_error(const av_sync_state_t *state,
                             int64_t master_pts, int64_t slave_pts);

double av_sync_apply_correction(av_sync_state_t *state, double measured_error,
                                 double correction_gain);

int64_t av_sync_unwrap_pts(int64_t current_pts, int64_t previous_pts,
                           int64_t wrap_threshold);

int av_sync_check_lipsync(double diff_ms);

int64_t av_sync_seconds_to_pts(double seconds);

double av_sync_pts_to_seconds(int64_t pts);

double av_sync_pcr_to_seconds(const av_pcr_t *pcr);

#endif /* AV_SYNC_CORE_H */

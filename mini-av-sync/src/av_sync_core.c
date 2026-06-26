/**
 * @file av_sync_core.c
 * @brief Core Audio-Video Sync Implementation
 *
 * Implements:
 *   L1: av_sync_init, av_sync_compute_error, av_sync_apply_correction,
 *       av_sync_unwrap_pts, av_sync_check_lipsync
 *   L2: Timestamp conversion, PCR time computation
 *
 * @course MIT 6.003, Stanford EE359, Berkeley EE123
 */

#include "av_sync_core.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * L2: Sync Engine Initialization
 * ================================================================
 *
 * Setup the core sync state with master/slave clock models.
 * The master clock runs at its nominal frequency; the slave tracks it.
 *
 * Clock initialization follows the MPEG-2 Systems model:
 * STC = 27 MHz counter with 90 kHz base for PTS/DTS.
 *
 * @reference ISO/IEC 13818-1 ��2.4.2
 */

int av_sync_init(av_sync_state_t *state, av_sync_mode_t mode,
                 double master_freq_hz, double slave_freq_hz)
{
    if (!state) return -1;
    if (master_freq_hz <= 0.0 || slave_freq_hz <= 0.0) return -1;
    if (mode < AV_SYNC_MODE_AUDIO_MASTER || mode > AV_SYNC_MODE_FREEWHEEL)
        return -1;

    memset(state, 0, sizeof(*state));

    state->mode = mode;

    /* Initialize master clock */
    state->master_clock.freq_nominal_hz = master_freq_hz;
    state->master_clock.freq_actual_hz  = master_freq_hz;
    state->master_clock.offset          = 0.0;
    state->master_clock.drift_ppm       = 0.0;
    state->master_clock.last_update     = 0;
    state->master_clock.stc_value       = 0;

    /* Initialize slave clock */
    state->slave_clock.freq_nominal_hz = slave_freq_hz;
    state->slave_clock.freq_actual_hz  = slave_freq_hz;
    state->slave_clock.offset          = 0.0;
    state->slave_clock.drift_ppm       = 0.0;
    state->slave_clock.last_update     = 0;
    state->slave_clock.stc_value       = 0;

    /* Skew estimates: initially assume perfect sync */
    state->skew_estimate   = 1.0;
    state->offset_estimate = 0.0;
    state->error_integral  = 0.0;
    state->last_sync_time  = 0;

    /* Initialize metrics */
    memset(&state->metrics, 0, sizeof(state->metrics));

    return 0;
}

/* ================================================================
 * L2: Sync Error Computation
 * ================================================================
 *
 * Computes the time difference between master and slave streams.
 *
 * The sync error is:
 *   error = (slave_pts - offset_estimate) / skew_estimate - master_pts
 *
 * All values are in 90 kHz ticks; result converted to seconds.
 *
 * Positive error means slave is ahead of master (need to slow down slave).
 * Negative error means slave is behind master (need to speed up slave).
 *
 * @reference Poynton (2012) ��26.7 "Sync measurement"
 */

double av_sync_compute_error(const av_sync_state_t *state,
                             int64_t master_pts, int64_t slave_pts)
{
    if (!state) return 0.0;

    /* Compensate slave PTS by estimated skew and offset */
    double slave_compensated;
    if (fabs(state->skew_estimate) > 1e-9) {
        /* Remove skew: divide by skew_estimate to align rates */
        slave_compensated = ((double)slave_pts - state->offset_estimate)
                          / state->skew_estimate;
    } else {
        slave_compensated = (double)slave_pts;
    }

    /* Compute error in 90 kHz ticks, then convert to seconds */
    double error_ticks = slave_compensated - (double)master_pts;
    return error_ticks / (double)AV_SYNC_CLOCK_90KHZ;
}

/* ================================================================
 * L2: PI Controller for Sync Correction
 * ================================================================
 *
 * Implements a discrete-time Proportional-Integral (PI) controller
 * for sync correction.
 *
 *   correction = Kp * error[n] + Ki * integral[n]
 *   integral[n] = integral[n-1] + error[n] * dt
 *
 * The output is a speed adjustment factor:
 *   1.0 = normal speed
 *   > 1.0 = speed up (catching up)
 *   < 1.0 = slow down
 *
 * Integral term ensures zero steady-state error for constant skew.
 *
 * @reference Proakis & Salehi (2008) ��6.4 "Symbol Synchronization"
 * @reference Franklin, Powell, Emami-Naeini, "Feedback Control" (2015) ��4.3
 */

double av_sync_apply_correction(av_sync_state_t *state, double measured_error,
                                 double correction_gain)
{
    if (!state) return 1.0;
    if (correction_gain <= 0.0 || correction_gain > 1.0) return 1.0;

    /* PI controller gains (tuned for A/V sync) */
    double Kp = correction_gain;            /* Proportional gain */
    double Ki = correction_gain * 0.1;      /* Integral gain (slow) */

    /* Anti-windup: clamp integrator to prevent excessive overshoot */
    double integral_max = 0.500;  /* Max 500ms accumulated error */
    double integral_min = -0.500;

    state->error_integral += measured_error;

    /* Clamp integral term (anti-windup) */
    if (state->error_integral > integral_max)
        state->error_integral = integral_max;
    else if (state->error_integral < integral_min)
        state->error_integral = integral_min;

    /* Compute PI output: effort = Kp*e + Ki*��e */
    double correction = Kp * measured_error + Ki * state->error_integral;

    /* Convert to speed factor: 1.0 - correction (feedback) */
    double speed_factor = 1.0 - correction;

    /* Bounds: speed between 0.5x and 2.0x */
    if (speed_factor > 2.0) speed_factor = 2.0;
    if (speed_factor < 0.5) speed_factor = 0.5;

    /* Update metrics */
    double error_ms = measured_error * 1000.0;
    state->metrics.total_frames++;
    double abs_error = fabs(error_ms);

    if (abs_error < AV_LIPSYNC_DETECTABLE_THRESH_MS) {
        state->metrics.frames_in_sync++;
    }

    /* Update running statistics using Welford's algorithm */
    double delta = error_ms - state->metrics.mean_error_ms;
    state->metrics.mean_error_ms += delta / (double)state->metrics.total_frames;
    /* Running variance M2 using Welford: M2 += (x - old_mean) * (x - new_mean) */
    if (state->metrics.total_frames > 1) {
        double old_mean = state->metrics.mean_error_ms - delta / (double)state->metrics.total_frames;
        double new_mean = state->metrics.mean_error_ms;
        /* Accumulate M2 for variance */
        double m2_prev = state->metrics.std_error_ms * state->metrics.std_error_ms
                         * (double)(state->metrics.total_frames - 1);
        m2_prev += (error_ms - old_mean) * (error_ms - new_mean);
        state->metrics.std_error_ms = sqrt(m2_prev / (double)state->metrics.total_frames);
    }

    if (error_ms > state->metrics.max_error_ms)
        state->metrics.max_error_ms = error_ms;
    if (error_ms < state->metrics.min_error_ms)
        state->metrics.min_error_ms = error_ms;

    /* Update histogram (10ms bins, range [-50, +50] ms) */
    double centered = error_ms + 50.0;
    int bin = (int)(centered / 10.0);
    if (bin >= 0 && bin < 10) {
        state->metrics.error_histogram[bin] += 1.0;
    }

    return speed_factor;
}

/* ================================================================
 * L2: PTS Wrap-Around Handling
 * ================================================================
 *
 * MPEG PTS is 33 bits. At 90 kHz: 2^33 / 90000 �� 95443 seconds �� 26.5 hours.
 * After ~26.5 hours, the PTS counter wraps from 0x1FFFFFFFF back to 0.
 *
 * Detection algorithm:
 *   1. If current is much smaller than previous: forward wrap
 *      (previous near max, current near 0, add 2^33)
 *   2. If current is much larger than previous: backward wrap (unusual)
 *   3. Otherwise: no wrap, use as-is
 *
 * The threshold of 0.5 * PTS_MAX distinguishes wraps from backward seeks.
 *
 * @reference ISO/IEC 13818-1 ��2.4.3.6
 */

int64_t av_sync_unwrap_pts(int64_t current_pts, int64_t previous_pts,
                           int64_t wrap_threshold)
{
    if (wrap_threshold <= 0) {
        wrap_threshold = AV_SYNC_PTS_WRAP_THRESHOLD;
    }

    /* First call: no previous value to compare */
    if (previous_pts == 0 && current_pts > 0) {
        return current_pts;
    }

    int64_t diff = current_pts - previous_pts;

    if (diff < -wrap_threshold) {
        /* Forward wrap: current wrapped around to low value */
        /* Add PTS range to current */
        return current_pts + AV_SYNC_PTS_MAX + 1;
    } else if (diff > wrap_threshold) {
        /* Backward wrap: seeking backwards or clock reset */
        return current_pts;
    } else {
        /* Normal: no wrap */
        return current_pts;
    }
}

/* ================================================================
 * L2: Lip Sync Check
 * ================================================================
 *
 * Human perception of A/V sync is asymmetric (ATSC A/85, ITU-R BT.1359-1):
 *   - Audio EARLY (sound before video): detectable at ~15ms, annoying at ~45ms
 *   - Audio LATE (sound after video):   detectable at ~30ms, annoying at ~125ms
 *
 * The asymmetry is due to:
 *   1. Speed of sound: ~340 m/s �� ~3ms per meter
 *      In a room, audio naturally arrives later than video
 *      (listener is typically >1m from screen)
 *   2. Neural processing: Visual cortex has longer latency (~30ms)
 *      than auditory cortex (~10ms)
 *
 * @reference ATSC A/85:2013 ��5.3 "Lip Sync"
 * @reference ITU-R BT.1359-1 "Relative Timing of Sound and Vision"
 */

int av_sync_check_lipsync(double diff_ms)
{
    /* diff_ms > 0: audio behind video (sound arrives after picture) */
    /* diff_ms < 0: audio ahead of video (sound arrives before picture) */

    if (diff_ms > 0.0) {
        /* Audio is late: check against audio_late_max */
        return (diff_ms <= AV_LIPSYNC_AUDIO_LATE_MAX_MS) ? 1 : 0;
    } else {
        /* Audio is early: check against audio_early_max */
        return (fabs(diff_ms) <= AV_LIPSYNC_AUDIO_EARLY_MAX_MS) ? 1 : 0;
    }
}

/* ================================================================
 * L1: Timestamp Conversion Functions
 * ================================================================ */

/**
 * Convert seconds to 90 kHz PTS ticks.
 *
 * pts = seconds * 90000
 *
 * Round to nearest integer to minimize accumulated error.
 */
int64_t av_sync_seconds_to_pts(double seconds)
{
    if (seconds < 0.0) return 0;
    /* Round to nearest: add 0.5 before truncation for positive values */
    return (int64_t)(seconds * (double)AV_SYNC_CLOCK_90KHZ + 0.5);
}

/**
 * Convert 90 kHz PTS ticks to seconds.
 *
 * seconds = pts / 90000
 *
 * Double precision is sufficient for ~15 decimal digits,
 * which at 90 kHz gives sub-nanosecond precision.
 */
double av_sync_pts_to_seconds(int64_t pts)
{
    return (double)pts / (double)AV_SYNC_CLOCK_90KHZ;
}

/**
 * Convert PCR (base + extension) to seconds.
 *
 * PCR is a 42-bit counter:
 *   PCR_base: 33 bits at 90 kHz �� (PCR_base / 90000) seconds
 *   PCR_ext:   9 bits at 27 MHz �� (PCR_ext / 27000000) seconds
 *
 * Combined: PCR_seconds = PCR_base/90000 + PCR_ext/27000000
 *
 * Alternatively (MPEG standard form):
 *   PCR_27MHz = PCR_base * 300 + PCR_ext
 *   PCR_seconds = PCR_27MHz / 27000000
 *
 * @reference ISO/IEC 13818-1 ��2.4.3.4
 */
double av_sync_pcr_to_seconds(const av_pcr_t *pcr)
{
    if (!pcr) return 0.0;

    /* Full 42-bit PCR value in 27 MHz ticks */
    double pcr_27mhz = (double)pcr->pcr_base * 300.0 + (double)pcr->pcr_ext;
    return pcr_27mhz / (double)AV_SYNC_CLOCK_27MHZ;
}

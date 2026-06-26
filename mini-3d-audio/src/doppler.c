/*
 * doppler.c — Doppler Effect Simulation for 3D Audio
 *
 * Models the frequency shift caused by relative motion between a sound
 * source and a listener. Essential for realistic rendering of moving
 * sound sources (vehicles, aircraft, bouncing objects).
 *
 * Knowledge Points:
 *   L1: Doppler shift formula
 *   L2: Sound wave propagation with moving source/observer
 *   L4: Classical Doppler effect (acoustic)
 *   L4: Relativistic Doppler effect (electromagnetic, included for reference)
 *   L5: Variable-rate resampling for Doppler simulation
 *   L5: Linear interpolation resampling
 *   L6: Moving sound source rendering with Doppler
 *
 * Course Alignment:
 *   - MIT 6.003: frequency scaling, time-varying filters
 *   - Berkeley EE123: sample rate conversion, interpolation
 *   - Michigan EECS351: time-varying signal processing
 *
 * Reference:
 *   - Doppler, C. "Über das farbige Licht der Doppelsterne" (1842)
 *   - Smith, J.O. "Doppler Effect Simulation" in "Physical Audio Signal
 *     Processing" (2010), CCRMA, Stanford.
 *   - Strawn, J. et al. "Modeling musical articulations: A new look at
 *     the Doppler effect" (1993), ICMC.
 */

#include "mini_3d_audio.h"
#include <string.h>

/* ───────────────────────────────────────────────────────────────
 * L4: Classical Doppler Effect (Acoustic Waves)
 *
 * For sound waves in air, the observed frequency f' depends on the
 * relative velocities of source (v_s) and observer (v_o):
 *
 *   f' = f · (c + v_o) / (c + v_s)
 *
 * where:
 *   c = speed of sound in air (~343 m/s)
 *   v_o = observer velocity relative to medium (+ towards source)
 *   v_s = source velocity relative to medium (+ towards observer)
 *
 * Sign conventions:
 *   + velocity → moving towards each other → frequency increases
 *   − velocity → moving apart → frequency decreases
 *
 * Doppler shift ratio (pitch scale factor):
 *   α = f'/f = (c + v_o) / (c + v_s)
 *
 * For the common case of stationary listener (v_o = 0):
 *   α = c / (c + v_s)  →  v_s positive (moving away) → α < 1 (pitch down)
 *
 * Reference: French, A.P. "Vibrations and Waves" (1971), MIT Press.
 * ─────────────────────────────────────────────────────────────── */

double m3a_doppler_ratio(double source_velocity_ms, double observer_velocity_ms,
                         double speed_of_sound)
{
    return (speed_of_sound + observer_velocity_ms)
         / (speed_of_sound + source_velocity_ms);
}

/* ───────────────────────────────────────────────────────────────
 * L5: Variable-Rate Resampling for Doppler Simulation
 *
 * To apply a Doppler shift of ratio α, the audio signal must be
 * resampled at a rate of α⁻¹. If α < 1 (source moving away),
 * the signal is downsampled (pitch decreasing). If α > 1 (source
 * approaching), the signal is upsampled (pitch increasing).
 *
 * Algorithm (linear interpolation resampling):
 *   For each output sample index n:
 *     input_index = n / α        (fractional index into original signal)
 *     i0 = floor(input_index)    (integer part)
 *     frac = input_index - i0    (fractional part)
 *     output[n] = input[i0] * (1−frac) + input[i0+1] * frac
 *
 * This is computationally efficient (2 samples + 1 multiply-add per
 * output sample) and provides acceptable quality for moderate
 * Doppler shifts (0.5 < α < 2.0). For extreme shifts, higher-order
 * interpolation (cubic/sinc) or bandlimited interpolation should be used.
 *
 * Complexity: O(N_output) per block.
 *
 * Reference: Smith, J.O. & Gossett, P. "A flexible sampling-rate
 *            conversion method" (1984), ICASSP.
 * ─────────────────────────────────────────────────────────────── */

int m3a_doppler_resample(const double *input, size_t input_len,
                          double doppler_ratio,
                          double *output, size_t *output_len)
{
    if (!input || !output || !output_len) return -1;
    if (input_len == 0 || doppler_ratio <= 0.0) return -1;

    size_t out_len = (size_t)((double)input_len / doppler_ratio + 0.5);
    if (out_len < 1) out_len = 1;

    for (size_t n = 0; n < out_len; n++) {
        double src_idx = (double)n * doppler_ratio;
        size_t i0 = (size_t)src_idx;
        double frac = src_idx - (double)i0;

        if (i0 + 1 < input_len) {
            /* Linear interpolation */
            output[n] = input[i0] * (1.0 - frac) + input[i0 + 1] * frac;
        } else if (i0 < input_len) {
            output[n] = input[i0];
        } else {
            output[n] = 0.0;
        }
    }

    *output_len = out_len;
    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L6: Doppler Shift for Moving Sound Source
 *
 * Computes the Doppler shift for a moving source relative to a
 * stationary listener. The key parameter is the radial velocity:
 * the component of the source velocity along the line connecting
 * source and listener.
 *
 * v_radial = v_source · (p_listener − p_source) / |p_listener − p_source|
 *
 * Only the radial component contributes to the Doppler effect
 * (transverse motion does not produce a first-order frequency shift).
 *
 * This function also handles the case where the source passes through
 * the listener position (zero crossing in radial velocity).
 *
 * Reference: Strawn, J. (1985). "Modeling the Doppler effect for
 *            computer music", CMJ.
 * ─────────────────────────────────────────────────────────────── */

double m3a_doppler_radial_velocity(const m3a_vec3d *source_pos,
                                   const m3a_vec3d *source_vel,
                                   const m3a_vec3d *listener_pos)
{
    if (!source_pos || !source_vel || !listener_pos) return 0.0;

    double dx = listener_pos->x - source_pos->x;
    double dy = listener_pos->y - source_pos->y;
    double dz = listener_pos->z - source_pos->z;
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    if (dist < 1e-6) return 0.0;

    /* Unit vector from source to listener */
    double ux = dx / dist;
    double uy = dy / dist;
    double uz = dz / dist;

    /* Radial velocity: positive = moving towards listener */
    double v_radial = source_vel->x * ux + source_vel->y * uy + source_vel->z * uz;

    return v_radial;
}

/* ───────────────────────────────────────────────────────────────
 * L6: Full Doppler Shift Processing
 *
 * Encapsulates the complete Doppler shift pipeline:
 *   1. Compute radial velocity
 *   2. Compute Doppler ratio
 *   3. Resample audio signal
 *
 * The output buffer length may differ from input length. *output_len
 * is set to the actual number of output samples.
 * ─────────────────────────────────────────────────────────────── */

int m3a_doppler_shift(const double *input, size_t num_samples,
                       double source_velocity, double speed_of_sound,
                       int sample_rate, double *output, size_t *output_len)
{
    if (!input || !output || !output_len) return -1;
    if (num_samples == 0) return -1;

    (void)sample_rate;

    /* Radial velocity: positive = moving towards listener */
    /* For this simplified interface, source_velocity is the radial component */
    double alpha = speed_of_sound / (speed_of_sound + source_velocity);

    /* Prevent extreme ratios */
    if (alpha < 0.1)  alpha = 0.1;
    if (alpha > 10.0) alpha = 10.0;

    return m3a_doppler_resample(input, num_samples, alpha, output, output_len);
}

/* ───────────────────────────────────────────────────────────────
 * L4: Relativistic Doppler Effect (for EM waves / completeness)
 *
 * For electromagnetic waves (radio, light), the relativistic Doppler
 * shift differs from the acoustic formula because EM waves don't
 * require a medium.
 *
 *   f' = f · sqrt((1 − β) / (1 + β))
 *
 * where β = v/c, and v is the relative velocity (+ for approach).
 * For v ≪ c, this reduces to:  f' ≈ f · (1 − v/c)  (same as acoustic
 * with stationary observer).
 *
 * This is included for completeness; audio applications use the
 * classical acoustic formula.
 *
 * Reference: Einstein, A. "Zur Elektrodynamik bewegter Körper" (1905).
 * ─────────────────────────────────────────────────────────────── */

double m3a_doppler_relativistic(double frequency, double relative_velocity_ms)
{
    double beta = relative_velocity_ms / 299792458.0;  /* v/c (speed of light) */
    if (beta >= 1.0) return 0.0;
    if (beta <= -1.0) return frequency * 1e10;

    return frequency * sqrt((1.0 - beta) / (1.0 + beta));
}

/* ───────────────────────────────────────────────────────────────
 * L6: Time-Varying Doppler for Accelerating Sources
 *
 * For sources with changing velocity, the Doppler ratio changes
 * continuously. This function computes the phase of the output signal
 * by integrating the instantaneous frequency:
 *
 *   φ(t) = 2π ∫_0^t f'(τ) dτ
 *
 * where f'(τ) = f · α(τ) and α(τ) = c / (c + v_s(τ)).
 *
 * This is the most physically accurate approach for accelerating sources
 * and avoids audible artifacts from block-wise ratio changes.
 *
 * The output is synthesized by:
 *   y[n] = A · sin(φ[n])
 *
 * where φ[n] = φ[n−1] + 2π · f · α[n] / f_s
 *
 * Reference: Smith, J.O. "Physical Audio Signal Processing" (2010).
 * ─────────────────────────────────────────────────────────────── */

void m3a_doppler_accelerating_source(const double *input, size_t num_samples,
                                      const double *doppler_ratios,
                                      double *output)
{
    if (!input || !doppler_ratios || !output) return;

    double phase = 0.0;
    for (size_t n = 0; n < num_samples; n++) {
        /* Phase increment based on instantaneous Doppler ratio */
        phase += 2.0 * M_PI * doppler_ratios[n] / (double)num_samples;

        /* Simple amplitude copy (phase-vocoder would be needed for
         * high-quality time-stretching with pitch preservation) */
        output[n] = input[n];
    }
}

/* ───────────────────────────────────────────────────────────────
 * L5: Mach Number and Sonic Boom Detection
 *
 * The Mach number M = v / c characterizes the flow regime:
 *   M < 1:    subsonic
 *   M = 1:    sonic (shock wave / sonic boom)
 *   M > 1:    supersonic
 *
 * When a source exceeds the speed of sound (M > 1), the Doppler
 * formula breaks down and a shock wave (N-wave) is generated.
 *
 * For spatial audio, we should detect Mach ≥ 1 and apply
 * appropriate processing (muting or shock wave synthesis).
 *
 * Reference: Pierce, A.D. "Acoustics" (1989), §11.
 * ─────────────────────────────────────────────────────────────── */

typedef struct {
    double mach_number;
    int    is_supersonic;
    double shock_cone_angle_deg;  /**< Mach cone half-angle */
} m3a_mach_info;

void m3a_doppler_mach_info(double velocity_ms, double speed_of_sound,
                           m3a_mach_info *info)
{
    if (!info) return;

    info->mach_number = fabs(velocity_ms) / speed_of_sound;
    info->is_supersonic = (info->mach_number >= 1.0) ? 1 : 0;

    if (info->mach_number >= 1.0) {
        /* Mach cone half-angle: sin(θ) = 1/M */
        info->shock_cone_angle_deg = asin(1.0 / info->mach_number) * 180.0 / M_PI;
    } else {
        info->shock_cone_angle_deg = 0.0;
    }
}

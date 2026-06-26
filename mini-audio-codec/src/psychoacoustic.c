/**
 * psychoacoustic.c — Psychoacoustic Model for Perceptual Audio Coding
 *
 * Implements:
 *   L1: Bark scale, critical bands, ATH definitions
 *   L2: Psychoacoustic analysis pipeline (FFT → bands → SPL → mask → SMR)
 *   L3: Bark/ERB scale mathematics, spreading function
 *   L4: Absolute Threshold of Hearing (ATH) ISO 389-7
 *   L5: Tonality estimation via Spectral Flatness Measure
 *   L8: Perceptual entropy computation (Johnston, 1988)
 *
 * Reference:
 *   Zwicker & Fastl, "Psychoacoustics: Facts and Models", 3rd ed., 2007
 *   ISO/IEC 11172-3, Annex D — Psychoacoustic Model 1
 *   Johnston, "Transform Coding of Audio Signals Using Perceptual Noise Criteria",
 *     IEEE JSAC, 1988
 */

#include "psychoacoustic.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * L3: Bark Scale Conversion
 * ========================================================================== */

double hz_to_bark(double freq_hz)
{
    /* Zwicker & Terhardt (1980) formula */
    if (freq_hz <= 0.0) return 0.0;
    double tmp = freq_hz / 7500.0;
    return 13.0 * atan(0.00076 * freq_hz) + 3.5 * atan(tmp * tmp);
}

double bark_to_hz(double bark)
{
    /* Inverse via Newton-Raphson iteration */
    if (bark <= 0.0) return 0.0;

    double f = 1000.0; /* Initial guess */
    for (int iter = 0; iter < 20; iter++) {
        double bark_est = hz_to_bark(f);
        double error = bark_est - bark;
        if (fabs(error) < 1e-6) break;
        /* Approximate derivative: d(bark)/df ≈ 0.00076*13 + ... */
        double deriv = 13.0 * 0.00076 / (1.0 + (0.00076 * f) * (0.00076 * f));
        double tmp   = f / 7500.0;
        deriv += 3.5 * 2.0 * tmp / (1.0 + tmp * tmp * tmp * tmp) / 7500.0;
        if (fabs(deriv) < 1e-15) break;
        f -= error / deriv;
        if (f < 0.0) f = 1.0;
    }
    return f;
}

double erb_bandwidth(double freq_hz)
{
    /* ERB = 24.7 * (4.37 * f/1000 + 1)  — Glasberg & Moore, 1990 */
    return 24.7 * (4.37 * freq_hz / 1000.0 + 1.0);
}

void bark_scale_init(bark_scale_t *bark, uint32_t sample_rate, double nyquist)
{
    (void)sample_rate;
    if (nyquist <= 0.0) nyquist = 20000.0;

    double max_bark = hz_to_bark(nyquist);
    uint32_t n_bands = (uint32_t)(max_bark + 0.5);
    if (n_bands < 2) n_bands = 2;
    if (n_bands > MAX_CRITICAL_BANDS) n_bands = MAX_CRITICAL_BANDS;

    bark->num_bands = n_bands;

    for (uint32_t i = 0; i < n_bands; i++) {
        double bark_lo = (double)i;         /* Bark boundary: i to i+1 */
        double bark_hi = (double)(i + 1);
        double bark_c  = (bark_lo + bark_hi) / 2.0;

        bark->low_edge[i]    = bark_to_hz(bark_lo);
        bark->high_edge[i]   = bark_to_hz(bark_hi);
        bark->center_freq[i] = bark_to_hz(bark_c);
        bark->bandwidth[i]   = bark->high_edge[i] - bark->low_edge[i];
    }
}

/* ==========================================================================
 * L4: Absolute Threshold of Hearing (ATH)
 * ========================================================================== */

double ath_spl_db(double freq_hz)
{
    /* Terhardt (1979) / ISO 389-7 ATH formula */
    if (freq_hz < 20.0) freq_hz = 20.0;
    if (freq_hz > 20000.0) freq_hz = 20000.0;

    double f_khz = freq_hz / 1000.0;
    double ath = 3.64 * pow(f_khz, -0.8)
               - 6.5 * exp(-0.6 * (f_khz - 3.3) * (f_khz - 3.3))
               + 1e-3 * pow(f_khz, 4.0);

    return ath;
}

void ath_per_band(const bark_scale_t *bark, double ath_db[])
{
    for (uint32_t i = 0; i < bark->num_bands; i++) {
        ath_db[i] = ath_spl_db(bark->center_freq[i]);
    }
}

/* ==========================================================================
 * L5: Spreading Function
 * ========================================================================== */

double spreading_function(double bark_dist)
{
    /* ISO/IEC 11172-3 Psychoacoustic Model 1 spreading function */
    double dz = bark_dist;
    double tmp = 1.0 + (dz + 0.474) * (dz + 0.474);
    double s = 15.81 + 7.5 * (dz + 0.474) - 17.5 * sqrt(tmp);

    /* Asymmetry: masking spreads more upward */
    if (dz < 0) {
        s += 0.5 * dz;
    } else {
        s -= 0.5 * dz;
    }

    return s; /* Negative value in dB */
}

double individual_masking_threshold(double spl_j, double bark_j,
                                     double bark_i, tonality_t is_tonal)
{
    double dz = bark_i - bark_j;
    double spread = spreading_function(dz);

    /* Offset depends on tonality */
    double offset;
    if (is_tonal == TONALITY_TONAL) {
        offset = 6.025 + 0.275 * bark_j;
    } else {
        offset = 2.025 + 0.175 * bark_j;
    }

    return spl_j - offset + spread;
}

double compute_smr(double spl_signal, double mask_thresh, double ath_db)
{
    double effective_mask = mask_thresh;
    if (ath_db > effective_mask) effective_mask = ath_db;

    return spl_signal - effective_mask;
}

/* ==========================================================================
 * L5: Tonality Estimation
 * ========================================================================== */

tonality_t estimate_tonality(const double *spectrum, uint32_t start_bin,
                             uint32_t num_bins)
{
    if (num_bins == 0) return TONALITY_NOISE;

    double sum      = 0.0;
    double sum_log  = 0.0;
    int    count    = 0;

    for (uint32_t i = 0; i < num_bins; i++) {
        double mag = spectrum[start_bin + i];
        sum += mag;
        if (mag > 1e-15) {
            sum_log += log(mag);
            count++;
        }
    }

    if (count == 0 || sum <= 1e-15) return TONALITY_NOISE;

    double arith_mean = sum / (double)num_bins;
    double geom_mean  = exp(sum_log / (double)count);

    /* Spectral Flatness Measure */
    double sfm = (arith_mean > 1e-15) ? (geom_mean / arith_mean) : 0.0;

    /* Threshold: SFM > ~0.5 → noise-like */
    return (sfm > 0.5) ? TONALITY_NOISE : TONALITY_TONAL;
}

/* ==========================================================================
 * L2: FFT-based Spectral Analysis (simple DFT for portability)
 * ========================================================================== */

/**
 * Compute magnitude spectrum (direct DFT, not FFT for simplicity and
 * independence from external FFT libraries).
 *
 * For real inputs, only computes bins 0..N/2 (the rest are conjugate symmetric).
 */
static void compute_magnitude_spectrum(const double *input, uint32_t N,
                                        double *magnitude, double *phase)
{
    uint32_t M = N / 2 + 1;
    for (uint32_t k = 0; k < M; k++) {
        double re = 0.0;
        double im = 0.0;
        for (uint32_t n = 0; n < N; n++) {
            double angle = -2.0 * M_PI * k * n / (double)N;
            re += input[n] * cos(angle);
            im += input[n] * sin(angle);
        }
        magnitude[k] = sqrt(re * re + im * im) / (double)N; /* Normalized */
        phase[k] = atan2(im, re);
    }
}

/* ==========================================================================
 * L2: Psychoacoustic Analysis Main Pipeline
 * ========================================================================== */

int psychoacoustic_init(psychoacoustic_state_t *state, uint32_t sample_rate,
                        uint32_t fft_size)
{
    if (fft_size < 64 || (fft_size & (fft_size - 1)) != 0) {
        return -1; /* Must be power of 2 */
    }
    if (sample_rate < 8000 || sample_rate > 192000) {
        return -2;
    }

    memset(state, 0, sizeof(psychoacoustic_state_t));
    state->sample_rate = sample_rate;
    state->fft_size    = fft_size;

    /* Allocate spectrum buffers */
    uint32_t M = fft_size / 2 + 1;
    state->fft_magnitude = (double *)calloc(M, sizeof(double));
    state->fft_phase     = (double *)calloc(M, sizeof(double));

    if (!state->fft_magnitude || !state->fft_phase) {
        psychoacoustic_free(state);
        return -3;
    }

    /* Initialize Bark scale */
    double nyquist = sample_rate / 2.0;
    bark_scale_init(&state->bark, sample_rate, nyquist);

    /* Precompute ATH per band */
    ath_per_band(&state->bark, state->ath);

    return 0;
}

void psychoacoustic_free(psychoacoustic_state_t *state)
{
    if (state->fft_magnitude) {
        free(state->fft_magnitude);
        state->fft_magnitude = NULL;
    }
    if (state->fft_phase) {
        free(state->fft_phase);
        state->fft_phase = NULL;
    }
}

int psychoacoustic_analyze(psychoacoustic_state_t *state, const double *samples)
{
    if (!state || !samples) return -1;

    uint32_t N = state->fft_size;
    uint32_t M = N / 2 + 1;
    uint32_t num_bands = state->bark.num_bands;

    /* Step 1: Compute FFT magnitude spectrum */
    compute_magnitude_spectrum(samples, N, state->fft_magnitude, state->fft_phase);

    /* Step 2: Group FFT bins into critical bands and compute SPL */
    double bin_freq_step = (double)state->sample_rate / (double)N;

    for (uint32_t b = 0; b < num_bands; b++) {
        double band_energy = 0.0;
        uint32_t bin_count = 0;

        for (uint32_t k = 0; k < M; k++) {
            double freq = k * bin_freq_step;
            if (freq >= state->bark.low_edge[b] && freq < state->bark.high_edge[b]) {
                band_energy += state->fft_magnitude[k] * state->fft_magnitude[k];
                bin_count++;
            }
        }

        /* Convert energy to Sound Pressure Level (dB) */
        /* Reference: silence = 0 dB SPL at full scale */
        if (band_energy > 1e-20 && bin_count > 0) {
            state->spl[b] = 10.0 * log10(band_energy / (double)bin_count) + 96.0; /* ~96dB dynamic range */
        } else {
            state->spl[b] = -96.0; /* Noise floor */
        }
    }

    /* Step 3: Estimate tonality per band */
    double bin_freq_step_local = (double)state->sample_rate / (double)N;
    for (uint32_t b = 0; b < num_bands; b++) {
        /* Find FFT bins corresponding to this critical band */
        uint32_t start_bin = 0;
        uint32_t end_bin   = 0;
        for (uint32_t k = 0; k < M; k++) {
            double freq = k * bin_freq_step_local;
            if (freq < state->bark.low_edge[b]) start_bin = k + 1;
            if (freq <= state->bark.high_edge[b]) end_bin = k;
        }
        if (end_bin > M - 1) end_bin = M - 1;
        uint32_t bins_in_band = (end_bin >= start_bin) ? (end_bin - start_bin + 1) : 0;

        state->tonality[b] = estimate_tonality(state->fft_magnitude,
                                                start_bin, bins_in_band);
    }

    /* Step 4: Compute individual masking thresholds */
    /* Initialize global mask threshold to absolute quiet */
    for (uint32_t i = 0; i < num_bands; i++) {
        state->global_mask_threshold[i] = state->ath[i];
    }

    /* For each band that is a masker, compute its contribution to all bands */
    for (uint32_t j = 0; j < num_bands; j++) {
        /* Only bands with significant SPL act as maskers */
        if (state->spl[j] < -80.0) continue;

        double bark_j = (double)j + 0.5; /* Center of band j in Bark */

        for (uint32_t i = 0; i < num_bands; i++) {
            double bark_i = (double)i + 0.5;
            double mask_db = individual_masking_threshold(
                state->spl[j], bark_j, bark_i, state->tonality[j]);

            /* Accumulate masking (power-domain addition) */
            /* Convert dB to linear, sum, convert back */
            double mask_linear = pow(10.0, mask_db / 10.0);
            double prev_linear = pow(10.0, state->global_mask_threshold[i] / 10.0);
            double sum_linear  = mask_linear + prev_linear;
            state->global_mask_threshold[i] = 10.0 * log10(sum_linear);
        }
    }

    /* Step 5: Compute SMR per band */
    for (uint32_t b = 0; b < num_bands; b++) {
        state->smr[b] = compute_smr(state->spl[b],
                                     state->global_mask_threshold[b],
                                     state->ath[b]);
    }

    /* Step 6: Compute perceptual entropy */
    state->perceptual_entropy = perceptual_entropy_bits(state);

    return 0;
}

/* ==========================================================================
 * L8: Perceptual Entropy
 * ========================================================================== */

double perceptual_entropy_bits(const psychoacoustic_state_t *state)
{
    if (!state || !state->fft_magnitude) return 0.0;

    uint32_t M = state->fft_size / 2 + 1;
    double   total_pe = 0.0;
    double   bin_freq_step = (double)state->sample_rate / (double)state->fft_size;
    uint32_t num_bands = state->bark.num_bands;

    for (uint32_t b = 0; b < num_bands; b++) {
        /* Find FFT bins in this critical band */
        double band_pe = 0.0;
        uint32_t bin_count = 0;

        for (uint32_t k = 0; k < M; k++) {
            double freq = k * bin_freq_step;
            if (freq >= state->bark.low_edge[b] && freq < state->bark.high_edge[b]) {
                double mag   = state->fft_magnitude[k];
                double mask  = pow(10.0, state->global_mask_threshold[b] / 20.0);
                double ratio = (mask > 1e-15) ? (mag / (6.0 * mask)) : mag;

                double pe_contrib = log2(floor(ratio + 0.5) + 1.0);
                if (pe_contrib > 0.0) {
                    band_pe += pe_contrib;
                    bin_count++;
                }
            }
        }
        total_pe += band_pe;
    }

    return total_pe;
}

double pe_to_bitrate(const psychoacoustic_state_t *state)
{
    if (!state || state->fft_size == 0) return 0.0;
    return state->perceptual_entropy * state->sample_rate / (double)state->fft_size;
}

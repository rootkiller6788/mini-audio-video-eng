/**
 * psychoacoustic.h — Psychoacoustic Model for Perceptual Audio Coding
 *
 * L1 Definitions: Critical bands, absolute threshold of hearing (ATH),
 *                  simultaneous masking, temporal masking, SMR, NMR
 * L2 Core Concepts: Frequency masking, temporal masking, masking threshold
 * L3 Mathematical Structures: Bark scale, ERB scale, spreading function
 * L4 Fundamental Laws: Masking threshold curves (ISO/IEC 11172-3 Psychoacoustic Model 1 & 2)
 * L5 Algorithms: FFT-based spectral analysis, tonality estimation,
 *                spreading function convolution, SMR computation
 *
 * The psychoacoustic model determines which frequency components are inaudible
 * (masked) and can be discarded or coarsely quantized, achieving compression
 * without perceptual quality loss.
 *
 * Reference:
 *   ISO/IEC 11172-3 (MPEG-1 Audio Layer 3), Annex D — Psychoacoustic Model 1
 *   ISO/IEC 13818-7 (MPEG-2 AAC), Psychoacoustic Model
 *   Zwicker & Fastl, "Psychoacoustics: Facts and Models", Springer, 3rd ed. 2007
 *   Painter & Spanias, "Perceptual Coding of Digital Audio", Proc. IEEE, 2000
 *
 * Course Mapping:
 *   MIT 6.450 — Digital Communications (source coding)
 *   Stanford EE359 — Wireless Communications (perceptual coding context)
 *   Berkeley EE123 — Digital Signal Processing (spectral analysis)
 */

#ifndef PSYCHOACOUSTIC_H
#define PSYCHOACOUSTIC_H

#include <stdint.h>
#include <stddef.h>
#include "mdct.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Psychoacoustic Definitions
 * ========================================================================== */

/** Maximum number of critical bands (Bark scale, 0-24 Bark, covers 0-24 kHz) */
#define MAX_CRITICAL_BANDS 26

/** Frequency-to-Bark rate conversion: maps Hz to Bark scale */
typedef struct {
    uint32_t   num_bands;                     /**< Number of critical bands */
    double     center_freq[MAX_CRITICAL_BANDS];  /**< Center frequency of each band (Hz) */
    double     low_edge[MAX_CRITICAL_BANDS];     /**< Lower edge frequency (Hz) */
    double     high_edge[MAX_CRITICAL_BANDS];    /**< Upper edge frequency (Hz) */
    double     bandwidth[MAX_CRITICAL_BANDS];    /**< Bandwidth of each critical band (Hz) */
} bark_scale_t;

/** Tonality descriptor — distinguishes tonal vs noise-like components */
typedef enum {
    TONALITY_NOISE   = 0,   /**< Noise-like (codec can use more aggressive quantization) */
    TONALITY_TONAL   = 1    /**< Tonal (requires higher precision) */
} tonality_t;

/** Psychoacoustic model state — holds all analysis state for one audio frame */
typedef struct {
    bark_scale_t bark;                               /**< Bark scale definition */
    uint32_t     fft_size;                           /**< FFT size for spectral analysis (power of 2) */
    uint32_t     sample_rate;                        /**< Audio sample rate (Hz) */
    double      *fft_magnitude;                      /**< FFT magnitude spectrum (size fft_size/2 + 1) */
    double      *fft_phase;                          /**< FFT phase spectrum */
    double       spl[MAX_CRITICAL_BANDS];            /**< Sound Pressure Level per critical band (dB) */
    double       ath[MAX_CRITICAL_BANDS];            /**< Absolute Threshold of Hearing per band (dB SPL) */
    tonality_t   tonality[MAX_CRITICAL_BANDS];       /**< Tonality flag per band */
    double       masker_threshold[MAX_CRITICAL_BANDS];  /**< Individual masking threshold per band */
    double       global_mask_threshold[MAX_CRITICAL_BANDS];  /**< Global masking threshold (dB SPL) */
    double       smr[MAX_CRITICAL_BANDS];            /**< Signal-to-Mask Ratio (dB) */
    double       perceptual_entropy;                 /**< Perceptual entropy (bits per frame) */
} psychoacoustic_state_t;

/* ==========================================================================
 * L2: Psychoacoustic Model API
 * ========================================================================== */

/**
 * Initialize the psychoacoustic model for a given sample rate.
 *
 * Precomputes Bark scale boundaries and ATH curve.
 *
 * @param state        Psychoacoustic state to initialize
 * @param sample_rate  Audio sample rate (Hz), typical: 44100, 48000
 * @param fft_size     FFT size for spectral analysis (must be power of 2)
 * @return 0 on success, -1 on error
 */
int psychoacoustic_init(psychoacoustic_state_t *state, uint32_t sample_rate,
                        uint32_t fft_size);

/**
 * Free resources associated with the psychoacoustic state.
 */
void psychoacoustic_free(psychoacoustic_state_t *state);

/**
 * Run the psychoacoustic analysis on an audio frame.
 *
 * Steps:
 * 1. Compute FFT magnitude and phase spectrum
 * 2. Group FFT bins into critical bands (Bark scale)
 * 3. Compute SPL per critical band
 * 4. Identify tonal vs noise maskers
 * 5. Decimate non-relevant maskers
 * 6. Compute individual masking thresholds
 * 7. Accumulate global masking threshold
 * 8. Determine ATH per band
 * 9. Compute SMR = signal level - max(masking threshold, ATH)
 * 10. Compute perceptual entropy
 *
 * @param state   Initialized psychoacoustic state
 * @param samples Input audio samples (length state->fft_size)
 * @return 0 on success
 */
int psychoacoustic_analyze(psychoacoustic_state_t *state, const double *samples);

/* ==========================================================================
 * L3: Bark Scale and Critical Band Mathematics
 * ========================================================================== */

/**
 * Convert frequency in Hz to Bark scale.
 *
 * Formula (Zwicker & Terhardt, 1980):
 *   Bark = 13 * arctan(0.00076 * f) + 3.5 * arctan((f/7500)^2)
 *
 * Alternative (Traunmüller, 1990):
 *   Bark = 26.81 * f / (1960 + f) - 0.53
 *
 * This implementation uses the Zwicker formula (more accurate).
 *
 * @param freq_hz  Frequency in Hertz
 * @return Frequency in Bark
 */
double hz_to_bark(double freq_hz);

/**
 * Convert Bark scale back to frequency in Hz.
 * Inverse of hz_to_bark() via numerical inversion (Newton-Raphson).
 */
double bark_to_hz(double bark);

/**
 * Compute the Equivalent Rectangular Bandwidth (ERB) at a given frequency.
 *
 * ERB scale (Glasberg & Moore, 1990):
 *   ERB = 24.7 * (4.37 * f / 1000 + 1)
 *
 * The ERB scale is an alternative to the Bark scale, more accurate at
 * low frequencies and used in modern codecs (e.g., Opus).
 *
 * @param freq_hz  Center frequency in Hz
 * @return ERB bandwidth in Hz
 */
double erb_bandwidth(double freq_hz);

/**
 * Initialize the Bark scale structure for a given frequency range.
 *
 * Divides 0 Hz to nyquist (sample_rate/2) into critical bands.
 * Each critical band corresponds to 1 Bark in width.
 *
 * @param bark         Bark scale structure to fill
 * @param sample_rate  Sample rate in Hz
 * @param nyquist      Upper frequency bound (typically sample_rate/2)
 */
void bark_scale_init(bark_scale_t *bark, uint32_t sample_rate, double nyquist);

/* ==========================================================================
 * L4: Absolute Threshold of Hearing (ATH)
 * ========================================================================== */

/**
 * Compute the Absolute Threshold of Hearing (ATH) at a given frequency.
 *
 * ATH defines the minimum SPL at which a pure tone is audible in quiet.
 * Formula (Terhardt, 1979 — ISO 389-7 reference):
 *   ATH(f) = 3.64*(f/1000)^(-0.8) - 6.5*exp(-0.6*(f/1000 - 3.3)^2)
 *            + 1e-3*(f/1000)^4   [dB SPL]
 *
 * This represents the fundamental limit of human hearing sensitivity.
 * Any sound below ATH is inaudible regardless of masking.
 *
 * @param freq_hz  Frequency in Hz (20 to 20000)
 * @return ATH in dB SPL
 */
double ath_spl_db(double freq_hz);

/**
 * Compute ATH values for all critical bands.
 */
void ath_per_band(const bark_scale_t *bark, double ath_db[]);

/* ==========================================================================
 * L5: Spreading Function and Masking Threshold
 * ========================================================================== */

/**
 * Compute the spreading function from one critical band to another.
 *
 * The spreading function S(i,j) describes how much a masker in band j
 * contributes to the masking threshold in band i.
 *
 * Formula (ISO/IEC 11172-3, Psychoacoustic Model 1):
 *   S(dz) = 15.81 + 7.5*(dz+0.474) - 17.5*sqrt(1 + (dz+0.474)^2)  [dB]
 *   where dz = i - j (Bark distance)
 *
 * For dz < 0 (upward spread): S(dz) = S(dz) + 0.5*dz
 * For dz >= 0 (downward spread): S(dz) = S(dz) - 0.5*dz
 *
 * Asymmetric: masking spreads more upward in frequency than downward.
 *
 * @param bark_dist  Distance in Bark (i - j)
 * @return Spreading attenuation in dB (negative value)
 */
double spreading_function(double bark_dist);

/**
 * Compute the individual masking threshold for a single masker.
 *
 * tm(j, i) = SPL(j) - offset(j) + S(i - j)
 *
 * where offset depends on tonality:
 *   tonal masker:   offset = 6.025 + 0.275 * bark(j)  [dB]
 *   noise masker:   offset = 2.025 + 0.175 * bark(j)  [dB]
 *
 * The offset represents the difference in masking effectiveness between
 * tonal maskers and noise maskers (tonal maskers are less effective because
 * the auditory system can more easily "hear through" them).
 *
 * @param spl_j       SPL of masker at band j (dB)
 * @param bark_j      Bark value of masker band
 * @param bark_i      Bark value of target band
 * @param is_tonal    TONALITY_TONAL or TONALITY_NOISE
 * @return Masking threshold contributed at band i (dB SPL)
 */
double individual_masking_threshold(double spl_j, double bark_j,
                                     double bark_i, tonality_t is_tonal);

/**
 * Compute the Signal-to-Mask Ratio for a critical band.
 *
 * SMR = SPL_signal - max(global_mask, ATH)
 *
 * If SMR > 0, the signal is audible above the masking threshold and
 * requires quantization with sufficient SNR.
 * If SMR <= 0, the signal is masked — quantization noise is inaudible.
 *
 * @param spl_signal   Signal SPL in band (dB)
 * @param mask_thresh  Masking threshold in band (dB)
 * @param ath_db       ATH in band (dB)
 * @return SMR in dB
 */
double compute_smr(double spl_signal, double mask_thresh, double ath_db);

/**
 * Estimate tonality of a spectrum within a critical band.
 *
 * Uses the Spectral Flatness Measure (SFM):
 *   SFM = geometric_mean / arithmetic_mean
 *
 *   SFM close to 0 → tonal (peaked spectrum)
 *   SFM close to 1 → noise-like (flat spectrum)
 *
 * Returns TONALITY_TONAL if tonality_index > 0.5, else TONALITY_NOISE.
 *
 * @param spectrum    Array of FFT magnitude values
 * @param start_bin   First FFT bin in the band
 * @param num_bins    Number of FFT bins in the band
 * @return Tonality classification
 */
tonality_t estimate_tonality(const double *spectrum, uint32_t start_bin,
                             uint32_t num_bins);

/* ==========================================================================
 * L8: Perceptual Entropy
 * ========================================================================== */

/**
 * Compute perceptual entropy of an audio frame.
 *
 * Perceptual Entropy (PE) measures the theoretical minimum bitrate needed
 * to encode a signal transparently (without perceptual distortion).
 *
 * PE = Σ_{bands} log2( floor(NINT( |X[k]| / (6 * sqrt(mask_thr[k])) ) + 1) )
 *
 * where NINT rounds to nearest integer.
 *
 * PE = 0: signal is completely masked, no bits needed
 * PE high: complex signal requiring high bitrate
 *
 * Reference: Johnston, "Estimation of Perceptual Entropy...", ICASSP 1988
 *
 * @param state  Psychoacoustic state after psychoacoustic_analyze()
 * @return Perceptual entropy in bits per frame
 */
double perceptual_entropy_bits(const psychoacoustic_state_t *state);

/**
 * Estimate target bitrate from perceptual entropy.
 * bitrate = PE * sample_rate / fft_size  [bits/sec]
 */
double pe_to_bitrate(const psychoacoustic_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PSYCHOACOUSTIC_H */

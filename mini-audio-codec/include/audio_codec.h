/**
 * audio_codec.h — Core Audio Codec Definitions and PCM API
 *
 * L1 Definitions: PCM sample format, codec parameters, audio frame structure
 * L2 Core Concepts: PCM encoding/decoding, companding (μ-law, A-law)
 * L4 Fundamental Laws: Nyquist-Shannon sampling theorem (audio context)
 *
 * Reference: Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
 *            ITU-T G.711 — Pulse Code Modulation (PCM) companding standard
 *            Rabiner & Schafer, "Theory and Applications of Digital Speech Processing"
 *
 * Course Mapping:
 *   MIT 6.003 — Signal Processing (sampling, quantization)
 *   Stanford EE102A — Signal Processing (A/D conversion)
 *   ETH 227-0427 — Signal Processing (quantization theory)
 */

#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Audio Sample Formats and Parameters
 * ========================================================================== */

/** PCM sample format — determines bit depth and representation */
typedef enum {
    PCM_S16LE = 0,  /**< Signed 16-bit little-endian PCM */
    PCM_S24LE,      /**< Signed 24-bit little-endian PCM (packed) */
    PCM_S32LE,      /**< Signed 32-bit little-endian PCM */
    PCM_FLOAT32,    /**< 32-bit IEEE 754 floating point, range [-1.0, 1.0] */
    PCM_FLOAT64,    /**< 64-bit IEEE 754 floating point, range [-1.0, 1.0] */
    PCM_ALAW,       /**< ITU-T G.711 A-law companded 8-bit */
    PCM_ULAW,       /**< ITU-T G.711 μ-law companded 8-bit */
    PCM_S8          /**< Signed 8-bit PCM */
} pcm_format_t;

/** Audio channel layout */
typedef enum {
    CH_MONO   = 1,  /**< Single channel (center) */
    CH_STEREO = 2,  /**< Left + Right */
    CH_2_1    = 3,  /**< L/R + LFE */
    CH_5_1    = 6,  /**< 5.1 surround */
    CH_7_1    = 8   /**< 7.1 surround */
} channel_layout_t;

/** Audio codec parameters — defines the codec stream properties */
typedef struct {
    uint32_t sample_rate;        /**< Samples per second (Hz): 8000, 44100, 48000, 96000 */
    uint16_t bit_depth;          /**< Bits per sample: 8, 16, 24, 32 */
    uint16_t num_channels;       /**< Number of audio channels */
    pcm_format_t format;         /**< PCM sample format */
    uint32_t frame_size;         /**< Samples per frame (per channel) */
    uint32_t bitrate;            /**< Target bitrate in bits per second */
} codec_params_t;

/** A single audio frame — the fundamental unit of audio codec processing */
typedef struct {
    int32_t   frame_index;       /**< Sequential frame number, starts from 0 */
    uint32_t  num_samples;       /**< Number of samples in this frame (per channel) */
    uint16_t  num_channels;      /**< Number of interleaved channels */
    double   *samples;           /**< Interleaved sample data (ch0_s0, ch1_s0, ch0_s1, ...) */
} audio_frame_t;

/** Audio stream descriptor — metadata for an entire audio file/stream */
typedef struct {
    codec_params_t params;       /**< Codec parameters */
    uint64_t total_frames;       /**< Total number of frames in stream */
    uint64_t total_samples;      /**< Total samples (all channels combined) */
    double    duration_sec;      /**< Duration in seconds */
} audio_stream_t;

/** Quantizer — maps continuous values to discrete PCM levels */
typedef struct {
    double   step_size;          /**< Quantization step size Δ = V_fullscale / 2^bits */
    double   full_scale;         /**< Full-scale voltage range */
    uint16_t bits;               /**< Resolution in bits */
    int32_t  min_level;          /**< Minimum quantization level */
    int32_t  max_level;          /**< Maximum quantization level */
} quantizer_t;

/* ==========================================================================
 * L2: Core Concepts — PCM Encoding/Decoding
 * ========================================================================== */

/**
 * Initialize codec parameters with standard values.
 * @param params    Pointer to codec_params_t to initialize
 * @param sr        Sample rate in Hz (e.g., 44100 for CD quality)
 * @param bits      Bit depth (e.g., 16 for CD quality)
 * @param channels  Number of channels (1 = mono, 2 = stereo)
 * @param format    PCM sample format
 */
void codec_params_init(codec_params_t *params, uint32_t sr, uint16_t bits,
                       uint16_t channels, pcm_format_t format);

/**
 * Validate codec parameters against known standards.
 * Returns 0 if valid, negative error code otherwise.
 */
int codec_params_validate(const codec_params_t *params);

/**
 * Compute the raw byte rate (bytes/sec) for uncompressed PCM audio.
 * Formula: sample_rate * bit_depth * num_channels / 8
 */
uint32_t codec_byte_rate(const codec_params_t *params);

/**
 * Compute frame duration in seconds.
 * frame_duration = frame_size / sample_rate
 */
double frame_duration_sec(const codec_params_t *params);

/**
 * Allocate an audio frame with the given number of samples and channels.
 * Returns 0 on success, -1 on allocation failure.
 */
int audio_frame_alloc(audio_frame_t *frame, uint32_t num_samples, uint16_t num_channels);

/**
 * Free resources held by an audio frame.
 */
void audio_frame_free(audio_frame_t *frame);

/**
 * Compute the RMS (Root Mean Square) energy of an audio frame.
 * RMS = sqrt( (1/N) * Σ x[n]^2 )
 * Used for loudness estimation, silence detection.
 */
double audio_frame_rms(const audio_frame_t *frame);

/**
 * Compute peak amplitude (absolute maximum sample value).
 */
double audio_frame_peak(const audio_frame_t *frame);

/**
 * Detect silence: returns 1 if RMS < threshold, 0 otherwise.
 */
int audio_frame_is_silence(const audio_frame_t *frame, double threshold_db);

/* ==========================================================================
 * L5: Algorithms — μ-law and A-law Companding (ITU-T G.711)
 * ========================================================================== */

/**
 * ITU-T G.711 μ-law compressor.
 * Compresses 16-bit linear PCM to 8-bit μ-law.
 * μ-law is used in North America and Japan (T1 digital telephony).
 *
 * Formula: y = sgn(x) * ln(1 + μ|x|) / ln(1 + μ), μ = 255
 *
 * Reference: ITU-T Recommendation G.711 (1988)
 */
uint8_t linear_to_ulaw(int16_t sample);

/**
 * ITU-T G.711 μ-law expander.
 * Expands 8-bit μ-law back to 16-bit linear PCM.
 */
int16_t ulaw_to_linear(uint8_t ulaw_byte);

/**
 * ITU-T G.711 A-law compressor.
 * Compresses 16-bit linear PCM to 8-bit A-law.
 * A-law is used in Europe (E1 digital telephony).
 *
 * Formula (piecewise): A = 87.6
 *
 * Reference: ITU-T Recommendation G.711 (1988)
 */
uint8_t linear_to_alaw(int16_t sample);

/**
 * ITU-T G.711 A-law expander.
 * Expands 8-bit A-law back to 16-bit linear PCM.
 */
int16_t alaw_to_linear(uint8_t alaw_byte);

/**
 * Convert a buffer of linear PCM samples to μ-law in place.
 * @param buf       Interleaved sample buffer (16-bit signed)
 * @param num_samples Total number of samples to convert
 * @param out       Output buffer for μ-law bytes (must be same length)
 */
void linear_to_ulaw_buffer(const int16_t *buf, uint8_t *out, size_t num_samples);

/**
 * Convert a buffer of μ-law bytes to linear PCM.
 */
void ulaw_to_linear_buffer(const uint8_t *buf, int16_t *out, size_t num_samples);

/* ==========================================================================
 * L4: Rate-Distortion and Quantization
 * ========================================================================== */

/**
 * Compute quantization noise power for a uniform quantizer.
 *
 * Theory: For uniform quantization with step size Δ,
 *   quantization error e is uniformly distributed on [-Δ/2, Δ/2].
 *   E[e^2] = Δ^2 / 12
 *
 * @param step_size   Quantization step Δ
 * @return Mean squared quantization error
 */
double quant_noise_power(double step_size);

/**
 * Compute SQNR (Signal-to-Quantization-Noise Ratio) in dB.
 *
 * Formula: SQNR ≈ 6.02 * bits + 1.76 dB (for full-scale sine wave)
 *
 * Reference: Oppenheim & Schafer, "Discrete-Time Signal Processing" (2010), Ch. 4.8
 *
 * @param bits          Number of quantization bits
 * @param signal_rms    RMS value of input signal (must be > 0)
 * @param full_scale    Full-scale range of quantizer
 * @return SQNR in dB; returns -INFINITY if signal_rms <= 0
 */
double compute_sqnr_db(uint16_t bits, double signal_rms, double full_scale);

/**
 * Compute the minimum bit depth needed to achieve a target SQNR.
 * Derived from SQNR formula: bits_min = ceil((sqnr_target_db - 1.76) / 6.02)
 *
 * @param sqnr_target_db Target SQNR in dB
 * @return Minimum bits needed (clamped to [1, 32])
 */
uint16_t min_bits_for_sqnr(double sqnr_target_db);

/**
 * Design a uniform quantizer for given parameters.
 */
void quantizer_init(quantizer_t *q, uint16_t bits, double full_scale);

/**
 * Quantize a single sample value to the nearest integer level.
 * Handles saturation (clipping) at min/max levels.
 */
int32_t quantize_sample(const quantizer_t *q, double value);

/**
 * Dequantize a quantized level back to a floating-point value.
 */
double dequantize_sample(const quantizer_t *q, int32_t level);

/* ==========================================================================
 * L6: WAV File I/O (RIFF/WAVE container format)
 * ========================================================================== */

/**
 * Write PCM audio data to a WAV file.
 *
 * RIFF/WAVE format structure:
 *   RIFF header (12 bytes) + fmt chunk (24 bytes) + data chunk header (8 bytes)
 *
 * @param filename      Output file path
 * @param params        Codec parameters
 * @param samples       Interleaved sample array (format must match params.format)
 * @param num_samples   Total number of samples (all channels combined)
 * @return 0 on success, negative on error
 */
int wav_write(const char *filename, const codec_params_t *params,
              const void *samples, uint64_t num_samples);

/**
 * Read a WAV file and populate codec parameters and sample data.
 * Allocates sample buffer internally; caller must free with free().
 *
 * @param filename      Input WAV file path
 * @param params        Output: codec parameters read from WAV header
 * @param samples_out   Output: pointer to allocated sample buffer
 * @param num_samples_out Output: number of samples read
 * @return 0 on success, negative on error
 */
int wav_read(const char *filename, codec_params_t *params,
             void **samples_out, uint64_t *num_samples_out);

/**
 * Read only the WAV header (no sample data).
 * Used for stream inspection without loading full audio into memory.
 */
int wav_read_header(const char *filename, codec_params_t *params,
                    uint64_t *num_samples_out);

/* ==========================================================================
 * L2: Sample Rate Conversion (Nyquist-Shannon Theorem Application)
 * ========================================================================== */

/**
 * Compute the decimation factor for rational sample rate conversion.
 *
 * Given input rate fs_in and output rate fs_out,
 * find integers L (interpolation) and M (decimation) such that:
 *   fs_out = fs_in * L / M
 *
 * Uses GCD for simplification.
 *
 * @param fs_in     Input sample rate
 * @param fs_out    Output sample rate
 * @param L_out     Output: interpolation factor
 * @param M_out     Output: decimation factor
 * @return 0 on success, -1 if rates are incompatible
 */
int compute_src_factors(uint32_t fs_in, uint32_t fs_out,
                        uint32_t *L_out, uint32_t *M_out);

/* ==========================================================================
 * Utility functions
 * ========================================================================== */

/** Convert dB to linear amplitude ratio: 10^(dB/20) */
double db_to_linear(double db);

/** Convert linear amplitude ratio to dB: 20 * log10(linear) */
double linear_to_db(double linear);

/** Clamp double value to [-1.0, 1.0] range */
double clamp_sample(double value);

/** Greatest Common Divisor (Euclidean algorithm) */
uint32_t gcd_u32(uint32_t a, uint32_t b);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CODEC_H */

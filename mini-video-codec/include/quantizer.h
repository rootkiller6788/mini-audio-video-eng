/**
 * quantizer.h — Quantization and Rate Control for Video Coding
 *
 * L1: QP definitions, quantization matrices, dead-zone parameters
 * L2: Scalar quantization, dead-zone quantization, rate-distortion
 * L4: Rate-Distortion Theory (Shannon's R(D) lower bound)
 * L5: QP→Qstep mapping, rate control algorithms (CBR, VBR, CQP)
 *
 * Quantization is the primary lossy compression step in video coding.
 * It maps DCT coefficients to a smaller set of reconstruction levels,
 * discarding perceptually less important information.
 *
 * Reference:
 *   ITU-T H.264, Section 8.5 — Transformation and Quantization
 *   Wiegand et al., "Rate-Constrained Coder Control and Comparison of
 *     Video Coding Standards", IEEE Trans. CSVT, 2003
 *   Sullivan & Wiegand, "Rate-Distortion Optimization for Video
 *     Compression", IEEE Signal Processing Magazine, 1998
 *   Cover & Thomas, "Elements of Information Theory", Ch. 10 — Rate Distortion
 *
 * Course Mapping:
 *   MIT 6.450 — Digital Communications (quantization)
 *   Stanford EE392J — Video Compression
 *   Berkeley EE225B — Source Coding
 */

#ifndef QUANTIZER_H
#define QUANTIZER_H

#include "video_codec.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Quantization Parameters
 * ========================================================================== */

/**
 * H.264 QP to quantization step (Qstep) mapping.
 *
 * QP doubles Qstep every 6 steps:
 *   Qstep(QP) = Qstep(QP%6) * 2^{floor(QP/6)}
 *
 * QP%6 → Qstep mapping:
 *   0→0.625, 1→0.6875, 2→0.8125, 3→0.875, 4→1.0, 5→1.125
 */
#define QP_MIN  0
#define QP_MAX 51

/** Quantization scaling list (flat default: all 16) */
#define DEFAULT_SCALING_4x4  16
#define DEFAULT_SCALING_8x8  16

/** Quantizer state for a single transform coefficient */
typedef struct {
    int32_t  qp;            /**< Quantization parameter (0-51) */
    double   qstep;         /**< Quantization step size */
    int32_t  mf;            /**< Multiplication factor (Qstep * 2^qbits) */
    int32_t  qbits;         /**< Right-shift amount for integer quant */
    int32_t  f;             /**< Rounding offset (dead-zone offset) */
} quantizer_t;

/** Rate control mode */
typedef enum {
    RC_CQP = 0,  /**< Constant QP (no rate control) */
    RC_CBR = 1,  /**< Constant Bitrate */
    RC_VBR = 2,  /**< Variable Bitrate */
    RC_CRF = 3,  /**< Constant Rate Factor (quality-based) */
    RC_ABR = 4,  /**< Average Bitrate */
} rc_mode_t;

/** Rate control state */
typedef struct {
    rc_mode_t mode;              /**< Rate control mode */
    uint32_t  target_bitrate;    /**< Target bitrate (bps) */
    uint32_t  vbv_bufsize;       /**< VBV buffer size (bits) */
    uint32_t  vbv_maxrate;       /**< VBV max bitrate (bps) */
    uint32_t  frame_rate_num;    /**< Frame rate numerator */
    uint32_t  frame_rate_den;    /**< Frame rate denominator */
    double    qp;                /**< Current (base) QP (floating for smooth adaptation) */
    uint32_t  qp_int;            /**< Rounded integer QP */
    uint64_t  bits_accum;        /**< Accumulated bits over sliding window */
    uint64_t  frames_encoded;    /**< Number of frames encoded */
    int64_t   buffer_fullness;   /**< VBV buffer fullness (bits); negative=underflow */
    double    last_frame_bits;   /**< Bits used for last frame */
    double    complexity_est;    /**< Estimated frame complexity */
    double    rate_factor;       /**< CRF quality factor */
} rc_state_t;

/** Quantization matrix for selective coefficient weighting */
typedef struct {
    uint32_t size;           /**< Block size (4 or 8) */
    uint8_t  scale[64];      /**< Scaling factors (H.264: 1-255, 16=flat) */
} quant_matrix_t;

/* ==========================================================================
 * L2: Core Concepts — QP / Qstep Conversion
 * ========================================================================== */

/**
 * Convert QP (0-51) to quantization step size.
 *
 * Qstep = Qstep_base[QP % 6] * 2^{QP / 6}
 *
 * Reference: ITU-T H.264, Table 7-3
 *
 * @param qp   Quantization parameter (0-51)
 * @return Quantization step size (0.625 to 224.0)
 */
double qp_to_qstep(uint32_t qp);

/**
 * Convert quantization step size to QP (inverse mapping).
 * Finds the nearest QP value.
 */
uint32_t qstep_to_qp(double qstep);

/**
 * Initialize a quantizer for a given QP value.
 * Precomputes mf, qbits, and rounding offset for integer DCT quantization.
 *
 * For H.264 4x4 forward transform:
 *   |W_ij| = (|C_ij| * MF + f) >> qbits
 *   where MF = Qstep * 2^qbits
 *
 * @param q        Quantizer struct to initialize
 * @param qp       Quantization parameter
 * @param intra    1 = intra (rounding offset = 2^{qbits}/3), 0 = inter (offset = 2^{qbits}/6)
 */
void quantizer_init(quantizer_t *q, uint32_t qp, int intra);

/* ==========================================================================
 * L2: Core Concepts — Quantization and Dequantization
 * ========================================================================== */

/**
 * Quantize a single DCT coefficient (dead-zone).
 *
 * Forward quantization with dead-zone (H.264 style):
 *   |W| = (|C| * MF + f) >> qbits
 *   sign(W) = sign(C)
 *
 * where f = 2^{qbits}/3 for intra, 2^{qbits}/6 for inter.
 * Dead-zone: small coefficients near zero are quantized to exactly zero.
 *
 * @param q        Initialized quantizer
 * @param coeff    Input DCT coefficient
 * @return Quantized coefficient level
 */
int32_t quantize_coeff(const quantizer_t *q, int32_t coeff);

/**
 * Dequantize a single quantized coefficient.
 *
 * Inverse quantization (H.264 style):
 *   C' = W * Qstep * scale  (with appropriate rounding)
 *
 * For integer implementation:
 *   C' = W * MF_inv << (qbits_inv)
 *
 * @param q        Quantizer with same QP as encoder
 * @param level    Quantized coefficient level
 * @return Reconstructed (dequantized) DCT coefficient
 */
int32_t dequantize_coeff(const quantizer_t *q, int32_t level);

/**
 * Quantize a 4x4 coefficient block (in-place).
 *
 * @param q        Quantizer
 * @param coeffs   Input: 16 DCT coefficients. Output: quantized levels.
 * @return Number of non-zero coefficients after quantization
 */
int quantize_block_4x4(const quantizer_t *q, int32_t *coeffs);

/**
 * Dequantize a 4x4 coefficient block (in-place).
 */
void dequantize_block_4x4(const quantizer_t *q, int32_t *coeffs);

/**
 * Quantize an 8x8 coefficient block (in-place).
 */
int quantize_block_8x8(const quantizer_t *q, int32_t *coeffs);

/**
 * Dequantize an 8x8 coefficient block (in-place).
 */
void dequantize_block_8x8(const quantizer_t *q, int32_t *coeffs);

/* ==========================================================================
 * L2: Core Concepts — Quantization with Scaling Matrix
 * ========================================================================== */

/**
 * Quantize a block using a custom scaling matrix.
 *
 * With scaling matrix:
 *   |W_ij| = (|C_ij| * MF * 16 / scale_ij + f) >> qbits
 *
 * The scaling matrix allows perceptual weighting: high-frequency
 * coefficients can be quantized more coarsely (larger scale factor)
 * since they are less visible to the human eye.
 *
 * @param coeffs     Input/output coefficients
 * @param block_size 4 or 8
 * @param qp         Quantization parameter
 * @param matrix     Scaling matrix (flat=16 for all entries)
 * @param intra      1=intra, 0=inter
 * @return Number of non-zero coefficients
 */
int quantize_block_scaled(int32_t *coeffs, uint32_t block_size,
                          uint32_t qp, const quant_matrix_t *matrix, int intra);

/**
 * Dequantize with scaling matrix.
 */
void dequantize_block_scaled(int32_t *coeffs, uint32_t block_size,
                             uint32_t qp, const quant_matrix_t *matrix);

/* ==========================================================================
 * L4: Rate-Distortion Theory
 * ========================================================================== */

/**
 * Compute the theoretical Rate-Distortion lower bound for a Gaussian source.
 *
 * For a memoryless Gaussian source with variance sigma^2 and squared error
 * distortion D:
 *   R(D) = 1/2 * log2(sigma^2 / D)  for 0 <= D <= sigma^2
 *   R(D) = 0                          for D > sigma^2
 *
 * This is the Shannon lower bound — no codec can achieve a lower rate
 * for the given distortion.
 *
 * Reference: Cover & Thomas, Theorem 10.3.2 (Gaussian RDF)
 *
 * @param variance   Signal variance (sigma^2)
 * @param distortion Target distortion D (MSE)
 * @return Rate in bits per sample, or 0 if distortion >= variance
 */
double rate_distortion_gaussian(double variance, double distortion);

/**
 * Compute distortion from quantizer step size (high-rate approximation).
 *
 * For a uniform quantizer with step Delta:
 *   D ~= Delta^2 / 12
 *
 * @param step_size   Quantization step Delta
 * @return Approximate MSE
 */
double quant_distortion(double step_size);

/**
 * Compute the SQNR (Signal-to-Quantization-Noise Ratio) for a given QP.
 *
 * For 8-bit video (peak = 255):
 *   SQNR ~= 6.02 * (8 - QP/6) + 1.76  dB  (approximate)
 *
 * @param qp       Quantization parameter (0-51)
 * @param bit_depth Bit depth of samples (8, 10, 12)
 * @return SQNR in dB
 */
double quant_sqnr_db(uint32_t qp, uint32_t bit_depth);

/* ==========================================================================
 * L5: Algorithms — Rate Control
 * ========================================================================== */

/**
 * Initialize rate control state.
 *
 * @param rc      Rate control state to initialize
 * @param mode    Rate control mode (CQP, CBR, VBR, CRF)
 * @param bitrate Target bitrate (bps), ignored for CQP/CRF
 */
void rc_init(rc_state_t *rc, rc_mode_t mode, uint32_t bitrate,
             uint32_t fps_num, uint32_t fps_den);

/**
 * Compute the QP for the next frame based on rate control state.
 *
 * CQP: returns fixed QP
 * CBR: adjusts QP based on buffer fullness and target bits
 * CRF: adjusts QP based on complexity
 *
 * @param rc          Rate control state
 * @param complexity  Estimated complexity of current frame (e.g., intra_sad)
 * @return QP value for the next frame (clamped to [0, 51])
 */
uint32_t rc_compute_qp(rc_state_t *rc, double complexity);

/**
 * Update rate control state after encoding a frame.
 *
 * @param rc         Rate control state
 * @param bits_used  Number of bits actually used for this frame
 */
void rc_update(rc_state_t *rc, uint64_t bits_used);

/**
 * Compute the target bits for a frame in CBR mode.
 *
 * target_bits = bitrate / frame_rate
 */
double rc_target_bits_per_frame(const rc_state_t *rc);

/**
 * Get the current buffer fullness as a fraction [0, 1].
 * 0.5 = perfectly on target, >0.8 = risk of overflow.
 */
double rc_buffer_fullness_frac(const rc_state_t *rc);

/**
 * Simple frame complexity estimator using intra DC SAD.
 * Returns an estimate of relative coding complexity.
 */
double estimate_frame_complexity(const video_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* QUANTIZER_H */

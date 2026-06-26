/**
 * dct_2d.h — 2D Discrete Cosine Transform for Video Coding
 *
 * L1: DCT transform state and block definitions (4x4, 8x8)
 * L3: Mathematical structure — Separable 2D DCT via 1D DCT row/column
 * L5: Integer DCT (H.264 style), Hadamard transform, scaling matrices
 * L4: Parseval energy conservation property
 *
 * The 2D DCT is the core transform used in all modern video codecs
 * (MPEG-1/2/4, H.261/263/264/265, VP8/9, AV1).
 * H.264 uses an integer approximation for exact bit-exact decoding.
 *
 * Reference:
 *   Ahmed, Natarajan & Rao, "Discrete Cosine Transform", IEEE Trans. C-23, 1974
 *   Malvar et al., "Low-Complexity Transform and Quantization in H.264/AVC",
 *     IEEE Trans. CSVT, 2003
 *   ITU-T H.264 Annex A — Transform and Quantization
 *   Rao & Yip, "Discrete Cosine Transform: Algorithms, Advantages, Applications" (1990)
 *
 * Course Mapping:
 *   MIT 6.003 — Fourier/DCT transforms
 *   Stanford EE392J — Image and Video Compression (transform coding)
 *   ETH 227-0447 — Image and Video Processing
 *   Berkeley EE225B — Digital Image Processing
 *   Illinois ECE 418 — Image and Video Processing
 */

#ifndef DCT_2D_H
#define DCT_2D_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Transform Block Types
 * ========================================================================== */

/** DCT transform size variants used in H.264 */
typedef enum {
    DCT_4x4  = 4,   /**< 4x4 integer DCT (all H.264 profiles) */
    DCT_8x8  = 8,   /**< 8x8 integer DCT (High profile and above) */
    DCT_16x16 = 16, /**< 16x16 DCT (HEVC/H.265) */
    DCT_32x32 = 32, /**< 32x32 DCT (HEVC/H.265) */
} dct_size_t;

/** 2D DCT engine state — holds precomputed tables for a given block size */
typedef struct {
    uint32_t N;               /**< Block dimension (4, 8, 16, 32) */
    uint32_t N2;              /**< N*N (total coefficients per block) */
    int     *forward_mat;     /**< Forward transform matrix (NxN, row-major) */
    int     *inverse_mat;     /**< Inverse transform matrix (NxN, row-major) */
    int      forward_shift;   /**< Right-shift after forward transform */
    int      inverse_shift;   /**< Right-shift after inverse transform */
    int      scaling_factor;  /**< Overall scaling factor for normalization */
} dct_engine_t;

/** Residual block — input to transform (pixel domain difference) */
typedef struct {
    uint32_t size;       /**< Block size (N) */
    int16_t *data;       /**< NxN residual samples, row-major */
    uint32_t stride;     /**< Row stride in samples */
} residual_block_t;

/** Coefficient block — output of forward transform (frequency domain) */
typedef struct {
    uint32_t size;       /**< Block size (N) */
    int32_t *coeffs;     /**< NxN DCT coefficients, row-major */
    uint32_t stride;     /**< Row stride in coefficients */
    uint32_t nonzero;    /**< Number of non-zero coefficients */
    uint32_t last_nz;    /**< Index of last non-zero coefficient (zigzag scan) */
} coeff_block_t;

/** Zigzag scan order table size */
#define ZIGZAG_4x4_LEN 16
#define ZIGZAG_8x8_LEN 64

/* ==========================================================================
 * L3: Mathematical Structures — 1D DCT Basis
 * ========================================================================== */

/**
 * Compute the 1D DCT Type-II basis value.
 *
 * DCT-II definition:
 *   X[k] = sum_{n=0}^{N-1} x[n] * cos(pi/N * (n + 0.5) * k)
 *
 * Basis function: phi(k, n) = cos(pi/N * (n + 0.5) * k)
 *
 * Reference: Ahmed, Natarajan & Rao (1974), Eq. (3)
 *
 * @param k   Frequency index (0..N-1), DC component at k=0
 * @param n   Time/sample index (0..N-1)
 * @param N   Transform size
 * @return Basis function value in [-1.0, 1.0]
 */
double dct_basis(uint32_t k, uint32_t n, uint32_t N);

/**
 * Compute the 1D DCT Type-III (inverse) basis value.
 *
 * IDCT basis: phi_inv(k, n) = cos(pi/N * k * (n + 0.5))
 * This is the transpose of the DCT-II basis.
 */
double idct_basis(uint32_t k, uint32_t n, uint32_t N);

/* ==========================================================================
 * L5: Algorithms — Forward and Inverse 2D DCT (Floating Point)
 * ========================================================================== */

/**
 * Compute the forward 2D DCT Type-II on an NxN block.
 *
 * Algorithm: separable row-column method
 *   1. Apply 1D DCT to each row:  Z[n][k] = sum_{m} x[n][m] * phi(k,m)
 *   2. Apply 1D DCT to each column: X[k][l] = sum_{n} Z[n][k] * phi(l,n)
 *
 * Normalization: DC coefficient is divided by N, others are scaled properly.
 *
 * Complexity: O(N^3) naive, can be optimized to O(N^2 log N) via FFT.
 *
 * @param input   Input NxN spatial domain block (row-major, N*N entries)
 * @param output  Output NxN DCT coefficient block (row-major, N*N entries)
 * @param N       Block size (must be 2, 4, 8, 16, or 32)
 */
void dct_2d_fp(const double *input, double *output, uint32_t N);

/**
 * Compute the inverse 2D DCT Type-III on an NxN block.
 *
 * Algorithm: separable row-column method (transpose of forward DCT).
 * Perfect reconstruction if no quantization is applied.
 *
 * @param coeffs  Input NxN DCT coefficient block (row-major)
 * @param output  Output NxN spatial domain block (row-major)
 * @param N       Block size
 */
void idct_2d_fp(const double *coeffs, double *output, uint32_t N);

/* ==========================================================================
 * L5: Algorithms — H.264 Integer DCT (4x4)
 * ========================================================================== */

/**
 * H.264 4x4 Integer Forward DCT.
 *
 * Transform matrix H (scaled by 2 for integer arithmetic):
 *   H = [1  1  1  1;  2  1 -1 -2;  1 -1 -1  1;  1 -2  2 -1]
 *
 * Forward: Y = (H * X * H^T) .* S_f (where S_f is post-scaling)
 * Implemented as: Y = C_f X C_f^T >> (scaling)
 *
 * The transform is designed so that the inverse uses exact integer arithmetic
 * with only additions and shifts (no multiplications in the core).
 *
 * Reference: ITU-T H.264, Section 8.5.8 "Transformation process for 4x4
 *            luma DC transform coefficients"
 *
 * @param block   4x4 input residual block (16 int16_t values, row-major)
 * @param coeffs  4x4 output DCT coefficients (16 int32_t values, row-major)
 */
void h264_dct_4x4_fwd(const int16_t *block, int32_t *coeffs);

/**
 * H.264 4x4 Integer Inverse DCT.
 *
 * Inverse transform matrix H_inv:
 *   [1  1  1  1/2;  1 1/2 -1 -1;  1 -1/2 -1 1;  1 -1 1 -1/2]
 *
 * Inverse: X' = H_inv^T * Y * H_inv
 *
 * Implemented with exact integer arithmetic that avoids drift between
 * encoder and decoder (normative in H.264 standard).
 *
 * @param coeffs  4x4 input DCT coefficients (16 int32_t)
 * @param block   4x4 output residual block (16 int16_t)
 */
void h264_idct_4x4_inv(const int32_t *coeffs, int16_t *block);

/**
 * H.264 4x4 Hadamard transform for luma DC coefficients (in 16x16 intra MB).
 *
 * Hadamard matrix H_4:
 *   [1  1  1  1;  1  1 -1 -1;  1 -1 -1  1;  1 -1  1 -1]
 *
 * Used to decorrelate the 16 DC coefficients from 4x4 blocks within a
 * 16x16 intra macroblock (provides additional compression).
 */
void h264_hadamard_4x4_fwd(const int32_t *input, int32_t *output);

/**
 * Inverse Hadamard 4x4 transform (same matrix — Hadamard is self-inverse
 * up to scaling).
 */
void h264_hadamard_4x4_inv(const int32_t *input, int32_t *output);

/* ==========================================================================
 * L5: Algorithms — H.264 Integer DCT (8x8, High Profile)
 * ========================================================================== */

/**
 * H.264 8x8 Integer Forward DCT (FRExt / High Profile).
 *
 * Uses an 8x8 integer transform matrix with 4-bit coefficients.
 * Designed for orthogonality with minimal dynamic range expansion.
 *
 * Reference: ITU-T H.264, Section 8.5.10
 *
 * @param block   8x8 input residual block (64 int16_t)
 * @param coeffs  8x8 output coefficients (64 int32_t)
 */
void h264_dct_8x8_fwd(const int16_t *block, int32_t *coeffs);

/**
 * H.264 8x8 Integer Inverse DCT.
 */
void h264_idct_8x8_inv(const int32_t *coeffs, int16_t *block);

/* ==========================================================================
 * L2: Core Concepts — Zigzag Scan and Coefficient Ordering
 * ========================================================================== */

/**
 * Get the 4x4 zigzag scan position mapping.
 *
 * Zigzag scan reorders 2D DCT coefficients into 1D in approximately
 * increasing frequency order, which tends to group non-zero coefficients
 * at the beginning of the scan (better for run-length / entropy coding).
 *
 * Returns a pointer to a static 16-element array mapping linear index
 * to (row, col) or linear 2D position.
 *
 * zigzag_4x4[i] = index into 4x4 row-major array for the i-th scan position
 */
const uint8_t *zigzag_4x4_table(void);

/**
 * Get the 8x8 zigzag scan position mapping.
 * Returns a pointer to a static 64-element array.
 */
const uint8_t *zigzag_8x8_table(void);

/**
 * Apply zigzag scan to reorder a 4x4 coefficient block into 1D array.
 *
 * @param coeffs_2d   Input: 4x4 coefficient block (row-major, 16 entries)
 * @param coeffs_1d   Output: 16 coefficients in zigzag scan order
 * @return Index of last non-zero coefficient (0-15, or -1 if all zero)
 */
int zigzag_scan_4x4(const int32_t *coeffs_2d, int32_t *coeffs_1d);

/**
 * Apply inverse zigzag scan (1D to 2D) for 4x4 block.
 */
void zigzag_iscan_4x4(const int32_t *coeffs_1d, int32_t *coeffs_2d);

/**
 * Apply zigzag scan for 8x8 block.
 */
int zigzag_scan_8x8(const int32_t *coeffs_2d, int32_t *coeffs_1d);

/**
 * Apply inverse zigzag scan for 8x8 block.
 */
void zigzag_iscan_8x8(const int32_t *coeffs_1d, int32_t *coeffs_2d);

/* ==========================================================================
 * L2: Core Concepts — Field vs. Frame Transform (MBAFF)
 * ========================================================================== */

/**
 * Field scan order for 4x4 blocks (used in interlaced/MBAFF coding).
 * In field mode, coefficients from alternating rows come from different
 * fields, so a field-optimized scan order groups coefficients from the
 * same field together.
 */
const uint8_t *field_scan_4x4_table(void);

/* ==========================================================================
 * L4: Fundamental Laws — Energy Conservation and PR Condition
 * ========================================================================== */

/**
 * Verify Parseval/Plancherel energy conservation for the DCT.
 *
 * For a unitary (orthonormal) DCT:
 *   sum_n |x[n]|^2 = sum_k |X[k]|^2
 *
 * For our scaled integer DCT, we check:
 *   |energy_spatial - energy_freq/scaling| < epsilon
 *
 * @return 1 if energy is conserved within tolerance, 0 otherwise
 */
int dct_verify_energy_conservation(const int16_t *block,
                                   const int32_t *coeffs,
                                   uint32_t N, double tolerance);

/**
 * Compute the DCT energy compaction efficiency.
 *
 * EC = (sum of largest K coefficient magnitudes) / (sum of all coefficient magnitudes)
 *
 * Higher EC means the DCT concentrates more energy in fewer coefficients,
 * which is desirable for compression.
 *
 * @param coeffs   NxN coefficient block
 * @param N        Block size
 * @param K        Number of top coefficients to consider
 * @return Energy compaction ratio in [0, 1]
 */
double dct_energy_compaction(const int32_t *coeffs, uint32_t N, uint32_t K);

/* ==========================================================================
 * L5: Algorithms — Transform Engine Initialization and Management
 * ========================================================================== */

/**
 * Initialize a DCT engine for a given block size.
 * Precomputes the forward and inverse transformation matrices.
 *
 * @param engine  Pointer to dct_engine_t (allocated by caller)
 * @param N       Block size (4, 8, 16, or 32)
 * @return 0 on success, -1 on invalid size or allocation failure
 */
int dct_engine_init(dct_engine_t *engine, uint32_t N);

/**
 * Free resources allocated by dct_engine_init.
 */
void dct_engine_free(dct_engine_t *engine);

/**
 * Apply forward transform using a pre-initialized engine.
 * Wrapper that dispatches to the correct size-specific implementation.
 *
 * @param engine   Initialized DCT engine
 * @param block    Input residual block (NxN, int16_t row-major)
 * @param coeffs   Output coefficient block (NxN, int32_t row-major)
 */
void dct_engine_fwd(const dct_engine_t *engine,
                    const int16_t *block, int32_t *coeffs);

/**
 * Apply inverse transform using a pre-initialized engine.
 */
void dct_engine_inv(const dct_engine_t *engine,
                    const int32_t *coeffs, int16_t *block);

/**
 * Dispatch table: get DCT function pointer for a given block size.
 * Returns NULL if size is not supported.
 */
typedef void (*dct_fwd_fn)(const int16_t *, int32_t *);
dct_fwd_fn dct_get_fwd_func(uint32_t N);

/**
 * Dispatch table: get IDCT function pointer for a given block size.
 */
typedef void (*dct_inv_fn)(const int32_t *, int16_t *);
dct_inv_fn dct_get_inv_func(uint32_t N);

#ifdef __cplusplus
}
#endif

#endif /* DCT_2D_H */

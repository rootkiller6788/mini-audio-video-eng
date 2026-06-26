/**
 * motion_est.h — Motion Estimation and Compensation for Video Coding
 *
 * L1: Motion vector, search range, block partition definitions
 * L2: Block matching, motion compensation, sub-pixel interpolation
 * L5: Full search, diamond search, hexagon search, SAD/SATD/SSD metrics
 * L6: Canonical H.264 motion estimation pipeline
 *
 * Motion estimation is the most computationally intensive part of a video
 * encoder (typically 60-80% of encoding time). It exploits temporal redundancy
 * between consecutive frames by finding the best matching block in a reference
 * frame and encoding only the residual difference plus a motion vector.
 *
 * Reference:
 *   Wiegand et al., "Overview of the H.264/AVC Video Coding Standard",
 *     IEEE Trans. CSVT, 2003
 *   Chen et al., "Fast Integer Pel and Fractional Pel Motion Estimation for
 *     H.264/AVC", Journal of Visual Communication, 2006
 *   Zhu & Ma, "A New Diamond Search Algorithm for Fast Block-Matching Motion
 *     Estimation", IEEE Trans. Image Proc., 2000
 *   Tourapis et al., "Enhanced Predictive Zonal Search for Single and Multiple
 *     Frame Motion Estimation", Proc. VCIP, 2002
 *
 * Course Mapping:
 *   MIT 6.344 — Digital Image Processing (motion estimation)
 *   Stanford EE392J — Digital Video Processing
 *   TU Munich — Video Coding
 *   Georgia Tech ECE 6601 — Video Compression
 */

#ifndef MOTION_EST_H
#define MOTION_EST_H

#include "video_codec.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Motion Vectors and Search Configuration
 * ========================================================================== */

/** Motion vector — integer or fractional pel displacement */
typedef struct {
    int16_t x;       /**< Horizontal displacement in quarter-pel units */
    int16_t y;       /**< Vertical displacement in quarter-pel units */
} motion_vector_t;

/** Block partition for motion compensation */
typedef struct {
    uint32_t x;              /**< Top-left x coordinate (pixels) in current frame */
    uint32_t y;              /**< Top-left y coordinate (pixels) in current frame */
    uint32_t width;          /**< Partition width (4, 8, or 16) */
    uint32_t height;         /**< Partition height (4, 8, or 16) */
    motion_vector_t mv;      /**< Motion vector for this partition */
    uint8_t ref_idx;         /**< Reference frame index (0 = L0[0]) */
    uint8_t pred_dir;        /**< 0=L0, 1=L1, 2=Bi-pred */
} me_partition_t;

/** Motion estimation search range */
typedef struct {
    int16_t min_x;           /**< Minimum horizontal search displacement */
    int16_t max_x;           /**< Maximum horizontal search displacement */
    int16_t min_y;           /**< Minimum vertical search displacement */
    int16_t max_y;           /**< Maximum vertical search displacement */
} me_search_range_t;

/** Motion estimation configuration */
typedef struct {
    me_search_range_t range;        /**< Search range in integer pels */
    uint32_t          subpel_refine; /**< 0=no, 1=half-pel, 2=quarter-pel */
    uint32_t          max_ref_frames; /**< Maximum reference frames to search */
    uint8_t           early_terminate; /**< Enable early termination heuristics */
    uint32_t          early_term_thresh; /**< SAD threshold for early stop */
    uint8_t           use_hadamard;  /**< Use SATD (Hadamard) instead of SAD */
} me_config_t;

/** Motion estimation result for a single macroblock */
typedef struct {
    motion_vector_t mv;          /**< Best motion vector found */
    uint32_t        cost;        /**< Best cost (SAD/SATD/SSD) */
    uint32_t        ref_idx;     /**< Best reference frame index */
    uint8_t         pred_dir;    /**< Prediction direction */
    uint32_t        partitions;  /**< Number of partitions (1, 2, or 4) */
    me_partition_t  part[16];    /**< Partition results (max 16 4x4 blocks per MB) */
} me_result_t;

/** Motion compensation prediction block */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *pred;          /**< Predicted pixel data (width*height entries) */
    uint8_t  owned;
} mc_pred_block_t;

/** Interpolation filter type for sub-pixel motion */
typedef enum {
    INTERP_NEAREST  = 0,  /**< Nearest neighbor (no interpolation) */
    INTERP_BILINEAR = 1,  /**< Bilinear interpolation */
    INTERP_H264_6TAP = 2, /**< H.264 6-tap Wiener filter for half-pel */
    INTERP_HEVC_8TAP = 3, /**< HEVC 8-tap DCT-based interpolation */
} interp_filter_t;

/* ==========================================================================
 * L2: Core Concepts — Sub-Pixel Interpolation
 * ========================================================================== */

/**
 * H.264 half-pel luma interpolation filter (6-tap Wiener).
 *
 * Filter coefficients: [1, -5, 20, 20, -5, 1] / 32
 *
 * Derived from Wiener-Hopf equations to minimize prediction error.
 * Applied separable: first horizontal, then vertical.
 *
 * Reference: ITU-T H.264, Section 8.4.2.2.1
 *
 * @param src      Source frame luma plane
 * @param src_w    Source frame width
 * @param src_h    Source frame height
 * @param x        Integer pixel x coordinate
 * @param y        Integer pixel y coordinate
 * @param dx       Fractional offset x (in quarter-pel units: 0,1,2,3)
 * @param dy       Fractional offset y (in quarter-pel units: 0,1,2,3)
 * @return Interpolated pixel value [0, 255]
 */
uint8_t h264_interp_luma(const uint8_t *src, uint32_t src_w, uint32_t src_h,
                         int32_t x, int32_t y, int32_t dx, int32_t dy);

/**
 * Bilinear chroma interpolation (1/8 pel precision in H.264).
 *
 * Reference: ITU-T H.264, Section 8.4.2.2.2
 *
 * @param src   Source chroma plane
 * @param src_w Chroma plane width
 * @param src_h Chroma plane height
 * @param x     Integer chroma pixel x
 * @param y     Integer chroma pixel y
 * @param dx    Fractional x in 1/8 pel (0..7)
 * @param dy    Fractional y in 1/8 pel (0..7)
 * @return Interpolated chroma value [0, 255]
 */
uint8_t h264_interp_chroma(const uint8_t *src, uint32_t src_w, uint32_t src_h,
                           int32_t x, int32_t y, int32_t dx, int32_t dy);

/* ==========================================================================
 * L5: Algorithms — Distortion Metrics for Block Matching
 * ========================================================================== */

/**
 * Compute SAD (Sum of Absolute Differences) between two blocks.
 *
 * SAD = sum_i sum_j |orig[i][j] - ref[i][j]|
 *
 * Most commonly used metric in video coding due to low computational cost.
 * O(N^2) complexity for NxN block.
 *
 * @param orig    Pointer to current block data (row-major)
 * @param o_stride Row stride of current block
 * @param ref     Pointer to reference block data (row-major)
 * @param r_stride Row stride of reference block
 * @param bw      Block width in pixels
 * @param bh      Block height in pixels
 * @return SAD value (0 for identical blocks)
 */
uint32_t compute_sad(const uint8_t *orig, uint32_t o_stride,
                     const uint8_t *ref, uint32_t r_stride,
                     uint32_t bw, uint32_t bh);

/**
 * Compute SSE (Sum of Squared Errors) between two blocks.
 *
 * SSE = sum_i sum_j (orig[i][j] - ref[i][j])^2
 *
 * Better correlates with perceived quality but more expensive (multiplications).
 */
uint64_t compute_sse(const uint8_t *orig, uint32_t o_stride,
                     const uint8_t *ref, uint32_t r_stride,
                     uint32_t bw, uint32_t bh);

/**
 * Compute SATD (Sum of Absolute Transformed Differences).
 *
 * 1. Compute differences: diff = orig - ref
 * 2. Apply 4x4 (or 8x8) Hadamard transform to each sub-block
 * 3. Sum absolute values of transformed differences
 *
 * SATD provides better rate-distortion correlation than SAD because
 * the Hadamard transform approximates the DCT energy compaction.
 *
 * Widely used in H.264/HEVC reference software for mode decision.
 *
 * @param orig     Current block (row-major)
 * @param o_stride Row stride of current block
 * @param ref      Reference block (row-major)
 * @param r_stride Row stride of reference block
 * @param bw       Block width (must be multiple of 4)
 * @param bh       Block height (must be multiple of 4)
 * @return SATD value
 */
uint32_t compute_satd(const uint8_t *orig, uint32_t o_stride,
                      const uint8_t *ref, uint32_t r_stride,
                      uint32_t bw, uint32_t bh);

/* ==========================================================================
 * L5: Algorithms — Motion Vector Cost
 * ========================================================================== */

/**
 * Compute motion vector coding cost (rate contribution).
 *
 * Uses Exp-Golomb code length approximation for MVD coding.
 * MVD bits ~= 2 * floor(log2(|mv| + 1)) + 3 (approximation of ue(v) coding)
 *
 * @param mv_x   Motion vector x component (quarter-pel)
 * @param mv_y   Motion vector y component (quarter-pel)
 * @param pmv_x  Predicted motion vector x (median of neighbors)
 * @param pmv_y  Predicted motion vector y
 * @return Estimated number of bits to code this MV
 */
uint32_t mv_cost_bits(int16_t mv_x, int16_t mv_y, int16_t pmv_x, int16_t pmv_y);

/* ==========================================================================
 * L5: Algorithms — Search Patterns
 * ========================================================================== */

/**
 * Full Search (exhaustive) block matching.
 *
 * Examines every integer-pel position within the search range.
 * Guarantees finding the global minimum SAD.
 *
 * Complexity: O(range_w * range_h * block_w * block_h)
 *
 * @param cur        Current frame (luma plane)
 * @param ref        Reference frame (luma plane)
 * @param cur_w      Current frame width
 * @param cur_h      Current frame height
 * @param bx         Block top-left x in current frame
 * @param by         Block top-left y in current frame
 * @param bw         Block width
 * @param bh         Block height
 * @param range      Search range
 * @param result     Output: best motion vector and cost
 */
void me_full_search(const uint8_t *cur, const uint8_t *ref,
                    uint32_t cur_w, uint32_t cur_h,
                    uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                    const me_search_range_t *range,
                    me_result_t *result);

/**
 * Diamond Search (DS) — fast block matching.
 *
 * Uses a diamond-shaped search pattern (large diamond LDSP + small diamond SDSP).
 * Much faster than full search while finding near-optimal results for most
 * natural video sequences.
 *
 * Algorithm:
 *   1. Start at (0,0) or predicted MV
 *   2. Check 9 points in LDSP pattern (distance 2)
 *   3. Move center to best point, repeat until center is best
 *   4. Switch to SDSP pattern (distance 1) for final refinement
 *
 * Complexity: ~O(sqrt(range_w * range_h) * block_w * block_h)
 *
 * Reference: Zhu & Ma (2000)
 */
void me_diamond_search(const uint8_t *cur, const uint8_t *ref,
                       uint32_t cur_w, uint32_t cur_h,
                       uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                       const me_search_range_t *range,
                       me_result_t *result);

/**
 * Hexagon Search (HS) — fast block matching.
 *
 * Uses a hexagon-shaped pattern with 7 check points.
 * Generally faster than diamond search for large motion.
 *
 * Algorithm:
 *   1. Start at predicted MV
 *   2. Check 7 points in hexagonal pattern
 *   3. Move center to best, repeat
 *   4. Final refinement with small diamond
 */
void me_hexagon_search(const uint8_t *cur, const uint8_t *ref,
                       uint32_t cur_w, uint32_t cur_h,
                       uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                       const me_search_range_t *range,
                       me_result_t *result);

/**
 * Sub-pixel refinement — refine integer-pel MV to quarter-pel precision.
 *
 * After finding the best integer-pel position, this function searches
 * half-pel and quarter-pel positions around it using H.264 interpolation.
 *
 * @param cur        Current frame
 * @param ref        Reference frame
 * @param result     Input: integer-pel MV. Output: refined sub-pel MV and cost
 * @param subpel     Refinement level: 1=half, 2=quarter
 */
void me_subpel_refine(const uint8_t *cur, const uint8_t *ref,
                      uint32_t cur_w, uint32_t cur_h,
                      uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                      me_result_t *result, uint32_t subpel);

/* ==========================================================================
 * L6: Canonical Problems — Motion Compensation
 * ========================================================================== */

/**
 * Motion-compensated prediction.
 *
 * Given a reference frame and motion vectors, generate the predicted block.
 * Handles sub-pixel interpolation automatically.
 *
 * @param ref_frame    Reference frame to fetch from
 * @param mv           Motion vector (quarter-pel units)
 * @param bx           Top-left x of block in current frame
 * @param by           Top-left y of block in current frame
 * @param bw           Block width
 * @param bh           Block height
 * @param pred_out     Output: predicted pixel block
 * @return 0 on success, -1 if prediction extends outside reference frame
 */
int motion_compensate(const video_frame_t *ref_frame,
                      motion_vector_t mv,
                      uint32_t bx, uint32_t by,
                      uint32_t bw, uint32_t bh,
                      uint8_t *pred_out);

/**
 * Bi-directional motion compensation.
 *
 * Averages predictions from two reference frames (typically one past,
 * one future). Each prediction is weighted equally (0.5 each).
 *
 * @param ref0        First reference frame
 * @param mv0         Motion vector for ref0
 * @param ref1        Second reference frame
 * @param mv1         Motion vector for ref1
 * @param bx, by, bw, bh  Block position and size
 * @param pred_out    Output: averaged prediction
 * @return 0 on success
 */
int motion_compensate_bipred(const video_frame_t *ref0, motion_vector_t mv0,
                             const video_frame_t *ref1, motion_vector_t mv1,
                             uint32_t bx, uint32_t by,
                             uint32_t bw, uint32_t bh,
                             uint8_t *pred_out);

/**
 * Compute residual between original and predicted block.
 *
 * residual[i][j] = orig[i][j] - pred[i][j]
 *
 * @param orig        Original pixel block
 * @param pred        Predicted pixel block
 * @param residual    Output: residual (signed 16-bit to handle range)
 * @param bw, bh      Block dimensions
 */
void compute_residual(const uint8_t *orig, const uint8_t *pred,
                      int16_t *residual, uint32_t bw, uint32_t bh);

/**
 * Reconstruct block from prediction and residual.
 *
 * recon[i][j] = clip(pred[i][j] + residual[i][j], 0, 255)
 *
 * @param pred        Motion-compensated prediction
 * @param residual    Decoded residual (after inverse transform)
 * @param recon       Output: reconstructed block
 * @param bw, bh      Block dimensions
 */
void reconstruct_block(const uint8_t *pred, const int16_t *residual,
                       uint8_t *recon, uint32_t bw, uint32_t bh);

/* ==========================================================================
 * L6: Canonical Problems — Motion Vector Prediction
 * ========================================================================== */

/**
 * Compute median motion vector predictor (MVP) from neighboring blocks.
 *
 * H.264 uses the median of MVs from left, top, and top-right (or top-left)
 * neighboring blocks. This predictor is used for differential MV coding.
 *
 * @param mv_a    MV of block A (left neighbor)
 * @param mv_b    MV of block B (top neighbor)
 * @param mv_c    MV of block C (top-right or top-left neighbor)
 * @param pred    Output: predicted motion vector
 */
void compute_mvp(const motion_vector_t *mv_a,
                 const motion_vector_t *mv_b,
                 const motion_vector_t *mv_c,
                 motion_vector_t *pred);

/* ==========================================================================
 * L5: Algorithms — Block Matching with Distortion Metrics
 * ========================================================================== */

/**
 * Compare two motion estimation results and return the index of the better one.
 *
 * Compares using RD cost: J = SAD + lambda_motion * MV_bits
 *
 * @param a        First candidate
 * @param b        Second candidate
 * @param lambda   Lagrangian multiplier for motion
 * @return 0 if 'a' is better, 1 if 'b' is better
 */
int me_compare_rd(const me_result_t *a, const me_result_t *b, double lambda);

/**
 * Initialize motion estimation configuration with sensible defaults.
 */
void me_config_init(me_config_t *config, uint32_t search_range_x,
                    uint32_t search_range_y);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_EST_H */

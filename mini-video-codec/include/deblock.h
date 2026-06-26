/**
 * deblock.h — Deblocking Filter for Video Coding
 *
 * L1: Boundary strength, filter parameters, edge types
 * L2: Blocking artifact reduction, in-loop filtering concept
 * L5: H.264 adaptive deblocking filter algorithm
 *
 * The deblocking filter (also called loop filter) is applied within the
 * coding loop to reduce blocking artifacts caused by block-based transform
 * and motion compensation. It is normative in H.264 (i.e., the decoder
 * must apply it identically) because it significantly improves both
 * objective and subjective quality.
 *
 * Reference:
 *   ITU-T H.264, Section 8.7 — Deblocking Filter Process
 *   List et al., "Adaptive Deblocking Filter", IEEE Trans. CSVT, 2003
 *   Norkin et al., "HEVC Deblocking Filter", IEEE Trans. CSVT, 2012
 *
 * Course Mapping:
 *   Stanford EE392J — Video Coding (post-processing)
 *   TU Munich — Video Coding
 */

#ifndef DEBLOCK_H
#define DEBLOCK_H

#include "video_codec.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Boundary Strength and Filter Parameters
 * ========================================================================== */

/** Boundary strength (Bs) — determines filter strength at each edge */
typedef enum {
    BS_NONE   = 0,  /**< No filtering needed */
    BS_WEAK   = 1,  /**< Weak filtering (different refs or MVs, same ref frame) */
    BS_MEDIUM = 2,  /**< Medium filtering (one block is intra) */
    BS_STRONG = 3,  /**< Strong filtering (edge is macroblock boundary, or
                          at least one block is intra with non-zero coeffs) */
    BS_STRONGEST = 4, /**< Strongest (both blocks are intra with coeffs, or
                            MB boundary with one intra) */
} boundary_strength_t;

/** Deblocking filter parameters (from slice header) */
typedef struct {
    int32_t alpha_c0_offset;   /**< Alpha offset (-6 to +6) */
    int32_t beta_offset;       /**< Beta offset (-6 to +6) */
    uint8_t disable_idc;       /**< 0=enable everywhere, 1=disable, 2=disable except MB boundaries */
} deblock_params_t;

/** Edge direction */
typedef enum {
    EDGE_VERTICAL   = 0,  /**< Vertical edge (filter columns across edge) */
    EDGE_HORIZONTAL = 1,  /**< Horizontal edge (filter rows across edge) */
} edge_direction_t;

/** Filter sample context — 8 samples across an edge */
typedef struct {
    uint8_t p3, p2, p1, p0;  /**< Left/top side samples (p0 closest to edge) */
    uint8_t q0, q1, q2, q3;  /**< Right/bottom side samples (q0 closest to edge) */
} filter_samples_t;

/** Per-edge filter state */
typedef struct {
    boundary_strength_t bs;      /**< Boundary strength */
    filter_samples_t samples;    /**< Input samples across the edge */
    filter_samples_t filtered;   /**< Output filtered samples */
    uint32_t qp_avg;             /**< Average QP of the two blocks */
    uint8_t  filter_applied;     /**< 1 = filter was applied */
    uint8_t  filter_strength;    /**< 0=none, 1=weak, 2=strong */
} edge_filter_state_t;

/* ==========================================================================
 * L5: Algorithms — H.264 Adaptive Deblocking
 * ========================================================================== */

/**
 * Compute boundary strength (Bs) between two adjacent 4x4 blocks.
 *
 * Rules (H.264 Section 8.7.2.2, simplified):
 *   - If either block is intra and on MB boundary: Bs = 4
 *   - If either block is intra: Bs = 3
 *   - If one block has coded residuals (non-zero coeffs): Bs = 2
 *   - If different reference frames or MV difference >= 1: Bs = 1
 *   - Otherwise: Bs = 0 (no filtering)
 *
 * @param is_mb_boundary  1 if this is a macroblock boundary
 * @param intra_a         1 if block A is intra-coded
 * @param intra_b         1 if block B is intra-coded
 * @param has_coeff_a     1 if block A has non-zero coefficients
 * @param has_coeff_b     1 if block B has non-zero coefficients
 * @param ref_a           Reference index of block A
 * @param ref_b           Reference index of block B
 * @param mv_diff_x       Absolute MV x difference in quarter-pel
 * @param mv_diff_y       Absolute MV y difference in quarter-pel
 * @return Boundary strength (0-4)
 */
boundary_strength_t deblock_compute_bs(int is_mb_boundary,
                                       int intra_a, int intra_b,
                                       int has_coeff_a, int has_coeff_b,
                                       int ref_a, int ref_b,
                                       int mv_diff_x, int mv_diff_y);

/**
 * Compute alpha and beta thresholds from average QP and offsets.
 *
 * alpha = clip3(0, 255, alpha_table[QP_avg + alpha_offset])
 * beta  = clip3(0, 255, beta_table[QP_avg + beta_offset])
 *
 * where QP_avg = (QP_a + QP_b + 1) >> 1
 *
 * Reference: ITU-T H.264, Table 8-16 and Table 8-17
 *
 * @param qp_avg     Average QP of two blocks
 * @param alpha_off  Alpha offset from slice header
 * @param beta_off   Beta offset from slice header
 * @param alpha_out  Output: alpha threshold
 * @param beta_out   Output: beta threshold
 */
void deblock_thresholds(uint32_t qp_avg, int32_t alpha_off, int32_t beta_off,
                        uint32_t *alpha_out, uint32_t *beta_out);

/**
 * Determine if deblocking should be applied to an edge.
 *
 * Conditions for filtering (H.264 Section 8.7.2.3):
 *   1. Bs > 0
 *   2. |p0 - q0| < alpha
 *   3. |p1 - p0| < beta
 *   4. |q1 - q0| < beta
 *
 * @param samples  Samples across the edge (p3..p0, q0..q3)
 * @param alpha    Alpha threshold
 * @param beta     Beta threshold
 * @param bs       Boundary strength
 * @return 1 if filtering should be applied, 0 otherwise
 */
int deblock_should_filter(const filter_samples_t *samples,
                          uint32_t alpha, uint32_t beta,
                          boundary_strength_t bs);

/**
 * Apply H.264 deblocking filter to a single edge.
 *
 * The filter modifies p0, p1, p2 (if strong) and q0, q1, q2 (if strong).
 *
 * Weak filter (H.264 Sec 8.7.2.3):
 *   - p0' = p0 + clip3(-C0, C0, (((q0-p0)<<2) + (p1-q1) + 4) >> 3)
 *   - p1' = p1 + clip3(-C0, C0, ((p2 + ((p0+q0+1)>>1) - (p1<<1)) >> 1))
 *
 * Strong filter (Bs >= 4, or for specific conditions with Bs=3):
 *   - p0' = (p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3
 *   - p1' = (p2 + p1 + p0 + q0 + 2) >> 2
 *   - p2' = (2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3
 *
 * The clip range C0 is determined by the table index (alpha,beta) and Bs.
 *
 * Reference: ITU-T H.264, Section 8.7.2.3
 *
 * @param state    Input: samples, bs, qp_avg. Output: filtered samples.
 * @param alpha    Alpha threshold
 * @param beta     Beta threshold
 */
void deblock_filter_edge(edge_filter_state_t *state,
                         uint32_t alpha, uint32_t beta);

/**
 * Apply deblocking filter to all 4x4 block edges in a macroblock.
 *
 * H.264 applies deblocking in raster scan order:
 *   1. Filter all vertical edges in luma (left to right)
 *   2. Filter all horizontal edges in luma (top to bottom)
 *   3. Filter vertical edges in chroma
 *   4. Filter horizontal edges in chroma
 *
 * @param frame_luma      Frame luma plane
 * @param frame_cb        Frame Cb plane
 * @param frame_cr        Frame Cr plane
 * @param stride_luma     Luma plane stride
 * @param stride_chroma   Chroma plane stride
 * @param mb_x            Macroblock x index (0..mb_width-1)
 * @param mb_y            Macroblock y index (0..mb_height-1)
 * @param qp              Average QP for this macroblock
 * @param params          Deblocking filter parameters
 * @param bs_horiz        Boundary strengths for horizontal edges (4x4 grid)
 * @param bs_vert         Boundary strengths for vertical edges (4x4 grid)
 */
void deblock_filter_mb(uint8_t *frame_luma, uint8_t *frame_cb, uint8_t *frame_cr,
                       uint32_t stride_luma, uint32_t stride_chroma,
                       uint32_t mb_x, uint32_t mb_y,
                       uint32_t qp, const deblock_params_t *params,
                       uint8_t bs_horiz[5][4],
                       uint8_t bs_vert[4][5]);

/**
 * Apply deblocking filter to an entire frame.
 *
 * Iterates over all macroblocks and applies edge filtering.
 *
 * @param frame    Frame to filter (all three planes modified in-place)
 * @param qp       Frame-level QP
 * @param params   Deblocking parameters
 */
void deblock_filter_frame(video_frame_t *frame, uint32_t qp,
                          const deblock_params_t *params);

/**
 * Initialize deblocking filter parameters with H.264 defaults.
 *
 * @param params   Output parameters
 */
void deblock_params_init(deblock_params_t *params);

/**
 * Get filter clip value C0 from table index and Bs.
 *
 * H.264 Table 8-18: c0 = clip_index_table[index_A][Bs-1]
 * where index_A = clip3(0, 2, (qp_avg + alpha_offset))
 */
int32_t deblock_c0(uint32_t index_a, boundary_strength_t bs);

#ifdef __cplusplus
}
#endif

#endif /* DEBLOCK_H */

/**
 * prediction.h — Intra and Inter Prediction for Video Coding
 *
 * L1: Intra prediction mode definitions (4x4, 8x8, 16x16)
 * L2: Spatial prediction, inter prediction, reference management
 * L5: Intra mode decision, prediction generation algorithms
 * L6: H.264 intra/inter prediction pipeline
 *
 * Prediction is the core of hybrid video coding. Intra prediction exploits
 * spatial redundancy within a frame; inter prediction exploits temporal
 * redundancy between frames. Together they remove the vast majority of
 * redundant information before transform coding.
 *
 * Reference:
 *   ITU-T H.264, Section 8.3 — Intra Prediction
 *   Wiegand et al., "Overview of the H.264/AVC Video Coding Standard" (2003)
 *   Richardson, "The H.264 Advanced Video Compression Standard" (2010)
 *
 * Course Mapping:
 *   MIT 6.344 — Digital Image Processing
 *   Stanford EE392J — Video Compression
 *   TU Munich — Image and Video Coding
 */

#ifndef PREDICTION_H
#define PREDICTION_H

#include "video_codec.h"
#include "motion_est.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Intra Prediction Modes
 * ========================================================================== */

/** H.264 4x4 luma intra prediction modes (9 modes total) */
typedef enum {
    INTRA_4x4_VERTICAL     = 0,  /**< Vertical: top pixels replicated down */
    INTRA_4x4_HORIZONTAL   = 1,  /**< Horizontal: left pixels replicated right */
    INTRA_4x4_DC           = 2,  /**< DC: mean of top and left pixels */
    INTRA_4x4_DIAG_DL      = 3,  /**< Diagonal Down-Left (45 degrees) */
    INTRA_4x4_DIAG_DR      = 4,  /**< Diagonal Down-Right (45 degrees) */
    INTRA_4x4_VERT_RIGHT   = 5,  /**< Vertical-Right (26.6 degrees) */
    INTRA_4x4_HORIZ_DOWN   = 6,  /**< Horizontal-Down (26.6 degrees) */
    INTRA_4x4_VERT_LEFT    = 7,  /**< Vertical-Left (26.6 degrees) */
    INTRA_4x4_HORIZ_UP     = 8,  /**< Horizontal-Up (26.6 degrees) */
} intra_4x4_mode_t;

/** H.264 16x16 luma intra prediction modes (4 modes) */
typedef enum {
    INTRA_16x16_VERTICAL   = 0,  /**< Vertical */
    INTRA_16x16_HORIZONTAL = 1,  /**< Horizontal */
    INTRA_16x16_DC         = 2,  /**< DC: mean of 16 top + 16 left pixels */
    INTRA_16x16_PLANE      = 3,  /**< Plane: bilinear plane fit */
} intra_16x16_mode_t;

/** H.264 8x8 chroma intra prediction modes */
typedef enum {
    INTRA_CHROMA_DC        = 0,  /**< DC with chroma-specific averaging */
    INTRA_CHROMA_HORIZONTAL = 1, /**< Horizontal */
    INTRA_CHROMA_VERTICAL   = 2, /**< Vertical */
    INTRA_CHROMA_PLANE      = 3, /**< Plane */
} intra_chroma_mode_t;

/** Intra prediction neighbor pixel availability flags */
typedef struct {
    uint8_t top_available;       /**< Pixels above current block are available */
    uint8_t left_available;      /**< Pixels left of current block are available */
    uint8_t top_left_available;  /**< Top-left pixel is available */
    uint8_t top_right_available; /**< Top-right pixels are available */
} intra_avail_t;

/** Intra prediction context for mode generation */
typedef struct {
    uint8_t top[33];      /**< Reference pixels above (max 16+1 for 4x4) */
    uint8_t left[33];     /**< Reference pixels to the left */
    uint8_t top_left;     /**< Top-left corner reference pixel */
    uint32_t block_size;  /**< Block size (4, 8, or 16) */
    intra_avail_t avail;  /**< Availability flags */
} intra_context_t;

/* ==========================================================================
 * L2: Core Concepts — Intra Prediction Generation
 * ========================================================================== */

/**
 * Set up intra prediction context from neighboring reconstructed pixels.
 *
 * Copies edge pixels from the already-reconstructed frame region above
 * and to the left of the current block position.
 *
 * @param ctx         Context to initialize
 * @param recon       Reconstructed frame (luma plane)
 * @param recon_stride Row stride of reconstructed frame
 * @param x           Block top-left x coordinate
 * @param y           Block top-left y coordinate
 * @param block_size  Block dimension (4, 8, or 16)
 * @param frame_w     Frame width
 * @param frame_h     Frame height
 */
void intra_context_init(intra_context_t *ctx,
                        const uint8_t *recon, uint32_t recon_stride,
                        int32_t x, int32_t y, uint32_t block_size,
                        uint32_t frame_w, uint32_t frame_h);

/**
 * Generate intra 4x4 prediction block.
 *
 * Each mode defines a specific directional or DC interpolation from
 * the 13 reference pixels (4 left + 4 top + 1 top-left + 4 top-right).
 *
 * Reference: ITU-T H.264, Section 8.3.1.2.1
 *
 * @param ctx      Intra prediction context
 * @param mode     Prediction mode (0-8)
 * @param pred     Output: 4x4 predicted pixel block (row-major, 16 bytes)
 */
void intra_pred_4x4(const intra_context_t *ctx, intra_4x4_mode_t mode,
                    uint8_t *pred);

/**
 * Generate intra 16x16 prediction block.
 *
 * 4 modes: Vertical, Horizontal, DC, and Plane.
 * Plane mode performs a linear fit to the reference pixels:
 *   P[x][y] = clip((a + b*(x-7) + c*(y-7) + 16) >> 5)
 *   where a = 16*(p[-1][15] + p[15][-1])
 *         b = (5*H + 32) >> 6   (horizontal gradient)
 *         c = (5*V + 32) >> 6   (vertical gradient)
 *
 * Reference: ITU-T H.264, Section 8.3.3
 */
void intra_pred_16x16(const intra_context_t *ctx, intra_16x16_mode_t mode,
                      uint8_t *pred);

/**
 * Generate intra 8x8 chroma prediction block.
 *
 * Uses the same 4 modes as 16x16 luma but applied to 8x8 chroma blocks.
 *
 * Reference: ITU-T H.264, Section 8.3.4
 */
void intra_pred_chroma_8x8(const intra_context_t *ctx,
                           intra_chroma_mode_t mode, uint8_t *pred);

/**
 * Generic intra prediction dispatcher.
 * Selects the correct function based on block size and prediction mode.
 *
 * @param block_size  4, 8, or 16
 * @param mode        Prediction mode (interpreted per block size)
 * @param ctx         Prediction context
 * @param pred        Output prediction block (block_size * block_size bytes)
 * @return 0 on success, -1 on invalid parameters
 */
int intra_pred_generate(uint32_t block_size, uint32_t mode,
                        const intra_context_t *ctx, uint8_t *pred);

/* ==========================================================================
 * L5: Algorithms — Intra Mode Decision (SAD/SATD-based)
 * ========================================================================== */

/**
 * Select the best intra 4x4 prediction mode using minimum SAD criterion.
 *
 * Tests all 9 modes against the original block and returns the one
 * with the lowest SAD (or SATD if use_satd is set).
 *
 * Complexity: O(9 * 16) = O(144) operations
 *
 * @param orig      Original 4x4 pixel block
 * @param orig_stride Row stride of original frame
 * @param ctx       Intra prediction context
 * @param use_satd  1=SATD, 0=SAD for cost metric
 * @param best_mode Output: best mode index (0-8)
 * @param best_cost Output: cost of best mode
 */
void intra_mode_decision_4x4(const uint8_t *orig, uint32_t orig_stride,
                             const intra_context_t *ctx,
                             int use_satd,
                             intra_4x4_mode_t *best_mode,
                             uint32_t *best_cost);

/**
 * Select the best intra 16x16 prediction mode.
 * Tests all 4 modes.
 */
void intra_mode_decision_16x16(const uint8_t *orig, uint32_t orig_stride,
                               const intra_context_t *ctx,
                               intra_16x16_mode_t *best_mode,
                               uint32_t *best_cost);

/**
 * Most Probable Mode (MPM) for 4x4 intra prediction.
 *
 * If the left and top neighboring 4x4 blocks both use intra prediction,
 * MPM = min(mode_left, mode_top).
 * Otherwise, MPM = DC (2).
 *
 * Only the difference from MPM is signaled to save bits.
 *
 * Reference: ITU-T H.264, Section 8.3.1.1
 */
intra_4x4_mode_t intra_get_mpm_4x4(intra_4x4_mode_t mode_left,
                                   intra_4x4_mode_t mode_top,
                                   int left_avail, int top_avail);

/* ==========================================================================
 * L2: Core Concepts — Inter Prediction Framework
 * ========================================================================== */

/**
 * Generate inter prediction (wrapper around motion compensation).
 *
 * Handles single-list (L0 or L1) and bi-predictive cases.
 *
 * @param ref_list    Reference frame list
 * @param ref_idx     Reference frame index in the list (0 = list0[0])
 * @param mv          Motion vector (quarter-pel units)
 * @param bx, by      Block position in current frame
 * @param bw, bh      Block dimensions
 * @param pred        Output prediction block
 * @return 0 on success
 */
int inter_pred_generate(const ref_frame_list_t *ref_list,
                        uint32_t ref_idx,
                        motion_vector_t mv,
                        uint32_t bx, uint32_t by,
                        uint32_t bw, uint32_t bh,
                        uint8_t *pred);

/**
 * Generate bi-predictive inter prediction.
 *
 * pred[i] = (pred0[i] + pred1[i] + 1) >> 1  (averaging with rounding)
 */
int inter_pred_bipred(const ref_frame_list_t *list0, uint32_t ref0,
                      motion_vector_t mv0,
                      const ref_frame_list_t *list1, uint32_t ref1,
                      motion_vector_t mv1,
                      uint32_t bx, uint32_t by,
                      uint32_t bw, uint32_t bh,
                      uint8_t *pred);

/**
 * Compute Skip mode SAD (16x16 block).
 *
 * Skip mode: no motion vector difference, no residual.
 * Reconstructed block = motion compensated prediction directly.
 *
 * @return SAD between original and skip prediction; 0 means perfect match
 */
uint32_t inter_skip_sad(const uint8_t *orig, uint32_t orig_stride,
                        const uint8_t *pred, uint32_t pred_stride,
                        uint32_t bw, uint32_t bh);

/* ==========================================================================
 * L5: Algorithms — Reference Frame Management
 * ========================================================================== */

/**
 * Determine if a motion vector points within the valid frame area.
 * Returns 1 if valid, 0 if out of bounds.
 */
int mv_in_bounds(int32_t bx, int32_t by, int32_t bw, int32_t bh,
                 motion_vector_t mv, uint32_t frame_w, uint32_t frame_h);

/**
 * Scale a motion vector for temporal direct mode.
 *
 * In B-frames, temporal direct mode derives MVs from a co-located
 * block in the first List1 reference frame. The MV is scaled by
 * the temporal distance ratio.
 *
 * MV_L0 = (tb / td) * col_MV
 * MV_L1 = ((tb - td) / td) * col_MV
 *
 * where tb = POC distance from current to L0 reference
 *       td = POC distance from L1 reference to L0 reference
 *
 * Reference: ITU-T H.264, Section 8.4.1.2.3
 */
void mv_scale_temporal(const motion_vector_t *col_mv,
                       int32_t tb, int32_t td,
                       motion_vector_t *mv_l0, motion_vector_t *mv_l1);

#ifdef __cplusplus
}
#endif

#endif /* PREDICTION_H */

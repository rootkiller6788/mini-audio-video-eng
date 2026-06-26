#ifndef HFR_MOTION_H
#define HFR_MOTION_H

#include "hfr_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ?? L1: Motion Estimation Definitions ???????????????????????????????? */

#define HFR_MOTION_MAX_VECTORS 4096
#define HFR_MOTION_SEARCH_RANGE_DEFAULT 16
#define HFR_MOTION_BLOCK_SIZE_DEFAULT 16

typedef struct {
    double dx;
    double dy;
    double confidence;
} hfr_motion_vector_t;

typedef struct {
    hfr_motion_vector_t *vectors;
    int num_vectors;
    int capacity;
    int width_blocks;
    int height_blocks;
    int block_size;
    double max_displacement;
    double avg_displacement;
    double *occlusion_map;
} hfr_motion_field_t;

typedef struct {
    int block_size;
    int search_range;
    int use_hierarchical;
    int pyramid_levels;
    double regularization;
    double smoothness_weight;
    int iterations;
    int use_color;
} hfr_motion_est_config_t;

typedef enum {
    HFR_ME_BLOCK_MATCH_SAD = 0,
    HFR_ME_BLOCK_MATCH_SSD,
    HFR_ME_BLOCK_MATCH_NCC,
    HFR_ME_BLOCK_MATCH_CENSUS,
    HFR_ME_OPTICAL_FLOW_HORN_SCHUNCK,
    HFR_ME_OPTICAL_FLOW_LUCAS_KANADE,
    HFR_ME_OPTICAL_FLOW_FARNEBACK,
    HFR_ME_OPTICAL_FLOW_TVL1,
    HFR_ME_PHASE_CORRELATION,
    HFR_ME_COUNT
} hfr_me_algorithm_t;

/* ?? L2: Motion Models ???????????????????????????????????????????????? */

typedef enum {
    HFR_MOTION_MODEL_TRANSLATION = 0,
    HFR_MOTION_MODEL_AFFINE,
    HFR_MOTION_MODEL_PERSPECTIVE,
    HFR_MOTION_MODEL_QUADRATIC,
    HFR_MOTION_MODEL_COUNT
} hfr_motion_model_t;

typedef struct {
    double a00, a01, a02;
    double a10, a11, a12;
} hfr_affine_transform_t;

typedef struct {
    double h00, h01, h02;
    double h10, h11, h12;
    double h20, h21, h22;
} hfr_perspective_transform_t;

/* ?? API: Block Matching (Exhaustive Search) ?????????????????????????? */

void hfr_me_config_init(hfr_motion_est_config_t *config);

hfr_motion_field_t *hfr_me_block_match_exhaustive(
    const double *prev_frame, const double *curr_frame,
    int width, int height,
    const hfr_motion_est_config_t *config);

hfr_motion_field_t *hfr_me_block_match_diamond(
    const double *prev_frame, const double *curr_frame,
    int width, int height,
    const hfr_motion_est_config_t *config);

hfr_motion_field_t *hfr_me_block_match_hexagon(
    const double *prev_frame, const double *curr_frame,
    int width, int height,
    const hfr_motion_est_config_t *config);

void hfr_motion_field_free(hfr_motion_field_t *field);

/* ?? API: Optical Flow ???????????????????????????????????????????????? */

/**
 * @brief Horn-Schunck optical flow (global smoothness constraint).
 *
 * Solves the variational formulation:
 *   min ? (I_x u + I_y v + I_t)^2 + alpha^2 (|grad u|^2 + |grad v|^2) dx dy
 *
 * using Gauss-Seidel iteration.
 *
 * @param prev_frame  Previous frame (grayscale, W*H).
 * @param curr_frame  Current frame (grayscale, W*H).
 * @param width       Frame width.
 * @param height      Frame height.
 * @param alpha       Regularization parameter (smoothness weight).
 * @param iterations  Number of Gauss-Seidel iterations.
 * @param u           Output horizontal flow (W*H, caller-allocated).
 * @param v           Output vertical flow (W*H, caller-allocated).
 */
void hfr_optical_flow_horn_schunck(
    const double *prev_frame, const double *curr_frame,
    int width, int height,
    double alpha, int iterations,
    double *u, double *v);

/**
 * @brief Lucas-Kanade optical flow (local window method).
 *
 * Assumes constant flow within a small window. Solves A^T A d = A^T b
 * using least squares for each pixel.
 *
 * @param prev_frame  Previous frame.
 * @param curr_frame  Current frame.
 * @param width       Frame width.
 * @param height      Frame height.
 * @param window_size Window size (e.g., 5 for 5x5).
 * @param u           Output horizontal flow (W*H).
 * @param v           Output vertical flow (W*H).
 */
void hfr_optical_flow_lucas_kanade(
    const double *prev_frame, const double *curr_frame,
    int width, int height, int window_size,
    double *u, double *v);

/**
 * @brief Compute temporal derivatives I_t for optical flow.
 *
 * I_t = I(x, y, t+1) - I(x, y, t)
 * Central difference with optional temporal smoothing.
 *
 * @param prev_frame  Frame at t.
 * @param curr_frame  Frame at t+1.
 * @param width       Frame width.
 * @param height      Frame height.
 * @param dt          Output temporal derivative (W*H).
 */
void hfr_temporal_derivative(const double *prev_frame, const double *curr_frame,
                             int width, int height, double *dt);

/**
 * @brief Compute spatial derivatives I_x, I_y using Sobel operator.
 *
 * @param frame   Input frame (W*H).
 * @param width   Frame width.
 * @param height  Frame height.
 * @param dx      Output x-derivative (W*H).
 * @param dy      Output y-derivative (W*H).
 */
void hfr_spatial_derivatives(const double *frame, int width, int height,
                             double *dx, double *dy);

/* ?? API: Motion Compensation (for frame interpolation) ??????????????? */

/**
 * @brief Motion-compensated frame interpolation (MCFI).
 *
 * Given two frames and a motion field, generates an intermediate frame
 * at temporal position t (0=prev, 1=next).
 *
 * This is the core algorithm for HFR frame rate upconversion:
 *
 * 1. Warp prev_frame forward by t * MV, warp next_frame backward by (1-t) * MV
 * 2. Blend the two warped frames with occlusion handling
 * 3. Fill holes with inpainting
 *
 * @param prev_frame  Previous frame (W*H).
 * @param curr_frame  Current (next) frame (W*H).
 * @param width       Frame width.
 * @param height      Frame height.
 * @param field       Motion vector field (block-based).
 * @param t           Temporal interpolation position [0,1].
 * @param output      Output interpolated frame (W*H, caller-allocated).
 */
void hfr_mcfi_interpolate(const double *prev_frame, const double *curr_frame,
                          int width, int height,
                          const hfr_motion_field_t *field,
                          double t, double *output);

/**
 * @brief Warp a frame according to a motion vector field.
 *
 * Backward warp using bilinear interpolation for sub-pixel precision.
 *
 * @param src        Source frame (W*H).
 * @param width      Frame width.
 * @param height     Frame height.
 * @param field      Motion field (dx, dy per block).
 * @param t          Temporal scaling factor for vectors.
 * @param dst        Destination warped frame (W*H).
 */
void hfr_motion_warp_frame(const double *src, int width, int height,
                           const hfr_motion_field_t *field,
                           double t, double *dst);

/**
 * @brief Detect occlusion regions from a motion field.
 *
 * Occlusion occurs where multiple source pixels map to the same
 * destination (covered) or where no source pixels map (uncovered).
 *
 * @param field      Motion field.
 * @param occlusion  Output occlusion map (width_blocks * height_blocks, 0-1).
 */
void hfr_detect_occlusion(const hfr_motion_field_t *field, double *occlusion);

/**
 * @brief Fill holes (uncovered regions) in a warped frame.
 *
 * Uses simple spatial inpainting (average of neighboring valid pixels).
 *
 * @param frame     Frame to inpaint (W*H, modified in place).
 * @param mask      Valid pixel mask (W*H, 1=valid, 0=hole).
 * @param width     Frame width.
 * @param height    Frame height.
 * @param radius    Inpainting radius (iterations).
 */
void hfr_inpaint_holes(double *frame, const double *mask,
                       int width, int height, int radius);

/* ?? API: Phase Correlation ??????????????????????????????????????????? */

/**
 * @brief Phase correlation for global motion estimation.
 *
 * Computes the cross-power spectrum of two images to find the
 * translation that maximizes correlation. Uses FFT-based approach.
 *
 *   delta = argmax F^{-1} { F{I1} * conj(F{I2}) / |F{I1} * conj(F{I2})| }
 *
 * @param prev_frame  Previous frame (W*H).
 * @param curr_frame  Current frame (W*H).
 * @param width       Frame width (must be power of 2).
 * @param height      Frame height (must be power of 2).
 * @param dx          Output: global x displacement.
 * @param dy          Output: global y displacement.
 * @param confidence  Output: correlation peak confidence [0,1].
 * @return            0 on success.
 */
int hfr_phase_correlation(const double *prev_frame, const double *curr_frame,
                          int width, int height,
                          double *dx, double *dy, double *confidence);

/* ?? API: Motion Field Analysis ???????????????????????????????????????? */

/**
 * @brief Compute magnitude map from motion field.
 *
 * @param field   Motion field.
 * @param magn    Output magnitude array (width_blocks * height_blocks).
 */
void hfr_motion_field_magnitude(const hfr_motion_field_t *field, double *magn);

/**
 * @brief Compute statistics of a motion field.
 *
 * Updates avg_displacement, max_displacement in the field struct.
 *
 * @param field  Motion field to analyze.
 */
void hfr_motion_field_statistics(hfr_motion_field_t *field);

/**
 * @brief Filter motion field to remove outliers (vector median filter).
 *
 * For each vector, replaces it with the median of its spatial neighbors
 * if it deviates too much.
 *
 * @param field     Motion field (modified in place).
 * @param threshold Maximum allowed deviation ratio.
 */
void hfr_motion_field_median_filter(hfr_motion_field_t *field, double threshold);

/**
 * @brief Compute the global motion model (affine) from a set of motion vectors.
 *
 * Uses least-squares fitting of the affine transform to the motion vectors.
 *
 * @param field     Motion field (input).
 * @param transform Output affine transform.
 * @return          Fitting residual (lower = better fit).
 */
double hfr_fit_affine_transform(const hfr_motion_field_t *field,
                                hfr_affine_transform_t *transform);

/**
 * @brief Apply an affine transform to a point.
 *
 * @param t     Affine transform.
 * @param x     Input x.
 * @param y     Input y.
 * @param out_x Output transformed x.
 * @param out_y Output transformed y.
 */
void hfr_affine_apply(const hfr_affine_transform_t *t,
                      double x, double y, double *out_x, double *out_y);

#ifdef __cplusplus
}
#endif

#endif /* HFR_MOTION_H */

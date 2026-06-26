/**
 * deblocking.c — H.264 Adaptive Deblocking Filter Implementation
 *
 * Implements boundary strength computation, threshold derivation,
 * edge filtering (weak/strong), and frame-level deblocking.
 *
 * Knowledge coverage:
 *   L1: Boundary strength, filter parameters
 *   L2: In-loop deblocking concept
 *   L5: H.264 adaptive deblocking filter algorithm
 */

#include "deblock.h"
#include "video_codec.h"
#include <string.h>

static int32_t clip3_local(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* ==========================================================================
 * L5: Boundary Strength Computation
 * ========================================================================== */

boundary_strength_t deblock_compute_bs(int is_mb_boundary,
                                       int intra_a, int intra_b,
                                       int has_coeff_a, int has_coeff_b,
                                       int ref_a, int ref_b,
                                       int mv_diff_x, int mv_diff_y)
{
    if (intra_a || intra_b) {
        if (is_mb_boundary)
            return BS_STRONGEST; /* Bs = 4 */
        return BS_STRONG;       /* Bs = 3 */
    }
    if (has_coeff_a || has_coeff_b)
        return BS_MEDIUM;       /* Bs = 2 */
    if (ref_a != ref_b || mv_diff_x >= 4 || mv_diff_y >= 4)
        return BS_WEAK;         /* Bs = 1 */
    return BS_NONE;             /* Bs = 0 */
}

/* ==========================================================================
 * L5: Alpha/Beta Threshold Tables (H.264 Table 8-16, 8-17)
 * ========================================================================== */

static const uint32_t alpha_table[52] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    4,4,5,6,7,8,9,10,12,13,15,17,20,22,25,28,
    32,36,40,45,50,56,63,71,80,90,101,113,127,144,162,182,
    203,226,255,255
};

static const uint32_t beta_table[52] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,2,2,3,3,3,3,4,4,4,6,6,7,7,8,8,
    9,9,10,10,11,11,12,12,13,13,14,14,15,15,16,16,
    17,17,18,18
};

void deblock_thresholds(uint32_t qp_avg, int32_t alpha_off, int32_t beta_off,
                        uint32_t *alpha_out, uint32_t *beta_out)
{
    int32_t idx_a = (int32_t)qp_avg + alpha_off;
    int32_t idx_b = (int32_t)qp_avg + beta_off;
    idx_a = clip3_local(idx_a, 0, 51);
    idx_b = clip3_local(idx_b, 0, 51);
    *alpha_out = alpha_table[idx_a];
    *beta_out  = beta_table[idx_b];
}

/* ==========================================================================
 * L5: Filter Decision and Edge Filtering
 * ========================================================================== */

static int abs_diff(uint8_t a, uint8_t b)
{
    int d = (int)a - (int)b;
    return d < 0 ? -d : d;
}

int deblock_should_filter(const filter_samples_t *samples,
                          uint32_t alpha, uint32_t beta,
                          boundary_strength_t bs)
{
    if (bs == BS_NONE) return 0;
    if (abs_diff(samples->p0, samples->q0) >= (int)alpha) return 0;
    if (abs_diff(samples->p1, samples->p0) >= (int)beta)  return 0;
    if (abs_diff(samples->q1, samples->q0) >= (int)beta)  return 0;
    return 1;
}

void deblock_filter_edge(edge_filter_state_t *state,
                         uint32_t alpha, uint32_t beta)
{
    if (!state || state->bs == BS_NONE) return;

    filter_samples_t *s = &state->samples;
    filter_samples_t *f = &state->filtered;
    *f = *s; /* Copy input to output initially */

    if (!deblock_should_filter(s, alpha, beta, state->bs)) {
        state->filter_applied = 0;
        state->filter_strength = 0;
        return;
    }
    state->filter_applied = 1;

    /* Determine strong vs. weak filtering */
    int strong = 0;
    if (state->bs >= BS_STRONGEST) {
        strong = 1;
    } else if (state->bs == BS_STRONG) {
        /* Additional conditions for strong filtering with Bs=3 */
        if (abs_diff(s->p2, s->p0) < (int)beta &&
            abs_diff(s->q2, s->q0) < (int)beta &&
            abs_diff(s->p0, s->q0) < ((int)alpha >> 2) + 2) {
            strong = 1;
        }
    }

    if (strong) {
        /* Strong filter (H.264 Section 8.7.2.3, Eqs 8-478 to 8-483) */
        f->p0 = (uint8_t)clip3_local(
            ((int)s->p2 + 2*(int)s->p1 + 2*(int)s->p0
             + 2*(int)s->q0 + (int)s->q1 + 4) >> 3, 0, 255);
        f->p1 = (uint8_t)clip3_local(
            ((int)s->p2 + (int)s->p1 + (int)s->p0 + (int)s->q0 + 2) >> 2,
            0, 255);
        f->p2 = (uint8_t)clip3_local(
            (2*(int)s->p3 + 3*(int)s->p2 + (int)s->p1
             + (int)s->p0 + (int)s->q0 + 4) >> 3, 0, 255);
        f->q0 = (uint8_t)clip3_local(
            ((int)s->q2 + 2*(int)s->q1 + 2*(int)s->q0
             + 2*(int)s->p0 + (int)s->p1 + 4) >> 3, 0, 255);
        f->q1 = (uint8_t)clip3_local(
            ((int)s->q2 + (int)s->q1 + (int)s->q0 + (int)s->p0 + 2) >> 2,
            0, 255);
        f->q2 = (uint8_t)clip3_local(
            (2*(int)s->q3 + 3*(int)s->q2 + (int)s->q1
             + (int)s->q0 + (int)s->p0 + 4) >> 3, 0, 255);
        state->filter_strength = 2;
    } else {
        /* Weak filter */
        int32_t c0 = deblock_c0(0, state->bs);
        int delta = clip3_local(
            (((int)s->q0 - (int)s->p0) << 2) + ((int)s->p1 - (int)s->q1) + 4,
            -c0, c0);
        if (delta != 0) {
            f->p0 = (uint8_t)clip3_local((int)s->p0 + delta, 0, 255);
            f->q0 = (uint8_t)clip3_local((int)s->q0 - delta, 0, 255);
            /* Conditionally filter p1 and q1 */
            if (abs_diff(s->p2, s->p0) < (int)beta) {
                int delta_p1 = clip3_local(
                    ((int)s->p2 + (((int)s->p0 + (int)s->q0 + 1) >> 1)
                     - ((int)s->p1 << 1)) >> 1, -c0 >> 1, c0 >> 1);
                f->p1 = (uint8_t)clip3_local((int)s->p1 + delta_p1, 0, 255);
            }
            if (abs_diff(s->q2, s->q0) < (int)beta) {
                int delta_q1 = clip3_local(
                    ((int)s->q2 + (((int)s->q0 + (int)s->p0 + 1) >> 1)
                     - ((int)s->q1 << 1)) >> 1, -c0 >> 1, c0 >> 1);
                f->q1 = (uint8_t)clip3_local((int)s->q1 + delta_q1, 0, 255);
            }
        }
        state->filter_strength = 1;
    }
}

int32_t deblock_c0(uint32_t index_a, boundary_strength_t bs)
{
    /* Simplified c0: from H.264 Table 8-18 */
    static const int32_t c0_table[4][4] = {
        {0,0,0,0}, {0,0,1,1}, {0,0,1,2}, {0,1,2,3}
    };
    uint32_t idx = (index_a > 2) ? 2 : index_a;
    uint32_t bsi = (uint32_t)bs;
    if (bsi > 3) bsi = 3;
    return c0_table[idx][bsi];
}

/* ==========================================================================
 * L5: Macroblock and Frame Deblocking
 * ========================================================================== */

/* Horizontal edge: filter rows across edge at height y */
static void filter_horiz_edge(uint8_t *plane, uint32_t stride,
                              uint32_t x_start, uint32_t y_edge,
                              uint32_t width, boundary_strength_t bs,
                              uint32_t qp_avg, uint32_t alpha, uint32_t beta)
{
    for (uint32_t x = x_start; x < x_start + width && x + 3 < stride; x++) {
        edge_filter_state_t state;
        memset(&state, 0, sizeof(state));
        state.bs = bs;
        state.qp_avg = qp_avg;
        state.samples.p3 = plane[(y_edge-3)*stride + x];
        state.samples.p2 = plane[(y_edge-2)*stride + x];
        state.samples.p1 = plane[(y_edge-1)*stride + x];
        state.samples.p0 = plane[(y_edge  )*stride + x];
        state.samples.q0 = plane[(y_edge+1)*stride + x];
        state.samples.q1 = plane[(y_edge+2)*stride + x];
        state.samples.q2 = plane[(y_edge+3)*stride + x];
        state.samples.q3 = plane[(y_edge+4)*stride + x];
        deblock_filter_edge(&state, alpha, beta);
        if (state.filter_applied) {
            plane[(y_edge-2)*stride + x] = state.filtered.p2;
            plane[(y_edge-1)*stride + x] = state.filtered.p1;
            plane[(y_edge  )*stride + x] = state.filtered.p0;
            plane[(y_edge+1)*stride + x] = state.filtered.q0;
            plane[(y_edge+2)*stride + x] = state.filtered.q1;
            plane[(y_edge+3)*stride + x] = state.filtered.q2;
        }
    }
}

/* Vertical edge: filter columns across edge at x position */
static void filter_vert_edge(uint8_t *plane, uint32_t stride,
                             uint32_t x_edge, uint32_t y_start,
                             uint32_t height, boundary_strength_t bs,
                             uint32_t qp_avg, uint32_t alpha, uint32_t beta)
{
    for (uint32_t y = y_start; y < y_start + height; y++) {
        edge_filter_state_t state;
        memset(&state, 0, sizeof(state));
        state.bs = bs;
        state.qp_avg = qp_avg;
        state.samples.p3 = plane[y*stride + (x_edge-3)];
        state.samples.p2 = plane[y*stride + (x_edge-2)];
        state.samples.p1 = plane[y*stride + (x_edge-1)];
        state.samples.p0 = plane[y*stride + (x_edge  )];
        state.samples.q0 = plane[y*stride + (x_edge+1)];
        state.samples.q1 = plane[y*stride + (x_edge+2)];
        state.samples.q2 = plane[y*stride + (x_edge+3)];
        state.samples.q3 = plane[y*stride + (x_edge+4)];
        deblock_filter_edge(&state, alpha, beta);
        if (state.filter_applied) {
            plane[y*stride + (x_edge-2)] = state.filtered.p2;
            plane[y*stride + (x_edge-1)] = state.filtered.p1;
            plane[y*stride + (x_edge  )] = state.filtered.p0;
            plane[y*stride + (x_edge+1)] = state.filtered.q0;
            plane[y*stride + (x_edge+2)] = state.filtered.q1;
            plane[y*stride + (x_edge+3)] = state.filtered.q2;
        }
    }
}

void deblock_filter_mb(uint8_t *frame_luma, uint8_t *frame_cb, uint8_t *frame_cr,
                       uint32_t stride_luma, uint32_t stride_chroma,
                       uint32_t mb_x, uint32_t mb_y,
                       uint32_t qp, const deblock_params_t *params,
                       uint8_t bs_horiz[5][4],
                       uint8_t bs_vert[4][5])
{
    if (!frame_luma || !params) return;
    uint32_t alpha, beta;
    deblock_thresholds(qp, params->alpha_c0_offset, params->beta_offset,
                       &alpha, &beta);
    uint32_t px = mb_x * 16;
    uint32_t py = mb_y * 16;
    /* Filter luma vertical edges (left-to-right within MB) */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            boundary_strength_t bs = (boundary_strength_t)bs_vert[i][j];
            filter_vert_edge(frame_luma, stride_luma, px + (uint32_t)(i*4),
                             py + (uint32_t)(j*4), 4, bs, qp, alpha, beta);
        }
    }
    /* Filter luma horizontal edges (top-to-bottom) */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            boundary_strength_t bs = (boundary_strength_t)bs_horiz[i][j];
            filter_horiz_edge(frame_luma, stride_luma, px + (uint32_t)(i*4),
                              py + (uint32_t)(j*4), 4, bs, qp, alpha, beta);
        }
    }
    /* Chroma: similar but 8x8 blocks */
    if (frame_cb) {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                boundary_strength_t bs = (boundary_strength_t)bs_vert[i*2][j*2];
                filter_vert_edge(frame_cb, stride_chroma, px/2 + (uint32_t)(i*4),
                                 py/2 + (uint32_t)(j*4), 4, bs, qp, alpha, beta);
            }
        }
    }
    if (frame_cr) {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                boundary_strength_t bs = (boundary_strength_t)bs_vert[i*2][j*2];
                filter_vert_edge(frame_cr, stride_chroma, px/2 + (uint32_t)(i*4),
                                 py/2 + (uint32_t)(j*4), 4, bs, qp, alpha, beta);
            }
        }
    }
}

void deblock_filter_frame(video_frame_t *frame, uint32_t qp,
                          const deblock_params_t *params)
{
    if (!frame || !frame->y.data) return;
    uint32_t mb_w = (frame->width  + 15) / 16;
    uint32_t mb_h = (frame->height + 15) / 16;
    for (uint32_t mb_y = 0; mb_y < mb_h; mb_y++) {
        for (uint32_t mb_x = 0; mb_x < mb_w; mb_x++) {
            /* Simplified: use uniform Bs grid */
            uint8_t bs_h[5][4] = {{0}};
            uint8_t bs_v[4][5] = {{0}};
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    bs_v[i][j] = (i == 0) ? BS_WEAK : BS_NONE;
                    bs_h[i][j] = (j == 0) ? BS_WEAK : BS_NONE;
                }
            }
            deblock_filter_mb(frame->y.data, frame->cb.data, frame->cr.data,
                              frame->y.stride,
                              frame->cb.data ? frame->cb.stride : 0,
                              mb_x, mb_y, qp, params, bs_h, bs_v);
        }
    }
}

void deblock_params_init(deblock_params_t *params)
{
    if (!params) return;
    params->alpha_c0_offset = 0;
    params->beta_offset     = 0;
    params->disable_idc     = 0;
}

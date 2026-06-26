/**
 * motion_est.c — Motion Estimation and Compensation
 *
 * Implements block matching (full search, diamond, hexagon), sub-pixel
 * interpolation, SAD/SATD/SSE metrics, and motion compensation.
 *
 * Knowledge coverage:
 *   L1: Motion vector definitions, search range
 *   L2: Sub-pixel interpolation (H.264 6-tap Wiener)
 *   L5: Block matching algorithms, search patterns, SAD/SATD
 *   L6: Motion compensation, MVD prediction, residual computation
 */

#include "motion_est.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Forward declaration of iclip3 (from video_codec.c) */
static int32_t my_clip3(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* ==========================================================================
 * L2: H.264 Sub-Pixel Interpolation
 * ========================================================================== */

/* 6-tap Wiener interpolation filter coefficients */
static const int h264_halfpel_coeffs[6] = { 1, -5, 20, 20, -5, 1 };

/* Get pixel value with clipping to frame boundary */
static uint8_t get_pixel_safe(const uint8_t *src, uint32_t w, uint32_t h,
                              int32_t x, int32_t y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= w) x = (int32_t)w - 1;
    if ((uint32_t)y >= h) y = (int32_t)h - 1;
    return src[y * w + x];
}

/* Horizontal 6-tap half-pel interpolation */
static uint8_t h264_horiz_halfpel(const uint8_t *src, uint32_t w, uint32_t h,
                                  int32_t x, int32_t y)
{
    int sum = 0;
    for (int i = 0; i < 6; i++) {
        sum += (int)get_pixel_safe(src, w, h, x + i - 2, y)
               * h264_halfpel_coeffs[i];
    }
    return (uint8_t)my_clip3((sum + 16) >> 5, 0, 255);
}

/* Vertical 6-tap half-pel interpolation */
static uint8_t h264_vert_halfpel(const uint8_t *src, uint32_t w, uint32_t h,
                                 int32_t x, int32_t y)
{
    int sum = 0;
    for (int i = 0; i < 6; i++) {
        sum += (int)get_pixel_safe(src, w, h, x, y + i - 2)
               * h264_halfpel_coeffs[i];
    }
    return (uint8_t)my_clip3((sum + 16) >> 5, 0, 255);
}

/* Average two pixels (for quarter-pel positions) */
static uint8_t avg_pixels(uint8_t a, uint8_t b)
{
    return (uint8_t)(((int)a + (int)b + 1) >> 1);
}

uint8_t h264_interp_luma(const uint8_t *src, uint32_t src_w, uint32_t src_h,
                         int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    /* dx, dy in quarter-pel units: 0, 1, 2, 3 */
    if (dx == 0 && dy == 0) {
        return get_pixel_safe(src, src_w, src_h, x, y);
    }
    if (dx == 0 && dy == 2) {
        return h264_vert_halfpel(src, src_w, src_h, x, y);
    }
    if (dx == 2 && dy == 0) {
        return h264_horiz_halfpel(src, src_w, src_h, x, y);
    }
    /* Half-pel center: average of horizontal and vertical */
    if (dx == 2 && dy == 2) {
        /* For the exact half-pel center, we'd do 6-tap in both dirs.
         * Simplified: bilinear from integer pixels */
        uint8_t a = get_pixel_safe(src, src_w, src_h, x, y);
        uint8_t b = get_pixel_safe(src, src_w, src_h, x+1, y);
        uint8_t c = get_pixel_safe(src, src_w, src_h, x, y+1);
        uint8_t d = get_pixel_safe(src, src_w, src_h, x+1, y+1);
        return avg_pixels(avg_pixels(a, b), avg_pixels(c, d));
    }
    /* Quarter-pel positions: average of integer and half-pel */
    uint8_t int_pel = get_pixel_safe(src, src_w, src_h, x, y);
    uint8_t half_h = (dx != 0) ? h264_horiz_halfpel(src, src_w, src_h, x, y)
                    : int_pel;
    uint8_t half_v = (dy != 0) ? h264_vert_halfpel(src, src_w, src_h, x, y)
                    : int_pel;
    if (dx != 0 && dy != 0) {
        return avg_pixels(half_h, half_v);
    }
    if (dx != 0) return avg_pixels(int_pel, half_h);
    return avg_pixels(int_pel, half_v);
}

uint8_t h264_interp_chroma(const uint8_t *src, uint32_t src_w, uint32_t src_h,
                           int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    /* Bilinear interpolation for chroma (1/8 pel precision) */
    int32_t x_frac = dx, y_frac = dy;
    uint8_t a = get_pixel_safe(src, src_w, src_h, x, y);
    uint8_t b = get_pixel_safe(src, src_w, src_h, x+1, y);
    uint8_t c = get_pixel_safe(src, src_w, src_h, x, y+1);
    uint8_t d = get_pixel_safe(src, src_w, src_h, x+1, y+1);
    int val = ((8 - x_frac) * (8 - y_frac) * (int)a
             + x_frac * (8 - y_frac) * (int)b
             + (8 - x_frac) * y_frac * (int)c
             + x_frac * y_frac * (int)d + 32) >> 6;
    return (uint8_t)my_clip3(val, 0, 255);
}

/* ==========================================================================
 * L5: Distortion Metrics
 * ========================================================================== */

uint32_t compute_sad(const uint8_t *orig, uint32_t o_stride,
                     const uint8_t *ref, uint32_t r_stride,
                     uint32_t bw, uint32_t bh)
{
    uint32_t sad = 0;
    for (uint32_t y = 0; y < bh; y++) {
        for (uint32_t x = 0; x < bw; x++) {
            int d = (int)orig[y * o_stride + x] - (int)ref[y * r_stride + x];
            sad += (uint32_t)(d < 0 ? -d : d);
        }
    }
    return sad;
}

uint64_t compute_sse(const uint8_t *orig, uint32_t o_stride,
                     const uint8_t *ref, uint32_t r_stride,
                     uint32_t bw, uint32_t bh)
{
    uint64_t sse = 0;
    for (uint32_t y = 0; y < bh; y++) {
        for (uint32_t x = 0; x < bw; x++) {
            int d = (int)orig[y * o_stride + x] - (int)ref[y * r_stride + x];
            sse += (uint64_t)(d * d);
        }
    }
    return sse;
}

/* 4x4 Hadamard transform for SATD */
static void hadamard_4x4(const int16_t *diff, int32_t *out)
{
    int32_t tmp[16];
    for (int i = 0; i < 4; i++) {
        int a = diff[i*4+0], b = diff[i*4+1];
        int c = diff[i*4+2], d = diff[i*4+3];
        tmp[i*4+0] = a + b + c + d;
        tmp[i*4+1] = a + b - c - d;
        tmp[i*4+2] = a - b - c + d;
        tmp[i*4+3] = a - b + c - d;
    }
    for (int j = 0; j < 4; j++) {
        int a = tmp[0*4+j], b = tmp[1*4+j];
        int c = tmp[2*4+j], d = tmp[3*4+j];
        out[0*4+j] = a + b + c + d;
        out[1*4+j] = a + b - c - d;
        out[2*4+j] = a - b - c + d;
        out[3*4+j] = a - b + c - d;
    }
}

uint32_t compute_satd(const uint8_t *orig, uint32_t o_stride,
                      const uint8_t *ref, uint32_t r_stride,
                      uint32_t bw, uint32_t bh)
{
    uint32_t satd = 0;
    int16_t diff[16];
    int32_t had[16];
    for (uint32_t by = 0; by < bh; by += 4) {
        for (uint32_t bx = 0; bx < bw; bx += 4) {
            for (uint32_t dy = 0; dy < 4; dy++) {
                for (uint32_t dx = 0; dx < 4; dx++) {
                    int o = (int)orig[(by+dy)*o_stride + (bx+dx)];
                    int r = (int)ref[(by+dy)*r_stride + (bx+dx)];
                    diff[dy*4+dx] = (int16_t)(o - r);
                }
            }
            hadamard_4x4(diff, had);
            for (int i = 0; i < 16; i++)
                satd += (uint32_t)(had[i] < 0 ? -had[i] : had[i]);
        }
    }
    return satd;
}

/* ==========================================================================
 * L5: Motion Vector Cost
 * ========================================================================== */

uint32_t mv_cost_bits(int16_t mv_x, int16_t mv_y, int16_t pmv_x, int16_t pmv_y)
{
    int32_t mvd_x = (int32_t)mv_x - (int32_t)pmv_x;
    int32_t mvd_y = (int32_t)mv_y - (int32_t)pmv_y;
    /* Approximate ue(v) bit length for MVD */
    uint32_t ue_x = (mvd_x < 0) ? (uint32_t)(((-mvd_x) << 1) | 1)
                                : (uint32_t)((mvd_x << 1) | 0);
    uint32_t ue_y = (mvd_y < 0) ? (uint32_t)(((-mvd_y) << 1) | 1)
                                : (uint32_t)((mvd_y << 1) | 0);
    /* Helper: ue(v) bit length = 2*floor(log2(v+1)) + 1 */
    uint32_t len_x = 1, len_y = 1;
    if (ue_x > 0) {
        uint32_t lz = 0, t = ue_x;
        while (t >> lz) lz++;
        len_x = (lz > 0) ? 2 * lz - 1 : 1;
    }
    if (ue_y > 0) {
        uint32_t lz = 0, t = ue_y;
        while (t >> lz) lz++;
        len_y = (lz > 0) ? 2 * lz - 1 : 1;
    }
    return len_x + len_y;
}

/* ==========================================================================
 * L5: Full Search Block Matching
 * ========================================================================== */

void me_full_search(const uint8_t *cur, const uint8_t *ref,
                    uint32_t cur_w, uint32_t cur_h,
                    uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                    const me_search_range_t *range,
                    me_result_t *result)
{
    if (!cur || !ref || !range || !result) return;
    uint32_t best_cost = UINT32_MAX;
    int16_t best_mv_x = 0, best_mv_y = 0;
    for (int16_t dy = range->min_y; dy <= range->max_y; dy++) {
        int32_t ref_y = (int32_t)by + dy;
        if (ref_y < 0 || (uint32_t)(ref_y + bh) > cur_h) continue;
        for (int16_t dx = range->min_x; dx <= range->max_x; dx++) {
            int32_t ref_x = (int32_t)bx + dx;
            if (ref_x < 0 || (uint32_t)(ref_x + bw) > cur_w) continue;
            uint32_t sad = compute_sad(
                &cur[by * cur_w + bx], cur_w,
                &ref[ref_y * cur_w + ref_x], cur_w,
                bw, bh);
            if (sad < best_cost) {
                best_cost = sad;
                best_mv_x = dx;
                best_mv_y = dy;
            }
        }
    }
    memset(result, 0, sizeof(*result));
    result->mv.x   = best_mv_x;
    result->mv.y   = best_mv_y;
    result->cost   = best_cost;
    result->ref_idx = 0;
    result->pred_dir = 0;
}

/* ==========================================================================
 * L5: Diamond Search (DS)
 * ========================================================================== */

/* Large Diamond Search Pattern points (distance 2) */
static const int16_t ldsp_offsets[9][2] = {
    {0,0}, {-2,0}, {2,0}, {0,-2}, {0,2}, {-1,-1}, {1,-1}, {-1,1}, {1,1}
};
/* Small Diamond Search Pattern points (distance 1) */
static const int16_t sdsp_offsets[5][2] = {
    {0,0}, {-1,0}, {1,0}, {0,-1}, {0,1}
};

void me_diamond_search(const uint8_t *cur, const uint8_t *ref,
                       uint32_t cur_w, uint32_t cur_h,
                       uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                       const me_search_range_t *range,
                       me_result_t *result)
{
    if (!cur || !ref || !range || !result) return;
    int16_t cx = 0, cy = 0;
    uint32_t best_cost = UINT32_MAX;
    /* LDSP phase */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < 9; i++) {
            int16_t tx = cx + ldsp_offsets[i][0];
            int16_t ty = cy + ldsp_offsets[i][1];
            if (tx < range->min_x || tx > range->max_x) continue;
            if (ty < range->min_y || ty > range->max_y) continue;
            int32_t ref_x = (int32_t)bx + tx;
            int32_t ref_y = (int32_t)by + ty;
            if (ref_x < 0 || (uint32_t)(ref_x + bw) > cur_w) continue;
            if (ref_y < 0 || (uint32_t)(ref_y + bh) > cur_h) continue;
            uint32_t sad = compute_sad(
                &cur[by * cur_w + bx], cur_w,
                &ref[ref_y * cur_w + ref_x], cur_w, bw, bh);
            if (sad < best_cost) {
                best_cost = sad;
                cx = tx; cy = ty;
                changed = 1;
            }
        }
    }
    /* SDSP refinement phase */
    changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < 5; i++) {
            int16_t tx = cx + sdsp_offsets[i][0];
            int16_t ty = cy + sdsp_offsets[i][1];
            if (tx < range->min_x || tx > range->max_x) continue;
            if (ty < range->min_y || ty > range->max_y) continue;
            int32_t ref_x = (int32_t)bx + tx;
            int32_t ref_y = (int32_t)by + ty;
            if (ref_x < 0 || (uint32_t)(ref_x + bw) > cur_w) continue;
            if (ref_y < 0 || (uint32_t)(ref_y + bh) > cur_h) continue;
            uint32_t sad = compute_sad(
                &cur[by * cur_w + bx], cur_w,
                &ref[ref_y * cur_w + ref_x], cur_w, bw, bh);
            if (sad < best_cost) {
                best_cost = sad;
                cx = tx; cy = ty;
                changed = 1;
            }
        }
    }
    memset(result, 0, sizeof(*result));
    result->mv.x  = cx;
    result->mv.y  = cy;
    result->cost  = best_cost;
    result->ref_idx = 0;
}

/* ==========================================================================
 * L5: Hexagon Search (HS)
 * ========================================================================== */

static const int16_t hex_offsets[7][2] = {
    {0,0}, {2,0}, {1,2}, {-1,2}, {-2,0}, {-1,-2}, {1,-2}
};

void me_hexagon_search(const uint8_t *cur, const uint8_t *ref,
                       uint32_t cur_w, uint32_t cur_h,
                       uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                       const me_search_range_t *range,
                       me_result_t *result)
{
    if (!cur || !ref || !range || !result) return;
    int16_t cx = 0, cy = 0;
    uint32_t best_cost = UINT32_MAX;
    /* Hexagon search phase */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < 7; i++) {
            int16_t tx = cx + hex_offsets[i][0];
            int16_t ty = cy + hex_offsets[i][1];
            if (tx < range->min_x || tx > range->max_x) continue;
            if (ty < range->min_y || ty > range->max_y) continue;
            int32_t ref_x = (int32_t)bx + tx;
            int32_t ref_y = (int32_t)by + ty;
            if (ref_x < 0 || (uint32_t)(ref_x + bw) > cur_w) continue;
            if (ref_y < 0 || (uint32_t)(ref_y + bh) > cur_h) continue;
            uint32_t sad = compute_sad(
                &cur[by * cur_w + bx], cur_w,
                &ref[ref_y * cur_w + ref_x], cur_w, bw, bh);
            if (sad < best_cost) {
                best_cost = sad;
                cx = tx; cy = ty;
                changed = 1;
            }
        }
    }
    /* Final SDSP refinement */
    int sdsp_changed = 1;
    while (sdsp_changed) {
        sdsp_changed = 0;
        for (int i = 0; i < 5; i++) {
            int16_t tx = cx + sdsp_offsets[i][0];
            int16_t ty = cy + sdsp_offsets[i][1];
            if (tx < range->min_x || tx > range->max_x) continue;
            if (ty < range->min_y || ty > range->max_y) continue;
            int32_t ref_x = (int32_t)bx + tx;
            int32_t ref_y = (int32_t)by + ty;
            if (ref_x < 0 || (uint32_t)(ref_x + bw) > cur_w) continue;
            if (ref_y < 0 || (uint32_t)(ref_y + bh) > cur_h) continue;
            uint32_t sad = compute_sad(
                &cur[by * cur_w + bx], cur_w,
                &ref[ref_y * cur_w + ref_x], cur_w, bw, bh);
            if (sad < best_cost) {
                best_cost = sad;
                cx = tx; cy = ty;
                sdsp_changed = 1;
            }
        }
    }
    memset(result, 0, sizeof(*result));
    result->mv.x  = cx;
    result->mv.y  = cy;
    result->cost  = best_cost;
    result->ref_idx = 0;
}

/* ==========================================================================
 * L2: Sub-Pixel Refinement
 * ========================================================================== */

void me_subpel_refine(const uint8_t *cur, const uint8_t *ref,
                      uint32_t cur_w, uint32_t cur_h,
                      uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                      me_result_t *result, uint32_t subpel)
{
    if (!cur || !ref || !result) return;
    int16_t best_mv_x = result->mv.x;
    int16_t best_mv_y = result->mv.y;
    uint32_t best_cost = result->cost;
    int step = (subpel >= 2) ? 1 : 2; /* 1=quarter-pel step, 2=half-pel step */
    for (int dy = -2; dy <= 2; dy += step) {
        for (int dx = -2; dx <= 2; dx += step) {
            if (dx == 0 && dy == 0) continue;
            int16_t tx = best_mv_x + (int16_t)dx;
            int16_t ty = best_mv_y + (int16_t)dy;
            /* Compute SAD with sub-pixel interpolation */
            uint32_t sad = 0;
            for (uint32_t py = 0; py < bh; py++) {
                for (uint32_t px = 0; px < bw; px++) {
                    int32_t ref_x_int = (int32_t)(bx + px) + tx / 4;
                    int32_t ref_y_int = (int32_t)(by + py) + ty / 4;
                    int32_t frac_x = (tx % 4 + 4) % 4;
                    int32_t frac_y = (ty % 4 + 4) % 4;
                    uint8_t ref_val = h264_interp_luma(ref, cur_w, cur_h,
                        ref_x_int, ref_y_int, frac_x, frac_y);
                    int d = (int)cur[(by+py)*cur_w + (bx+px)] - (int)ref_val;
                    sad += (uint32_t)(d < 0 ? -d : d);
                }
            }
            if (sad < best_cost) {
                best_cost = sad;
                best_mv_x = tx;
                best_mv_y = ty;
            }
        }
    }
    result->mv.x  = best_mv_x;
    result->mv.y  = best_mv_y;
    result->cost  = best_cost;
}

/* ==========================================================================
 * L6: Motion Compensation
 * ========================================================================== */

int motion_compensate(const video_frame_t *ref_frame,
                      motion_vector_t mv,
                      uint32_t bx, uint32_t by,
                      uint32_t bw, uint32_t bh,
                      uint8_t *pred_out)
{
    if (!ref_frame || !ref_frame->y.data || !pred_out) return -1;
    int32_t mv_x_qpel = mv.x;
    int32_t mv_y_qpel = mv.y;
    for (uint32_t py = 0; py < bh; py++) {
        for (uint32_t px = 0; px < bw; px++) {
            int32_t ref_x = (int32_t)(bx + px) + mv_x_qpel / 4;
            int32_t ref_y = (int32_t)(by + py) + mv_y_qpel / 4;
            int32_t frac_x = (mv_x_qpel % 4 + 4) % 4;
            int32_t frac_y = (mv_y_qpel % 4 + 4) % 4;
            /* Bounds check */
            if (ref_x < 0 || (uint32_t)ref_x >= ref_frame->width ||
                ref_y < 0 || (uint32_t)ref_y >= ref_frame->height) {
                /* Edge case: use boundary pixel */
                if (ref_x < 0) ref_x = 0;
                if (ref_y < 0) ref_y = 0;
                if ((uint32_t)ref_x >= ref_frame->width)
                    ref_x = (int32_t)ref_frame->width - 1;
                if ((uint32_t)ref_y >= ref_frame->height)
                    ref_y = (int32_t)ref_frame->height - 1;
                pred_out[py*bw + px] = ref_frame->y.data[
                    (uint32_t)ref_y * ref_frame->y.stride + (uint32_t)ref_x];
            } else {
                pred_out[py*bw + px] = h264_interp_luma(
                    ref_frame->y.data, ref_frame->width, ref_frame->height,
                    ref_x, ref_y, frac_x, frac_y);
            }
        }
    }
    return 0;
}

int motion_compensate_bipred(const video_frame_t *ref0, motion_vector_t mv0,
                             const video_frame_t *ref1, motion_vector_t mv1,
                             uint32_t bx, uint32_t by,
                             uint32_t bw, uint32_t bh,
                             uint8_t *pred_out)
{
    uint8_t *pred0 = (uint8_t *)malloc(bw * bh);
    uint8_t *pred1 = (uint8_t *)malloc(bw * bh);
    if (!pred0 || !pred1) { free(pred0); free(pred1); return -1; }
    int r0 = motion_compensate(ref0, mv0, bx, by, bw, bh, pred0);
    int r1 = motion_compensate(ref1, mv1, bx, by, bw, bh, pred1);
    for (uint32_t i = 0; i < bw * bh; i++) {
        int a = (r0 == 0) ? (int)pred0[i] : 128;
        int b = (r1 == 0) ? (int)pred1[i] : 128;
        pred_out[i] = (uint8_t)((a + b + 1) >> 1);
    }
    free(pred0); free(pred1);
    return 0;
}

void compute_residual(const uint8_t *orig, const uint8_t *pred,
                      int16_t *residual, uint32_t bw, uint32_t bh)
{
    for (uint32_t i = 0; i < bw * bh; i++) {
        residual[i] = (int16_t)((int)orig[i] - (int)pred[i]);
    }
}

void reconstruct_block(const uint8_t *pred, const int16_t *residual,
                       uint8_t *recon, uint32_t bw, uint32_t bh)
{
    for (uint32_t i = 0; i < bw * bh; i++) {
        int val = (int)pred[i] + (int)residual[i];
        recon[i] = (uint8_t)my_clip3(val, 0, 255);
    }
}

/* ==========================================================================
 * L6: Motion Vector Prediction (Median)
 * ========================================================================== */

void compute_mvp(const motion_vector_t *mv_a,
                 const motion_vector_t *mv_b,
                 const motion_vector_t *mv_c,
                 motion_vector_t *pred)
{
    if (!pred) return;
    /* Default to zero if neighbors unavailable */
    int16_t ax = mv_a ? mv_a->x : 0, ay = mv_a ? mv_a->y : 0;
    int16_t bx = mv_b ? mv_b->x : 0, by = mv_b ? mv_b->y : 0;
    int16_t cx = mv_c ? mv_c->x : 0, cy = mv_c ? mv_c->y : 0;
    /* Median of three */
    if (ax > bx) { int16_t t = ax; ax = bx; bx = t; }
    if (bx > cx) { bx = cx; }
    if (ax > bx) { bx = ax; }
    pred->x = bx;
    if (ay > by) { int16_t t = ay; ay = by; by = t; }
    if (by > cy) { by = cy; }
    if (ay > by) { by = ay; }
    pred->y = by;
}

/* ==========================================================================
 * L5: RD Comparison and Configuration
 * ========================================================================== */

int me_compare_rd(const me_result_t *a, const me_result_t *b, double lambda)
{
    if (!a || !b) return 0;
    double cost_a = (double)a->cost + lambda * 4.0; /* rough MV bit cost */
    double cost_b = (double)b->cost + lambda * 4.0;
    return (cost_b < cost_a) ? 1 : 0;
}

void me_config_init(me_config_t *config, uint32_t search_range_x,
                    uint32_t search_range_y)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    int16_t sx = (int16_t)search_range_x;
    int16_t sy = (int16_t)search_range_y;
    config->range.min_x = (int16_t)(-sx);
    config->range.max_x = sx;
    config->range.min_y = (int16_t)(-sy);
    config->range.max_y = sy;
    config->subpel_refine      = 2;
    config->max_ref_frames     = 1;
    config->early_terminate    = 0;
    config->use_hadamard       = 0;
}

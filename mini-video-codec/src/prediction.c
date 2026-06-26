/**
 * prediction.c — Intra and Inter Prediction Implementation
 *
 * Implements H.264 intra prediction (4x4, 16x16, chroma), inter prediction
 * via motion compensation wrappers, mode decision, and MPM logic.
 *
 * Knowledge coverage:
 *   L1: Intra prediction mode definitions, neighbor context
 *   L2: Spatial intra prediction, inter prediction framework
 *   L5: Intra mode decision (SAD-based), MPM derivation
 *   L6: H.264 intra/inter prediction pipeline
 */

#include "prediction.h"
#include "motion_est.h"
#include <stdlib.h>
#include <string.h>

static int32_t clip3(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* ==========================================================================
 * L2: Intra Prediction Context Setup
 * ========================================================================== */

void intra_context_init(intra_context_t *ctx,
                        const uint8_t *recon, uint32_t recon_stride,
                        int32_t x, int32_t y, uint32_t block_size,
                        uint32_t frame_w, uint32_t frame_h)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->block_size = block_size;

    /* Top edge: copy up to block_size pixels from above */
    if (y > 0) {
        ctx->avail.top_available = 1;
        for (uint32_t i = 0; i < block_size && (uint32_t)(x + (int32_t)i) < frame_w; i++)
            ctx->top[i] = recon[(y-1) * recon_stride + (x + i)];
        /* Top-right: pixels above and to the right */
        ctx->avail.top_right_available = 1;
        for (uint32_t i = block_size; i < 2 * block_size && (uint32_t)(x + (int32_t)i) < frame_w; i++)
            ctx->top[i] = recon[(y-1) * recon_stride + (x + i)];
    }

    /* Left edge: copy block_size pixels from left */
    if (x > 0) {
        ctx->avail.left_available = 1;
        for (uint32_t i = 0; i < block_size && (uint32_t)(y + (int32_t)i) < frame_h; i++)
            ctx->left[i] = recon[(y + i) * recon_stride + (x - 1)];
    }

    /* Top-left corner */
    if (x > 0 && y > 0) {
        ctx->avail.top_left_available = 1;
        ctx->top_left = recon[(y-1) * recon_stride + (x - 1)];
    }

    /* Unavailable pixels default to 128 (mid-gray) as per H.264 spec */
    if (!ctx->avail.top_available) {
        for (uint32_t i = 0; i < 33; i++) ctx->top[i] = 128;
    }
    if (!ctx->avail.left_available) {
        for (uint32_t i = 0; i < 33; i++) ctx->left[i] = 128;
    }
}

/* ==========================================================================
 * L5: H.264 4x4 Intra Prediction (9 modes)
 * ========================================================================== */

static void intra_4x4_vertical(const uint8_t top[9], uint8_t *pred)
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            pred[y*4 + x] = top[x];
}

static void intra_4x4_horizontal(const uint8_t left[9], uint8_t *pred)
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            pred[y*4 + x] = left[y];
}

static void intra_4x4_dc(const uint8_t top[9], const uint8_t left[9],
                         uint8_t *pred, int left_avail, int top_avail)
{
    int sum = 0, shift = 0;
    if (left_avail) { for (int i = 0; i < 4; i++) sum += left[i]; shift += 2; }
    if (top_avail)  { for (int i = 0; i < 4; i++) sum += top[i];  shift += 2; }
    uint8_t dc = (shift > 0) ? (uint8_t)((sum + (1 << (shift-1))) >> shift) : 128;
    for (int i = 0; i < 16; i++) pred[i] = dc;
}

static void intra_4x4_diag_dl(const uint8_t top[13], uint8_t *pred)
{
    /* Diagonal Down-Left: 45-degree angle */
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = x + y + 1;
            if (idx <= 6)
                pred[y*4 + x] = (uint8_t)clip3(
                    ((int)top[idx] + 2*(int)top[idx+1] + (int)top[idx+2] + 2) >> 2, 0, 255);
            else
                pred[y*4 + x] = top[7];
        }
    }
}

static void intra_4x4_diag_dr(const uint8_t top[9], const uint8_t left[9],
                              uint8_t top_left, uint8_t *pred)
{
    /* Diagonal Down-Right: 135-degree angle */
    static const int map[4][4] = {{4,3,2,1},{3,2,1,0},{2,1,0,-1},{1,0,-1,-2}};
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = map[y][x];
            uint8_t val;
            if (idx > 0) val = top[idx-1];
            else if (idx == 0) val = top_left;
            else val = left[-idx-1];
            pred[y*4 + x] = val;
        }
    }
}

static void intra_4x4_vert_right(const uint8_t top[9], const uint8_t left[9],
                                 uint8_t top_left, uint8_t *pred)
{
    /* Vertical-Right */
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int zVR = 2*x - y;
            uint8_t val;
            if (zVR == 0 || zVR == 2 || zVR == 4 || zVR == 6)
                val = (uint8_t)(((int)top[x + (zVR>>1)] + (int)top[x + (zVR>>1) + 1] + 1) >> 1);
            else if (zVR == 1 || zVR == 3 || zVR == 5)
                val = (uint8_t)(((int)top[x+(zVR>>1)] + 2*(int)top[x+(zVR>>1)+1]
                                + (int)top[x+(zVR>>1)+2] + 2) >> 2);
            else if (zVR == -1)
                val = (uint8_t)(((int)top_left + 2*(int)top[0] + (int)top[1] + 2) >> 2);
            else
                val = left[y - (zVR>>1) - 1];
            pred[y*4 + x] = val;
        }
    }
}

static void intra_4x4_horiz_down(const uint8_t top[9], const uint8_t left[9],
                                 uint8_t top_left, uint8_t *pred)
{
    /* Horizontal-Down */
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int zHD = 2*y - x;
            uint8_t val;
            if (zHD == 0 || zHD == 2 || zHD == 4 || zHD == 6)
                val = (uint8_t)(((int)left[y + (zHD>>1)] + (int)left[y + (zHD>>1) + 1] + 1) >> 1);
            else if (zHD == 1 || zHD == 3 || zHD == 5)
                val = (uint8_t)(((int)left[y+(zHD>>1)] + 2*(int)left[y+(zHD>>1)+1]
                                + (int)left[y+(zHD>>1)+2] + 2) >> 2);
            else if (zHD == -1)
                val = (uint8_t)(((int)top_left + 2*(int)left[0] + (int)left[1] + 2) >> 2);
            else
                val = top[x - (zHD>>1) - 1];
            pred[y*4 + x] = val;
        }
    }
}

static void intra_4x4_vert_left(const uint8_t top[13], uint8_t *pred)
{
    /* Vertical-Left */
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int tidx = x + (y >> 1);
            if ((y & 1) == 0)
                pred[y*4 + x] = (uint8_t)(((int)top[tidx] + (int)top[tidx+1] + 1) >> 1);
            else
                pred[y*4 + x] = (uint8_t)(((int)top[tidx] + 2*(int)top[tidx+1]
                                           + (int)top[tidx+2] + 2) >> 2);
        }
    }
}

static void intra_4x4_horiz_up(const uint8_t left[9], uint8_t *pred)
{
    /* Horizontal-Up */
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int lidx = y + (x >> 1);
            if ((x & 1) == 0)
                pred[y*4 + x] = (uint8_t)(((int)left[lidx] + (int)left[lidx+1] + 1) >> 1);
            else
                pred[y*4 + x] = (uint8_t)(((int)left[lidx] + 2*(int)left[lidx+1]
                                           + (int)left[lidx+2] + 2) >> 2);
        }
    }
}

void intra_pred_4x4(const intra_context_t *ctx, intra_4x4_mode_t mode,
                    uint8_t *pred)
{
    if (!ctx || !pred) return;
    /* We need top[0..8] (but top_right gives up to top[12] for some modes).
     * Use the full context memory (top[33]) */
    uint8_t top_ext[13];
    for (int i = 0; i < 13; i++) top_ext[i] = ctx->top[i];
    switch (mode) {
        case INTRA_4x4_VERTICAL:   intra_4x4_vertical(ctx->top, pred); break;
        case INTRA_4x4_HORIZONTAL: intra_4x4_horizontal(ctx->left, pred); break;
        case INTRA_4x4_DC:
            intra_4x4_dc(ctx->top, ctx->left, pred,
                         ctx->avail.left_available, ctx->avail.top_available);
            break;
        case INTRA_4x4_DIAG_DL:    intra_4x4_diag_dl(top_ext, pred); break;
        case INTRA_4x4_DIAG_DR:
            intra_4x4_diag_dr(ctx->top, ctx->left, ctx->top_left, pred); break;
        case INTRA_4x4_VERT_RIGHT:
            intra_4x4_vert_right(ctx->top, ctx->left, ctx->top_left, pred); break;
        case INTRA_4x4_HORIZ_DOWN:
            intra_4x4_horiz_down(ctx->top, ctx->left, ctx->top_left, pred); break;
        case INTRA_4x4_VERT_LEFT:  intra_4x4_vert_left(top_ext, pred); break;
        case INTRA_4x4_HORIZ_UP:   intra_4x4_horiz_up(ctx->left, pred); break;
        default: memset(pred, 128, 16); break;
    }
}

/* ==========================================================================
 * L5: 16x16 Intra Prediction (4 modes)
 * ========================================================================== */

void intra_pred_16x16(const intra_context_t *ctx, intra_16x16_mode_t mode,
                      uint8_t *pred)
{
    if (!ctx || !pred) return;
    switch (mode) {
        case INTRA_16x16_VERTICAL:
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    pred[y*16 + x] = ctx->top[x];
            break;
        case INTRA_16x16_HORIZONTAL:
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    pred[y*16 + x] = ctx->left[y];
            break;
        case INTRA_16x16_DC: {
            int sum = 0, cnt = 0;
            if (ctx->avail.top_available) {
                for (int i = 0; i < 16; i++) { sum += ctx->top[i]; cnt++; }
            }
            if (ctx->avail.left_available) {
                for (int i = 0; i < 16; i++) { sum += ctx->left[i]; cnt++; }
            }
            uint8_t dc = (cnt > 0) ? (uint8_t)((sum + cnt/2) / cnt) : 128;
            memset(pred, dc, 256);
            break;
        }
        case INTRA_16x16_PLANE: {
            int H = 0, V = 0;
            for (int i = 0; i < 7; i++) {
                H += (i + 1) * ((int)ctx->top[8 + i] - (int)ctx->top[6 - i]);
                V += (i + 1) * ((int)ctx->left[8 + i] - (int)ctx->left[6 - i]);
            }
            /* i=7: p[15][-1] - p[-1][-1] = top[15] - top_left */
            H += 8 * ((int)ctx->top[15] - (int)ctx->top_left);
            V += 8 * ((int)ctx->left[15] - (int)ctx->top_left);
            int a = 16 * ((int)ctx->left[15] + (int)ctx->top[15]);
            int b = (5 * H + 32) >> 6;
            int c = (5 * V + 32) >> 6;
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    int val = (a + b*(x-7) + c*(y-7) + 16) >> 5;
                    pred[y*16 + x] = (uint8_t)clip3(val, 0, 255);
                }
            }
            break;
        }
    }
}

void intra_pred_chroma_8x8(const intra_context_t *ctx,
                           intra_chroma_mode_t mode, uint8_t *pred)
{
    if (!ctx || !pred) return;
    switch (mode) {
        case INTRA_CHROMA_DC: {
            int sum = 0, cnt = 0;
            for (int i = 0; i < 8; i++) { sum += ctx->top[i]; cnt++; }
            for (int i = 0; i < 8; i++) { sum += ctx->left[i]; cnt++; }
            uint8_t dc = (cnt > 0) ? (uint8_t)((sum + cnt/2) / cnt) : 128;
            memset(pred, dc, 64);
            break;
        }
        case INTRA_CHROMA_HORIZONTAL:
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    pred[y*8 + x] = ctx->left[y];
            break;
        case INTRA_CHROMA_VERTICAL:
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    pred[y*8 + x] = ctx->top[x];
            break;
        case INTRA_CHROMA_PLANE: {
            int H = 0, V = 0;
            for (int i = 0; i < 3; i++) {
                H += (i+1) * ((int)ctx->top[4+i] - (int)ctx->top[2-i]);
                V += (i+1) * ((int)ctx->left[4+i] - (int)ctx->left[2-i]);
            }
            /* i=3: p[7][-1] - p[-1][-1] = top[7] - top_left */
            H += 4 * ((int)ctx->top[7] - (int)ctx->top_left);
            V += 4 * ((int)ctx->left[7] - (int)ctx->top_left);
            int a = 16 * ((int)ctx->left[7] + (int)ctx->top[7]);
            int b = (17 * H + 16) >> 5;
            int c = (17 * V + 16) >> 5;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int val = (a + b*(x-3) + c*(y-3) + 16) >> 5;
                    pred[y*8 + x] = (uint8_t)clip3(val, 0, 255);
                }
            }
            break;
        }
    }
}

int intra_pred_generate(uint32_t block_size, uint32_t mode,
                        const intra_context_t *ctx, uint8_t *pred)
{
    if (!ctx || !pred) return -1;
    switch (block_size) {
        case 4:  intra_pred_4x4(ctx, (intra_4x4_mode_t)mode, pred); break;
        case 16: intra_pred_16x16(ctx, (intra_16x16_mode_t)mode, pred); break;
        case 8:  intra_pred_chroma_8x8(ctx, (intra_chroma_mode_t)mode, pred); break;
        default: return -1;
    }
    return 0;
}

/* ==========================================================================
 * L5: Intra Mode Decision (SAD-based)
 * ========================================================================== */

void intra_mode_decision_4x4(const uint8_t *orig, uint32_t orig_stride,
                             const intra_context_t *ctx,
                             int use_satd,
                             intra_4x4_mode_t *best_mode,
                             uint32_t *best_cost)
{
    if (!orig || !ctx || !best_mode || !best_cost) return;
    *best_mode = INTRA_4x4_DC;
    *best_cost = UINT32_MAX;
    for (int m = 0; m <= 8; m++) {
        uint8_t pred[16];
        intra_pred_4x4(ctx, (intra_4x4_mode_t)m, pred);
        uint32_t cost;
        if (use_satd) {
            cost = compute_satd(orig, orig_stride, pred, 4, 4, 4);
        } else {
            cost = compute_sad(orig, orig_stride, pred, 4, 4, 4);
        }
        if (cost < *best_cost) {
            *best_cost = cost;
            *best_mode = (intra_4x4_mode_t)m;
        }
    }
}

void intra_mode_decision_16x16(const uint8_t *orig, uint32_t orig_stride,
                               const intra_context_t *ctx,
                               intra_16x16_mode_t *best_mode,
                               uint32_t *best_cost)
{
    if (!orig || !ctx || !best_mode || !best_cost) return;
    *best_mode = INTRA_16x16_DC;
    *best_cost = UINT32_MAX;
    for (int m = 0; m < 4; m++) {
        uint8_t pred[256];
        intra_pred_16x16(ctx, (intra_16x16_mode_t)m, pred);
        uint32_t cost = compute_sad(orig, orig_stride, pred, 16, 16, 16);
        if (cost < *best_cost) {
            *best_cost = cost;
            *best_mode = (intra_16x16_mode_t)m;
        }
    }
}

intra_4x4_mode_t intra_get_mpm_4x4(intra_4x4_mode_t mode_left,
                                   intra_4x4_mode_t mode_top,
                                   int left_avail, int top_avail)
{
    if (!left_avail && !top_avail) return INTRA_4x4_DC;
    if (!left_avail) return mode_top;
    if (!top_avail) return mode_left;
    return (mode_left < mode_top) ? mode_left : mode_top;
}

/* ==========================================================================
 * L6: Inter Prediction via Motion Compensation
 * ========================================================================== */

int inter_pred_generate(const ref_frame_list_t *ref_list,
                        uint32_t ref_idx,
                        motion_vector_t mv,
                        uint32_t bx, uint32_t by,
                        uint32_t bw, uint32_t bh,
                        uint8_t *pred)
{
    if (!ref_list || !pred) return -1;
    if (ref_idx >= ref_list->num_frames) return -1;
    video_frame_t *ref_frame = ref_list->frames[ref_idx];
    if (!ref_frame) return -1;
    return motion_compensate(ref_frame, mv, bx, by, bw, bh, pred);
}

int inter_pred_bipred(const ref_frame_list_t *list0, uint32_t ref0,
                      motion_vector_t mv0,
                      const ref_frame_list_t *list1, uint32_t ref1,
                      motion_vector_t mv1,
                      uint32_t bx, uint32_t by,
                      uint32_t bw, uint32_t bh,
                      uint8_t *pred)
{
    if (!list0 || !list1 || !pred) return -1;
    video_frame_t *f0 = (ref0 < list0->num_frames) ? list0->frames[ref0] : NULL;
    video_frame_t *f1 = (ref1 < list1->num_frames) ? list1->frames[ref1] : NULL;
    if (!f0 && !f1) return -1;
    if (!f0) return motion_compensate(f1, mv1, bx, by, bw, bh, pred);
    if (!f1) return motion_compensate(f0, mv0, bx, by, bw, bh, pred);
    return motion_compensate_bipred(f0, mv0, f1, mv1, bx, by, bw, bh, pred);
}

uint32_t inter_skip_sad(const uint8_t *orig, uint32_t orig_stride,
                        const uint8_t *pred, uint32_t pred_stride,
                        uint32_t bw, uint32_t bh)
{
    return compute_sad(orig, orig_stride, pred, pred_stride, bw, bh);
}

/* ==========================================================================
 * L5: Motion Vector Bounds and Temporal Scaling
 * ========================================================================== */

int mv_in_bounds(int32_t bx, int32_t by, int32_t bw, int32_t bh,
                 motion_vector_t mv, uint32_t frame_w, uint32_t frame_h)
{
    int32_t ref_x = bx + mv.x / 4;  /* mv in quarter-pel */
    int32_t ref_y = by + mv.y / 4;
    return (ref_x >= 0 && ref_x + bw <= (int32_t)frame_w &&
            ref_y >= 0 && ref_y + bh <= (int32_t)frame_h);
}

void mv_scale_temporal(const motion_vector_t *col_mv,
                       int32_t tb, int32_t td,
                       motion_vector_t *mv_l0, motion_vector_t *mv_l1)
{
    if (!col_mv || !mv_l0 || !mv_l1) return;
    if (td == 0) { mv_l0->x = 0; mv_l0->y = 0; mv_l1->x = 0; mv_l1->y = 0; return; }
    int32_t tx = (16384 + abs(td / 2)) / td;
    int32_t ds = clip3((tb * tx + 32) >> 6, -128, 127);
    mv_l0->x = (int16_t)((ds * col_mv->x + 128) >> 8);
    mv_l0->y = (int16_t)((ds * col_mv->y + 128) >> 8);
    ds = clip3(((tb - td) * tx + 32) >> 6, -128, 127);
    mv_l1->x = (int16_t)((ds * col_mv->x + 128) >> 8);
    mv_l1->y = (int16_t)((ds * col_mv->y + 128) >> 8);
}

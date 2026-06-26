/**
 * example_motion_comp.c — Motion Estimation and Compensation Demo
 *
 * Demonstrates: full search, diamond search, hexagon search, sub-pixel
 * refinement, motion compensation, and SAD/SATD/SSE comparison.
 *
 * L6 Canonical Problem: Block-based motion estimation and compensation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/video_codec.h"
#include "../include/motion_est.h"
#include "../include/prediction.h"

int main(void) {
    printf("=== Motion Estimation & Compensation Demo ===\n\n");

    /* 1. Create synthetic frames (64x64) */
    uint32_t fw = 64, fh = 64;
    uint8_t *cur = (uint8_t *)calloc(fw * fh, 1);
    uint8_t *ref = (uint8_t *)calloc(fw * fh, 1);
    if (!cur || !ref) { printf("malloc failed\n"); return 1; }

    /* Current frame: gradient */
    for (uint32_t y = 0; y < fh; y++)
        for (uint32_t x = 0; x < fw; x++)
            cur[y * fw + x] = (uint8_t)(x + y);

    /* Reference frame: same gradient, shifted by (5,3) pixels */
    memset(ref, 128, fw * fh);
    for (uint32_t y = 3; y < fh; y++)
        for (uint32_t x = 5; x < fw; x++)
            ref[y * fw + x] = (uint8_t)((x - 5) + (y - 3));

    printf("Frames: 64x64, current=gradient, ref=gradient shifted (5,3)\n\n");

    /* 2. Full search motion estimation */
    me_search_range_t range = {-8, 8, -8, 8};
    me_result_t fs_result;
    printf("Full Search (block 16,16 8x8, range +/-8):\n");
    me_full_search(cur, ref, fw, fh, 16, 16, 8, 8, &range, &fs_result);
    printf("  Best MV: (%d, %d), SAD=%u\n",
           fs_result.mv.x, fs_result.mv.y, fs_result.cost);
    printf("  Expected MV: (5, 3) — %s\n",
           fs_result.mv.x == 5 && fs_result.mv.y == 3 ? "CORRECT" : "partial match");

    /* 3. Diamond search */
    me_result_t ds_result;
    printf("\nDiamond Search:\n");
    me_diamond_search(cur, ref, fw, fh, 16, 16, 8, 8, &range, &ds_result);
    printf("  Best MV: (%d, %d), SAD=%u\n",
           ds_result.mv.x, ds_result.mv.y, ds_result.cost);

    /* 4. Hexagon search */
    me_result_t hs_result;
    printf("\nHexagon Search:\n");
    me_hexagon_search(cur, ref, fw, fh, 16, 16, 8, 8, &range, &hs_result);
    printf("  Best MV: (%d, %d), SAD=%u\n",
           hs_result.mv.x, hs_result.mv.y, hs_result.cost);

    /* 5. Compare distortion metrics */
    printf("\nDistortion metrics (8x8 block at 16,16):\n");
    uint32_t sad = compute_sad(&cur[16*fw+16], fw, &ref[16*fw+16], fw, 8, 8);
    uint64_t sse = compute_sse(&cur[16*fw+16], fw, &ref[16*fw+16], fw, 8, 8);
    uint32_t satd = compute_satd(&cur[16*fw+16], fw, &ref[16*fw+16], fw, 8, 8);
    printf("  No motion: SAD=%u, SSE=%llu, SATD=%u\n",
           sad, (unsigned long long)sse, satd);

    /* At best MV position */
    uint32_t best_sad = compute_sad(&cur[16*fw+16], fw,
        &ref[(16+fs_result.mv.y)*fw+(16+fs_result.mv.x)], fw, 8, 8);
    printf("  At best MV(%d,%d): SAD=%u\n",
           fs_result.mv.x, fs_result.mv.y, best_sad);

    /* 6. Motion compensation demo */
    printf("\nMotion Compensation:\n");
    video_frame_t ref_frame;
    video_frame_alloc(&ref_frame, fw, fh, PIX_FMT_YUV420P);
    /* Copy ref data to frame */
    for (uint32_t y = 0; y < fh; y++)
        for (uint32_t x = 0; x < fw; x++)
            ref_frame.y.data[y * ref_frame.y.stride + x] = ref[y * fw + x];

    uint8_t pred_block[64];
    motion_compensate(&ref_frame, fs_result.mv, 16, 16, 8, 8, pred_block);

    /* Compute residual */
    int16_t residual[64];
    compute_residual(&cur[16*fw+16], pred_block, residual, 8, 8);

    /* Reconstruct */
    uint8_t recon[64];
    reconstruct_block(pred_block, residual, recon, 8, 8);

    /* Verify reconstruction */
    uint32_t recon_sad = 0;
    for (int i = 0; i < 64; i++) {
        int d = (int)cur[16*fw+16 + i] - (int)recon[i];
        recon_sad += (uint32_t)(d < 0 ? -d : d);
    }
    printf("  Reconstruction SAD: %u (0 = perfect)\n", recon_sad);

    /* 7. MV prediction */
    printf("\nMV Prediction (median):\n");
    motion_vector_t mv_a = {4, 0}, mv_b = {0, 4}, mv_c = {2, 2};
    motion_vector_t mvp;
    compute_mvp(&mv_a, &mv_b, &mv_c, &mvp);
    printf("  MVs: A=(%d,%d) B=(%d,%d) C=(%d,%d)\n",
           mv_a.x, mv_a.y, mv_b.x, mv_b.y, mv_c.x, mv_c.y);
    printf("  Median MVP: (%d, %d)\n", mvp.x, mvp.y);

    /* 8. MV cost estimation */
    uint32_t mv_bits = mv_cost_bits(fs_result.mv.x, fs_result.mv.y,
                                    mvp.x, mvp.y);
    printf("  Estimated MV cost: %u bits\n", mv_bits);

    /* 9. MV bounds check */
    printf("\nMV Bounds Check:\n");
    printf("  MV (5,3) in bounds: %s\n",
           mv_in_bounds(16, 16, 8, 8, fs_result.mv, fw, fh) ? "yes" : "no");
    motion_vector_t out_of_bounds = {-20, -20};
    printf("  MV (-20,-20) in bounds: %s\n",
           mv_in_bounds(16, 16, 8, 8, out_of_bounds, fw, fh) ? "yes" : "no");

    video_frame_free(&ref_frame);
    free(cur);
    free(ref);
    printf("\nDemo complete.\n");
    return 0;
}

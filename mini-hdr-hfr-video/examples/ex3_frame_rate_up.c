#include "hfr_core.h"
#include "hfr_motion.h"
#include <stdio.h>
#include <math.h>

int main(void)
{
    printf("=== HFR Frame Rate Upconversion Example ===\n\n");

    /* Create synthetic frames */
    int w = 32, h = 24;
    hfr_frame_t *frame_a = hfr_frame_alloc(w, h, 1);
    hfr_frame_t *frame_b = hfr_frame_alloc(w, h, 1);
    hfr_frame_t *frame_c = hfr_frame_alloc(w, h, 1);

    /* Frame A: gradient */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            hfr_frame_pixel_set(frame_a, x, y, 0, (double)x / (double)w);

    /* Frame B: shifted gradient (simulating motion) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double src_x = (double)x - 4.0;
            if (src_x < 0) src_x = 0;
            if (src_x >= w) src_x = w - 1;
            int ix = (int)src_x;
            double fx = src_x - (double)ix;
            double v0 = hfr_frame_pixel_get(frame_a, ix, y, 0);
            double v1 = hfr_frame_pixel_get(frame_a, (ix < w - 1) ? ix + 1 : ix, y, 0);
            hfr_frame_pixel_set(frame_b, x, y, 0, v0 * (1.0 - fx) + v1 * fx);
        }

    /* Frame C: further shifted */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double src_x = (double)x - 8.0;
            if (src_x < 0) src_x = 0;
            if (src_x >= w) src_x = w - 1;
            int ix = (int)src_x;
            double fx = src_x - (double)ix;
            double v0 = hfr_frame_pixel_get(frame_a, ix, y, 0);
            double v1 = hfr_frame_pixel_get(frame_a, (ix < w - 1) ? ix + 1 : ix, y, 0);
            hfr_frame_pixel_set(frame_c, x, y, 0, v0 * (1.0 - fx) + v1 * fx);
        }

    /* Frame rate conversion from 24fps to 60fps */
    printf("--- Frame Rate Conversion: 24fps -> 60fps ---\n");
    hfr_conversion_config_t conv_cfg;
    hfr_conversion_config_init(&conv_cfg);
    conv_cfg.framerate_in = 24.0;
    conv_cfg.framerate_out = 60.0;

    double ratio = hfr_compute_conversion_ratio(24.0, 60.0);
    printf("  Conversion ratio: %.3fx\n", ratio);

    /* Show conversion offsets */
    printf("  Input frame | Output frame\n");
    printf("  ------------+--------------\n");
    for (int fi = 0; fi < 5; fi++) {
        int out_idx = hfr_find_conversion_offset(24.0, 60.0, fi);
        printf("  %10d  | %12d\n", fi, out_idx);
    }

    /* Frame difference analysis */
    printf("\n--- Frame Difference (MAD) ---\n");
    double diff_ab = hfr_compute_frame_difference_mad(frame_a, frame_b);
    double diff_ac = hfr_compute_frame_difference_mad(frame_a, frame_c);
    printf("  MAD(A, B) = %.6f\n", diff_ab);
    printf("  MAD(A, C) = %.6f\n", diff_ac);
    printf("  (Larger difference for larger motion, as expected)\n");

    /* Frame blending (interpolation) */
    printf("\n--- Frame Interpolation (Blend A->B at t=0.5) ---\n");
    hfr_frame_t *interp = hfr_frame_alloc(w, h, 1);
    hfr_frame_blend(frame_a, frame_b, 0.5, interp);
    printf("  A[16,12] = %.4f\n", hfr_frame_pixel_get(frame_a, 16, 12, 0));
    printf("  B[16,12] = %.4f\n", hfr_frame_pixel_get(frame_b, 16, 12, 0));
    printf("  Interp[16,12] = %.4f\n", hfr_frame_pixel_get(interp, 16, 12, 0));

    /* Motion detection */
    printf("\n--- Motion Detection ---\n");
    double motion = hfr_motion_detect_pixel(frame_a, frame_b, 16, 12);
    printf("  Motion at (16,12) = %.6f\n", motion);
    motion = hfr_motion_detect_pixel(frame_a, frame_a, 16, 12);
    printf("  Motion at (16,12) same frame = %.6f (should be 0)\n", motion);

    /* Block matching motion estimation */
    printf("\n--- Motion Estimation (Block Matching) ---\n");
    hfr_motion_est_config_t me_cfg;
    hfr_me_config_init(&me_cfg);
    me_cfg.block_size = 8;
    me_cfg.search_range = 8;

    hfr_motion_field_t *field = hfr_me_block_match_exhaustive(
        frame_a->data, frame_b->data, w, h, &me_cfg);

    if (field) {
        printf("  Motion field: %d blocks (%dx%d)\n",
               field->num_vectors, field->width_blocks, field->height_blocks);
        printf("  Block size: %d\n", field->block_size);
        printf("  Avg displacement: %.2f pixels\n", field->avg_displacement);
        printf("  Max displacement: %.2f pixels\n", field->max_displacement);

        /* Check a few vectors */
        for (int by = 0; by < field->height_blocks && by < 3; by++) {
            for (int bx = 0; bx < field->width_blocks && bx < 3; bx++) {
                int idx = by * field->width_blocks + bx;
                printf("  Block(%d,%d): dx=%.1f dy=%.1f conf=%.3f\n",
                       bx, by, field->vectors[idx].dx,
                       field->vectors[idx].dy, field->vectors[idx].confidence);
            }
        }
        hfr_motion_field_free(field);
    }

    /* Temporal denoising */
    printf("\n--- Temporal Denoising (3-frame median) ---\n");
    hfr_frame_t *denoised = hfr_frame_alloc(w, h, 1);
    hfr_temporal_median_3(frame_a, frame_b, frame_c, denoised);
    printf("  A[10,12]=%.4f B[10,12]=%.4f C[10,12]=%.4f -> Denoised=%.4f\n",
           hfr_frame_pixel_get(frame_a, 10, 12, 0),
           hfr_frame_pixel_get(frame_b, 10, 12, 0),
           hfr_frame_pixel_get(frame_c, 10, 12, 0),
           hfr_frame_pixel_get(denoised, 10, 12, 0));

    /* Shutter angle / motion blur */
    printf("\n--- Cinematic Shutter / Motion Blur ---\n");
    double ss_180 = hfr_shutter_speed_from_angle(180.0, 24.0);
    double ss_90 = hfr_shutter_speed_from_angle(90.0, 24.0);
    printf("  180-degree shutter @ 24fps: %.4f sec (1/%.0f)\n", ss_180, 1.0 / ss_180);
    printf("   90-degree shutter @ 24fps: %.4f sec (1/%.0f)\n", ss_90, 1.0 / ss_90);

    double blur_kernel = hfr_motion_blur_kernel_size(180.0, 24.0, 2.5);
    printf("  Motion blur kernel size (2.5x FPS ratio): %.2f pixels\n", blur_kernel);

    /* Cleanup */
    hfr_frame_free(frame_a);
    hfr_frame_free(frame_b);
    hfr_frame_free(frame_c);
    hfr_frame_free(interp);
    hfr_frame_free(denoised);

    printf("\n=== Example complete ===\n");
    return 0;
}

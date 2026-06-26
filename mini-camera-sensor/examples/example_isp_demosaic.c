/**
 * @file example_isp_demosaic.c
 * @brief L6 Example: Bayer demosaicing comparison
 *
 * Creates a synthetic Bayer pattern (simple horizontal gradient),
 * applies bilinear, MHC, and gradient-corrected demosaicing,
 * and compares quality against ground truth.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../include/camera_sensor.h"
#include "../include/pixel_array.h"
#include "../include/demosaic.h"

int main(void)
{
    printf("=== Bayer Demosaicing Comparison ===\n\n");

    /* Create a synthetic 64x64 Bayer test pattern */
    const uint32_t W = 64, H = 64;
    raw_frame_t *raw = raw_frame_alloc(W, H, CFA_BAYER_RGGB);
    rgb_image_planar_t *truth = rgb_planar_alloc(W, H);

    /* Generate synthetic image: horizontal ramp R, vertical ramp B,
     * constant green */
    uint32_t x, y;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            bayer_color_t c = bayer_color_at(x, y, CFA_BAYER_RGGB);
            double r_val = (double)x * 65535.0 / W;
            double g_val = 32768.0;
            double b_val = (double)y * 65535.0 / H;

            if (c == BAYER_COLOR_R) {
                raw->data[y * raw->stride + x] = (uint16_t)r_val;
            } else if (c == BAYER_COLOR_GR || c == BAYER_COLOR_GB) {
                raw->data[y * raw->stride + x] = (uint16_t)g_val;
            } else {
                raw->data[y * raw->stride + x] = (uint16_t)b_val;
            }

            /* Ground truth */
            truth->r[y * W + x] = (uint16_t)r_val;
            truth->g[y * W + x] = (uint16_t)g_val;
            truth->b[y * W + x] = (uint16_t)b_val;
        }
    }

    /* Test bilinear demosaicing */
    rgb_image_planar_t *bilinear = rgb_planar_alloc(W, H);
    demosaic_bilinear(raw, bilinear);

    /* Test MHC demosaicing */
    rgb_image_planar_t *mhc = rgb_planar_alloc(W, H);
    demosaic_mhc(raw, mhc);

    /* Test gradient-corrected */
    rgb_image_planar_t *grad = rgb_planar_alloc(W, H);
    demosaic_gradient_corrected(raw, grad);

    /* Compute PSNR for each method */
    double psnr_b = demosaic_psnr_channel(bilinear->r, truth->r, W*H, 65535);
    double psnr_m = demosaic_psnr_channel(mhc->r, truth->r, W*H, 65535);
    double psnr_g = demosaic_psnr_channel(grad->r, truth->r, W*H, 65535);

    printf("Red channel PSNR:\n");
    printf("  Bilinear:           %.2f dB\n", psnr_b);
    printf("  Malvar-He-Cutler:   %.2f dB\n", psnr_m);
    printf("  Gradient-corrected: %.2f dB\n", psnr_g);

    /* Full quality assessment */
    demosaic_quality_t qm, qb;
    demosaic_quality_assess(mhc, truth, &qm);
    demosaic_quality_assess(bilinear, truth, &qb);

    printf("\nQuality metrics (MHC):\n");
    printf("  PSNR: R=%.1f G=%.1f B=%.1f dB\n", qm.psnr_r, qm.psnr_g, qm.psnr_b);
    printf("  SSIM proxy: %.3f\n", qm.ssim);
    printf("  Delta-E avg: %.2f\n", qm.delta_e_avg);

    /* Cleanup */
    raw_frame_free(raw);
    rgb_planar_free(truth);
    rgb_planar_free(bilinear);
    rgb_planar_free(mhc);
    rgb_planar_free(grad);

    printf("\n=== Example Complete ===\n");
    return 0;
}

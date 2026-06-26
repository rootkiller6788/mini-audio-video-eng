/**
 * example_video_codec.c — End-to-End Video Codec Pipeline Demo
 *
 * Demonstrates: parameter init, frame allocation, intra prediction,
 * DCT, quantization, dequantization, IDCT, reconstruction, and PSNR.
 *
 * This simulates a minimal H.264-style intra encoding/decoding cycle.
 *
 * L6 Canonical Problem: Intra frame encoding and reconstruction pipeline.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/video_codec.h"
#include "../include/dct_2d.h"
#include "../include/prediction.h"
#include "../include/quantizer.h"

int main(void) {
    printf("=== Video Codec Intra Pipeline Demo ===\n\n");

    /* Step 1: Create codec parameters for 720p */
    codec_video_params_t params;
    video_params_init(&params, 1280, 720, 30, 1);
    params.bitrate = 5000000;
    printf("Codec config: %ux%u @ %u fps, %u kbps\n",
           params.dims.width, params.dims.height,
           params.frame_rate_num / params.frame_rate_den,
           params.bitrate / 1000);
    printf("MB count: %u\n", video_frame_mb_count(&params.dims));
    printf("Compression ratio: %.1f:1\n", video_compression_ratio(&params));

    /* Step 2: Allocate original and reconstructed frames */
    video_frame_t orig, recon;
    video_frame_alloc(&orig, 64, 64, PIX_FMT_YUV420P);
    video_frame_alloc(&recon, 64, 64, PIX_FMT_YUV420P);

    /* Fill original with gradient pattern */
    for (uint32_t y = 0; y < 64; y++)
        for (uint32_t x = 0; x < 64; x++)
            orig.y.data[y * orig.y.stride + x] = (uint8_t)((x + y) % 256);

    printf("\nOriginal frame: gradient fill, %ux%u\n", orig.width, orig.height);

    /* Step 3: Intra prediction (DC mode for 16x16 luma block) */
    uint8_t recon_block[256];
    quantizer_t q;
    quantizer_init(&q, 28, 1);  /* QP=28, intra */

    /* Setup intra context from empty reconstruction (first frame) */
    intra_context_t ctx;
    uint8_t fake_recon[4096];
    memset(fake_recon, 128, 4096);
    intra_context_init(&ctx, fake_recon, 64, 0, 0, 16, 64, 64);

    /* Generate prediction */
    uint8_t pred[256];
    intra_pred_16x16(&ctx, INTRA_16x16_DC, pred);
    printf("Intra DC prediction generated (mean = %d)\n", (int)pred[0]);

    /* Step 4: Compute residual, DCT, quantize (simulated on top-left 4x4 block) */
    int16_t residual[16];
    int32_t coeffs[16];
    for (int i = 0; i < 16; i++) {
        residual[i] = (int16_t)((int)orig.y.data[i] - (int)pred[i]);
    }
    printf("\nTop-left 4x4 residual (sample):");
    for (int i = 0; i < 4; i++)
        printf(" %d", residual[i]);
    printf("\n");

    h264_dct_4x4_fwd(residual, coeffs);
    printf("DCT coefficients (first row):");
    for (int i = 0; i < 4; i++)
        printf(" %d", coeffs[i]);
    printf("\n");

    int nz_count = quantize_block_4x4(&q, coeffs);
    printf("After quantization (QP=28): %d non-zero coeffs\n", nz_count);

    /* Step 5: Dequantize, IDCT, reconstruct */
    dequantize_block_4x4(&q, coeffs);
    int16_t restored_residual[16];
    h264_idct_4x4_inv(coeffs, restored_residual);
    for (int i = 0; i < 16; i++) {
        int val = (int)pred[i] + (int)restored_residual[i];
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        recon_block[i] = (uint8_t)val;
    }
    printf("Reconstructed block (first 4 pixels):");
    for (int i = 0; i < 4; i++)
        printf(" %d", recon_block[i]);
    printf("\n");

    /* Step 6: Copy to reconstruction frame */
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 4; x++)
            recon.y.data[y * recon.y.stride + x] = recon_block[y * 4 + x];

    /* Step 7: Compute PSNR */
    double psnr_y, psnr_u, psnr_v;
    video_frame_psnr(&orig, &recon, &psnr_y, &psnr_u, &psnr_v);
    printf("\nPSNR-Y: %.2f dB (after 1-block intra coding)\n", psnr_y);

    /* Step 8: Rate-Distortion analysis */
    double lambda = rd_lambda_from_qp(28);
    rd_cost_t cost;
    rd_cost_compute(&cost, 1000.0, nz_count * 50, lambda);
    printf("RD cost: D=%.1f + lambda=%.3f * R=%llu = %.2f\n",
           cost.distortion, cost.lambda,
           (unsigned long long)cost.rate_bits, cost.rd_cost);

    /* SQNR */
    double sqnr = quant_sqnr_db(28, 8);
    printf("Theoretical SQNR at QP=28: %.2f dB\n\n", sqnr);

    video_frame_free(&orig);
    video_frame_free(&recon);
    printf("Demo complete.\n");
    return 0;
}

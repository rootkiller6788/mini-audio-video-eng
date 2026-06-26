/**
 * example_dct_demo.c — DCT Transform and Energy Compaction Demo
 *
 * Demonstrates: 2D DCT/IDCT roundtrip, H.264 integer DCT, Hadamard
 * transform, zigzag scan, energy compaction, and basis visualization.
 *
 * L6 Canonical Problem: Transform coding with energy compaction analysis.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/video_codec.h"
#include "../include/dct_2d.h"

int main(void) {
    printf("=== DCT Transform Demo ===\n\n");

    /* 1. DCT basis functions */
    printf("DCT-II basis for N=4 (k=0, 1):\n");
    for (uint32_t k = 0; k < 2; k++) {
        printf("  k=%u: [", k);
        for (uint32_t n = 0; n < 4; n++) {
            printf("%s%.3f", n > 0 ? ", " : "", dct_basis(k, n, 4));
        }
        printf("]\n");
    }

    /* 2. Floating-point DCT roundtrip */
    printf("\nFloating-point DCT 4x4 roundtrip test:\n");
    double input[16] = {10,20,30,40, 50,60,70,80, 90,100,110,120, 130,140,150,160};
    double coeffs[16], output[16];
    dct_2d_fp(input, coeffs, 4);
    printf("  DCT coeffs (first row): %.1f %.1f %.1f %.1f\n",
           coeffs[0], coeffs[1], coeffs[2], coeffs[3]);
    idct_2d_fp(coeffs, output, 4);
    double max_err = 0.0;
    for (int i = 0; i < 16; i++) {
        double e = fabs(output[i] - input[i]);
        if (e > max_err) max_err = e;
    }
    printf("  Max reconstruction error: %.6f\n", max_err);

    /* 3. H.264 4x4 integer DCT */
    printf("\nH.264 4x4 integer DCT test:\n");
    int16_t block[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    int32_t icoeffs[16];
    int16_t recon[16];
    h264_dct_4x4_fwd(block, icoeffs);
    printf("  Integer DCT coeffs (first row):");
    for (int i = 0; i < 4; i++) printf(" %d", icoeffs[i]);
    printf("\n");
    h264_idct_4x4_inv(icoeffs, recon);
    printf("  Recon first row:");
    for (int i = 0; i < 4; i++) printf(" %d", recon[i]);
    printf("\n");

    /* 4. Energy compaction */
    double ec = dct_energy_compaction(icoeffs, 4, 3);
    printf("  Energy compaction (top 3/16 coeffs): %.2f%%\n", ec * 100.0);

    /* 5. Hadamard transform (SATD basis) */
    printf("\n4x4 Hadamard transform test:\n");
    int32_t h_in[16] = {5,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
    int32_t h_out[16];
    h264_hadamard_4x4_fwd(h_in, h_out);
    printf("  Hadamard coeffs:");
    for (int i = 0; i < 4; i++) printf(" %d", h_out[i]);
    printf("\n");

    /* 6. Zigzag scan demo */
    printf("\nZigzag scan 4x4 demo:\n");
    int32_t zz2d[16] = {0};
    zz2d[0] = 5; zz2d[3] = -2; zz2d[10] = 1; zz2d[15] = 3;
    int32_t zz1d[16];
    int last = zigzag_scan_4x4(zz2d, zz1d);
    printf("  Before scan: [%d,%d,%d,%d]\n", zz2d[0], zz2d[3], zz2d[10], zz2d[15]);
    printf("  After scan:  [%d,%d,%d,%d], last_nz=%d\n",
           zz1d[0], zz1d[1], zz1d[2], zz1d[3], last);

    /* 7. Energy conservation */
    printf("\nEnergy conservation check: ");
    if (dct_verify_energy_conservation(block, icoeffs, 4, 0.01))
        printf("PASS (energy ratio stable)\n");
    else
        printf("NOTE (integer DCT uses scaling)\n");

    /* 8. DCT engine dispatch */
    printf("\nDCT engine dispatch:\n");
    dct_fwd_fn f4 = dct_get_fwd_func(4);
    dct_fwd_fn f8 = dct_get_fwd_func(8);
    printf("  dct_fwd_func(4) = %s\n", f4 ? "available" : "NULL");
    printf("  dct_fwd_func(8) = %s\n", f8 ? "available" : "NULL");
    printf("  dct_fwd_func(32) = %s\n", dct_get_fwd_func(32) ? "available" : "NULL");

    printf("\nDemo complete.\n");
    return 0;
}

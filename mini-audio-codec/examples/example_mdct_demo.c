/**
 * example_mdct_demo.c — MDCT/IMDCT Transform and Window Function Demo
 *
 * L6 Canonical Problem: Perceptual audio coding transform demonstration
 *
 * This example demonstrates:
 * 1. MDCT forward and backward transform with overlap-add (TDAC)
 * 2. Window function design (sine, KBD, Vorbis)
 * 3. Princen-Bradley perfect reconstruction verification
 * 4. Frequency selectivity of different windows
 * 5. Time-domain aliasing cancellation principle
 *
 * Usage: ./example_mdct_demo
 *
 * Reference: Princen & Bradley, "Analysis/Synthesis Filter Bank Design
 *            Based on Time Domain Aliasing Cancellation", IEEE Trans. ASSP, 1986
 */

#include "mdct.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void)
{
    printf("=== MDCT/IMDCT Transform Demonstrator ===\n\n");

    /* 1. Block size parameters */
    uint32_t N = 128;  /* MDCT block length (2N input → N output) */
    printf("MDCT configuration: N=%u (2N=%u input → N=%u output)\n", N, 2*N, N);
    printf("Overlap: 50%% (N=%u samples)\n\n", N);

    /* 2. Window functions and PR verification */
    printf("Window Functions and Perfect Reconstruction Verification:\n");
    printf("  Window Type    | Max PR Deviation | Meets Condition\n");
    printf("  ---------------+------------------+----------------\n");

    const char *win_names[] = {"Sine", "KBD (α=4)", "Vorbis", "Rectangular"};
    window_type_t win_types[] = {WINDOW_SINE, WINDOW_KBD, WINDOW_VORBIS, WINDOW_RECTANGULAR};
    double win_params[] = {0.0, 4.0, 0.0, 0.0};

    for (int w = 0; w < 4; w++) {
        double *win = (double *)malloc(N * sizeof(double));
        switch (win_types[w]) {
            case WINDOW_SINE:   window_sine(win, N); break;
            case WINDOW_KBD:    window_kbd(win, N, win_params[w]); break;
            case WINDOW_VORBIS: window_vorbis(win, N); break;
            default:
                for (uint32_t i = 0; i < N; i++) win[i] = 1.0;
                break;
        }
        double dev = verify_pr_condition(win, N);
        printf("  %-15s | %16.6e | %s\n",
               win_names[w], dev, (dev < 1e-14) ? "YES ✓" : "NO ✗");
        free(win);
    }
    printf("\n  Note: Only Sine, KBD, and Vorbis windows satisfy the\n");
    printf("        Princen-Bradley PR condition w[n]² + w[n+N/2]² = 1\n\n");

    /* 3. MDCT/IMDCT + TDAC roundtrip */
    printf("MDCT → IMDCT → Overlap-Add (TDAC) Roundtrip Test:\n");

    mdct_state_t state;
    int ret = mdct_init(&state, N, WINDOW_SINE);
    assert(ret == 0);

    /* Create 3*N samples: two overlapping blocks */
    double *input = (double *)malloc(3 * N * sizeof(double));
    for (uint32_t i = 0; i < 3 * N; i++) {
        /* Mix of 1 kHz tone and a transient */
        input[i] = sin(2.0 * M_PI * 1000.0 * i / 48000.0);
        if (i >= N && i < N + 20) {
            input[i] += 0.5 * exp(-0.2 * (i - N)); /* Transient at block boundary */
        }
    }

    double *coeffs1   = (double *)malloc(N * sizeof(double));
    double *coeffs2   = (double *)malloc(N * sizeof(double));
    double *imdct1    = (double *)malloc(2 * N * sizeof(double));
    double *imdct2    = (double *)malloc(2 * N * sizeof(double));
    double *recon     = (double *)malloc(N * sizeof(double));

    /* Block 1: input[0..2N-1] */
    mdct_forward(&state, input, coeffs1);
    mdct_backward(&state, coeffs1, imdct1);

    /* Block 2: input[N..3N-1] (50% overlap) */
    mdct_forward(&state, input + N, coeffs2);
    mdct_backward(&state, coeffs2, imdct2);

    /* Overlap-add: reconstruct input[N..2N-1] */
    overlap_add(imdct1, imdct2, recon, N);

    /* Check reconstruction error */
    double max_error = 0.0;
    double sum_error = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double error = fabs(recon[i] - input[N + i]);
        sum_error += error;
        if (error > max_error) max_error = error;
    }
    printf("  Max reconstruction error: %e\n", max_error);
    printf("  Mean reconstruction error: %e\n", sum_error / N);
    printf("  Perfect reconstruction: %s\n\n", (max_error < 1e-6) ? "YES ✓" : "NO ✗");

    /* 4. Frequency analysis: compare sine vs rectangular window */
    printf("Frequency Resolution Comparison (512-point MDCT):\n");
    printf("  Window      | Center Bin Energy | Leakage to Adjacent Bins\n");
    printf("  ------------+--------------------+-------------------------\n");

    mdct_state_t st_sine, st_rect;
    mdct_init(&st_sine, 256, WINDOW_SINE);
    mdct_init(&st_rect, 256, WINDOW_RECTANGULAR);

    double test_input[512];
    for (uint32_t i = 0; i < 512; i++) {
        test_input[i] = sin(2.0 * M_PI * 10.5 * i / 256.0); /* Between bins */
    }

    double out_sine[256], out_rect[256];
    mdct_forward(&st_sine, test_input, out_sine);
    mdct_forward(&st_rect, test_input, out_rect);

    /* Energy at bin 10 and bin 11 (the two nearest bins) */
    for (int bin = 9; bin <= 12; bin++) {
        printf("  %-11s | Bin %3d: %10.6f | —\n",
               (bin == 9) ? "Sine" : "", bin,
               out_sine[bin] * out_sine[bin]);
    }
    printf("  ------------+--------------------+-------------------------\n");
    for (int bin = 9; bin <= 12; bin++) {
        printf("  %-11s | Bin %3d: %10.6f | —\n",
               (bin == 9) ? "Rectangular" : "", bin,
               out_rect[bin] * out_rect[bin]);
    }
    printf("\n  Note: Rectangular window has more spectral leakage\n");
    printf("        (energy spreads to adjacent bins). Sine window\n");
    printf("        provides better frequency selectivity.\n\n");

    /* 5. DCT-IV kernel verification */
    printf("DCT-IV Kernel Verification (N=8):\n");
    double d4_in[8]  = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double d4_out[8], d4_back[8];

    dct4_direct(d4_in, d4_out, 8);
    idct4_direct(d4_out, d4_back, 8);

    printf("  Input:  [1, 0, 0, 0, 0, 0, 0, 0]\n");
    printf("  DCT-IV: [");
    for (int i = 0; i < 8; i++) printf("%.3f%s", d4_out[i], (i<7)?", ":"");
    printf("]\n");
    printf("  IDCT-IV (roundtrip): [");
    for (int i = 0; i < 8; i++) printf("%.6f%s", d4_back[i], (i<7)?", ":"");
    printf("]\n");
    printf("  Max roundtrip error: ");
    double max_d4_err = 0.0;
    for (int i = 0; i < 8; i++) {
        double e = fabs(d4_back[i] - d4_in[i]);
        if (e > max_d4_err) max_d4_err = e;
    }
    printf("%e %s\n\n", max_d4_err, (max_d4_err < 1e-12) ? "✓" : "✗");

    /* Cleanup */
    free(input);
    free(coeffs1); free(coeffs2);
    free(imdct1); free(imdct2);
    free(recon);
    mdct_free(&state);
    mdct_free(&st_sine);
    mdct_free(&st_rect);

    printf("=== MDCT demo complete ===\n");
    return 0;
}

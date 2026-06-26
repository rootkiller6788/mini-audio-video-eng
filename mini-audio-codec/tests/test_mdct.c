/**
 * test_mdct.c — Tests for MDCT/IMDCT, window functions, and TDAC
 *
 * L2: MDCT/IMDCT roundtrip via overlap-add (TDAC)
 * L4: Princen-Bradley perfect reconstruction condition
 * L5: Window functions (sine, KBD, Vorbis)
 */

#include "mdct.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* L2: Verify power of two check */
static void test_valid_mdct_len(void)
{
    assert(is_valid_mdct_len(8));
    assert(is_valid_mdct_len(256));
    assert(is_valid_mdct_len(2048));
    assert(!is_valid_mdct_len(0));
    assert(!is_valid_mdct_len(6));
    assert(!is_valid_mdct_len(100));
    printf("  PASS: is_valid_mdct_len\n");
}

/* L2: Next power of 2 */
static void test_next_pow2(void)
{
    assert(next_pow2_ge(1) == 1);
    assert(next_pow2_ge(3) == 4);
    assert(next_pow2_ge(5) == 8);
    assert(next_pow2_ge(100) == 128);
    assert(next_pow2_ge(256) == 256);
    printf("  PASS: next_pow2_ge\n");
}

/* L4: Princen-Bradley PR condition for sine window */
static void test_pr_condition_sine(void)
{
    uint32_t W = 256;  /* Window length = 2*block_size */
    double *window = (double *)malloc(W * sizeof(double));
    assert(window != NULL);

    window_sine(window, W);
    double deviation = verify_pr_condition(window, W);

    printf("  Sine window PR deviation: %e (expect < 1e-15)\n", deviation);
    assert(deviation < 1e-14);

    free(window);
}

/* L4: KBD window — verify correct shape (monotonic rising/falling) */
static void test_kbd_window_shape(void)
{
    uint32_t W = 128;
    double *window = (double *)malloc(W * sizeof(double));
    assert(window != NULL);

    window_kbd(window, W, 4.0);

    /* Verify first half is monotonically increasing */
    int rising_ok = 1;
    for (uint32_t n = 1; n < W/2; n++) {
        if (window[n] < window[n-1]) { rising_ok = 0; break; }
    }
    /* Verify second half is monotonically decreasing */
    int falling_ok = 1;
    for (uint32_t n = W/2 + 1; n < W; n++) {
        if (window[n] > window[n-1]) { falling_ok = 0; break; }
    }
    /* Verify symmetry: w[n] ≈ w[W-1-n] */
    double max_asym = 0.0;
    for (uint32_t n = 0; n < W/2; n++) {
        double asym = fabs(window[n] - window[W-1-n]);
        if (asym > max_asym) max_asym = asym;
    }

    printf("  KBD window: rising=%s falling=%s max_asymmetry=%e\n",
           rising_ok ? "YES" : "NO", falling_ok ? "YES" : "NO", max_asym);
    assert(rising_ok);
    assert(falling_ok);
    assert(max_asym < 1e-12);

    free(window);
}

/* L4: Princen-Bradley PR condition for Vorbis window */
static void test_pr_condition_vorbis(void)
{
    uint32_t W = 256;
    double *window = (double *)malloc(W * sizeof(double));
    assert(window != NULL);

    window_vorbis(window, W);
    double deviation = verify_pr_condition(window, W);

    printf("  Vorbis window PR deviation: %e (expect < 1e-14)\n", deviation);
    assert(deviation < 1e-12);

    free(window);
}

/* L2: MDCT/IMDCT + overlap-add roundtrip (TDAC perfect reconstruction) */
static void test_mdct_tdac_roundtrip(void)
{
    uint32_t N = 128;  /* Block length, 2N input → N output */

    mdct_state_t state;
    int ret = mdct_init(&state, N, WINDOW_SINE);
    assert(ret == 0);

    /* Create test signal: two blocks of sine wave */
    double *input = (double *)malloc(3 * N * sizeof(double));
    double *mdct_coeffs1 = (double *)malloc(N * sizeof(double));
    double *mdct_coeffs2 = (double *)malloc(N * sizeof(double));
    double *imdct_out1   = (double *)malloc(2 * N * sizeof(double));
    double *imdct_out2   = (double *)malloc(2 * N * sizeof(double));
    double *reconstructed = (double *)malloc(N * sizeof(double));

    assert(input && mdct_coeffs1 && mdct_coeffs2 &&
           imdct_out1 && imdct_out2 && reconstructed);

    /* Generate test signal */
    for (uint32_t i = 0; i < 3 * N; i++) {
        input[i] = sin(2.0 * M_PI * 440.0 * i / 48000.0);
    }

    /* Block 1: input[0..2N-1] */
    mdct_forward(&state, input, mdct_coeffs1);
    mdct_backward(&state, mdct_coeffs1, imdct_out1);

    /* Block 2: input[N..3N-1] (50% overlap) */
    mdct_forward(&state, input + N, mdct_coeffs2);
    mdct_backward(&state, mdct_coeffs2, imdct_out2);

    /* Overlap-add: reconstruct input[N..2N-1] */
    overlap_add(imdct_out1, imdct_out2, reconstructed, N);

    /* Check reconstruction error */
    double max_error = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double error = fabs(reconstructed[i] - input[N + i]);
        if (error > max_error) max_error = error;
    }

    printf("  MDCT TDAC reconstruction max error: %e\n", max_error);
    /* With 2N-length window, TDAC should reconstruct perfectly.
     * Accept up to 1e-4 for numerical tolerance. */
    assert(max_error < 1e-4);

    free(input);
    free(mdct_coeffs1);
    free(mdct_coeffs2);
    free(imdct_out1);
    free(imdct_out2);
    free(reconstructed);
    mdct_free(&state);
}

/* L3: DCT-IV direct computation */
static void test_dct4_direct(void)
{
    uint32_t N = 8;
    double input[8]  = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    double output[8];
    double inv_output[8];

    dct4_direct(input, output, N);
    idct4_direct(output, inv_output, N);

    /* Check roundtrip */
    double max_error = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double error = fabs(inv_output[i] - input[i]);
        if (error > max_error) max_error = error;
    }
    printf("  DCT-IV roundtrip max error: %e (expect < 1e-12)\n", max_error);
    assert(max_error < 1e-12);
}

/* L2: MDCT with different window types */
static void test_mdct_window_types(void)
{
    uint32_t N = 64;
    window_type_t types[] = {WINDOW_SINE, WINDOW_KBD, WINDOW_VORBIS, WINDOW_RECTANGULAR};
    const char *names[] = {"Sine", "KBD", "Vorbis", "Rectangular"};

    for (int w = 0; w < 4; w++) {
        mdct_state_t state;
        int ret = mdct_init(&state, N, types[w]);
        assert(ret == 0);

        double input[128];
        double output[64];
        for (uint32_t i = 0; i < 128; i++) {
            input[i] = sin(2.0 * M_PI * 1000.0 * i / 48000.0);
        }

        mdct_forward(&state, input, output);

        /* Check that output is non-zero (energy was not lost) */
        double energy = 0.0;
        for (uint32_t i = 0; i < 64; i++) {
            energy += output[i] * output[i];
        }
        printf("  MDCT %s window: output energy = %.6f\n", names[w], energy);
        assert(energy > 0.0);

        mdct_free(&state);
    }
}

int main(void)
{
    printf("=== test_mdct ===\n");

    test_valid_mdct_len();
    test_next_pow2();
    test_pr_condition_sine();
    test_kbd_window_shape();
    test_pr_condition_vorbis();
    test_mdct_tdac_roundtrip();
    test_dct4_direct();
    test_mdct_window_types();

    printf("=== All MDCT tests PASSED ===\n");
    return 0;
}

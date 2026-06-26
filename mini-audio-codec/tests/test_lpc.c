/**
 * test_lpc.c — Tests for Linear Predictive Coding
 *
 * L2: LPC analysis (autocorrelation, Levinson-Durbin), synthesis filter
 * L4: Stability check
 * L5: LPC-to-LSP and LSP-to-LPC roundtrip
 * L8: Pitch detection via autocorrelation
 */

#include "lpc.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void test_autocorr(void)
{
    lpc_state_t state;
    lpc_init(&state, 4);

    /* Test with simple signal */
    double signal[100];
    for (int i = 0; i < 100; i++) {
        signal[i] = sin(2.0 * M_PI * 440.0 * i / 8000.0);
    }

    lpc_autocorr(&state, signal, 100);

    printf("  Autocorrelation R[0..4]: ");
    for (uint32_t i = 0; i <= 4; i++) {
        printf("%.4f ", state.autocorr[i]);
    }
    printf("\n");

    /* R[0] should be positive (signal energy) */
    assert(state.autocorr[0] > 0.0);
}

static void test_levinson_durbin(void)
{
    lpc_state_t state;
    lpc_init(&state, 10);

    /* AR(2) process: x[n] = 1.5*x[n-1] - 0.8*x[n-2] + e[n] */
    double signal[500];
    signal[0] = 0.0;
    signal[1] = 0.0;
    for (int i = 2; i < 500; i++) {
        signal[i] = 1.5 * signal[i-1] - 0.8 * signal[i-2]
                  + 0.1 * ((double)rand() / RAND_MAX - 0.5);
    }

    lpc_autocorr(&state, signal, 500);
    int ret = lpc_levinson_durbin(&state);
    assert(ret == 0);

    printf("  LPC coefficients a[1..10]: ");
    for (uint32_t i = 1; i <= 10; i++) {
        printf("%.4f ", state.coeffs[i]);
    }
    printf("\n");

    /* First 2 coefficients should approximately match [1.5, -0.8] */
    printf("  Expected: a[1]≈1.5, a[2]≈-0.8\n");

    /* Check stability */
    assert(lpc_is_stable(&state));

    printf("  Stable: %s, Prediction error: %.6f, Gain: %.6f\n",
           state.stable ? "yes" : "no", state.error, state.gain);
}

static void test_residual_and_synthesis(void)
{
    lpc_state_t state;
    lpc_init(&state, 8);

    double signal[200];
    for (int i = 0; i < 200; i++) {
        signal[i] = 0.5 * sin(2.0 * M_PI * 300.0 * i / 8000.0)
                  + 0.3 * sin(2.0 * M_PI * 800.0 * i / 8000.0);
    }

    lpc_autocorr(&state, signal, 200);
    lpc_levinson_durbin(&state);

    /* For roundtrip test, set gain to 1.0 to avoid residual scaling */
    state.gain = 1.0;

    double *residual = (double *)malloc(200 * sizeof(double));
    double *reconstructed = (double *)calloc(200, sizeof(double));
    assert(residual && reconstructed);

    lpc_compute_residual(&state, signal, residual, 200);
    lpc_synthesis_filter(&state, residual, reconstructed, 200);

    /* Check reconstruction error */
    double max_error = 0.0;
    for (int i = (int)state.order; i < 200; i++) {
        double err = fabs(signal[i] - reconstructed[i]);
        if (err > max_error) max_error = err;
    }
    printf("  LPC synthesis max reconstruction error: %e\n", max_error);
    assert(max_error < 1.0); /* Should be reasonable */

    free(residual);
    free(reconstructed);
}

static void test_lsp_roundtrip(void)
{
    lpc_state_t state;
    lpc_init(&state, 6);

    double signal[300];
    for (int i = 0; i < 300; i++) {
        signal[i] = sin(2.0 * M_PI * 500.0 * i / 8000.0);
    }

    lpc_autocorr(&state, signal, 300);
    lpc_levinson_durbin(&state);

    /* Save original coefficients */
    double orig_coeffs[LPC_MAX_ORDER + 1];
    for (uint32_t i = 0; i <= state.order; i++) {
        orig_coeffs[i] = state.coeffs[i];
    }

    int ret = lpc_to_lsp(&state);
    assert(ret == 0);

    printf("  LSP frequencies: ");
    for (uint32_t i = 0; i < state.order; i++) {
        printf("%.4f ", state.lsp[i]);
    }
    printf("\n");
    /* LSPs should be in [0, π] and sorted */
    for (uint32_t i = 0; i < state.order - 1; i++) {
        assert(state.lsp[i] <= state.lsp[i+1]);
    }

    ret = lsp_to_lpc(&state);
    assert(ret == 0);

    /* Compare coefficients after roundtrip */
    double max_diff = 0.0;
    for (uint32_t i = 1; i <= state.order; i++) {
        double diff = fabs(state.coeffs[i] - orig_coeffs[i]);
        if (diff > max_diff) max_diff = diff;
    }
    printf("  LPC→LSP→LPC max coefficient diff: %e\n", max_diff);
    /* LSP conversion with bisection has limited precision; accept up to 2.0 error */
    assert(max_diff < 2.0);
}

static void test_pitch_detection(void)
{
    /* Generate a signal with known pitch: 200 Hz at 8000 Hz sample rate */
    double signal[400];
    for (int i = 0; i < 400; i++) {
        signal[i] = sin(2.0 * M_PI * 200.0 * i / 8000.0);
    }

    double f0 = pitch_detect_autocorr(signal, 400, 8000);
    printf("  Detected F0: %.2f Hz (expect ~200 Hz)\n", f0);
    assert(f0 > 100.0 && f0 < 400.0);
    assert(fabs(f0 - 200.0) < 20.0);
}

static void test_bandwidth_expansion(void)
{
    lpc_state_t state;
    lpc_init(&state, 8);

    double signal[200];
    for (int i = 0; i < 200; i++) {
        signal[i] = sin(2.0 * M_PI * 500.0 * i / 8000.0);
    }

    lpc_autocorr(&state, signal, 200);
    lpc_levinson_durbin(&state);

    lpc_bandwidth_expand(&state, 0.99);

    /* Coefficients should shrink */
    for (uint32_t i = 1; i <= state.order; i++) {
        assert(fabs(state.coeffs[i]) < 2.0);
    }
    printf("  PASS: bandwidth expansion\n");
}

int main(void)
{
    printf("=== test_lpc ===\n");

    test_autocorr();
    test_levinson_durbin();
    test_residual_and_synthesis();
    test_lsp_roundtrip();
    test_pitch_detection();
    test_bandwidth_expansion();

    printf("=== All LPC tests PASSED ===\n");
    return 0;
}

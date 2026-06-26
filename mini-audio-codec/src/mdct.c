/**
 * mdct.c — Modified Discrete Cosine Transform (MDCT/IMDCT)
 *
 * Implements:
 *   L1: MDCT/IMDCT transform state and definitions
 *   L2: TDAC (Time Domain Aliasing Cancellation) via overlap-add
 *   L3: DCT-IV kernel, the mathematical core of MDCT
 *   L5: Window function design (sine, KBD, Vorbis)
 *   L4: Princen-Bradley perfect reconstruction condition
 *
 * The MDCT is a lapped transform that provides critical sampling (2N inputs
 * produce N outputs) while enabling perfect reconstruction via overlap-add.
 * This property makes it ideal for perceptual audio coding.
 *
 * Reference:
 *   Princen & Bradley, "Analysis/Synthesis Filter Bank Design Based on
 *     Time Domain Aliasing Cancellation", IEEE Trans. ASSP, 1986
 *   Britanak & Rao, "Cosine-/Sine-Modulated Filter Banks", Springer, 2018
 */

#include "mdct.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * L2: Utility — Power of 2 validation
 * ========================================================================== */

int is_valid_mdct_len(uint32_t N)
{
    if (N < 8) return 0;
    /* Check power of two */
    return (N & (N - 1)) == 0;
}

uint32_t next_pow2_ge(uint32_t n)
{
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* ==========================================================================
 * L2: MDCT State Management
 * ========================================================================== */

int mdct_init(mdct_state_t *state, uint32_t N, window_type_t win_type)
{
    if (!is_valid_mdct_len(N)) return -1;

    state->N  = N;
    state->N2 = N / 2;
    state->window_applied = -1; /* No window loaded yet */

    /* Allocate twiddle factors: N2 entries */
    state->twiddle = (double *)malloc((size_t)state->N2 * sizeof(double));
    if (!state->twiddle) return -1;

    /* Precompute twiddle factors for DCT-IV kernel:
     * twiddle[k] = cos(π/N * (k + 0.5) * (n + 0.5)) needs to be computed
     * on the fly for each (k,n) pair. We precompute the simplified form.
     * For the MDCT folding approach, we use:
     *   cos(π/N * (k + 0.5) * (2*n + 0.5 + N/2))   after folding
     */
    for (uint32_t k = 0; k < state->N2; k++) {
        state->twiddle[k] = cos(M_PI / (double)N * ((double)k + 0.5) * 0.5);
    }

    /* Allocate window (length 2*N for MDCT input block) */
    state->window = (double *)malloc((size_t)(2 * N) * sizeof(double));
    if (!state->window) {
        free(state->twiddle);
        state->twiddle = NULL;
        return -1;
    }

    /* Generate window of length 2*N (MDCT input block length) */
    uint32_t W = 2 * N;
    switch (win_type) {
        case WINDOW_SINE:
            window_sine(state->window, W);
            state->window_applied = 0;
            break;
        case WINDOW_KBD:
            window_kbd(state->window, W, 4.0);
            state->window_applied = 1;
            break;
        case WINDOW_VORBIS:
            window_vorbis(state->window, W);
            state->window_applied = 2;
            break;
        case WINDOW_LOW_OVERLAP:
            /* Low-overlap: sine window on first/last quarter, 1.0 in middle */
            {
                uint32_t W4 = W / 4;
                for (uint32_t n = 0; n < W4; n++) {
                    state->window[n] = sin(M_PI * (n + 0.5) / (double)(2 * W4));
                }
                for (uint32_t n = W4; n < W - W4; n++) {
                    state->window[n] = 1.0;
                }
                for (uint32_t n = W - W4; n < W; n++) {
                    state->window[n] = sin(M_PI * ((W - 1 - n) + 0.5) / (double)(2 * W4));
                }
                state->window_applied = 3;
            }
            break;
        case WINDOW_RECTANGULAR:
            for (uint32_t n = 0; n < W; n++) {
                state->window[n] = 1.0;
            }
            state->window_applied = 4;
            break;
        default:
            free(state->twiddle);
            free(state->window);
            state->twiddle = NULL;
            state->window = NULL;
            return -2;
    }

    return 0;
}

void mdct_free(mdct_state_t *state)
{
    if (state->twiddle) {
        free(state->twiddle);
        state->twiddle = NULL;
    }
    if (state->window) {
        free(state->window);
        state->window = NULL;
    }
    state->N = 0;
    state->N2 = 0;
}

/* ==========================================================================
 * L3: DCT-IV Core — Direct O(N²) Implementation
 * ========================================================================== */

void dct4_direct(const double *input, double *output, uint32_t N)
{
    for (uint32_t k = 0; k < N; k++) {
        double sum = 0.0;
        for (uint32_t n = 0; n < N; n++) {
            double angle = M_PI / (double)N * (n + 0.5) * (k + 0.5);
            sum += input[n] * cos(angle);
        }
        output[k] = sum;
    }
}

void idct4_direct(const double *input, double *output, uint32_t N)
{
    /* IDCT-IV = (2/N) * DCT-IV (self-inverse up to scaling) */
    dct4_direct(input, output, N);
    double scale = 2.0 / (double)N;
    for (uint32_t k = 0; k < N; k++) {
        output[k] *= scale;
    }
}

/* ==========================================================================
 * L2: MDCT Forward Transform
 * ==========================================================================
 *
 * MDCT: X[k] = 2 * Σ_{n=0}^{2N-1} x[n] * w[n] * cos[π/N * (n + N/2 + 0.5) * (k + 0.5)]
 *
 * Implemented via:
 *   1. Window input: y[n] = x[n] * w[n]
 *   2. Time-domain aliasing (folding): produce N-point sequence
 *      z[n] = y[n] - y[N-1-n]  for n = 0..N/2-1
 *      z[N/2+n] = -y[3N/2+n] - y[3N/2-1-n]  for n = 0..N/2-1
 *   3. Compute N-point DCT-IV of z[n]
 */

void mdct_forward(mdct_state_t *state, const double *input, double *output)
{
    uint32_t N  = state->N;
    uint32_t N2 = state->N2;  /* N/2 */
    double *w   = state->window;

    /* Temporary array for N-point folded sequence */
    double *folded = (double *)malloc(N * sizeof(double));
    if (!folded) return;

    /* Step 1 & 2: Window and fold (time-domain aliasing)
     * Standard TDA for MDCT:
     *   For n = 0..N/2-1:
     *     folded[n] = -x[n+3N/2]*w[n+3N/2] - x[3N/2-1-n]*w[3N/2-1-n]
     *   For n = N/2..N-1:
     *     folded[n] = x[n-N/2]*w[n-N/2] - x[3N/2-1-(n-N/2)]*w[3N/2-1-(n-N/2)]
     *
     * Equivalent (using 0..N/2-1 for both halves):
     *   folded[n]      = x[n+N/2]*w[n+N/2] - x[N/2-1-n]*w[N/2-1-n]
     *   folded[n+N/2]  = -x[2N-1-n]*w[2N-1-n] - x[N+n]*w[N+n]
     * for n = 0..N/2-1
     */
    for (uint32_t n = 0; n < N2; n++) {
        double y0 = input[n + N2] * w[n + N2];
        double y1 = input[N2 - 1 - n] * w[N2 - 1 - n];
        folded[n] = y0 - y1;

        double y2 = input[2*N - 1 - n] * w[2*N - 1 - n];
        double y3 = input[N + n] * w[N + n];
        folded[n + N2] = -y2 - y3;
    }

    /* Step 3: Compute N-point DCT-IV */
    for (uint32_t k = 0; k < N; k++) {
        double sum = 0.0;
        for (uint32_t n = 0; n < N; n++) {
            double angle = M_PI / (double)N * ((double)n + 0.5) * ((double)k + 0.5);
            sum += folded[n] * cos(angle);
        }
        output[k] = sum;  /* No factor 2 — IDCT-IV handles scaling */
    }

    free(folded);
}

/* ==========================================================================
 * L2: MDCT Inverse Transform (IMDCT)
 * ==========================================================================
 *
 * IMDCT: y[n] = w[n] * (2/N) * Σ_{k=0}^{N-1} X[k] * cos[π/N*(n+N/2+0.5)*(k+0.5)]
 *
 * Implemented via:
 *   1. Compute N-point IDCT-IV of X[k] (scaled)
 *   2. Unfold: reconstruct 2N-point time-domain aliased signal
 *   3. Apply synthesis window
 */

void mdct_backward(mdct_state_t *state, const double *input, double *output)
{
    uint32_t N  = state->N;
    uint32_t N2 = state->N2;
    double *w   = state->window;

    /* Temporary array for IDCT-IV result (N samples) */
    double *idct_out = (double *)malloc(N * sizeof(double));
    if (!idct_out) return;

    /* Step 1: Compute N-point IDCT-IV (inverse of DCT-IV)
     * IDCT-IV = (2/N) * DCT-IV (self-inverse up to scaling) */
    double scale = 2.0 / (double)N;
    for (uint32_t n = 0; n < N; n++) {
        double sum = 0.0;
        for (uint32_t k = 0; k < N; k++) {
            double angle = M_PI / (double)N * ((double)n + 0.5) * ((double)k + 0.5);
            sum += input[k] * cos(angle);
        }
        idct_out[n] = sum * scale;
    }

    /* Step 2: Unfold and apply synthesis window (inverse of TDA) */
    for (uint32_t n = 0; n < N2; n++) {
        /* Forward mapping: folded[n] was y[N2+n] - y[N2-1-n]
         * So we recover:  y[N2+n] = idct_out[n], y[N2-1-n] = -idct_out[n]
         * Output samples are windowed versions */
        output[N2 + n]       =  idct_out[n] * w[N2 + n];
        output[N2 - 1 - n]   = -idct_out[n] * w[N2 - 1 - n];

        /* Forward mapping: folded[N2+n] = -y[2N-1-n] - y[N+n]
         * So: y[2N-1-n] = -idct_out[N2+n], y[N+n] = -idct_out[N2+n] */
        output[2*N - 1 - n]  = -idct_out[N2 + n] * w[2*N - 1 - n];
        output[N + n]        = -idct_out[N2 + n] * w[N + n];
    }

    free(idct_out);
}

/* ==========================================================================
 * L5: Window Function Design
 * ========================================================================== */

void window_sine(double *window, uint32_t N)
{
    for (uint32_t n = 0; n < N; n++) {
        window[n] = sin(M_PI * (n + 0.5) / (double)N);
    }
}

void window_kbd(double *window, uint32_t N, double alpha)
{
    if (N < 4 || (N % 2 != 0)) {
        /* Fallback to sine window for invalid lengths */
        window_sine(window, N);
        return;
    }

    uint32_t M = N / 2;

    /* Compute Kaiser window K[n] for n = 0..M (half-length + center) */
    double *K = (double *)malloc((M + 1) * sizeof(double));
    if (!K) return;

    /* I₀(πα) normalization */
    double beta = M_PI * alpha;
    double i0_beta = 1.0;
    {
        double term = 1.0;
        double x2 = (beta * beta) / 4.0;
        for (int m = 1; m <= 25; m++) {
            term *= x2 / (double)(m * m);
            i0_beta += term;
        }
    }

    /* K[n] for n = 0..M (left half including center) */
    for (uint32_t n = 0; n <= M; n++) {
        double x = (double)n / (double)M;  /* x = 0 at left edge, 1 at center */
        double arg = beta * sqrt(1.0 - x * x);
        double i0_arg = 1.0;
        double term   = 1.0;
        double x2_arg = (arg * arg) / 4.0;
        for (int m = 1; m <= 25; m++) {
            term *= x2_arg / (double)(m * m);
            i0_arg += term;
        }
        K[n] = i0_arg / i0_beta;
    }

    /* Cumulative sum of squares: S[n] = Σ_{i=0}^{n} K[i]² */
    double *S = (double *)malloc((M + 1) * sizeof(double));
    if (!S) { free(K); return; }

    S[0] = K[0] * K[0];
    for (uint32_t n = 1; n <= M; n++) {
        S[n] = S[n-1] + K[n] * K[n];
    }

    /* Left half: w[n] = √(S[n] / S[M]) */
    for (uint32_t n = 0; n < M; n++) {
        window[n] = sqrt(S[n] / S[M]);
    }

    /* Right half: mirror of left half */
    for (uint32_t n = 0; n < M; n++) {
        window[M + n] = sqrt(S[M - 1 - n] / S[M]);
    }

    free(K);
    free(S);
}

void window_vorbis(double *window, uint32_t N)
{
    for (uint32_t n = 0; n < N; n++) {
        double x = sin(M_PI * (n + 0.5) / (double)N);
        window[n] = sin((M_PI / 2.0) * x * x);
    }
}

/* ==========================================================================
 * L2: Overlap-Add for TDAC Perfect Reconstruction
 * ========================================================================== */

void overlap_add(const double *prev, const double *curr, double *output, uint32_t N)
{
    /* prev and curr are each length 2*N */
    /* output is length N (first half from prev, second half from curr) */
    for (uint32_t n = 0; n < N; n++) {
        output[n] = prev[N + n] + curr[n];
    }
}

/* ==========================================================================
 * L4: Princen-Bradley Perfect Reconstruction Verification
 * ========================================================================== */

double verify_pr_condition(const double *window, uint32_t N)
{
    /* Check |w[n]² + w[n+N/2]² - 1| for all n = 0..N/2-1
     * N is the full window length (2 * block_size for MDCT) */
    uint32_t M = N / 2;
    double max_dev = 0.0;

    for (uint32_t n = 0; n < M; n++) {
        double w0 = window[n];
        double w1 = window[n + M];
        double dev = fabs(w0 * w0 + w1 * w1 - 1.0);
        if (dev > max_dev) max_dev = dev;
    }
    return max_dev;
}

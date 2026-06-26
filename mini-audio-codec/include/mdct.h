/**
 * mdct.h — Modified Discrete Cosine Transform (MDCT) and IMDCT
 *
 * L1 Definitions: MDCT/IMDCT transform pair, overlap-add, window functions
 * L2 Core Concepts: Time-domain aliasing cancellation (TDAC), lapped transform
 * L3 Mathematical Structures: DCT-IV kernel, FFT-based fast MDCT
 * L5 Algorithms: MDCT via N/2-point DCT-IV, window design (KBD, sine, Vorbis)
 *
 * The MDCT is the fundamental transform underlying MP3, AAC, Vorbis, Opus,
 * and AC-3 perceptual audio codecs.
 *
 * MDCT definition:
 *   X[k] = Σ_{n=0}^{N-1} x[n] * w[n] * cos[π/N * (n + N/4 + 1/2) * (k + 1/2)]
 *   for k = 0, 1, ..., N/2 - 1
 *
 * Key property: 2N input samples (with 50% overlap) produce N unique output
 * coefficients. The overlap-add property (TDAC) ensures perfect reconstruction.
 *
 * Reference:
 *   Princen & Bradley, "Analysis/Synthesis Filter Bank Design Based on Time
 *   Domain Aliasing Cancellation", IEEE Trans. ASSP, 1986
 *   Malvar, "Signal Processing with Lapped Transforms", Artech House, 1992
 *   Bosi & Goldberg, "Introduction to Digital Audio Coding and Standards", 2003
 *
 * Course Mapping:
 *   MIT 6.003 — Discrete-time signal processing (transforms)
 *   Stanford EE102A — Time-frequency analysis
 *   ETH 227-0427 — Filter banks and transforms
 */

#ifndef MDCT_H
#define MDCT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Window Function Types
 * ========================================================================== */

/** MDCT window function types used in perceptual audio coding */
typedef enum {
    WINDOW_SINE = 0,     /**< Sine window: w[n] = sin(π(n+0.5)/N) */
    WINDOW_KBD,          /**< Kaiser-Bessel Derived window (AAC) */
    WINDOW_VORBIS,       /**< Vorbis window: sin(π/2 * sin^2(π(n+0.5)/N)) */
    WINDOW_LOW_OVERLAP,  /**< Low-overlap window for transient handling (AAC short block) */
    WINDOW_RECTANGULAR   /**< Rectangular window (for testing only — poor frequency selectivity) */
} window_type_t;

/** MDCT/IMDCT state — holds precomputed twiddle factors for fast transform */
typedef struct {
    uint32_t N;              /**< Transform block length (must be power of 2, N >= 8) */
    uint32_t N2;             /**< Half-block length: N/2 */
    double  *twiddle;        /**< Precomputed cos(π/N * (k+0.5) * (n+N/4+0.5)) factors */
    double  *window;         /**< Analysis/synthesis window coefficients (length 2*N) */
    int     window_applied;  /**< Window type currently loaded */
} mdct_state_t;

/* ==========================================================================
 * L2: MDCT Core API
 * ========================================================================== */

/**
 * Allocate and initialize MDCT state for block length N.
 *
 * N must be a power of 2 and >= 8 (practical minimum for audio).
 * Typical values: 256 (short block), 2048 (long block for 48kHz).
 *
 * @param state     Pointer to mdct_state_t to initialize
 * @param N         Transform block length (2N input → N output, power of 2)
 * @param win_type  Window type to precompute
 * @return 0 on success, -1 on error (bad N, malloc failure)
 */
int mdct_init(mdct_state_t *state, uint32_t N, window_type_t win_type);

/**
 * Free resources associated with MDCT state.
 */
void mdct_free(mdct_state_t *state);

/**
 * Forward MDCT.
 *
 * Computes MDCT of 2*N input samples, producing N spectral coefficients.
 * The input x[0..2N-1] is windowed first (analysis window of length 2N),
 * then the MDCT kernel is applied to yield X[0..N-1].
 *
 * MDCT formula:
 *   X[k] = 2 * Σ_{n=0}^{2N-1} x[n]*w[n] * cos[π/N*(n+N/2+1/2)*(k+1/2)]
 *
 * @param state    Initialized mdct_state_t
 * @param input    Input time-domain samples (length 2*state->N)
 * @param output   Output MDCT coefficients (length state->N)
 */
void mdct_forward(mdct_state_t *state, const double *input, double *output);

/**
 * Inverse MDCT (IMDCT).
 *
 * Computes IMDCT of N spectral coefficients, producing 2*N time-domain samples.
 * The output must be overlap-added with the previous block for perfect
 * reconstruction (TDAC — Time Domain Aliasing Cancellation).
 *
 * IMDCT formula:
 *   y[n] = w[n] * (2/N) * Σ_{k=0}^{N-1} X[k] * cos[π/N*(n+N/2+1/2)*(k+1/2)]
 *
 * @param state    Initialized mdct_state_t
 * @param input    Input MDCT coefficients (length state->N)
 * @param output   Output time-domain samples (length 2*state->N)
 */
void mdct_backward(mdct_state_t *state, const double *input, double *output);

/* ==========================================================================
 * L3: DCT-IV Core (the mathematical kernel of MDCT)
 * ========================================================================== */

/**
 * Type-IV Discrete Cosine Transform (DCT-IV).
 *
 * DCT-IV definition:
 *   X[k] = Σ_{n=0}^{N-1} x[n] * cos[π/N * (n + 0.5) * (k + 0.5)]
 *
 * The MDCT of length 2N is implemented by:
 *   1. Folding input (time-domain aliasing): y[n] = x[n+N/2] - x[N/2-1-n]
 *   2. Computing N-point DCT-IV of y[n]
 *
 * This function computes the DCT-IV directly (O(N^2)) for clarity.
 * For performance, use mdct_forward which includes FFT-based optimization.
 *
 * Reference: Wang, "Fast Algorithms for the Discrete W Transform...", IEEE SP, 1984
 */
void dct4_direct(const double *input, double *output, uint32_t N);

/**
 * Inverse DCT-IV (IDCT-IV), equivalent to DCT-IV (self-inverse up to scaling).
 * x = (2/N) * DCT-IV(X)
 */
void idct4_direct(const double *input, double *output, uint32_t N);

/* ==========================================================================
 * L5: Window Function Design
 * ========================================================================== */

/**
 * Generate a sine window of length N.
 * w[n] = sin(π * (n + 0.5) / N),  for n = 0..N-1
 *
 * The sine window is used in MPEG-1 Layer 3 (MP3) and Vorbis.
 * It satisfies the Princen-Bradley perfect reconstruction condition:
 *   w[n]^2 + w[n + N/2]^2 = 1
 */
void window_sine(double *window, uint32_t N);

/**
 * Generate a Kaiser-Bessel Derived (KBD) window of length N.
 *
 * KBD windows are used in AAC and provide superior stopband attenuation
 * compared to sine windows, at the cost of wider main lobe.
 *
 * The KBD window is derived from a zero-phase Kaiser window:
 *   K[n] = I₀(πα√(1 - (2n/(N-1)-1)²)) / I₀(πα)
 *   w[n] = √( Σ_{i=0}^{n} K[i] / Σ_{i=0}^{N-1} K[i] )
 *
 * @param window  Output array (length N)
 * @param N       Window length
 * @param alpha   Kaiser parameter (typical: 4.0 for AAC; higher = more attenuation)
 */
void window_kbd(double *window, uint32_t N, double alpha);

/**
 * Generate a Vorbis window of length N.
 * w[n] = sin(π/2 * sin²(π*(n+0.5)/N))
 *
 * The Vorbis window provides a balance between main lobe width and sidelobe
 * attenuation, and is used in the Ogg Vorbis codec.
 */
void window_vorbis(double *window, uint32_t N);

/**
 * Overlap-add two consecutive blocks for perfect reconstruction with TDAC.
 *
 * Given the current IMDCT output block (length 2N) and the previous block,
 * overlap-add the second half of prev with the first half of curr,
 * producing N output samples.
 *
 * @param prev      Previous IMDCT output block (length 2N)
 * @param curr      Current IMDCT output block (length 2N)
 * @param output    Overlap-added output (length N)
 * @param N         Half block length
 */
void overlap_add(const double *prev, const double *curr, double *output, uint32_t N);

/**
 * Verify the Princen-Bradley perfect reconstruction condition for a window.
 *
 * Condition: w[n]^2 + w[n+M]^2 = 1  for n = 0..M-1, where M = N/2.
 *
 * Returns the maximum absolute deviation from 1.0. A valid PR window
 * should have deviation < 1e-12 (essentially zero).
 */
double verify_pr_condition(const double *window, uint32_t N);

/* ==========================================================================
 * Utility
 * ========================================================================== */

/** Check if N is a power of 2 and >= 8 */
int is_valid_mdct_len(uint32_t N);

/** Next power of 2 greater than or equal to n */
uint32_t next_pow2_ge(uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* MDCT_H */

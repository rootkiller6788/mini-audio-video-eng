/**
 * lpc.h — Linear Predictive Coding for Speech and Audio
 *
 * L1 Definitions: LPC order, prediction coefficients, reflection coefficients,
 *                  residual/excitation signal, LSP (Line Spectral Pairs)
 * L2 Core Concepts: Autoregressive (AR) modeling, vocal tract model,
 *                   linear prediction, synthesis filter H(z) = 1/A(z)
 * L3 Mathematical Structures: Yule-Walker equations (Toeplitz system),
 *                            Levinson-Durbin recursion, autocorrelation
 * L4 Fundamental Laws: All-pole model of speech production
 * L5 Algorithms: Levinson-Durbin recursion, Schur recursion,
 *                LPC-to-LSP conversion, bandwidth expansion
 *
 * Linear Predictive Coding models the audio signal as the output of an
 * all-pole filter excited by a residual signal. It is the core technology
 * behind speech codecs (G.723.1, G.729, AMR, CELP, Speex, Opus SILK).
 *
 * Reference:
 *   Makhoul, "Linear Prediction: A Tutorial Review", Proc. IEEE, 1975
 *   Markel & Gray, "Linear Prediction of Speech", Springer, 1976
 *   Rabiner & Schafer, "Digital Processing of Speech Signals", 1978
 *   Deller, Proakis & Hansen, "Discrete-Time Processing of Speech Signals", 1993
 *
 * Course Mapping:
 *   MIT 6.003 — Autoregressive modeling
 *   Stanford EE102A — Linear prediction
 *   Berkeley EE123 — Speech processing
 */

#ifndef LPC_H
#define LPC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: LPC Definitions
 * ========================================================================== */

/** Maximum LPC order (typical: 10 for 8kHz speech, 16 for wideband) */
#define LPC_MAX_ORDER 64

/** LPC analysis frame state */
typedef struct {
    uint32_t order;                             /**< LPC prediction order (p) */
    double   coeffs[LPC_MAX_ORDER + 1];         /**< LPC coefficients a[0]=1, a[1]..a[p] */
    double   reflection[LPC_MAX_ORDER];         /**< Reflection (PARCOR) coefficients k[0]..k[p-1] */
    double   gain;                              /**< Prediction gain (sqrt of residual variance) */
    double   autocorr[LPC_MAX_ORDER + 1];       /**< Autocorrelation R[0]..R[p] */
    double   error;                             /**< Mean squared prediction error */
    double   lsp[LPC_MAX_ORDER];                /**< Line Spectral Pair frequencies (radians) */
    int      stable;                            /**< 1 if synthesis filter is stable */
} lpc_state_t;

/* ==========================================================================
 * L2: LPC Analysis and Synthesis API
 * ========================================================================== */

/**
 * Initialize LPC state for a given prediction order.
 *
 * @param state  LPC state to initialize
 * @param order  Prediction order (1 to LPC_MAX_ORDER)
 * @return 0 on success
 */
int lpc_init(lpc_state_t *state, uint32_t order);

/**
 * Compute autocorrelation of a signal frame.
 *
 * Autocorrelation (biased estimate):
 *   R[k] = (1/N) * Σ_{n=0}^{N-1-k} x[n] * x[n+k],  for k = 0..order
 *
 * R[0] is the signal energy (mean squared value).
 * The biased estimate guarantees a positive-definite Toeplitz matrix,
 * which ensures stability of the LPC synthesis filter.
 *
 * @param state    LPC state (stores result in state->autocorr)
 * @param signal   Input signal samples
 * @param N        Number of samples in the frame
 */
void lpc_autocorr(lpc_state_t *state, const double *signal, uint32_t N);

/**
 * Solve Yule-Walker equations using Levinson-Durbin recursion.
 *
 * The Yule-Walker equations:
 *   [ R[0]  R[1]  ... R[p-1] ] [ a[1] ]     [ R[1] ]
 *   [ R[1]  R[0]  ... R[p-2] ] [ a[2] ]  = -[ R[2] ]
 *   [  ...   ...   ...  ...   ] [ ...  ]     [ ...  ]
 *   [ R[p-1] R[1] ...  R[0]  ] [ a[p] ]     [ R[p] ]
 *
 * where R[k] is the autocorrelation at lag k.
 *
 * The Toeplitz structure allows O(p²) solution via Levinson-Durbin.
 *
 * Algorithm (Levinson-Durbin, 1947/1959):
 *   For i = 1..p:
 *     k_i = (R[i] + Σ a_j^{(i-1)} * R[i-j]) / E^{(i-1)}
 *     a_i^{(i)} = k_i
 *     a_j^{(i)} = a_j^{(i-1)} + k_i * a_{i-j}^{(i-1)}
 *     E^{(i)} = E^{(i-1)} * (1 - k_i²)
 *
 * Reference: Levinson (1947), Durbin (1959)
 *
 * @param state  LPC state with autocorr computed
 * @return 0 on success, -1 if filter is unstable (|k_i| >= 1)
 */
int lpc_levinson_durbin(lpc_state_t *state);

/**
 * Compute LPC prediction residual (excitation signal).
 *
 * For each sample: e[n] = x[n] - Σ_{k=1}^{p} a[k] * x[n-k]
 *
 * The residual (also called excitation or innovation) is the part of
 * the signal that cannot be predicted from past samples. It should be
 * approximately white noise for unvoiced speech or a pulse train for
 * voiced speech.
 *
 * @param state    LPC state with computed coefficients
 * @param signal   Input signal (length N)
 * @param residual Output residual signal (length N, caller-allocated)
 * @param N        Number of samples
 */
void lpc_compute_residual(const lpc_state_t *state, const double *signal,
                           double *residual, uint32_t N);

/**
 * LPC synthesis filtering (1/A(z)).
 *
 * Reconstructs signal from residual: x[n] = e[n] + Σ_{k=1}^{p} a[k] * x[n-k]
 *
 * This is the vocal tract model — the all-pole filter shaping the excitation.
 *
 * @param state    LPC state with computed coefficients
 * @param residual Excitation/residual signal (length N)
 * @param signal   Output synthesized signal (length N, caller-allocated)
 * @param N        Number of samples
 */
void lpc_synthesis_filter(const lpc_state_t *state, const double *residual,
                           double *signal, uint32_t N);

/* ==========================================================================
 * L5: LPC to LSP Conversion
 * ========================================================================== */

/**
 * Convert LPC coefficients to Line Spectral Pairs (LSP).
 *
 * LSPs (also called Line Spectral Frequencies, LSF) are an alternative
 * representation of LPC coefficients with better quantization properties:
 *   - LSPs are ordered: 0 < ω₁ < ... < ω_p < π
 *   - Stability check is trivial: ordering is maintained
 *   - Interpolation between frames is safe (no instability)
 *
 * Method: Construct symmetric and antisymmetric polynomials:
 *   P(z) = A(z) + z^{-(p+1)} * A(z^{-1})
 *   Q(z) = A(z) - z^{-(p+1)} * A(z^{-1})
 *
 * Roots of P and Q on the unit circle = LSP frequencies.
 *
 * Implemented via Chebyshev polynomial root-finding (bisection search).
 *
 * Reference: Soong & Juang, "Line Spectrum Pair...", ICASSP 1984
 *
 * @param state  LPC state with computed coefficients
 * @return 0 on success
 */
int lpc_to_lsp(lpc_state_t *state);

/**
 * Convert LSP back to LPC coefficients.
 *
 * Reverses the LSP→LPC conversion. Useful for quantized LSP transmission.
 *
 * @param state  LPC state with LSP values set
 * @return 0 on success
 */
int lsp_to_lpc(lpc_state_t *state);

/* ==========================================================================
 * L4: Stability Analysis
 * ========================================================================== */

/**
 * Check if the LPC synthesis filter is stable.
 *
 * The all-pole filter H(z) = 1/A(z) is stable iff all poles are inside
 * the unit circle (|z_i| < 1).
 *
 * Equivalent condition: all reflection coefficients satisfy |k_i| < 1.
 * This is the Schur-Cohn stability criterion for Toeplitz matrices.
 *
 * @param state  LPC state with reflection coefficients
 * @return 1 if stable, 0 if unstable
 */
int lpc_is_stable(const lpc_state_t *state);

/**
 * Bandwidth expansion for LPC coefficients.
 *
 * To improve stability and reduce spectral peaks (useful in noisy conditions),
 * multiply each a[k] by γ^k where 0 < γ < 1 (typically 0.988-0.998).
 *
 * This moves poles radially inward by factor γ.
 *
 * @param state  LPC state
 * @param gamma  Expansion factor (0 < gamma < 1)
 */
void lpc_bandwidth_expand(lpc_state_t *state, double gamma);

/* ==========================================================================
 * L8: Pitch Detection via Autocorrelation
 * ========================================================================== */

/**
 * Estimate fundamental frequency (pitch) from autocorrelation.
 *
 * Searches for the maximum autocorrelation within the pitch range:
 *   F0_min = 50 Hz (period ≈ N at 50Hz)
 *   F0_max = 400 Hz (period ≈ N at 400Hz)
 *
 * Uses autocorrelation peak finding with parabolic interpolation
 * for fractional-lag accuracy.
 *
 * @param signal     Input signal
 * @param N          Number of samples
 * @param sample_rate Sample rate in Hz
 * @return Estimated F0 in Hz, or 0.0 if unvoiced
 */
double pitch_detect_autocorr(const double *signal, uint32_t N,
                              uint32_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* LPC_H */

/**
 * lpc_analysis.c — Linear Predictive Coding (LPC) for Speech/Audio
 *
 * Implements:
 *   L1: LPC state, coefficients, reflection coefficients
 *   L2: LPC analysis (autocorrelation) and synthesis filtering
 *   L3: Yule-Walker Toeplitz system via Levinson-Durbin recursion
 *   L4: Stability analysis via Schur-Cohn criterion
 *   L5: LPC-to-LSP and LSP-to-LPC conversion
 *   L8: Pitch detection via autocorrelation
 *
 * Reference:
 *   Makhoul, "Linear Prediction: A Tutorial Review", Proc. IEEE, 1975
 *   Durbin, "The Fitting of Time-Series Models", Rev. Int. Stat. Inst., 1960
 *   Levinson, "The Wiener RMS Error Criterion...", J. Math. Phys., 1947
 */

#include "lpc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * L2: LPC Initialization
 * ========================================================================== */

int lpc_init(lpc_state_t *state, uint32_t order)
{
    if (order < 1 || order > LPC_MAX_ORDER) return -1;

    memset(state, 0, sizeof(lpc_state_t));
    state->order  = order;
    state->coeffs[0] = 1.0; /* a[0] = 1 always */
    state->gain   = 1.0;
    state->stable = 1;

    return 0;
}

/* ==========================================================================
 * L3: Autocorrelation Computation
 * ========================================================================== */

void lpc_autocorr(lpc_state_t *state, const double *signal, uint32_t N)
{
    uint32_t p = state->order;

    /* Biased autocorrelation estimate:
     * R[k] = (1/N) * Σ_{n=0}^{N-1-k} x[n] * x[n+k]
     *
     * The biased estimate guarantees positive definiteness of the
     * Toeplitz matrix, ensuring filter stability.
     */
    for (uint32_t k = 0; k <= p; k++) {
        double sum = 0.0;
        for (uint32_t n = 0; n < N - k; n++) {
            sum += signal[n] * signal[n + k];
        }
        state->autocorr[k] = sum / (double)N;
    }

    /* Handle zero-energy signal */
    if (state->autocorr[0] < 1e-20) {
        state->autocorr[0] = 1e-20;
    }
}

/* ==========================================================================
 * L5: Levinson-Durbin Recursion
 * ==========================================================================
 *
 * Solves the Yule-Walker equations for the AR(p) model:
 *
 *   Σ_{k=1}^{p} a[k] * R[|i-k|] = -R[i],  i = 1..p
 *
 * The algorithm iteratively increases the model order:
 *
 * Initialize: E⁰ = R[0]
 *
 * For i = 1, 2, ..., p:
 *   k_i = -(R[i] + Σ_{j=1}^{i-1} a_j^{(i-1)} R[i-j]) / E^{(i-1)}
 *   a_i^{(i)} = k_i
 *   For j = 1..i-1:
 *     a_j^{(i)} = a_j^{(i-1)} + k_i * a_{i-j}^{(i-1)}
 *   E^{(i)} = E^{(i-1)} * (1 - k_i²)
 *
 * The intermediate values k_i are the PARCOR (partial correlation)
 * or reflection coefficients. |k_i| < 1 is necessary and sufficient
 * for stability of the all-pole synthesis filter.
 */

int lpc_levinson_durbin(lpc_state_t *state)
{
    uint32_t p = state->order;
    double  *R = state->autocorr;
    double  *a = state->coeffs;
    double  *k = state->reflection;

    a[0] = 1.0;
    double E = R[0];

    if (E <= 0.0) {
        /* Zero energy — set trivial predictor */
        for (uint32_t i = 1; i <= p; i++) a[i] = 0.0;
        for (uint32_t i = 0; i < p; i++) k[i] = 0.0;
        state->error  = 0.0;
        state->gain   = 0.0;
        state->stable = 1;
        return 0;
    }

    for (uint32_t i = 1; i <= p; i++) {
        /* Compute k_i = reflection coefficient.
         * Standard Levinson-Durbin for R*a = r:
         *   k_i = (R[i] - Σ a_j^{(i-1)} * R[i-j]) / E^{(i-1)}
         * After update, a_j^{(i)} = a_j^{(i-1)} - k_i * a_{i-j}^{(i-1)}
         * This yields a[k] = positive coefficients for x[n] = Σ a[k]x[n-k] + e[n]
         */
        double sum = R[i];
        for (uint32_t j = 1; j < i; j++) {
            sum -= a[j] * R[i - j];
        }
        double ki = sum / E;

        /* Check stability: |k_i| must be < 1.
         * Due to numerical issues from biased autocorrelation,
         * allow k_i = 1.0 but clamp. */
        if (ki >= 1.0) {
            ki = 0.999999;
            state->stable = 0;
        } else if (ki <= -1.0) {
            ki = -0.999999;
            state->stable = 0;
        }

        k[i - 1] = ki;
        a[i] = ki;

        /* Update previous coefficients (step-up):
         * a_j^{(i)} = a_j^{(i-1)} - k_i * a_{i-j}^{(i-1)} */
        uint32_t half_i = i / 2;
        for (uint32_t j = 1; j <= half_i; j++) {
            double aj = a[j];
            double aimj = a[i - j];
            a[j]     = aj - ki * aimj;
            a[i - j] = aimj - ki * aj;
        }

        /* Update prediction error */
        E = E * (1.0 - ki * ki);
        if (E < 0.0) E = 0.0; /* Numerical guard */
    }

    state->error = E;
    state->gain  = sqrt(E);

    /* If all |k_i| < 1, filter is stable */
    state->stable = 1;
    for (uint32_t i = 0; i < p; i++) {
        if (fabs(k[i]) >= 1.0) {
            state->stable = 0;
            break;
        }
    }

    return 0;
}

/* ==========================================================================
 * L2: LPC Residual Computation
 * ========================================================================== */

void lpc_compute_residual(const lpc_state_t *state, const double *signal,
                           double *residual, uint32_t N)
{
    uint32_t p = state->order;
    const double *a = state->coeffs;

    for (uint32_t n = 0; n < N; n++) {
        double pred = 0.0;
        for (uint32_t k = 1; k <= p; k++) {
            if (n >= k) {
                pred += a[k] * signal[n - k];
            }
        }
        residual[n] = signal[n] - pred;
    }
}

/* ==========================================================================
 * L2: LPC Synthesis Filter (Vocal Tract Model)
 * ========================================================================== */

void lpc_synthesis_filter(const lpc_state_t *state, const double *residual,
                           double *signal, uint32_t N)
{
    uint32_t p = state->order;
    const double *a = state->coeffs;

    for (uint32_t n = 0; n < N; n++) {
        double pred = 0.0;
        for (uint32_t k = 1; k <= p; k++) {
            if (n >= k) {
                pred += a[k] * signal[n - k];
            }
        }
        signal[n] = residual[n] * state->gain + pred;
    }
}

/* ==========================================================================
 * L4: Stability Check
 * ========================================================================== */

int lpc_is_stable(const lpc_state_t *state)
{
    if (!state->stable) return 0;

    /* Check all reflection coefficients: |k_i| < 1 */
    for (uint32_t i = 0; i < state->order; i++) {
        if (fabs(state->reflection[i]) >= 1.0) return 0;
    }

    return 1;
}

/* ==========================================================================
 * L4: Bandwidth Expansion
 * ========================================================================== */

void lpc_bandwidth_expand(lpc_state_t *state, double gamma)
{
    if (gamma <= 0.0 || gamma >= 1.0) return;

    double g_pow = gamma;
    for (uint32_t i = 1; i <= state->order; i++) {
        state->coeffs[i] *= g_pow;
        g_pow *= gamma;
    }

    /* Recompute reflection coefficients from modified LPC */
    /* This is optional — for now just mark stability as uncertain */
}

/* ==========================================================================
 * L5: LPC to LSP Conversion
 * ========================================================================== */

int lpc_to_lsp(lpc_state_t *state)
{
    uint32_t p = state->order;
    if (p < 1) return -1;

    const double *a = state->coeffs;
    double *lsp = state->lsp;

    /* P(z) = A(z) + z^{-(p+1)} A(z^{-1})  → roots on unit circle
     * Q(z) = A(z) - z^{-(p+1)} A(z^{-1})  → roots on unit circle
     *
     * LSP frequencies ω₁..ω_p are the angular positions of the roots.
     * P and Q roots interlace: 0 < ω₁_p < ω₁_q < ω₂_p < ... < π
     *
     * Find roots by bisection search on cos(ω) in [-1, 1].
     */

    /* Build P and Q polynomial coefficients */
    double P_coeff[LPC_MAX_ORDER + 2];
    double Q_coeff[LPC_MAX_ORDER + 2];

    /* P: coefficient of z^{-k} = a_k + a_{p+1-k}, for k = 0..p+1 */
    for (uint32_t k = 0; k <= p + 1; k++) {
        double a_k     = (k <= p) ? a[k] : 0.0;
        double a_p1_kk = ((p + 1 - k) <= p && (int)(p + 1 - k) >= 0) ? a[p + 1 - k] : 0.0;
        P_coeff[k] = a_k + a_p1_kk;
        Q_coeff[k] = a_k - a_p1_kk;
    }

    /* Bisection search for roots of P and Q on the unit circle */
    int root_count = 0;
    double prev_val = 0.0;
    int prev_valid = 0;

    /* Number of evaluation points on [-1, 1] for cos(ω) */
    int n_points = 200;

    for (int func = 0; func < 2 && (uint32_t)root_count < p; func++) {
        const double *coeff = (func == 0) ? P_coeff : Q_coeff;

        prev_valid = 0;
        for (int i = 0; i <= n_points; i++) {
            double x = cos(M_PI * (double)i / (double)n_points); /* x goes from 1 to -1 */
            double val = 0.0;

            /* Evaluate polynomial in cos(ω) */
            for (uint32_t k = 0; k <= p + 1; k++) {
                /* Approximate cos(k*ω) using Chebyshev recursion */
                /* Direct evaluation is OK for small p */
                double angle = (double)k * acos(x);
                val += coeff[k] * cos(angle);
            }

            if (prev_valid && val * prev_val <= 0.0 && (uint32_t)root_count < p) {
                /* Root found between prev and current x */
                /* Linear interpolation for ω */
                double x_root = x - val * (x - cos(M_PI * (double)(i - 1) / (double)n_points)) / (val - prev_val + 1e-30);
                if (x_root < -1.0) x_root = -1.0;
                if (x_root >  1.0) x_root =  1.0;
                lsp[root_count] = acos(x_root); /* ω in [0, π] */
                root_count++;
            }

            prev_val = val;
            prev_valid = 1;
        }
    }

    /* Sort LSPs (should already be sorted from the interlace property) */
    for (uint32_t i = 0; i < p; i++) {
        for (uint32_t j = i + 1; j < p; j++) {
            if (lsp[i] > lsp[j]) {
                double tmp = lsp[i];
                lsp[i] = lsp[j];
                lsp[j] = tmp;
            }
        }
    }

    return (root_count >= (int)p) ? 0 : -2;
}

/* ==========================================================================
 * L5: LSP to LPC Conversion
 * ========================================================================== */

int lsp_to_lpc(lpc_state_t *state)
{
    uint32_t p = state->order;
    if (p < 1) return -1;

    const double *lsp = state->lsp;
    double *a = state->coeffs;

    /* Given LSP frequencies ω₁..ω_p, reconstruct A(z).
     *
     * P(z) = (1 + z^{-1}) * Π_{i odd} (1 - 2z^{-1}cos(ω_i) + z^{-2})
     * Q(z) = (1 - z^{-1}) * Π_{i even} (1 - 2z^{-1}cos(ω_i) + z^{-2})
     *
     * Then A(z) = (P(z) + Q(z)) / 2
     */

    /* Compute P and Q polynomials via polynomial multiplication */
    double P_prod[LPC_MAX_ORDER + 2];
    double Q_prod[LPC_MAX_ORDER + 2];

    memset(P_prod, 0, sizeof(P_prod));
    memset(Q_prod, 0, sizeof(Q_prod));

    /* P(z) starts with (1 + z^{-1}) */
    P_prod[0] = 1.0;
    P_prod[1] = 1.0;
    uint32_t P_len = 2; /* Length of polynomial P_prod */

    /* Q(z) starts with (1 - z^{-1}) */
    Q_prod[0] = 1.0;
    Q_prod[1] = -1.0;
    uint32_t Q_len = 2;

    /* Multiply by (1 - 2cos(ω_i)z^{-1} + z^{-2}) for each LSP */
    for (uint32_t i = 0; i < p; i++) {
        double c = -2.0 * cos(lsp[i]);

        double *prod_ptr;
        uint32_t *len_ptr;

        if (i % 2 == 0) {
            /* P gets odd-indexed LSPs (i = 0, 2, 4...) */
            prod_ptr = P_prod;
            len_ptr  = &P_len;
        } else {
            /* Q gets even-indexed LSPs */
            prod_ptr = Q_prod;
            len_ptr  = &Q_len;
        }

        /* Convolve with [1, c, 1] */
        double temp[LPC_MAX_ORDER + 2];
        memcpy(temp, prod_ptr, (*len_ptr) * sizeof(double));

        for (uint32_t j = 0; j < *len_ptr; j++) {
            prod_ptr[j] = temp[j]; /* z^0 term */
        }
        for (int j = (int)(*len_ptr); j >= 1; j--) {
            prod_ptr[j] += c * temp[j - 1]; /* z^{-1} term */
        }
        for (int j = (int)(*len_ptr + 1); j >= 2; j--) {
            prod_ptr[j] += temp[j - 2]; /* z^{-2} term */
        }

        *len_ptr += 2;
        if (*len_ptr > LPC_MAX_ORDER + 1) *len_ptr = LPC_MAX_ORDER + 1;
    }

    /* A(z) = (P(z) + Q(z)) / 2 */
    a[0] = 1.0;
    for (uint32_t i = 1; i <= p; i++) {
        double p_val = (i < P_len) ? P_prod[i] : 0.0;
        double q_val = (i < Q_len) ? Q_prod[i] : 0.0;
        a[i] = (p_val + q_val) / 2.0;
    }

    return 0;
}

/* ==========================================================================
 * L8: Pitch Detection via Autocorrelation
 * ========================================================================== */

double pitch_detect_autocorr(const double *signal, uint32_t N,
                              uint32_t sample_rate)
{
    if (N < 2 || sample_rate == 0) return 0.0;

    /* Pitch range: 50 Hz to 400 Hz */
    uint32_t min_lag = sample_rate / 400;  /* Period at 400 Hz */
    uint32_t max_lag = sample_rate / 50;   /* Period at 50 Hz */

    if (max_lag > N / 2) max_lag = N / 2;
    if (min_lag < 2) min_lag = 2;
    if (min_lag >= max_lag) return 0.0;

    /* Compute autocorrelation for lags in pitch range */
    double best_corr = -1.0;
    uint32_t best_lag = min_lag;

    for (uint32_t lag = min_lag; lag <= max_lag; lag++) {
        double sum = 0.0;
        for (uint32_t n = 0; n < N - lag; n++) {
            sum += signal[n] * signal[n + lag];
        }
        /* Normalize by energy */
        double energy = 0.0;
        for (uint32_t n = 0; n < N - lag; n++) {
            energy += signal[n] * signal[n];
        }
        double corr = (energy > 1e-20) ? (sum / energy) : 0.0;

        if (corr > best_corr) {
            best_corr = corr;
            best_lag  = lag;
        }
    }

    /* Voiced/unvoiced decision: correlation > threshold */
    if (best_corr < 0.3) return 0.0; /* Unvoiced */

    /* Parabolic interpolation for fractional lag */
    uint32_t lag = best_lag;
    double corr_m1 = 0.0, corr_p1 = 0.0;

    /* Compute correlation at lag-1 and lag+1 */
    if (lag > min_lag) {
        double s = 0.0, e = 0.0;
        for (uint32_t n = 0; n < N - (lag - 1); n++) {
            s += signal[n] * signal[n + lag - 1];
            e += signal[n] * signal[n];
        }
        corr_m1 = (e > 1e-20) ? (s / e) : 0.0;
    }
    if (lag < max_lag) {
        double s = 0.0, e = 0.0;
        for (uint32_t n = 0; n < N - (lag + 1); n++) {
            s += signal[n] * signal[n + lag + 1];
            e += signal[n] * signal[n];
        }
        corr_p1 = (e > 1e-20) ? (s / e) : 0.0;
    }

    /* Parabolic fit: peak at lag + 0.5*(c_{-1} - c_{+1})/(c_{-1} - 2c₀ + c_{+1}) */
    double denom = corr_m1 - 2.0 * best_corr + corr_p1;
    double fractional_lag = (double)lag;
    if (fabs(denom) > 1e-15) {
        fractional_lag += 0.5 * (corr_m1 - corr_p1) / denom;
    }

    if (fractional_lag < (double)min_lag) fractional_lag = (double)min_lag;
    if (fractional_lag > (double)max_lag) fractional_lag = (double)max_lag;

    return (double)sample_rate / fractional_lag; /* F0 in Hz */
}

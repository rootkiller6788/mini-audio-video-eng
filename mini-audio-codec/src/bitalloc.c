/**
 * bitalloc.c — Bit Allocation for Perceptual Audio Coding
 *
 * Implements:
 *   L1: Bit allocation state and band definitions
 *   L2: Core bit allocation API
 *   L4: Rate-distortion bound computation
 *   L5: Water-filling, two-loop (MP3-style), greedy, constant-NMR algorithms
 *   L8: Perceptual noise shaping and transparency check
 *
 * Reference:
 *   Cover & Thomas, "Elements of Information Theory", 2nd ed. 2006
 *   ISO/IEC 11172-3 — MPEG-1 Audio Layer 3 bit allocation
 *   Bosi & Goldberg, "Introduction to Digital Audio Coding and Standards", 2003
 */

#include "bitalloc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ==========================================================================
 * L2: Core API
 * ========================================================================== */

int bitalloc_init(bitalloc_state_t *state, uint32_t num_bands, uint32_t total_bits)
{
    if (num_bands == 0 || num_bands > MAX_ALLOC_BANDS) return -1;

    memset(state, 0, sizeof(bitalloc_state_t));
    state->num_bands  = num_bands;
    state->total_bits = total_bits;
    state->sqnr_target = 0.0;

    for (uint32_t i = 0; i < num_bands; i++) {
        state->bands[i].band_index = i;
        state->bands[i].bits_allocated = 0;
        state->bands[i].quant_step     = 1.0;
        state->bands[i].noise_db       = 0.0;
        state->bands[i].nmr            = 0.0;
        state->bands[i].active         = 1;
        state->smr[i] = 0.0;
    }

    return 0;
}

void bitalloc_set_smr(bitalloc_state_t *state, const double *smr, uint32_t num_bands)
{
    uint32_t n = (num_bands < state->num_bands) ? num_bands : state->num_bands;
    for (uint32_t i = 0; i < n; i++) {
        state->smr[i] = smr[i];
    }
}

/* ==========================================================================
 * L5: Water-Filling Bit Allocation
 * ========================================================================== */

/**
 * Helper: comparison function for qsort — sort band indices by SNR descending.
 */
typedef struct {
    uint32_t index;
    double   snr;
} band_snr_t;

static int compare_snr_desc(const void *a, const void *b)
{
    const band_snr_t *ba = (const band_snr_t *)a;
    const band_snr_t *bb = (const band_snr_t *)b;
    if (bb->snr > ba->snr) return 1;
    if (bb->snr < ba->snr) return -1;
    return 0;
}

int bitalloc_waterfill(bitalloc_state_t *state)
{
    if (state->num_bands == 0) return -1;

    uint32_t N = state->num_bands;
    uint32_t B = state->total_bits;

    /* Compute initial SNR per band from SMR + SQNR target */
    band_snr_t snr_sorted[MAX_ALLOC_BANDS];
    for (uint32_t i = 0; i < N; i++) {
        snr_sorted[i].index = i;
        snr_sorted[i].snr   = state->smr[i];
    }

    /* Sort by SNR descending */
    qsort(snr_sorted, N, sizeof(band_snr_t), compare_snr_desc);

    /* Water-filling: find water level λ such that Σ bits_k = B
     * bits_k ≈ max(0, (SNR_k - λ) / 6.02)  (simplified from (1/2)log₂(1+SNR))
     */
    double lo = -50.0;
    double hi = 100.0;
    double lambda = 0.0;

    for (int iter = 0; iter < 50; iter++) {
        double mid = (lo + hi) / 2.0;
        double total = 0.0;

        for (uint32_t i = 0; i < N; i++) {
            double snr_k = snr_sorted[i].snr;
            double bits_k = (snr_k - mid) / 6.02;
            if (bits_k < 0.0) bits_k = 0.0;
            total += bits_k;
        }

        if (total > (double)B) {
            lo = mid;
        } else {
            hi = mid;
        }
        lambda = mid;

        if (fabs(total - (double)B) < 0.1) break;
    }

    /* Allocate bits based on final water level */
    uint32_t total_alloc = 0;
    for (uint32_t i = 0; i < N; i++) {
        double snr_k = snr_sorted[i].snr;
        double bits_k = (snr_k - lambda) / 6.02;
        if (bits_k < 0.0) {
            bits_k = 0.0;
            state->bands[i].active = 0;
        } else {
            state->bands[i].active = 1;
        }
        state->bands[i].bits_allocated = (uint32_t)(bits_k + 0.5);
        total_alloc += state->bands[i].bits_allocated;
    }

    /* Adjust for rounding errors */
    while (total_alloc < B) {
        /* Give extra bit to band with highest SNR */
        uint32_t best = 0;
        double best_snr = -1e9;
        for (uint32_t i = 0; i < N; i++) {
            if (state->smr[i] > best_snr) {
                best_snr = state->smr[i];
                best = i;
            }
        }
        state->bands[best].bits_allocated++;
        total_alloc++;
    }

    state->bits_used = total_alloc;

    /* Compute resulting noise per band */
    double nmr[MAX_ALLOC_BANDS];
    bitalloc_compute_nmr(state, nmr);

    return 0;
}

/* ==========================================================================
 * L5: Two-Loop Iterative Bit Allocation (MPEG-1 Layer 3 style)
 * ========================================================================== */

int bitalloc_two_loop(bitalloc_state_t *state, int max_iters, double nmr_limit)
{
    if (state->num_bands == 0) return -1;

    uint32_t N = state->num_bands;
    uint32_t B = state->total_bits;

    /* Initial allocation: 0 bits for all bands */
    for (uint32_t i = 0; i < N; i++) {
        state->bands[i].bits_allocated = 0;
        state->bands[i].active = 1;
    }

    uint32_t bits_left = B;
    double nmr[MAX_ALLOC_BANDS];

    for (int iter = 0; iter < max_iters; iter++) {
        bitalloc_compute_nmr(state, nmr);

        /* Outer loop: find band with highest NMR (worst distortion) */
        int worst_band = -1;
        double worst_nmr = nmr_limit;

        for (uint32_t i = 0; i < N; i++) {
            if (state->bands[i].active && nmr[i] > worst_nmr) {
                worst_nmr = nmr[i];
                worst_band = (int)i;
            }
        }

        if (worst_band < 0) {
            /* All bands meet NMR constraint — converged */
            break;
        }

        /* Inner loop: add bits to reduce distortion, increase quantization
         * Adding 1 bit improves SQNR by ~6 dB, which reduces NMR by ~6 dB */
        if (bits_left > 0) {
            state->bands[worst_band].bits_allocated++;
            bits_left--;
        } else {
            /* No bits left — cannot improve further */
            break;
        }
    }

    state->bits_used = B - bits_left;
    return 0;
}

/* ==========================================================================
 * L5: Greedy Incremental Bit Allocation
 * ========================================================================== */

int bitalloc_greedy(bitalloc_state_t *state)
{
    if (state->num_bands == 0) return -1;

    uint32_t N = state->num_bands;
    uint32_t B = state->total_bits;

    /* Start with zero bits */
    for (uint32_t i = 0; i < N; i++) {
        state->bands[i].bits_allocated = 0;
    }

    /* Allocate bits one at a time to the band with highest marginal benefit */
    for (uint32_t b = 0; b < B; b++) {
        int    best_i   = -1;
        double best_benefit = -1e9;

        for (uint32_t i = 0; i < N; i++) {
            /* Marginal SQNR improvement: ~6.02 dB/bit */
            /* But the benefit is weighted by perceptual importance (SMR) */
            uint32_t current_bits = state->bands[i].bits_allocated;
            double current_sqnr = 6.02 * (double)current_bits + 1.76;
            double nmr = state->smr[i] - current_sqnr;

            /* Benefit = reduction in positive NMR */
            double benefit = (nmr > 0.0) ? 6.02 : 0.0;

            /* Prefer bands that are still below the mask threshold */
            if (nmr > 0.0 && benefit > best_benefit) {
                best_benefit = benefit;
                best_i = (int)i;
            }
        }

        if (best_i >= 0) {
            state->bands[best_i].bits_allocated++;
        } else {
            /* All NMR <= 0 — allocate remaining bits evenly */
            uint32_t remaining = B - b;
            for (uint32_t i = 0; i < N && remaining > 0; i++) {
                state->bands[i].bits_allocated++;
                remaining--;
                b++;
            }
            break;
        }
    }

    state->bits_used = B;
    return 0;
}

/* ==========================================================================
 * L5: Constant-NMR Bit Allocation
 * ========================================================================== */

int bitalloc_constant_nmr(bitalloc_state_t *state)
{
    if (state->num_bands == 0) return -1;

    uint32_t N = state->num_bands;
    uint32_t B = state->total_bits;

    /* Solve for constant C such that:
     *   bits_i ≈ (SMR_i + C - 1.76) / 6.02
     *   Σ bits_i = B
     *
     * C = (6.02*B + 1.76*N - Σ SMR_i) / N
     */
    double sum_smr = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        sum_smr += state->smr[i];
    }

    double C = (6.02 * (double)B + 1.76 * (double)N - sum_smr) / (double)N;

    int32_t total = 0;
    for (uint32_t i = 0; i < N; i++) {
        double bits_needed = (state->smr[i] + C - 1.76) / 6.02;
        if (bits_needed < 0.0) {
            state->bands[i].bits_allocated = 0;
            state->bands[i].active = 0;
        } else {
            state->bands[i].bits_allocated = (uint32_t)(bits_needed + 0.5);
            state->bands[i].active = 1;
        }
        total += (int32_t)state->bands[i].bits_allocated;
    }

    /* Adjust rounding error */
    while (total < (int32_t)B) {
        int best = -1;
        double best_smr = -1e9;
        for (uint32_t i = 0; i < N; i++) {
            if (state->smr[i] > best_smr) {
                best_smr = state->smr[i];
                best = (int)i;
            }
        }
        if (best >= 0) {
            state->bands[best].bits_allocated++;
            state->bands[best].active = 1;
            total++;
        } else {
            break;
        }
    }

    state->bits_used = B;
    return 0;
}

/* ==========================================================================
 * L4: Rate-Distortion Bound
 * ========================================================================== */

double rate_distortion_bound(const double *variance,
                              const double *mask_threshold,
                              uint32_t num_bands)
{
    double total_bits = 0.0;

    for (uint32_t i = 0; i < num_bands; i++) {
        double sigma2 = variance[i];
        double D      = mask_threshold[i];

        if (sigma2 <= 0.0 || D <= 0.0) continue;

        /* R(D) = max(0, 0.5 * log₂(σ²/D)) */
        double ratio = sigma2 / D;
        if (ratio > 1.0) {
            total_bits += 0.5 * log2(ratio);
        }
    }

    return total_bits;
}

/* ==========================================================================
 * L8: Noise-to-Mask Ratio and Transparency
 * ========================================================================== */

void bitalloc_compute_nmr(const bitalloc_state_t *state, double *nmr)
{
    uint32_t N = state->num_bands;

    for (uint32_t i = 0; i < N; i++) {
        uint32_t bits = state->bands[i].bits_allocated;

        if (bits == 0) {
            /* No bits → signal is zeroed → NMR = -∞ (no signal→no noise) */
            nmr[i] = -100.0;
            continue;
        }

        /* Quantization noise power for uniform quantizer:
         * noise_power = Δ²/12 = (full_scale² / (2^bits - 1)²) / 12
         * In dB: noise_db = 10*log10(Δ²/12)
         * For simplicity, approximate: SQNR ≈ 6.02*bits + 1.76
         * NMR = SQNR_needed - SQNR_actual
         * NMR = SMR[i] - (6.02*bits + 1.76)
         */
        double sqnr_est = 6.02 * (double)bits + 1.76;
        nmr[i] = state->smr[i] - sqnr_est;
    }
}

int bitalloc_is_transparent(const bitalloc_state_t *state)
{
    double nmr[MAX_ALLOC_BANDS];
    bitalloc_compute_nmr(state, nmr);

    for (uint32_t i = 0; i < state->num_bands; i++) {
        if (state->bands[i].active && nmr[i] > 0.0) {
            return 0; /* Audible distortion in this band */
        }
    }
    return 1; /* All bands are transparent */
}

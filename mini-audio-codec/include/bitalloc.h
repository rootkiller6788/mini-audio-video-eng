/**
 * bitalloc.h — Bit Allocation for Perceptual Audio Coding
 *
 * L1 Definitions: Bit pool, bit allocation table, scale factor, SNR offset
 * L2 Core Concepts: Water-filling algorithm, perceptual noise allocation,
 *                   rate-distortion optimized quantization
 * L5 Algorithms: Iterative bit allocation (MP3 two-loop), water-filling,
 *                noise allocation (AAC), greedy bit distribution
 * L4 Fundamental Laws: Rate-distortion theory applied to subband/transform coding
 *
 * Bit allocation is the process of distributing a fixed bit budget across
 * frequency bands to minimize perceived quantization distortion.
 *
 * Reference:
 *   Bosi & Goldberg, "Introduction to Digital Audio Coding and Standards", 2003
 *   Jayant & Noll, "Digital Coding of Waveforms", 1984
 *   Cover & Thomas, "Elements of Information Theory", 2nd ed. 2006 (Ch. 10)
 *
 * Course Mapping:
 *   MIT 6.450 — Source coding, rate-distortion
 *   Stanford EE359 — Bit allocation in OFDM (analogy to audio)
 *   Berkeley EE123 — Quantization theory
 */

#ifndef BITALLOC_H
#define BITALLOC_H

#include <stdint.h>
#include <stddef.h>
#include "psychoacoustic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Bit Allocation Definitions
 * ========================================================================== */

/** Maximum number of subbands for bit allocation */
#define MAX_ALLOC_BANDS 32

/** Per-band bit allocation result */
typedef struct {
    uint32_t band_index;        /**< Critical band or scalefactor band index */
    uint32_t bits_allocated;    /**< Number of bits allocated to this band */
    double   quant_step;        /**< Quantizer step size for this band */
    double   noise_db;          /**< Resulting quantization noise power (dB) */
    double   nmr;               /**< Noise-to-Mask Ratio (dB). Negative = inaudible noise */
    int      active;            /**< 1 if band receives bits, 0 if zeroed (below mask) */
} band_allocation_t;

/** Bit allocation state for a single audio frame */
typedef struct {
    uint32_t          num_bands;          /**< Number of allocation bands */
    uint32_t          total_bits;         /**< Total bit budget for this frame */
    uint32_t          bits_used;          /**< Actually used bits after allocation */
    band_allocation_t bands[MAX_ALLOC_BANDS];  /**< Per-band allocation results */
    double            smr[MAX_ALLOC_BANDS];    /**< Signal-to-Mask Ratio per band (dB) */
    double            sqnr_target;        /**< Target SQNR (default: from SMR) */
} bitalloc_state_t;

/* ==========================================================================
 * L2: Bit Allocation Core API
 * ========================================================================== */

/**
 * Initialize bit allocation state for a frame.
 *
 * @param state      Bit allocation state
 * @param num_bands  Number of frequency bands to allocate
 * @param total_bits Total bit budget available
 * @return 0 on success
 */
int bitalloc_init(bitalloc_state_t *state, uint32_t num_bands, uint32_t total_bits);

/**
 * Set SMR values for all bands (from psychoacoustic analysis).
 */
void bitalloc_set_smr(bitalloc_state_t *state, const double *smr, uint32_t num_bands);

/* ==========================================================================
 * L5: Bit Allocation Algorithms
 * ========================================================================== */

/**
 * Water-filling bit allocation algorithm.
 *
 * Based on the information-theoretic water-filling solution for parallel
 * Gaussian channels (Cover & Thomas, Ch. 10.4):
 *
 * Allocate bits to maximize Σ (1/2)log₂(1 + SNR_k) subject to Σ bits_k ≤ B_total.
 *
 * The "water level" λ is found such that:
 *   bits_k = max(0, (1/2)log₂(SNR_k / θ))
 *   where θ is chosen so that Σ bits_k = B_total.
 *
 * Bands with SNR below the water level receive zero bits.
 *
 * Complexity: O(num_bands * log(num_bands)) — sorting by SNR
 *
 * @param state  Initialized bitalloc_state_t with SMR values set
 * @return 0 on success, -1 if total_bits inadequate for even 1 bit per band
 */
int bitalloc_waterfill(bitalloc_state_t *state);

/**
 * Iterative two-loop bit allocation (MP3/MPEG style).
 *
 * Inner loop (rate control): increment quantizer step sizes until
 *   Huffman-coded bits ≤ available bits.
 *
 * Outer loop (distortion control): for bands where NMR > 0,
 *   refine quantization (more bits) to reduce audible distortion.
 *
 * This is a simplified version of the ISO/IEC 11172-3 two-loop algorithm.
 * It converges to a locally optimal solution.
 *
 * @param state       Initialized bitalloc_state_t
 * @param max_iters   Maximum iterations (typical: 30)
 * @param nmr_limit   NMR must be ≤ nmr_limit for all bands (typical: 0 dB)
 * @return 0 on success, -1 if cannot meet nmr_limit within max_iters
 */
int bitalloc_two_loop(bitalloc_state_t *state, int max_iters, double nmr_limit);

/**
 * Greedy incremental bit allocation.
 *
 * Start with zero bits for all bands. Iteratively add one bit to the band
 * with the highest marginal SQNR improvement per bit.
 *
 * Marginal improvement for band k:
 *   ΔSQNR ≈ 6.02 dB per additional bit (for uniform quantization)
 *
 * Complexity: O(total_bits * num_bands)
 * Optimal for independent bands with uniform quantization improvement.
 *
 * @param state  Initialized bitalloc_state_t
 * @return 0 on success
 */
int bitalloc_greedy(bitalloc_state_t *state);

/**
 * Constant-NMR bit allocation.
 *
 * Goal: allocate bits such that all bands have the same NMR.
 * This approximates the optimal perceptual allocation.
 *
 * NQR (Noise-to-Mask Ratio) = SQNR - SMR.
 * We want NQR constant across bands: SQNR_k = SMR_k + constant.
 *
 * Since SQNR ≈ 6.02*bits_k + 1.76 (for sine):
 *   bits_k ≈ (SMR_k + constant - 1.76) / 6.02
 *
 * Solve for constant such that Σ bits_k = total_bits.
 *
 * @param state  Initialized bitalloc_state_t
 * @return 0 on success
 */
int bitalloc_constant_nmr(bitalloc_state_t *state);

/* ==========================================================================
 * L4: Rate-Distortion Bound Computation
 * ========================================================================== */

/**
 * Compute the theoretical rate-distortion bound for a set of subbands.
 *
 * R(D) = Σ (1/2) log₂(σ²_k / D_k)  subject to Σ D_k ≤ D_total
 *
 * where σ²_k is the signal variance and D_k is the allowed distortion
 * in band k.
 *
 * This gives the minimum achievable bitrate for a given perceptual distortion
 * level, which serves as a benchmark for codec efficiency.
 *
 * Reference: Berger, "Rate Distortion Theory", Prentice-Hall, 1971
 *
 * @param variance       Signal variance per band (length num_bands)
 * @param mask_threshold Masking threshold per band (power, not dB)
 * @param num_bands      Number of bands
 * @return Rate-Distortion bound in bits
 */
double rate_distortion_bound(const double *variance,
                              const double *mask_threshold,
                              uint32_t num_bands);

/* ==========================================================================
 * L8: Perceptual Noise Shaping
 * ========================================================================== */

/**
 * Compute noise-to-mask ratio (NMR) for each band after allocation.
 *
 * NMR[k] = noise_power_db[k] - mask_threshold_db[k]
 *
 * NMR < 0 → quantization noise is below masking threshold (transparent)
 * NMR = 0 → noise just at threshold
 * NMR > 0 → audible quantization distortion
 *
 * @param state  Bit allocation state after an allocation algorithm
 * @param nmr    Output NMR array (dB), length = state->num_bands
 */
void bitalloc_compute_nmr(const bitalloc_state_t *state, double *nmr);

/**
 * Check if the current allocation achieves perceptual transparency.
 *
 * Returns 1 if all NMR[k] < 0 (inaudible quantization noise).
 * Returns 0 if any band has NMR > 0 (audible distortion).
 */
int bitalloc_is_transparent(const bitalloc_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* BITALLOC_H */

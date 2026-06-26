/**
 * subband.h — Subband Filter Bank for Perceptual Audio Coding
 *
 * L1 Definitions: Subband, analysis/synthesis filter bank, polyphase filter,
 *                  decimation, interpolation, perfect reconstruction
 * L2 Core Concepts: M-channel filter bank, quadrature mirror filter (QMF),
 *                   polyphase decomposition, noble identities
 * L3 Mathematical Structures: Polyphase matrix, modulation matrix,
 *                            z-transform analysis, delay chain
 * L5 Algorithms: MPEG-1 32-band polyphase filter bank (PQMF),
 *                DCT-based filter bank, perfect reconstruction verification
 *
 * Subband coding decomposes the audio signal into M frequency bands,
 * each of which can be independently quantized according to perceptual
 * importance. The polyphase quadrature mirror filter (PQMF) bank is the
 * foundation of MPEG-1 Audio Layers I and II.
 *
 * Reference:
 *   Vaidyanathan, "Multirate Systems and Filter Banks", Prentice Hall, 1993
 *   ISO/IEC 11172-3, Annex C — Analysis Subband Filter
 *   Nussbaumer, "Pseudo QMF Filter Bank", IBM Tech. Disclosure Bull., 1981
 *   Rothweiler, "Polyphase Quadrature Filters...", ICASSP 1983
 *
 * Course Mapping:
 *   MIT 6.003 — Discrete-time filters, multirate processing
 *   Stanford EE102A — Filter design
 *   ETH 227-0427 — Multirate signal processing
 */

#ifndef SUBBAND_H
#define SUBBAND_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Subband Filter Bank Definitions
 * ========================================================================== */

/** MPEG-1 standard: 32 subbands, filter length 512 (16 * 32) */
#define MPEG1_NUM_SUBBANDS  32
#define MPEG1_FILTER_ORDER  512
#define MPEG1_PROTO_LEN     512

/** Maximum subband configuration supported */
#define MAX_SUBBANDS 64
#define MAX_FILTER_LEN 1024

/** PQMF analysis filter bank state */
typedef struct {
    uint32_t   num_subbands;           /**< Number of subbands (M) */
    uint32_t   prototype_len;          /**< Prototype filter length (N = M * K) */
    uint32_t   decimation_factor;      /**< Decimation factor (usually = num_subbands) */
    double    *prototype_filter;       /**< Prototype lowpass filter coefficients */
    double    *state_buffer;           /**< Delay line / shift register for filtering */
    uint32_t   state_index;            /**< Current position in circular buffer */
    double    *subband_samples;        /**< Output subband samples (M values per block) */
    double    *windowed_input;         /**< Temporary buffer for windowed input */
} subband_filterbank_t;

/** Analysis/synthesis pair for perfect reconstruction testing */
typedef struct {
    subband_filterbank_t analysis;    /**< Analysis filter bank */
    subband_filterbank_t synthesis;   /**< Synthesis filter bank */
    double              *recon_buffer; /**< Reconstruction buffer */
} subband_codec_t;

/* ==========================================================================
 * L2: Subband Filter Bank API
 * ========================================================================== */

/**
 * Initialize a PQMF analysis filter bank.
 *
 * Creates an M-channel filter bank with the standard MPEG-1 prototype
 * lowpass filter (length = 16*M, cutoff = π/M).
 *
 * @param fb             Filter bank state to initialize
 * @param num_subbands   Number of subbands (must be power of 2, typical: 32)
 * @return 0 on success, -1 on error
 */
int subband_analysis_init(subband_filterbank_t *fb, uint32_t num_subbands);

/**
 * Initialize a PQMF synthesis filter bank.
 *
 * Creates the matching synthesis filter bank for perfect reconstruction
 * with the analysis bank above.
 *
 * @param fb             Filter bank state to initialize
 * @param num_subbands   Number of subbands (must match analysis bank)
 * @return 0 on success, -1 on error
 */
int subband_synthesis_init(subband_filterbank_t *fb, uint32_t num_subbands);

/**
 * Free resources associated with a filter bank.
 */
void subband_filterbank_free(subband_filterbank_t *fb);

/**
 * Process one block of input through the analysis filter bank.
 *
 * For M subbands, each call consumes M new input samples and produces
 * M subband output samples (one per subband).
 *
 * The MPEG-1 analysis filter bank:
 *   1. Shift in M new samples into a 512-sample FIFO buffer
 *   2. Window the 512 samples by the prototype coefficients C[n]
 *   3. Compute 64 Y[j] = Σ_{i=0}^{7} windowed[j + 64*i]
 *   4. Apply 32-point DCT-like modulation to get 32 subband samples S[k]
 *
 * @param fb       Initialized analysis filter bank
 * @param input    M new input samples (time domain)
 * @param output   M output subband samples (subband domain)
 */
void subband_analysis_process(subband_filterbank_t *fb,
                               const double *input, double *output);

/**
 * Process one block of subband samples through the synthesis filter bank.
 *
 * Inverse of analysis: reconstructs time-domain samples from subband samples.
 * Must be paired with matching analysis bank for perfect reconstruction.
 *
 * @param fb       Initialized synthesis filter bank
 * @param input    M subband input samples
 * @param output   M reconstructed time-domain samples
 */
void subband_synthesis_process(subband_filterbank_t *fb,
                                const double *input, double *output);

/* ==========================================================================
 * L3: Polyphase Decomposition
 * ========================================================================== */

/**
 * Compute the polyphase components of a prototype filter.
 *
 * Given a prototype filter h[n] of length N = M*K (M subbands, K polyphase
 * components each), decompose into M components:
 *
 *   E_i(z) = Σ_{k=0}^{K-1} h[i + k*M] * z^{-k}
 *
 * This is the type-1 polyphase decomposition used in analysis filter banks.
 *
 * @param prototype    Prototype filter (length N)
 * @param M            Number of subbands (polyphase components)
 * @param K            Length of each polyphase component (N/M)
 * @param polyphase    Output: M-by-K matrix, polyphase[m][k] = h[m + k*M]
 */
void polyphase_decompose(const double *prototype, uint32_t M, uint32_t K,
                          double **polyphase);

/**
 * Compute modulation matrix for a cosine-modulated filter bank.
 *
 * For a PQMF bank:
 *   h_k[n] = 2*h_p[n] * cos(π/M * (k + 0.5) * (n - (N-1)/2) + Φ_k)
 *
 * where Φ_k = (-1)^k * π/4 for aliasing cancellation.
 *
 * @param M          Number of subbands
 * @param N          Prototype filter length
 * @param prototype  Prototype filter coefficients
 * @param mod_matrix Output: M-by-N modulation matrix
 */
void compute_modulation_matrix(uint32_t M, uint32_t N,
                                const double *prototype,
                                double **mod_matrix);

/* ==========================================================================
 * L4: Perfect Reconstruction Condition
 * ========================================================================== */

/**
 * Verify perfect reconstruction (PR) for a cosine-modulated filter bank.
 *
 * For a PQMF bank with prototype filter h[n]:
 * PR condition: h[M-1-n] = h[n] (linear phase / symmetry)
 * plus additional constraints on the polyphase components.
 *
 * This function checks the PR deviation by processing an impulse through
 * the analysis-synthesis chain and measuring the reconstruction error.
 *
 * @param fb_analysis   Initialized analysis filter bank
 * @param fb_synthesis  Initialized synthesis filter bank
 * @return Peak reconstruction error (should be < 1e-12 for PR)
 */
double verify_pr_subband(const subband_filterbank_t *fb_analysis,
                          const subband_filterbank_t *fb_synthesis);

/* ==========================================================================
 * L5: MPEG-1 Standard Prototype Filter
 * ========================================================================== */

/**
 * Generate the standard MPEG-1 Layer I/II prototype lowpass filter.
 *
 * This is the C[n] window coefficients from ISO/IEC 11172-3, Table C.1.
 * Length: 512 taps (for M=32 subbands, K=16 polyphase components each).
 *
 * The filter was designed for:
 *   - Passband ripple < 0.1 dB
 *   - Stopband attenuation > 96 dB
 *   - Transition bandwidth ≈ π/(2M)
 *
 * @param coeffs  Output coefficient array (length 512)
 */
void mpeg1_prototype_filter(double *coeffs);

/**
 * Generate a Kaiser-window-designed prototype lowpass filter
 * for an arbitrary number of subbands.
 *
 * @param coeffs         Output coefficient array (length M*K)
 * @param M              Number of subbands (power of 2)
 * @param K              Polyphase length per subband
 * @param cutoff_ratio   Cutoff frequency as fraction of π/M (default: 1.0)
 * @param beta           Kaiser window beta (default: 8.0 for ~80dB attenuation)
 */
void design_kernel_prototype(double *coeffs, uint32_t M, uint32_t K,
                              double cutoff_ratio, double beta);

#ifdef __cplusplus
}
#endif

#endif /* SUBBAND_H */

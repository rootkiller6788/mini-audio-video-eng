/**
 * entropy_codec.h — Entropy Coding for Video Codecs
 *
 * L1: Bitstream reader/writer, Exp-Golomb codes, CAVLC table definitions
 * L2: Variable-length coding, arithmetic coding (CABAC basics)
 * L4: Source coding theorem (Shannon entropy bound)
 * L5: Exp-Golomb (ue/se/te), CAVLC encode/decode, binarization
 *
 * Entropy coding is the final lossless compression stage that removes
 * statistical redundancy from syntax elements (MVD, coeff_level, etc.).
 * H.264 uses two methods: CAVLC (Baseline/Main) and CABAC (Main/High).
 *
 * Reference:
 *   ITU-T H.264, Section 9 — Entropy Coding
 *   Marpe et al., "Context-Based Adaptive Binary Arithmetic Coding in
 *     the H.264/AVC Video Compression Standard", IEEE Trans. CSVT, 2003
 *   Richardson, "The H.264 Advanced Video Compression Standard" (2010)
 *   Cover & Thomas, "Elements of Information Theory" (2006), Ch. 5 — Data Compression
 *
 * Course Mapping:
 *   MIT 6.450 — Digital Communications (source coding)
 *   Stanford EE392J — Video Compression
 *   TU Munich — Video Coding / Data Compression
 *   Georgia Tech ECE 6601 — Video Compression
 */

#ifndef ENTROPY_CODEC_H
#define ENTROPY_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Bitstream I/O
 * ========================================================================== */

/** Bitstream writer — appends bits to a byte buffer */
typedef struct {
    uint8_t *buf;            /**< Output byte buffer */
    uint32_t buf_size;       /**< Buffer capacity in bytes */
    uint32_t byte_pos;       /**< Current byte position in buffer */
    uint32_t bit_pos;        /**< Current bit position within current byte (0-7) */
    uint32_t total_bits;     /**< Total bits written so far */
} bs_writer_t;

/** Bitstream reader — reads bits from a byte buffer */
typedef struct {
    const uint8_t *buf;      /**< Input byte buffer */
    uint32_t buf_size;       /**< Buffer size in bytes */
    uint32_t byte_pos;       /**< Current byte position */
    uint32_t bit_pos;        /**< Current bit position (0-7) */
    uint32_t total_bits;     /**< Total bits read so far */
    uint8_t  error;          /**< Set to 1 on read beyond buffer */
} bs_reader_t;

/** CAVLC decoded coefficient token */
typedef struct {
    int32_t  total_coeff;    /**< Total non-zero coefficients (0-16) */
    int32_t  trailing_ones;  /**< Number of trailing +/-1 values (0-3) */
    uint32_t trailing_signs; /**< Bitmask of trailing one signs (1 bit each, 0=+, 1=-) */
    int32_t  levels[16];     /**< Non-zero coefficient levels */
    int32_t  total_zeros;    /**< Total zeros before last coefficient */
    int32_t  run_before[16]; /**< Run of zeros before each non-zero coeff */
} cavlc_block_t;

/** CAVLC VLC table entry */
typedef struct {
    uint32_t code;           /**< Binary codeword */
    uint8_t  len;            /**< Length in bits */
    int32_t  value;          /**< Decoded value */
} vlc_entry_t;

/* ==========================================================================
 * L2: Core Concepts — Bitstream Operations
 * ========================================================================== */

/**
 * Initialize a bitstream writer.
 *
 * @param w      Writer struct to initialize
 * @param buf    Pre-allocated buffer (caller-owned)
 * @param size   Buffer size in bytes
 */
void bs_writer_init(bs_writer_t *w, uint8_t *buf, uint32_t size);

/**
 * Initialize a bitstream reader.
 *
 * @param r       Reader struct to initialize
 * @param buf     Input byte buffer
 * @param size    Buffer size in bytes
 */
void bs_reader_init(bs_reader_t *r, const uint8_t *buf, uint32_t size);

/**
 * Write a single bit to the bitstream.
 * Returns -1 if buffer full.
 */
int bs_write_bit(bs_writer_t *w, uint8_t bit);

/**
 * Write N bits to the bitstream (N <= 32).
 * MSB-first ordering.
 * Returns -1 if insufficient buffer space.
 */
int bs_write_bits(bs_writer_t *w, uint32_t value, uint32_t n);

/**
 * Read a single bit from the bitstream.
 * Returns -1 on error (buffer exhausted).
 */
int bs_read_bit(bs_reader_t *r);

/**
 * Read N bits from the bitstream (N <= 32).
 * Returns the value, or 0 on error (check r->error).
 */
uint32_t bs_read_bits(bs_reader_t *r, uint32_t n);

/**
 * Byte-align the bitstream writer (pad to next byte boundary with 0 bits).
 */
void bs_byte_align(bs_writer_t *w);

/**
 * Byte-align the bitstream reader.
 */
void bs_reader_byte_align(bs_reader_t *r);

/**
 * Get the number of bytes written (ceil(total_bits/8)).
 */
uint32_t bs_writer_byte_count(const bs_writer_t *w);

/**
 * Get the number of bytes read.
 */
uint32_t bs_reader_byte_count(const bs_reader_t *r);

/* ==========================================================================
 * L5: Algorithms — Exponential-Golomb Coding
 * ========================================================================== */

/**
 * Write an unsigned Exp-Golomb coded value (ue(v)).
 *
 * Code structure for value v:
 *   1. Prefix: v+1 in unary (v zeros followed by a 1)
 *   2. Suffix: v in binary using floor(log2(v+1)) bits
 *   Actually the standard form is simpler:
 *     codeword = [M zeros] [1] [INFO], where M = floor(log2(v+1)),
 *     INFO = (v+1) - 2^M, in M bits
 *
 * Reference: ITU-T H.264, Section 9.1
 *
 * @param w     Bitstream writer
 * @param value Unsigned integer to encode
 * @return Number of bits written, or -1 on error
 */
int bs_write_ue(bs_writer_t *w, uint32_t value);

/**
 * Read an unsigned Exp-Golomb coded value (ue(v)).
 *
 * @param r     Bitstream reader
 * @return Decoded unsigned value, or 0 on error (check r->error)
 */
uint32_t bs_read_ue(bs_reader_t *r);

/**
 * Write a signed Exp-Golomb coded value (se(v)).
 *
 * Mapping to unsigned: ue_val = (|v| << 1) | (v < 0)
 * i.e., 0→1, 1→2, -1→3, 2→4, -2→5, ...
 *
 * @param w     Bitstream writer
 * @param value Signed integer to encode
 * @return Number of bits written, or -1 on error
 */
int bs_write_se(bs_writer_t *w, int32_t value);

/**
 * Read a signed Exp-Golomb coded value (se(v)).
 *
 * @param r     Bitstream reader
 * @return Decoded signed value, or 0 on error
 */
int32_t bs_read_se(bs_reader_t *r);

/**
 * Write a truncated Exp-Golomb coded value (te(v)).
 *
 * Used when the maximum possible value is known (e.g., ref_idx).
 * If range == 1, value is coded as the inverted bit.
 *
 * @param w      Bitstream writer
 * @param value  Value to encode
 * @param range  Maximum value + 1 (e.g., num_ref_idx_active)
 * @return Bits written, -1 on error
 */
int bs_write_te(bs_writer_t *w, uint32_t value, uint32_t range);

/**
 * Read a truncated Exp-Golomb coded value (te(v)).
 */
uint32_t bs_read_te(bs_reader_t *r, uint32_t range);

/**
 * Compute the bit-length of a ue(v) codeword for value v.
 * (Without actually writing to bitstream — useful for rate estimation.)
 *
 * bits_ue(v) = 2 * floor(log2(v+1)) + 1
 */
uint32_t ue_bit_length(uint32_t value);

/**
 * Compute the bit-length of a se(v) codeword for value v.
 * bits_se(v) = bits_ue( (|v| << 1) | (v < 0) )
 */
uint32_t se_bit_length(int32_t value);

/* ==========================================================================
 * L5: Algorithms — CAVLC (Context-Adaptive Variable Length Coding)
 * ========================================================================== */

/**
 * CAVLC encode a 4x4 coefficient block.
 *
 * CAVLC encoding steps (H.264 Section 9.2):
 *   1. Zigzag scan (caller must do this before calling)
 *   2. Encode coeff_token (TotalCoeff + TrailingOnes)
 *   3. Encode sign of each trailing one
 *   4. Encode levels of remaining non-zero coefficients
 *   5. Encode total_zeros
 *   6. Encode run_before for each non-zero coefficient
 *
 * Complexity: O(N) where N = number of non-zero coefficients
 *
 * @param w        Bitstream writer
 * @param coeffs   16 coefficients in zigzag scan order
 * @param nC       Number of non-zero coeffs in neighboring blocks (0-16)
 *                 Determines which VLC table to use for coeff_token.
 * @return Number of bits written, or -1 on error
 */
int cavlc_encode_4x4(bs_writer_t *w, const int32_t *coeffs, uint32_t nC);

/**
 * CAVLC decode a 4x4 coefficient block.
 *
 * @param r        Bitstream reader
 * @param coeffs   Output: 16 decoded coefficients in zigzag order
 * @param nC       Context: non-zero count in neighbors
 * @return 0 on success, -1 on error
 */
int cavlc_decode_4x4(bs_reader_t *r, int32_t *coeffs, uint32_t nC);

/**
 * CAVLC encode coeff_token (TotalCoeff + TrailingOnes).
 * Internal function exposed for rate estimation.
 *
 * nC selects VLC table:
 *   nC=0..1 -> Table 1 (Num-VLC0, biased for small values)
 *   nC=2..3 -> Table 2 (Num-VLC1)
 *   nC=4..7 -> Table 3 (Num-VLC2, FLC for nC>=8)
 */
int cavlc_write_coeff_token(bs_writer_t *w, int total_coeff, int trailing_ones,
                            uint32_t nC);

/**
 * CAVLC write level prefix/suffix (Level VLC).
 * Levels are coded with adaptive VLC table selection that depends on
 * the magnitude of previously coded levels.
 */
int cavlc_write_levels(bs_writer_t *w, const int32_t *levels, int num_levels);

/**
 * CAVLC write total_zeros and run_before fields.
 */
int cavlc_write_total_zeros(bs_writer_t *w, int total_zeros, int total_coeff);
int cavlc_write_run_before(bs_writer_t *w, int zeros_left, int run);

/* ==========================================================================
 * L5: Algorithms — CABAC Binarization (Selected Bin Strings)
 * ========================================================================== */

/**
 * CABAC uses binarization to map syntax elements to binary sequences.
 * These functions compute the bin string representation (not full CABAC
 * with context modeling and arithmetic coding, which requires a much
 * larger state machine).
 *
 * Binarization methods defined in H.264:
 *   - Unary (U): value → value '1's + terminating '0'
 *   - Truncated Unary (TU): like unary, capped at cMax
 *   - Fixed-Length (FL): value in fixed number of bits
 *   - k-th order Exp-Golomb (EGk): for large values
 */

/**
 * Compute the number of bins for a unary binarization of value.
 */
uint32_t cabac_unary_bins(uint32_t value);

/**
 * Compute the number of bins for a truncated unary binarization.
 */
uint32_t cabac_trunc_unary_bins(uint32_t value, uint32_t c_max);

/**
 * Compute the number of bins for a k-th order Exp-Golomb binarization.
 */
uint32_t cabac_egk_bins(uint32_t value, uint32_t k);

/* ==========================================================================
 * L4: Shannon Source Coding Theorem Verification
 * ========================================================================== */

/**
 * Compute Shannon entropy of a symbol distribution.
 *
 * H(X) = -sum p_i * log2(p_i)
 *
 * This is the theoretical lower bound for lossless compression
 * of an i.i.d. source with distribution p_i.
 *
 * Reference: Shannon, "A Mathematical Theory of Communication" (1948)
 *
 * @param probs     Array of symbol probabilities (must sum to 1.0)
 * @param num_syms  Number of symbols in alphabet
 * @return Entropy in bits per symbol, or 0 if invalid input
 */
double shannon_entropy(const double *probs, uint32_t num_syms);

/**
 * Compute the average code length of a set of codewords.
 *
 * L_avg = sum p_i * len_i
 *
 * @param probs    Symbol probabilities
 * @param lengths  Codeword lengths in bits
 * @param num_syms Number of symbols
 * @return Average code length in bits per symbol
 */
double avg_code_length(const double *probs, const uint32_t *lengths,
                       uint32_t num_syms);

/**
 * Verify that a prefix code satisfies the Kraft inequality.
 *
 * Kraft sum: sum_i 2^{-len_i} <= 1
 *
 * Equal iff the code is complete (no unused codewords).
 *
 * Reference: Cover & Thomas, Theorem 5.2.1
 *
 * @param lengths  Codeword lengths
 * @param num      Number of codewords
 * @return The Kraft sum value (<= 1.0 for valid prefix codes)
 */
double kraft_sum(const uint32_t *lengths, uint32_t num);

/* ==========================================================================
 * L2: Core Concepts — Emulation Prevention (H.264 Annex B)
 * ========================================================================== */

/**
 * Apply emulation prevention bytes to a raw byte stream.
 *
 * H.264 Annex B format: after every 0x0000 byte pair, insert 0x03
 * to prevent accidental start code emulation (0x000001 = start code).
 *
 * @param in      Input bytes
 * @param in_len  Input length
 * @param out     Output buffer (must be large enough: up to in_len * 1.5)
 * @return Number of output bytes
 */
uint32_t emulation_prevent(const uint8_t *in, uint32_t in_len, uint8_t *out);

/**
 * Remove emulation prevention bytes from a raw byte stream.
 */
uint32_t emulation_remove(const uint8_t *in, uint32_t in_len, uint8_t *out);

/**
 * Find the next start code (0x000001 or 0x00000001) in a byte buffer.
 *
 * @param buf   Byte buffer
 * @param len   Buffer length
 * @param pos   Input: starting position. Output: position of start code, or len if not found
 * @return 3 if 0x000001 found, 4 if 0x00000001 found, 0 if none
 */
int find_start_code(const uint8_t *buf, uint32_t len, uint32_t *pos);

#ifdef __cplusplus
}
#endif

#endif /* ENTROPY_CODEC_H */

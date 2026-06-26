/**
 * entropy.h — Entropy Coding for Audio Codecs
 *
 * L1 Definitions: Entropy, Huffman code, prefix code, codebook, symbol
 * L2 Core Concepts: Lossless source coding, variable-length codes,
 *                   Huffman tree construction, canonical Huffman coding
 * L3 Mathematical Structures: Source entropy H(X) = -Σ pᵢ log₂(pᵢ),
 *                            Kraft inequality, Shannon-Fano codes
 * L4 Fundamental Laws: Shannon's Source Coding Theorem:
 *     H(X) ≤ L_avg < H(X) + 1  (for block length 1)
 * L5 Algorithms: Huffman tree construction, canonical Huffman encoding,
 *                Rice/Golomb coding (used in FLAC), run-length coding
 *
 * Reference:
 *   Huffman, "A Method for the Construction of Minimum-Redundancy Codes",
 *     Proc. IRE, 1952
 *   Cover & Thomas, "Elements of Information Theory", 2nd ed. 2006 (Ch. 5)
 *   ISO/IEC 11172-3 (MPEG-1 Audio) — Huffman codebook tables
 *   Sayood, "Introduction to Data Compression", 5th ed. 2017
 *
 * Course Mapping:
 *   MIT 6.450 — Source coding theorem
 *   Stanford EE359 — Shannon theory
 *   Berkeley EE123 — Data compression
 */

#ifndef ENTROPY_H
#define ENTROPY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Entropy Coding Definitions
 * ========================================================================== */

/** Maximum Huffman code length (typical codec limit) */
#define MAX_HUFFMAN_CODE_LEN 32

/** Maximum alphabet size for Huffman coding */
#define MAX_HUFFMAN_SYMBOLS 256

/** A single Huffman code entry */
typedef struct {
    uint16_t symbol;                /**< Input symbol value */
    uint8_t  code_len;              /**< Code length in bits */
    uint32_t code;                  /**< Code bits (right-aligned) */
} huffman_entry_t;

/** Huffman codebook — complete set of codes for one coding table */
typedef struct {
    uint32_t        num_symbols;    /**< Number of symbols in codebook */
    huffman_entry_t entries[MAX_HUFFMAN_SYMBOLS];  /**< Code entries sorted by symbol */
    int             sorted;         /**< 1 if entries are symbol-sorted */
    double          avg_len;        /**< Average code length (bits) */
    double          entropy;        /**< Source entropy H (bits) */
    double          efficiency;     /**< Coding efficiency = H / avg_len */
} huffman_codebook_t;

/** Huffman tree node (internal, for tree construction) */
typedef struct htree_node_t {
    int32_t  symbol;                /**< Leaf symbol (>= 0), or -1 for internal */
    uint64_t freq;                  /**< Symbol frequency (for tree construction) */
    double   prob;                  /**< Symbol probability */
    struct htree_node_t *left;      /**< Left child */
    struct htree_node_t *right;     /**< Right child */
} htree_node_t;

/** Bitstream writer state for entropy encoding */
typedef struct {
    uint8_t *buffer;                /**< Output byte buffer */
    uint64_t capacity;              /**< Buffer capacity in bytes */
    uint64_t byte_pos;              /**< Current write position in bytes */
    uint8_t  bit_buf;               /**< Accumulator for partial byte */
    int      bit_count;             /**< Bits accumulated (0-7) */
    uint64_t total_bits_written;    /**< Total number of bits written */
} bitstream_writer_t;

/** Bitstream reader state for entropy decoding */
typedef struct {
    const uint8_t *buffer;          /**< Input byte buffer */
    uint64_t capacity;              /**< Buffer capacity in bytes */
    uint64_t byte_pos;              /**< Current read position in bytes */
    uint8_t  bit_buf;               /**< Read-ahead buffer */
    int      bit_count;             /**< Available bits in read-ahead buffer */
    uint64_t total_bits_read;       /**< Total number of bits read */
} bitstream_reader_t;

/* ==========================================================================
 * L3: Entropy Computation
 * ========================================================================== */

/**
 * Compute Shannon entropy of a discrete probability distribution.
 *
 * H = -Σ pᵢ * log₂(pᵢ)
 *
 * where pᵢ is the probability of symbol i.
 * H = 0 if any pᵢ = 1 (no uncertainty).
 *
 * This is the fundamental lower bound on lossless compression —
 * no code can have average length less than H.
 *
 * Reference: Shannon, "A Mathematical Theory of Communication", 1948
 *
 * @param prob     Array of probabilities, must sum to 1.0
 * @param n        Number of symbols
 * @return Entropy in bits per symbol
 */
double shannon_entropy(const double *prob, uint32_t n);

/**
 * Compute entropy from frequency counts.
 * pᵢ = freq[i] / total
 */
double shannon_entropy_from_counts(const uint64_t *freq, uint32_t n);

/**
 * Compute the Kraft sum to verify a prefix code.
 *
 * Kraft inequality: Σ 2^{-lᵢ} ≤ 1
 *
 * where lᵢ is the length of code i.
 * Equality holds for complete codes.
 *
 * @param code_lens  Array of code lengths (bits)
 * @param n          Number of codewords
 * @return Kraft sum value
 */
double kraft_sum(const uint8_t *code_lens, uint32_t n);

/* ==========================================================================
 * L5: Huffman Coding
 * ========================================================================== */

/**
 * Build a Huffman tree from symbol frequencies.
 *
 * Algorithm:
 * 1. Create leaf nodes for each symbol with frequency
 * 2. Repeatedly combine two lowest-frequency nodes into parent
 * 3. Continue until single root node
 * 4. Traverse tree to assign codes (0 = left, 1 = right)
 *
 * Complexity: O(n log n) using priority queue (binary heap)
 *
 * Reference: Huffman, Proc. IRE, 1952
 *
 * @param root_out      Pointer to root node (caller must free with htree_free)
 * @param freq          Array of symbol frequencies
 * @param num_symbols   Number of symbols (≤ MAX_HUFFMAN_SYMBOLS)
 * @return 0 on success, -1 on error
 */
int huffman_build_tree(htree_node_t **root_out,
                        const uint64_t *freq, uint32_t num_symbols);

/**
 * Free a Huffman tree.
 */
void huffman_tree_free(htree_node_t *root);

/**
 * Generate a canonical Huffman codebook from a Huffman tree.
 *
 * Canonical Huffman codes are preferred in practical codecs because:
 * - Only code lengths need to be transmitted (not the tree)
 * - Encoding/decoding can use fast table lookup
 * - The specific bits assigned are standardized for MPEG audio
 *
 * @param codebook     Output codebook
 * @param root         Huffman tree root
 * @param num_symbols  Number of symbols
 */
void huffman_generate_canonical(huffman_codebook_t *codebook,
                                 htree_node_t *root, uint32_t num_symbols);

/**
 * Encode a single symbol using a Huffman codebook.
 *
 * Writes the variable-length code to the bitstream.
 *
 * @param bs       Bitstream writer
 * @param codebook Huffman codebook
 * @param symbol   Symbol to encode
 * @return 0 on success, -1 if symbol not found or buffer full
 */
int huffman_encode_symbol(bitstream_writer_t *bs,
                           const huffman_codebook_t *codebook, uint16_t symbol);

/**
 * Decode a single symbol from a Huffman-coded bitstream.
 *
 * Uses table-based decoding for O(1) per symbol.
 *
 * @param bs       Bitstream reader
 * @param codebook Huffman codebook (must be sorted by symbol)
 * @param symbol_out Decoded symbol
 * @return 0 on success, -1 on decode error
 */
int huffman_decode_symbol(bitstream_reader_t *bs,
                           const huffman_codebook_t *codebook, uint16_t *symbol_out);

/* ==========================================================================
 * L5: Rice / Golomb Coding (used in FLAC, ALS)
 * ========================================================================== */

/**
 * Encode a non-negative integer using Rice coding.
 *
 * Rice coding (special case of Golomb with M = 2^k):
 *   quotient  q = value >> k          (unary coded)
 *   remainder r = value & ((1<<k)-1)  (binary coded, k bits)
 *
 * Optimal when values follow a geometric distribution.
 * Used in FLAC for residual coding after LPC prediction.
 *
 * @param bs       Bitstream writer
 * @param value    Non-negative value to encode
 * @param k        Rice parameter (k bits for remainder, typical: 0-15)
 * @return 0 on success
 */
int rice_encode(bitstream_writer_t *bs, uint32_t value, int k);

/**
 * Decode a Rice-coded value.
 */
int rice_decode(bitstream_reader_t *bs, int k, uint32_t *value_out);

/**
 * Compute the optimal Rice parameter k for a set of values.
 *
 * k = floor(log₂(mean(values) * 0.693147))
 *
 * Derivation: For geometric distribution with mean μ,
 * the optimal Golomb parameter M ≈ μ / ln(2).
 * For Rice coding, k ≈ log₂(μ * ln(2)) ≈ log₂(μ * 0.693).
 *
 * @param values     Array of non-negative integer values
 * @param n          Number of values
 * @return Optimal Rice parameter k (0-31)
 */
int rice_optimal_k(const uint32_t *values, uint32_t n);

/* ==========================================================================
 * L2: Bitstream I/O Primitives
 * ========================================================================== */

/** Initialize bitstream writer with external buffer */
void bitstream_writer_init(bitstream_writer_t *bs, uint8_t *buffer, uint64_t capacity);

/** Initialize bitstream reader with external buffer */
void bitstream_reader_init(bitstream_reader_t *bs, const uint8_t *buffer, uint64_t capacity);

/** Write n bits to bitstream (n ≤ 32) */
int bitstream_write_bits(bitstream_writer_t *bs, uint32_t value, int n);

/** Read n bits from bitstream (n ≤ 32) */
int bitstream_read_bits(bitstream_reader_t *bs, int n, uint32_t *value_out);

/** Flush remaining bits in the write accumulator (pad with zeros) */
void bitstream_flush(bitstream_writer_t *bs);

/** Get number of bytes written (ceil(total_bits / 8)) */
uint64_t bitstream_bytes_written(const bitstream_writer_t *bs);

/** Get number of bytes read (ceil(total_bits / 8)) */
uint64_t bitstream_bytes_read(const bitstream_reader_t *bs);

#ifdef __cplusplus
}
#endif

#endif /* ENTROPY_H */

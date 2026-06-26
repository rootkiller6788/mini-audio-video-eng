/**
 * entropy.c — Entropy Coding (Huffman, Rice, Bitstream I/O)
 *
 * Implements:
 *   L1: Entropy coding definitions, bitstream primitives
 *   L2: Bitstream reader/writer
 *   L3: Shannon entropy computation, Kraft inequality
 *   L4: Shannon's Source Coding Theorem verification
 *   L5: Huffman tree construction, canonical Huffman coding,
 *       Rice/Golomb coding, optimal Rice parameter selection
 *
 * Reference:
 *   Huffman, "A Method for the Construction of Minimum-Redundancy Codes", 1952
 *   Cover & Thomas, "Elements of Information Theory", 2006
 */

#include "entropy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * L3: Shannon Entropy
 * ========================================================================== */

double shannon_entropy(const double *prob, uint32_t n)
{
    double H = 0.0;
    double sum = 0.0;

    for (uint32_t i = 0; i < n; i++) {
        sum += prob[i];
        if (prob[i] > 1e-15) {
            H -= prob[i] * log2(prob[i]);
        }
    }

    /* Verify probabilities sum to 1.0 (within tolerance) */
    /* If not, treat as relative frequencies and normalize implicitly */
    if (sum > 1e-15 && fabs(sum - 1.0) > 0.01) {
        /* Renormalize */
        H = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            double p = prob[i] / sum;
            if (p > 1e-15) {
                H -= p * log2(p);
            }
        }
    }

    return H;
}

double shannon_entropy_from_counts(const uint64_t *freq, uint32_t n)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        total += freq[i];
    }
    if (total == 0) return 0.0;

    double H = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / (double)total;
            H -= p * log2(p);
        }
    }
    return H;
}

double kraft_sum(const uint8_t *code_lens, uint32_t n)
{
    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        if (code_lens[i] > 0 && code_lens[i] <= 64) {
            sum += pow(2.0, -(double)code_lens[i]);
        }
    }
    return sum;
}

/* ==========================================================================
 * L5: Huffman Tree Construction
 * ========================================================================== */

/**
 * Priority queue node for Huffman tree building.
 */
typedef struct {
    htree_node_t *node;
    uint64_t      freq;
} pq_entry_t;

/**
 * Simple binary heap for Huffman tree construction.
 */
typedef struct {
    pq_entry_t *entries;
    uint32_t    size;
    uint32_t    capacity;
} min_heap_t;

static void heap_init(min_heap_t *heap, uint32_t capacity)
{
    heap->entries = (pq_entry_t *)malloc((size_t)capacity * sizeof(pq_entry_t));
    heap->size     = 0;
    heap->capacity = capacity;
}

static void heap_free(min_heap_t *heap)
{
    if (heap->entries) {
        free(heap->entries);
        heap->entries = NULL;
    }
    heap->size = 0;
}

static void heap_push(min_heap_t *heap, htree_node_t *node, uint64_t freq)
{
    if (heap->size >= heap->capacity) return;

    uint32_t i = heap->size;
    heap->size++;

    while (i > 0) {
        uint32_t parent = (i - 1) / 2;
        if (heap->entries[parent].freq <= freq) break;
        heap->entries[i] = heap->entries[parent];
        i = parent;
    }
    heap->entries[i].node = node;
    heap->entries[i].freq = freq;
}

static htree_node_t *heap_pop(min_heap_t *heap, uint64_t *freq_out)
{
    if (heap->size == 0) return NULL;

    htree_node_t *result = heap->entries[0].node;
    if (freq_out) *freq_out = heap->entries[0].freq;

    heap->size--;
    pq_entry_t last = heap->entries[heap->size];

    uint32_t i = 0;
    while (1) {
        uint32_t left  = 2 * i + 1;
        uint32_t right = 2 * i + 2;
        uint32_t smallest = i;

        if (left < heap->size &&
            heap->entries[left].freq < heap->entries[smallest].freq) {
            smallest = left;
        }
        if (right < heap->size &&
            heap->entries[right].freq < heap->entries[smallest].freq) {
            smallest = right;
        }
        if (smallest == i) break;

        heap->entries[i] = heap->entries[smallest];
        i = smallest;
    }
    heap->entries[i] = last;

    return result;
}

int huffman_build_tree(htree_node_t **root_out,
                        const uint64_t *freq, uint32_t num_symbols)
{
    if (num_symbols == 0 || num_symbols > MAX_HUFFMAN_SYMBOLS) return -1;
    if (!root_out) return -1;

    min_heap_t heap;
    heap_init(&heap, num_symbols * 2);

    /* Create leaf nodes for each symbol with non-zero frequency */
    uint32_t active = 0;
    for (uint32_t i = 0; i < num_symbols; i++) {
        if (freq[i] > 0) {
            htree_node_t *leaf = (htree_node_t *)calloc(1, sizeof(htree_node_t));
            if (!leaf) {
                heap_free(&heap);
                return -2;
            }
            leaf->symbol = (int32_t)i;
            leaf->freq   = freq[i];
            leaf->prob   = 0.0;
            heap_push(&heap, leaf, freq[i]);
            active++;
        }
    }

    if (active == 0) {
        heap_free(&heap);
        *root_out = NULL;
        return -3;
    }

    /* If only one symbol, create a dummy node to make a valid tree */
    if (active == 1) {
        htree_node_t *only = heap_pop(&heap, NULL);
        htree_node_t *dummy = (htree_node_t *)calloc(1, sizeof(htree_node_t));
        if (!dummy) {
            heap_free(&heap);
            huffman_tree_free(only);
            return -2;
        }
        dummy->symbol = -1;
        dummy->freq   = only->freq;
        dummy->left   = only;
        dummy->right  = NULL;
        heap_free(&heap);
        *root_out = dummy;
        return 0;
    }

    /* Build tree: combine two smallest until one node remains */
    while (heap.size > 1) {
        uint64_t f1, f2;
        htree_node_t *n1 = heap_pop(&heap, &f1);
        htree_node_t *n2 = heap_pop(&heap, &f2);

        htree_node_t *parent = (htree_node_t *)calloc(1, sizeof(htree_node_t));
        if (!parent) {
            heap_free(&heap);
            huffman_tree_free(n1);
            huffman_tree_free(n2);
            return -2;
        }
        parent->symbol = -1;
        parent->freq   = f1 + f2;
        parent->left   = n1;
        parent->right  = n2;

        heap_push(&heap, parent, f1 + f2);
    }

    *root_out = heap_pop(&heap, NULL);
    heap_free(&heap);
    return 0;
}

void huffman_tree_free(htree_node_t *root)
{
    if (!root) return;
    huffman_tree_free(root->left);
    huffman_tree_free(root->right);
    free(root);
}

/* ==========================================================================
 * L5: Canonical Huffman Code Generation
 * ========================================================================== */

/**
 * Recursive helper: traverse tree and assign code lengths.
 */
static void huffman_assign_lengths(htree_node_t *node, uint8_t depth,
                                    uint8_t *code_lens, uint32_t num_symbols)
{
    if (!node) return;
    if (node->symbol >= 0 && (uint32_t)node->symbol < num_symbols) {
        code_lens[node->symbol] = depth;
        return;
    }
    huffman_assign_lengths(node->left,  depth + 1, code_lens, num_symbols);
    huffman_assign_lengths(node->right, depth + 1, code_lens, num_symbols);
}

void huffman_generate_canonical(huffman_codebook_t *codebook,
                                 htree_node_t *root, uint32_t num_symbols)
{
    memset(codebook, 0, sizeof(huffman_codebook_t));
    codebook->num_symbols = num_symbols;

    /* Step 1: Get code lengths from tree */
    uint8_t code_lens[MAX_HUFFMAN_SYMBOLS] = {0};
    huffman_assign_lengths(root, 0, code_lens, num_symbols);

    /* Step 2: Sort symbols by code length, then by symbol value for canonical */
    /* Canonical: shortest codes first, numerically increasing within same length */
    uint32_t sorted[MAX_HUFFMAN_SYMBOLS];
    for (uint32_t i = 0; i < num_symbols; i++) {
        sorted[i] = i;
    }

    /* Bubble sort by (code_len, symbol) — sufficient for small alphabets */
    for (uint32_t i = 0; i < num_symbols; i++) {
        for (uint32_t j = i + 1; j < num_symbols; j++) {
            if (code_lens[sorted[i]] > code_lens[sorted[j]] ||
                (code_lens[sorted[i]] == code_lens[sorted[j]] &&
                 sorted[i] > sorted[j])) {
                uint32_t t = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = t;
            }
        }
    }

    /* Step 3: Generate canonical codes */
    uint32_t code = 0;
    uint8_t  prev_len = 0;

    for (uint32_t i = 0; i < num_symbols; i++) {
        uint32_t sym = sorted[i];
        uint8_t  len = code_lens[sym];

        if (len == 0) {
            codebook->entries[sym].symbol   = 0;
            codebook->entries[sym].code_len = 0;
            codebook->entries[sym].code     = 0;
            continue;
        }

        if (len > prev_len) {
            code <<= (len - prev_len);
            prev_len = len;
        }

        codebook->entries[sym].symbol   = (uint16_t)sym;
        codebook->entries[sym].code_len = len;
        codebook->entries[sym].code     = code;

        code++;
    }

    /* Compute statistics */
    double avg = 0.0;
    /* Use the code lengths directly */
    for (uint32_t i = 0; i < num_symbols; i++) {
        uint8_t len = code_lens[i];
        avg += (double)len;
    }
    codebook->avg_len   = avg / (double)num_symbols;
    codebook->entropy   = 0.0;  /* Will be set by caller if needed */
    codebook->efficiency = (codebook->entropy > 0.0) ?
        (codebook->entropy / codebook->avg_len) : 0.0;
    codebook->sorted    = 1;
}

/* ==========================================================================
 * L5: Huffman Encoding/Decoding
 * ========================================================================== */

int huffman_encode_symbol(bitstream_writer_t *bs,
                           const huffman_codebook_t *codebook, uint16_t symbol)
{
    if (symbol >= codebook->num_symbols) return -1;

    huffman_entry_t entry = codebook->entries[symbol];
    if (entry.code_len == 0) return -2; /* Symbol not in codebook */

    return bitstream_write_bits(bs, entry.code, entry.code_len);
}

int huffman_decode_symbol(bitstream_reader_t *bs,
                           const huffman_codebook_t *codebook, uint16_t *symbol_out)
{
    /* Use a simple linear search through codebook entries.
     * For production codecs, this would use a lookup table (canonical
     * Huffman enables fast table-based decoding).
     *
     * This implementation reads one bit at a time and matches against
     * codes of the same length.
     */
    uint32_t code = 0;
    int      bits = 0;

    for (int len = 1; len <= MAX_HUFFMAN_CODE_LEN; len++) {
        uint32_t bit;
        if (bitstream_read_bits(bs, 1, &bit) < 0) return -1;
        code = (code << 1) | (bit & 1);
        bits++;

        /* Search for matching code of this exact length */
        for (uint32_t i = 0; i < codebook->num_symbols; i++) {
            if (codebook->entries[i].code_len == (uint8_t)bits &&
                codebook->entries[i].code == code) {
                *symbol_out = codebook->entries[i].symbol;
                return 0;
            }
        }
    }

    return -2; /* No matching code found */
}

/* ==========================================================================
 * L5: Rice / Golomb Coding
 * ========================================================================== */

int rice_encode(bitstream_writer_t *bs, uint32_t value, int k)
{
    if (k < 0 || k > 31) return -1;

    /* Quotient = value >> k, coded in unary */
    uint32_t q = value >> k;
    for (uint32_t i = 0; i < q; i++) {
        if (bitstream_write_bits(bs, 1, 1) < 0) return -1; /* 1-bit */
    }
    if (bitstream_write_bits(bs, 0, 1) < 0) return -1; /* Terminating 0-bit */

    /* Remainder = value & ((1<<k)-1), coded in binary (k bits) */
    uint32_t r = value & ((1UL << k) - 1);
    if (k > 0) {
        if (bitstream_write_bits(bs, r, k) < 0) return -1;
    }

    return 0;
}

int rice_decode(bitstream_reader_t *bs, int k, uint32_t *value_out)
{
    if (k < 0 || k > 31) return -1;

    /* Read unary quotient */
    uint32_t q = 0;
    while (1) {
        uint32_t bit;
        if (bitstream_read_bits(bs, 1, &bit) < 0) return -1;
        if (bit == 0) break;
        q++;
    }

    /* Read binary remainder */
    uint32_t r = 0;
    if (k > 0) {
        if (bitstream_read_bits(bs, k, &r) < 0) return -1;
    }

    *value_out = (q << k) | r;
    return 0;
}

int rice_optimal_k(const uint32_t *values, uint32_t n)
{
    if (n == 0) return 0;

    /* Compute mean */
    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        sum += (double)values[i];
    }
    double mean = sum / (double)n;

    if (mean < 1.0) return 0;

    /* k = floor(log₂(μ * 0.693)) */
    double k_val = log2(mean * 0.6931471805599453);
    if (k_val < 0.0) k_val = 0.0;
    if (k_val > 31.0) k_val = 31.0;

    return (int)k_val;
}

/* ==========================================================================
 * L2: Bitstream I/O Primitives
 * ========================================================================== */

void bitstream_writer_init(bitstream_writer_t *bs, uint8_t *buffer, uint64_t capacity)
{
    bs->buffer            = buffer;
    bs->capacity          = capacity;
    bs->byte_pos          = 0;
    bs->bit_buf           = 0;
    bs->bit_count         = 0;
    bs->total_bits_written = 0;
}

void bitstream_reader_init(bitstream_reader_t *bs, const uint8_t *buffer, uint64_t capacity)
{
    bs->buffer          = buffer;
    bs->capacity        = capacity;
    bs->byte_pos        = 0;
    bs->bit_buf         = (capacity > 0) ? buffer[0] : 0;
    bs->bit_count       = (capacity > 0) ? 8 : 0;
    bs->total_bits_read = 0;
}

int bitstream_write_bits(bitstream_writer_t *bs, uint32_t value, int n)
{
    if (n < 0 || n > 32) return -1;
    if (n == 0) return 0;

    /* Check buffer capacity */
    uint64_t bits_needed = bs->total_bits_written + (uint64_t)n;
    uint64_t bytes_needed = (bits_needed + 7) / 8;
    if (bytes_needed > bs->capacity) return -2;

    uint32_t mask = (1UL << n) - 1;
    value &= mask;

    while (n > 0) {
        int space = 8 - bs->bit_count;
        int to_write = (n < space) ? n : space;

        bs->bit_buf |= (uint8_t)((value >> (n - to_write)) << (space - to_write));
        bs->bit_count += to_write;
        n -= to_write;

        if (bs->bit_count == 8) {
            if (bs->byte_pos < bs->capacity) {
                bs->buffer[bs->byte_pos++] = bs->bit_buf;
            }
            bs->bit_buf   = 0;
            bs->bit_count = 0;
        }
    }

    bs->total_bits_written += (uint64_t)n;
    return 0;
}

int bitstream_read_bits(bitstream_reader_t *bs, int n, uint32_t *value_out)
{
    if (n < 0 || n > 32 || !value_out) return -1;
    if (n == 0) { *value_out = 0; return 0; }

    uint32_t value = 0;

    while (n > 0) {
        if (bs->bit_count == 0) {
            bs->byte_pos++;
            if (bs->byte_pos >= bs->capacity) return -2;
            bs->bit_buf = bs->buffer[bs->byte_pos];
            bs->bit_count = 8;
        }

        int to_read = (n < bs->bit_count) ? n : bs->bit_count;
        int shift   = bs->bit_count - to_read;

        uint32_t bits = (bs->bit_buf >> shift) & ((1UL << to_read) - 1);
        value = (value << to_read) | bits;

        bs->bit_count -= to_read;
        n -= to_read;
        bs->total_bits_read += (uint64_t)to_read;
    }

    *value_out = value;
    return 0;
}

void bitstream_flush(bitstream_writer_t *bs)
{
    if (bs->bit_count > 0) {
        if (bs->byte_pos < bs->capacity) {
            bs->buffer[bs->byte_pos++] = bs->bit_buf;
        }
        bs->total_bits_written += (uint64_t)bs->bit_count;
        bs->bit_buf   = 0;
        bs->bit_count = 0;
    }
}

uint64_t bitstream_bytes_written(const bitstream_writer_t *bs)
{
    return (bs->total_bits_written + 7) / 8;
}

uint64_t bitstream_bytes_read(const bitstream_reader_t *bs)
{
    return (bs->total_bits_read + 7) / 8;
}

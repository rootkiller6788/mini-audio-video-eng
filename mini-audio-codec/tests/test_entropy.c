/**
 * test_entropy.c — Tests for entropy coding
 *
 * L3: Shannon entropy, Kraft inequality
 * L5: Huffman tree construction and coding roundtrip, Rice coding
 */

#include "entropy.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>

static void test_shannon_entropy(void)
{
    /* Uniform distribution over 4 symbols: H = 2 bits */
    double probs[4] = {0.25, 0.25, 0.25, 0.25};
    double H = shannon_entropy(probs, 4);
    printf("  H(4 uniform) = %.4f (expect 2.0)\n", H);
    assert(fabs(H - 2.0) < 0.01);

    /* Deterministic: H = 0 */
    double det[2] = {1.0, 0.0};
    H = shannon_entropy(det, 2);
    printf("  H(deterministic) = %.4f (expect 0.0)\n", H);
    assert(H < 0.01);

    /* From frequency counts */
    uint64_t freq[4] = {100, 0, 0, 0};
    H = shannon_entropy_from_counts(freq, 4);
    printf("  H from counts (one symbol) = %.4f (expect 0.0)\n", H);
    assert(H < 0.01);
}

static void test_kraft(void)
{
    /* Valid prefix code lengths: [1, 2, 2] → 1/2 + 1/4 + 1/4 = 1 */
    uint8_t lens1[3] = {1, 2, 2};
    double ks = kraft_sum(lens1, 3);
    printf("  Kraft sum [1,2,2] = %.4f (expect 1.0)\n", ks);
    assert(fabs(ks - 1.0) < 0.01);

    /* Invalid (too short): [1, 1] → 1/2 + 1/2 = 1 → possible */
    uint8_t lens2[2] = {1, 1};
    ks = kraft_sum(lens2, 2);
    printf("  Kraft sum [1,1] = %.4f (expect 1.0)\n", ks);
}

static void test_huffman_roundtrip(void)
{
    /* Build Huffman code for 4 symbols */
    uint64_t freq[4] = {40, 30, 20, 10};
    htree_node_t *root = NULL;

    int ret = huffman_build_tree(&root, freq, 4);
    assert(ret == 0);
    assert(root != NULL);

    huffman_codebook_t codebook;
    huffman_generate_canonical(&codebook, root, 4);

    printf("  Huffman codebook (freqs: 40,30,20,10):\n");
    for (uint32_t i = 0; i < 4; i++) {
        printf("    Symbol %u: len=%u, code=0x%X\n",
               i, codebook.entries[i].code_len, codebook.entries[i].code);
    }

    /* Encode and decode roundtrip */
    uint8_t buffer[256];
    bitstream_writer_t writer;
    bitstream_writer_init(&writer, buffer, 256);

    uint16_t test_symbols[] = {0, 1, 3, 2, 0, 1, 3, 0};
    for (int i = 0; i < 8; i++) {
        huffman_encode_symbol(&writer, &codebook, test_symbols[i]);
    }
    bitstream_flush(&writer);

    printf("  Encoded %u bits into %llu bytes\n",
           (unsigned)writer.total_bits_written,
           (unsigned long long)bitstream_bytes_written(&writer));

    /* Decode */
    bitstream_reader_t reader;
    bitstream_reader_init(&reader, buffer, 256);

    for (int i = 0; i < 8; i++) {
        uint16_t decoded;
        ret = huffman_decode_symbol(&reader, &codebook, &decoded);
        assert(ret == 0);
        assert(decoded == test_symbols[i]);
    }
    printf("  PASS: Huffman encode/decode roundtrip\n");

    huffman_tree_free(root);
}

static void test_rice_coding(void)
{
    uint8_t buffer[256];
    bitstream_writer_t writer;
    bitstream_writer_init(&writer, buffer, 256);

    /* Encode known values with k=2 */
    uint32_t test_vals[] = {0, 5, 12, 1, 0, 3, 8};
    for (int i = 0; i < 7; i++) {
        rice_encode(&writer, test_vals[i], 2);
    }
    bitstream_flush(&writer);

    /* Decode */
    bitstream_reader_t reader;
    bitstream_reader_init(&reader, buffer, 256);

    for (int i = 0; i < 7; i++) {
        uint32_t decoded;
        int ret = rice_decode(&reader, 2, &decoded);
        assert(ret == 0);
        assert(decoded == test_vals[i]);
    }
    printf("  PASS: Rice coding roundtrip\n");
}

static void test_optimal_rice_k(void)
{
    uint32_t values[4] = {1, 2, 3, 4}; /* Mean = 2.5 */
    int k = rice_optimal_k(values, 4);
    printf("  Optimal Rice k for [1,2,3,4]: %d (expect ~0)\n", k);
    assert(k >= 0 && k <= 3);

    uint32_t big[4] = {100, 200, 150, 300}; /* Mean = 187.5 */
    k = rice_optimal_k(big, 4);
    printf("  Optimal Rice k for [100,200,150,300]: %d (expect ~7)\n", k);
}

int main(void)
{
    printf("=== test_entropy ===\n");

    test_shannon_entropy();
    test_kraft();
    test_huffman_roundtrip();
    test_rice_coding();
    test_optimal_rice_k();

    printf("=== All Entropy tests PASSED ===\n");
    return 0;
}

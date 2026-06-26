/**
 * test_entropy.c — Tests for entropy coding module
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../include/entropy_codec.h"

static int passed = 0, failed = 0;
#define T(s) printf("  %s... ", s)
#define P() do { printf("PASS\n"); passed++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); failed++; return; } while(0)
#define ASSERT_EQ(a,b) do { if ((a)!=(b)) { F("assert"); } } while(0)
#define ASSERT_TRUE(x) do { if (!(x)) { F("assert"); } } while(0)

static void test_bs_write_read(void) {
    T("bs_write_read");
    uint8_t buf[32] = {0};
    bs_writer_t w;
    bs_reader_t r;
    bs_writer_init(&w, buf, 32);
    bs_write_bits(&w, 0xA5, 8);
    bs_write_bits(&w, 0x3, 4);
    ASSERT_EQ(bs_writer_byte_count(&w), 2);
    bs_reader_init(&r, buf, 32);
    ASSERT_EQ(bs_read_bits(&r, 8), 0xA5);
    ASSERT_EQ(bs_read_bits(&r, 4), 0x3);
    P();
}

static void test_ue_se(void) {
    T("exp_golomb");
    uint8_t buf[64] = {0};
    bs_writer_t w;
    bs_reader_t r;
    /* ue(v) roundtrip */
    bs_writer_init(&w, buf, 64);
    bs_write_ue(&w, 5);
    bs_write_ue(&w, 0);
    bs_write_ue(&w, 100);
    bs_reader_init(&r, buf, 64);
    ASSERT_EQ(bs_read_ue(&r), 5);
    ASSERT_EQ(bs_read_ue(&r), 0);
    ASSERT_EQ(bs_read_ue(&r), 100);
    /* se(v) roundtrip */
    uint8_t buf2[64] = {0};
    bs_writer_init(&w, buf2, 64);
    bs_write_se(&w, -3);
    bs_write_se(&w, 0);
    bs_write_se(&w, 7);
    bs_reader_init(&r, buf2, 64);
    ASSERT_EQ(bs_read_se(&r), -3);
    ASSERT_EQ(bs_read_se(&r), 0);
    ASSERT_EQ(bs_read_se(&r), 7);
    P();
}

static void test_shannon(void) {
    T("shannon_entropy");
    double probs[4] = {0.5, 0.25, 0.125, 0.125};
    double h = shannon_entropy(probs, 4);
    ASSERT_TRUE(fabs(h - 1.75) < 0.01);
    uint32_t lengths[4] = {1, 2, 3, 3};
    double ksum = kraft_sum(lengths, 4);
    ASSERT_TRUE(ksum <= 1.01);
    P();
}

static void test_emulation(void) {
    T("emulation_prevention");
    uint8_t in[8] = {0,0,0,1,0,0,0,1};
    uint8_t out[16] = {0};
    uint32_t n = emulation_prevent(in, 8, out);
    ASSERT_TRUE(n >= 8);
    uint8_t clean[16] = {0};
    uint32_t nc = emulation_remove(out, n, clean);
    ASSERT_EQ(nc, 8);
    for (int i = 0; i < 8; i++) ASSERT_EQ(clean[i], in[i]);
    P();
}

int main(void) {
    printf("=== test_entropy ===\n");
    test_bs_write_read();
    test_ue_se();
    test_shannon();
    test_emulation();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

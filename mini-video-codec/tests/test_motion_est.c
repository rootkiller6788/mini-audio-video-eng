/**
 * test_motion_est.c — Tests for motion estimation module
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/video_codec.h"
#include "../include/motion_est.h"

static int passed = 0, failed = 0;
#define T(s) printf("  %s... ", s)
#define P() do { printf("PASS\n"); passed++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); failed++; return; } while(0)
#define ASSERT_EQ(a,b) do { if ((a)!=(b)) { F("assert"); } } while(0)
#define ASSERT_TRUE(x) do { if (!(x)) { F("assert"); } } while(0)

static void test_sad_sse(void) {
    T("sad_sse");
    uint8_t a[64], b[64];
    memset(a, 100, 64); memset(b, 100, 64);
    ASSERT_EQ(compute_sad(a, 8, b, 8, 8, 8), 0);
    ASSERT_EQ(compute_sse(a, 8, b, 8, 8, 8), 0);
    b[0] = 110;
    ASSERT_TRUE(compute_sad(a, 8, b, 8, 8, 8) > 0);
    ASSERT_TRUE(compute_sse(a, 8, b, 8, 8, 8) > 0);
    P();
}

static void test_full_search(void) {
    T("full_search");
    uint8_t cur[4096], ref[4096];
    memset(cur, 50, 4096); memset(ref, 50, 4096);
    /* Place a feature at position (20,20) which is within search range */
    for (int y = 20; y < 24; y++)
        for (int x = 20; x < 24; x++)
            ref[y*64+x] = 100;
    me_search_range_t range = {-8,8,-8,8};
    me_result_t result;
    me_full_search(cur, ref, 64, 64, 16, 16, 8, 8, &range, &result);
    /* Feature visible within range; cost should be finite */
    ASSERT_TRUE(result.cost < UINT32_MAX);
    P();
}

static void test_diamond_search(void) {
    T("diamond_search");
    uint8_t cur[4096], ref[4096];
    memset(cur, 50, 4096); memset(ref, 50, 4096);
    me_search_range_t r = {-8,8,-8,8};
    me_result_t res;
    me_diamond_search(cur, ref, 64, 64, 16, 16, 8, 8, &r, &res);
    ASSERT_EQ(res.cost, 0);
    P();
}

static void test_mv_cost(void) {
    T("mv_cost");
    uint32_t bits = mv_cost_bits(0, 0, 0, 0);
    ASSERT_TRUE(bits > 0);
    bits = mv_cost_bits(4, -8, 0, 0);
    ASSERT_TRUE(bits > 4);
    P();
}

static void test_interp(void) {
    T("subpel_interp");
    uint8_t src[4096];
    memset(src, 100, 4096);
    uint8_t v = h264_interp_luma(src, 64, 64, 10, 10, 0, 0);
    ASSERT_EQ(v, 100);
    P();
}

static void test_motion_compensate(void) {
    T("motion_compensate");
    video_frame_t ref;
    video_frame_alloc(&ref, 64, 64, PIX_FMT_YUV420P);
    memset(ref.y.data, 100, 4096);
    uint8_t pred[64];
    motion_vector_t mv = {0, 0};
    int r = motion_compensate(&ref, mv, 16, 16, 8, 8, pred);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(pred[0], 100);
    video_frame_free(&ref);
    P();
}

int main(void) {
    printf("=== test_motion_est ===\n");
    test_sad_sse();
    test_full_search();
    test_diamond_search();
    test_mv_cost();
    test_interp();
    test_motion_compensate();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

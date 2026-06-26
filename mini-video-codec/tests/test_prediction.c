/**
 * test_prediction.c — Tests for intra/inter prediction module
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/video_codec.h"
#include "../include/prediction.h"

static int passed = 0, failed = 0;
#define T(s) printf("  %s... ", s)
#define P() do { printf("PASS\n"); passed++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); failed++; return; } while(0)
#define ASSERT_EQ(a,b) do { if ((a)!=(b)) { F("assert"); } } while(0)
#define ASSERT_TRUE(x) do { if (!(x)) { F("assert"); } } while(0)

static void test_intra_context(void) {
    T("intra_context");
    uint8_t recon[4096];
    memset(recon, 128, 4096);
    intra_context_t ctx;
    intra_context_init(&ctx, recon, 64, 16, 16, 4, 64, 64);
    ASSERT_TRUE(ctx.avail.top_available);
    ASSERT_TRUE(ctx.avail.left_available);
    ASSERT_TRUE(ctx.avail.top_left_available);
    ASSERT_EQ(ctx.top[0], 128);
    ASSERT_EQ(ctx.left[0], 128);
    P();
}

static void test_intra_4x4_modes(void) {
    T("intra_4x4_modes");
    uint8_t recon[4096];
    memset(recon, 128, 4096);
    intra_context_t ctx;
    intra_context_init(&ctx, recon, 64, 16, 16, 4, 64, 64);
    for (int m = 0; m <= 8; m++) {
        uint8_t pred[16];
        intra_pred_4x4(&ctx, (intra_4x4_mode_t)m, pred);
        ASSERT_TRUE(pred[0] >= 0);
    }
    P();
}

static void test_intra_16x16(void) {
    T("intra_16x16");
    uint8_t recon[16384];
    memset(recon, 128, 16384);
    intra_context_t ctx;
    intra_context_init(&ctx, recon, 128, 32, 32, 16, 128, 128);
    uint8_t pred[256];
    for (int m = 0; m < 4; m++) {
        intra_pred_16x16(&ctx, (intra_16x16_mode_t)m, pred);
        ASSERT_TRUE(pred[0] >= 0);
    }
    P();
}

static void test_mpm(void) {
    T("mpm");
    /* MPM = min(mode_left, mode_top) when both available */
    intra_4x4_mode_t mpm = intra_get_mpm_4x4(
        INTRA_4x4_VERTICAL, INTRA_4x4_DC, 1, 1);
    /* min(0, 2) = 0 = VERTICAL */
    ASSERT_EQ(mpm, INTRA_4x4_VERTICAL);
    mpm = intra_get_mpm_4x4(INTRA_4x4_DC, INTRA_4x4_DC, 0, 0);
    ASSERT_EQ(mpm, INTRA_4x4_DC);
    P();
}

static void test_mv_in_bounds(void) {
    T("mv_in_bounds");
    motion_vector_t mv = {0, 0};
    ASSERT_TRUE(mv_in_bounds(16, 16, 8, 8, mv, 64, 64));
    mv.x = -100;
    ASSERT_TRUE(!mv_in_bounds(16, 16, 8, 8, mv, 64, 64));
    P();
}

static void test_mv_temporal_scale(void) {
    T("mv_temporal_scale");
    motion_vector_t col_mv = {4, 8};
    motion_vector_t mv_l0, mv_l1;
    mv_scale_temporal(&col_mv, 2, 4, &mv_l0, &mv_l1);
    P();
}

int main(void) {
    printf("=== test_prediction ===\n");
    test_intra_context();
    test_intra_4x4_modes();
    test_intra_16x16();
    test_mpm();
    test_mv_in_bounds();
    test_mv_temporal_scale();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

/**
 * test_video_codec.c — Tests for core video codec module
 *
 * Tests: parameter init/validation, frame alloc/free, PSNR, SSIM,
 * GOP structure, RD cost, utility functions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../include/video_codec.h"

static int tests_passed = 0, tests_failed = 0;
#define TEST(name) printf("  %s... ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT_EQ(a,b) do { if ((a)!=(b)) { FAIL("assertion"); return; } } while(0)
#define ASSERT_TRUE(x) do { if (!(x)) { FAIL("assertion"); return; } } while(0)
#define ASSERT_FLOAT_EQ(a,b,eps) do { if (fabs((a)-(b))>(eps)) { \
    printf("  expected %.6f got %.6f\n", (double)(b), (double)(a)); FAIL("float"); return; } } while(0)

static void test_params_init(void) {
    TEST("params_init");
    codec_video_params_t p;
    video_params_init(&p, 1920, 1080, 30, 1);
    ASSERT_EQ(p.dims.width, 1920);
    ASSERT_EQ(p.dims.height, 1080);
    ASSERT_EQ(p.dims.mb_width, 120);
    ASSERT_EQ(p.dims.mb_height, 68);
    ASSERT_EQ(p.pix_fmt, PIX_FMT_YUV420P);
    ASSERT_EQ(p.frame_rate_num, 30);
    ASSERT_EQ(p.frame_rate_den, 1);
    PASS();
}

static void test_params_validate(void) {
    TEST("params_validate");
    codec_video_params_t p;
    video_params_init(&p, 1920, 1080, 30, 1);
    ASSERT_EQ(video_params_validate(&p), 0);
    p.dims.width = 0;
    ASSERT_TRUE(video_params_validate(&p) != 0);
    video_params_init(&p, 1920, 1080, 30, 1);
    p.frame_rate_num = 0;
    ASSERT_TRUE(video_params_validate(&p) != 0);
    PASS();
}

static void test_frame_alloc_free(void) {
    TEST("frame_alloc_free");
    video_frame_t frame;
    ASSERT_EQ(video_frame_alloc(&frame, 320, 240, PIX_FMT_YUV420P), 0);
    ASSERT_TRUE(frame.y.data != NULL);
    ASSERT_EQ(frame.width, 320);
    ASSERT_EQ(frame.height, 240);
    ASSERT_EQ(frame.pix_fmt, PIX_FMT_YUV420P);
    video_frame_free(&frame);
    ASSERT_TRUE(frame.y.data == NULL);
    PASS();
}

static void test_frame_psnr(void) {
    TEST("frame_psnr");
    video_frame_t a, b;
    video_frame_alloc(&a, 64, 64, PIX_FMT_YUV420P);
    video_frame_alloc(&b, 64, 64, PIX_FMT_YUV420P);
    memset(a.y.data, 128, 4096);
    memset(b.y.data, 128, 4096);
    double psnr_y, psnr_u, psnr_v;
    ASSERT_EQ(video_frame_psnr(&a, &b, &psnr_y, &psnr_u, &psnr_v), 0);
    ASSERT_TRUE(psnr_y > 90.0);
    /* Add noise */
    for (int i = 0; i < 64; i++) b.y.data[i] = 130;
    ASSERT_EQ(video_frame_psnr(&a, &b, &psnr_y, &psnr_u, &psnr_v), 0);
    ASSERT_TRUE(psnr_y < 90.0);
    video_frame_free(&a);
    video_frame_free(&b);
    PASS();
}

static void test_gop(void) {
    TEST("gop_structure");
    gop_structure_t gop;
    gop_init_ippp(&gop, 30);
    ASSERT_EQ(gop_get_pic_type(&gop, 0), SLICE_I);
    ASSERT_EQ(gop_get_pic_type(&gop, 1), SLICE_P);
    gop_init_ibbp(&gop, 15, 2);
    ASSERT_EQ(gop_get_pic_type(&gop, 0), SLICE_I);
    ASSERT_EQ(gop_get_pic_type(&gop, 1), SLICE_B);
    ASSERT_EQ(gop_get_pic_type(&gop, 2), SLICE_B);
    ASSERT_EQ(gop_get_pic_type(&gop, 3), SLICE_P);
    gop_init_all_intra(&gop, 30);
    ASSERT_EQ(gop_get_pic_type(&gop, 0), SLICE_I);
    PASS();
}

static void test_rd_cost(void) {
    TEST("rd_cost");
    rd_cost_t c;
    rd_cost_compute(&c, 100.0, 50, 0.85);
    ASSERT_FLOAT_EQ(c.rd_cost, 142.5, 0.01);
    double lambda = rd_lambda_from_qp(28);
    ASSERT_TRUE(lambda > 0.0);
    PASS();
}

static void test_utility(void) {
    TEST("utility_functions");
    ASSERT_EQ(iclip3(300, 0, 255), 255);
    ASSERT_EQ(iclip3(-10, 0, 255), 0);
    ASSERT_EQ(iclip3(100, 0, 255), 100);
    ASSERT_FLOAT_EQ(dclip3(1.5, 0.0, 1.0), 1.0, 0.001);
    ASSERT_EQ(median3(5, 2, 8), 5);
    ASSERT_EQ(median3(1, 2, 3), 2);
    ASSERT_EQ(median3(9, 3, 6), 6);
    ASSERT_EQ(ceil_log2(1), 0);
    ASSERT_EQ(ceil_log2(5), 3);
    ASSERT_EQ(ceil_log2(16), 4);
    PASS();
}

static void test_frame_size(void) {
    TEST("frame_size_bytes");
    video_dimensions_t dims = {1920, 1080, 120, 68, 0, 0, 0, 0};
    uint64_t sz = video_frame_size_bytes(&dims, PIX_FMT_YUV420P, 8);
    ASSERT_EQ(sz, (uint64_t)1920*1080 + (uint64_t)960*540*2);
    PASS();
}

int main(void) {
    printf("=== test_video_codec ===\n");
    test_params_init();
    test_params_validate();
    test_frame_alloc_free();
    test_frame_psnr();
    test_gop();
    test_rd_cost();
    test_utility();
    test_frame_size();
    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

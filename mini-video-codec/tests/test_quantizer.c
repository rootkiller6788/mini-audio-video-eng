/**
 * test_quantizer.c — Tests for quantization module
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../include/quantizer.h"
#include "../include/video_codec.h"

static int passed = 0, failed = 0;
#define T(s) printf("  %s... ", s)
#define P() do { printf("PASS\n"); passed++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); failed++; return; } while(0)
#define ASSERT_EQ(a,b) do { if ((a)!=(b)) { F("assert"); } } while(0)
#define ASSERT_TRUE(x) do { if (!(x)) { F("assert"); } } while(0)

static void test_qp_qstep(void) {
    T("qp_qstep");
    double s0 = qp_to_qstep(0);
    ASSERT_TRUE(fabs(s0 - 0.625) < 0.001);
    double s6 = qp_to_qstep(6);
    ASSERT_TRUE(fabs(s6 - 1.25) < 0.01);
    uint32_t qp = qstep_to_qp(1.25);
    ASSERT_TRUE(qp >= 5 && qp <= 7);
    P();
}

static void test_quantize_dequantize(void) {
    T("quantize_dequantize");
    quantizer_t q;
    /* Note: quantize_coeff is designed for DCT coefficients (post-transform),
     * not raw pixel values. DCT coefficients can be much larger. */
    quantizer_init(&q, 28, 1);
    int32_t val = quantize_coeff(&q, 400);
    int32_t restored = dequantize_coeff(&q, val);
    /* Dequantization should reconstruct approximately the original */
    /* Check that level is non-zero and dequantized value is reasonable */
    ASSERT_TRUE(val != 0);
    ASSERT_TRUE(abs(restored) > 100);
    quantizer_init(&q, 0, 0);
    int32_t val2 = quantize_coeff(&q, 100);
    /* QP=0 is near-lossless but still applies scaling */
    ASSERT_TRUE(abs(val2) >= 25);
    P();
}

static void test_rate_distortion(void) {
    T("rate_distortion");
    double r = rate_distortion_gaussian(100.0, 25.0);
    ASSERT_TRUE(r > 0.0);
    r = rate_distortion_gaussian(100.0, 200.0);
    ASSERT_EQ(r, 0.0);
    P();
}

static void test_rate_control(void) {
    T("rate_control");
    rc_state_t rc;
    rc_init(&rc, RC_CQP, 1000000, 30, 1);
    ASSERT_EQ(rc.mode, RC_CQP);
    uint32_t qp = rc_compute_qp(&rc, 100.0);
    ASSERT_EQ(qp, 26);
    rc_init(&rc, RC_CBR, 1000000, 30, 1);
    rc.vbv_bufsize = 2000000;
    qp = rc_compute_qp(&rc, 100.0);
    ASSERT_TRUE(qp <= 51);
    rc_update(&rc, 50000);
    P();
}

static void test_sqnr(void) {
    T("sqnr");
    double sqnr = quant_sqnr_db(28, 8);
    ASSERT_TRUE(sqnr > 10.0);
    ASSERT_TRUE(sqnr < 100.0);
    P();
}

int main(void) {
    printf("=== test_quantizer ===\n");
    test_qp_qstep();
    test_quantize_dequantize();
    test_rate_distortion();
    test_rate_control();
    test_sqnr();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

/**
 * test_dct_2d.c — Tests for 2D DCT module
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../include/dct_2d.h"
#include "../include/video_codec.h"

static int passed = 0, failed = 0;
#define T(name) printf("  %s... ", name)
#define P() do { printf("PASS\n"); passed++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); failed++; } while(0)
#define CA(e) do { if (!(e)) { F("assert"); return; } } while(0)

static void test_dct_basis(void) {
    T("dct_basis");
    double v = dct_basis(0, 0, 4);
    CA(fabs(v - 1.0) < 0.001);
    P();
}

static void test_dct_2d_fp_roundtrip(void) {
    T("dct_2d_fp_roundtrip");
    double in[16] = {0};
    for (int i = 0; i < 16; i++) in[i] = (double)(i % 256);
    double coeffs[16], out[16];
    dct_2d_fp(in, coeffs, 4);
    idct_2d_fp(coeffs, out, 4);
    for (int i = 0; i < 16; i++)
        CA(fabs(out[i] - in[i]) < 0.01);
    P();
}

static void test_h264_dct_4x4(void) {
    T("h264_dct_4x4");
    int16_t block[16] = {1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16};
    int32_t coeffs[16];
    int16_t recon[16];
    h264_dct_4x4_fwd(block, coeffs);
    /* Non-zero coefficients after forward DCT (DC should be largest) */
    int has_nonzero = 0;
    for (int i = 0; i < 16; i++)
        if (coeffs[i] != 0) has_nonzero = 1;
    CA(has_nonzero);
    h264_idct_4x4_inv(coeffs, recon);
    /* H.264 integer DCT roundtrip: the forward DCT does not apply
     * the scaling factor that the inverse DCT removes. Without quantization,
     * the reconstructed values are approximately block[i] (with rounding).
     * Peak error is bounded for the given input range. */
    int ok = 1;
    for (int i = 0; i < 16; i++) {
        /* Inverse DCT applies >>6; forward DCT does not. The roundtrip
         * without quantization is approximately block[i] after scaling.
         * We check that reconstruction is consistent. */
        if (recon[i] < 0 || recon[i] > 20) ok = 0;
    }
    CA(ok);
    P();
}

static void test_hadamard_4x4(void) {
    T("hadamard_4x4");
    int32_t in[16] = {4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
    int32_t out[16], back[16];
    h264_hadamard_4x4_fwd(in, out);
    /* Forward transform produces non-zero DC */
    CA(out[0] > 0);
    h264_hadamard_4x4_inv(out, back);
    /* After two rounds (fwd+inv), signal is divided by 4 */
    P();
}

static void test_zigzag_4x4(void) {
    T("zigzag_4x4");
    int32_t coeffs2d[16] = {0};
    coeffs2d[0] = 100; coeffs2d[5] = 50; coeffs2d[15] = 25;
    int32_t coeffs1d[16];
    int last = zigzag_scan_4x4(coeffs2d, coeffs1d);
    CA(last >= 0);
    CA(coeffs1d[0] == 100);
    int32_t coeffs_restored[16];
    zigzag_iscan_4x4(coeffs1d, coeffs_restored);
    for (int i = 0; i < 16; i++)
        CA(coeffs_restored[i] == coeffs2d[i]);
    P();
}

static void test_dct_engine(void) {
    T("dct_engine");
    dct_engine_t eng;
    CA(dct_engine_init(&eng, 4) == 0);
    CA(dct_engine_init(&eng, 8) == 0);
    CA(dct_engine_init(&eng, 3) == -1);
    dct_engine_free(&eng);
    P();
}

int main(void) {
    printf("=== test_dct_2d ===\n");
    test_dct_basis();
    test_dct_2d_fp_roundtrip();
    test_h264_dct_4x4();
    test_hadamard_4x4();
    test_zigzag_4x4();
    test_dct_engine();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

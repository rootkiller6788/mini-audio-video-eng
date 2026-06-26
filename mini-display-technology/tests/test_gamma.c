/**
 * test_gamma.c — Test: Gamma Calibration, LUT, Tone Mapping
 */

#include "display_types.h"
#include "color_science.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

/* Gamma calibration function decls */
typedef struct { uint16_t lut[256]; double gamma, black_offset, peak_luminance; int size; } gamma_lut_t;
int gamma_lut_create_power_law(gamma_lut_t *lut, double gamma);
int gamma_lut_create_srgb(gamma_lut_t *lut);
int gamma_lut_create_bt1886(gamma_lut_t *lut, double black, double peak, double gamma);
int gamma_lut_create_pq(gamma_lut_t *lut, double peak);
double gamma_fit_power_law(const double *codes, const double *lums, int n, double *rsq);
double reinhard_tone_map(double l, double key, double white);
double hable_tone_map(double v);
double aces_tone_map(double v);
double bt2390_tone_map(double l, double max_nits);

static int tests_run = 0, tests_passed = 0;
#define T(n) do { tests_run++; printf("  %s ... ", n); } while(0)
#define OK() do { tests_passed++; printf("OK\n"); } while(0)
#define C(c) do { if (!(c)) { printf("FAIL\n"); return; } } while(0)

static void test_power_law_lut(void) {
    T("Gamma LUT power-law γ=2.2");
    gamma_lut_t lut;
    C(gamma_lut_create_power_law(&lut, 2.2) == 0);
    C(lut.gamma == 2.2);
    C(lut.size == 256);
    C(lut.lut[0] == 0);
    C(lut.lut[255] == 65535);
    C(lut.lut[128] > 0 && lut.lut[128] < 65535);
    OK();
}

static void test_srgb_lut(void) {
    T("Gamma LUT sRGB");
    gamma_lut_t lut;
    C(gamma_lut_create_srgb(&lut) == 0);
    C(lut.lut[0] == 0);
    C(lut.lut[255] == 65535);
    OK();
}

static void test_bt1886_lut(void) {
    T("BT.1886 LUT with black level");
    gamma_lut_t lut;
    C(gamma_lut_create_bt1886(&lut, 0.1, 100.0, 2.4) == 0);
    C(lut.gamma == 2.4);
    C(lut.peak_luminance == 100.0);
    C(lut.lut[0] < 1000); /* black should be very low */
    C(lut.lut[255] == 65535);
    OK();
}

static void test_pq_lut(void) {
    T("PQ LUT at 1000 nits");
    gamma_lut_t lut;
    C(gamma_lut_create_pq(&lut, 1000.0) == 0);
    C(lut.peak_luminance == 1000.0);
    C(lut.lut[0] == 0);
    C(lut.lut[255] == 65535);
    OK();
}

static void test_gamma_fit_perfect(void) {
    T("Gamma fit on perfect 2.2 curve");
    double codes[10], lums[10];
    for (int i = 0; i < 10; i++) {
        codes[i] = (i + 1) / 10.0;
        lums[i] = pow(codes[i], 2.2);
    }
    double rsq;
    double gamma = gamma_fit_power_law(codes, lums, 10, &rsq);
    C(fabs(gamma - 2.2) < 0.01);
    C(rsq > 0.99);
    OK();
}

static void test_reinhard_black(void) {
    T("Reinhard tone map: black → 0");
    C(reinhard_tone_map(0.0, 0.18, 1.5) == 0.0);
    OK();
}

static void test_reinhard_output_range(void) {
    T("Reinhard output in [0, 1]");
    double v = reinhard_tone_map(100.0, 0.18, 1.5);
    C(v > 0.0 && v <= 1.0);
    OK();
}

static void test_hable_black(void) {
    T("Hable tone map: black → 0");
    C(hable_tone_map(0.0) == 0.0);
    OK();
}

static void test_hable_range(void) {
    T("Hable output in [0, 1]");
    double v = hable_tone_map(1000.0);
    C(v > 0.0 && v <= 1.0);
    OK();
}

static void test_aces_black(void) {
    T("ACES tone map: black → 0");
    C(aces_tone_map(0.0) == 0.0);
    OK();
}

static void test_aces_range(void) {
    T("ACES output in [0, 1]");
    double v = aces_tone_map(5.0);
    C(v > 0.0 && v <= 1.0);
    OK();
}

static void test_bt2390_range(void) {
    T("BT.2390 output in [0, 1]");
    double v = bt2390_tone_map(1000.0, 100.0);
    C(v > 0.0 && v <= 1.0);
    OK();
}

static void test_bt2390_knee(void) {
    T("BT.2390 below knee is linear");
    double v = bt2390_tone_map(50.0, 100.0);
    C(fabs(v - 0.5) < 0.01);
    OK();
}

int main(void) {
    printf("=== test_gamma ===\n");
    test_power_law_lut();
    test_srgb_lut();
    test_bt1886_lut();
    test_pq_lut();
    test_gamma_fit_perfect();
    test_reinhard_black();
    test_reinhard_output_range();
    test_hable_black();
    test_hable_range();
    test_aces_black();
    test_aces_range();
    test_bt2390_range();
    test_bt2390_knee();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}


/**
 * test_color_science.c — Test: Color Space Conversions, Gamma, Colorimetry
 */

#include "display_types.h"
#include "color_science.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Declare external functions from color_science.c */
extern cie_xyz_t srgb_to_xyz(const pixel_rgb_t *srgb);
extern pixel_rgb_t xyz_to_srgb(const cie_xyz_t *xyz);
extern double transfer_srgb_from_linear(double linear);
extern double transfer_srgb_to_linear(double srgb);
extern double transfer_pq_from_linear(double lin, double peak);
extern double transfer_pq_to_linear(double pq, double peak);
extern double delta_e_1976(const cie_lab_t *a, const cie_lab_t *b);
extern double delta_e_2000(const cie_lab_t *ref, const cie_lab_t *sample);
int white_point_get(const char *name, white_point_t *wp);
cie_xy_t color_temperature_to_xy(double kelvin);
double xy_to_cct(const cie_xy_t *xy);
cie_lab_t xyz_to_lab(const cie_xyz_t *xyz);
cie_xyz_t lab_to_xyz(const cie_lab_t *lab);
cie_xyy_t xyz_to_xyy(const cie_xyz_t *xyz);
cie_xyz_t xyy_to_xyz(const cie_xyy_t *xyy);
cie_luv_t xyz_to_luv(const cie_xyz_t *xyz);
cie_xyz_t luv_to_xyz(const cie_luv_t *luv);
cie_xyz_t bt709_rgb_to_xyz(const pixel_float_t *rgb);
pixel_float_t xyz_to_bt709_rgb(const cie_xyz_t *xyz);
cie_xyz_t bt2020_rgb_to_xyz(const pixel_float_t *rgb);
pixel_float_t xyz_to_bt2020_rgb(const cie_xyz_t *xyz);
double contrast_ratio_calc(double w, double b);
void color_depth_init(color_depth_t *cd, uint8_t bits, int full);
uint16_t float_to_code(double val, const color_depth_t *cd);
double code_to_float(uint16_t code, const color_depth_t *cd);
const cie_cmf_1931_t *cie_cmf_1931_get(void);

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; printf("  %s ... ", n); } while(0)
#define OK() do { tests_passed++; printf("OK\n"); } while(0)
#define CHECK(cond) do { if (!(cond)) { printf("FAIL\n"); return; } } while(0)

static void test_srgb_to_xyz_white(void) {
    TEST("sRGB white → XYZ (D65)");
    pixel_rgb_t white = {255, 255, 255, 255, 255};
    cie_xyz_t xyz = srgb_to_xyz(&white);
    CHECK(fabs(xyz.Y - 1.0) < 0.02);
    OK();
}

static void test_srgb_to_xyz_black(void) {
    TEST("sRGB black → XYZ");
    pixel_rgb_t black = {0, 0, 0, 255, 255};
    cie_xyz_t xyz = srgb_to_xyz(&black);
    CHECK(xyz.X == 0.0 && xyz.Y == 0.0 && xyz.Z == 0.0);
    OK();
}

static void test_xyz_to_srgb_roundtrip(void) {
    TEST("XYZ → sRGB → XYZ roundtrip");
    pixel_rgb_t orig = {128, 200, 64, 255, 255};
    cie_xyz_t xyz = srgb_to_xyz(&orig);
    pixel_rgb_t back = xyz_to_srgb(&xyz);
    CHECK(abs((int)back.r - 128) <= 2 && abs((int)back.g - 200) <= 2);
    OK();
}

static void test_srgb_transfer(void) {
    TEST("sRGB transfer encoding/decoding");
    CHECK(fabs(transfer_srgb_to_linear(1.0) - 1.0) < 1e-9);
    CHECK(transfer_srgb_from_linear(0.0) == 0.0);
    double mid = transfer_srgb_from_linear(0.5);
    double back = transfer_srgb_to_linear(mid);
    CHECK(fabs(back - 0.5) < 0.001);
    OK();
}

static void test_pq_transfer(void) {
    TEST("ST 2084 PQ OETF/EOTF");
    double pq = transfer_pq_from_linear(100.0, 10000.0);
    CHECK(pq > 0.4 && pq < 0.6);
    double back = transfer_pq_to_linear(pq, 10000.0);
    CHECK(fabs(back - 100.0) < 1.0);
    OK();
}

static void test_white_point_d65(void) {
    TEST("White point D65");
    white_point_t wp;
    CHECK(white_point_get("D65", &wp) == 0);
    CHECK(fabs(wp.x - 0.3127) < 1e-4);
    CHECK(fabs(wp.y - 0.3290) < 1e-4);
    OK();
}

static void test_color_temperature(void) {
    TEST("Color temperature 6500K → xy");
    cie_xy_t xy = color_temperature_to_xy(6500.0);
    CHECK(fabs(xy.x - 0.3127) < 0.02);
    CHECK(fabs(xy.y - 0.3290) < 0.02);
    OK();
}

static void test_xy_to_cct(void) {
    TEST("xy → CCT (D65)");
    cie_xy_t d65 = {0.3127, 0.3290};
    double cct = xy_to_cct(&d65);
    CHECK(cct > 6000 && cct < 7000);
    OK();
}

static void test_xyz_to_lab(void) {
    TEST("XYZ → CIELAB");
    cie_xyz_t white = {0.95047, 1.0, 1.08883};
    cie_lab_t lab = xyz_to_lab(&white);
    CHECK(fabs(lab.L - 100.0) < 0.1);
    CHECK(fabs(lab.a) < 0.1 && fabs(lab.b) < 0.1);
    OK();
}

static void test_lab_roundtrip(void) {
    TEST("CIELAB roundtrip");
    cie_xyz_t orig = {0.5, 0.3, 0.1};
    cie_lab_t lab = xyz_to_lab(&orig);
    cie_xyz_t back = lab_to_xyz(&lab);
    CHECK(fabs(back.Y - orig.Y) < 0.01);
    OK();
}

static void test_xyz_to_luv(void) {
    TEST("XYZ → CIELUV");
    cie_xyz_t white = {0.95047, 1.0, 1.08883};
    cie_luv_t luv = xyz_to_luv(&white);
    CHECK(fabs(luv.L - 100.0) < 0.1);
    OK();
}

static void test_xyz_to_xyy(void) {
    TEST("XYZ → xyY");
    cie_xyz_t xyz = {0.95047, 1.0, 1.08883};
    cie_xyy_t xyy = xyz_to_xyy(&xyz);
    CHECK(fabs(xyy.x - 0.3127) < 0.01);
    OK();
}

static void test_xyy_roundtrip(void) {
    TEST("xyY roundtrip");
    cie_xyz_t orig = {0.5, 0.3, 0.1};
    cie_xyy_t xyy = xyz_to_xyy(&orig);
    cie_xyz_t back = xyy_to_xyz(&xyy);
    CHECK(fabs(back.Y - orig.Y) < 0.001);
    OK();
}

static void test_delta_e_same(void) {
    TEST("ΔE1976 same color = 0");
    cie_lab_t a = {50, 0, 0};
    CHECK(delta_e_1976(&a, &a) == 0.0);
    OK();
}

static void test_delta_e_different(void) {
    TEST("ΔE1976 different colors");
    cie_lab_t a = {50, 0, 0};
    cie_lab_t b = {60, 10, -5};
    double de = delta_e_1976(&a, &b);
    CHECK(de > 0.0);
    CHECK(de < 50.0);
    OK();
}

static void test_delta_e_2000(void) {
    TEST("ΔE2000 (Sharma dataset #1)");
    cie_lab_t ref = {50, 2.6772, -79.7751};
    cie_lab_t sample = {50, 0, -82.7485};
    double de = delta_e_2000(&ref, &sample);
    CHECK(de > 1.0 && de < 3.0);
    OK();
}

static void test_contrast_ratio(void) {
    TEST("Contrast ratio 1000:1");
    double cr = contrast_ratio_calc(500.0, 0.5);
    CHECK(cr == 1000.0);
    OK();
}

static void test_bt709_matrix(void) {
    TEST("BT.709 RGB → XYZ red");
    pixel_float_t red = {1, 0, 0, 1};
    cie_xyz_t xyz = bt709_rgb_to_xyz(&red);
    CHECK(xyz.X > 0.4 && xyz.Y > 0.2);
    OK();
}

static void test_bt2020_matrix(void) {
    TEST("BT.2020 RGB → XYZ and back");
    pixel_float_t green = {0, 1, 0, 1};
    cie_xyz_t xyz = bt2020_rgb_to_xyz(&green);
    pixel_float_t back = xyz_to_bt2020_rgb(&xyz);
    CHECK(fabs(back.g - 1.0) < 1e-6);
    OK();
}

static void test_color_depth_full(void) {
    TEST("Color depth 8-bit full range");
    color_depth_t cd;
    color_depth_init(&cd, 8, 1);
    CHECK(cd.code_min == 0 && cd.code_max == 255);
    CHECK(float_to_code(1.0, &cd) == 255);
    CHECK(float_to_code(0.0, &cd) == 0);
    OK();
}

static void test_color_depth_limited(void) {
    TEST("Color depth 8-bit limited range");
    color_depth_t cd;
    color_depth_init(&cd, 8, 0);
    CHECK(cd.code_min == 16 && cd.code_max == 235);
    OK();
}

static void test_cmf_available(void) {
    TEST("CIE 1931 CMF data available");
    const cie_cmf_1931_t *cmf = cie_cmf_1931_get();
    CHECK(cmf != NULL);
    CHECK(cmf->y_bar[30] > 0.0); /* ~555nm, peak */
    OK();
}

static void test_mid_gray(void) {
    TEST("18% gray luminance");
    pixel_rgb_t gray = {46, 46, 46, 255, 255}; /* ~0.18 */
    cie_xyz_t xyz = srgb_to_xyz(&gray);
    CHECK(xyz.Y > 0.01 && xyz.Y < 0.05); /* 18% of 1.0 */
    OK();
}

int main(void) {
    printf("=== test_color_science ===\n");
    test_srgb_to_xyz_white();
    test_srgb_to_xyz_black();
    test_xyz_to_srgb_roundtrip();
    test_srgb_transfer();
    test_pq_transfer();
    test_white_point_d65();
    test_color_temperature();
    test_xy_to_cct();
    test_xyz_to_lab();
    test_lab_roundtrip();
    test_xyz_to_luv();
    test_xyz_to_xyy();
    test_xyy_roundtrip();
    test_delta_e_same();
    test_delta_e_different();
    test_delta_e_2000();
    test_contrast_ratio();
    test_bt709_matrix();
    test_bt2020_matrix();
    test_color_depth_full();
    test_color_depth_limited();
    test_cmf_available();
    test_mid_gray();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}


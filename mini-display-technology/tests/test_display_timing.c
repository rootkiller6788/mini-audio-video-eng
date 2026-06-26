/**
 * test_display_timing.c — Test: Display Timing Computation & VESA Modes
 *
 * Tests:
 *   L1: timing_validate, pixel_clock_hz, horizontal_freq
 *   L5: vesa_cvt_compute, vesa_gtf_compute
 *   L6: vesa_dmt_lookup, vesa_dmt_get_mode
 */

#include "display_types.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#define EPS 1e-9

/* Prototypes from src */
extern int timing_validate(const display_timing_t *t);
extern uint64_t timing_pixel_clock_hz(const display_timing_t *t);
extern double timing_horizontal_freq_khz(const display_timing_t *t);
extern double mode_bandwidth_mbps(const display_mode_t *m);
extern int timing_compare(const display_timing_t *a, const display_timing_t *b, double tol);
int vesa_cvt_compute(uint32_t ha, uint32_t va, double hz, aspect_ratio_t a,
                     int rb, int il, timing_result_t *r);
int vesa_gtf_compute(uint32_t ha, uint32_t va, double hz, double m,
                     timing_result_t *r);
int vesa_dmt_lookup(uint32_t w, uint32_t h, double hz, vesa_mode_t *m);
int vesa_dmt_mode_count(void);
int vesa_dmt_get_mode(int idx, vesa_mode_t *m);
extern double compute_ppi(uint32_t w, uint32_t h, double diag_mm);

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)
#define ASSERT_EQ(a, b, msg) do { if ((a) != (b)) { FAIL(msg); return; } } while(0)
#define ASSERT_DELTA(a, b, d) do { if (fabs((a) - (b)) > (d)) { printf("FAIL: got %f expected %f\n", (a), (b)); return; } } while(0)

/* ======================================================== */

static void test_timing_validate(void) {
    TEST("timing_validate valid");
    display_timing_t t = {148500, 1920, 280, 44, 88, 148, 1080, 45, 5, 4, 36, 0, 0, 0, 60.0};
    ASSERT_EQ(timing_validate(&t), 0, "valid 1080p60 timing should pass");
    PASS();
}

static void test_timing_validate_invalid(void) {
    TEST("timing_validate detects errors");
    display_timing_t t = {0, 1920, 280, 44, 88, 148, 1080, 45, 5, 4, 36, 0, 0, 0, 60.0};
    ASSERT_EQ(timing_validate(&t), -1, "zero pixel clock should fail");
    t.pixel_clock_khz = 148500; t.h_active = 0;
    ASSERT_EQ(timing_validate(&t), -2, "zero h_active should fail");
    PASS();
}

static void test_pixel_clock(void) {
    TEST("pixel_clock_hz computation");
    display_timing_t t = {148500, 1920, 280, 44, 88, 148, 1080, 45, 5, 4, 36, 0, 0, 0, 60.0};
    uint64_t hz = timing_pixel_clock_hz(&t);
    assert(hz == 148500000ULL);
    PASS();
}

static void test_horizontal_freq(void) {
    TEST("horizontal_freq_khz");
    display_timing_t t = {148500, 1920, 280, 44, 88, 148, 1080, 45, 5, 4, 36, 0, 0, 0, 60.0};
    double hk = timing_horizontal_freq_khz(&t);
    double expected = 148500.0 / 2200.0;
    ASSERT_DELTA(hk, expected, 0.01);
    PASS();
}

static void test_cvt_1080p60(void) {
    TEST("vesa_cvt_compute 1080p60");
    timing_result_t r;
    int ok = vesa_cvt_compute(1920, 1080, 60.0, ASPECT_16_9, 0, 0, &r);
    assert(ok == 0);
    assert(r.valid == 1);
    assert(r.timing.h_active == 1920);
    assert(r.timing.v_active == 1080);
    assert(r.timing.scan_mode == SCAN_PROGRESSIVE);
    PASS();
}

static void test_cvt_reduced_blanking(void) {
    TEST("vesa_cvt_compute CVT-RB");
    timing_result_t r;
    int ok = vesa_cvt_compute(1920, 1080, 60.0, ASPECT_16_9, 1, 0, &r);
    assert(ok == 0);
    assert(r.valid == 1);
    /* CVT-RB should have smaller blanking */
    assert(r.timing.h_blank < 500);
    PASS();
}

static void test_gtf_1080p60(void) {
    TEST("vesa_gtf_compute 1080p60");
    timing_result_t r;
    int ok = vesa_gtf_compute(1920, 1080, 60.0, 18.0, &r);
    assert(ok == 0);
    assert(r.valid == 1);
    assert(r.timing.h_active == 1920);
    PASS();
}

static void test_dmt_lookup(void) {
    TEST("vesa_dmt_lookup 1920x1080@60");
    vesa_mode_t m;
    int found = vesa_dmt_lookup(1920, 1080, 60.0, &m);
    assert(found == 1);
    assert(m.width == 1920 && m.height == 1080);
    PASS();
}

static void test_dmt_lookup_notfound(void) {
    TEST("vesa_dmt_lookup nonexistent");
    vesa_mode_t m;
    int found = vesa_dmt_lookup(1234, 5678, 99.0, &m);
    /* Should return 0 or closest match */
    printf("(found=%d) ", found);
    PASS();
}

static void test_dmt_mode_count(void) {
    TEST("vesa_dmt_mode_count >= 40");
    int cnt = vesa_dmt_mode_count();
    assert(cnt >= 40);
    PASS();
}

static void test_dmt_get_mode(void) {
    TEST("vesa_dmt_get_mode 0");
    vesa_mode_t m;
    int ok = vesa_dmt_get_mode(0, &m);
    assert(ok == 1 && m.width == 640 && m.height == 480);
    PASS();
}

static void test_ppi(void) {
    TEST("compute_ppi 1920x1080 @ 15-inch");
    double ppi = compute_ppi(1920, 1080, 15.0 * 25.4);
    assert(ppi > 140 && ppi < 150);
    PASS();
}

static void test_ppi_retina(void) {
    TEST("compute_ppi iPhone 15 (2556x1179 @ 6.1 inch)");
    double ppi = compute_ppi(2556, 1179, 6.1 * 25.4);
    assert(ppi > 450 && ppi < 470);
    PASS();
}

static void test_mode_to_string(void) {
    TEST("mode_to_string output");
    display_mode_t m;
    memset(&m, 0, sizeof(m));
    m.resolution.width = 3840; m.resolution.height = 2160;
    m.timing.refresh_rate_hz = 60.0;
    m.bits_per_color = 10;
    char buf[128];
    int n = mode_to_string(&m, buf, sizeof(buf));
    assert(n > 0);
    printf("'%s' ", buf);
    PASS();
}

int main(void) {
    printf("=== test_display_timing ===\n");
    test_timing_validate();
    test_timing_validate_invalid();
    test_pixel_clock();
    test_horizontal_freq();
    test_cvt_1080p60();
    test_cvt_reduced_blanking();
    test_gtf_1080p60();
    test_dmt_lookup();
    test_dmt_lookup_notfound();
    test_dmt_mode_count();
    test_dmt_get_mode();
    test_ppi();
    test_ppi_retina();
    test_mode_to_string();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}


#include "hdr_core.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabs((a) - (b)) > (eps)) { FAIL("value mismatch"); printf("    expected %.12f, got %.12f\n", (double)(b), (double)(a)); return; } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { FAIL("assertion failed: " #cond); return; } \
} while(0)

static void test_pq_params_init(void)
{
    TEST("PQ params initialization");
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    ASSERT_NEAR(pq.m1, 2610.0 / 16384.0, 1e-10);
    ASSERT_NEAR(pq.m1, 2610.0 / 16384.0, 1e-10);
    /* m2 = 2523/4096 * 128 per SMPTE ST 2084 */
    ASSERT_NEAR(pq.m2, 2523.0 / 4096.0 * 128.0, 1e-10);
    ASSERT_NEAR(pq.c1, 3424.0 / 4096.0, 1e-10);
    /* c2 = 2413/4096 * 32, c3 = 2392/4096 * 32 */
    ASSERT_NEAR(pq.c2, 2413.0 / 4096.0 * 32.0, 1e-10);
    ASSERT_NEAR(pq.c3, 2392.0 / 4096.0 * 32.0, 1e-10);
    PASS();
}

static void test_pq_eotf_bounds(void)
{
    TEST("PQ EOTF boundary conditions");
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    /* Signal 0 should give luminance 0 */
    double L0 = hdr_pq_eotf(0.0, &pq);
    ASSERT_NEAR(L0, 0.0, 1e-10);
    /* Signal 1 should give ~10000 cd/m2 */
    double L1 = hdr_pq_eotf(1.0, &pq);
    ASSERT_NEAR(L1, 10000.0, 0.1);
    PASS();
}

static void test_pq_eotf_oetf_roundtrip(void)
{
    TEST("PQ EOTF/OETF roundtrip (0.5 signal)");
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    double signal = 0.5;
    double lum = hdr_pq_eotf(signal, &pq);
    double recovered = hdr_pq_oetf(lum, &pq);
    ASSERT_NEAR(signal, recovered, 1e-6);
    PASS();
}

static void test_pq_oetf_bounds(void)
{
    TEST("PQ OETF boundary conditions");
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    double s0 = hdr_pq_oetf(0.0, &pq);
    ASSERT_NEAR(s0, 0.0, 1e-10);
    double s1 = hdr_pq_oetf(10000.0, &pq);
    ASSERT_NEAR(s1, 1.0, 1e-6);
    PASS();
}

static void test_hlg_oetf_bounds(void)
{
    TEST("HLG OETF boundary conditions");
    hdr_hlg_params_t hlg;
    hdr_hlg_params_init(&hlg);
    ASSERT_NEAR(hdr_hlg_oetf(0.0, &hlg), 0.0, 1e-10);
    ASSERT_NEAR(hdr_hlg_oetf(1.0, &hlg), 1.0, 1e-6);
    /* At 1/12, sqrt(3*1/12) = sqrt(1/4) = 0.5 */
    double mid = hdr_hlg_oetf(1.0 / 12.0, &hlg);
    ASSERT_NEAR(mid, 0.5, 1e-6);
    PASS();
}

static void test_bt1886_eotf(void)
{
    TEST("BT.1886 EOTF");
    hdr_bt1886_params_t bt;
    hdr_bt1886_params_init(&bt, 100.0, 2.4);
    double L0 = hdr_bt1886_eotf(0.0, &bt);
    ASSERT_NEAR(L0, 0.0, 1e-10);
    double L1 = hdr_bt1886_eotf(1.0, &bt);
    ASSERT_NEAR(L1, 100.0, 0.1);
    PASS();
}

static void test_transfer_evaluator(void)
{
    TEST("Transfer evaluator routing");
    hdr_transfer_evaluator_t eval;
    hdr_transfer_eval_init(&eval, HDR_TF_PQ_ST2084, 1);
    /* PQ EOTF: signal 0.5 -> some luminance */
    double L = hdr_transfer_evaluate(0.5, &eval);
    ASSERT_TRUE(L > 0.0 && L < 10000.0);
    /* Test linear */
    hdr_transfer_eval_init(&eval, HDR_TF_LINEAR, 1);
    L = hdr_transfer_evaluate(0.75, &eval);
    ASSERT_NEAR(L, 0.75, 1e-10);
    PASS();
}

static void test_histogram_create_add(void)
{
    TEST("Luminance histogram creation and add");
    hdr_luminance_histogram_t *hist = hdr_histogram_create(100, -2.0, 3.0);
    ASSERT_TRUE(hist != NULL);
    ASSERT_TRUE(hist->num_bins == 100);
    ASSERT_TRUE(hist->total_samples == 0);
    int rc = hdr_histogram_add(hist, 100.0);
    ASSERT_TRUE(rc == 0);
    ASSERT_TRUE(hist->total_samples == 1);
    hdr_histogram_destroy(hist);
    PASS();
}

static void test_histogram_percentiles(void)
{
    TEST("Histogram percentile computation");
    hdr_luminance_histogram_t *hist = hdr_histogram_create(100, -2.0, 3.0);
    ASSERT_TRUE(hist != NULL);
    for (int i = 0; i < 1000; i++) {
        hdr_histogram_add(hist, 100.0);
    }
    hdr_histogram_compute_percentiles(hist);
    ASSERT_NEAR(log10(hist->percentile_50), log10(100.0), 0.1);
    hdr_histogram_destroy(hist);
    PASS();
}

static void test_lut_build_lookup(void)
{
    TEST("Transfer LUT build and lookup");
    hdr_transfer_lut_t lut;
    int rc = hdr_lut_build_forward(&lut, HDR_TF_LINEAR, 1024, 0.0, 1.0);
    ASSERT_TRUE(rc == 0);
    double val = hdr_lut_lookup_forward(&lut, 0.5);
    ASSERT_NEAR(val, 0.5, 0.01);
    hdr_lut_destroy(&lut);
    PASS();
}

static void test_metadata_init(void)
{
    TEST("HDR metadata initialization");
    hdr_metadata_t meta;
    hdr_metadata_init(&meta);
    ASSERT_NEAR(meta.mastering_max_luminance, 1000.0, 1e-10);
    ASSERT_TRUE(meta.is_hlg_based == 0);
    PASS();
}

static void test_primaries_get(void)
{
    TEST("Primary set retrieval");
    const hdr_primaries_set_t *p = hdr_primaries_get(HDR_PRIMARIES_BT2020);
    ASSERT_TRUE(p != NULL);
    ASSERT_NEAR(p->red.x, 0.708, 1e-3);
    ASSERT_NEAR(p->green.y, 0.797, 1e-3);
    ASSERT_NEAR(p->blue.x, 0.131, 1e-3);
    PASS();
}

static void test_chromaticity_to_xyz(void)
{
    TEST("Chromaticity to XYZ conversion");
    hdr_chromaticity_t chroma = {0.3127, 0.3290};
    double X, Z;
    hdr_chromaticity_to_xyz(chroma, 100.0, &X, &Z);
    ASSERT_NEAR(Z, 108.88, 0.2);
    PASS();
}

static void test_weber_jnd(void)
{
    TEST("Weber-Fechner JND");
    hdr_weber_model_t model;
    hdr_weber_init(&model, 100.0);
    double jnd = hdr_weber_jnd(&model, 100.0);
    ASSERT_TRUE(jnd > 0.0);
    ASSERT_NEAR(jnd, 1.0, 0.5);
    PASS();
}

static void test_barten_min_bit_depth(void)
{
    TEST("Barten CSF minimum bit depth");
    int bits = hdr_barten_min_bit_depth(1000.0, 0.005);
    ASSERT_TRUE(bits >= 8);
    ASSERT_TRUE(bits <= 16);
    int bits_sdr = hdr_barten_min_bit_depth(100.0, 0.1);
    ASSERT_TRUE(bits_sdr >= 8 && bits_sdr <= 12);
    PASS();
}

static void test_null_pointer_safety(void)
{
    TEST("Null pointer safety");
    hdr_pq_params_init(NULL);
    hdr_hlg_params_init(NULL);
    hdr_bt1886_params_init(NULL, 0.0, 0.0);
    double r = hdr_pq_eotf(0.5, NULL);
    ASSERT_NEAR(r, 0.0, 1e-10);
    r = hdr_hlg_oetf(0.5, NULL);
    ASSERT_NEAR(r, 0.0, 1e-10);
    r = hdr_bt1886_eotf(0.5, NULL);
    ASSERT_NEAR(r, 0.0, 1e-10);
    hdr_metadata_init(NULL);
    hdr_display_init(NULL, 0.0, 0.0, 0, 0);
    hdr_weber_init(NULL, 0.0);
    PASS();
}

int main(void)
{
    printf("=== test_hdr_core ===\n");
    test_pq_params_init();
    test_pq_eotf_bounds();
    test_pq_eotf_oetf_roundtrip();
    test_pq_oetf_bounds();
    test_hlg_oetf_bounds();
    test_bt1886_eotf();
    test_transfer_evaluator();
    test_histogram_create_add();
    test_histogram_percentiles();
    test_lut_build_lookup();
    test_metadata_init();
    test_primaries_get();
    test_chromaticity_to_xyz();
    test_weber_jnd();
    test_barten_min_bit_depth();
    test_null_pointer_safety();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

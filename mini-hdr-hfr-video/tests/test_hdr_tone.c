#include "hdr_tone_mapping.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static int passed = 0, run = 0;
#define T(s) do { run++; printf("  %s ... ", s); } while(0)
#define P() do { printf("PASS\n"); passed++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); } while(0)
#define NEAR(a,b,e) do { if(fabs((a)-(b))>(e)){F("near");printf("  exp %.6f got %.6f\n",(double)(b),(double)(a));return;} } while(0)
#define OK(c) do { if(!(c)){F(#c);return;} } while(0)

static void test_config_init(void)
{
    T("TMO config init");
    tmo_config_t cfg;
    tmo_config_init(&cfg);
    OK(cfg.method == TMO_REINHARD_GLOBAL);
    OK(cfg.key_value == 0.18);
    P();
}

static void test_image_alloc_free(void)
{
    T("Image alloc/free");
    hdr_image_buffer_t *img = hdr_image_alloc(64, 64, 1000.0, HDR_PRIMARIES_BT2020);
    OK(img != NULL);
    OK(img->width == 64 && img->height == 64);
    hdr_image_free(img);
    P();
}

static void test_image_fill(void)
{
    T("Image fill");
    hdr_image_buffer_t *img = hdr_image_alloc(8, 8, 1000.0, HDR_PRIMARIES_BT2020);
    OK(img != NULL);
    hdr_image_fill(img, 1.0, 0.5, 0.0);
    OK(img->pixels[0].r == 1.0);
    OK(img->pixels[0].g == 0.5);
    OK(img->pixels[0].b == 0.0);
    hdr_image_free(img);
    P();
}

static void test_test_pattern(void)
{
    T("Test pattern creation");
    hdr_image_buffer_t *img = hdr_image_create_test_pattern(32, 32, 1000.0);
    OK(img != NULL);
    OK(img->width == 32 && img->height == 32);
    OK(img->peak_nits == 1000.0);
    hdr_image_free(img);
    P();
}

static void test_scene_analysis(void)
{
    T("Scene analysis");
    hdr_image_buffer_t *img = hdr_image_alloc(16, 16, 1000.0, HDR_PRIMARIES_BT2020);
    OK(img != NULL);
    hdr_image_fill(img, 100.0, 100.0, 100.0);
    tmo_scene_analysis_t analysis;
    tmo_analyze_scene(img, &analysis);
    OK(analysis.max_luminance > 0.0);
    OK(analysis.dynamic_range_stops >= 0.0);
    hdr_image_free(img);
    P();
}

static void test_reinhard_global(void)
{
    T("Reinhard global tone mapping");
    double Ld = tmo_reinhard_global(1.0, 4.0);
    OK(Ld > 0.0 && Ld < 1.0);
    double Ld0 = tmo_reinhard_global(0.0, 1.0);
    OK(Ld0 == 0.0);
    P();
}

static void test_drago_log(void)
{
    T("Drago logarithmic tone mapping");
    double Ld = tmo_drago_log(500.0, 1000.0, 0.85);
    OK(Ld > 0.0 && Ld <= 1.0);
    P();
}

static void test_tmo_apply(void)
{
    T("TMO apply to image");
    hdr_image_buffer_t *img = hdr_image_alloc(16, 16, 2000.0, HDR_PRIMARIES_BT2020);
    OK(img != NULL);
    for (int i = 0; i < 256; i++) {
        double v = (double)i / 256.0 * 2000.0;
        img->pixels[i].r = v; img->pixels[i].g = v; img->pixels[i].b = v;
    }
    tmo_config_t cfg;
    tmo_config_init(&cfg);
    int ow, oh;
    hdr_rgb_pixel_t *out = tmo_apply(img, &cfg, &ow, &oh);
    OK(out != NULL);
    OK(ow == 16 && oh == 16);
    OK(out[0].r >= 0.0 && out[0].r <= 1.0);
    free(out);
    hdr_image_free(img);
    P();
}

static void test_bt2446_method_a(void)
{
    T("BT.2446 Method A");
    double sdr = tmo_bt2446_method_a(0.5, 100.0);
    OK(sdr > 0.0 && sdr <= 1.0);
    sdr = tmo_bt2446_method_a(0.0, 100.0);
    OK(sdr == 0.0);
    P();
}

static void test_bt2446_method_b(void)
{
    T("BT.2446 Method B");
    double sdr = tmo_bt2446_method_b(0.5, 0.0);
    OK(sdr > 0.0 && sdr <= 1.0);
    P();
}

static void test_bilateral_filter(void)
{
    T("Bilateral filter");
    double src[100], dst[100];
    for (int i = 0; i < 100; i++) src[i] = (double)(i % 10);
    tmo_bilateral_params_t params;
    params.spatial_sigma = 2.0;
    params.range_sigma = 1.0;
    params.kernel_size = 5;
    params.use_log_domain = 0;
    params.sampling_ratio = 1.0;
    tmo_bilateral_filter(src, dst, 10, 10, &params);
    OK(dst[0] >= 0.0);
    P();
}

static void test_base_detail_decompose(void)
{
    T("Base/detail decomposition");
    double src[100], base[100], detail[100];
    for (int i = 0; i < 100; i++) src[i] = (double)((i * 7) % 23) + 1.0;
    tmo_bilateral_params_t params;
    params.spatial_sigma = 1.0; params.range_sigma = 2.0;
    params.kernel_size = 3; params.use_log_domain = 0; params.sampling_ratio = 1.0;
    tmo_base_detail_decompose(src, base, detail, 10, 10, &params);
    OK(base[0] > 0.0);
    P();
}

static void test_null_safety(void)
{
    T("Null pointer safety");
    tmo_config_init(NULL);
    tmo_analyze_scene(NULL, NULL);
    OK(tmo_apply(NULL, NULL, NULL, NULL) == NULL);
    hdr_image_free(NULL);
    hdr_image_fill(NULL, 0, 0, 0);
    OK(hdr_image_create_test_pattern(0, 0, 0) == NULL);
    tmo_bilateral_filter(NULL, NULL, 0, 0, NULL);
    P();
}

int main(void)
{
    printf("=== test_hdr_tone ===\n");
    test_config_init();
    test_image_alloc_free();
    test_image_fill();
    test_test_pattern();
    test_scene_analysis();
    test_reinhard_global();
    test_drago_log();
    test_tmo_apply();
    test_bt2446_method_a();
    test_bt2446_method_b();
    test_bilateral_filter();
    test_base_detail_decompose();
    test_null_safety();
    printf("\n%d/%d tests passed\n", passed, run);
    return (passed == run) ? 0 : 1;
}

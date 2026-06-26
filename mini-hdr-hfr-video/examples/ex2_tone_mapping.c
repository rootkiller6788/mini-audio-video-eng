#include "hdr_tone_mapping.h"
#include <stdio.h>
#include <math.h>

int main(void)
{
    printf("=== HDR Tone Mapping Example ===\n\n");

    /* Create a synthetic HDR test image */
    int w = 64, h = 48;
    hdr_image_buffer_t *hdr_img = hdr_image_create_test_pattern(w, h, 4000.0);
    if (!hdr_img) { printf("Failed to create test image\n"); return 1; }

    printf("Input HDR image: %dx%d, peak=%.0f nits\n", w, h, hdr_img->peak_nits);

    /* Analyze the scene */
    tmo_scene_analysis_t analysis;
    tmo_analyze_scene(hdr_img, &analysis);

    printf("\n--- Scene Analysis ---\n");
    printf("  Log-average luminance: %.3f (%.1f cd/m2)\n",
           analysis.log_average_luminance, pow(10.0, analysis.log_average_luminance));
    printf("  Geometric mean: %.2f cd/m2\n", analysis.geometric_mean);
    printf("  Min luminance: %.4f cd/m2\n", analysis.min_luminance);
    printf("  Max luminance: %.1f cd/m2\n", analysis.max_luminance);
    printf("  Dynamic range: %.1f stops\n", analysis.dynamic_range_stops);
    printf("  50th percentile: %.2f cd/m2\n", analysis.percentile_50);
    printf("  99th percentile: %.2f cd/m2\n", analysis.percentile_99);

    /* Apply Reinhard global tone mapping */
    tmo_config_t cfg;
    tmo_config_init(&cfg);
    cfg.method = TMO_REINHARD_GLOBAL;
    cfg.target_peak = 100.0;

    int ow, oh;
    hdr_rgb_pixel_t *sdr_pixels = tmo_apply(hdr_img, &cfg, &ow, &oh);

    if (sdr_pixels) {
        printf("\n--- Reinhard Global TMO Output ---\n");
        printf("  Output size: %dx%d\n", ow, oh);
        double min_val = 1e30, max_val = -1e30, avg_val = 0.0;
        for (int i = 0; i < ow * oh; i++) {
            double v = (sdr_pixels[i].r + sdr_pixels[i].g + sdr_pixels[i].b) / 3.0;
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
            avg_val += v;
        }
        avg_val /= (double)(ow * oh);
        printf("  Output range: [%.4f, %.4f]\n", min_val, max_val);
        printf("  Average value: %.4f\n", avg_val);
        free(sdr_pixels);
    }

    /* Apply Drago logarithmic tone mapping */
    cfg.method = TMO_DRAGO_LOGARITHMIC;
    sdr_pixels = tmo_apply(hdr_img, &cfg, &ow, &oh);

    if (sdr_pixels) {
        printf("\n--- Drago Logarithmic TMO Output ---\n");
        double min_val = 1e30, max_val = -1e30;
        for (int i = 0; i < ow * oh; i++) {
            double v = (sdr_pixels[i].r + sdr_pixels[i].g + sdr_pixels[i].b) / 3.0;
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
        }
        printf("  Output range: [%.4f, %.4f]\n", min_val, max_val);
        free(sdr_pixels);
    }

    /* Demonstrate BT.2446 HDR-to-SDR down-conversion */
    printf("\n--- ITU-R BT.2446 HDR-to-SDR ---\n");
    double pq_test_vals[] = {0.1, 0.3, 0.5, 0.7, 0.9};
    printf("  PQ Signal | Method A (SDR) | Method B (SDR)\n");
    printf("  ----------+----------------+---------------\n");
    for (int i = 0; i < 5; i++) {
        double sdr_a = tmo_bt2446_method_a(pq_test_vals[i], 100.0);
        double sdr_b = tmo_bt2446_method_b(pq_test_vals[i], 0.0);
        printf("  %8.2f  | %12.4f  | %12.4f\n", pq_test_vals[i], sdr_a, sdr_b);
    }

    /* Demonstrate bilateral filter */
    printf("\n--- Bilateral Filter (edge-preserving smoothing) ---\n");
    double src[64], dst[64];
    for (int i = 0; i < 64; i++) src[i] = (i < 32) ? 0.1 : 0.9;
    src[31] = 0.5; src[32] = 0.5;
    tmo_bilateral_params_t bf_params;
    bf_params.spatial_sigma = 2.0;
    bf_params.range_sigma = 0.3;
    bf_params.kernel_size = 5;
    bf_params.use_log_domain = 0;
    bf_params.sampling_ratio = 1.0;
    tmo_bilateral_filter(src, dst, 8, 8, &bf_params);
    printf("  Edge-preserving result at edge boundary:\n");
    printf("  src[30]=%.2f src[31]=%.2f | src[32]=%.2f src[33]=%.2f\n",
           src[30], src[31], src[32], src[33]);
    printf("  dst[30]=%.2f dst[31]=%.2f | dst[32]=%.2f dst[33]=%.2f\n",
           dst[30], dst[31], dst[32], dst[33]);

    hdr_image_free(hdr_img);
    printf("\n=== Example complete ===\n");
    return 0;
}

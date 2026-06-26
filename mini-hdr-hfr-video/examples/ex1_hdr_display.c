#include "hdr_core.h"
#include <stdio.h>
#include <math.h>

int main(void)
{
    printf("=== HDR Display Pipeline Example ===\n\n");

    /* Initialize transfer functions */
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    hdr_hlg_params_t hlg;
    hdr_hlg_params_init(&hlg);

    /* Example 1: PQ signal values to display luminance */
    printf("--- PQ EOTF: Signal to Display Luminance ---\n");
    printf("  Signal | Luminance (cd/m2) | Perceptual Note\n");
    printf("  -------+--------------------+----------------\n");
    double signals[] = {0.0, 0.1, 0.2, 0.3, 0.5, 0.7, 0.9, 1.0};
    for (int i = 0; i < 8; i++) {
        double lum = hdr_pq_eotf(signals[i], &pq);
        const char *note = "dark";
        if (lum > 1.0) note = "dim";
        if (lum > 100.0) note = "normal";
        if (lum > 1000.0) note = "bright";
        if (lum > 5000.0) note = "very bright";
        printf("  %5.2f  | %16.2f  | %s\n", signals[i], lum, note);
    }

    /* Example 2: Scene luminance to HLG signal */
    printf("\n--- HLG OETF: Scene Light to HLG Signal ---\n");
    printf("  Scene Light | HLG Signal\n");
    printf("  ------------+------------\n");
    double scene_vals[] = {0.001, 0.01, 0.05, 0.1, 0.2, 0.5, 0.8, 1.0};
    for (int i = 0; i < 8; i++) {
        double sig = hdr_hlg_oetf(scene_vals[i], &hlg);
        printf("  %9.3f   | %9.5f\n", scene_vals[i], sig);
    }

    /* Example 3: Build a LUT for fast PQ EOTF evaluation */
    printf("\n--- LUT-Based PQ Evaluation (1024 entries) ---\n");
    hdr_transfer_lut_t lut;
    if (hdr_lut_build_forward(&lut, HDR_TF_PQ_ST2084, 1024, 0.0, 1.0) == 0) {
        double test_signals[] = {0.0, 0.25, 0.5, 0.75, 1.0};
        for (int i = 0; i < 5; i++) {
            double direct = hdr_pq_eotf(test_signals[i], &pq);
            double lut_val = hdr_lut_lookup_forward(&lut, test_signals[i]);
            printf("  Signal=%.2f: direct=%.1f, LUT=%.1f, error=%.2f\n",
                   test_signals[i], direct, lut_val, fabs(direct - lut_val));
        }
        hdr_lut_destroy(&lut);
    }

    /* Example 4: Display characterization */
    printf("\n--- Display Models ---\n");
    hdr_display_model_t sdr_display, hdr_display;
    hdr_display_init(&sdr_display, 100.0, 0.1, HDR_PRIMARIES_BT709, 0);
    hdr_display_init(&hdr_display, 1000.0, 0.005, HDR_PRIMARIES_BT2020, 1);

    printf("  SDR Display : peak=%.0f nits, bit_depth=%d, HDR=%s\n",
           sdr_display.peak_luminance, sdr_display.max_bit_depth,
           sdr_display.is_hdr_capable ? "yes" : "no");
    printf("  HDR Display : peak=%.0f nits, bit_depth=%d, HDR=%s, PQ=%s, HLG=%s\n",
           hdr_display.peak_luminance, hdr_display.max_bit_depth,
           hdr_display.is_hdr_capable ? "yes" : "no",
           hdr_display.supports_pq ? "yes" : "no",
           hdr_display.supports_hlg ? "yes" : "no");

    /* Example 5: Metadata */
    printf("\n--- HDR Metadata ---\n");
    hdr_metadata_t meta;
    hdr_metadata_init(&meta);
    printf("  Mastering display max luminance: %.0f cd/m2\n", meta.mastering_max_luminance);
    printf("  Mastering display min luminance: %.4f cd/m2\n", meta.mastering_min_luminance);
    printf("  Color primaries: %s\n", meta.mastering_primaries.name);

    /* Example 6: Histogram-based analysis */
    printf("\n--- Luminance Histogram Analysis ---\n");
    hdr_luminance_histogram_t *hist = hdr_histogram_create(256, -4.0, 4.0);
    if (hist) {
        for (int i = 0; i < 10000; i++) {
            double lum = pow(10.0, -3.0 + 6.0 * (double)i / 10000.0);
            hdr_histogram_add(hist, lum);
        }
        hdr_histogram_compute_percentiles(hist);
        printf("  Median luminance: %.2f cd/m2\n", hist->percentile_50);
        printf("  90th percentile: %.2f cd/m2\n", hist->percentile_90);
        printf("  99th percentile: %.2f cd/m2\n", hist->percentile_99);
        double maxcll = hdr_metadata_compute_maxcll(hist, 0.999);
        printf("  MaxCLL (99.9%%): %.2f cd/m2\n", maxcll);
        printf("  MaxFALL: %.2f cd/m2\n", hdr_metadata_compute_maxfall(hist));
        hdr_histogram_destroy(hist);
    }

    /* Example 7: Barten CSF and bit depth */
    printf("\n--- Barten CSF: Minimum Bit Depth ---\n");
    double test_peaks[] = {100.0, 500.0, 1000.0, 4000.0, 10000.0};
    for (int i = 0; i < 5; i++) {
        int bits = hdr_barten_min_bit_depth(test_peaks[i], 0.005);
        printf("  Peak %.0f cd/m2 => %d bits\n", test_peaks[i], bits);
    }

    printf("\n=== Example complete ===\n");
    return 0;
}

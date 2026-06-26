/**
 * example_dithering.c — End-to-End: Image Dithering & Processing Pipeline
 *
 * Demonstrates a complete display image processing pipeline:
 *   1. Generate test image (grayscale ramp + checkerboard)
 *   2. Compute histogram and equalize
 *   3. Apply dithering (Floyd-Steinberg) for 1-bit depth reduction
 *   4. Measure quality via PSNR
 *
 * L6: Canonical Problem — Display image processing: scaling, dithering,
 *     histogram equalization, quality assessment.
 *
 * L7: Application — E-ink display dithering (Kindle, reMarkable),
 *     Billboard LED display processing.
 *
 * Usage: ./example_dithering
 */

#include "display_types.h"
#include "image_process.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* External */
extern int framebuffer_alloc(framebuffer_t *fb, uint32_t w, uint32_t h, pixel_format_t f);
extern void framebuffer_free(framebuffer_t *fb);
extern void framebuffer_clear(framebuffer_t *fb, const pixel_rgb_t *color);
extern int framebuffer_pixel_write(framebuffer_t *fb, uint32_t x, uint32_t y, const pixel_rgb_t *p);
int image_scale(const framebuffer_t *s, framebuffer_t *d, scale_method_t h, scale_method_t v);
int image_dither(const framebuffer_t *s, framebuffer_t *d, dither_method_t m, uint8_t bits);
void histogram_compute(const framebuffer_t *fb, image_histogram_t *hist);
void histogram_cdf_compute(const image_histogram_t *hist, histogram_cdf_t *cdf);
void histogram_equalize(framebuffer_t *fb, const histogram_cdf_t *cdf);
double framebuffer_psnr(const framebuffer_t *ref, const framebuffer_t *test);
uint8_t otsu_threshold(const image_histogram_t *hist);
void framebuffer_binarize(framebuffer_t *fb, uint8_t thresh);
void generate_gray_step_wedge(framebuffer_t *fb, int steps);
void generate_checkerboard(framebuffer_t *fb, int sq);
int framebuffer_rgb_to_gray(const framebuffer_t *rgb, framebuffer_t *gray);
double framebuffer_ssim(const framebuffer_t *ref, const framebuffer_t *test, int ws);
void generate_zone_plate(framebuffer_t *fb, double max_freq);

static void print_histogram_summary(const image_histogram_t *hist) {
    printf("  Pixels: %u, Mean: %.1f, StdDev: %.2f\n",
           hist->total_pixels, hist->mean, hist->std_dev);
    printf("  Range: [%.0f, %.0f]\n", hist->min_val, hist->max_val);
}

int main(void) {
    printf("══════════════════════════════════════════════════\n");
    printf("  Display Dithering & Image Processing Pipeline\n");
    printf("══════════════════════════════════════════════════\n\n");

    const int W = 256, H = 128;

    /* ================================================================
     * Step 1: Generate a synthetic test image
     * ================================================================ */
    printf("### Step 1: Generate Synthetic Test Image ###\n");
    framebuffer_t original;
    if (framebuffer_alloc(&original, W, H, PIXFMT_MONO8) != 0) {
        printf("ERROR: allocation failed\n"); return 1;
    }

    /* Horizontal grayscale ramp (top half) + checkerboard (bottom half) */
    generate_gray_step_wedge(&original, 16);
    printf("  Created %u×%u grayscale test image\n", W, H);

    image_histogram_t hist_orig;
    histogram_compute(&original, &hist_orig);
    print_histogram_summary(&hist_orig);

    /* ================================================================
     * Step 2: Compute histogram and equalize
     * ================================================================ */
    printf("\n### Step 2: Histogram Equalization ###\n");
    histogram_cdf_t cdf;
    histogram_cdf_compute(&hist_orig, &cdf);

    framebuffer_t equalized;
    if (framebuffer_alloc(&equalized, W, H, PIXFMT_MONO8) != 0) {
        printf("ERROR: allocation failed\n"); framebuffer_free(&original); return 1;
    }
    memcpy(equalized.data, original.data, original.total_bytes);
    histogram_equalize(&equalized, &cdf);

    image_histogram_t hist_eq;
    histogram_compute(&equalized, &hist_eq);
    printf("  After equalization: ");
    print_histogram_summary(&hist_eq);
    printf("  Mean should be ~128: %.1f\n", hist_eq.mean);

    /* ================================================================
     * Step 3: Apply dithering (reduce to 1-bit)
     * ================================================================ */
    printf("\n### Step 3: Floyd-Steinberg Dithering (→ 1-bit) ###\n");

    framebuffer_t dithered;
    if (framebuffer_alloc(&dithered, W, H, PIXFMT_MONO8) != 0) {
        printf("ERROR: allocation failed\n"); return 1;
    }
    memcpy(dithered.data, original.data, original.total_bytes);
    image_dither(&original, &dithered, DITHER_FLOYD_STEINBERG, 1);
    printf("  Dithering applied: 8-bit → 1-bit\n");

    /* Count dither pattern: ~50% pixels should be 1 vs 0 */
    uint32_t ones = 0;
    for (uint32_t i = 0; i < dithered.total_bytes; i++)
        if (dithered.data[i] > 127) ones++;
    printf("  Ones ratio: %.1f%%\n", 100.0 * ones / dithered.total_bytes);

    /* Quality assessment */
    double psnr_fs = framebuffer_psnr(&original, &dithered);
    printf("  PSNR (Floyd-Steinberg): %.2f dB\n", psnr_fs);

    /* ================================================================
     * Step 4: Compare different dithering methods
     * ================================================================ */
    printf("\n### Step 4: Dithering Method Comparison ###\n");

    dither_method_t methods[] = {
        DITHER_FLOYD_STEINBERG, DITHER_ATKINSON,
        DITHER_ORDERED_BAYER, DITHER_JARVIS_JUDICE,
        DITHER_STUCKI, DITHER_NOISE
    };
    const char *method_names[] = {
        "Floyd-Steinberg", "Atkinson", "Ordered Bayer",
        "Jarvis-Judice", "Stucki", "Noise"
    };
    int n_methods = 6;

    framebuffer_t dith_test;
    framebuffer_alloc(&dith_test, W, H, PIXFMT_MONO8);

    printf("  %-18s  %8s\n", "Method", "PSNR (dB)");
    printf("  ---------------------------------\n");
    for (int i = 0; i < n_methods; i++) {
        memcpy(dith_test.data, original.data, original.total_bytes);
        image_dither(&original, &dith_test, methods[i], 1);
        double psnr = framebuffer_psnr(&original, &dith_test);
        printf("  %-18s  %8.2f\n", method_names[i], psnr);
    }
    framebuffer_free(&dith_test);

    /* ================================================================
     * Step 5: E-Ink Simulation (Atkinson dither for Kindle)
     * ================================================================ */
    printf("\n### Step 5: E-Ink Simulation (Atkinson Dither) ###\n");
    framebuffer_t eink;
    framebuffer_alloc(&eink, W, H, PIXFMT_MONO8);
    memcpy(eink.data, original.data, original.total_bytes);
    image_dither(&original, &eink, DITHER_ATKINSON, 1);

    /* Atkinson gives more organic, softer dither — better for e-ink */
    uint32_t eink_ones = 0;
    for (uint32_t i = 0; i < eink.total_bytes; i++)
        if (eink.data[i] > 127) eink_ones++;
    printf("  Atkinson dither ones ratio: %.1f%%\n",
           100.0 * eink_ones / eink.total_bytes);
    printf("  (Atkinson: softer, more organic — ideal for e-ink panels)\n");

    double psnr_atkinson = framebuffer_psnr(&original, &eink);
    printf("  PSNR: %.2f dB\n", psnr_atkinson);
    framebuffer_free(&eink);

    /* ================================================================
     * Step 6: Zone Plate — Frequency Response Test
     * ================================================================ */
    printf("\n### Step 6: Zone Plate (Resolution Test Pattern) ###\n");
    framebuffer_t zone;
    framebuffer_alloc(&zone, W, W, PIXFMT_MONO8);
    generate_zone_plate(&zone, 50.0);

    image_histogram_t hist_zone;
    histogram_compute(&zone, &hist_zone);
    print_histogram_summary(&hist_zone);

    /* Otsu threshold for binarization */
    uint8_t thresh = otsu_threshold(&hist_zone);
    printf("  Otsu threshold: %u\n", thresh);
    framebuffer_free(&zone);

    /* ================================================================
     * Step 7: Billboard LED Display Processing
     * ================================================================ */
    printf("\n### Step 7: Billboard LED Display Processing ###\n");
    framebuffer_t billboard;
    framebuffer_alloc(&billboard, 64, 64, PIXFMT_RGB888);

    /* Simulate a low-res billboard image (scaled from small source) */
    framebuffer_t tiny;
    framebuffer_alloc(&tiny, 8, 8, PIXFMT_RGB888);
    pixel_rgb_t colors[4] = {
        {255, 0, 0, 255, 255}, {0, 255, 0, 255, 255},
        {0, 0, 255, 255, 255}, {255, 255, 0, 255, 255}
    };
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            framebuffer_pixel_write(&tiny, x, y, &colors[(x+y)%4]);

    /* Scale up (simulating a billboard display processor) */
    image_scale(&tiny, &billboard, SCALE_NEAREST, SCALE_NEAREST);
    printf("  Scaled 8×8 → 64×64 (nearest-neighbor)\n");
    printf("  LED billboard pixel pitch: 10mm → 640mm × 640mm = ~25\"\n");
    printf("  Viewing distance ≥ 5m for acceptable quality\n");

    framebuffer_free(&tiny);
    framebuffer_free(&billboard);

    /* ================================================================
     * Cleanup and Summary
     * ================================================================ */
    framebuffer_free(&original);
    framebuffer_free(&equalized);
    framebuffer_free(&dithered);

    printf("\n══════════════════════════════════════════════════\n");
    printf("  Dithering pipeline complete.\n");
    printf("  Dithering reduces banding in smooth gradients\n");
    printf("  at the cost of introducing HF noise that the\n");
    printf("  human visual system filters out spatially.\n");
    printf("══════════════════════════════════════════════════\n");
    return 0;
}


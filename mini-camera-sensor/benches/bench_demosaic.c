/**
 * @file bench_demosaic.c
 * @brief Performance benchmark for demosaicing algorithms
 */
#include <stdio.h>
#include <time.h>
#include "../include/camera_sensor.h"
#include "../include/pixel_array.h"
#include "../include/demosaic.h"

int main(void)
{
    const uint32_t W = 512, H = 512;
    printf("=== Demosaicing Benchmark (%ux%u) ===\n", (unsigned)W, (unsigned)H);

    /* Create test frame */
    raw_frame_t *raw = raw_frame_alloc(W, H, CFA_BAYER_RGGB);
    uint32_t i, n = W * H;
    for (i = 0; i < n; i++) {
        raw->data[i] = (pixel_raw_t)(i % 4096);
    }

    rgb_image_planar_t *rgb = rgb_planar_alloc(W, H);
    clock_t start, end;
    double elapsed;

    /* Bilinear */
    start = clock();
    for (i = 0; i < 10; i++) demosaic_bilinear(raw, rgb);
    end = clock();
    elapsed = (double)(end - start) / CLOCKS_PER_SEC / 10.0;
    printf("Bilinear:           %8.3f ms\n", elapsed * 1000.0);

    /* MHC */
    start = clock();
    for (i = 0; i < 10; i++) demosaic_mhc(raw, rgb);
    end = clock();
    elapsed = (double)(end - start) / CLOCKS_PER_SEC / 10.0;
    printf("Malvar-He-Cutler:   %8.3f ms\n", elapsed * 1000.0);

    /* Gradient-corrected */
    start = clock();
    for (i = 0; i < 10; i++) demosaic_gradient_corrected(raw, rgb);
    end = clock();
    elapsed = (double)(end - start) / CLOCKS_PER_SEC / 10.0;
    printf("Gradient-corrected: %8.3f ms\n", elapsed * 1000.0);

    /* AHD */
    start = clock();
    for (i = 0; i < 10; i++) demosaic_ahd(raw, rgb);
    end = clock();
    elapsed = (double)(end - start) / CLOCKS_PER_SEC / 10.0;
    printf("AHD (Hirakawa-Parks):%7.3f ms\n", elapsed * 1000.0);

    raw_frame_free(raw);
    rgb_planar_free(rgb);

    printf("=== Benchmark Complete ===\n");
    return 0;
}

/**
 * @file demosaic.h
 * @brief Bayer demosaicing — reconstruct full-color RGB from CFA data
 *
 * L1: Bayer pattern, demosaicing, zippering, false color, maze artifacts
 * L2: spatial-spectral sampling, green priority, color difference interpolation
 * L3: 2D interpolation, Laplacian, gradient operators, edge-directed weighting
 * L5: bilinear, Malvar-He-Cutler (2004), gradient-corrected, AHD (2005)
 * L6: artifact-free demosaicing, PSNR optimization
 * L8: state-of-art quality benchmarks
 *
 * Reference: Malvar et al. IEEE ICASSP 2004; Hirakawa & Parks IEEE T-IP 2005;
 *            Gunturk et al. IEEE SPM 2005
 */
#ifndef DEMOSAIC_H
#define DEMOSAIC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "camera_sensor.h"
#include "pixel_array.h"

/*===========================================================================
 * L1+L2: Demosaicing types
 *===========================================================================*/

typedef enum {
    DEMOSAIC_BILINEAR = 0,
    DEMOSAIC_MHC = 1,           /* Malvar-He-Cutler 2004 */
    DEMOSAIC_GBC = 2,           /* Gradient-Based Correction */
    DEMOSAIC_EDGE_SENSING = 3,
    DEMOSAIC_AHD = 4,           /* Adaptive Homogeneity-Directed */
    DEMOSAIC_HSL = 5
} demosaic_algorithm_t;

typedef struct {
    double psnr_r, psnr_g, psnr_b; /* Per-channel PSNR [dB] */
    double cpsnr;                  /* Color PSNR */
    double ssim;                   /* Structural similarity */
    double delta_e_avg;            /* Avg CIEDE2000 error */
} demosaic_quality_t;

typedef struct {
    uint16_t *r, *g, *b;       /* Planar RGB channels, row-major */
    uint32_t width, height;
} rgb_image_planar_t;

typedef struct {
    double weights[25];
    int32_t size;
} demosaic_kernel_t;

/*===========================================================================
 * L5: Demosaicing algorithms
 *===========================================================================*/

rgb_image_planar_t *rgb_planar_alloc(uint32_t w, uint32_t h);
void rgb_planar_free(rgb_image_planar_t *img);

/** Bilinear: simple, fast, ~30-35 dB PSNR. O(w*h). */
int demosaic_bilinear(const raw_frame_t *raw, rgb_image_planar_t *rgb);

/** Malvar-He-Cutler: 5x5 linear with Laplacian correction. ~37-41 dB. O(w*h). */
int demosaic_mhc(const raw_frame_t *raw, rgb_image_planar_t *rgb);

/** Gradient-corrected: interpolate along edges. ~35-39 dB. O(w*h). */
int demosaic_gradient_corrected(const raw_frame_t *raw,
                                 rgb_image_planar_t *rgb);

/** Adaptive Homogeneity-Directed: Hirakawa-Parks 2005. ~38-43 dB. O(w*h). */
int demosaic_ahd(const raw_frame_t *raw, rgb_image_planar_t *rgb);

/** Generic demosaic dispatcher. */
int demosaic_execute(demosaic_algorithm_t algo, const raw_frame_t *raw,
                      rgb_image_planar_t *rgb);

/*===========================================================================
 * L5: Helper functions
 *===========================================================================*/

/** Horizontal and vertical gradients at (x,y) with 5x5 neighborhood. O(1). */
void demosaic_gradients(const raw_frame_t *raw, uint32_t x, uint32_t y,
                         double *gh, double *gv);

/** Edge-directed green interpolation at R/B pixel. O(1). */
uint16_t demosaic_interpolate_green_edge(const raw_frame_t *raw,
                                          uint32_t x, uint32_t y);

/** Color-difference R/B interpolation at G pixel. O(1). */
uint16_t demosaic_interpolate_color_at_green(const raw_frame_t *raw,
                                              uint16_t *g_plane,
                                              uint32_t x, uint32_t y,
                                              uint8_t is_red);

/** R at B (or B at R) via 4-diagonal neighbor interpolation. O(1). */
uint16_t demosaic_interpolate_color_at_opposite(const raw_frame_t *raw,
                                                  uint16_t *g_plane,
                                                  uint32_t x, uint32_t y,
                                                  uint8_t is_red_at_blue);

/*===========================================================================
 * L6: Quality assessment
 *===========================================================================*/

void demosaic_quality_assess(const rgb_image_planar_t *est,
                              const rgb_image_planar_t *ref,
                              demosaic_quality_t *metrics);

double demosaic_psnr_channel(const uint16_t *a, const uint16_t *b,
                              uint32_t n, uint16_t max_val);

#endif /* DEMOSAIC_H */

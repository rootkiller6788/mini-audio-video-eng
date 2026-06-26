/**
 * @file pixel_array.h
 * @brief Pixel array data structures, readout patterns, and binning
 *
 * L1: pixel_raw_t, pixel_defect_type_t, bayer_color_t, raw_frame_t
 * L2: pixel addressing, binning, subsampling, defect correction
 * L3: 2D array striding, coordinate transforms
 * L5: defect detection, column FPN correction, ROI extraction
 *
 * Reference: Holst & Lomheim (2011) Ch.5-7, Nakamura (2005) Ch.3
 */
#ifndef PIXEL_ARRAY_H
#define PIXEL_ARRAY_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "camera_sensor.h"

/*===========================================================================
 * L1: Pixel type definitions
 *===========================================================================*/

typedef uint16_t pixel_raw_t;
typedef int32_t  pixel_signed_t;
typedef uint32_t pixel_accum_t;

typedef enum {
    BAYER_COLOR_R = 0,
    BAYER_COLOR_GR = 1,
    BAYER_COLOR_GB = 2,
    BAYER_COLOR_B = 3
} bayer_color_t;

typedef enum {
    DEFECT_NONE = 0,
    DEFECT_DEAD = 1,
    DEFECT_HOT = 2,
    DEFECT_STUCK_HIGH = 3,
    DEFECT_STUCK_LOW = 4,
    DEFECT_BLINKING = 5,
    DEFECT_COLUMN = 6,
    DEFECT_CLUSTER = 7
} pixel_defect_type_t;

typedef struct {
    uint32_t x, y;
    pixel_defect_type_t type;
    double severity;
} pixel_defect_t;

typedef struct {
    pixel_defect_t *defects;
    uint32_t count;
    uint32_t capacity;
} defect_map_t;

typedef struct {
    pixel_raw_t *data;
    uint32_t width, height, stride;
    cfa_pattern_t cfa;
    uint32_t black_level;
    uint32_t max_value;
} raw_frame_t;

typedef struct {
    double mean, variance, stddev;
    uint32_t min, max;
    uint32_t saturated_count, black_count;
    double histogram[256];
} pixel_statistics_t;

/*===========================================================================
 * L2: Bayer coordinate operations
 *===========================================================================*/

/** Get Bayer color at (x,y) given CFA pattern. O(1). */
bayer_color_t bayer_color_at(uint32_t x, uint32_t y, cfa_pattern_t cfa);

/** Find nearest same-color neighbor offsets. O(1). */
void bayer_nearest_color(bayer_color_t target, uint32_t x, uint32_t y,
                          cfa_pattern_t cfa, int32_t *dx, int32_t *dy);

/** Count pixels of each Bayer color in a region. O(w*h). */
void bayer_color_counts(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                         cfa_pattern_t cfa, uint32_t *r, uint32_t *gr,
                         uint32_t *gb, uint32_t *b);

/*===========================================================================
 * L2: Raw frame operations
 *===========================================================================*/

raw_frame_t *raw_frame_alloc(uint32_t w, uint32_t h, cfa_pattern_t cfa);
void raw_frame_free(raw_frame_t *f);
void raw_frame_fill(raw_frame_t *f, pixel_raw_t v);
void raw_frame_copy(const raw_frame_t *src, raw_frame_t *dst);
void raw_frame_subtract(raw_frame_t *dst, const raw_frame_t *src);
void raw_frame_apply_gain(raw_frame_t *f, const double *gain);
void raw_frame_statistics(const raw_frame_t *f, pixel_statistics_t *s);
void pixel_statistics_print(const pixel_statistics_t *s);

/*===========================================================================
 * L5: Binning, subsampling, ROI
 *===========================================================================*/

int raw_frame_bin_2x2(const raw_frame_t *src, raw_frame_t *dst);
int raw_frame_bin_nxn(const raw_frame_t *src, raw_frame_t *dst, uint32_t n);
int raw_frame_bin_vertical(const raw_frame_t *src, raw_frame_t *dst, uint32_t n);
int raw_frame_subsample_h2(const raw_frame_t *src, raw_frame_t *dst);
int raw_frame_subsample_v2(const raw_frame_t *src, raw_frame_t *dst);
int raw_frame_subsample(const raw_frame_t *src, raw_frame_t *dst,
                         uint32_t skip_h, uint32_t skip_v);
int raw_frame_roi_extract(const raw_frame_t *src, raw_frame_t *dst,
                           uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh);

/*===========================================================================
 * L5: Defect correction
 *===========================================================================*/

uint32_t detect_hot_pixels(const raw_frame_t *dark, double k, defect_map_t *m);
uint32_t detect_dead_pixels(const raw_frame_t *flat, double k, defect_map_t *m);
pixel_raw_t defect_pixel_correct(const raw_frame_t *f, uint32_t x, uint32_t y);
void raw_frame_defect_correct(raw_frame_t *f, const defect_map_t *m);
void defect_map_init(defect_map_t *m);
int defect_map_add(defect_map_t *m, uint32_t x, uint32_t y,
                    pixel_defect_type_t t, double severity);
void defect_map_free(defect_map_t *m);

/*===========================================================================
 * L5: Column FPN correction
 *===========================================================================*/

void estimate_column_fpn(const raw_frame_t *dark, double *offsets);
void apply_column_fpn_correction(raw_frame_t *f, const double *offsets);

/*===========================================================================
 * L5: Gain and black level
 *===========================================================================*/

void raw_frame_digital_gain(raw_frame_t *f, double gain);
void raw_frame_black_level_subtract(raw_frame_t *f, uint16_t bl);
void raw_frame_clamp(raw_frame_t *f, uint16_t bl);

#endif /* PIXEL_ARRAY_H */

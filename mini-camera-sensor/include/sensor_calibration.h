/**
 * @file sensor_calibration.h
 * @brief Sensor calibration: defect maps, flat-field, lens shading, CCM, PTC
 *
 * L1: calibration types, calibration data structures
 * L2: calibration methodology, golden module reference
 * L5: defect map generation, flat-field calibration, lens shading calibration,
 *     PTC characterization, CCM calibration, dark frame subtraction
 * L7: factory calibration pipeline, golden module calibration
 *
 * Reference: EMVA 1288 Standard; Janesick "Photon Transfer" (2007);
 *            ISO 14524 (OECF); ISO 15739 (noise measurements)
 */
#ifndef SENSOR_CALIBRATION_H
#define SENSOR_CALIBRATION_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "camera_sensor.h"
#include "pixel_array.h"
#include "color_science.h"

/*===========================================================================
 * L1: Calibration data types
 *===========================================================================*/

typedef struct {
    uint32_t width, height;    /* Map dimensions */
    double *data;              /* Per-pixel gain [row-major] */
} gain_map_t;

typedef struct {
    double a2, a4, a6;         /* Radial polynomial coefficients */
    double cx, cy;             /* Optical center */
} lens_shading_coeffs_t;

typedef struct {
    uint16_t bl_r, bl_gr;      /* Per-channel black level */
    uint16_t bl_gb, bl_b;
} black_level_calib_t;

typedef struct {
    double read_noise_e;       /* From PTC: y-intercept */
    double conversion_gain_dn_e; /* From PTC: slope */
    double prnu_coeff;         /* From PTC: high-signal deviation */
    double fwc_e;              /* From PTC: saturation knee */
    double linearity_error;    /* Max deviation from linear [%] */
    double dr_db;              /* Dynamic range */
    double snr_max_db;         /* Peak SNR */
} ptc_calibration_t;

typedef struct {
    double ccm[9];             /* 3x3 sensor→sRGB matrix */
    double wb_r, wb_g, wb_b;   /* D65 white balance gains */
    double delta_e_avg;        /* Avg calibration error */
} color_calibration_t;

typedef struct {
    black_level_calib_t black_level;
    defect_map_t defect_map;
    lens_shading_coeffs_t lens_shading;
    ptc_calibration_t ptc;
    color_calibration_t color;
    double temperature_c;      /* Calibration temperature */
    uint64_t timestamp;        /* Calibration timestamp (Unix) */
} sensor_calibration_t;

/*===========================================================================
 * L5: Calibration algorithms
 *===========================================================================*/

/** Initialize calibration structure with safe defaults */
void calibration_init(sensor_calibration_t *cal);

/** Free calibration resources */
void calibration_free(sensor_calibration_t *cal);

/*---------------------------------------------------------------------------
 * Black level calibration (L5)
 * Measures optical black pixels (shielded from light).
 *---------------------------------------------------------------------------*/

/** Calibrate black level from dark frame */
void calibrate_black_level(const raw_frame_t *dark, black_level_calib_t *bl);

/*---------------------------------------------------------------------------
 * Defect map generation (L5)
 * Detects dead, hot, stuck pixels from dark + flat-field frames.
 *---------------------------------------------------------------------------*/

/** Generate full defect map from dark + flat field */
int calibrate_defect_map(const raw_frame_t *dark_short,
                          const raw_frame_t *dark_long,
                          const raw_frame_t *flat,
                          defect_map_t *map);

/*---------------------------------------------------------------------------
 * Lens shading calibration (L5)
 * Fits radial gain model: gain(r)=1+a2*r^2+a4*r^4+a6*r^6
 *---------------------------------------------------------------------------*/

/** Calibrate lens shading coefficients from flat-field frame */
int calibrate_lens_shading(const raw_frame_t *flat,
                            lens_shading_coeffs_t *ls);

/** Generate per-pixel gain map from lens shading coefficients. O(w*h). */
int calibrate_generate_gain_map(const lens_shading_coeffs_t *ls,
                                 uint32_t w, uint32_t h,
                                 cfa_pattern_t cfa, gain_map_t *map);

/*---------------------------------------------------------------------------
 * PTC characterization (L5)
 * Gold standard sensor noise + gain characterization.
 *---------------------------------------------------------------------------*/

/** Full PTC analysis: shoots multiple exposure levels, fits model */
int calibrate_ptc(const uint16_t **frames, uint32_t n_levels,
                   uint32_t n_frames_per_level, uint32_t n_pixels,
                   ptc_calibration_t *ptc);

/*---------------------------------------------------------------------------
 * Color calibration (L5+L7)
 * CCM + WB from ColorChecker chart.
 *---------------------------------------------------------------------------*/

/** Calibrate color from ColorChecker measurements */
int calibrate_color(const double *sensor_rgb, const double *target_rgb,
                     uint32_t n_patches, color_calibration_t *cc);

/** Apply full color calibration to a linear RGB pixel */
rgb_linear_t calibrate_apply_color(rgb_linear_t rgb,
                                    const color_calibration_t *cc);

/*---------------------------------------------------------------------------
 * Full calibration pipeline (L7)
 *---------------------------------------------------------------------------*/

/** Run complete factory calibration pipeline */
int calibrate_sensor_full(const raw_frame_t *dark_short,
                           const raw_frame_t *dark_long,
                           const raw_frame_t *flat,
                           const uint16_t **ptc_frames, uint32_t ptc_levels,
                           uint32_t ptc_frames_per_level, uint32_t ptc_pixels,
                           const double *ccm_sensor, const double *ccm_target,
                           uint32_t ccm_patches,
                           sensor_calibration_t *cal);

/** Print calibration report to stdout */
void calibration_report(const sensor_calibration_t *cal);

/** Save/load calibration to/from file */
int calibration_save(const sensor_calibration_t *cal, const char *filename);
int calibration_load(sensor_calibration_t *cal, const char *filename);

/** Gain map allocation and management */
gain_map_t *gain_map_alloc(uint32_t w, uint32_t h);
void gain_map_free(gain_map_t *map);
void gain_map_fill(gain_map_t *map, double value);

#endif /* SENSOR_CALIBRATION_H */

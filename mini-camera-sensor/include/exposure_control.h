/**
 * @file exposure_control.h
 * @brief Auto Exposure (AE) and Auto Gain Control (AGC) algorithms
 *
 * L1: exposure value (EV), exposure triangle, metering modes, flicker
 * L2: 18% gray, zone system, exposure convergence, anti-banding
 * L3: PI controller, luminance statistics, histogram analysis
 * L5: mean-based AE, histogram percentile AE, center-weighted/spot/matrix
 *     metering, flicker avoidance, HDR bracket computation
 * L6: fast AE convergence, scene change detection, backlight handling
 * L7: smartphone AE, surveillance AE, automotive AE
 *
 * Reference: Kuno et al. IEEE T-CE 1998; Poynton (2012) Ch.4
 */
#ifndef EXPOSURE_CONTROL_H
#define EXPOSURE_CONTROL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "camera_sensor.h"
#include "pixel_array.h"

/*===========================================================================
 * L1: AE type definitions
 *===========================================================================*/

typedef enum {
    AE_METERING_AVERAGE = 0,
    AE_METERING_CENTER_WEIGHTED = 1,
    AE_METERING_SPOT = 2,
    AE_METERING_MATRIX = 3,
    AE_METERING_HIGHLIGHT_PRIORITY = 4,
    AE_METERING_FACE_PRIORITY = 5
} ae_metering_mode_t;

typedef enum {
    AE_SPEED_FAST = 0,
    AE_SPEED_NORMAL = 1,
    AE_SPEED_SLOW = 2,
    AE_SPEED_ADAPTIVE = 3
} ae_speed_t;

typedef enum {
    FLICKER_OFF = 0,
    FLICKER_50HZ = 1,
    FLICKER_60HZ = 2,
    FLICKER_AUTO = 3
} flicker_mode_t;

typedef enum {
    AE_STATE_IDLE = 0,
    AE_STATE_SEARCHING = 1,
    AE_STATE_CONVERGING = 2,
    AE_STATE_LOCKED = 3,
    AE_STATE_ANTI_BANDING = 4
} ae_state_t;

typedef struct {
    uint32_t x, y, w, h;
    double weight;
} ae_metering_zone_t;

typedef struct {
    double scene_luminance;
    double current_mean, current_median, target_mean;
    double error_pct;
    uint32_t saturated_pixels, dark_pixels;
    double pct_05, pct_50, pct_95;
} ae_statistics_t;

typedef struct {
    ae_metering_mode_t metering;
    ae_speed_t speed;
    flicker_mode_t flicker;
    double target_luminance;       /* 0-1, typ 0.18 */
    double exposure_compensation;  /* EV, [-2,+2] */
    uint8_t max_saturated_pct;
    double min_exposure_us, max_exposure_us;
    double min_gain, max_gain;
    uint8_t use_gain_first;
    uint8_t anti_flicker_enabled;
    double ae_kp, ae_ki;           /* PI gains */
} ae_config_t;

typedef struct {
    ae_state_t state;
    double current_exposure_us;
    double current_gain;
    double error_integral;
    uint32_t frame_count, stable_count;
    double prev_luminance;
    uint8_t scene_changed;
    ae_statistics_t stats;
} ae_state_data_t;

/*===========================================================================
 * L5: AE metering algorithms
 *===========================================================================*/

/** Compute AE statistics from raw Bayer frame. O(w*h). */
void ae_compute_statistics(const raw_frame_t *f, const ae_config_t *cfg,
                            ae_statistics_t *s);

double ae_meter_average(const raw_frame_t *f);
double ae_meter_center_weighted(const raw_frame_t *f);
double ae_meter_spot(const raw_frame_t *f, double spot_ratio);
double ae_meter_matrix(const raw_frame_t *f, const ae_metering_zone_t *zones,
                        uint32_t n_zones);
double ae_meter_percentile(const raw_frame_t *f, double pct);

/*===========================================================================
 * L5: AE control algorithms
 *===========================================================================*/

void ae_config_init_default(ae_config_t *cfg);
void ae_state_init(ae_state_data_t *s, double init_exp, double init_gain);

/** PI control: error=target-current; adj=Kp*err+Ki*integral(err). O(1). */
int ae_pi_control(const ae_statistics_t *s, ae_config_t *cfg,
                   ae_state_data_t *state, double *new_exp, double *new_gain);

/** Binary search initial exposure. O(log(range)). */
void ae_search_exposure(ae_state_data_t *s, ae_config_t *cfg,
                         double *exp, double *gain, uint8_t *done);

int ae_detect_scene_change(const ae_statistics_t *prev,
                            const ae_statistics_t *curr, double threshold);

/*===========================================================================
 * L6: Flicker detection
 *===========================================================================*/

flicker_mode_t flicker_detect(const double *means, uint32_t n,
                               double interval_s);

double flicker_safe_exposure(double desired_us, flicker_mode_t f,
                              uint32_t *multiples);

/*===========================================================================
 * L6: HDR bracket
 *===========================================================================*/

void ae_hdr_bracket(double base_us, double ratio, uint32_t n, double *exp);
int ae_determine_hdr_bracket(const raw_frame_t *f, double *exp,
                              uint32_t *n, double *ratio);

/*===========================================================================
 * L6: Utilities
 *===========================================================================*/

double ae_exposure_value(double exp_us, double gain_lin);
void ae_statistics_print(const ae_statistics_t *s);
int ae_is_acceptable(const ae_statistics_t *s, const ae_config_t *cfg);

#endif /* EXPOSURE_CONTROL_H */

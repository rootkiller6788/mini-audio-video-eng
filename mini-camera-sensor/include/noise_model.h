/**
 * @file noise_model.h
 * @brief Sensor noise models — physical noise sources and simulation
 *
 * L1: shot noise, read noise, dark noise, PRNU, FPN, quantization,
 *     kTC noise, 1/f noise, RTS noise
 * L2: noise budget, noise vs ISO/temperature, PTC noise analysis
 * L3: Poisson, Gaussian approximation, noise propagation
 * L4: kTC = sqrt(kT/C)/q, Johnson-Nyquist = sqrt(4kTR*BW), Poisson σ=√N
 * L5: noise synthesis, noise estimation from frames, PTC estimation
 * L6: SNR curve analysis, optimal gain selection
 *
 * Reference: Janesick "Photon Transfer" (2007); Tian & Fowler IEEE T-ED 2001
 */
#ifndef NOISE_MODEL_H
#define NOISE_MODEL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "camera_sensor.h"

/*===========================================================================
 * L1: Noise component enumeration and parameter structures
 *===========================================================================*/

typedef enum {
    NOISE_SHOT=0, NOISE_READ=1, NOISE_DARK_SHOT=2, NOISE_PRNU=3,
    NOISE_FPN_COLUMN=4, NOISE_FPN_ROW=5, NOISE_KTC=6,
    NOISE_QUANTIZATION=7, NOISE_1F=8, NOISE_RTS=9, NOISE_COUNT=10
} noise_component_t;

typedef struct {
    double shot_coeff;         /* Poisson: sqrt(signal) */
    double read_noise_e;       /* Read noise [e- RMS] */
    double dark_current_e;     /* Dark current [e-/px/s] */
    double prnu_coeff;         /* PRNU proportional coeff */
    double fpn_col_e;          /* Column FPN [e- RMS] */
    double fpn_row_e;          /* Row FPN [e- RMS] */
    double ktc_noise_e;        /* Reset noise [e- RMS] */
    double quant_lsb_e;        /* Quantization LSB [e-] */
    double flicker_coeff;      /* 1/f noise scaling */
    double rts_amplitude_e;    /* RTS amplitude [e-] */
    double rts_time_constant;  /* RTS correlation time [s] */
} noise_params_t;

typedef struct {
    double rts_state;
    double flicker_state;
    uint64_t sample_count;
} noise_state_t;

typedef struct {
    double read_noise_est;
    double dark_current_est;
    double prnu_est;
    double fpn_col_est;
    double fpn_row_est;
    double total_noise_est;
    double confidence;
} noise_measurement_t;

/*===========================================================================
 * L4: Fundamental noise laws
 *===========================================================================*/

/** kTC reset noise: σ = sqrt(k*T*C)/q [e- RMS]. O(1). */
double noise_ktc_reset_e(double temp_k, double capacitance_f);

/** Johnson-Nyquist: v_rms = sqrt(4*k*T*R*BW) [V]. O(1). */
double noise_johnson_nyquist_vrms(double res_ohm, double bw_hz, double temp_k);

/** Quantization noise: σ = LSB/sqrt(12). O(1). */
double noise_quantization_rms(double lsb);

/*===========================================================================
 * L5: Noise synthesis — accurate physical noise generation
 *===========================================================================*/

void noise_params_init_default(noise_params_t *np);

/** Generate Poisson shot noise for signal_e electrons. O(1). */
double noise_generate_shot(double signal_e);

/** Generate Gaussian read noise. O(1). */
double noise_generate_read(double sigma_e);

/** Generate dark current noise for exposure_s seconds. O(1). */
double noise_generate_dark(double dark_rate_e_s, double exposure_s);

/** Apply PRNU: signal*(1 + coeff*prnu_fixed). O(1). */
double noise_apply_prnu(double signal_e, double coeff, double prnu_fixed);

/** Generate total pixel noise (all sources combined). O(1). */
double noise_generate_pixel(double signal_e, const noise_params_t *np,
                             noise_state_t *state, double exposure_s,
                             double prnu_fixed);

/** Generate noisy raw frame from ideal signal. O(w*h). */
void noise_generate_frame(uint16_t *ideal, uint32_t w, uint32_t h,
                           const noise_params_t *np, noise_state_t *states,
                           double exposure_s, const double *prnu_map,
                           uint16_t *noisy);

/*===========================================================================
 * L5: Noise estimation from measurements
 *===========================================================================*/

/** Read noise from two dark frames: stddev(d1-d2)/sqrt(2). O(n). */
double noise_estimate_read_noise(const double *dark1, const double *dark2,
                                  uint32_t n);

/** Total noise from N flat-field frames. O(N*n). */
void noise_estimate_total(const double **flats, uint32_t n_frames,
                           uint32_t n_pix, double *total, double *fixed);

/** PTC parameter estimation: variance=read^2+mean/CG+(PRNU*mean)^2. O(n). */
int noise_ptc_estimate(const double *means, const double *vars, uint32_t n,
                        double *read_noise, double *cg, double *prnu);

/** SNR vs signal curve computation. O(n_points). */
void noise_snr_curve(const noise_params_t *np, uint32_t n_pts, double fwc,
                      double *signal_out, double *snr_out);

/*===========================================================================
 * L6: SNR vs gain analysis
 *===========================================================================*/

/** Input-referred noise and FWC as function of analog gain. O(1). */
void noise_vs_gain(double gain, const noise_params_t *np,
                    double *fwc_out, double *noise_out);

/** Optimal analog gain maximizing DR for target signal. O(1). */
double noise_optimal_gain(double target_signal_e, const noise_params_t *np,
                           double max_gain);

#endif /* NOISE_MODEL_H */

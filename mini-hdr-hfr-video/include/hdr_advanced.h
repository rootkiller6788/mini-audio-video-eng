#ifndef HDR_ADVANCED_H
#define HDR_ADVANCED_H

#include "hdr_core.h"
#include "hdr_tone_mapping.h"
#include "hdr_color.h"

#ifdef __cplusplus
extern "C" {
#endif

/* JzAzBz -- Perceptually Uniform HDR Color Space (ITU-R BT.2124) */
typedef struct {
    double Jz;
    double Az;
    double Bz;
} jzazbz_t;

void color_xyz_to_jzazbz(const cie_xyz_t *xyz, double lum_max, jzazbz_t *jzaz);
void color_jzazbz_to_xyz(const jzazbz_t *jzaz, double lum_max, cie_xyz_t *xyz);
void color_bt2020_to_jzazbz(const hdr_rgb_pixel_t *rgb, double lum_max, jzazbz_t *jzaz);

/* Chromatic Adaptation Transform (Bradford) */
void color_bradford_cat(const cie_xyz_t *xyz_src, const cie_xyz_t *white_src,
                        const cie_xyz_t *white_dst, cie_xyz_t *xyz_dst);
void color_bradford_matrix(const cie_xyz_t *white_src, const cie_xyz_t *white_dst,
                           double matrix[3][3]);
void color_illuminant_xyz(char illuminant, cie_xyz_t *xyz);

/* HDR Quality Assessment */
typedef struct {
    double psnr_linear;
    double psnr_pu;
    double pu_ssim;
    double log_rmse;
    double delta_e_itp;
    double hdr_vdp_score;
} hdr_quality_metrics_t;

double hdr_pu_encode(double luminance);
double hdr_pu_decode(double pu_value);
void hdr_quality_assess(const hdr_image_buffer_t *ref, const hdr_image_buffer_t *test,
                        hdr_quality_metrics_t *metrics);
void hdr_hdr_vdp_map(const hdr_image_buffer_t *ref, const hdr_image_buffer_t *test,
                     double *prob_map, int width, int height);

/* ACES Support */
void color_aces_ap0_to_ap1(const hdr_rgb_pixel_t *ap0, hdr_rgb_pixel_t *ap1);
void color_aces_ap1_to_ap0(const hdr_rgb_pixel_t *ap1, hdr_rgb_pixel_t *ap0);
void color_aces_rrt_approximate(const hdr_rgb_pixel_t *scene_linear, hdr_rgb_pixel_t *display);

/* HDR Statistics & Psychophysics */
double hdr_stevens_brightness(double luminance);
double hdr_surround_contrast_factor(double luminance, double surround_lum);
double hdr_optimal_peak_for_ambient(double ambient_lux, double black_level);

#ifdef __cplusplus
}
#endif

#endif /* HDR_ADVANCED_H */

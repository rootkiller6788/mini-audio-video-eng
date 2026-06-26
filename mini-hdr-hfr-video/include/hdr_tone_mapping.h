#ifndef HDR_TONE_MAPPING_H
#define HDR_TONE_MAPPING_H

#include "hdr_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ?? L1: Tone Mapping Definitions ????????????????????????????????????? */

typedef enum {
    TMO_LINEAR_CLIP = 0,
    TMO_REINHARD_GLOBAL,
    TMO_REINHARD_LOCAL,
    TMO_DRAGO_LOGARITHMIC,
    TMO_DURAND_BILATERAL,
    TMO_MANTIUK_CONTRAST,
    TMO_WARD_HISTOGRAM,
    TMO_FATTAL_GRADIENT,
    TMO_BT2446_METHOD_A,
    TMO_BT2446_METHOD_B,
    TMO_CUSTOM_PARAMETRIC,
    TMO_COUNT
} tone_map_operator_t;

typedef enum {
    TMO_COLOR_NAIVE_RGB = 0,
    TMO_COLOR_LUMINANCE_ONLY,
    TMO_COLOR_CIECAM02,
    TMO_COLOR_IPT,
    TMO_COLOR_ICTCP,
    TMO_COLOR_COUNT
} tmo_color_strategy_t;

typedef struct {
    tone_map_operator_t    method;
    tmo_color_strategy_t   color_strategy;
    int                    auto_exposure;
    double                 key_value;
    double                 target_peak;
    double                 saturation;
    double                 contrast_boost;
    int                    tone_map_width;
    int                    tone_map_height;
    double                 shadow_boost;
    double                 highlight_rolloff;
    double                 gamma_correction;
} tmo_config_t;

typedef struct {
    double log_average_luminance;
    double geometric_mean;
    double min_luminance;
    double max_luminance;
    double dynamic_range_stops;
    double percentile_01;
    double percentile_50;
    double percentile_99;
    double shadow_clip;
    double highlight_clip;
} tmo_scene_analysis_t;

typedef struct {
    double spatial_sigma;
    double range_sigma;
    int    kernel_size;
    int    use_log_domain;
    double sampling_ratio;
} tmo_bilateral_params_t;

typedef struct {
    double r;
    double g;
    double b;
} hdr_rgb_pixel_t;

typedef struct {
    int              width;
    int              height;
    hdr_rgb_pixel_t *pixels;
    double           peak_nits;
    hdr_primaries_t  primaries;
} hdr_image_buffer_t;

/* API declarations */
void tmo_config_init(tmo_config_t *config);
void tmo_analyze_scene(const hdr_image_buffer_t *image, tmo_scene_analysis_t *analysis);
double tmo_compute_key(double log_average, double burn_pct);
double tmo_reinhard_global(double L_world, double L_white);
double tmo_drago_log(double L_w, double L_max, double bias);
hdr_rgb_pixel_t *tmo_apply(const hdr_image_buffer_t *image, const tmo_config_t *config, int *out_width, int *out_height);
hdr_image_buffer_t *hdr_image_alloc(int width, int height, double peak_nits, hdr_primaries_t primaries);
void hdr_image_free(hdr_image_buffer_t *image);
void hdr_image_fill(hdr_image_buffer_t *image, double r, double g, double b);
hdr_image_buffer_t *hdr_image_create_test_pattern(int width, int height, double max_nits);
double tmo_bt2446_method_a(double pq_signal, double display_peak);
double tmo_bt2446_method_b(double hlg_signal, double gain);
void tmo_bilateral_filter(const double *src, double *dst, int width, int height, const tmo_bilateral_params_t *params);
void tmo_base_detail_decompose(const double *src, double *base, double *detail, int width, int height, const tmo_bilateral_params_t *params);

#ifdef __cplusplus
}
#endif

#endif /* HDR_TONE_MAPPING_H */

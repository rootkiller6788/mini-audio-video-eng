#ifndef HDR_CORE_H
#define HDR_CORE_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── L1: Fundamental Definitions ─────────────────────────────────────── */

#define HDR_SDR_PEAK_NITS      100.0f
#define HDR_HLG_PEAK_NITS     1000.0f
#define HDR_PQ_PEAK_NITS     10000.0f
#define HDR_DISPLAY_P3_NITS    400.0f
#define HDR_OLED_PEAK_NITS     540.0f
#define HDR_LCD_HDR1000_NITS  1000.0f
#define HDR_DOLBY_CINEMA_NITS  108.0f

typedef enum {
    HDR_TF_SDR_GAMMA_22 = 0,
    HDR_TF_SDR_GAMMA_24,
    HDR_TF_PQ_ST2084,
    HDR_TF_HLG_ARIB_B67,
    HDR_TF_SLOG3,
    HDR_TF_CLOG2,
    HDR_TF_VLOG,
    HDR_TF_ACES_AP0,
    HDR_TF_LINEAR,
    HDR_TF_COUNT
} hdr_transfer_function_t;

typedef enum {
    HDR_PRIMARIES_BT709 = 0,
    HDR_PRIMARIES_DCI_P3,
    HDR_PRIMARIES_BT2020,
    HDR_PRIMARIES_ACES_AP0,
    HDR_PRIMARIES_ACES_AP1,
    HDR_PRIMARIES_COUNT
} hdr_primaries_t;

typedef struct {
    double x;
    double y;
} hdr_chromaticity_t;

typedef struct {
    hdr_chromaticity_t red;
    hdr_chromaticity_t green;
    hdr_chromaticity_t blue;
    hdr_chromaticity_t white;
    const char *name;
} hdr_primaries_set_t;

typedef struct {
    hdr_primaries_set_t mastering_primaries;
    double mastering_max_luminance;
    double mastering_min_luminance;
    double max_content_light_level;
    double max_frame_average_light_level;
    double pq_min_signal;
    double pq_max_signal;
    int is_hlg_based;
    int is_scene_referred;
    double scene_max_rgb[3];
    double scene_average_rgb[3];
} hdr_metadata_t;

typedef struct {
    int width;
    int height;
    int bit_depth;
    int channels;
    int chroma_subsample;
    uint8_t encoding;
    uint8_t transfer_type;
    uint8_t primaries_type;
    uint8_t pixel_range;
    uint8_t sample_order;
    uint8_t bit_packing;
    uint8_t endianness;
    uint8_t reserved[32];
} hdr_pixel_format_t;

typedef struct {
    int num_bins;
    int bin_count;
    double log_min;
    double log_max;
    double bin_width;
    uint64_t *bins;
    uint64_t total_samples;
    double percentile_50;
    double percentile_90;
    double percentile_99;
} hdr_luminance_histogram_t;

typedef struct {
    double input_value;
    double output_value;
    double first_deriv;
    double second_deriv;
} hdr_transfer_point_t;

typedef struct {
    hdr_transfer_function_t tf_type;
    int lut_size;
    double input_min;
    double input_max;
    double *forward_lut;
    double *inverse_lut;
    double input_step;
    int use_cache;
    double last_input;
    double last_output;
} hdr_transfer_lut_t;

typedef struct {
    double m1;
    double m2;
    double c1;
    double c2;
    double c3;
} hdr_pq_params_t;

typedef struct {
    double a;
    double b;
    double c;
    double system_gamma;
    double nominal_peak;
} hdr_hlg_params_t;

typedef struct {
    double gamma;
    double black_offset;
    double nominal_peak;
} hdr_bt1886_params_t;

typedef struct {
    hdr_transfer_function_t type;
    union {
        hdr_pq_params_t     pq;
        hdr_hlg_params_t    hlg;
        hdr_bt1886_params_t bt1886;
        double              pure_gamma;
    } params;
    int direction;
} hdr_transfer_evaluator_t;

typedef struct {
    double weber_fraction;
    double adaptation_level;
    double jnd_threshold;
} hdr_weber_model_t;

typedef struct {
    double luminance;
    double spatial_freq;
    double pupil_diameter;
    double temporal_freq;
    double csf_threshold;
    double mtf_optical;
    double mtf_neural;
    double quantum_efficiency;
} hdr_barten_csf_t;

typedef struct {
    double peak_luminance;
    double black_level;
    double diffuse_white;
    hdr_primaries_t primaries;
    double color_volume[8][3];
    double eotf_gamma;
    int is_hdr_capable;
    int supports_pq;
    int supports_hlg;
    double min_frame_interval_ms;
    int max_bit_depth;
    double ambient_lux;
} hdr_display_model_t;

/* API declarations */
void hdr_pq_params_init(hdr_pq_params_t *pq);
void hdr_hlg_params_init(hdr_hlg_params_t *hlg);
void hdr_bt1886_params_init(hdr_bt1886_params_t *bt, double peak_nits, double gamma_val);
double hdr_pq_eotf(double signal, const hdr_pq_params_t *pq);
double hdr_pq_oetf(double luminance, const hdr_pq_params_t *pq);
double hdr_hlg_oetf(double linear_light, const hdr_hlg_params_t *hlg);
double hdr_hlg_eotf(double signal, const hdr_hlg_params_t *hlg);
double hdr_bt1886_eotf(double signal, const hdr_bt1886_params_t *bt);
double hdr_transfer_evaluate(double value, const hdr_transfer_evaluator_t *eval);
void hdr_transfer_eval_init(hdr_transfer_evaluator_t *eval, hdr_transfer_function_t type, int dir);
int hdr_lut_build_forward(hdr_transfer_lut_t *lut, hdr_transfer_function_t tf_type, int lut_size, double in_min, double in_max);
double hdr_lut_lookup_forward(const hdr_transfer_lut_t *lut, double value);
int hdr_lut_build_inverse(hdr_transfer_lut_t *lut, hdr_transfer_function_t tf_type, int lut_size, double out_min, double out_max);
double hdr_lut_lookup_inverse(const hdr_transfer_lut_t *lut, double value);
void hdr_lut_destroy(hdr_transfer_lut_t *lut);
void hdr_metadata_init(hdr_metadata_t *meta);
double hdr_metadata_compute_maxcll(const hdr_luminance_histogram_t *histogram, double percentile);
double hdr_metadata_compute_maxfall(const hdr_luminance_histogram_t *histogram);
hdr_luminance_histogram_t *hdr_histogram_create(int num_bins, double log_min, double log_max);
void hdr_histogram_clear(hdr_luminance_histogram_t *hist);
int hdr_histogram_add(hdr_luminance_histogram_t *hist, double luminance);
void hdr_histogram_compute_percentiles(hdr_luminance_histogram_t *hist);
void hdr_histogram_destroy(hdr_luminance_histogram_t *hist);
void hdr_display_init(hdr_display_model_t *display, double peak_nits, double black_nits, hdr_primaries_t primaries, int is_hdr);
const hdr_primaries_set_t *hdr_primaries_get(hdr_primaries_t type);
void hdr_chromaticity_to_xyz(hdr_chromaticity_t chroma, double Y, double *X, double *Z);
void hdr_weber_init(hdr_weber_model_t *model, double adaptation);
double hdr_weber_jnd(const hdr_weber_model_t *model, double L);
double hdr_barten_csf_compute(hdr_barten_csf_t *csf);
int hdr_barten_min_bit_depth(double peak_luminance, double black_luminance);

#ifdef __cplusplus
}
#endif

#endif /* HDR_CORE_H */

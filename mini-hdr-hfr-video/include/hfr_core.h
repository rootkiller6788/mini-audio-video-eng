#ifndef HFR_CORE_H
#define HFR_CORE_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ?? L1: Fundamental HFR Definitions ?????????????????????????????????? */

#define HFR_STANDARD_24P     24.0
#define HFR_STANDARD_25P     25.0
#define HFR_STANDARD_30P     30.0
#define HFR_NTSC_29_97       29.97
#define HFR_STANDARD_48P     48.0
#define HFR_STANDARD_50P     50.0
#define HFR_STANDARD_60P     60.0
#define HFR_NTSC_59_94       59.94
#define HFR_HIGH_100P       100.0
#define HFR_HIGH_120P       120.0
#define HFR_ULTRA_240P      240.0
#define HFR_ULTRA_300P      300.0
#define HFR_MAX_FRAMERATE   480.0

#define HFR_DEFAULT_SHUTTER_ANGLE 180.0

typedef enum {
    HFR_FRAME_NONE = 0,
    HFR_FRAME_ORIGINAL,
    HFR_FRAME_INTERPOLATED,
    HFR_FRAME_MERGED,
    HFR_FRAME_MIXED,
    HFR_FRAME_TYPE_COUNT
} hfr_frame_type_t;

typedef enum {
    HFR_INTERP_FRAME_BLEND = 0,
    HFR_INTERP_FRAME_DUPLICATE,
    HFR_INTERP_FRAME_SAMPLING,
    HFR_INTERP_MOTION_COMP_MCFI,
    HFR_INTERP_OPTICAL_FLOW,
    HFR_INTERP_PHASE_BASED,
    HFR_INTERP_NEURAL,
    HFR_INTERP_COUNT
} hfr_interp_method_t;

typedef enum {
    HFR_PULLDOWN_2_3 = 0,
    HFR_PULLDOWN_2_3_3_2,
    HFR_PULLDOWN_24_TO_60,
    HFR_PULLDOWN_25_TO_50,
    HFR_PULLDOWN_EURO,
    HFR_PULLDOWN_COUNT
} hfr_pulldown_t;

typedef enum {
    HFR_SCAN_PROGRESSIVE = 0,
    HFR_SCAN_INTERLACED_TOP_FIRST,
    HFR_SCAN_INTERLACED_BOTTOM_FIRST,
    HFR_SCAN_COUNT
} hfr_scan_type_t;

/* ?? L1: Frame Structures ????????????????????????????????????????????? */

typedef struct {
    int width;
    int height;
    int bit_depth;
    int channels;
    double *data;
    uint64_t pts;
    uint64_t duration;
    int64_t frame_index;
    double timestamp_sec;
    hfr_frame_type_t type;
    hfr_scan_type_t scan;
    double shutter_angle;
} hfr_frame_t;

typedef struct {
    char codec[8];
    double framerate;
    int width;
    int height;
    int bit_depth;
    double pixel_aspect_ratio;
    hfr_scan_type_t scan_type;
    int is_hdr;
    int is_interlaced;
    double duration_sec;
    uint64_t total_frames;
    double avg_bitrate_bps;
    int color_primaries;
    int transfer_characteristics;
} hfr_video_info_t;

typedef struct {
    double framerate_in;
    double framerate_out;
    int suppress_judder;
    double motion_blur_amount;
    double cadence_threshold;
    int detect_cadence;
    int adaptive_blend;
} hfr_conversion_config_t;

typedef struct {
    double *frames;
    int num_frames;
    int allocated_frames;
    int width;
    int height;
} hfr_frame_buffer_t;

/* ?? API: Frame Management ???????????????????????????????????????????? */

hfr_frame_t *hfr_frame_alloc(int width, int height, int channels);
void hfr_frame_free(hfr_frame_t *frame);
int hfr_frame_copy(const hfr_frame_t *src, hfr_frame_t *dst);
void hfr_frame_fill(hfr_frame_t *frame, double value);
double hfr_frame_pixel_get(const hfr_frame_t *frame, int x, int y, int channel);
void hfr_frame_pixel_set(hfr_frame_t *frame, int x, int y, int channel, double value);

/* ?? API: Frame Buffer ???????????????????????????????????????????????? */

hfr_frame_buffer_t *hfr_buffer_create(int initial_capacity, int width, int height);
void hfr_buffer_destroy(hfr_frame_buffer_t *buffer);
int hfr_buffer_push(hfr_frame_buffer_t *buffer, const double *frame_data);
const double *hfr_buffer_get(const hfr_frame_buffer_t *buffer, int index);
int hfr_buffer_size(const hfr_frame_buffer_t *buffer);

/* ?? API: Frame Rate Conversion ??????????????????????????????????????? */

void hfr_conversion_config_init(hfr_conversion_config_t *config);
double hfr_compute_conversion_ratio(double fps_in, double fps_out);
int hfr_find_conversion_offset(double fps_in, double fps_out, int frame_index);
int hfr_detect_original_cadence(const double *frame_diffs, int num_frames, double threshold);
double hfr_compute_frame_difference_mad(const hfr_frame_t *a, const hfr_frame_t *b);

/* ?? API: Pull-Down / Cadence ????????????????????????????????????????? */

int hfr_pulldown_pattern_length(hfr_pulldown_t pattern);
void hfr_pulldown_apply_2_3(const hfr_frame_t *input_frames, int input_count, hfr_frame_t *output_frames, int *output_count);
void hfr_pulldown_inverse_telecine(const hfr_frame_t *input_frames, int input_count, hfr_frame_t *output_frames, int *output_count);
int hfr_detect_pulldown_pattern(const hfr_frame_t *frames, int num_frames);

/* ?? API: Frame Interpolation ????????????????????????????????????????? */

double hfr_interp_lerp(double a, double b, double t);
void hfr_frame_blend(const hfr_frame_t *prev, const hfr_frame_t *next, double weight, hfr_frame_t *output);
void hfr_frame_duplicate(const hfr_frame_t *src, hfr_frame_t *dst);
int hfr_interpolate_frames(const hfr_frame_t *prev, const hfr_frame_t *next, hfr_frame_t *output, hfr_interp_method_t method, double weight);

/* ?? API: Temporal Processing ????????????????????????????????????????? */

double *hfr_compute_frame_diff(const hfr_frame_t *a, const hfr_frame_t *b);
void hfr_temporal_median_3(const hfr_frame_t *prev, const hfr_frame_t *curr, const hfr_frame_t *next, hfr_frame_t *output);
void hfr_temporal_denoise_simple(hfr_frame_buffer_t *buffer, int frame_idx, int radius, hfr_frame_t *output);

/* ?? API: Deinterlacing ??????????????????????????????????????????????? */

void hfr_deinterlace_weave(const hfr_frame_t *top_field, const hfr_frame_t *bottom_field, hfr_frame_t *output);
void hfr_deinterlace_bob(const hfr_frame_t *field, hfr_frame_t *output, int is_top_field);
void hfr_deinterlace_yadif(const hfr_frame_t *prev, const hfr_frame_t *curr, const hfr_frame_t *next, hfr_frame_t *output, int field_parity);

/* ?? API: Motion Adaptive Processing ?????????????????????????????????? */

double hfr_motion_detect_pixel(const hfr_frame_t *prev, const hfr_frame_t *curr, int x, int y);
void hfr_motion_map_compute(const hfr_frame_t *prev, const hfr_frame_t *curr, double *motion_map);
void hfr_motion_adaptive_blend(const hfr_frame_t *prev, const hfr_frame_t *curr, const double *motion_map, double threshold, hfr_frame_t *output);

/* ?? API: Shutter / Motion Blur ??????????????????????????????????????? */

double hfr_shutter_speed_from_angle(double angle_degrees, double framerate);
double hfr_motion_blur_kernel_size(double shutter_angle, double framerate, double fps_ratio);
void hfr_add_motion_blur_1d(double *scanline, int length, double kernel_pixels);

#ifdef __cplusplus
}
#endif

#endif /* HFR_CORE_H */

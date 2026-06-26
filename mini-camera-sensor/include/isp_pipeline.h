/**
 * @file isp_pipeline.h
 * @brief Image Signal Processing pipeline — RAW→RGB→YUV data flow
 *
 * L1: ISP stage enumeration, pixel formats (RGB8, RGB16, YUV)
 * L2: ISP data flow, stage ordering from raw to display
 * L5: black level, lens shading, white balance, demosaic, CCM, gamma,
 *     noise reduction, edge enhancement, tone mapping, false color suppression
 * L6: RAW-to-sRGB rendering, YUV encoding for video
 * L7: digital camera ISP, smartphone camera pipeline, machine vision ISP
 *
 * Reference: Poynton (2012) Ch.22-28; Ramanath et al. IEEE SPM 2005
 */
#ifndef ISP_PIPELINE_H
#define ISP_PIPELINE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "camera_sensor.h"
#include "pixel_array.h"

/*===========================================================================
 * L1: ISP stage enumeration and types
 *===========================================================================*/

typedef enum {
    ISP_STAGE_RAW_INPUT = 0,
    ISP_STAGE_BLACK_LEVEL = 1,
    ISP_STAGE_DEFECT_CORRECTION = 2,
    ISP_STAGE_LENS_SHADING = 3,
    ISP_STAGE_WHITE_BALANCE = 4,
    ISP_STAGE_DEMOSAIC = 5,
    ISP_STAGE_COLOR_CORRECTION = 6,
    ISP_STAGE_GAMMA = 7,
    ISP_STAGE_COLOR_SPACE = 8,
    ISP_STAGE_NOISE_REDUCTION = 9,
    ISP_STAGE_EDGE_ENHANCE = 10,
    ISP_STAGE_TONE_MAPPING = 11,
    ISP_STAGE_CONTRAST = 12,
    ISP_STAGE_SATURATION = 13,
    ISP_STAGE_CROP_SCALE = 14,
    ISP_STAGE_FALSE_COLOR = 15,
    ISP_STAGE_FORMAT_OUTPUT = 16,
    ISP_STAGE_COUNT = 17
} isp_stage_t;

typedef struct {
    isp_stage_t stage;
    uint8_t enabled;
    void *params;
} isp_stage_config_t;

typedef struct {
    isp_stage_config_t stages[ISP_STAGE_COUNT];
    uint8_t stages_enabled;
} isp_pipeline_config_t;

/* RGB pixel types */
typedef struct { uint8_t r, g, b; } rgb_pixel_t;
typedef struct { uint16_t r, g, b; } rgb16_pixel_t;
typedef struct { float r, g, b; } rgbf_pixel_t;

typedef struct { uint8_t y; int8_t u; int8_t v; } yuv_pixel_t;

typedef struct {
    void *data;
    uint32_t width, height;
    uint8_t bits_per_channel;
} rgb_image_t;

typedef struct {
    yuv_pixel_t *data;
    uint32_t width, height;
} yuv_image_t;

/*===========================================================================
 * L5: ISP pipeline stage functions — each implements independent knowledge
 *===========================================================================*/

void isp_pipeline_init_default(isp_pipeline_config_t *p);
void isp_stage_enable(isp_pipeline_config_t *p, isp_stage_t s, uint8_t en);

rgb_image_t *rgb_image_alloc(uint32_t w, uint32_t h, uint8_t bpc);
void rgb_image_free(rgb_image_t *img);
yuv_image_t *yuv_image_alloc(uint32_t w, uint32_t h);
void yuv_image_free(yuv_image_t *img);

/* Black level: subtract per-channel black offset. O(w*h). */
int isp_black_level_correct(raw_frame_t *raw, uint16_t bl_r, uint16_t bl_gr,
                             uint16_t bl_gb, uint16_t bl_b);

/* Lens shading: radial gain = 1 + a2*r^2 + a4*r^4 + a6*r^6. O(w*h). */
int isp_lens_shading_correct_rgb(rgb_image_t *img, double a2, double a4,
                                  double a6, double cx, double cy);

/* White balance: apply per-channel gains. O(w*h). */
int isp_white_balance_rgb(rgb_image_t *img, double wb_r, double wb_g,
                           double wb_b);

/* Gray World WB estimation: gains = G_avg / channel_avg. O(w*h). */
void isp_gray_world_estimate(const rgb_image_t *img,
                              double *wb_r, double *wb_g, double *wb_b);

/* White Patch WB estimation: brightest region = white. O(w*h). */
void isp_white_patch_estimate(const rgb_image_t *img,
                               double *wb_r, double *wb_g, double *wb_b);

/* 3x3 Color Correction Matrix. O(w*h). */
int isp_color_correction_rgb(rgb_image_t *img, const double ccm[9]);

/* CCM calibration from ColorChecker patches. O(n). */
int isp_ccm_calibrate(const double *sensor_rgb, const double *target_rgb,
                       uint32_t n, double ccm[9]);

/* sRGB gamma encode: piecewise linear+power. O(w*h). */
int isp_gamma_srgb_encode(rgb_image_t *img);
int isp_gamma_srgb_decode(rgb_image_t *img);

/* Power-law gamma: V_out = V_in^gamma. O(w*h). */
int isp_gamma_power_law(rgb_image_t *img, double gamma);

/* RGB→YUV BT.601: Y=0.299R+0.587G+0.114B. O(w*h). */
int isp_rgb_to_yuv_bt601(const rgb_image_t *rgb, yuv_image_t *yuv);

/* RGB→YUV BT.709: Y=0.2126R+0.7152G+0.0722B. O(w*h). */
int isp_rgb_to_yuv_bt709(const rgb_image_t *rgb, yuv_image_t *yuv);

/* 3x3 median filter for salt-and-pepper noise. O(w*h). */
int isp_median_filter_3x3(rgb_image_t *img);

/* Bilateral filter (edge-preserving denoising). O(w*h). */
int isp_bilateral_filter(rgb_image_t *img, double sigma_s, double sigma_r);

/* Unsharp mask sharpening. O(w*h). */
int isp_unsharp_mask(rgb_image_t *img, double amount, double sigma);

/* Reinhard global tone mapping. O(w*h). */
int isp_tone_map_reinhard(rgb_image_t *img, double key);

/* Contrast/brightness: out = (in-0.5)*contrast + 0.5 + brightness. O(w*h). */
int isp_contrast_brightness(rgb_image_t *img, double contrast, double bright);

/* False color suppression via chroma median filter. O(w*h). */
int isp_false_color_suppress(yuv_image_t *yuv, uint32_t kernel);

/*===========================================================================
 * L6: Complete pipeline execution
 *===========================================================================*/

/** Full ISP pipeline: RAW → sRGB. Allocates output image internally. */
int isp_pipeline_execute(const raw_frame_t *raw,
                          const isp_pipeline_config_t *p, rgb_image_t **out);

/** Full ISP pipeline: RAW → YUV for video encoding. */
int isp_pipeline_execute_yuv(const raw_frame_t *raw,
                              const isp_pipeline_config_t *p, yuv_image_t **out);

void isp_pipeline_print(const isp_pipeline_config_t *p);

#endif /* ISP_PIPELINE_H */

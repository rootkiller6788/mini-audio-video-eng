/**
 * video_codec.h — Core Video Codec Definitions and Frame Management
 *
 * L1 Definitions: Video pixel formats, frame structures, GOP types, slice types
 * L2 Core Concepts: Color space conversion, chroma subsampling, frame buffer
 * L4 Fundamental Laws: 2D sampling theorem, chroma subsampling theory
 *
 * Reference:
 *   Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
 *   ITU-T H.264 / ISO/IEC 14496-10 — Advanced Video Coding
 *   ITU-T H.265 / ISO/IEC 23008-2 — High Efficiency Video Coding
 *
 * Course Mapping:
 *   Stanford EE392J — Digital Video Processing
 *   MIT 6.344 — Digital Image Processing
 *   Berkeley EE225B — Digital Image Processing
 *   ETH 227-0447 — Image and Video Processing
 *   Michigan EECS 556 — Image Processing
 */

#ifndef VIDEO_CODEC_H
#define VIDEO_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Pixel Formats and Chroma Subsampling
 * ========================================================================== */

/** Chroma subsampling format — determines Y:Cb:Cr sample ratio */
typedef enum {
    CSF_YUV400  = 0,  /**< Luma only, no chroma (grayscale) */
    CSF_YUV420  = 1,  /**< 4:2:0 — chroma at 1/2 horiz and 1/2 vert resolution */
    CSF_YUV422  = 2,  /**< 4:2:2 — chroma at 1/2 horiz resolution */
    CSF_YUV444  = 3,  /**< 4:4:4 — full chroma resolution */
    CSF_YUV411  = 4,  /**< 4:1:1 — chroma at 1/4 horiz resolution */
    CSF_NV12    = 5,  /**< Semi-planar 4:2:0 (interleaved CbCr plane) */
    CSF_NV21    = 6,  /**< Semi-planar 4:2:0 (interleaved CrCb plane) */
} chroma_subsampling_t;

/** Raw pixel format — fully specifies the color component layout */
typedef enum {
    PIX_FMT_YUV420P = 0,    /**< Planar YUV 4:2:0, 12 bpp */
    PIX_FMT_YUV422P = 1,    /**< Planar YUV 4:2:2, 16 bpp */
    PIX_FMT_YUV444P = 2,    /**< Planar YUV 4:4:4, 24 bpp */
    PIX_FMT_NV12    = 3,    /**< Semi-planar YUV 4:2:0, 12 bpp */
    PIX_FMT_RGB24   = 4,    /**< Packed RGB 8:8:8, 24 bpp */
    PIX_FMT_BGR24   = 5,    /**< Packed BGR 8:8:8, 24 bpp */
    PIX_FMT_RGBA32  = 6,    /**< Packed RGBA 8:8:8:8, 32 bpp */
    PIX_FMT_YUV420P10LE = 7, /**< Planar YUV 4:2:0 10-bit LE */
} pixel_format_t;

/** Color primaries — defines the RGB color gamut reference */
typedef enum {
    COLOR_PRI_BT709     = 1,  /**< ITU-R BT.709 (sRGB, HDTV) */
    COLOR_PRI_BT470M    = 4,  /**< ITU-R BT.470 System M (NTSC) */
    COLOR_PRI_BT470BG   = 5,  /**< ITU-R BT.470 System B/G (PAL/SECAM) */
    COLOR_PRI_BT2020    = 9,  /**< ITU-R BT.2020 (UHDTV wide gamut) */
    COLOR_PRI_SMPTE240M = 7,  /**< SMPTE 240M (HDTV production) */
} color_primaries_t;

/** Transfer characteristics — opto-electronic transfer function (OETF) */
typedef enum {
    TRANSFER_BT709   = 1,   /**< ITU-R BT.709 (gamma approx 2.2) */
    TRANSFER_SRGB    = 13,  /**< IEC 61966-2-1 sRGB */
    TRANSFER_PQ      = 16,  /**< SMPTE ST 2084 (PQ / HDR10) */
    TRANSFER_HLG     = 18,  /**< ARIB STD-B67 (HLG / HDR) */
    TRANSFER_LINEAR  = 8,   /**< Linear light (no gamma) */
} transfer_char_t;

/** Color matrix coefficients — YCbCr to RGB conversion matrix */
typedef enum {
    MATRIX_BT709    = 1,  /**< ITU-R BT.709 (HDTV) */
    MATRIX_BT601    = 5,  /**< ITU-R BT.601 (SDTV) */
    MATRIX_BT2020   = 9,  /**< ITU-R BT.2020 (UHDTV) */
    MATRIX_IDENTITY = 0,  /**< Identity (RGB) */
} color_matrix_t;

/** Video color space — complete color specification */
typedef struct {
    color_primaries_t primaries;
    transfer_char_t   transfer;
    color_matrix_t    matrix;
    uint8_t           full_range;
} video_color_spec_t;

/** Picture (slice) type — H.264 slice_type values */
typedef enum {
    SLICE_P  = 0,  /**< P-slice: predicted from one reference */
    SLICE_B  = 1,  /**< B-slice: bi-predictive from two references */
    SLICE_I  = 2,  /**< I-slice: intra-coded, no temporal prediction */
    SLICE_SP = 3,  /**< SP-slice: switching P (stream switching) */
    SLICE_SI = 4,  /**< SI-slice: switching I */
} slice_type_t;

/** GOP (Group of Pictures) structure definition */
typedef struct {
    uint32_t gop_size;
    uint32_t keyint_max;
    uint32_t idr_period;
    uint32_t num_b_frames;
    uint8_t  open_gop;
    uint8_t  hierarchical_b;
} gop_structure_t;

/** Macroblock (MB) partitioning — H.264 MB types */
typedef enum {
    MB_I_4x4   = 0,  /**< Intra 4x4 */
    MB_I_8x8   = 1,  /**< Intra 8x8 (FRExt only) */
    MB_I_16x16 = 2,  /**< Intra 16x16 */
    MB_P_16x16 = 3,  /**< P-skip / P 16x16 */
    MB_P_16x8  = 4,  /**< P 16x8 — two 16x8 partitions */
    MB_P_8x16  = 5,  /**< P 8x16 — two 8x16 partitions */
    MB_P_8x8   = 6,  /**< P 8x8 — four 8x8 sub-MBs */
    MB_B_16x16 = 7,  /**< B-skip / B Direct 16x16 */
    MB_B_16x8  = 8,  /**< B 16x8 */
    MB_B_8x16  = 9,  /**< B 8x16 */
    MB_B_8x8   = 10, /**< B 8x8 */
} mb_type_t;

/** Video frame dimensions */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mb_width;
    uint32_t mb_height;
    uint32_t crop_left;
    uint32_t crop_right;
    uint32_t crop_top;
    uint32_t crop_bottom;
} video_dimensions_t;

/** Pixel aspect ratio */
typedef struct {
    uint16_t sar_width;
    uint16_t sar_height;
} pixel_aspect_ratio_t;

/** Video codec parameters — complete stream configuration */
typedef struct {
    video_dimensions_t   dims;
    pixel_format_t       pix_fmt;
    video_color_spec_t   color;
    pixel_aspect_ratio_t par;
    gop_structure_t      gop;
    uint32_t             frame_rate_num;
    uint32_t             frame_rate_den;
    uint32_t             bitrate;
    uint32_t             vbv_bufsize;
    uint32_t             vbv_maxrate;
    uint8_t              profile_idc;
    uint8_t              level_idc;
    uint8_t              entropy_coding;
    uint8_t              transform_8x8;
    uint8_t              num_ref_frames;
} codec_video_params_t;

/** Single 2D block of pixel data (generic NxN block) */
typedef struct {
    uint32_t size;
    int16_t *coeffs;
    uint32_t stride;
} video_block_t;

/** A video plane (Y, Cb, or Cr component of a frame) */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t *data;
    uint8_t  owned;
} video_plane_t;

/** Complete video frame — planar YUV data */
typedef struct {
    uint32_t       frame_num;
    uint32_t       poc;
    uint32_t       width;
    uint32_t       height;
    pixel_format_t pix_fmt;
    slice_type_t   slice_type;
    video_plane_t  y;
    video_plane_t  cb;
    video_plane_t  cr;
    int64_t        pts;
    int64_t        dts;
    uint32_t       qp;
    uint8_t        key_frame;
    uint8_t        reference;
} video_frame_t;

/** Reference frame list (for motion compensation) */
typedef struct {
    video_frame_t *frames[16];
    uint32_t       num_frames;
    uint32_t       max_frames;
    int32_t        cur_poc;
} ref_frame_list_t;

/** NAL unit header — Network Abstraction Layer */
typedef struct {
    uint8_t forbidden_zero_bit;
    uint8_t nal_ref_idc;
    uint8_t nal_unit_type;
} nal_unit_header_t;

/** NAL unit types (selected — H.264 Table 7-1) */
typedef enum {
    NAL_SLICE_NON_IDR = 1,
    NAL_SLICE_IDR     = 5,
    NAL_SEI           = 6,
    NAL_SPS           = 7,
    NAL_PPS           = 8,
    NAL_AUD           = 9,
    NAL_FILLER        = 12,
} nal_unit_type_t;

/** Sequence Parameter Set (H.264 SPS) */
typedef struct {
    uint8_t  profile_idc;
    uint8_t  level_idc;
    uint32_t seq_parameter_set_id;
    uint32_t chroma_format_idc;
    uint32_t bit_depth_luma;
    uint32_t bit_depth_chroma;
    uint8_t  qpprime_y_zero_transform_bypass;
    uint8_t  seq_scaling_matrix_present;
    uint32_t log2_max_frame_num;
    uint32_t pic_order_cnt_type;
    uint32_t log2_max_pic_order_cnt_lsb;
    uint32_t max_num_ref_frames;
    uint32_t pic_width_in_mbs;
    uint32_t pic_height_in_map_units;
    uint32_t frame_mbs_only_flag;
    uint32_t mb_adaptive_frame_field_flag;
    uint8_t  direct_8x8_inference_flag;
    uint8_t  vui_parameters_present;
} sps_t;

/** Picture Parameter Set (H.264 PPS) */
typedef struct {
    uint32_t pic_parameter_set_id;
    uint32_t seq_parameter_set_id;
    uint8_t  entropy_coding_mode_flag;
    uint8_t  pic_order_present_flag;
    uint32_t num_slice_groups;
    uint32_t num_ref_idx_l0_active;
    int32_t  pic_init_qp;
    int32_t  pic_init_qs;
    int32_t  chroma_qp_index_offset;
    uint8_t  deblocking_filter_control;
    uint8_t  constrained_intra_pred;
    uint8_t  redundant_pic_cnt_present;
    uint8_t  transform_8x8_mode_flag;
    uint8_t  pic_scaling_matrix_present;
} pps_t;

/** Prediction weight for weighted prediction */
typedef struct {
    int32_t luma_weight;
    int32_t luma_offset;
    int32_t chroma_weight[2];
    int32_t chroma_offset[2];
} wp_weight_t;

/** Slice header — decoded from bitstream */
typedef struct {
    uint32_t    first_mb_in_slice;
    slice_type_t slice_type;
    uint32_t    pps_id;
    uint32_t    frame_num;
    uint8_t     field_pic_flag;
    uint8_t     bottom_field_flag;
    uint32_t    idr_pic_id;
    int32_t     pic_order_cnt_lsb;
    int32_t     delta_pic_order_cnt_bottom;
    int32_t     delta_pic_order_cnt[2];
    int32_t     slice_qp_delta;
    uint8_t     disable_deblocking_filter_idc;
    int32_t     slice_alpha_c0_offset;
    int32_t     slice_beta_offset;
    wp_weight_t wp_weights[2][32];
    uint32_t    num_ref_idx_active_override;
    uint32_t    cabac_init_idc;
    int32_t     slice_qs_delta;
    uint8_t     sp_for_switch_flag;
    uint32_t    slice_group_change_cycle;
} slice_header_t;

/** Rate-Distortion (RD) cost for mode decision */
typedef struct {
    double   distortion;
    uint64_t rate_bits;
    double   rd_cost;
    double   lambda;
} rd_cost_t;

/* ==========================================================================
 * L2: Core Concepts — Video Frame Management API
 * ========================================================================== */

void video_params_init(codec_video_params_t *params, uint32_t width,
                       uint32_t height, uint32_t fps_num, uint32_t fps_den);

int video_params_validate(const codec_video_params_t *params);

uint32_t video_frame_mb_count(const video_dimensions_t *dims);

void video_chroma_dims(const video_dimensions_t *dims,
                       chroma_subsampling_t cs,
                       uint32_t *cw_out, uint32_t *ch_out);

uint64_t video_frame_size_bytes(const video_dimensions_t *dims,
                                pixel_format_t fmt, uint32_t bit_depth);

uint64_t video_uncompressed_bitrate(const video_dimensions_t *dims,
                                    uint32_t fps_num, uint32_t fps_den,
                                    uint32_t bits_per_pixel);

double video_compression_ratio(const codec_video_params_t *params);

int video_frame_alloc(video_frame_t *frame, uint32_t width, uint32_t height,
                      pixel_format_t fmt);

void video_frame_free(video_frame_t *frame);

uint8_t *video_frame_luma_pixel(video_frame_t *frame, uint32_t x, uint32_t y);

uint8_t video_frame_get_luma(const video_frame_t *frame, int32_t x, int32_t y);

int video_frame_psnr(const video_frame_t *orig, const video_frame_t *decoded,
                     double *psnr_y, double *psnr_u, double *psnr_v);

double video_frame_ssim_luma(const video_frame_t *orig,
                             const video_frame_t *decoded);

void video_frame_clear(video_frame_t *frame, uint8_t luma_val);

int video_frame_copy(video_frame_t *dst, const video_frame_t *src);

uint64_t video_frame_sad_luma(const video_frame_t *a, const video_frame_t *b);

void ref_frame_list_init(ref_frame_list_t *list, uint32_t max_frames);

int ref_frame_list_add(ref_frame_list_t *list, video_frame_t *frame);

video_frame_t *ref_frame_list_find_closest(const ref_frame_list_t *list,
                                           int32_t poc);

void ref_frame_list_clear(ref_frame_list_t *list);

void gop_init_ippp(gop_structure_t *gop, uint32_t keyint);
void gop_init_ibbp(gop_structure_t *gop, uint32_t keyint, uint32_t num_b);
void gop_init_hierarchical(gop_structure_t *gop, uint32_t keyint,
                           uint32_t temporal_layers);
void gop_init_all_intra(gop_structure_t *gop, uint32_t keyint);
slice_type_t gop_get_pic_type(const gop_structure_t *gop,
                              uint32_t frame_in_gop);

void rd_cost_compute(rd_cost_t *cost, double distortion, uint64_t bits,
                     double lambda);
double rd_lambda_from_qp(uint32_t qp);

/* Utility functions */
int32_t iclip3(int32_t v, int32_t low, int32_t high);
double dclip3(double v, double low, double high);
int32_t median3(int32_t a, int32_t b, int32_t c);
uint32_t ceil_log2(uint32_t x);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_CODEC_H */

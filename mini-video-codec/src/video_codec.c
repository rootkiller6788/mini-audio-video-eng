/**
 * video_codec.c — Core Video Codec Implementation
 *
 * Implements frame management, PSNR/SSIM metrics, GOP logic, and parameter
 * initialization/validation. This is the foundational module that all other
 * codec components build upon.
 *
 * Knowledge coverage:
 *   L1: Frame/Pixel format definitions, GOP structures
 *   L2: Video frame management (alloc/free/copy/clear)
 *   L4: PSNR (derived from MSE = E[(x-y)^2])
 *   L6: GOP picture type determination
 */

#include "video_codec.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * L1/L2: Video Parameter Initialization and Validation
 * ========================================================================== */

void video_params_init(codec_video_params_t *params, uint32_t width,
                       uint32_t height, uint32_t fps_num, uint32_t fps_den)
{
    memset(params, 0, sizeof(*params));
    params->dims.width  = width;
    params->dims.height = height;
    params->dims.mb_width  = (width  + 15) / 16;
    params->dims.mb_height = (height + 15) / 16;
    params->dims.crop_left = params->dims.crop_right = 0;
    params->dims.crop_top  = params->dims.crop_bottom = 0;
    params->pix_fmt = PIX_FMT_YUV420P;
    params->color.primaries = COLOR_PRI_BT709;
    params->color.transfer  = TRANSFER_BT709;
    params->color.matrix    = MATRIX_BT709;
    params->color.full_range = 0;
    params->par.sar_width  = 1;
    params->par.sar_height = 1;
    params->frame_rate_num = fps_num;
    params->frame_rate_den = fps_den;
    params->bitrate     = 2000000;
    params->vbv_bufsize = 2000000;
    params->vbv_maxrate = 2000000;
    params->profile_idc = 66;
    params->level_idc   = 40;
    params->entropy_coding = 0;
    params->transform_8x8  = 0;
    params->num_ref_frames = 1;
    gop_init_ippp(&params->gop, 30);
}

int video_params_validate(const codec_video_params_t *params)
{
    if (!params) return -1;
    if (params->dims.width == 0 || params->dims.height == 0) return -2;
    if (params->dims.width > 7680 || params->dims.height > 4320) return -3;
    if (params->dims.width % 2 != 0 || params->dims.height % 2 != 0) return -4;
    if (params->frame_rate_num == 0 || params->frame_rate_den == 0) return -5;
    if (params->profile_idc < 66 || params->profile_idc > 244) return -6;
    if (params->level_idc < 10 || params->level_idc > 62) return -7;
    /* H.264 level 4.0 limits: max 8192 MBs, max 245760 samples/sec */
    uint32_t mb_count = params->dims.mb_width * params->dims.mb_height;
    int max_mbs = 8192;
    if (params->level_idc == 40) max_mbs = 8192;
    if (params->level_idc == 41) max_mbs = 8192;
    if (params->level_idc == 50) max_mbs = 22080;
    if (params->level_idc == 51) max_mbs = 36864;
    if (mb_count > (uint32_t)max_mbs) return -8;
    return 0;
}

uint32_t video_frame_mb_count(const video_dimensions_t *dims)
{
    if (!dims) return 0;
    return dims->mb_width * dims->mb_height;
}

void video_chroma_dims(const video_dimensions_t *dims,
                       chroma_subsampling_t cs,
                       uint32_t *cw_out, uint32_t *ch_out)
{
    if (!dims || !cw_out || !ch_out) return;
    switch (cs) {
        case CSF_YUV400:
            *cw_out = 0; *ch_out = 0; break;
        case CSF_YUV420:
        case CSF_NV12:
        case CSF_NV21:
            *cw_out = (dims->width  + 1) / 2;
            *ch_out = (dims->height + 1) / 2;
            break;
        case CSF_YUV422:
            *cw_out = (dims->width  + 1) / 2;
            *ch_out = dims->height;
            break;
        case CSF_YUV411:
            *cw_out = (dims->width  + 3) / 4;
            *ch_out = dims->height;
            break;
        case CSF_YUV444:
            *cw_out = dims->width;
            *ch_out = dims->height;
            break;
        default:
            *cw_out = 0; *ch_out = 0; break;
    }
}

uint64_t video_frame_size_bytes(const video_dimensions_t *dims,
                                pixel_format_t fmt, uint32_t bit_depth)
{
    if (!dims) return 0;
    uint32_t bytes_per_sample = (bit_depth + 7) / 8;
    uint64_t luma = (uint64_t)dims->width * dims->height * bytes_per_sample;
    switch (fmt) {
        case PIX_FMT_YUV420P:
        case PIX_FMT_NV12:
            return luma + luma / 2; /* 12 bpp */
        case PIX_FMT_YUV422P:
            return luma + luma / 2 + luma / 2; /* 16 bpp */
        case PIX_FMT_YUV444P:
            return luma * 3;
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
            return luma * 3;
        case PIX_FMT_RGBA32:
            return luma * 4;
        case PIX_FMT_YUV420P10LE:
            return (luma + luma / 2) * 2; /* 10-bit = 2 bytes per sample */
        default:
            return luma * 3;
    }
}

uint64_t video_uncompressed_bitrate(const video_dimensions_t *dims,
                                    uint32_t fps_num, uint32_t fps_den,
                                    uint32_t bits_per_pixel)
{
    if (!dims || fps_den == 0) return 0;
    return (uint64_t)dims->width * dims->height * fps_num / fps_den
           * bits_per_pixel;
}

double video_compression_ratio(const codec_video_params_t *params)
{
    if (!params || params->bitrate == 0) return 1.0;
    uint64_t uncomp_bps = video_uncompressed_bitrate(
        &params->dims, params->frame_rate_num, params->frame_rate_den, 12);
    if (uncomp_bps == 0) return 1.0;
    return (double)uncomp_bps / (double)params->bitrate;
}

/* ==========================================================================
 * L2: Video Frame Management
 * ========================================================================== */

int video_frame_alloc(video_frame_t *frame, uint32_t width, uint32_t height,
                      pixel_format_t fmt)
{
    if (!frame) return -1;
    memset(frame, 0, sizeof(*frame));
    frame->width  = width;
    frame->height = height;
    frame->pix_fmt = fmt;

    /* Luma plane */
    frame->y.width  = width;
    frame->y.height = height;
    frame->y.stride = width;
    frame->y.data   = (uint8_t *)calloc(1, (size_t)width * height);
    if (!frame->y.data) { video_frame_free(frame); return -1; }
    frame->y.owned = 1;

    /* Chroma planes based on format */
    uint32_t cw = 0, ch = 0;
    video_dimensions_t dims;
    dims.width = width; dims.height = height;
    video_chroma_dims(&dims, CSF_YUV420, &cw, &ch);

    if (fmt == PIX_FMT_YUV420P || fmt == PIX_FMT_YUV422P ||
        fmt == PIX_FMT_YUV444P) {
        if (fmt == PIX_FMT_YUV422P) { ch = height; }
        if (fmt == PIX_FMT_YUV444P) { cw = width; ch = height; }
        frame->cb.width  = cw;
        frame->cb.height = ch;
        frame->cb.stride = cw;
        frame->cb.data   = (uint8_t *)calloc(1, (size_t)cw * ch);
        if (!frame->cb.data) { video_frame_free(frame); return -1; }
        frame->cb.owned = 1;
        frame->cr.width  = cw;
        frame->cr.height = ch;
        frame->cr.stride = cw;
        frame->cr.data   = (uint8_t *)calloc(1, (size_t)cw * ch);
        if (!frame->cr.data) { video_frame_free(frame); return -1; }
        frame->cr.owned = 1;
    }
    return 0;
}

void video_frame_free(video_frame_t *frame)
{
    if (!frame) return;
    if (frame->y.owned && frame->y.data) { free(frame->y.data); frame->y.data = NULL; }
    if (frame->cb.owned && frame->cb.data) { free(frame->cb.data); frame->cb.data = NULL; }
    if (frame->cr.owned && frame->cr.data) { free(frame->cr.data); frame->cr.data = NULL; }
    memset(frame, 0, sizeof(*frame));
}

uint8_t *video_frame_luma_pixel(video_frame_t *frame, uint32_t x, uint32_t y)
{
    if (!frame || !frame->y.data) return NULL;
    if (x >= frame->width || y >= frame->height) return NULL;
    return &frame->y.data[y * frame->y.stride + x];
}

uint8_t video_frame_get_luma(const video_frame_t *frame, int32_t x, int32_t y)
{
    if (!frame || !frame->y.data) return 0;
    /* Clip to frame boundaries (edge replication) */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= frame->width)  x = (int32_t)frame->width - 1;
    if ((uint32_t)y >= frame->height) y = (int32_t)frame->height - 1;
    return frame->y.data[y * frame->y.stride + x];
}

int video_frame_psnr(const video_frame_t *orig, const video_frame_t *decoded,
                     double *psnr_y, double *psnr_u, double *psnr_v)
{
    if (!orig || !decoded || !psnr_y || !psnr_u || !psnr_v) return -1;
    if (orig->width != decoded->width || orig->height != decoded->height)
        return -1;
    if (!orig->y.data || !decoded->y.data) return -1;

    double mse_y = 0.0, mse_u = 0.0, mse_v = 0.0;
    uint64_t n_y  = (uint64_t)orig->y.width  * orig->y.height;
    uint64_t n_cb = (uint64_t)orig->cb.width * orig->cb.height;
    uint64_t n_cr = (uint64_t)orig->cr.width * orig->cr.height;

    for (uint64_t i = 0; i < n_y; i++) {
        double d = (double)orig->y.data[i] - (double)decoded->y.data[i];
        mse_y += d * d;
    }
    mse_y /= (double)n_y;
    *psnr_y = (mse_y > 0.0) ? 10.0 * log10(65025.0 / mse_y) : 100.0;

    if (orig->cb.data && decoded->cb.data) {
        for (uint64_t i = 0; i < n_cb; i++) {
            double d = (double)orig->cb.data[i] - (double)decoded->cb.data[i];
            mse_u += d * d;
        }
        mse_u /= (double)n_cb;
        *psnr_u = (mse_u > 0.0) ? 10.0 * log10(65025.0 / mse_u) : 100.0;
    } else {
        *psnr_u = 100.0;
    }

    if (orig->cr.data && decoded->cr.data) {
        for (uint64_t i = 0; i < n_cr; i++) {
            double d = (double)orig->cr.data[i] - (double)decoded->cr.data[i];
            mse_v += d * d;
        }
        mse_v /= (double)n_cr;
        *psnr_v = (mse_v > 0.0) ? 10.0 * log10(65025.0 / mse_v) : 100.0;
    } else {
        *psnr_v = 100.0;
    }
    return 0;
}

double video_frame_ssim_luma(const video_frame_t *orig,
                             const video_frame_t *decoded)
{
    if (!orig || !decoded) return 0.0;
    if (orig->width != decoded->width || orig->height != decoded->height)
        return 0.0;
    const double C1 = 6.5025;  /* (0.01 * 255)^2 */
    const double C2 = 58.5225; /* (0.03 * 255)^2 */
    uint32_t w = orig->width, h = orig->height;
    double ssim_sum = 0.0;
    uint32_t num_blocks = 0;

    for (uint32_t by = 0; by + 8 <= h; by += 8) {
        for (uint32_t bx = 0; bx + 8 <= w; bx += 8) {
            double sum_x = 0, sum_y = 0, sum_xx = 0, sum_yy = 0, sum_xy = 0;
            uint32_t n = 0;
            for (uint32_t dy = 0; dy < 8; dy++) {
                for (uint32_t dx = 0; dx < 8; dx++) {
                    double x = (double)orig->y.data[(by+dy)*orig->y.stride + (bx+dx)];
                    double y = (double)decoded->y.data[(by+dy)*decoded->y.stride + (bx+dx)];
                    sum_x  += x;  sum_y  += y;
                    sum_xx += x*x; sum_yy += y*y;
                    sum_xy += x*y;
                    n++;
                }
            }
            double mu_x = sum_x / n, mu_y = sum_y / n;
            double var_x = sum_xx / n - mu_x * mu_x;
            double var_y = sum_yy / n - mu_y * mu_y;
            double cov   = sum_xy / n - mu_x * mu_y;
            double num = (2.0 * mu_x * mu_y + C1) * (2.0 * cov + C2);
            double den = (mu_x*mu_x + mu_y*mu_y + C1) * (var_x + var_y + C2);
            ssim_sum += (den > 0) ? num / den : 1.0;
            num_blocks++;
        }
    }
    return (num_blocks > 0) ? ssim_sum / num_blocks : 0.0;
}

void video_frame_clear(video_frame_t *frame, uint8_t luma_val)
{
    if (!frame || !frame->y.data) return;
    memset(frame->y.data, luma_val,
           (size_t)frame->y.width * frame->y.height);
    if (frame->cb.data)
        memset(frame->cb.data, 128,
               (size_t)frame->cb.width * frame->cb.height);
    if (frame->cr.data)
        memset(frame->cr.data, 128,
               (size_t)frame->cr.width * frame->cr.height);
}

int video_frame_copy(video_frame_t *dst, const video_frame_t *src)
{
    if (!dst || !src) return -1;
    if (dst->width != src->width || dst->height != src->height) return -1;
    if (dst->pix_fmt != src->pix_fmt) return -1;
    if (!dst->y.data || !src->y.data) return -1;

    memcpy(dst->y.data, src->y.data,
           (size_t)dst->y.width * dst->y.height);
    if (dst->cb.data && src->cb.data)
        memcpy(dst->cb.data, src->cb.data,
               (size_t)dst->cb.width * dst->cb.height);
    if (dst->cr.data && src->cr.data)
        memcpy(dst->cr.data, src->cr.data,
               (size_t)dst->cr.width * dst->cr.height);
    dst->frame_num  = src->frame_num;
    dst->poc        = src->poc;
    dst->slice_type = src->slice_type;
    dst->qp         = src->qp;
    dst->key_frame  = src->key_frame;
    dst->reference  = src->reference;
    return 0;
}

uint64_t video_frame_sad_luma(const video_frame_t *a, const video_frame_t *b)
{
    if (!a || !b || !a->y.data || !b->y.data) return 0;
    uint32_t w = a->width, h = a->height;
    if (w != b->width || h != b->height) return 0;
    uint64_t sad = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            int d = (int)a->y.data[y * a->y.stride + x]
                  - (int)b->y.data[y * b->y.stride + x];
            sad += (uint64_t)(d < 0 ? -d : d);
        }
    }
    return sad;
}

/* ==========================================================================
 * L2: Reference Frame List Management
 * ========================================================================== */

void ref_frame_list_init(ref_frame_list_t *list, uint32_t max_frames)
{
    if (!list) return;
    memset(list, 0, sizeof(*list));
    list->max_frames = (max_frames > 16) ? 16 : max_frames;
}

int ref_frame_list_add(ref_frame_list_t *list, video_frame_t *frame)
{
    if (!list || !frame) return -1;
    if (list->num_frames >= list->max_frames) return -1;
    /* Shift frames to make room (FIFO-style reference management) */
    if (list->num_frames > 0) {
        for (int i = (int)list->num_frames; i > 0; i--)
            list->frames[i] = list->frames[i - 1];
    }
    list->frames[0] = frame;
    list->num_frames++;
    return 0;
}

video_frame_t *ref_frame_list_find_closest(const ref_frame_list_t *list,
                                           int32_t poc)
{
    if (!list || list->num_frames == 0) return NULL;
    video_frame_t *best = NULL;
    int32_t best_dist = INT32_MAX;
    for (uint32_t i = 0; i < list->num_frames; i++) {
        if (!list->frames[i]) continue;
        int32_t dist = poc - (int32_t)list->frames[i]->poc;
        if (dist < 0) dist = -dist;
        if (dist < best_dist) {
            best_dist = dist;
            best = list->frames[i];
        }
    }
    return best;
}

void ref_frame_list_clear(ref_frame_list_t *list)
{
    if (!list) return;
    list->num_frames = 0;
}

/* ==========================================================================
 * L6: GOP Structure Functions
 * ========================================================================== */

void gop_init_ippp(gop_structure_t *gop, uint32_t keyint)
{
    if (!gop) return;
    memset(gop, 0, sizeof(*gop));
    gop->gop_size    = keyint;
    gop->keyint_max  = keyint;
    gop->idr_period  = keyint;
    gop->num_b_frames = 0;
    gop->open_gop     = 0;
    gop->hierarchical_b = 0;
}

void gop_init_ibbp(gop_structure_t *gop, uint32_t keyint, uint32_t num_b)
{
    if (!gop) return;
    memset(gop, 0, sizeof(*gop));
    gop->gop_size     = keyint;
    gop->keyint_max   = keyint;
    gop->idr_period   = keyint;
    gop->num_b_frames = num_b;
    gop->open_gop     = 0;
    gop->hierarchical_b = 0;
}

void gop_init_hierarchical(gop_structure_t *gop, uint32_t keyint,
                           uint32_t temporal_layers)
{
    if (!gop) return;
    memset(gop, 0, sizeof(*gop));
    gop->gop_size       = keyint;
    gop->keyint_max     = keyint;
    gop->idr_period     = keyint;
    gop->num_b_frames   = (temporal_layers > 1) ? temporal_layers - 1 : 0;
    gop->open_gop       = 0;
    gop->hierarchical_b = (temporal_layers > 1) ? 1 : 0;
}

void gop_init_all_intra(gop_structure_t *gop, uint32_t keyint)
{
    if (!gop) return;
    memset(gop, 0, sizeof(*gop));
    gop->gop_size     = 1;
    gop->keyint_max   = 1;
    gop->idr_period   = 1;
    gop->num_b_frames = 0;
    gop->open_gop     = 0;
    gop->hierarchical_b = 0;
    (void)keyint;
}

slice_type_t gop_get_pic_type(const gop_structure_t *gop, uint32_t frame_in_gop)
{
    if (!gop) return SLICE_P;
    if (gop->gop_size == 1) return SLICE_I;  /* All-Intra */
    if (frame_in_gop == 0) return SLICE_I;    /* Keyframe */
    if (gop->num_b_frames == 0) return SLICE_P; /* IPPP */
    /* IBBP: determine if this is a B-frame */
    uint32_t pos = (frame_in_gop - 1) % (gop->num_b_frames + 1);
    if (pos < gop->num_b_frames) return SLICE_B;
    return SLICE_P;
}

/* ==========================================================================
 * L2: Rate-Distortion Cost
 * ========================================================================== */

void rd_cost_compute(rd_cost_t *cost, double distortion, uint64_t bits,
                     double lambda)
{
    if (!cost) return;
    cost->distortion = distortion;
    cost->rate_bits  = bits;
    cost->lambda     = lambda;
    cost->rd_cost    = distortion + lambda * (double)bits;
}

double rd_lambda_from_qp(uint32_t qp)
{
    /* H.264 JM reference formula */
    double qp_d = (double)qp;
    return 0.85 * pow(2.0, (qp_d - 12.0) / 3.0);
}

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

int32_t iclip3(int32_t v, int32_t low, int32_t high)
{
    if (v < low)  return low;
    if (v > high) return high;
    return v;
}

double dclip3(double v, double low, double high)
{
    if (v < low)  return low;
    if (v > high) return high;
    return v;
}

int32_t median3(int32_t a, int32_t b, int32_t c)
{
    int32_t t;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) { b = c; }
    if (a > b) { b = a; }
    return b;
}

uint32_t ceil_log2(uint32_t x)
{
    if (x == 0) return 0;
    uint32_t result = 0;
    uint32_t v = x - 1;
    while (v > 0) { v >>= 1; result++; }
    return result;
}

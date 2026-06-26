/**
 * color_space.c — Color Space Conversion (YUV <-> RGB)
 *
 * Implements ITU-R BT.601 and BT.709 YCbCr/RGB conversions,
 * limited/full range handling, and chroma subsampling operations.
 *
 * Knowledge coverage:
 *   L1: Color space matrices (BT.601, BT.709, BT.2020)
 *   L2: YUV <-> RGB conversion, limited/full range
 *   L5: Chroma subsampling/upsampling
 *
 * Reference:
 *   ITU-R BT.601 — Studio encoding parameters of digital television
 *     for standard 4:3 and wide-screen 16:9 aspect ratios
 *   ITU-R BT.709 — Parameter values for the HDTV standards
 *   Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
 */

#include "video_codec.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

/* ==========================================================================
 * L2: YUV <-> RGB Conversion (BT.601 SD)
 * ========================================================================== */

/**
 * BT.601 YCbCr -> RGB (limited range: Y 16-235, CbCr 16-240)
 *
 * R = 1.164*(Y-16) + 1.596*(Cr-128)
 * G = 1.164*(Y-16) - 0.813*(Cr-128) - 0.392*(Cb-128)
 * B = 1.164*(Y-16) + 2.017*(Cb-128)
 */
void yuv_to_rgb_bt601_limited(uint8_t y, uint8_t cb, uint8_t cr,
                               uint8_t *r, uint8_t *g, uint8_t *b)
{
    int yi  = y - 16;
    int cbi = cb - 128;
    int cri = cr - 128;
    int ri = (int)(1.164 * yi + 1.596 * cri + 0.5);
    int gi = (int)(1.164 * yi - 0.813 * cri - 0.392 * cbi + 0.5);
    int bi = (int)(1.164 * yi + 2.017 * cbi + 0.5);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    *r = (uint8_t)ri; *g = (uint8_t)gi; *b = (uint8_t)bi;
}

/**
 * BT.601 RGB -> YCbCr (limited range)
 *
 * Y  =  0.257*R + 0.504*G + 0.098*B + 16
 * Cb = -0.148*R - 0.291*G + 0.439*B + 128
 * Cr =  0.439*R - 0.368*G - 0.071*B + 128
 */
void rgb_to_yuv_bt601_limited(uint8_t r, uint8_t g, uint8_t b,
                               uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    int ri = r, gi = g, bi = b;
    int yi  = (int)( 0.257*ri + 0.504*gi + 0.098*bi + 0.5) + 16;
    int cbi = (int)(-0.148*ri - 0.291*gi + 0.439*bi + 0.5) + 128;
    int cri = (int)( 0.439*ri - 0.368*gi - 0.071*bi + 0.5) + 128;
    if (yi  < 0) yi  = 0; else if (yi  > 255) yi  = 255;
    if (cbi < 0) cbi = 0; else if (cbi > 255) cbi = 255;
    if (cri < 0) cri = 0; else if (cri > 255) cri = 255;
    *y = (uint8_t)yi; *cb = (uint8_t)cbi; *cr = (uint8_t)cri;
}

/* ==========================================================================
 * L2: YUV <-> RGB Conversion (BT.709 HD)
 * ========================================================================== */

/**
 * BT.709 YCbCr -> RGB (limited range)
 *
 * R = 1.164*(Y-16) + 1.793*(Cr-128)
 * G = 1.164*(Y-16) - 0.534*(Cr-128) - 0.213*(Cb-128)
 * B = 1.164*(Y-16) + 2.115*(Cb-128)
 */
void yuv_to_rgb_bt709_limited(uint8_t y, uint8_t cb, uint8_t cr,
                               uint8_t *r, uint8_t *g, uint8_t *b)
{
    int yi  = y - 16;
    int cbi = cb - 128;
    int cri = cr - 128;
    int ri = (int)(1.164 * yi + 1.793 * cri + 0.5);
    int gi = (int)(1.164 * yi - 0.534 * cri - 0.213 * cbi + 0.5);
    int bi = (int)(1.164 * yi + 2.115 * cbi + 0.5);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    *r = (uint8_t)ri; *g = (uint8_t)gi; *b = (uint8_t)bi;
}

/**
 * BT.709 RGB -> YCbCr (limited range)
 */
void rgb_to_yuv_bt709_limited(uint8_t r, uint8_t g, uint8_t b,
                               uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    int ri = r, gi = g, bi = b;
    int yi  = (int)( 0.213*ri + 0.715*gi + 0.072*bi + 0.5) + 16;
    int cbi = (int)(-0.115*ri - 0.385*gi + 0.500*bi + 0.5) + 128;
    int cri = (int)( 0.500*ri - 0.454*gi - 0.046*bi + 0.5) + 128;
    if (yi  < 0) yi  = 0; else if (yi  > 255) yi  = 255;
    if (cbi < 0) cbi = 0; else if (cbi > 255) cbi = 255;
    if (cri < 0) cri = 0; else if (cri > 255) cri = 255;
    *y = (uint8_t)yi; *cb = (uint8_t)cbi; *cr = (uint8_t)cri;
}

/* ==========================================================================
 * L2: Full Range YUV <-> RGB (JPEG/JFIF style)
 * ========================================================================== */

void yuv_to_rgb_full_range(uint8_t y, uint8_t cb, uint8_t cr,
                            uint8_t *r, uint8_t *g, uint8_t *b)
{
    int yi = y, cbi = cb - 128, cri = cr - 128;
    int ri = yi + ((1402 * cri) >> 10);
    int gi = yi - ((344 * cbi + 714 * cri) >> 10);
    int bi = yi + ((1772 * cbi) >> 10);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    *r = (uint8_t)ri; *g = (uint8_t)gi; *b = (uint8_t)bi;
}

void rgb_to_yuv_full_range(uint8_t r, uint8_t g, uint8_t b,
                            uint8_t *y, uint8_t *cb, uint8_t *cr)
{
    int ri = r, gi = g, bi = b;
    int yi  = (( 19595*ri + 38470*gi +  7471*bi) >> 16);
    int cbi = ((-11056*ri - 21712*gi + 32768*bi) >> 16) + 128;
    int cri = (( 32768*ri - 27440*gi -  5328*bi) >> 16) + 128;
    if (yi  < 0) yi  = 0; else if (yi  > 255) yi  = 255;
    if (cbi < 0) cbi = 0; else if (cbi > 255) cbi = 255;
    if (cri < 0) cri = 0; else if (cri > 255) cri = 255;
    *y = (uint8_t)yi; *cb = (uint8_t)cbi; *cr = (uint8_t)cri;
}

/* ==========================================================================
 * L2: RGB24 Packed -> Planar YUV420 Conversion
 * ========================================================================== */

/**
 * Convert packed RGB24 frame to planar YUV 4:2:0.
 *
 * Steps:
 *   1. RGB -> YUV (per pixel)
 *   2. Chroma subsampling: average 2x2 Cb/Cr blocks to single sample
 */
void rgb24_to_yuv420p(const uint8_t *rgb, uint32_t width, uint32_t height,
                      uint8_t *y_out, uint8_t *cb_out, uint8_t *cr_out)
{
    if (!rgb || !y_out || !cb_out || !cr_out) return;
    /* RGB to YUV (full range for simplicity) */
    for (uint32_t py = 0; py < height; py++) {
        for (uint32_t px = 0; px < width; px++) {
            uint32_t idx = (py * width + px) * 3;
            uint8_t rv = rgb[idx], gv = rgb[idx+1], bv = rgb[idx+2];
            uint8_t yv, cbv, crv;
            rgb_to_yuv_full_range(rv, gv, bv, &yv, &cbv, &crv);
            y_out[py * width + px] = yv;
            /* Store full chroma temporarily (will subsample below) */
        }
    }
    /* Chroma subsampling 4:2:0: average 2x2 blocks */
    for (uint32_t py = 0; py < height; py += 2) {
        for (uint32_t px = 0; px < width; px += 2) {
            int sum_cb = 0, sum_cr = 0, cnt = 0;
            for (uint32_t dy = 0; dy < 2 && py+dy < height; dy++) {
                for (uint32_t dx = 0; dx < 2 && px+dx < width; dx++) {
                    uint32_t idx = ((py+dy) * width + (px+dx)) * 3;
                    uint8_t rv = rgb[idx], gv = rgb[idx+1], bv = rgb[idx+2];
                    uint8_t yv, cbv, crv;
                    rgb_to_yuv_full_range(rv, gv, bv, &yv, &cbv, &crv);
                    sum_cb += cbv; sum_cr += crv; cnt++;
                    /* Write luma if not already set */
                    if (dy == 0 || dx == 0 || cnt == 4) continue;
                }
            }
            uint32_t cx = px / 2, cy = py / 2;
            uint32_t cw = (width + 1) / 2;
            cb_out[cy * cw + cx] = (uint8_t)((sum_cb + cnt/2) / cnt);
            cr_out[cy * cw + cx] = (uint8_t)((sum_cr + cnt/2) / cnt);
        }
    }
}

/**
 * Convert planar YUV 4:2:0 to packed RGB24.
 *
 * Uses nearest-neighbor chroma upsampling.
 */
void yuv420p_to_rgb24(const uint8_t *y_in, const uint8_t *cb_in,
                      const uint8_t *cr_in,
                      uint32_t width, uint32_t height,
                      uint8_t *rgb_out)
{
    if (!y_in || !cb_in || !cr_in || !rgb_out) return;
    uint32_t cw = (width + 1) / 2;
    for (uint32_t py = 0; py < height; py++) {
        for (uint32_t px = 0; px < width; px++) {
            uint32_t cx = px / 2, cy = py / 2;
            uint8_t yv = y_in[py * width + px];
            uint8_t cbv = cb_in[cy * cw + cx];
            uint8_t crv = cr_in[cy * cw + cx];
            uint8_t rv, gv, bv;
            yuv_to_rgb_full_range(yv, cbv, crv, &rv, &gv, &bv);
            uint32_t idx = (py * width + px) * 3;
            rgb_out[idx]   = rv;
            rgb_out[idx+1] = gv;
            rgb_out[idx+2] = bv;
        }
    }
}

/* ==========================================================================
 * L2: Chroma Upsampling (Nearest-Neighbor and Bilinear)
 * ========================================================================== */

/**
 * Nearest-neighbor chroma upsampling from 4:2:0 to 4:4:4.
 */
void chroma_upsample_nearest(const uint8_t *src, uint32_t src_w,
                             uint32_t src_h, uint8_t *dst)
{
    if (!src || !dst) return;
    for (uint32_t y = 0; y < src_h * 2; y++) {
        for (uint32_t x = 0; x < src_w * 2; x++) {
            dst[y * src_w * 2 + x] = src[(y/2) * src_w + (x/2)];
        }
    }
}

/**
 * Bilinear chroma upsampling from 4:2:0 to 4:4:4.
 */
void chroma_upsample_bilinear(const uint8_t *src, uint32_t src_w,
                               uint32_t src_h, uint8_t *dst)
{
    if (!src || !dst) return;
    for (uint32_t y = 0; y < src_h * 2; y++) {
        for (uint32_t x = 0; x < src_w * 2; x++) {
            int sx = (int)x / 2;
            int sy = (int)y / 2;
            int fx = x & 1, fy = y & 1;
            int sx1 = (sx + 1 < (int)src_w) ? sx + 1 : sx;
            int sy1 = (sy + 1 < (int)src_h) ? sy + 1 : sy;
            int a = src[sy  * src_w + sx];
            int b = src[sy  * src_w + sx1];
            int c = src[sy1 * src_w + sx];
            int d = src[sy1 * src_w + sx1];
            int val = ((2-fx)*(2-fy)*a + fx*(2-fy)*b + (2-fx)*fy*c + fx*fy*d + 2) >> 2;
            if (val > 255) val = 255;
            dst[y * src_w * 2 + x] = (uint8_t)val;
        }
    }
}

/* ==========================================================================
 * L2: Gamma Transfer Function Application
 * ========================================================================== */

/**
 * Apply gamma correction (power law) to a single component.
 * out = 255 * (in/255)^(1/gamma)
 */
uint8_t apply_gamma(uint8_t value, double gamma)
{
    if (gamma <= 0.0) return value;
    double normalized = (double)value / 255.0;
    double corrected  = pow(normalized, 1.0 / gamma);
    int out = (int)(corrected * 255.0 + 0.5);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return (uint8_t)out;
}

/**
 * Remove gamma (linearize) a single component.
 * out = 255 * (in/255)^gamma
 */
uint8_t remove_gamma(uint8_t value, double gamma)
{
    if (gamma <= 0.0) return value;
    double normalized = (double)value / 255.0;
    double linear     = pow(normalized, gamma);
    int out = (int)(linear * 255.0 + 0.5);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return (uint8_t)out;
}

/**
 * Apply BT.709 transfer function (approximate gamma 2.2 with linear segment).
 */
uint8_t bt709_oetf(double linear)
{
    /* BT.709 OETF:  V = 4.5*L for L<0.018,  V = 1.099*L^0.45 - 0.099 for L>=0.018 */
    double v;
    if (linear < 0.018)
        v = 4.5 * linear;
    else
        v = 1.099 * pow(linear, 0.45) - 0.099;
    int out = (int)(v * 255.0 + 0.5);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return (uint8_t)out;
}

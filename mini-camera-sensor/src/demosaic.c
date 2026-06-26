/**
 * @file demosaic.c
 * @brief Bayer demosaicing algorithms implementation
 *
 * Implements L5 algorithms:
 *   - Bilinear interpolation (simplest, ~30-35 dB PSNR)
 *   - Malvar-He-Cutler (2004) high-quality linear (~37-41 dB)
 *   - Gradient-corrected bilinear (edge-aware, ~35-39 dB)
 *   - Adaptive Homogeneity-Directed (Hirakawa & Parks 2005, ~38-43 dB)
 *
 * L6: Quality assessment (PSNR, SSIM, delta-E)
 *
 * Reference: Malvar et al. IEEE ICASSP 2004;
 *            Hirakawa & Parks IEEE T-IP 2005;
 *            Gunturk et al. IEEE SPM 2005
 */
#include "demosaic.h"

/*===========================================================================
 * Memory management
 *===========================================================================*/

rgb_image_planar_t *rgb_planar_alloc(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return NULL;
    rgb_image_planar_t *img = (rgb_image_planar_t *)malloc(sizeof(*img));
    if (img == NULL) return NULL;

    size_t n = (size_t)w * h;
    img->r = (uint16_t *)calloc(n, sizeof(uint16_t));
    img->g = (uint16_t *)calloc(n, sizeof(uint16_t));
    img->b = (uint16_t *)calloc(n, sizeof(uint16_t));

    if (img->r == NULL || img->g == NULL || img->b == NULL) {
        free(img->r); free(img->g); free(img->b);
        free(img);
        return NULL;
    }

    img->width = w;
    img->height = h;
    return img;
}

void rgb_planar_free(rgb_image_planar_t *img)
{
    if (img == NULL) return;
    free(img->r);
    free(img->g);
    free(img->b);
    free(img);
}

/*===========================================================================
 * L5: Bilinear demosaicing
 *
 * Simplest approach:
 *   Green at R/B: average of 4 adjacent G neighbors (N, S, E, W)
 *   R at G (on R row): average of horizontal R neighbors
 *   R at B: average of 4 diagonal R neighbors
 *   Same for B.
 *
 * Artifacts: zippering along edges, false colors near high frequencies.
 *===========================================================================*/

int demosaic_bilinear(const raw_frame_t *raw, rgb_image_planar_t *rgb)
{
    if (raw == NULL || rgb == NULL) return -1;
    if (raw->width < 3 || raw->height < 3) return -1;

    uint32_t w = raw->width, h = raw->height;
    uint32_t x, y;

    /* Copy known values from Bayer pattern */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            pixel_raw_t v = raw->data[y * raw->stride + x];
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c == BAYER_COLOR_R) {
                rgb->r[y*w+x] = v;
            } else if (c == BAYER_COLOR_GR || c == BAYER_COLOR_GB) {
                rgb->g[y*w+x] = v;
            } else { /* B */
                rgb->b[y*w+x] = v;
            }
        }
    }

    /* Interpolate green at red/blue pixels */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c == BAYER_COLOR_R || c == BAYER_COLOR_B) {
                uint32_t sum = 0, cnt = 0;
                if (y > 0) { sum += raw->data[(y-1)*raw->stride + x]; cnt++; }
                if (y < h-1) { sum += raw->data[(y+1)*raw->stride + x]; cnt++; }
                if (x > 0) { sum += raw->data[y*raw->stride + x-1]; cnt++; }
                if (x < w-1) { sum += raw->data[y*raw->stride + x+1]; cnt++; }
                rgb->g[y*w+x] = (cnt > 0) ? (uint16_t)((sum + cnt/2) / cnt) : 0;
            }
        }
    }

    /* Interpolate red at green/blue pixels */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c != BAYER_COLOR_R) {
                uint32_t sum = 0, cnt = 0;
                if (c == BAYER_COLOR_B) {
                    /* R at B: average of 4 diagonal R neighbors */
                    if (y>0 && x>0) { sum += raw->data[(y-1)*raw->stride + x-1]; cnt++; }
                    if (y>0 && x<w-1) { sum += raw->data[(y-1)*raw->stride + x+1]; cnt++; }
                    if (y<h-1 && x>0) { sum += raw->data[(y+1)*raw->stride + x-1]; cnt++; }
                    if (y<h-1 && x<w-1) { sum += raw->data[(y+1)*raw->stride + x+1]; cnt++; }
                } else {
                    /* R at G: horizontal or vertical neighbors */
                    if (x > 0) { sum += raw->data[y*raw->stride + x-1]; cnt++; }
                    if (x < w-1) { sum += raw->data[y*raw->stride + x+1]; cnt++; }
                }
                rgb->r[y*w+x] = (cnt > 0) ? (uint16_t)((sum + cnt/2) / cnt) : 0;
            }
        }
    }

    /* Interpolate blue at red/green pixels */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c != BAYER_COLOR_B) {
                uint32_t sum = 0, cnt = 0;
                if (c == BAYER_COLOR_R) {
                    if (y>0 && x>0) { sum += raw->data[(y-1)*raw->stride + x-1]; cnt++; }
                    if (y>0 && x<w-1) { sum += raw->data[(y-1)*raw->stride + x+1]; cnt++; }
                    if (y<h-1 && x>0) { sum += raw->data[(y+1)*raw->stride + x-1]; cnt++; }
                    if (y<h-1 && x<w-1) { sum += raw->data[(y+1)*raw->stride + x+1]; cnt++; }
                } else {
                    if (y > 0) { sum += raw->data[(y-1)*raw->stride + x]; cnt++; }
                    if (y < h-1) { sum += raw->data[(y+1)*raw->stride + x]; cnt++; }
                }
                rgb->b[y*w+x] = (cnt > 0) ? (uint16_t)((sum + cnt/2) / cnt) : 0;
            }
        }
    }

    return 0;
}

/*===========================================================================
 * L5: Malvar-He-Cutler demosaicing (2004)
 *
 * Uses 5x5 linear filter kernels with Laplacian second-order correction.
 * Kernels designed to minimize mean squared error for typical natural images.
 *
 * Green at R/B:
 *   G = sum of 4 neighbor G * 1/4
 *       + Laplacian correction from center pixel
 *
 * The key innovation is the Laplacian correction term that reduces
 * zippering without explicit edge detection.
 *
 * Kernels from Malvar et al. Table 1 (optimized for PSNR).
 *===========================================================================*/

/** MHC kernel weights for green at red pixel (RGGB pattern) */
static const int16_t MHC_G_AT_R[25] = {
     0,  0, -1,  0,  0,
     0,  0,  2,  0,  0,
    -1,  2,  4,  2, -1,
     0,  0,  2,  0,  0,
     0,  0, -1,  0,  0
};

/** MHC kernel weights for green at blue pixel */
static const int16_t MHC_G_AT_B[25] = {
     0,  0, -1,  0,  0,
     0,  0,  2,  0,  0,
    -1,  2,  4,  2, -1,
     0,  0,  2,  0,  0,
     0,  0, -1,  0,  0
};

/** MHC kernel for red at green (on red row) */
static const int16_t MHC_R_AT_GR[25] = {
     0,  0,  1,  0,  0,
     0, -1,  0, -1,  0,
    -1,  4,  5,  4, -1,
     0, -1,  0, -1,  0,
     0,  0,  1,  0,  0
};

/** MHC kernel for red at blue */
static const int16_t MHC_R_AT_B[25] = {
     0,  0, -3,  0,  0,
     0,  4,  0,  4,  0,
    -3,  0, 12,  0, -3,
     0,  4,  0,  4,  0,
     0,  0, -3,  0,  0
};

/** MHC kernel for blue at red — same as R at B by symmetry */
#define MHC_B_AT_R MHC_R_AT_B

/** MHC kernel for blue at green (on blue row) — symmetric to R at GR */
static const int16_t MHC_B_AT_GB[25] = {
     0,  0,  1,  0,  0,
     0, -1,  0, -1,  0,
    -1,  4,  5,  4, -1,
     0, -1,  0, -1,  0,
     0,  0,  1,  0,  0
};

/** Apply a 5x5 kernel to Bayer data, output scaled by 1/divisor */
static uint16_t apply_5x5_kernel(const raw_frame_t *raw, uint32_t cx,
                                  uint32_t cy, const int16_t kernel[25],
                                  int32_t divisor)
{
    if (divisor <= 0) divisor = 1;
    int32_t sum = 0;
    int32_t sy, sx;
    for (sy = -2; sy <= 2; sy++) {
        for (sx = -2; sx <= 2; sx++) {
            int32_t nx = (int32_t)cx + sx;
            int32_t ny = (int32_t)cy + sy;
            int32_t k = kernel[(sy+2)*5 + (sx+2)];
            if (nx >= 0 && ny >= 0 && nx < (int32_t)raw->width &&
                ny < (int32_t)raw->height) {
                sum += (int32_t)raw->data[(uint32_t)ny*raw->stride+(uint32_t)nx]*k;
            }
        }
    }
    int32_t result = (sum + divisor/2) / divisor;
    if (result < 0) result = 0;
    if (result > 65535) result = 65535;
    return (uint16_t)result;
}

int demosaic_mhc(const raw_frame_t *raw, rgb_image_planar_t *rgb)
{
    if (raw == NULL || rgb == NULL) return -1;
    if (raw->width < 5 || raw->height < 5) {
        /* Fall back to bilinear for very small images */
        return demosaic_bilinear(raw, rgb);
    }

    uint32_t w = raw->width, h = raw->height;
    uint32_t x, y;

    /* Copy known values */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            pixel_raw_t v = raw->data[y * raw->stride + x];
            if (c == BAYER_COLOR_R) rgb->r[y*w+x] = v;
            else if (c == BAYER_COLOR_GR || c == BAYER_COLOR_GB) rgb->g[y*w+x] = v;
            else rgb->b[y*w+x] = v;
        }
    }

    /* Interpolate green using MHC kernels */
    for (y = 2; y < h - 2; y++) {
        for (x = 2; x < w - 2; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c == BAYER_COLOR_R) {
                rgb->g[y*w+x] = apply_5x5_kernel(raw, x, y, MHC_G_AT_R, 8);
            } else if (c == BAYER_COLOR_B) {
                rgb->g[y*w+x] = apply_5x5_kernel(raw, x, y, MHC_G_AT_B, 8);
            }
        }
    }

    /* Interpolate red */
    for (y = 2; y < h - 2; y++) {
        for (x = 2; x < w - 2; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c == BAYER_COLOR_GR) {
                rgb->r[y*w+x] = apply_5x5_kernel(raw, x, y, MHC_R_AT_GR, 8);
            } else if (c == BAYER_COLOR_B) {
                rgb->r[y*w+x] = apply_5x5_kernel(raw, x, y, MHC_R_AT_B, 16);
            } else if (c == BAYER_COLOR_GB) {
                /* R at G on B row: horizontal interpolation with correction */
                if (x >= 2 && x < w-2) {
                    int32_t g_val = rgb->g[y*w+x];
                    int32_t r_left = (x>=1) ? raw->data[y*raw->stride+x-1] : 0;
                    int32_t r_right = (x+1<w) ? raw->data[y*raw->stride+x+1] : 0;
                    int32_t g_left = (x>=1) ? rgb->g[y*w+(x-1)] : 0;
                    int32_t g_right = (x+1<w) ? rgb->g[y*w+(x+1)] : 0;
                    int32_t r_val = g_val + ((r_left-g_left)+(r_right-g_right))/2;
                    if (r_val < 0) r_val = 0;
                    rgb->r[y*w+x] = (uint16_t)r_val;
                }
            }
        }
    }

    /* Interpolate blue */
    for (y = 2; y < h - 2; y++) {
        for (x = 2; x < w - 2; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c == BAYER_COLOR_GB) {
                rgb->b[y*w+x] = apply_5x5_kernel(raw, x, y, MHC_B_AT_GB, 8);
            } else if (c == BAYER_COLOR_R) {
                rgb->b[y*w+x] = apply_5x5_kernel(raw, x, y, MHC_B_AT_R, 16);
            } else if (c == BAYER_COLOR_GR) {
                /* B at G on R row: vertical interpolation with correction */
                if (y >= 2 && y < h-2) {
                    int32_t g_val = rgb->g[y*w+x];
                    int32_t b_above = (y>=1) ? raw->data[(y-1)*raw->stride+x] : 0;
                    int32_t b_below = (y+1<h) ? raw->data[(y+1)*raw->stride+x] : 0;
                    int32_t g_above = (y>=1) ? rgb->g[(y-1)*w+x] : 0;
                    int32_t g_below = (y+1<h) ? rgb->g[(y+1)*w+x] : 0;
                    int32_t b_val = g_val + ((b_above-g_above)+(b_below-g_below))/2;
                    if (b_val < 0) b_val = 0;
                    rgb->b[y*w+x] = (uint16_t)b_val;
                }
            }
        }
    }

    /* Fill borders using bilinear for pixels < 2 from edge */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (x < 2 || x >= w-2 || y < 2 || y >= h-2) {
                bayer_color_t c = bayer_color_at(x, y, raw->cfa);
                if (c != BAYER_COLOR_GR && c != BAYER_COLOR_GB && rgb->g[y*w+x] == 0) {
                    /* Bilinear fallback for green at border R/B pixels */
                    uint32_t sum = 0, cnt = 0;
                    if (y>0) { sum+=raw->data[(y-1)*raw->stride+x]; cnt++; }
                    if (y<h-1) { sum+=raw->data[(y+1)*raw->stride+x]; cnt++; }
                    if (x>0) { sum+=raw->data[y*raw->stride+x-1]; cnt++; }
                    if (x<w-1) { sum+=raw->data[y*raw->stride+x+1]; cnt++; }
                    rgb->g[y*w+x] = (cnt>0)?(uint16_t)((sum+cnt/2)/cnt):0;
                }
            }
        }
    }

    return 0;
}

/*===========================================================================
 * L5: Gradient-corrected bilinear demosaicing
 *
 * Interpolates along edges (direction of smaller gradient).
 * Computes horizontal and vertical gradients, chooses the smaller,
 * and uses that direction for interpolation.
 *===========================================================================*/

int demosaic_gradient_corrected(const raw_frame_t *raw,
                                 rgb_image_planar_t *rgb)
{
    if (raw == NULL || rgb == NULL) return -1;
    if (raw->width < 3 || raw->height < 3) return -1;

    uint32_t w = raw->width, h = raw->height;
    uint32_t x, y;

    /* Copy known values */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            pixel_raw_t v = raw->data[y * raw->stride + x];
            if (c == BAYER_COLOR_R) rgb->r[y*w+x] = v;
            else if (c == BAYER_COLOR_GR || c == BAYER_COLOR_GB) rgb->g[y*w+x] = v;
            else rgb->b[y*w+x] = v;
        }
    }

    /* Interpolate green with gradient direction */
    for (y = 1; y < h - 1; y++) {
        for (x = 1; x < w - 1; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c == BAYER_COLOR_R || c == BAYER_COLOR_B) {
                /* Horizontal gradient */
                int32_t gh = abs((int32_t)raw->data[y*raw->stride+x-1] -
                                 (int32_t)raw->data[y*raw->stride+x+1]);
                /* Vertical gradient */
                int32_t gv = abs((int32_t)raw->data[(y-1)*raw->stride+x] -
                                 (int32_t)raw->data[(y+1)*raw->stride+x]);

                uint32_t sum = 0, cnt = 0;
                if (gh < gv) {
                    /* Interpolate horizontally */
                    if (x > 0) { sum += raw->data[y*raw->stride+x-1]; cnt++; }
                    if (x < w-1) { sum += raw->data[y*raw->stride+x+1]; cnt++; }
                } else if (gv < gh) {
                    /* Interpolate vertically */
                    if (y > 0) { sum += raw->data[(y-1)*raw->stride+x]; cnt++; }
                    if (y < h-1) { sum += raw->data[(y+1)*raw->stride+x]; cnt++; }
                } else {
                    /* Equal gradients: average all 4 */
                    if (y>0) { sum += raw->data[(y-1)*raw->stride+x]; cnt++; }
                    if (y<h-1) { sum += raw->data[(y+1)*raw->stride+x]; cnt++; }
                    if (x>0) { sum += raw->data[y*raw->stride+x-1]; cnt++; }
                    if (x<w-1) { sum += raw->data[y*raw->stride+x+1]; cnt++; }
                }
                rgb->g[y*w+x] = (cnt > 0) ? (uint16_t)((sum + cnt/2) / cnt) : 0;
            }
        }
    }

    /* Interpolate red/blue using color difference */
    for (y = 1; y < h - 1; y++) {
        for (x = 1; x < w - 1; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c != BAYER_COLOR_R) {
                /* R = G + (R-G) interpolated from neighbors */
                int32_t g_val = (int32_t)rgb->g[y*w+x];
                int32_t sum_diff = 0, cnt = 0;
                int32_t dy;
                const int32_t neighbors[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
                for (dy = 0; dy < 4; dy++) {
                    int32_t nx = (int32_t)x + neighbors[dy][0];
                    int32_t ny = (int32_t)y + neighbors[dy][1];
                    if (nx >= 0 && ny >= 0 && nx < (int32_t)w && ny < (int32_t)h) {
                        sum_diff += (int32_t)rgb->r[(uint32_t)ny*w+(uint32_t)nx] -
                                    (int32_t)rgb->g[(uint32_t)ny*w+(uint32_t)nx];
                        cnt++;
                    }
                }
                int32_t r_val = g_val + (cnt > 0 ? (sum_diff + cnt/2) / cnt : 0);
                if (r_val < 0) r_val = 0;
                rgb->r[y*w+x] = (uint16_t)r_val;
            }
            if (c != BAYER_COLOR_B) {
                /* B = G + (B-G) interpolated */
                int32_t g_val = (int32_t)rgb->g[y*w+x];
                int32_t sum_diff = 0, cnt = 0;
                int32_t dy;
                const int32_t neighbors[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
                for (dy = 0; dy < 4; dy++) {
                    int32_t nx = (int32_t)x + neighbors[dy][0];
                    int32_t ny = (int32_t)y + neighbors[dy][1];
                    if (nx >= 0 && ny >= 0 && nx < (int32_t)w && ny < (int32_t)h) {
                        sum_diff += (int32_t)rgb->b[(uint32_t)ny*w+(uint32_t)nx] -
                                    (int32_t)rgb->g[(uint32_t)ny*w+(uint32_t)nx];
                        cnt++;
                    }
                }
                int32_t b_val = g_val + (cnt > 0 ? (sum_diff + cnt/2) / cnt : 0);
                if (b_val < 0) b_val = 0;
                rgb->b[y*w+x] = (uint16_t)b_val;
            }
        }
    }

    return 0;
}

/*===========================================================================
 * L5: Adaptive Homogeneity-Directed (AHD) — Hirakawa & Parks 2005
 *
 * Two-pass:
 *   Pass 1: Interpolate G using homogeneity-directed weights
 *   Pass 2: R/B interpolation guided by homogeneous regions
 *===========================================================================*/

int demosaic_ahd(const raw_frame_t *raw, rgb_image_planar_t *rgb)
{
    if (raw == NULL || rgb == NULL) return -1;
    if (raw->width < 5 || raw->height < 5) {
        return demosaic_mhc(raw, rgb); /* Fall back to MHC for small images */
    }

    uint32_t w = raw->width, h = raw->height;
    uint32_t x, y;

    /* Copy known values */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            pixel_raw_t v = raw->data[y * raw->stride + x];
            if (c == BAYER_COLOR_R) rgb->r[y*w+x] = v;
            else if (c == BAYER_COLOR_GR || c == BAYER_COLOR_GB)
                rgb->g[y*w+x] = v;
            else rgb->b[y*w+x] = v;
        }
    }

    /* Pass 1: Adaptive green interpolation */
    for (y = 2; y < h - 2; y++) {
        for (x = 2; x < w - 2; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c == BAYER_COLOR_R || c == BAYER_COLOR_B) {
                /* N, S, E, W green values */
                uint32_t g_n = raw->data[(y-1)*raw->stride + x];
                uint32_t g_s = raw->data[(y+1)*raw->stride + x];
                uint32_t g_e = raw->data[y*raw->stride + x+1];
                uint32_t g_w = raw->data[y*raw->stride + x-1];

                /* Homogeneity: Laplacian magnitude */
                int32_t lap_h = abs((int32_t)raw->data[y*raw->stride+x-2] +
                                     (int32_t)raw->data[y*raw->stride+x+2] -
                                     2*(int32_t)raw->data[y*raw->stride+x]);
                int32_t lap_v = abs((int32_t)raw->data[(y-2)*raw->stride+x] +
                                     (int32_t)raw->data[(y+2)*raw->stride+x] -
                                     2*(int32_t)raw->data[y*raw->stride+x]);

                double wh, wv;
                if (fabs((double)lap_h) + fabs((double)lap_v) < 1.0) {
                    wh = wv = 0.5;
                } else {
                    wh = (double)lap_v / (double)(lap_h + lap_v + 1);
                    wv = (double)lap_h / (double)(lap_h + lap_v + 1);
                }

                double g_interp = wh * (g_e + g_w) / 2.0 +
                                  wv * (g_n + g_s) / 2.0;
                rgb->g[y*w+x] = (uint16_t)(g_interp + 0.5);
            }
        }
    }

    /* Pass 2: R/B interpolation guided by homogeneous G */
    for (y = 2; y < h - 2; y++) {
        for (x = 2; x < w - 2; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            if (c != BAYER_COLOR_R) {
                int32_t g_val = rgb->g[y*w+x];
                /* Use color difference in 4 diagonal directions */
                int32_t sum_diff = 0, cnt = 0;
                int32_t d;
                const int32_t dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
                for (d = 0; d < 4; d++) {
                    int32_t nx = (int32_t)x + dirs[d][0];
                    int32_t ny = (int32_t)y + dirs[d][1];
                    if (nx>=0 && ny>=0 && nx<(int32_t)w && ny<(int32_t)h) {
                        uint32_t ui = (uint32_t)ny*w + (uint32_t)nx;
                        if (rgb->r[ui] > 0 || rgb->g[ui] > 0) {
                            sum_diff += (int32_t)rgb->r[ui] - (int32_t)rgb->g[ui];
                            cnt++;
                        }
                    }
                }
                int32_t rv = g_val + (cnt > 0 ? (sum_diff+cnt/2)/cnt : 0);
                rgb->r[y*w+x] = (uint16_t)(rv < 0 ? 0 : rv);
            }
            if (c != BAYER_COLOR_B) {
                int32_t g_val = rgb->g[y*w+x];
                int32_t sum_diff = 0, cnt = 0;
                int32_t d;
                const int32_t dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
                for (d = 0; d < 4; d++) {
                    int32_t nx = (int32_t)x + dirs[d][0];
                    int32_t ny = (int32_t)y + dirs[d][1];
                    if (nx>=0 && ny>=0 && nx<(int32_t)w && ny<(int32_t)h) {
                        uint32_t ui = (uint32_t)ny*w + (uint32_t)nx;
                        sum_diff += (int32_t)rgb->b[ui] - (int32_t)rgb->g[ui];
                        cnt++;
                    }
                }
                int32_t bv = g_val + (cnt > 0 ? (sum_diff+cnt/2)/cnt : 0);
                rgb->b[y*w+x] = (uint16_t)(bv < 0 ? 0 : bv);
            }
        }
    }

    return 0;
}

/*===========================================================================
 * L5: Dispatcher and helpers
 *===========================================================================*/

int demosaic_execute(demosaic_algorithm_t algo, const raw_frame_t *raw,
                      rgb_image_planar_t *rgb)
{
    switch (algo) {
        case DEMOSAIC_BILINEAR:
            return demosaic_bilinear(raw, rgb);
        case DEMOSAIC_MHC:
            return demosaic_mhc(raw, rgb);
        case DEMOSAIC_GBC:
            return demosaic_gradient_corrected(raw, rgb);
        case DEMOSAIC_AHD:
            return demosaic_ahd(raw, rgb);
        default:
            return demosaic_bilinear(raw, rgb);
    }
}

void demosaic_gradients(const raw_frame_t *raw, uint32_t x, uint32_t y,
                         double *gh, double *gv)
{
    if (raw == NULL || gh == NULL || gv == NULL) return;
    *gh = *gv = 0.0;

    if (x < 2 || y < 2 || x >= raw->width-2 || y >= raw->height-2) return;

    /* Horizontal gradient: 5-tap [1, 0, -2, 0, 1] */
    *gh = fabs((double)raw->data[y*raw->stride+(x-2)] +
               (double)raw->data[y*raw->stride+(x+2)] -
               2.0*(double)raw->data[y*raw->stride+x]);

    /* Vertical gradient: 5-tap [1, 0, -2, 0, 1]^T */
    *gv = fabs((double)raw->data[(y-2)*raw->stride+x] +
               (double)raw->data[(y+2)*raw->stride+x] -
               2.0*(double)raw->data[y*raw->stride+x]);
}

uint16_t demosaic_interpolate_green_edge(const raw_frame_t *raw,
                                          uint32_t x, uint32_t y)
{
    if (raw == NULL || x >= raw->width || y >= raw->height) return 0;
    if (x < 1 || y < 1 || x >= raw->width-1 || y >= raw->height-1) {
        return raw->data[y * raw->stride + x];
    }
    double gh, gv;
    demosaic_gradients(raw, x, y, &gh, &gv);

    uint32_t n = raw->data[(y-1)*raw->stride+x];
    uint32_t s = raw->data[(y+1)*raw->stride+x];
    uint32_t e = raw->data[y*raw->stride+x+1];
    uint32_t w = raw->data[y*raw->stride+x-1];

    if (gh < gv) {
        return (uint16_t)((e + w + 1) / 2);
    } else if (gv < gh) {
        return (uint16_t)((n + s + 1) / 2);
    } else {
        return (uint16_t)((n + s + e + w + 2) / 4);
    }
}

uint16_t demosaic_interpolate_color_at_green(const raw_frame_t *raw,
                                              uint16_t *g_plane,
                                              uint32_t x, uint32_t y,
                                              uint8_t is_red)
{
    if (raw == NULL || g_plane == NULL) return 0;
    uint32_t w = raw->width, h = raw->height;
    int32_t g_val = g_plane[y*w+x];

    /* Determine if this green is on red row or blue row */
    bayer_color_t c = bayer_color_at(x, y, raw->cfa);
    uint8_t on_red_row = (c == BAYER_COLOR_GR);

    int32_t sum_diff = 0, cnt = 0;

    if ((is_red && on_red_row) || (!is_red && !on_red_row)) {
        /* Horizontal neighbors */
        if (x > 0) { sum_diff += (int32_t)g_plane[y*w+(x-1)] - (int32_t)raw->data[y*raw->stride+x-1]; cnt++; }
        if (x < w-1) { sum_diff += (int32_t)g_plane[y*w+(x+1)] - (int32_t)raw->data[y*raw->stride+x+1]; cnt++; }
    } else {
        /* Vertical neighbors */
        if (y > 0) { sum_diff += (int32_t)g_plane[(y-1)*w+x] - (int32_t)raw->data[(y-1)*raw->stride+x]; cnt++; }
        if (y < h-1) { sum_diff += (int32_t)g_plane[(y+1)*w+x] - (int32_t)raw->data[(y+1)*raw->stride+x]; cnt++; }
    }

    int32_t result = g_val - (cnt > 0 ? (sum_diff + cnt/2) / cnt : 0);
    if (result < 0) result = 0;
    return (uint16_t)result;
}

uint16_t demosaic_interpolate_color_at_opposite(const raw_frame_t *raw,
                                                  uint16_t *g_plane,
                                                  uint32_t x, uint32_t y,
                                                  uint8_t is_red_at_blue)
{
    if (raw == NULL || g_plane == NULL) return 0;
    uint32_t w = raw->width, h = raw->height;
    int32_t g_val = g_plane[y*w+x];

    /* R at B (or B at R): interpolate from 4 diagonal neighbors */
    int32_t sum_diff = 0, cnt = 0;
    int32_t d;
    const int32_t diag[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for (d = 0; d < 4; d++) {
        int32_t nx = (int32_t)x + diag[d][0];
        int32_t ny = (int32_t)y + diag[d][1];
        if (nx >= 0 && ny >= 0 && nx < (int32_t)w && ny < (int32_t)h) {
            uint32_t ui = (uint32_t)ny*w + (uint32_t)nx;
            if (is_red_at_blue) {
                sum_diff += (int32_t)g_plane[ui] - (int32_t)raw->data[(uint32_t)ny*raw->stride+(uint32_t)nx];
            } else {
                sum_diff += (int32_t)g_plane[ui] - (int32_t)raw->data[(uint32_t)ny*raw->stride+(uint32_t)nx];
            }
            cnt++;
        }
    }
    (void)is_red_at_blue;

    int32_t result = g_val - (cnt > 0 ? (sum_diff + cnt/2) / cnt : 0);
    if (result < 0) result = 0;
    return (uint16_t)result;
}

/*===========================================================================
 * L6: Quality assessment
 *===========================================================================*/

void demosaic_quality_assess(const rgb_image_planar_t *est,
                              const rgb_image_planar_t *ref,
                              demosaic_quality_t *metrics)
{
    if (est == NULL || ref == NULL || metrics == NULL) return;
    memset(metrics, 0, sizeof(*metrics));

    uint32_t n = est->width * est->height;
    if (n == 0) return;

    metrics->psnr_r = demosaic_psnr_channel(est->r, ref->r, n, 65535);
    metrics->psnr_g = demosaic_psnr_channel(est->g, ref->g, n, 65535);
    metrics->psnr_b = demosaic_psnr_channel(est->b, ref->b, n, 65535);
    metrics->cpsnr = (metrics->psnr_r + metrics->psnr_g + metrics->psnr_b) / 3.0;

    /* Simple SSIM proxy: correlation-based */
    double sum_re=0, sum_rr=0, sum_ee=0, sum_er=0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        sum_re += (double)ref->r[i] * (double)est->r[i];
        sum_rr += (double)ref->r[i] * (double)ref->r[i];
        sum_ee += (double)est->r[i] * (double)est->r[i];
        sum_er += (double)est->r[i] * (double)ref->r[i];
    }
    double r2 = (n * sum_re - sum_er * sum_er) /
                (sqrt((n*sum_rr - sum_er*sum_er)*(n*sum_ee - sum_er*sum_er)) + 1e-10);
    metrics->ssim = (r2 > 0.0) ? r2 : 0.0;

    /* delta-E: simplified — average per-channel difference */
    double sum_de = 0.0;
    for (i = 0; i < n; i++) {
        double dr = (double)est->r[i] - (double)ref->r[i];
        double dg = (double)est->g[i] - (double)ref->g[i];
        double db = (double)est->b[i] - (double)ref->b[i];
        sum_de += sqrt(dr*dr + dg*dg + db*db) / 65535.0 * 100.0;
    }
    metrics->delta_e_avg = sum_de / n;
}

double demosaic_psnr_channel(const uint16_t *a, const uint16_t *b,
                              uint32_t n, uint16_t max_val)
{
    if (a == NULL || b == NULL || n == 0) return 0.0;

    double mse = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double diff = (double)a[i] - (double)b[i];
        mse += diff * diff;
    }
    mse /= n;

    if (mse < 1e-10) return 100.0; /* Perfect reconstruction */
    double psnr = 20.0 * log10((double)max_val) - 10.0 * log10(mse);
    return psnr;
}

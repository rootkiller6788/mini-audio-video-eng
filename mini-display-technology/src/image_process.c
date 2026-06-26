/**
 * image_process.c — Display Image Processing Pipeline
 *
 * Implements:
 *   L1: Convolution kernel, histogram, dither matrix structures
 *   L2: Frame blending, alpha compositing, test pattern generation
 *   L5: Bilinear/bicubic/Lanczos scaling, Floyd-Steinberg/Atkinson/
 *       ordered dithering, histogram equalization, CLAHE
 *   L6: SMPTE color bars, zone plate, end-to-end scaling+dithering
 *
 * Reference:
 *   Poynton, "Digital Video and HD" (2012)
 *   Keys, "Cubic Convolution Interpolation", IEEE T-ASSP (1981)
 *   Floyd & Steinberg, SID 1976 Digest
 *   Zuiderveld, "CLAHE" in Graphics Gems IV (1994)
 *
 * L7 Applications:
 *   - Boeing 747 cockpit display anti-aliased rendering
 *   - F-35 helmet-mounted display image warping
 *   - Apollo Lunar Module guidance display simulation
 *   - NASA Mars rover panoramic image stitching
 *   - Fukushima nuclear inspection robot display processing
 * L8 Advanced:
 *   - Monte Carlo error diffusion for blue-noise dithering
 *   - fuzzy adaptive contrast enhancement
 *   - Game of Life cellular automata test pattern generation
 */

#include "image_process.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define PI M_PI

/* ==========================================================================
 * L5: Interpolation Kernel Functions
 * ========================================================================== */

double bicubic_weight(double t, double a)
{
    double at = fabs(t);
    if (at < 1.0) {
        return (a + 2.0) * at * at * at - (a + 3.0) * at * at + 1.0;
    } else if (at < 2.0) {
        return a * at * at * at - 5.0 * a * at * at + 8.0 * a * at - 4.0 * a;
    }
    return 0.0;
}

double lanczos_weight(double t, int a)
{
    if (t == 0.0) return 1.0;
    if (fabs(t) >= a) return 0.0;
    double pix = PI * t;
    double pia = PI * t / a;
    return a * sin(pix) * sin(pia) / (pix * pix);
}

separable_kernel_t *separable_kernel_lanczos(int a, double scale_ratio)
{
    int radius = (scale_ratio < 1.0) ? (int)(a / scale_ratio + 1) : a;
    int len = 2 * radius + 1;
    separable_kernel_t *k = malloc(sizeof(separable_kernel_t));
    if (!k) return NULL;
    k->weights = malloc(len * sizeof(double));
    if (!k->weights) { free(k); return NULL; }
    k->radius = radius;
    k->sum = 0.0;

    for (int i = 0; i < len; i++) {
        double t = fabs((double)(i - radius) * scale_ratio);
        k->weights[i] = lanczos_weight(t, a);
        k->sum += k->weights[i];
    }
    if (k->sum > 0.0) {
        for (int i = 0; i < len; i++)
            k->weights[i] /= k->sum;
        k->sum = 1.0;
    }
    return k;
}

separable_kernel_t *separable_kernel_bicubic(double scale_ratio)
{
    int radius = (scale_ratio < 1.0) ? (int)(2.0 / scale_ratio + 1) : 2;
    int len = 2 * radius + 1;
    separable_kernel_t *k = malloc(sizeof(separable_kernel_t));
    if (!k) return NULL;
    k->weights = malloc(len * sizeof(double));
    if (!k->weights) { free(k); return NULL; }
    k->radius = radius;
    k->sum = 0.0;

    for (int i = 0; i < len; i++) {
        double t = fabs((double)(i - radius) * scale_ratio);
        k->weights[i] = bicubic_weight(t, -0.5);
        k->sum += k->weights[i];
    }
    if (k->sum > 0.0) {
        for (int i = 0; i < len; i++)
            k->weights[i] /= k->sum;
        k->sum = 1.0;
    }
    return k;
}

void separable_kernel_free(separable_kernel_t *k)
{
    if (k) {
        free(k->weights);
        free(k);
    }
}

/* ==========================================================================
 * L5: Image Scaling
 * ========================================================================== */

/**
 * Nearest-neighbor scaling: copy src pixel to nearest dst pixel.
 */
static void scale_nearest(const uint8_t *src, int sw, int sh, int ss,
                          uint8_t *dst, int dw, int dh, int ds, int bpp)
{
    for (int dy = 0; dy < dh; dy++) {
        int sy = (int)((double)dy * sh / dh);
        if (sy >= sh) sy = sh - 1;
        for (int dx = 0; dx < dw; dx++) {
            int sx = (int)((double)dx * sw / dw);
            if (sx >= sw) sx = sw - 1;
            memcpy(dst + dy * ds + dx * bpp,
                   src + sy * ss + sx * bpp, bpp);
        }
    }
}

/**
 * Bilinear interpolation scaling.
 */
static void scale_bilinear(const uint8_t *src, int sw, int sh, int ss,
                           uint8_t *dst, int dw, int dh, int ds, int bpp)
{
    for (int dy = 0; dy < dh; dy++) {
        double fy = (double)dy * (sh - 1) / (dh > 1 ? dh - 1 : 1);
        int sy0 = (int)fy;
        int sy1 = (sy0 + 1 < sh) ? sy0 + 1 : sy0;
        double wy = fy - sy0;

        for (int dx = 0; dx < dw; dx++) {
            double fx = (double)dx * (sw - 1) / (dw > 1 ? dw - 1 : 1);
            int sx0 = (int)fx;
            int sx1 = (sx0 + 1 < sw) ? sx0 + 1 : sx0;
            double wx = fx - sx0;

            for (int c = 0; c < bpp; c++) {
                double v00 = src[sy0 * ss + sx0 * bpp + c];
                double v10 = src[sy0 * ss + sx1 * bpp + c];
                double v01 = src[sy1 * ss + sx0 * bpp + c];
                double v11 = src[sy1 * ss + sx1 * bpp + c];
                double v = (1 - wx) * (1 - wy) * v00 + wx * (1 - wy) * v10
                         + (1 - wx) * wy * v01 + wx * wy * v11;
                int iv = (int)(v + 0.5);
                if (iv < 0) iv = 0; if (iv > 255) iv = 255;
                dst[dy * ds + dx * bpp + c] = (uint8_t)iv;
            }
        }
    }
}

/**
 * Separable convolution scaling (for bicubic/lanczos kernels).
 * First horizontal pass, then vertical pass.
 */
static void scale_separable(const uint8_t *src, int sw, int sh, int ss,
                            uint8_t *dst, int dw, int dh, int ds,
                            int bpp, separable_kernel_t *kernel)
{
    if (!kernel) return;

    /* Temporary buffer for horizontal pass */
    double **temp = malloc(dh * sizeof(double *));
    for (int y = 0; y < dh; y++)
        temp[y] = calloc(dw * bpp, sizeof(double));

    double src_x = (double)sw / dw;
    double scale = (src_x < 1.0) ? src_x : 1.0;

    /* Horizontal pass */
    for (int dy = 0; dy < dh; dy++) {
        int sy = (int)((double)dy * sh / dh + 0.5);
        if (sy < 0) sy = 0; if (sy >= sh) sy = sh - 1;

        for (int dx = 0; dx < dw; dx++) {
            double cx = ((double)dx + 0.5) * (double)sw / dw - 0.5;
            int ix = (int)cx;
            for (int c = 0; c < bpp; c++) {
                double sum = 0.0;
                for (int k = -kernel->radius; k <= kernel->radius; k++) {
                    int sx = ix + k;
                    if (sx < 0) sx = 0;
                    if (sx >= sw) sx = sw - 1;
                    sum += src[sy * ss + sx * bpp + c]
                           * kernel->weights[k + kernel->radius];
                }
                temp[dy][dx * bpp + c] = sum;
            }
        }
    }

    /* Vertical pass */
    for (int dy = 0; dy < dh; dy++) {
        double cy = ((double)dy + 0.5) * (double)sh / dh - 0.5;
        int iy = (int)cy;
        for (int dx = 0; dx < dw; dx++) {
            for (int c = 0; c < bpp; c++) {
                double sum = 0.0;
                for (int k = -kernel->radius; k <= kernel->radius; k++) {
                    int sy = iy + k;
                    if (sy < 0) sy = 0;
                    if (sy >= dh) sy = dh - 1;
                    sum += temp[sy][dx * bpp + c]
                           * kernel->weights[k + kernel->radius];
                }
                int iv = (int)(sum + 0.5);
                if (iv < 0) iv = 0; if (iv > 255) iv = 255;
                dst[dy * ds + dx * bpp + c] = (uint8_t)iv;
            }
        }
    }

    for (int y = 0; y < dh; y++) free(temp[y]);
    free(temp);
}

int image_scale_separable(const framebuffer_t *src, framebuffer_t *dst,
                          separable_kernel_t *kernel)
{
    if (!src || !dst || !src->data || !dst->data || !kernel) return -1;
    int bpp = (int)src->bytes_per_pixel;
    if (bpp <= 0 || bpp > 6) return -1;

    scale_separable(src->data, (int)src->width, (int)src->height,
                    (int)src->stride_bytes,
                    dst->data, (int)dst->width, (int)dst->height,
                    (int)dst->stride_bytes, bpp, kernel);
    return 0;
}

int image_scale(const framebuffer_t *src, framebuffer_t *dst,
                scale_method_t h_method, scale_method_t v_method)
{
    if (!src || !dst || !src->data || !dst->data) return -1;
    int bpp = (int)src->bytes_per_pixel;
    if (bpp <= 0) return -1;

    /* Use horizontal method for both directions */
    switch (h_method) {
        case SCALE_NEAREST:
            scale_nearest(src->data, (int)src->width, (int)src->height,
                          (int)src->stride_bytes,
                          dst->data, (int)dst->width, (int)dst->height,
                          (int)dst->stride_bytes, bpp);
            break;
        case SCALE_BILINEAR:
            scale_bilinear(src->data, (int)src->width, (int)src->height,
                           (int)src->stride_bytes,
                           dst->data, (int)dst->width, (int)dst->height,
                           (int)dst->stride_bytes, bpp);
            break;
        case SCALE_BICUBIC: {
            separable_kernel_t *k = separable_kernel_bicubic(
                (double)src->width / dst->width);
            scale_separable(src->data, (int)src->width, (int)src->height,
                            (int)src->stride_bytes,
                            dst->data, (int)dst->width, (int)dst->height,
                            (int)dst->stride_bytes, bpp, k);
            separable_kernel_free(k);
            break;
        }
        case SCALE_LANCZOS2: {
            separable_kernel_t *k = separable_kernel_lanczos(
                2, (double)src->width / dst->width);
            scale_separable(src->data, (int)src->width, (int)src->height,
                            (int)src->stride_bytes,
                            dst->data, (int)dst->width, (int)dst->height,
                            (int)dst->stride_bytes, bpp, k);
            separable_kernel_free(k);
            break;
        }
        case SCALE_LANCZOS3: {
            separable_kernel_t *k = separable_kernel_lanczos(
                3, (double)src->width / dst->width);
            scale_separable(src->data, (int)src->width, (int)src->height,
                            (int)src->stride_bytes,
                            dst->data, (int)dst->width, (int)dst->height,
                            (int)dst->stride_bytes, bpp, k);
            separable_kernel_free(k);
            break;
        }
        case SCALE_BOX:
        default:
            scale_nearest(src->data, (int)src->width, (int)src->height,
                          (int)src->stride_bytes,
                          dst->data, (int)dst->width, (int)dst->height,
                          (int)dst->stride_bytes, bpp);
            break;
    }
    return 0;
}

/* ==========================================================================
 * L5: Dithering / Error Diffusion
 * ========================================================================== */

/**
 * Floyd-Steinberg error diffusion dither.
 *
 * Each pixel quantized to output_levels levels; quantization error
 * distributed to neighbors with weights: 7/16 right, 3/16 down-left,
 * 5/16 down, 1/16 down-right.
 *
 * Complexity: O(width × height)
 */
void dither_floyd_steinberg_apply(double *channel, int width, int height,
                                  int output_levels, int stride)
{
    double *errors = calloc((height + 2) * stride, sizeof(double));
    if (!errors) return;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * stride + x;
            double val = channel[idx] + errors[idx];
            /* Quantize */
            int q = (int)(val * (output_levels - 1) + 0.5);
            if (q < 0) q = 0;
            if (q >= output_levels) q = output_levels - 1;
            double quantized = (double)q / (output_levels - 1);
            double err = val - quantized;

            channel[idx] = quantized;

            /* Distribute error */
            if (x + 1 < width)
                errors[y * stride + x + 1] += err * 7.0 / 16.0;
            if (y + 1 < height) {
                if (x - 1 >= 0)
                    errors[(y + 1) * stride + x - 1] += err * 3.0 / 16.0;
                errors[(y + 1) * stride + x] += err * 5.0 / 16.0;
                if (x + 1 < width)
                    errors[(y + 1) * stride + x + 1] += err * 1.0 / 16.0;
            }
        }
    }
    free(errors);
}

/**
 * Atkinson dither (Bill Atkinson, MacPaint).
 * Distributes 1/8 of error to each of 6 neighbors.
 */
static void dither_atkinson_apply(double *channel, int width, int height,
                                  int output_levels, int stride)
{
    double *errors = calloc((height + 3) * stride, sizeof(double));
    if (!errors) return;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * stride + x;
            double val = channel[idx] + errors[idx];
            int q = (int)(val * (output_levels - 1) + 0.5);
            if (q < 0) q = 0;
            if (q >= output_levels) q = output_levels - 1;
            double quantized = (double)q / (output_levels - 1);
            double err = val - quantized;
            channel[idx] = quantized;

            double e = err / 8.0;
            if (x + 1 < width)  errors[idx + 1] += e;
            if (x + 2 < width)  errors[idx + 2] += e;
            if (y + 1 < height) {
                if (x - 1 >= 0) errors[(y+1)*stride + x-1] += e;
                errors[(y+1)*stride + x] += e;
                if (x + 1 < width) errors[(y+1)*stride + x+1] += e;
                if (y + 2 < height) errors[(y+2)*stride + x] += e;
            }
        }
    }
    free(errors);
}

/**
 * Jarvis-Judice-Ninke error diffusion.
 */
static void dither_jarvis_apply(double *channel, int width, int height,
                                int output_levels, int stride)
{
    double *errors = calloc((height + 3) * stride, sizeof(double));
    if (!errors) return;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * stride + x;
            double val = channel[idx] + errors[idx];
            int q = (int)(val * (output_levels - 1) + 0.5);
            if (q < 0) q = 0;
            if (q >= output_levels) q = output_levels - 1;
            double err = val - (double)q / (output_levels - 1);
            channel[idx] = (double)q / (output_levels - 1);

            double pat[] = {7.0/48, 5.0/48, 3.0/48, 5.0/48, 7.0/48,
                             5.0/48, 3.0/48, 1.0/48, 3.0/48, 5.0/48};
            int dxo[] = {-2, -1, 0, 1, 2, -2, -1, 0, 1, 2};
            int dyo[] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1};

            for (int n = 0; n < 10; n++) {
                int nx = x + dxo[n], ny = y + dyo[n];
                if (nx >= 0 && nx < width && ny < height)
                    errors[ny * stride + nx] += err * pat[n];
            }
        }
    }
    free(errors);
}

/**
 * Stucki error diffusion.
 */
static void dither_stucki_apply(double *channel, int width, int height,
                                int output_levels, int stride)
{
    double *errors = calloc((height + 3) * stride, sizeof(double));
    if (!errors) return;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * stride + x;
            double val = channel[idx] + errors[idx];
            int q = (int)(val * (output_levels - 1) + 0.5);
            if (q < 0) q = 0;
            if (q >= output_levels) q = output_levels - 1;
            double err = val - (double)q / (output_levels - 1);
            channel[idx] = (double)q / (output_levels - 1);

            double pat[] = {8.0/42, 4.0/42, 2.0/42, 4.0/42, 8.0/42,
                             4.0/42, 2.0/42, 1.0/42, 2.0/42, 4.0/42};
            int dxo[] = {-2, -1, 0, 1, 2, -2, -1, 0, 1, 2};
            int dyo[] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1};

            for (int n = 0; n < 10; n++) {
                int nx = x + dxo[n], ny = y + dyo[n];
                if (nx >= 0 && nx < width && ny < height)
                    errors[ny * stride + nx] += err * pat[n];
            }
            /* Row 2 weights (y+2) */
            double pat2[] = {2.0/42, 4.0/42, 2.0/42};
            int dxo2[] = {-2, -1, 0};
            for (int n = 0; n < 3; n++) {
                int nx = x + dxo2[n], ny = y + 2;
                if (nx >= 0 && nx < width && ny < height)
                    errors[ny * stride + nx] += err * pat2[n];
            }
        }
    }
    free(errors);
}

void bayer_matrix_generate(uint8_t *matrix, int n)
{
    if (!matrix || n <= 0) return;
    int size = 1 << n;
    if (size == 1) { matrix[0] = 0; return; }
    if (size == 2) {
        matrix[0] = 0; matrix[1] = 2;
        matrix[2] = 3; matrix[3] = 1;
        return;
    }
    /* Recursive Bayer: D_2n = [4*D_n,      4*D_n+2
     *                          4*D_n+3,    4*D_n+1] */
    int half = size / 2;
    bayer_matrix_generate(matrix, n - 1);
    for (int y = 0; y < half; y++) {
        for (int x = 0; x < half; x++) {
            uint8_t v = matrix[y * half + x];
            int base = size * size;
            matrix[y * size + x] = v * 4;
            matrix[y * size + x + half] = v * 4 + 2;
            matrix[(y + half) * size + x] = v * 4 + 3;
            matrix[(y + half) * size + x + half] = v * 4 + 1;
        }
    }
    /* Normalize */
    for (int i = 0; i < size * size; i++)
        matrix[i] = (uint8_t)(matrix[i] * 255 / (size * size));
}

void clustered_dot_screen_generate(double *screen, int size)
{
    if (!screen || size <= 1) return;
    /* Generate a simple clustered-dot screen via ellipse pattern */
    double cx = (size - 1) / 2.0, cy = (size - 1) / 2.0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double dx = (x - cx) / cx, dy = (y - cy) / cy;
            double r2 = dx * dx + dy * dy;
            screen[y * size + x] = r2 > 1.0 ? 1.0 : r2;
        }
    }
}

int image_dither(const framebuffer_t *src, framebuffer_t *dst,
                 dither_method_t method, uint8_t target_bits_per_channel)
{
    if (!src || !dst || !src->data || !dst->data) return -1;
    int width = (int)src->width, height = (int)src->height;
    int out_levels = 1 << target_bits_per_channel;

    /* Copy src to dst first */
    if (src->data != dst->data)
        memcpy(dst->data, src->data, src->total_bytes);

    int bpp = (int)src->bytes_per_pixel;
    if (bpp == 1) {
        /* Grayscale: extract one channel */
        double *channel = malloc(width * height * sizeof(double));
        if (!channel) return -1;
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                channel[y * width + x] = src->data[y * src->stride_bytes + x] / 255.0;

        switch (method) {
            case DITHER_FLOYD_STEINBERG:
                dither_floyd_steinberg_apply(channel, width, height, out_levels, width);
                break;
            case DITHER_ATKINSON:
                dither_atkinson_apply(channel, width, height, out_levels, width);
                break;
            case DITHER_JARVIS_JUDICE:
                dither_jarvis_apply(channel, width, height, out_levels, width);
                break;
            case DITHER_STUCKI:
                dither_stucki_apply(channel, width, height, out_levels, width);
                break;
            case DITHER_ORDERED_BAYER: {
                uint8_t bayer[16][16]; /* max 2^4 */
                bayer_matrix_generate((uint8_t*)bayer, 4);
                for (int y = 0; y < height; y++)
                    for (int x = 0; x < width; x++) {
                        double thresh = bayer[y % 16][x % 16] / 255.0;
                        channel[y * width + x] = (channel[y * width + x] > thresh) ? 1.0 : 0.0;
                    }
                break;
            }
            case DITHER_NOISE:
                for (int y = 0; y < height; y++)
                    for (int x = 0; x < width; x++) {
                        double noise = ((double)rand() / RAND_MAX - 0.5) * 0.1;
                        double v = channel[y * width + x] + noise;
                        int q = (int)(v * (out_levels - 1) + 0.5);
                        if (q < 0) q = 0; if (q >= out_levels) q = out_levels - 1;
                        channel[y * width + x] = (double)q / (out_levels - 1);
                    }
                break;
            default:
                dither_floyd_steinberg_apply(channel, width, height, out_levels, width);
                break;
        }

        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                int v = (int)(channel[y * width + x] * 255.0 + 0.5);
                if (v < 0) v = 0; if (v > 255) v = 255;
                dst->data[y * dst->stride_bytes + x] = (uint8_t)v;
            }
        free(channel);
    }
    return 0;
}

void error_diffuser_init(error_diffuser_t *ed, dither_method_t method, int width)
{
    if (!ed) return;
    ed->method = method;
    ed->width = width;
    ed->error_buf = calloc(width, sizeof(double));
}

void error_diffuser_free(error_diffuser_t *ed)
{
    if (ed) {
        free(ed->error_buf);
        ed->error_buf = NULL;
    }
}

/* ==========================================================================
 * L5: Histogram Operations
 * ========================================================================== */

void histogram_compute(const framebuffer_t *fb, image_histogram_t *hist)
{
    if (!fb || !hist) return;
    memset(hist, 0, sizeof(*hist));
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            /* Grayscale assumed: pixel[0] */
            uint8_t v = fb->data[y * fb->stride_bytes + x * fb->bytes_per_pixel];
            hist->bins[v]++;
        }
    }
    hist->total_pixels = fb->width * fb->height;
    histogram_stats_compute(hist);
}

void histogram_stats_compute(image_histogram_t *hist)
{
    if (!hist || hist->total_pixels == 0) return;
    double sum = 0.0, sum_sq = 0.0;
    double mn = 255.0, mx = 0.0;
    for (int i = 0; i < 256; i++) {
        sum += i * hist->bins[i];
        sum_sq += (double)i * i * hist->bins[i];
        if (hist->bins[i] > 0) {
            if (i < mn) mn = i;
            if (i > mx) mx = i;
        }
    }
    double n = hist->total_pixels;
    hist->mean = sum / n;
    hist->variance = sum_sq / n - hist->mean * hist->mean;
    hist->std_dev = sqrt(hist->variance > 0 ? hist->variance : 0);
    hist->min_val = mn;
    hist->max_val = mx;
}

void histogram_cdf_compute(const image_histogram_t *hist, histogram_cdf_t *cdf)
{
    if (!hist || !cdf || hist->total_pixels == 0) return;
    memset(cdf, 0, sizeof(*cdf));
    uint32_t cum = 0;
    for (int i = 0; i < 256; i++) {
        cum += hist->bins[i];
        cdf->cdf[i] = (double)cum / hist->total_pixels;
    }
    /* Find min nonzero CDF */
    cdf->min_cdf_nonzero = 1.0;
    for (int i = 0; i < 256; i++) {
        if (cdf->cdf[i] > 0.0 && cdf->cdf[i] < cdf->min_cdf_nonzero)
            cdf->min_cdf_nonzero = cdf->cdf[i];
    }
    if (cdf->min_cdf_nonzero >= 1.0) cdf->min_cdf_nonzero = 0.0;
    /* Build LUT */
    double denom = 1.0 - cdf->min_cdf_nonzero;
    if (denom <= 0.0) denom = 1.0;
    for (int i = 0; i < 256; i++) {
        double v = (cdf->cdf[i] - cdf->min_cdf_nonzero) / denom * 255.0;
        if (v < 0) v = 0; if (v > 255) v = 255;
        cdf->lut[i] = (uint8_t)(v + 0.5);
    }
}

void histogram_equalize(framebuffer_t *fb, const histogram_cdf_t *cdf)
{
    if (!fb || !cdf || !fb->data) return;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            uint8_t v = fb->data[y * fb->stride_bytes + x * fb->bytes_per_pixel];
            fb->data[y * fb->stride_bytes + x * fb->bytes_per_pixel] = cdf->lut[v];
        }
    }
}

/**
 * CLAHE — Contrast Limited Adaptive Histogram Equalization.
 *
 * Divides image into tiles, clips histogram at clip_limit * tile_pixels,
 * redistributes clipped pixels evenly. Bilinear interpolation at boundaries.
 */
int clahe_apply(framebuffer_t *fb, const clahe_params_t *params)
{
    if (!fb || !params || !fb->data || fb->bytes_per_pixel != 1) return -1;
    int tw = (int)fb->width / params->tile_cols;
    int th = (int)fb->height / params->tile_rows;
    if (tw < 8 || th < 8) return -1;

    /* Per-tile LUTs */
    uint8_t **luts = malloc(params->tile_rows * sizeof(uint8_t *));
    for (int ty = 0; ty < params->tile_rows; ty++) {
        luts[ty] = malloc(params->tile_cols * 256 * sizeof(uint8_t));
    }

    for (int ty = 0; ty < params->tile_rows; ty++) {
        for (int tx = 0; tx < params->tile_cols; tx++) {
            uint32_t bins[256] = {0};
            int x0 = tx * tw, y0 = ty * th;
            int x1 = (tx + 1) * tw, y1 = (ty + 1) * th;
            if (x1 > (int)fb->width) x1 = (int)fb->width;
            if (y1 > (int)fb->height) y1 = (int)fb->height;
            int count = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    bins[fb->data[y * fb->stride_bytes + x]]++;
                    count++;
                }
            /* Clip histogram */
            int clip = (int)(count * params->clip_limit / 256.0);
            if (clip < 1) clip = 1;
            int excess = 0;
            for (int i = 0; i < 256; i++) {
                if (bins[i] > (uint32_t)clip) {
                    excess += bins[i] - clip;
                    bins[i] = clip;
                }
            }
            int redist = excess / 256;
            for (int i = 0; i < 256; i++)
                bins[i] += redist;
            /* CDF → LUT */
            uint32_t cum = 0;
            double scale = 255.0 / count;
            for (int i = 0; i < 256; i++) {
                cum += bins[i];
                int v = (int)(cum * scale + 0.5);
                if (v < 0) v = 0; if (v > 255) v = 255;
                luts[ty][tx * 256 + i] = (uint8_t)v;
            }
        }
    }

    /* Apply with bilinear interpolation */
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            uint8_t v = fb->data[y * fb->stride_bytes + x];
            /* Tile coordinates */
            double tfx = (double)(x + tw/2) / tw - 1.0;
            double tfy = (double)(y + th/2) / th - 1.0;
            int tx0 = (int)tfx, ty0 = (int)tfy;
            if (tx0 < 0) tx0 = 0; if (tx0 >= params->tile_cols - 1) tx0 = params->tile_cols - 2;
            if (ty0 < 0) ty0 = 0; if (ty0 >= params->tile_rows - 1) ty0 = params->tile_rows - 2;
            int tx1 = tx0 + 1, ty1 = ty0 + 1;
            double wx = tfx - tx0, wy = tfy - ty0;

            int v00 = luts[ty0][tx0 * 256 + v];
            int v10 = luts[ty0][tx1 * 256 + v];
            int v01 = luts[ty1][tx0 * 256 + v];
            int v11 = luts[ty1][tx1 * 256 + v];

            int out = (int)((1-wx)*(1-wy)*v00 + wx*(1-wy)*v10 + (1-wx)*wy*v01 + wx*wy*v11 + 0.5);
            if (out < 0) out = 0; if (out > 255) out = 255;
            fb->data[y * fb->stride_bytes + x] = (uint8_t)out;
        }
    }

    for (int ty = 0; ty < params->tile_rows; ty++) free(luts[ty]);
    free(luts);
    return 0;
}

/* ==========================================================================
 * L2 / L5: 2D Convolution
 * ========================================================================== */

conv_kernel2d_t *conv_kernel2d_alloc(int rows, int cols,
                                      int origin_row, int origin_col)
{
    conv_kernel2d_t *k = malloc(sizeof(conv_kernel2d_t));
    if (!k) return NULL;
    k->rows = rows;
    k->cols = cols;
    k->origin_row = origin_row;
    k->origin_col = origin_col;
    k->sum = 0.0;
    k->weights = calloc(rows * cols, sizeof(double));
    if (!k->weights) { free(k); return NULL; }
    return k;
}

void conv_kernel2d_free(conv_kernel2d_t *k)
{
    if (k) {
        free(k->weights);
        free(k);
    }
}

int image_conv2d(const framebuffer_t *src, framebuffer_t *dst,
                 const conv_kernel2d_t *kernel)
{
    if (!src || !dst || !kernel || !src->data || !dst->data) return -1;
    /* Grayscale only */
    for (uint32_t y = 0; y < src->height; y++) {
        for (uint32_t x = 0; x < src->width; x++) {
            double sum = 0.0;
            for (int ky = 0; ky < kernel->rows; ky++) {
                int sy = (int)y + ky - kernel->origin_row;
                if (sy < 0) sy = 0;
                if (sy >= (int)src->height) sy = (int)src->height - 1;
                for (int kx = 0; kx < kernel->cols; kx++) {
                    int sx = (int)x + kx - kernel->origin_col;
                    if (sx < 0) sx = 0;
                    if (sx >= (int)src->width) sx = (int)src->width - 1;
                    sum += src->data[sy * src->stride_bytes + sx]
                           * kernel->weights[ky * kernel->cols + kx];
                }
            }
            if (kernel->sum > 0.0) sum /= kernel->sum;
            int v = (int)(sum + 0.5);
            if (v < 0) v = 0; if (v > 255) v = 255;
            dst->data[y * dst->stride_bytes + x] = (uint8_t)v;
        }
    }
    return 0;
}

conv_kernel2d_t *kernel_gaussian_blur(double sigma, int size)
{
    if (size < 3 || size % 2 == 0) size = 3;
    int half = size / 2;
    conv_kernel2d_t *k = conv_kernel2d_alloc(size, size, half, half);
    if (!k) return NULL;
    double s2 = 2.0 * sigma * sigma;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double dx = x - half, dy = y - half;
            k->weights[y * size + x] = exp(-(dx*dx + dy*dy) / s2);
            k->sum += k->weights[y * size + x];
        }
    }
    return k;
}

conv_kernel2d_t *kernel_unsharp_mask(double strength)
{
    conv_kernel2d_t *k = conv_kernel2d_alloc(3, 3, 1, 1);
    if (!k) return NULL;
    double lap[9] = {0, -1, 0, -1, 4, -1, 0, -1, 0};
    for (int i = 0; i < 9; i++) {
        k->weights[i] = (i == 4) ? 1.0 + strength * 4.0 : -strength;
    }
    k->sum = 1.0;
    return k;
}

conv_kernel2d_t *kernel_sobel(int gradient_x)
{
    conv_kernel2d_t *k = conv_kernel2d_alloc(3, 3, 1, 1);
    if (!k) return NULL;
    int gx[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    int gy[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    for (int i = 0; i < 9; i++)
        k->weights[i] = gradient_x ? gx[i] : gy[i];
    k->sum = 1.0;
    return k;
}

conv_kernel2d_t *kernel_box_blur(int size)
{
    if (size < 3 || size % 2 == 0) size = 3;
    int half = size / 2;
    conv_kernel2d_t *k = conv_kernel2d_alloc(size, size, half, half);
    if (!k) return NULL;
    double v = 1.0 / (size * size);
    for (int i = 0; i < size * size; i++) k->weights[i] = v;
    k->sum = 1.0;
    return k;
}

int image_sharpen(framebuffer_t *fb, double strength)
{
    conv_kernel2d_t *k = kernel_unsharp_mask(strength);
    if (!k) return -1;
    framebuffer_t tmp;
    if (framebuffer_alloc(&tmp, fb->width, fb->height, fb->format) != 0) {
        conv_kernel2d_free(k); return -1;
    }
    int r = image_conv2d(fb, &tmp, k);
    if (r == 0) memcpy(fb->data, tmp.data, tmp.total_bytes);
    framebuffer_free(&tmp);
    conv_kernel2d_free(k);
    return r;
}

int image_blur_gaussian(framebuffer_t *fb, double sigma)
{
    conv_kernel2d_t *k = kernel_gaussian_blur(sigma, (int)(sigma * 6 + 1) | 1);
    if (!k) return -1;
    framebuffer_t tmp;
    if (framebuffer_alloc(&tmp, fb->width, fb->height, fb->format) != 0) {
        conv_kernel2d_free(k); return -1;
    }
    int r = image_conv2d(fb, &tmp, k);
    if (r == 0) memcpy(fb->data, tmp.data, tmp.total_bytes);
    framebuffer_free(&tmp);
    conv_kernel2d_free(k);
    return r;
}

/* ==========================================================================
 * L2: Frame Operations (Compositing/Blending)
 * ========================================================================== */

int framebuffer_alpha_blend(const framebuffer_t *src, framebuffer_t *dst,
                            uint32_t dx, uint32_t dy, double alpha)
{
    if (!src || !dst || !src->data || !dst->data) return -1;
    if (alpha < 0.0) alpha = 0.0; if (alpha > 1.0) alpha = 1.0;
    uint32_t sw = src->width, sh = src->height;
    for (uint32_t y = 0; y < sh && dy + y < dst->height; y++) {
        for (uint32_t x = 0; x < sw && dx + x < dst->width; x++) {
            for (uint32_t c = 0; c < src->bytes_per_pixel && c < 3; c++) {
                uint32_t s_idx = y * src->stride_bytes + x * src->bytes_per_pixel + c;
                uint32_t d_idx = (dy + y) * dst->stride_bytes + (dx + x) * dst->bytes_per_pixel + c;
                double sv = src->data[s_idx];
                double dv = dst->data[d_idx];
                int v = (int)(sv * alpha + dv * (1.0 - alpha) + 0.5);
                if (v < 0) v = 0; if (v > 255) v = 255;
                dst->data[d_idx] = (uint8_t)v;
            }
        }
    }
    return 0;
}

int framebuffer_overlay(const framebuffer_t *overlay, framebuffer_t *base,
                        uint32_t x, uint32_t y)
{
    if (!overlay || !base || !overlay->data || !base->data) return -1;
    uint32_t ow = overlay->width, oh = overlay->height;
    for (uint32_t oy = 0; oy < oh && y + oy < base->height; oy++) {
        for (uint32_t ox = 0; ox < ow && x + ox < base->width; ox++) {
            for (uint32_t c = 0; c < overlay->bytes_per_pixel && c < base->bytes_per_pixel; c++) {
                uint32_t s_idx = oy * overlay->stride_bytes + ox * overlay->bytes_per_pixel + c;
                uint32_t d_idx = (y + oy) * base->stride_bytes + (x + ox) * base->bytes_per_pixel + c;
                base->data[d_idx] = overlay->data[s_idx];
            }
        }
    }
    return 0;
}

void framebuffer_fill_gradient(framebuffer_t *fb,
                               const pixel_rgb_t *tl, const pixel_rgb_t *tr,
                               const pixel_rgb_t *bl, const pixel_rgb_t *br)
{
    if (!fb || !fb->data) return;
    for (uint32_t y = 0; y < fb->height; y++) {
        double vf = (fb->height > 1) ? (double)y / (fb->height - 1) : 0.0;
        for (uint32_t x = 0; x < fb->width; x++) {
            double hf = (fb->width > 1) ? (double)x / (fb->width - 1) : 0.0;
            pixel_rgb_t p;
            p.max_val = 255;
            for (int c = 0; c < 3; c++) {
                uint16_t vt, vb, va;
                if (c == 0) { vt = tl->r; vb = bl->r; va = tr->r; }
                else if (c == 1) { vt = tl->g; vb = bl->g; va = tr->g; }
                else { vt = tl->b; vb = bl->b; va = tr->b; }
                double v = vt * (1 - vf) + vb * vf;
                uint16_t vx = (uint16_t)(va * (1 - vf) + (c == 0 ? br->r : c == 1 ? br->g : br->b) * vf);
                int val = (int)(v * (1 - hf) + vx * hf + 0.5);
                if (val < 0) val = 0; if (val > 255) val = 255;
                if (c == 0) p.r = val; else if (c == 1) p.g = val; else p.b = val;
            }
            framebuffer_pixel_write(fb, x, y, &p);
        }
    }
}

void generate_smpte_color_bars(framebuffer_t *fb)
{
    if (!fb || !fb->data) return;
    /* Standard SMPTE color bars: white, yellow, cyan, green, magenta, red, blue */
    pixel_rgb_t colors[7] = {
        {255, 255, 255, 255, 255}, /* White */
        {255, 255, 0,   255, 255}, /* Yellow */
        {0,   255, 255, 255, 255}, /* Cyan */
        {0,   255, 0,   255, 255}, /* Green */
        {255, 0,   255, 255, 255}, /* Magenta */
        {255, 0,   0,   255, 255}, /* Red */
        {0,   0,   255, 255, 255}, /* Blue */
    };
    uint32_t bar_w = fb->width / 7;
    for (int i = 0; i < 7; i++) {
        uint32_t x0 = i * bar_w;
        uint32_t x1 = (i + 1) * bar_w;
        if (i == 6) x1 = fb->width;
        for (uint32_t y = 0; y < fb->height * 2 / 3; y++)
            for (uint32_t x = x0; x < x1; x++)
                framebuffer_pixel_write(fb, x, y, &colors[i]);
    }
    /* Bottom third: -I, white, +Q, black pattern */
    pixel_rgb_t black = {0, 0, 0, 255, 255};
    for (uint32_t y = fb->height * 2 / 3; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            framebuffer_pixel_write(fb, x, y, &black);
            if (x < fb->width / 4) {
                pixel_rgb_t i_neg = {0, 0, 160, 255, 255};
                framebuffer_pixel_write(fb, x, y, &i_neg);
            } else if (x < fb->width / 2) {
                pixel_rgb_t white = {255, 255, 255, 255, 255};
                framebuffer_pixel_write(fb, x, y, &white);
            }
        }
    }
}

void generate_gray_step_wedge(framebuffer_t *fb, int num_steps)
{
    if (!fb || !fb->data || num_steps < 2) return;
    uint32_t step_w = fb->width / num_steps;
    for (int i = 0; i < num_steps; i++) {
        uint8_t v = (uint8_t)(i * 255 / (num_steps - 1));
        pixel_rgb_t p = {v, v, v, 255, 255};
        uint32_t x0 = i * step_w;
        uint32_t x1 = (i + 1) * step_w;
        if (i == num_steps - 1) x1 = fb->width;
        framebuffer_fill_rect(fb, x0, 0, x1 - x0, fb->height, &p);
    }
}

void generate_zone_plate(framebuffer_t *fb, double max_freq)
{
    if (!fb || !fb->data) return;
    double cx = (fb->width - 1) / 2.0;
    double cy = (fb->height - 1) / 2.0;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            double dx = (x - cx) / cx;
            double dy = (y - cy) / cy;
            double r2 = dx * dx + dy * dy;
            double freq = r2 * max_freq;
            double v = 0.5 + 0.5 * cos(2.0 * PI * freq);
            uint8_t gv = (uint8_t)(v * 255.0 + 0.5);
            pixel_rgb_t p = {gv, gv, gv, 255, 255};
            framebuffer_pixel_write(fb, x, y, &p);
        }
    }
}

void generate_checkerboard(framebuffer_t *fb, int square_size)
{
    if (!fb || !fb->data || square_size <= 0) return;
    pixel_rgb_t white = {255, 255, 255, 255, 255};
    pixel_rgb_t black = {0, 0, 0, 255, 255};
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            int check = ((x / square_size) + (y / square_size)) % 2;
            framebuffer_pixel_write(fb, x, y, check ? &white : &black);
        }
    }
}

int framebuffer_rgb_to_gray(const framebuffer_t *rgb, framebuffer_t *gray)
{
    if (!rgb || !gray || !rgb->data || !gray->data) return -1;
    if (rgb->bytes_per_pixel < 3 || gray->bytes_per_pixel < 1) return -1;
    for (uint32_t y = 0; y < rgb->height; y++) {
        for (uint32_t x = 0; x < rgb->width; x++) {
            uint8_t *p = rgb->data + y * rgb->stride_bytes + x * rgb->bytes_per_pixel;
            double luma = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            gray->data[y * gray->stride_bytes + x] = (uint8_t)(luma + 0.5);
        }
    }
    return 0;
}

double framebuffer_psnr(const framebuffer_t *ref, const framebuffer_t *test)
{
    if (!ref || !test || !ref->data || !test->data) return 0.0;
    if (ref->width != test->width || ref->height != test->height) return 0.0;
    double mse = 0.0;
    uint64_t n = (uint64_t)ref->width * ref->height;
    for (uint32_t y = 0; y < ref->height; y++) {
        for (uint32_t x = 0; x < ref->width; x++) {
            double diff = ref->data[y * ref->stride_bytes + x]
                        - test->data[y * test->stride_bytes + x];
            mse += diff * diff;
        }
    }
    mse /= n;
    if (mse <= 0.0) return 100.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

double framebuffer_ssim(const framebuffer_t *ref, const framebuffer_t *test,
                        int window_size)
{
    if (!ref || !test || !ref->data || !test->data) return 0.0;
    /* Simplified 1D SSIM for grayscale: constant values */
    const double C1 = 6.5025, C2 = 58.5225;
    double mu_x = 0, mu_y = 0, sigma_x2 = 0, sigma_y2 = 0, sigma_xy = 0;
    uint64_t n = (uint64_t)ref->width * ref->height;
    for (uint32_t i = 0; i < n && i < ref->total_bytes; i++) {
        double xi = ref->data[i], yi = test->data[i];
        mu_x += xi; mu_y += yi;
    }
    mu_x /= n; mu_y /= n;
    for (uint32_t i = 0; i < n && i < ref->total_bytes; i++) {
        double dx = ref->data[i] - mu_x;
        double dy = test->data[i] - mu_y;
        sigma_x2 += dx * dx;
        sigma_y2 += dy * dy;
        sigma_xy += dx * dy;
    }
    sigma_x2 /= (n - 1); sigma_y2 /= (n - 1); sigma_xy /= (n - 1);

    double num = (2.0 * mu_x * mu_y + C1) * (2.0 * sigma_xy + C2);
    double den = (mu_x * mu_x + mu_y * mu_y + C1) * (sigma_x2 + sigma_y2 + C2);
    return (den > 0.0) ? num / den : 0.0;
}

uint8_t otsu_threshold(const image_histogram_t *hist)
{
    if (!hist || hist->total_pixels == 0) return 128;
    double total = hist->total_pixels;
    double sum_b = 0;
    double w_b = 0;
    double max_var = 0;
    uint8_t threshold = 0;

    double sum_total = 0;
    for (int i = 0; i < 256; i++) sum_total += i * hist->bins[i];

    for (int t = 0; t < 256; t++) {
        w_b += hist->bins[t];
        if (w_b == 0) continue;
        double w_f = total - w_b;
        if (w_f == 0) break;
        sum_b += t * hist->bins[t];
        double m_b = sum_b / w_b;
        double m_f = (sum_total - sum_b) / w_f;
        double var = w_b * w_f * (m_b - m_f) * (m_b - m_f);
        if (var > max_var) { max_var = var; threshold = (uint8_t)t; }
    }
    return threshold;
}

void framebuffer_binarize(framebuffer_t *fb, uint8_t threshold)
{
    if (!fb || !fb->data) return;
    for (uint32_t p = 0; p < fb->total_bytes; p++) {
        fb->data[p] = (fb->data[p] > threshold) ? 255 : 0;
    }
}


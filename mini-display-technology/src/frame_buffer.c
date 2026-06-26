/**
 * frame_buffer.c — Frame Buffer Operations
 *
 * Implements:
 *   L1: Frame buffer allocation and management
 *   L2: Pixel read/write, fill/clear, test patterns
 *   L5: Optimized blit with stride handling, page-flip emulation
 *   L6: End-to-end frame buffer integrity verification
 *
 * Reference:
 *   Poynton, "Digital Video and HD" (2012), Ch. 2-4
 *   Computer Graphics: Principles and Practice (Foley, 1996), Ch. 18
 *
 * L7 Applications:
 *   - DC motor driver H-bridge display for embedded motor controllers
 *   - Quadrotor drone FPV video frame buffer management
 *   - Beer brewery fermentation tank HMI display rendering
 *   - Prison security CCTV multi-display video wall
 *   - Easter egg: hidden test pattern for factory calibration
 *   - climate monitoring station outdoor display buffer management
 * L8 Advanced:
 *   - Markov blanket spatial prediction for frame buffer compression
 *   - agent-based display power management for multi-monitor systems
 *   DirectFB / Linux framebuffer API design
 */

#include "display_types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ==========================================================================
 * L1/L2: Frame Buffer Lifecycle
 * ========================================================================== */

int framebuffer_alloc(framebuffer_t *fb, uint32_t width, uint32_t height,
                      pixel_format_t format)
{
    if (!fb || width == 0 || height == 0) return -1;
    memset(fb, 0, sizeof(*fb));

    fb->width = width;
    fb->height = height;
    fb->format = format;
    fb->bytes_per_pixel = pixel_format_bytes(format);

    if (fb->bytes_per_pixel == 0) {
        /* Default to 3 bytes for unknown formats */
        fb->bytes_per_pixel = 3;
    }

    /* Align stride to 16 bytes for SIMD efficiency */
    fb->stride_bytes = ((width * fb->bytes_per_pixel + 15) / 16) * 16;
    fb->total_bytes = (size_t)fb->stride_bytes * height;

    fb->data = calloc(1, fb->total_bytes);
    if (!fb->data) {
        memset(fb, 0, sizeof(*fb));
        return -1;
    }

    /* Initialize to black */
    memset(fb->data, 0, fb->total_bytes);
    return 0;
}

void framebuffer_free(framebuffer_t *fb)
{
    if (fb) {
        free(fb->data);
        fb->data = NULL;
        fb->total_bytes = 0;
    }
}

/* ==========================================================================
 * L2: Pixel Access
 * ========================================================================== */

void framebuffer_clear(framebuffer_t *fb, const pixel_rgb_t *color)
{
    if (!fb || !fb->data) return;

    uint8_t r, g, b;
    if (color) {
        /* Scale from max_val to 0-255 */
        if (color->max_val > 0) {
            r = (uint8_t)((uint32_t)color->r * 255 / color->max_val);
            g = (uint8_t)((uint32_t)color->g * 255 / color->max_val);
            b = (uint8_t)((uint32_t)color->b * 255 / color->max_val);
        } else {
            r = g = b = 0;
        }
    } else {
        r = g = b = 0;
    }

    /* Fill row by row with the appropriate pattern */
    for (uint32_t y = 0; y < fb->height; y++) {
        uint8_t *row = fb->data + y * fb->stride_bytes;
        for (uint32_t x = 0; x < fb->width; x++) {
            uint8_t *pixel = row + x * fb->bytes_per_pixel;
            uint32_t bpp = fb->bytes_per_pixel;
            switch (bpp) {
                case 1:
                    pixel[0] = (uint8_t)((uint16_t)r * 77 + (uint16_t)g * 150 + (uint16_t)b * 28) >> 8;
                    break;
                    break;
                case 2:
                    pixel[0] = (uint8_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
                    pixel[1] = (uint8_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)) >> 8;
                    break;
                case 3:
                    pixel[0] = r; pixel[1] = g; pixel[2] = b;
                    break;
                case 4:
                    pixel[0] = r; pixel[1] = g; pixel[2] = b;
                    pixel[3] = 255;
                    break;
                case 6:
                    memset(pixel, r, 2);
                    memset(pixel + 2, g, 2);
                    memset(pixel + 4, b, 2);
                    break;
                default:
                    pixel[0] = r; if (fb->bytes_per_pixel > 1) pixel[1] = g;
                    if (fb->bytes_per_pixel > 2) pixel[2] = b;
                    break;
            }
        }
    }
}

int framebuffer_pixel_write(framebuffer_t *fb, uint32_t x, uint32_t y,
                            const pixel_rgb_t *p)
{
    if (!fb || !fb->data || !p || x >= fb->width || y >= fb->height) return -1;

    uint8_t *pixel = fb->data + y * fb->stride_bytes + x * fb->bytes_per_pixel;

    /* Scale from p->max_val to 0-255 */
    uint8_t r, g, b;
    if (p->max_val > 0) {
        r = (uint8_t)((uint32_t)p->r * 255 / p->max_val);
        g = (uint8_t)((uint32_t)p->g * 255 / p->max_val);
        b = (uint8_t)((uint32_t)p->b * 255 / p->max_val);
    } else {
        r = g = b = 0;
    }

    switch (fb->bytes_per_pixel) {
        case 1:
            pixel[0] = (uint8_t)(((uint16_t)r * 77 + (uint16_t)g * 150 + (uint16_t)b * 28) >> 8);
            break;
        case 2:
            pixel[0] = (uint8_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            break;
        case 3:
            pixel[0] = r; pixel[1] = g; pixel[2] = b;
            break;
        case 4:
            pixel[0] = r; pixel[1] = g; pixel[2] = b; pixel[3] = 255;
            break;
        case 6:
            memset(pixel, 0, 2);
            pixel[0] = r; pixel[1] = r >> 8;
            pixel[2] = g; pixel[3] = g >> 8;
            pixel[4] = b; pixel[5] = b >> 8;
            break;
        default:
            pixel[0] = r;
            if (fb->bytes_per_pixel > 1) pixel[1] = g;
            if (fb->bytes_per_pixel > 2) pixel[2] = b;
            break;
    }
    return 0;
}

int framebuffer_pixel_read(const framebuffer_t *fb, uint32_t x, uint32_t y,
                           pixel_rgb_t *p)
{
    if (!fb || !fb->data || !p || x >= fb->width || y >= fb->height) return -1;

    const uint8_t *pixel = fb->data + y * fb->stride_bytes + x * fb->bytes_per_pixel;
    p->max_val = 255;
    p->a = 255;

    switch (fb->bytes_per_pixel) {
        case 1:
            p->r = p->g = p->b = pixel[0];
            break;
        case 2: {
            uint16_t v = ((uint16_t)pixel[0] << 8) | pixel[1];
            p->r = ((v >> 11) & 0x1F) * 255 / 31;
            p->g = ((v >> 5) & 0x3F) * 255 / 63;
            p->b = (v & 0x1F) * 255 / 31;
            break;
        }
        case 3:
            p->r = pixel[0]; p->g = pixel[1]; p->b = pixel[2];
            break;
        case 4:
            p->r = pixel[0]; p->g = pixel[1]; p->b = pixel[2]; p->a = pixel[3];
            break;
        default:
            p->r = pixel[0];
            p->g = (fb->bytes_per_pixel > 1) ? pixel[1] : pixel[0];
            p->b = (fb->bytes_per_pixel > 2) ? pixel[2] : pixel[0];
            break;
    }
    return 0;
}

/* ==========================================================================
 * L2: High-Level Drawing Primitives
 * ========================================================================== */

void framebuffer_fill_rect(framebuffer_t *fb, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const pixel_rgb_t *color)
{
    if (!fb || !fb->data || !color) return;
    if (x >= fb->width || y >= fb->height) return;
    if (x + w > fb->width) w = fb->width - x;
    if (y + h > fb->height) h = fb->height - y;

    for (uint32_t row = y; row < y + h; row++) {
        for (uint32_t col = x; col < x + w; col++) {
            framebuffer_pixel_write(fb, col, row, color);
        }
    }
}

void framebuffer_gradient_h(framebuffer_t *fb,
                            double r_start, double r_end,
                            double g_start, double g_end,
                            double b_start, double b_end)
{
    if (!fb || !fb->data) return;
    if (fb->width <= 1) return;

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            double t = (double)x / (fb->width - 1);
            pixel_rgb_t p;
            p.max_val = 255;
            p.a = 255;
            p.r = (uint16_t)((r_start * (1.0 - t) + r_end * t) * 255.0 + 0.5);
            p.g = (uint16_t)((g_start * (1.0 - t) + g_end * t) * 255.0 + 0.5);
            p.b = (uint16_t)((b_start * (1.0 - t) + b_end * t) * 255.0 + 0.5);
            framebuffer_pixel_write(fb, x, y, &p);
        }
    }
}

void framebuffer_gray_ramp(framebuffer_t *fb, uint8_t bits)
{
    if (!fb || !fb->data || bits == 0) return;
    uint16_t max_val = (1 << bits) - 1;

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            /* Horizontal ramp: 0 to max_val across the width */
            double frac = (double)x / (fb->width > 1 ? fb->width - 1 : 1);
            uint16_t val = (uint16_t)(frac * max_val + 0.5);
            pixel_rgb_t p;
            p.max_val = max_val;
            p.r = p.g = p.b = val;
            p.a = max_val;
            framebuffer_pixel_write(fb, x, y, &p);
        }
    }
}

void framebuffer_test_pattern(framebuffer_t *fb)
{
    if (!fb || !fb->data) return;

    /* Color bars with standard test colors */
    pixel_rgb_t colors[8] = {
        {255, 255, 255, 255, 255}, /* White */
        {255, 255, 0,   255, 255}, /* Yellow */
        {0,   255, 255, 255, 255}, /* Cyan */
        {0,   255, 0,   255, 255}, /* Green */
        {255, 0,   255, 255, 255}, /* Magenta */
        {255, 0,   0,   255, 255}, /* Red */
        {0,   0,   255, 255, 255}, /* Blue */
        {0,   0,   0,   255, 255}, /* Black */
    };

    uint32_t bar_w = fb->width / 8;
    for (int i = 0; i < 8; i++) {
        uint32_t x0 = i * bar_w;
        uint32_t x1 = (i == 7) ? fb->width : (i + 1) * bar_w;
        framebuffer_fill_rect(fb, x0, 0, x1 - x0, fb->height * 3 / 4, &colors[i]);
    }

    /* Bottom quarter: grayscale steps */
    for (uint32_t y = fb->height * 3 / 4; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            uint8_t g = (uint8_t)(x * 255 / fb->width);
            pixel_rgb_t gray = {g, g, g, 255, 255};
            framebuffer_pixel_write(fb, x, y, &gray);
        }
    }
}

/* ==========================================================================
 * L2/L5: Blit Operations
 * ========================================================================== */

int framebuffer_blit(const framebuffer_t *src, framebuffer_t *dst,
                     uint32_t sx, uint32_t sy, uint32_t w, uint32_t h,
                     uint32_t dx, uint32_t dy)
{
    if (!src || !dst || !src->data || !dst->data) return -1;
    if (sx + w > src->width || sy + h > src->height) return -1;
    if (dx + w > dst->width || dy + h > dst->height) return -1;

    uint32_t bpp = (src->bytes_per_pixel < dst->bytes_per_pixel)
                   ? src->bytes_per_pixel : dst->bytes_per_pixel;

    /* Optimized: copy entire rows when stride and bpp match */
    int can_memcpy = (src->bytes_per_pixel == dst->bytes_per_pixel)
                  && (src->stride_bytes == dst->stride_bytes);

    for (uint32_t row = 0; row < h; row++) {
        const uint8_t *srow = src->data + (sy + row) * src->stride_bytes + sx * src->bytes_per_pixel;
        uint8_t *drow = dst->data + (dy + row) * dst->stride_bytes + dx * dst->bytes_per_pixel;

        if (can_memcpy) {
            memcpy(drow, srow, w * bpp);
        } else {
            /* Pixel-by-pixel copy with format conversion */
            for (uint32_t col = 0; col < w; col++) {
                const uint8_t *sp = srow + col * src->bytes_per_pixel;
                uint8_t *dp = drow + col * dst->bytes_per_pixel;
                memcpy(dp, sp, bpp);
                /* Zero remaining bytes if dst is wider */
                if (dst->bytes_per_pixel > src->bytes_per_pixel)
                    memset(dp + bpp, 0, dst->bytes_per_pixel - bpp);
            }
        }
    }
    return 0;
}

/* ==========================================================================
 * L2: Framebuffer Integrity
 * ========================================================================== */

/**
 * Compute a simple checksum using FNV-1a 32-bit hash.
 */
uint32_t framebuffer_checksum(const framebuffer_t *fb)
{
    if (!fb || !fb->data) return 0;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < fb->total_bytes; i++) {
        hash ^= fb->data[i];
        hash *= 16777619u;
    }
    return hash;
}

int framebuffer_verify(const framebuffer_t *fb, uint32_t expected_checksum)
{
    if (!fb || !fb->data) return 0;
    uint32_t actual = framebuffer_checksum(fb);
    return (actual == expected_checksum) ? 1 : 0;
}

/* ==========================================================================
 * L5: Frame Buffer Statistics
 * ========================================================================== */

/**
 * Compute mean pixel value (grayscale only).
 */
double framebuffer_mean(const framebuffer_t *fb)
{
    if (!fb || !fb->data || fb->total_bytes == 0) return 0.0;
    uint64_t sum = 0;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            sum += fb->data[y * fb->stride_bytes + x * fb->bytes_per_pixel];
        }
    }
    return (double)sum / (fb->width * fb->height);
}

/**
 * Compute frame variance.
 */
double framebuffer_variance(const framebuffer_t *fb)
{
    double mean = framebuffer_mean(fb);
    if (fb->width * fb->height <= 1) return 0.0;
    double sum_sq = 0.0;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            double v = fb->data[y * fb->stride_bytes + x * fb->bytes_per_pixel];
            sum_sq += (v - mean) * (v - mean);
        }
    }
    return sum_sq / (fb->width * fb->height - 1);
}

/**
 * Find minimum and maximum pixel values.
 */
void framebuffer_minmax(const framebuffer_t *fb, uint8_t *min_val, uint8_t *max_val)
{
    uint8_t mn = 255, mx = 0;
    if (fb && fb->data) {
        for (uint32_t y = 0; y < fb->height; y++) {
            for (uint32_t x = 0; x < fb->width; x++) {
                uint8_t v = fb->data[y * fb->stride_bytes + x * fb->bytes_per_pixel];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
        }
    }
    if (min_val) *min_val = mn;
    if (max_val) *max_val = mx;
}

/**
 * Count non-zero pixels.
 */
uint64_t framebuffer_count_nonzero(const framebuffer_t *fb)
{
    if (!fb || !fb->data) return 0;
    uint64_t count = 0;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            if (fb->data[y * fb->stride_bytes + x * fb->bytes_per_pixel] != 0)
                count++;
        }
    }
    return count;
}

/**
 * Flip framebuffer vertically (in-place).
 */
void framebuffer_flip_vertical(framebuffer_t *fb)
{
    if (!fb || !fb->data || fb->height <= 1) return;
    uint8_t *temp = malloc(fb->stride_bytes);
    if (!temp) return;

    for (uint32_t y = 0; y < fb->height / 2; y++) {
        uint32_t y2 = fb->height - 1 - y;
        uint8_t *row1 = fb->data + y * fb->stride_bytes;
        uint8_t *row2 = fb->data + y2 * fb->stride_bytes;
        memcpy(temp, row1, fb->stride_bytes);
        memcpy(row1, row2, fb->stride_bytes);
        memcpy(row2, temp, fb->stride_bytes);
    }
    free(temp);
}

/**
 * Flip framebuffer horizontally (in-place).
 */
void framebuffer_flip_horizontal(framebuffer_t *fb)
{
    if (!fb || !fb->data || fb->width <= 1) return;
    uint8_t *temp = malloc(fb->bytes_per_pixel);
    if (!temp) return;

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width / 2; x++) {
            uint32_t x2 = fb->width - 1 - x;
            uint8_t *p1 = fb->data + y * fb->stride_bytes + x * fb->bytes_per_pixel;
            uint8_t *p2 = fb->data + y * fb->stride_bytes + x2 * fb->bytes_per_pixel;
            memcpy(temp, p1, fb->bytes_per_pixel);
            memcpy(p1, p2, fb->bytes_per_pixel);
            memcpy(p2, temp, fb->bytes_per_pixel);
        }
    }
    free(temp);
}

/**
 * Rotate framebuffer 180° (in-place).
 */
void framebuffer_rotate_180(framebuffer_t *fb)
{
    framebuffer_flip_horizontal(fb);
    framebuffer_flip_vertical(fb);
}

/**
 * Apply brightness/contrast adjustment.
 *   out = (in - 128) * contrast + 128 + brightness
 */
void framebuffer_adjust_brightness_contrast(framebuffer_t *fb,
                                            double brightness,
                                            double contrast)
{
    if (!fb || !fb->data) return;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            for (uint32_t c = 0; c < fb->bytes_per_pixel && c < 3; c++) {
                uint8_t *p = fb->data + y * fb->stride_bytes + x * fb->bytes_per_pixel + c;
                double v = ((double)*p - 128.0) * contrast + 128.0 + brightness;
                int iv = (int)(v + 0.5);
                if (iv < 0) iv = 0; if (iv > 255) iv = 255;
                *p = (uint8_t)iv;
            }
        }
    }
}


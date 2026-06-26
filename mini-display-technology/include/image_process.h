/**
 * image_process.h — Image Processing for Display Pipeline
 *
 * L1 Definitions: Scale kernel, dither matrix, histogram, resampling filters
 * L2 Core Concepts: Scaling algorithms, dithering methods, sharpening,
 *                   histogram equalization, frame composition
 * L3 Mathematical Structures: 2D convolution kernels, separable filters,
 *                             error diffusion matrices, cumulative histograms
 * L5 Algorithms: Bilinear/bicubic/Lanczos scaling, Floyd-Steinberg dithering,
 *                CLAHE, unsharp mask, Wiener deconvolution
 *
 * Reference:
 *   Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
 *   Gonzalez & Woods, "Digital Image Processing" (2018)
 *   Keys, "Cubic Convolution Interpolation for Digital Image Processing" (1981)
 *   Floyd & Steinberg, "An Adaptive Algorithm for Spatial Grayscale" (1976)
 *   Duchon, "Lanczos Filtering in One and Two Dimensions" (1979)
 *
 * Course Mapping:
 *   MIT 6.003 — Signal Processing (sampling rate conversion, filter design)
 *   Stanford EE102A — Signal Processing (resampling, interpolation)
 *   Berkeley EE123 — DSP (2D filtering, image enhancement)
 *   Illinois ECE 310 — DSP (multirate systems, decimation/interpolation)
 *   Michigan EECS 351 — DSP (image processing fundamentals)
 */

#ifndef IMAGE_PROCESS_H
#define IMAGE_PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include "display_types.h"
#include "color_science.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Image Processing Structures
 * ========================================================================== */

/** Scaling / resampling interpolation method */
typedef enum {
    SCALE_NEAREST    = 0, /**< Nearest-neighbor (pixel replication) */
    SCALE_BILINEAR   = 1, /**< Bilinear interpolation */
    SCALE_BICUBIC    = 2, /**< Bicubic (Keys α=-0.5) */
    SCALE_LANCZOS2   = 3, /**< Lanczos-2 (a=2, 4 taps) */
    SCALE_LANCZOS3   = 4, /**< Lanczos-3 (a=3, 6 taps) */
    SCALE_BOX        = 5  /**< Box/average (downsampling antialias) */
} scale_method_t;

/** Dithering algorithm type */
typedef enum {
    DITHER_FLOYD_STEINBERG = 0, /**< Floyd-Steinberg error diffusion */
    DITHER_ATKINSON        = 1, /**< Atkinson (Bill Atkinson) dither */
    DITHER_ORDERED_BAYER   = 2, /**< Ordered dither with Bayer matrix */
    DITHER_CLUSTERED_DOT   = 3, /**< Clustered-dot halftone */
    DITHER_JARVIS_JUDICE   = 4, /**< Jarvis-Judice-Ninke diffusion */
    DITHER_STUCKI          = 5, /**< Stucki error diffusion */
    DITHER_NOISE           = 6  /**< Simple white noise dither */
} dither_method_t;

/** 2D convolution kernel */
typedef struct {
    double  *weights;   /**< Kernel weights (row-major) */
    int      rows;      /**< Kernel height */
    int      cols;      /**< Kernel width */
    double   sum;       /**< Precomputed sum of weights */
    int      origin_row; /**< Row index of kernel origin */
    int      origin_col; /**< Col index of kernel origin */
} conv_kernel2d_t;

/** Image histogram */
typedef struct {
    uint32_t bins[256];     /**< Count per 8-bit bin */
    uint32_t total_pixels;  /**< Total pixels counted */
    double   mean;          /**< Mean value */
    double   variance;      /**< Variance */
    double   std_dev;       /**< Standard deviation */
    double   min_val;       /**< Minimum value observed */
    double   max_val;       /**< Maximum value observed */
} image_histogram_t;

/** Cumulative distribution function (for histogram equalization) */
typedef struct {
    double cdf[256];       /**< Cumulative probability [0, 1] */
    uint8_t lut[256];      /**< Equalization lookup table */
    double min_cdf_nonzero; /**< Minimum nonzero CDF value */
} histogram_cdf_t;

/** CLAHE (Contrast Limited Adaptive Histogram Equalization) parameters */
typedef struct {
    int    tile_rows;      /**< Number of tiles vertically */
    int    tile_cols;      /**< Number of tiles horizontally */
    double clip_limit;     /**< Contrast limit (0 = no limit, typ. 0.01) */
    int    num_bins;       /**< Number of histogram bins (typ. 256) */
} clahe_params_t;

/** Error diffusion state */
typedef struct {
    dither_method_t method;
    double *error_buf;     /**< Per-pixel error accumulator (row) */
    int     width;         /**< Width of error buffer */
} error_diffuser_t;

/** Separable 1D filter kernel (for vertical/horizontal passes) */
typedef struct {
    double *weights;
    int     radius;        /**< Half-width: kernel length = 2*radius+1 */
    double  sum;
} separable_kernel_t;

/* ==========================================================================
 * L2 / L5: Image Scaling API
 * ========================================================================== */

/**
 * Scale a framebuffer to a new size.
 *
 * Horizontal and vertical scaling can use different methods.
 * For downsampling, a low-pass prefilter is applied automatically.
 *
 * @param src   Source framebuffer
 * @param dst   Destination framebuffer (pre-allocated at target size)
 * @param h_method Horizontal scaling method
 * @param v_method Vertical scaling method
 * @return 0 on success, -1 on error
 */
int image_scale(const framebuffer_t *src, framebuffer_t *dst,
                scale_method_t h_method, scale_method_t v_method);

/**
 * Scale a grayscale buffer using separable filtering.
 * More efficient than full 2D convolution for large images.
 */
int image_scale_separable(const framebuffer_t *src, framebuffer_t *dst,
                          separable_kernel_t *kernel);

/**
 * Compute a bicubic interpolation weight (Keys family).
 * @param t   distance from sample point (|t| < 2 for a=-0.5)
 * @param a   Keys parameter: -0.5 (Catmull-Rom), -0.75, -1.0
 * @return weight
 */
double bicubic_weight(double t, double a);

/** Compute a Lanczos windowed-sinc weight of order a */
double lanczos_weight(double t, int a);

/** Create a separable Lanczos-a kernel (allocates memory, caller frees) */
separable_kernel_t *separable_kernel_lanczos(int a, double scale_ratio);

/** Create a separable bicubic kernel */
separable_kernel_t *separable_kernel_bicubic(double scale_ratio);

/** Free a separable kernel */
void separable_kernel_free(separable_kernel_t *k);

/* ==========================================================================
 * L5: Dithering / Halftoning
 * ========================================================================== */

/**
 * Dither a framebuffer from higher bit depth to lower.
 * Reduces color depth (e.g., 24-bit → 8-bit, or 10-bit → 8-bit) with
 * spatial/temporal noise shaping to preserve perceptual detail.
 */
int image_dither(const framebuffer_t *src, framebuffer_t *dst,
                 dither_method_t method, uint8_t target_bits_per_channel);

/**
 * Apply Floyd-Steinberg error diffusion to a single channel.
 * Operates on float values [0,1], quantizes to output_bits levels.
 */
void dither_floyd_steinberg_apply(double *channel, int width, int height,
                                  int output_levels, int stride);

/** Generate Bayer ordered dither matrix of size 2^n */
void bayer_matrix_generate(uint8_t *matrix, int n);

/** Generate clustered-dot halftone screen */
void clustered_dot_screen_generate(double *screen, int size);

/** Initialize error diffuser state */
void error_diffuser_init(error_diffuser_t *ed, dither_method_t method,
                         int width);

/** Free error diffuser state */
void error_diffuser_free(error_diffuser_t *ed);

/* ==========================================================================
 * L5: Histogram Operations
 * ========================================================================== */

/** Compute histogram of a grayscale framebuffer (pixel values 0–255) */
void histogram_compute(const framebuffer_t *fb, image_histogram_t *hist);

/** Compute histogram statistics (mean, variance, etc.) */
void histogram_stats_compute(image_histogram_t *hist);

/** Compute CDF from histogram */
void histogram_cdf_compute(const image_histogram_t *hist, histogram_cdf_t *cdf);

/** Apply histogram equalization LUT to framebuffer */
void histogram_equalize(framebuffer_t *fb, const histogram_cdf_t *cdf);

/**
 * CLAHE — Contrast Limited Adaptive Histogram Equalization.
 *
 * Divides image into tiles, equalizes each independently with contrast
 * limiting, and uses bilinear interpolation at tile boundaries.
 *
 * Reference: Zuiderveld, "Contrast Limited Adaptive Histogram Equalization"
 *            in Graphics Gems IV (1994), pp. 474-485.
 */
int clahe_apply(framebuffer_t *fb, const clahe_params_t *params);

/* ==========================================================================
 * L2 / L5: 2D Convolution / Filtering
 * ========================================================================== */

/**
 * Apply a 2D convolution kernel to a framebuffer (grayscale only).
 * Uses "same" output size with zero-padding at borders.
 */
int image_conv2d(const framebuffer_t *src, framebuffer_t *dst,
                 const conv_kernel2d_t *kernel);

/** Initialize a 2D convolution kernel (allocates weight array) */
conv_kernel2d_t *conv_kernel2d_alloc(int rows, int cols,
                                      int origin_row, int origin_col);

/** Free a 2D convolution kernel */
void conv_kernel2d_free(conv_kernel2d_t *k);

/* ==========================================================================
 * L5: Image Enhancement Filters
 * ========================================================================== */

/** Generate a Gaussian blur kernel of given sigma */
conv_kernel2d_t *kernel_gaussian_blur(double sigma, int size);

/** Generate an unsharp mask kernel (Laplacian-based sharpening) */
conv_kernel2d_t *kernel_unsharp_mask(double strength);

/** Generate a Sobel edge detection kernel (Gx or Gy) */
conv_kernel2d_t *kernel_sobel(int gradient_x);

/** Generate a 3×3 box blur kernel (simple averaging) */
conv_kernel2d_t *kernel_box_blur(int size);

/** Apply sharpening filter to framebuffer using unsharp masking */
int image_sharpen(framebuffer_t *fb, double strength);

/** Apply Gaussian blur to framebuffer */
int image_blur_gaussian(framebuffer_t *fb, double sigma);

/* ==========================================================================
 * L2: Frame Operations (Compositing/Blending)
 * ========================================================================== */

/**
 * Alpha-blend two framebuffers: dst = src * alpha + dst * (1 - alpha)
 * Assumes premultiplied alpha in source.
 */
int framebuffer_alpha_blend(const framebuffer_t *src, framebuffer_t *dst,
                            uint32_t dx, uint32_t dy, double alpha);

/**
 * Overlay a smaller framebuffer onto a larger one at position (x, y).
 * With per-pixel alpha from source.
 */
int framebuffer_overlay(const framebuffer_t *overlay, framebuffer_t *base,
                        uint32_t x, uint32_t y);

/** Generate a horizontal color gradient framebuffer */
void framebuffer_fill_gradient(framebuffer_t *fb,
                               const pixel_rgb_t *top_left,
                               const pixel_rgb_t *top_right,
                               const pixel_rgb_t *bottom_left,
                               const pixel_rgb_t *bottom_right);

/** Generate a standard SMPTE color bars test pattern */
void generate_smpte_color_bars(framebuffer_t *fb);

/** Generate a grayscale step wedge (for gamma calibration) */
void generate_gray_step_wedge(framebuffer_t *fb, int num_steps);

/** Generate a zone plate (circular frequency sweep) test pattern */
void generate_zone_plate(framebuffer_t *fb, double max_freq);

/** Generate a full-frame pixel checkerboard */
void generate_checkerboard(framebuffer_t *fb, int square_size);

/** Convert an RGB framebuffer to grayscale (BT.709 luma coefficients) */
int framebuffer_rgb_to_gray(const framebuffer_t *rgb, framebuffer_t *gray);

/** Compute PSNR between two framebuffers (in dB) */
double framebuffer_psnr(const framebuffer_t *ref, const framebuffer_t *test);

/** Compute SSIM (Structural Similarity) window between two framebuffers */
double framebuffer_ssim(const framebuffer_t *ref, const framebuffer_t *test,
                        int window_size);

/** Compute Otsu's threshold for automatic binarization */
uint8_t otsu_threshold(const image_histogram_t *hist);

/** Binarize a grayscale framebuffer using a threshold */
void framebuffer_binarize(framebuffer_t *fb, uint8_t threshold);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_PROCESS_H */


#include "hdr_tone_mapping.h"
#include <stdio.h>
#include <math.h>

/* ?? Reinhard global defaults ????????????????????????????????????????? */
static const double REINHARD_DEFAULT_KEY = 0.18;
static const double REINHARD_DEFAULT_WHITE = 1.5;
static const double DRAGO_DEFAULT_BIAS = 0.85;

void tmo_config_init(tmo_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->method = TMO_REINHARD_GLOBAL;
    config->color_strategy = TMO_COLOR_LUMINANCE_ONLY;
    config->auto_exposure = 1;
    config->key_value = REINHARD_DEFAULT_KEY;
    config->target_peak = 100.0;
    config->saturation = 1.0;
    config->contrast_boost = 0.0;
    config->shadow_boost = 0.0;
    config->highlight_rolloff = 0.0;
    config->gamma_correction = 2.2;
}

/* ?? Image Buffer Allocation ?????????????????????????????????????????? */

hdr_image_buffer_t *hdr_image_alloc(int width, int height, double peak_nits, hdr_primaries_t primaries)
{
    if (width <= 0 || height <= 0) return NULL;
    hdr_image_buffer_t *img = (hdr_image_buffer_t *)calloc(1, sizeof(*img));
    if (!img) return NULL;
    img->pixels = (hdr_rgb_pixel_t *)calloc((size_t)(width * height), sizeof(hdr_rgb_pixel_t));
    if (!img->pixels) { free(img); return NULL; }
    img->width = width;
    img->height = height;
    img->peak_nits = peak_nits;
    img->primaries = primaries;
    return img;
}

void hdr_image_free(hdr_image_buffer_t *image)
{
    if (!image) return;
    if (image->pixels) { free(image->pixels); image->pixels = NULL; }
    free(image);
}

void hdr_image_fill(hdr_image_buffer_t *image, double r, double g, double b)
{
    if (!image || !image->pixels) return;
    int total = image->width * image->height;
    for (int i = 0; i < total; i++) {
        image->pixels[i].r = r;
        image->pixels[i].g = g;
        image->pixels[i].b = b;
    }
}

hdr_image_buffer_t *hdr_image_create_test_pattern(int width, int height, double max_nits)
{
    hdr_image_buffer_t *img = hdr_image_alloc(width, height, max_nits, HDR_PRIMARIES_BT2020);
    if (!img) return NULL;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double fx = (double)x / (double)width;
            double fy = (double)y / (double)height;

            /* Horizontal gradient: black to max_nits */
            double gray = fx * max_nits;

            /* Vertical color patches */
            if (fy < 0.2) {
                /* Red patch */
                img->pixels[y * width + x].r = gray;
                img->pixels[y * width + x].g = 0.0;
                img->pixels[y * width + x].b = 0.0;
            } else if (fy < 0.4) {
                /* Green patch */
                img->pixels[y * width + x].r = 0.0;
                img->pixels[y * width + x].g = gray;
                img->pixels[y * width + x].b = 0.0;
            } else if (fy < 0.6) {
                /* Blue patch */
                img->pixels[y * width + x].r = 0.0;
                img->pixels[y * width + x].g = 0.0;
                img->pixels[y * width + x].b = gray;
            } else if (fy < 0.8) {
                /* Gray patch */
                img->pixels[y * width + x].r = gray;
                img->pixels[y * width + x].g = gray;
                img->pixels[y * width + x].b = gray;
            } else {
                /* White patch */
                double w = fx * max_nits * 0.5 + max_nits * 0.5;
                img->pixels[y * width + x].r = w;
                img->pixels[y * width + x].g = w;
                img->pixels[y * width + x].b = w;
            }
        }
    }
    return img;
}

/* ?? Scene Analysis ??????????????????????????????????????????????????? */

/**
 * Compute the luminance Y from linear RGB (BT.2020 coefficients).
 */
static double rgb_to_luminance(const hdr_rgb_pixel_t *pix)
{
    return 0.2627 * pix->r + 0.6780 * pix->g + 0.0593 * pix->b;
}

void tmo_analyze_scene(const hdr_image_buffer_t *image, tmo_scene_analysis_t *analysis)
{
    if (!image || !image->pixels || !analysis) return;
    memset(analysis, 0, sizeof(*analysis));

    int total = image->width * image->height;
    if (total <= 0) return;

    /* Compute luminances and find min/max */
    double log_sum = 0.0;
    double geom_sum_log = 0.0;
    double lum_min = 1e30;
    double lum_max = -1e30;
    int valid_count = 0;

    /* We need sorted luminances for percentile computation.
     * For efficiency, use a histogram-based approximation. */
    hdr_luminance_histogram_t *hist = hdr_histogram_create(256, -4.0, 4.0);
    if (!hist) return;

    for (int i = 0; i < total; i++) {
        double L = rgb_to_luminance(&image->pixels[i]);
        if (L <= 0.0) continue;
        valid_count++;
        log_sum += log10(L);
        geom_sum_log += log(L);
        if (L < lum_min) lum_min = L;
        if (L > lum_max) lum_max = L;
        hdr_histogram_add(hist, L);
    }

    if (valid_count == 0) { hdr_histogram_destroy(hist); return; }

    analysis->log_average_luminance = log_sum / (double)valid_count;
    analysis->geometric_mean = exp(geom_sum_log / (double)valid_count);
    analysis->min_luminance = lum_min;
    analysis->max_luminance = lum_max;
    analysis->dynamic_range_stops = log2(lum_max / (lum_min > 0.0 ? lum_min : 0.0001));

    hdr_histogram_compute_percentiles(hist);
    analysis->percentile_01 = hdr_metadata_compute_maxcll(hist, 0.01);
    analysis->percentile_50 = hist->percentile_50;
    analysis->percentile_99 = hist->percentile_99;

    /* Shadow/highlight clipping suggestions */
    analysis->shadow_clip = analysis->percentile_01 * 0.5;
    analysis->highlight_clip = analysis->percentile_99 * 0.95;

    hdr_histogram_destroy(hist);
}

double tmo_compute_key(double log_average, double burn_pct)
{
    if (burn_pct <= 0.0) burn_pct = 0.5;
    double avg_lum = exp(log_average);
    return avg_lum / (2.0 * burn_pct);
}

/* ?? Reinhard Global Tone Mapping ????????????????????????????????????? */

double tmo_reinhard_global(double L_world, double L_white)
{
    if (L_world <= 0.0) return 0.0;
    if (L_white <= 0.0) L_white = REINHARD_DEFAULT_WHITE;

    /* L_d = L_w / (1 + L_w) * (1 + L_w / L_white^2)  */
    double L_w_scaled = L_world;
    double numerator = L_w_scaled * (1.0 + L_w_scaled / (L_white * L_white));
    double denominator = 1.0 + L_w_scaled;
    double L_d = numerator / denominator;

    if (L_d > 1.0) L_d = 1.0;
    return L_d;
}

/* ?? Drago Logarithmic Tone Mapping ??????????????????????????????????? */

double tmo_drago_log(double L_w, double L_max, double bias)
{
    if (L_w <= 0.0) return 0.0;
    if (L_max <= 0.0) L_max = 1.0;
    if (bias <= 0.0 || bias > 1.0) bias = DRAGO_DEFAULT_BIAS;

    double log_L_w = log10(1.0 + L_w);
    double log_L_max = log10(1.0 + L_max);

    double numerator = log_L_w;
    double denominator = log_L_max;

    /* Bias term */
    double bias_exp = (log_L_w - log10(1.0 + L_w)) / log10(0.5);
    double bias_term = pow(bias, bias_exp);

    double L_d = (numerator / denominator) * bias_term;

    if (L_d > 1.0) L_d = 1.0;
    return L_d;
}

/* ?? Core Tone Mapping Application ???????????????????????????????????? */

hdr_rgb_pixel_t *tmo_apply(const hdr_image_buffer_t *image, const tmo_config_t *config, int *out_width, int *out_height)
{
    if (!image || !image->pixels || !config || !out_width || !out_height) return NULL;

    int w = image->width;
    int h = image->height;
    int total = w * h;

    tmo_scene_analysis_t analysis;
    tmo_analyze_scene(image, &analysis);

    hdr_rgb_pixel_t *output = (hdr_rgb_pixel_t *)calloc((size_t)total, sizeof(hdr_rgb_pixel_t));
    if (!output) return NULL;

    *out_width = w;
    *out_height = h;

    double key = config->key_value;
    if (config->auto_exposure) {
        key = tmo_compute_key(analysis.log_average_luminance, 0.5);
    }

    for (int i = 0; i < total; i++) {
        hdr_rgb_pixel_t pix = image->pixels[i];

        if (config->color_strategy == TMO_COLOR_LUMINANCE_ONLY) {
            /* Compute luminance, compress it, then scale RGB */
            double L = rgb_to_luminance(&pix);
            if (L <= 0.0) {
                output[i].r = 0.0;
                output[i].g = 0.0;
                output[i].b = 0.0;
                continue;
            }

            double L_world = (key / analysis.geometric_mean) * L;
            double L_display = 0.0;

            switch (config->method) {
            case TMO_REINHARD_GLOBAL:
                L_display = tmo_reinhard_global(L_world, 4.0);
                break;
            case TMO_DRAGO_LOGARITHMIC:
                L_display = tmo_drago_log(L, analysis.max_luminance, 0.85);
                break;
            case TMO_LINEAR_CLIP:
            default:
                L_display = L / analysis.max_luminance;
                if (L_display > 1.0) L_display = 1.0;
                break;
            }

            /* Scale RGB by the compression ratio */
            double scale = (L > 0.0) ? (L_display / (L / analysis.max_luminance)) * (analysis.max_luminance / config->target_peak) : 0.0;

            /* Apply saturation adjustment */
            double sat = config->saturation;
            double gray = (pix.r + pix.g + pix.b) / 3.0;
            double dr = pix.r - gray;
            double dg = pix.g - gray;
            double db = pix.b - gray;

            output[i].r = (gray + sat * dr) * scale;
            output[i].g = (gray + sat * dg) * scale;
            output[i].b = (gray + sat * db) * scale;

            /* Clamp */
            if (output[i].r > 1.0) output[i].r = 1.0;
            if (output[i].g > 1.0) output[i].g = 1.0;
            if (output[i].b > 1.0) output[i].b = 1.0;
            if (output[i].r < 0.0) output[i].r = 0.0;
            if (output[i].g < 0.0) output[i].g = 0.0;
            if (output[i].b < 0.0) output[i].b = 0.0;
        } else {
            /* Naive per-channel compression */
            double max_chan = pix.r;
            if (pix.g > max_chan) max_chan = pix.g;
            if (pix.b > max_chan) max_chan = pix.b;
            if (max_chan <= 0.0) {
                output[i].r = output[i].g = output[i].b = 0.0;
                continue;
            }
            double L_display = tmo_reinhard_global(max_chan / analysis.max_luminance, 4.0);
            double scale = (max_chan > 0.0) ? L_display / (max_chan / analysis.max_luminance) : 0.0;
            output[i].r = pix.r * scale / config->target_peak;
            output[i].g = pix.g * scale / config->target_peak;
            output[i].b = pix.b * scale / config->target_peak;
            if (output[i].r > 1.0) output[i].r = 1.0;
            if (output[i].g > 1.0) output[i].g = 1.0;
            if (output[i].b > 1.0) output[i].b = 1.0;
        }
    }

    return output;
}

/* ?? ITU-R BT.2446-1 Method A: PQ HDR to SDR ???????????????????????? */

double tmo_bt2446_method_a(double pq_signal, double display_peak)
{
    if (pq_signal <= 0.0) return 0.0;
    if (pq_signal >= 1.0) return 1.0;

    /* BT.2446 Method A uses a piecewise curve with 3 knees.
     * This is a simplified implementation:
     *
     * - Shadow knee: linear scaling up to ~0.15 PQ
     * - Mid-tone knee: smooth transition
     * - Highlight knee: compression above ~0.6 PQ
     *
     * The output is mapped to BT.1886 SDR space.
     */
    double sdr_signal;

    if (pq_signal < 0.15) {
        /* Shadow knee: lift and stretch */
        sdr_signal = pq_signal * 1.5;
    } else if (pq_signal < 0.60) {
        /* Mid-tone: smooth S-curve */
        double t = (pq_signal - 0.15) / 0.45;
        /* Smoothstep for natural transition */
        double smooth = t * t * (3.0 - 2.0 * t);
        double start_val = 0.15 * 1.5;
        double end_val = 0.75;
        sdr_signal = start_val + (end_val - start_val) * smooth;
    } else {
        /* Highlight knee: soft compression */
        double t = (pq_signal - 0.60) / 0.40;
        double start_val = 0.75;
        /* Logarithmic compression towards 1.0 */
        sdr_signal = start_val + 0.25 * (1.0 - exp(-3.0 * t));
    }

    /* Scale by display peak */
    double peak_factor = display_peak / 1000.0;
    sdr_signal *= peak_factor;
    if (sdr_signal > 1.0) sdr_signal = 1.0;

    return sdr_signal;
}

/* ?? ITU-R BT.2446-1 Method B: HLG HDR to SDR ???????????????????????? */

double tmo_bt2446_method_b(double hlg_signal, double gain)
{
    if (hlg_signal <= 0.0) return 0.0;
    if (hlg_signal >= 1.0) return 1.0;

    /* Method B applies user gain adjustment (in dB-equivalent)
     * then maps through a gamma-like OOTF.
     *
     * gain_dB = pow(10, gain / 20)  (linear gain from dB)
     */

    double linear_gain = pow(10.0, gain / 20.0);
    if (linear_gain < 1e-6) linear_gain = 1e-6;
    if (linear_gain > 1e6) linear_gain = 1e6;

    /* Apply gain to HLG signal */
    double adjusted = hlg_signal * linear_gain;

    /* Display rendering: inverse OETF + OOTF
     * For simplicity, use a gamma mapping from HLG to SDR */
    double E;
    double threshold = sqrt(3.0 / 12.0);
    if (adjusted <= threshold) {
        E = (adjusted * adjusted) / 3.0;
    } else {
        /* Approximate inverse HLG */
        E = adjusted;
    }

    /* Apply display gamma for SDR */
    double sdr = pow(E, 1.0 / 2.2);
    if (sdr > 1.0) sdr = 1.0;

    return sdr;
}

/* ?? Bilateral Filter ????????????????????????????????????????????????? */

/**
 * Gaussian function: exp(-x^2 / (2 * sigma^2)).
 */
static double gaussian(double x, double sigma)
{
    return exp(-0.5 * (x * x) / (sigma * sigma));
}

void tmo_bilateral_filter(const double *src, double *dst, int width, int height, const tmo_bilateral_params_t *params)
{
    if (!src || !dst || !params || width <= 0 || height <= 0) return;

    double spatial_sigma = params->spatial_sigma;
    double range_sigma = params->range_sigma;
    int kernel_radius = params->kernel_size / 2;
    if (kernel_radius < 1) kernel_radius = (int)(3.0 * spatial_sigma);
    if (kernel_radius < 1) kernel_radius = 1;

    int step = (int)params->sampling_ratio;
    if (step < 1) step = 1;

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            double center_val = src[y * width + x];
            double sum_weight = 0.0;
            double sum_val = 0.0;

            /* Weighted sum over kernel */
            int y_min = y - kernel_radius;
            int y_max = y + kernel_radius;
            int x_min = x - kernel_radius;
            int x_max = x + kernel_radius;

            if (y_min < 0) y_min = 0;
            if (y_max >= height) y_max = height - 1;
            if (x_min < 0) x_min = 0;
            if (x_max >= width) x_max = width - 1;

            for (int ky = y_min; ky <= y_max; ky += step) {
                for (int kx = x_min; kx <= x_max; kx += step) {
                    double spatial_dist = sqrt((double)((kx - x) * (kx - x) + (ky - y) * (ky - y)));
                    double range_dist;

                    if (params->use_log_domain) {
                        double log_center = log10(center_val + 1e-10);
                        double log_neighbor = log10(src[ky * width + kx] + 1e-10);
                        range_dist = fabs(log_center - log_neighbor);
                    } else {
                        range_dist = fabs(center_val - src[ky * width + kx]);
                    }

                    double w_spatial = gaussian(spatial_dist, spatial_sigma);
                    double w_range = gaussian(range_dist, range_sigma);
                    double weight = w_spatial * w_range;

                    sum_weight += weight;
                    sum_val += weight * src[ky * width + kx];
                }
            }

            /* Fill the output block with the filtered value */
            double filtered = (sum_weight > 0.0) ? (sum_val / sum_weight) : center_val;
            int fill_y_max = y + step;
            int fill_x_max = x + step;
            if (fill_y_max > height) fill_y_max = height;
            if (fill_x_max > width) fill_x_max = width;

            for (int fy = y; fy < fill_y_max; fy++) {
                for (int fx = x; fx < fill_x_max; fx++) {
                    dst[fy * width + fx] = filtered;
                }
            }
        }
    }
}

/* ?? Base/Detail Decomposition ???????????????????????????????????????? */

void tmo_base_detail_decompose(const double *src, double *base, double *detail, int width, int height, const tmo_bilateral_params_t *params)
{
    if (!src || !base || !detail || width <= 0 || height <= 0) return;

    int total = width * height;

    /* 1. Extract base layer via bilateral filter */
    tmo_bilateral_filter(src, base, width, height, params);

    /* 2. Compute detail layer: detail = log(src) - log(base)
     *    (operating in log domain for multiplicative decomposition) */
    for (int i = 0; i < total; i++) {
        double safe_src = (src[i] > 1e-10) ? src[i] : 1e-10;
        double safe_base = (base[i] > 1e-10) ? base[i] : 1e-10;
        detail[i] = log10(safe_src) - log10(safe_base);
    }
}

/* ?? Additional Utility: Luminance to Perceived Lightness (JND scale) ? */

/**
 * Convert absolute luminance to a perceptually uniform scale.
 *
 * Uses a L* approximation (CIE 1976) but mapped to [0, 1].
 *
 * @param luminance  Absolute luminance in cd/m2.
 * @param white_ref  Reference white luminance in cd/m2.
 * @return           Perceptual lightness in [0, 1].
 */
double tmo_luminance_to_lightness(double luminance, double white_ref)
{
    if (white_ref <= 0.0) white_ref = 100.0;
    double Y = luminance / white_ref;
    double delta = 6.0 / 29.0;
    if (Y > delta * delta * delta) {
        return 1.16 * pow(Y, 1.0 / 3.0) - 0.16;
    } else {
        return Y / (3.0 * delta * delta);
    }
}

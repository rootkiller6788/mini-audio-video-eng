/**
 * gamma_calibration.c — Display Gamma Calibration and Tone Mapping
 *
 * Implements:
 *   L1: Gamma LUT (lookup table), tone curve parameters
 *   L2: Gamma calibration from stepped luminance measurement
 *   L4: Weber-Fechner law (basis for gamma encoding)
 *   L5: Gamma curve fitting, SDR-to-HDR tone mapping algorithms
 *   L6: End-to-end calibration: measure → fit → verify
 *   L8: Advanced HDR tone mapping (Reinhard, Hable, ACES)
 *
 * Reference:
 *   Poynton, "Digital Video and HD" (2012), Ch. 27-30
 *   ITU-R BT.1886 — Reference Electro-Optical Transfer Function
 *   SMPTE ST 2084 — High Dynamic Range EOTF
 *   Reinhard et al., "Photographic Tone Reproduction for Digital Images" (2002)
 *   Hable, "Uncharted 2: HDR Lighting" (2010)
 *   ACES — Academy Color Encoding System (S-2014-003)
 *
 * L7 Applications:
 *   - Tesla infotainment display calibration (ISO 16505 HMI standards)
 *   - SpaceX Dragon capsule crew display HDR rendering
 *   - NASA mission control display color management
 *   - Boeing 787 cockpit display gamma specification
 * L8 Advanced:
 *   - Monte Carlo path tracing for display light simulation
 *   - Adaptive luminance control for automotive HUD
 */

#include "display_types.h"
#include "color_science.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ==========================================================================
 * L1: Gamma Lookup Table
 * ========================================================================== */

/** 1D gamma LUT: maps input [0, 255] to output [0, 65535] (16-bit precision) */
typedef struct {
    uint16_t lut[256];       /**< Lookup table entries */
    double   gamma;          /**< Fitted gamma value */
    double   black_offset;   /**< Black level offset (BT.1886) */
    double   peak_luminance; /**< Peak white luminance */
    int      size;           /**< Number of entries (256) */
} gamma_lut_t;

/** Multi-channel 3D LUT entry */
typedef struct {
    uint16_t r, g, b;  /**< Output values */
} lut3d_entry_t;

/** 3D color LUT for advanced color management */
typedef struct {
    lut3d_entry_t *table;  /**< Size³ entries */
    int            size;   /**< Grid size per dimension (e.g., 17, 33) */
} color_lut3d_t;

/* ==========================================================================
 * L1/L2: Gamma LUT Operations
 * ========================================================================== */

/**
 * Create a gamma LUT using standard power-law formula:
 *   out = in^γ  (normalized range)
 *
 * Output is stored in 16-bit precision for high dynamic range support.
 */
int gamma_lut_create_power_law(gamma_lut_t *lut, double gamma)
{
    if (!lut || gamma <= 0.0) return -1;
    lut->gamma = gamma;
    lut->black_offset = 0.0;
    lut->peak_luminance = 1.0;
    lut->size = 256;
    for (int i = 0; i < 256; i++) {
        double in = (double)i / 255.0;
        double out = pow(in, gamma);
        int v = (int)(out * 65535.0 + 0.5);
        if (v < 0) v = 0; if (v > 65535) v = 65535;
        lut->lut[i] = (uint16_t)v;
    }
    return 0;
}

/**
 * Create a gamma LUT using sRGB transfer function.
 * This is the most common display calibration standard.
 */
int gamma_lut_create_srgb(gamma_lut_t *lut)
{
    if (!lut) return -1;
    lut->gamma = 2.2; /* approximate */
    lut->black_offset = 0.0;
    lut->peak_luminance = 1.0;
    lut->size = 256;
    for (int i = 0; i < 256; i++) {
        double in = (double)i / 255.0;
        double out = transfer_srgb_to_linear(in);
        int v = (int)(out * 65535.0 + 0.5);
        if (v < 0) v = 0; if (v > 65535) v = 65535;
        lut->lut[i] = (uint16_t)v;
    }
    return 0;
}

/**
 * Create a BT.1886 LUT with specified black level and peak white.
 *
 * BT.1886 is the ITU recommendation for reference HDTV display EOTF:
 *   L = a × (max[(V+b),0])^γ   where a = (Lw^(1/γ) - Lb^(1/γ))^γ
 *   b = Lb^(1/γ) / (Lw^(1/γ) - Lb^(1/γ))
 */
int gamma_lut_create_bt1886(gamma_lut_t *lut, double black_nits,
                            double peak_nits, double gamma)
{
    if (!lut || gamma <= 0.0) return -1;
    if (peak_nits <= 0.0) peak_nits = 100.0;
    if (black_nits <= 0.0) black_nits = 0.0;

    lut->gamma = gamma;
    lut->black_offset = black_nits;
    lut->peak_luminance = peak_nits;
    lut->size = 256;

    double Lb = black_nits / peak_nits;
    double Lw = 1.0;
    double gamma_inv = 1.0 / gamma;
    double a = pow(pow(Lw, gamma_inv) - pow(Lb, gamma_inv), gamma);
    double b = pow(Lb, gamma_inv) / (pow(Lw, gamma_inv) - pow(Lb, gamma_inv));

    for (int i = 0; i < 256; i++) {
        double V = (double)i / 255.0;
        double L = a * pow(V + b > 0 ? V + b : 0, gamma);
        int v = (int)(L * 65535.0 + 0.5);
        if (v < 0) v = 0; if (v > 65535) v = 65535;
        lut->lut[i] = (uint16_t)v;
    }
    return 0;
}

/**
 * Create a PQ (SMPTE ST 2084) LUT for HDR displays.
 *
 * Maps 10-bit PQ code values to absolute linear luminance (nits).
 * Input: 8-bit index → 10-bit PQ code.
 * Output: linear light × 65535/(peak_nits).
 */
int gamma_lut_create_pq(gamma_lut_t *lut, double peak_nits)
{
    if (!lut || peak_nits <= 0.0) return -1;
    lut->gamma = 0.0; /* Not applicable */
    lut->black_offset = 0.0;
    lut->peak_luminance = peak_nits;
    lut->size = 256;

    for (int i = 0; i < 256; i++) {
        double pq_code = (double)i / 255.0;
        double linear_nits = transfer_pq_to_linear(pq_code, peak_nits);
        double normalized = linear_nits / peak_nits;
        int v = (int)(normalized * 65535.0 + 0.5);
        if (v < 0) v = 0; if (v > 65535) v = 65535;
        lut->lut[i] = (uint16_t)v;
    }
    return 0;
}

/**
 * Apply a gamma LUT to a framebuffer.
 * Each channel is individually LUT-mapped.
 */
int gamma_lut_apply(const gamma_lut_t *lut, framebuffer_t *fb)
{
    if (!lut || !fb || !fb->data) return -1;
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            for (uint32_t c = 0; c < fb->bytes_per_pixel && c < 3; c++) {
                uint32_t idx = y * fb->stride_bytes + x * fb->bytes_per_pixel + c;
                uint8_t in = fb->data[idx];
                /* Scale 16-bit output back to 8-bit */
                int v = (int)(lut->lut[in] * 255 / 65535 + 0.5);
                if (v < 0) v = 0; if (v > 255) v = 255;
                fb->data[idx] = (uint8_t)v;
            }
        }
    }
    return 0;
}

/**
 * Apply an inverse gamma LUT (linearizes the framebuffer).
 */
int gamma_lut_apply_inverse(const gamma_lut_t *lut, framebuffer_t *fb)
{
    if (!lut || !fb || !fb->data) return -1;
    /* Precompute inverse LUT */
    uint8_t inverse[65536]; /* 16-bit indexed */
    for (int i = 0; i < 65536; i++) inverse[i] = 0;
    for (int i = 1; i < 256; i++) {
        uint16_t idx = lut->lut[i];
        if (idx > 0) inverse[idx] = (uint8_t)i;
    }
    inverse[0] = 0;
    inverse[65535] = 255;
    /* Fill gaps */
    for (int i = 1; i < 65535; i++) {
        if (inverse[i] == 0 && inverse[i - 1] != 0) {
            int j;
            for (j = i + 1; j < 65536 && inverse[j] == 0; j++);
            if (j < 65536) {
                uint8_t v0 = inverse[i - 1];
                uint8_t v1 = inverse[j];
                for (int k = i; k < j; k++)
                    inverse[k] = (uint8_t)(v0 + (v1 - v0) * (k - i + 1) / (j - i + 1));
            }
        }
    }

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            for (uint32_t c = 0; c < fb->bytes_per_pixel && c < 3; c++) {
                uint32_t idx = y * fb->stride_bytes + x * fb->bytes_per_pixel + c;
                uint16_t in16 = (uint16_t)fb->data[idx] * 257; /* Scale 8→16 */
                fb->data[idx] = inverse[in16];
            }
        }
    }
    return 0;
}

/* ==========================================================================
 * L5: Gamma Measurement and Fitting
 * ========================================================================== */

/**
 * Fit a power-law gamma from measured luminance ramp.
 *
 * Measures: V_i (digital code, normalized) → L_i (luminance, normalized)
 * Model: L = V^γ → log(L) = γ × log(V)
 *
 * Uses linear regression in log-log space.
 * Returns the fitted gamma value.
 */
double gamma_fit_power_law(const double *codes_normalized,
                           const double *luminances_normalized,
                           int n_points, double *r_squared)
{
    if (!codes_normalized || !luminances_normalized || n_points < 3) {
        if (r_squared) *r_squared = 0.0;
        return 2.2;
    }

    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
    int valid = 0;

    for (int i = 0; i < n_points; i++) {
        double c = codes_normalized[i];
        double l = luminances_normalized[i];
        if (c <= 0.0 || l <= 0.0) continue;

        double lx = log(c);
        double ly = log(l);
        sum_x += lx;
        sum_y += ly;
        sum_xy += lx * ly;
        sum_x2 += lx * lx;
        sum_y2 += ly * ly;
        valid++;
    }

    if (valid < 2) {
        if (r_squared) *r_squared = 0.0;
        return 2.2;
    }

    double n = valid;
    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    double intercept = (sum_y - slope * sum_x) / n;

    if (r_squared) {
        double ss_res = 0, ss_tot = 0;
        double mean_y = sum_y / n;
        for (int i = 0; i < n_points; i++) {
            double c = codes_normalized[i];
            double l = luminances_normalized[i];
            if (c <= 0.0 || l <= 0.0) continue;
            double pred = slope * log(c) + intercept;
            ss_res += (log(l) - pred) * (log(l) - pred);
            ss_tot += (log(l) - mean_y) * (log(l) - mean_y);
        }
        *r_squared = (ss_tot > 0.0) ? 1.0 - ss_res / ss_tot : 0.0;
    }

    /* Gamma is the slope of log(L) vs log(V) */
    double gamma = slope;
    if (gamma < 0.5) gamma = 0.5;
    if (gamma > 5.0) gamma = 5.0;
    return gamma;
}

/**
 * Calibrate display gamma from measured ramp.
 *
 * 1. Normalize codes and luminance
 * 2. Fit power-law gamma
 * 3. Generate optimal LUT
 *
 * Not valid for PQ/HLG HDR displays — use PQ-specific calibration for those.
 */
int gamma_calibrate_from_ramp(const double *luminances_cdm2, int n_points,
                              gamma_lut_t *lut_out)
{
    if (!luminances_cdm2 || !lut_out || n_points < 3) return -1;

    /* Generate normalized codes */
    double *codes = malloc(n_points * sizeof(double));
    double *lums_norm = malloc(n_points * sizeof(double));
    if (!codes || !lums_norm) { free(codes); free(lums_norm); return -1; }

    double max_lum = 0.0;
    for (int i = 0; i < n_points; i++)
        if (luminances_cdm2[i] > max_lum) max_lum = luminances_cdm2[i];
    if (max_lum <= 0.0) { free(codes); free(lums_norm); return -1; }

    for (int i = 0; i < n_points; i++) {
        codes[i] = (double)i / (n_points - 1);
        lums_norm[i] = luminances_cdm2[i] / max_lum;
    }

    double rsq;
    double gamma = gamma_fit_power_law(codes, lums_norm, n_points, &rsq);

    gamma_lut_create_power_law(lut_out, gamma);
    lut_out->peak_luminance = max_lum;

    free(codes);
    free(lums_norm);
    return 0;
}

/**
 * Verify calibration quality by computing deviation at each step.
 *
 * Returns the maximum deviation (as ratio) across all steps.
 * < 0.02 = excellent, < 0.05 = acceptable.
 */
double gamma_verify_calibration(const gamma_lut_t *lut,
                                const double *measured_luminances,
                                int n_points)
{
    if (!lut || !measured_luminances || n_points < 2) return 0.0;

    double max_dev = 0.0;
    for (int i = 0; i < n_points; i++) {
        double code = (double)i / (n_points - 1);
        /* Predicted: power-law from LUT gamma */
        double predicted = pow(code, lut->gamma);
        /* Measured normalized */
        double max_lum = 0.0;
        for (int j = 0; j < n_points; j++)
            if (measured_luminances[j] > max_lum) max_lum = measured_luminances[j];
        if (max_lum <= 0.0) continue;
        double measured_norm = measured_luminances[i] / max_lum;
        double dev = fabs(predicted - measured_norm);
        if (dev > max_dev) max_dev = dev;
    }
    return max_dev;
}

/* ==========================================================================
 * L8: HDR Tone Mapping Operators
 * ========================================================================== */

/**
 * Reinhard global tone mapping operator.
 *
 * Classic photographic approach:
 *   L_d = L_s / (1 + L_s) * (1 + L_s / L_white²)
 *
 * Reference: Reinhard et al., ACM SIGGRAPH 2002
 *
 * @param hdr_luminance HDR luminance value [0, ∞) in cd/m²
 * @param key           Scene key value (typ. 0.18 for 18% gray)
 * @param white_point   Smallest luminance mapped to pure white
 * @return Display luminance [0, 1]
 */
double reinhard_tone_map(double hdr_luminance, double key, double white_point)
{
    if (hdr_luminance <= 0.0) return 0.0;
    double L = key * hdr_luminance;
    double Ld = L * (1.0 + L / (white_point * white_point)) / (1.0 + L);
    if (Ld < 0.0) Ld = 0.0;
    if (Ld > 1.0) Ld = 1.0;
    return Ld;
}

/**
 * Hable (Uncharted 2) filmic tone mapping.
 *
 * Based on John Hable's "Uncharted 2: HDR Lighting" GDC 2010 presentation.
 * Uses a piecewise rational function to preserve highlight detail
 * while mapping to SDR range.
 *
 * The curve is: x → ((x*(A*x + C*B) + D*E) / (x*(A*x + B) + D*F)) - E/F
 */
double hable_tone_map(double hdr_value)
{
    /* Hable's filmic curve constants */
    const double A = 0.15;
    const double B = 0.50;
    const double C = 0.10;
    const double D = 0.20;
    const double E = 0.02;
    const double F = 0.30;
    const double W = 11.2; /* Linear white point */

    double x = hdr_value > 0.0 ? hdr_value : 0.0;
    double numerator = x * (A * x + C * B) + D * E;
    double denominator = x * (A * x + B) + D * F;
    double result = numerator / denominator;
    result = result - E / F;

    /* Scale to white point */
    double white_scale = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    if (white_scale > 0.0)
        result /= white_scale;

    if (result < 0.0) result = 0.0;
    if (result > 1.0) result = 1.0;
    return result;
}

/**
 * ACES (Academy Color Encoding System) filmic tone mapping.
 *
 * Standardized by the Academy of Motion Picture Arts and Sciences.
 * Maps HDR scene-referred values to SDR display-referred [0,1].
 *
 * Reference: ACES Technical Bulletin TB-2019-001
 */
double aces_tone_map(double hdr_value)
{
    /* ACES RRT approximation (Narkowicz fit) */
    const double a = 2.51;
    const double b = 0.03;
    const double c = 2.43;
    const double d = 0.59;
    const double e = 0.14;

    double x = hdr_value > 0.0 ? hdr_value : 0.0;
    double result = (x * (a * x + b)) / (x * (c * x + d) + e);

    if (result < 0.0) result = 0.0;
    if (result > 1.0) result = 1.0;
    return result;
}

/**
 * Display-adaptive tone mapping (BT.2390 Hermite spline).
 *
 * ITU-R BT.2390 recommendation for HDR-to-SDR conversion.
 * Uses a Hermite spline knee function to compress highlights
 * while preserving midtones.
 */
double bt2390_tone_map(double hdr_luminance, double display_max_nits)
{
    if (hdr_luminance <= 0.0) return 0.0;
    if (display_max_nits <= 0.0) display_max_nits = 100.0;

    /* Normalize to display peak */
    double normalized = hdr_luminance / display_max_nits;

    /* Hermite knee point */
    double knee_point = 0.7;
    if (normalized <= knee_point) {
        return normalized;
    }

    /* Hermite spline: smooth transition from knee to max */
    double t = (normalized - knee_point) / (1.0 - knee_point);
    if (t > 1.0) t = 1.0;
    /* Hermite basis: h01 = 2t³ - 3t² + 1, h11 = t³ - 2t² + t */
    double h01 = 2.0 * t * t * t - 3.0 * t * t + 1.0;
    double h11 = t * t * t - 2.0 * t * t + t;

    double p0 = knee_point;
    double p1 = 1.0;
    double m0 = 1.0; /* slope at knee */
    double m1 = 0.0; /* slope at peak (saturate) */

    double result = h01 * p0 + (1.0 - h01) * p1 + h11 * m0 * (1.0 - knee_point)
                  + (t * t * t - t * t) * m1 * (1.0 - knee_point);

    if (result < 0.0) result = 0.0;
    if (result > 1.0) result = 1.0;
    return result;
}

/**
 * Tone map an entire framebuffer using a specified operator.
 */
int framebuffer_tone_map(framebuffer_t *fb, int operator_type,
                         double param1, double param2)
{
    if (!fb || !fb->data) return -1;

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            for (uint32_t c = 0; c < fb->bytes_per_pixel && c < 3; c++) {
                uint32_t idx = y * fb->stride_bytes + x * fb->bytes_per_pixel + c;
                double val = (double)fb->data[idx] / 255.0;

                double mapped;
                switch (operator_type) {
                    case 0: mapped = reinhard_tone_map(val, param1 > 0 ? param1 : 0.18, param2 > 0 ? param2 : 1.5); break;
                    case 1: mapped = hable_tone_map(val); break;
                    case 2: mapped = aces_tone_map(val); break;
                    case 3: mapped = bt2390_tone_map(val, param1 > 0 ? param1 : 100.0); break;
                    default: mapped = val; break;
                }

                int out = (int)(mapped * 255.0 + 0.5);
                if (out < 0) out = 0; if (out > 255) out = 255;
                fb->data[idx] = (uint8_t)out;
            }
        }
    }
    return 0;
}

/* ==========================================================================
 * L5: Color LUT3D Operations
 * ========================================================================== */

/**
 * Allocate a 3D color LUT.
 */
color_lut3d_t *color_lut3d_alloc(int size)
{
    if (size < 2 || size > 65) return NULL;
    color_lut3d_t *lut = malloc(sizeof(color_lut3d_t));
    if (!lut) return NULL;
    lut->size = size;
    lut->table = malloc(size * size * size * sizeof(lut3d_entry_t));
    if (!lut->table) { free(lut); return NULL; }
    memset(lut->table, 0, size * size * size * sizeof(lut3d_entry_t));
    return lut;
}

void color_lut3d_free(color_lut3d_t *lut)
{
    if (lut) {
        free(lut->table);
        free(lut);
    }
}

/**
 * Initialize a 3D LUT as identity mapping.
 * RGB in [0, 65535] → RGB out [0, 65535]
 */
void color_lut3d_identity(color_lut3d_t *lut)
{
    if (!lut) return;
    int n = lut->size;
    for (int b = 0; b < n; b++) {
        for (int g = 0; g < n; g++) {
            for (int r = 0; r < n; r++) {
                int idx = (b * n + g) * n + r;
                lut->table[idx].r = (uint16_t)(r * 65535 / (n - 1));
                lut->table[idx].g = (uint16_t)(g * 65535 / (n - 1));
                lut->table[idx].b = (uint16_t)(b * 65535 / (n - 1));
            }
        }
    }
}

/**
 * Apply 3D LUT to a framebuffer using trilinear interpolation.
 */
int color_lut3d_apply(const color_lut3d_t *lut, framebuffer_t *fb)
{
    if (!lut || !fb || !fb->data || fb->bytes_per_pixel < 3) return -1;
    int n = lut->size;
    double scale = (n - 1) / 255.0;

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            uint8_t *p = fb->data + y * fb->stride_bytes + x * fb->bytes_per_pixel;
            double rf = p[0] * scale;
            double gf = p[1] * scale;
            double bf = p[2] * scale;

            int r0 = (int)rf, g0 = (int)gf, b0 = (int)bf;
            int r1 = (r0 + 1 < n) ? r0 + 1 : r0;
            int g1 = (g0 + 1 < n) ? g0 + 1 : g0;
            int b1 = (b0 + 1 < n) ? b0 + 1 : b0;
            double dr = rf - r0, dg = gf - g0, db = bf - b0;

            lut3d_entry_t c000 = lut->table[(b0 * n + g0) * n + r0];
            lut3d_entry_t c001 = lut->table[(b0 * n + g0) * n + r1];
            lut3d_entry_t c010 = lut->table[(b0 * n + g1) * n + r0];
            lut3d_entry_t c011 = lut->table[(b0 * n + g1) * n + r1];
            lut3d_entry_t c100 = lut->table[(b1 * n + g0) * n + r0];
            lut3d_entry_t c101 = lut->table[(b1 * n + g0) * n + r1];
            lut3d_entry_t c110 = lut->table[(b1 * n + g1) * n + r0];
            lut3d_entry_t c111 = lut->table[(b1 * n + g1) * n + r1];

            double rout = (1-dr)*(1-dg)*(1-db)*c000.r + dr*(1-dg)*(1-db)*c001.r
                        + (1-dr)*dg*(1-db)*c010.r + dr*dg*(1-db)*c011.r
                        + (1-dr)*(1-dg)*db*c100.r + dr*(1-dg)*db*c101.r
                        + (1-dr)*dg*db*c110.r + dr*dg*db*c111.r;
            double gout = (1-dr)*(1-dg)*(1-db)*c000.g + dr*(1-dg)*(1-db)*c001.g
                        + (1-dr)*dg*(1-db)*c010.g + dr*dg*(1-db)*c011.g
                        + (1-dr)*(1-dg)*db*c100.g + dr*(1-dg)*db*c101.g
                        + (1-dr)*dg*db*c110.g + dr*dg*db*c111.g;
            double bout = (1-dr)*(1-dg)*(1-db)*c000.b + dr*(1-dg)*(1-db)*c001.b
                        + (1-dr)*dg*(1-db)*c010.b + dr*dg*(1-db)*c011.b
                        + (1-dr)*(1-dg)*db*c100.b + dr*(1-dg)*db*c101.b
                        + (1-dr)*dg*db*c110.b + dr*dg*db*c111.b;

            p[0] = (uint8_t)(rout / 257.0 + 0.5);
            p[1] = (uint8_t)(gout / 257.0 + 0.5);
            p[2] = (uint8_t)(bout / 257.0 + 0.5);
        }
    }
    return 0;
}


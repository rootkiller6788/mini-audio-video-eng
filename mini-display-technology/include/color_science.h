/**
 * color_science.h — Color Science for Display Technology
 *
 * L1 Definitions: CIE 1931 XYZ, xyY, Lab, Luv color spaces,
 *                  color temperature, white point, tristimulus
 * L2 Core Concepts: Gamma transfer functions, gamut mapping,
 *                   color difference metrics, chromatic adaptation
 * L3 Mathematical Structures: Color transformation matrices (3×3),
 *                             CIECAM02 forward/reverse models,
 *                             chromatic adaptation transforms
 * L4 Fundamental Laws: Grassmann's laws of additive color mixing,
 *                      Weber-Fechner law (gamma encoding basis)
 *
 * Reference:
 *   Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
 *   CIE 15:2018 — Colorimetry
 *   IEC 61966-2-1 — sRGB color space
 *   ITU-R BT.709, BT.2020, BT.2100
 *   Fairchild, "Color Appearance Models" (2013)
 *
 * Course Mapping:
 *   MIT 6.003 — Signal Processing (perceptual encoding)
 *   Stanford EE247 — Optical/Display (colorimetry)
 *   Berkeley EE117 — EM Waves (spectral power distributions)
 *   ETH 227-0455 — EM/Optics (radiometry → photometry)
 *   TU Munich — Display Technology (color management)
 */

#ifndef COLOR_SCIENCE_H
#define COLOR_SCIENCE_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "display_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Color Spaces and Chromaticity
 * ========================================================================== */

/** CIE 1931 XYZ tristimulus values (absolute, cd/m²) */
typedef struct {
    double X;  /**< CIE X tristimulus (red-like, includes luminance) */
    double Y;  /**< CIE Y tristimulus = luminance (cd/m²) */
    double Z;  /**< CIE Z tristimulus (blue-like) */
} cie_xyz_t;

/** CIE 1931 xy chromaticity coordinates */
typedef struct {
    double x;  /**< Chromaticity x  [0, ~0.8] */
    double y;  /**< Chromaticity y  [0, ~0.9] */
} cie_xy_t;

/** CIE xyY (chromaticity + luminance) */
typedef struct {
    double x, y;  /**< Chromaticity coordinates */
    double Y;     /**< Luminance (cd/m²) */
} cie_xyy_t;

/** CIE 1976 L*a*b* (CIELAB) — perceptually uniform color space */
typedef struct {
    double L;  /**< Lightness    [0, 100] */
    double a;  /**< Green–Red   [-128, 127] */
    double b;  /**< Blue–Yellow [-128, 127] */
} cie_lab_t;

/** CIE 1976 L*u*v* (CIELUV) — alternative uniform color space */
typedef struct {
    double L;  /**< Lightness [0, 100] */
    double u;  /**< u' related axis */
    double v;  /**< v' related axis */
} cie_luv_t;

/** White point definition (CIE 1931 xy) */
typedef struct {
    double x, y;       /**< Chromaticity of white */
    const char *name;  /**< Human-readable name */
} white_point_t;

/** RGB color primaries definition (CIE 1931 xy) */
typedef struct {
    cie_xy_t red;     /**< Red primary chromaticity */
    cie_xy_t green;   /**< Green primary chromaticity */
    cie_xy_t blue;    /**< Blue primary chromaticity */
    white_point_t white; /**< Reference white point */
} rgb_primaries_t;

/** 3×3 color transformation matrix */
typedef struct {
    double m[3][3];  /**< Row-major matrix: m[row][col] */
} color_matrix_t;

/** Color gamut descriptor */
typedef struct {
    rgb_primaries_t primaries; /**< RGB primaries */
    double volume_xyz;        /**< Gamut volume in XYZ space (relative) */
    double coverage_srgb;     /**< Coverage of sRGB gamut (0-1) */
    double coverage_dcip3;    /**< Coverage of DCI-P3 gamut (0-1) */
    double coverage_bt2020;   /**< Coverage of BT.2020 gamut (0-1) */
} color_gamut_t;

/* ==========================================================================
 * L2: Transfer Functions (Electro-Optical / Opto-Electrical)
 * ========================================================================== */

/** Transfer function / gamma curve type */
typedef enum {
    TF_POWER_LAW    = 0,  /**< Simple power law: out = in^γ */
    TF_SRGB         = 1,  /**< sRGB piecewise (IEC 61966-2-1) */
    TF_BT1886       = 2,  /**< BT.1886 reference EOTF */
    TF_PQ_ST2084    = 3,  /**< SMPTE ST 2084 Perceptual Quantizer */
    TF_HLG_ARIB_B67 = 4,  /**< ARIB STD-B67 Hybrid Log-Gamma */
    TF_PURE_LOG     = 5,  /**< Cineon-style logarithmic */
    TF_LINEAR       = 6   /**< Linear light (gamma = 1.0) */
} transfer_func_t;

/** Parameters for transfer function */
typedef struct {
    transfer_func_t type;
    double gamma;            /**< Power-law exponent */
    double black_offset;     /**< Black level offset (BT.1886) */
    double peak_luminance;   /**< Peak luminance for PQ/HLG (cd/m²) */
    double system_gamma;     /**< System gamma (HLG: ~1.2) */
} transfer_params_t;

/* ==========================================================================
 * L3: Spectral Power Distribution
 * ========================================================================== */

/** Spectral power distribution sampling (380-780nm, 5nm steps) */
#define SPD_SAMPLES 81
#define SPD_START_NM 380
#define SPD_STEP_NM 5

typedef struct {
    double values[SPD_SAMPLES]; /**< Spectral radiance at each λ */
    double lambda_start_nm;
    double lambda_step_nm;
} spectral_power_t;

/** CIE 1931 2° Standard Observer color matching functions */
typedef struct {
    double x_bar[SPD_SAMPLES]; /**< x̄(λ) — red cone sensitivity */
    double y_bar[SPD_SAMPLES]; /**< ȳ(λ) — luminance efficiency = V(λ) */
    double z_bar[SPD_SAMPLES]; /**< z̄(λ) — blue cone sensitivity */
} cie_cmf_1931_t;

/* ==========================================================================
 * L1/L2: Core Color Science API
 * ========================================================================== */

/* --- White Point Constants --- */
/** Get standard white point by name: "D65", "D50", "DCI-P3", "E" */
int white_point_get(const char *name, white_point_t *wp);

/** Get the CIE xy chromaticity for a given color temperature (Planckian locus) */
cie_xy_t color_temperature_to_xy(double kelvin);

/** Compute correlated color temperature from xy chromaticity (McCamy) */
double xy_to_cct(const cie_xy_t *xy);

/* --- Color Space Conversions --- */

/** sRGB (nonlinear) → CIE XYZ (D65) */
cie_xyz_t srgb_to_xyz(const pixel_rgb_t *srgb);

/** CIE XYZ → sRGB (nonlinear, clamped) */
pixel_rgb_t xyz_to_srgb(const cie_xyz_t *xyz);

/** BT.709 RGB → CIE XYZ */
cie_xyz_t bt709_rgb_to_xyz(const pixel_float_t *rgb);

/** CIE XYZ → BT.709 RGB */
pixel_float_t xyz_to_bt709_rgb(const cie_xyz_t *xyz);

/** BT.2020 RGB → CIE XYZ */
cie_xyz_t bt2020_rgb_to_xyz(const pixel_float_t *rgb);

/** CIE XYZ → BT.2020 RGB */
pixel_float_t xyz_to_bt2020_rgb(const cie_xyz_t *xyz);

/** RGB → YCbCr (BT.709) */
pixel_ycbcr_t rgb_to_ycbcr_bt709(const pixel_float_t *rgb);

/** YCbCr → RGB (BT.709) */
pixel_float_t ycbcr_to_rgb_bt709(const pixel_ycbcr_t *ycc);

/** RGB → YCbCr (BT.601) */
pixel_ycbcr_t rgb_to_ycbcr_bt601(const pixel_float_t *rgb);

/** YCbCr → RGB (BT.601) */
pixel_float_t ycbcr_to_rgb_bt601(const pixel_ycbcr_t *ycc);

/** CIE XYZ → CIELAB (D65 reference white) */
cie_lab_t xyz_to_lab(const cie_xyz_t *xyz);

/** CIELAB → CIE XYZ */
cie_xyz_t lab_to_xyz(const cie_lab_t *lab);

/** CIE XYZ → CIELUV */
cie_luv_t xyz_to_luv(const cie_xyz_t *xyz);

/** CIELUV → CIE XYZ */
cie_xyz_t luv_to_xyz(const cie_luv_t *luv);

/** CIE XYZ → xyY */
cie_xyy_t xyz_to_xyy(const cie_xyz_t *xyz);

/** xyY → CIE XYZ */
cie_xyz_t xyy_to_xyz(const cie_xyy_t *xyy);

/* --- Color Difference Metrics --- */

/** CIE 1976 ΔE*ab (Euclidean distance in CIELAB) */
double delta_e_1976(const cie_lab_t *a, const cie_lab_t *b);

/** CIE 1994 ΔE (textiles/graphic arts weighting) */
double delta_e_1994(const cie_lab_t *ref, const cie_lab_t *sample,
                    int is_textiles);

/** CIE 2000 ΔE (most accurate, includes hue/chroma/lightness corrections) */
double delta_e_2000(const cie_lab_t *ref, const cie_lab_t *sample);

/* --- Gamut Operations --- */

/** Check if a CIE xy chromaticity is inside a given RGB gamut triangle */
int gamut_contains_xy(const rgb_primaries_t *primaries, const cie_xy_t *xy);

/** Compute gamut coverage ratio (area ratio of intersection / target) */
double gamut_coverage(const rgb_primaries_t *measured,
                      const rgb_primaries_t *reference);

/** Compute the RGB primaries matrix for converting XYZ to linear RGB */
color_matrix_t primaries_to_matrix(const rgb_primaries_t *p);

/** Compute the inverse matrix (linear RGB → XYZ) */
color_matrix_t primaries_to_matrix_inv(const rgb_primaries_t *p);

/** Multiply a 3×3 color matrix by a 3-vector */
void color_matrix_vec_mul(const color_matrix_t *mat,
                          double x, double y, double z,
                          double *rx, double *ry, double *rz);

/** Multiply two 3×3 color matrices: C = A * B */
color_matrix_t color_matrix_mul(const color_matrix_t *a,
                                const color_matrix_t *b);

/* --- Chromatic Adaptation --- */

/** Bradford chromatic adaptation matrix (source → destination white) */
color_matrix_t chromatic_adaptation_bradford(const white_point_t *src,
                                              const white_point_t *dst);

/** Apply chromatic adaptation to an XYZ value */
cie_xyz_t adapt_xyz(const cie_xyz_t *xyz, const color_matrix_t *adapt);

/* --- Transfer Functions (Gamma) --- */

/** sRGB nonlinear encoding (linear → sRGB), IEC 61966-2-1 */
double transfer_srgb_from_linear(double linear);

/** sRGB inverse (sRGB → linear) */
double transfer_srgb_to_linear(double srgb);

/** Power-law gamma encode: out = in^(1/γ) */
double transfer_power_encode(double linear, double gamma);

/** Power-law gamma decode: out = in^γ */
double transfer_power_decode(double encoded, double gamma);

/** SMPTE ST 2084 PQ OETF (linear nits → PQ code value) */
double transfer_pq_from_linear(double linear_nits, double peak_nits);

/** SMPTE ST 2084 PQ EOTF (PQ code value → linear nits) */
double transfer_pq_to_linear(double pq_code, double peak_nits);

/** HLG OETF (scene light → HLG signal) per ARIB STD-B67 */
double transfer_hlg_from_linear(double scene_linear);

/** HLG EOTF (HLG signal → display light) */
double transfer_hlg_to_linear(double hlg_signal, double peak_nits);

/** BT.1886 EOTF (reference display) */
double transfer_bt1886_to_linear(double encoded, double black_level,
                                 double peak_white);

/* --- Spectral Operations --- */

/** Get the standard CIE 1931 2° CMF */
const cie_cmf_1931_t *cie_cmf_1931_get(void);

/** Compute XYZ tristimulus from SPD using CIE 1931 CMF */
cie_xyz_t spd_to_xyz(const spectral_power_t *spd);

/** Compute CIE xy chromaticity from SPD */
cie_xy_t spd_to_xy(const spectral_power_t *spd);

/** Compute luminance (Y) from SPD using V(λ) photopic curve */
double spd_to_luminance(const spectral_power_t *spd);

/* --- Color Depth & Range --- */

/** Initialize a color_depth_t from standard video ranges */
void color_depth_init(color_depth_t *cd, uint8_t bits, int full_range);

/** Map a float [0,1] color component to digital code value */
uint16_t float_to_code(double val, const color_depth_t *cd);

/** Map a digital code value to float [0,1] */
double code_to_float(uint16_t code, const color_depth_t *cd);

/* --- Display Measurement --- */

/** Compute contrast ratio from black/white luminance */
double contrast_ratio_calc(double white_lum, double black_lum);

/** Initialize luminance_spec_t from measured values */
void luminance_spec_init(luminance_spec_t *spec, double black, double white,
                         double gamma, double wx, double wy);

/** Estimate display gamma from measured luminance ramp */
double estimate_gamma_from_ramp(const double *codes, const double *luminances,
                                int n_samples);

#ifdef __cplusplus
}
#endif

#endif /* COLOR_SCIENCE_H */


/**
 * @file color_science.h
 * @brief Color science: XYZ/Lab/xy spaces, white balance, CCM, CCT
 *
 * L1: CIE XYZ, CIE xy, CIELAB, CIELUV, illuminants, color temperature
 * L2: trichromatic theory, chromatic adaptation (Bradford, von Kries)
 * L3: 3x3 color matrices, CIE 1931 Standard Observer
 * L5: Gray World, White Patch, Shades of Gray WB; CCM calibration (LS);
 *     XYZ↔sRGB conversion, CCT estimation (McCamy)
 * L6: ColorChecker calibration, illuminant estimation
 * L7: camera color calibration, display calibration
 *
 * Reference: Poynton (2012) Ch.21-28; Wyszecki & Stiles (1982);
 *            McCamy, Color Res. Appl. 1992
 */
#ifndef COLOR_SCIENCE_H
#define COLOR_SCIENCE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/*===========================================================================
 * L1: Color data types
 *===========================================================================*/

typedef struct { double X, Y, Z; } cie_xyz_t;
typedef struct { double x, y; } cie_xy_t;
typedef struct { double L, a, b; } cie_lab_t;
typedef struct { double L, u, v; } cie_luv_t;
typedef struct { double r, g, b; } rgb_linear_t;

typedef enum {
    ILLUMINANT_A=0, ILLUMINANT_B=1, ILLUMINANT_C=2,
    ILLUMINANT_D50=3, ILLUMINANT_D55=4, ILLUMINANT_D65=5,
    ILLUMINANT_D75=6, ILLUMINANT_E=7, ILLUMINANT_F2=8,
    ILLUMINANT_F7=9, ILLUMINANT_F11=10
} illuminant_t;

typedef struct { double r_gain, g_gain, b_gain; } wb_gains_t;
typedef struct { double m[9]; } color_matrix_t;

typedef enum {
    CC_DARK_SKIN=0, CC_LIGHT_SKIN=1, CC_BLUE_SKY=2, CC_FOLIAGE=3,
    CC_BLUE_FLOWER=4, CC_BLUISH_GREEN=5, CC_ORANGE=6, CC_PURPLISH_BLUE=7,
    CC_MODERATE_RED=8, CC_PURPLE=9, CC_YELLOW_GREEN=10, CC_ORANGE_YELLOW=11,
    CC_BLUE=12, CC_GREEN=13, CC_RED=14, CC_YELLOW=15, CC_MAGENTA=16,
    CC_CYAN=17, CC_WHITE=18, CC_NEUTRAL_8=19, CC_NEUTRAL_65=20,
    CC_NEUTRAL_5=21, CC_NEUTRAL_35=22, CC_BLACK=23, CC_COUNT=24
} colorchecker_patch_t;

/*===========================================================================
 * L1+L3: Illuminant data
 *===========================================================================*/

cie_xyz_t illuminant_get_xyz(illuminant_t ill);
cie_xy_t  illuminant_get_xy(illuminant_t ill);

/*===========================================================================
 * L3+L5: Color space conversions
 *===========================================================================*/

rgb_linear_t srgb_to_linear(rgb_linear_t srgb);
rgb_linear_t linear_to_srgb(rgb_linear_t linear);
cie_xyz_t    srgb_linear_to_xyz(rgb_linear_t rgb);
rgb_linear_t xyz_to_srgb_linear(cie_xyz_t xyz);
cie_xy_t     xyz_to_xy(cie_xyz_t xyz);
cie_xyz_t    xy_to_xyz(cie_xy_t xy, double Y);
cie_lab_t    xyz_to_lab(cie_xyz_t xyz, cie_xyz_t white);
cie_xyz_t    lab_to_xyz(cie_lab_t lab, cie_xyz_t white);

/*===========================================================================
 * L5: CCT estimation
 *===========================================================================*/

/** McCamy cubic: n=(x-0.3320)/(y-0.1858); CCT=-449n^3+3525n^2-6823.3n+5520.33 */
double color_xy_to_cct_mccamy(cie_xy_t xy);

/** Robertson iterative method, more accurate wide-range */
double color_xyz_to_cct(cie_xyz_t xyz);

/** Planckian locus xy at temperature T */
cie_xy_t color_planckian_xy(double temp_k);

/*===========================================================================
 * L5: White balance algorithms
 *===========================================================================*/

/** Gray World: avg(R)=avg(G)=avg(B); gains=G_avg/channel_avg. O(n). */
void color_gray_world(const rgb_linear_t *samples, uint32_t n, wb_gains_t *g);

/** White Patch (Retinex): brightest=white. O(n). */
void color_white_patch(const rgb_linear_t *samples, uint32_t n, wb_gains_t *g);

/** Shades of Gray (Finlayson 2004): p-norm generalization. O(n). */
void color_shades_of_gray(const rgb_linear_t *samples, uint32_t n,
                           double p, wb_gains_t *g);

rgb_linear_t color_apply_wb(rgb_linear_t rgb, const wb_gains_t *g);

/*===========================================================================
 * L5+L7: CCM calibration and application
 *===========================================================================*/

/** LS CCM: min||S*CCM^T - T||^2 via 3x3 normal equations. O(n). */
int color_ccm_calibrate(const double *sensor_rgb, const double *target_rgb,
                         uint32_t n, double ccm[9]);

rgb_linear_t color_apply_ccm(rgb_linear_t rgb, const double ccm[9]);

/*===========================================================================
 * L5: Chromatic adaptation transforms
 *===========================================================================*/

rgb_linear_t color_bradford_cat(rgb_linear_t rgb, illuminant_t src,
                                 illuminant_t dst);
rgb_linear_t color_von_kries_cat(rgb_linear_t rgb, const wb_gains_t *src_lms,
                                  const wb_gains_t *dst_lms);

/*===========================================================================
 * L3: Color difference metrics
 *===========================================================================*/

double color_delta_e_1976(cie_lab_t lab1, cie_lab_t lab2);
double color_delta_e_2000(cie_lab_t lab1, cie_lab_t lab2);
double color_delta_e_cmc(cie_lab_t ref, cie_lab_t sample, double l, double c);

/*===========================================================================
 * L7: ColorChecker reference data
 *===========================================================================*/

void colorchecker_get_srgb(colorchecker_patch_t p, uint8_t *r, uint8_t *g,
                            uint8_t *b);
cie_xyz_t colorchecker_get_xyz(colorchecker_patch_t p);
cie_lab_t colorchecker_get_lab(colorchecker_patch_t p);

#endif /* COLOR_SCIENCE_H */

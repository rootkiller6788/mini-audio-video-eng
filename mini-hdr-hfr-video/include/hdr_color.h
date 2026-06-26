#ifndef HDR_COLOR_H
#define HDR_COLOR_H

#include "hdr_core.h"
#include "hdr_tone_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ?? L1: Color Science Definitions ???????????????????????????????????? */

typedef enum {
    COLOR_SPACE_RGB_LINEAR = 0,
    COLOR_SPACE_sRGB,
    COLOR_SPACE_BT709_YUV,
    COLOR_SPACE_BT2020_YUV,
    COLOR_SPACE_DCI_P3_RGB,
    COLOR_SPACE_XYZ_CIE1931,
    COLOR_SPACE_ICTCP_BT2100,
    COLOR_SPACE_IPT_EBNER,
    COLOR_SPACE_LAB_CIE1976,
    COLOR_SPACE_OKLAB,
    COLOR_SPACE_YCbCr_BT601,
    COLOR_SPACE_YCbCr_BT709,
    COLOR_SPACE_YCbCr_BT2020,
    COLOR_SPACE_COUNT
} color_space_t;

typedef enum {
    CHROMA_SAMPLE_444 = 0,
    CHROMA_SAMPLE_422,
    CHROMA_SAMPLE_420,
    CHROMA_SAMPLE_411,
    CHROMA_SAMPLE_COUNT
} chroma_subsampling_t;

typedef struct {
    double X;
    double Y;
    double Z;
} cie_xyz_t;

typedef struct {
    double L;
    double a;
    double b;
} cie_lab_t;

typedef struct {
    double L;
    double M;
    double S;
} lms_cone_t;

typedef struct {
    double I;
    double Ct;
    double Cp;
} ictcp_t;

typedef struct {
    double m[3][3];
} color_matrix_3x3_t;

typedef struct {
    double c00, c01, c02;
    double c10, c11, c12;
    double c20, c21, c22;
} color_matrix_flat_t;

/* ?? L2: Gamut Mapping ???????????????????????????????????????????????? */

typedef enum {
    GAMUT_MAP_CLIP = 0,
    GAMUT_MAP_SOFT_CLIP,
    GAMUT_MAP_DESATURATE,
    GAMUT_MAP_HUE_PRESERVING,
    GAMUT_MAP_MINDE,
    GAMUT_MAP_CUSP,
    GAMUT_MAP_COUNT
} gamut_map_method_t;

typedef struct {
    gamut_map_method_t method;
    double soft_clip_knee;
    int preserve_luminance;
    double chroma_compression;
    double black_point_compensation;
} gamut_map_config_t;

/* ?? L3: Color Matrix Operations ?????????????????????????????????????? */

/**
 * @brief Matrix multiplication for color transforms: C = A * B.
 *
 * Both compact and flat representations supported.
 *
 * @param a     Left matrix (3x3).
 * @param b     Right matrix (3x3).
 * @param result Output matrix (3x3).
 */
void color_matrix_multiply(const double a[3][3], const double b[3][3], double result[3][3]);

/**
 * @brief Apply a 3x3 color matrix to an XYZ triplet.
 *
 * @param m     Color matrix (3x3).
 * @param in    Input XYZ.
 * @param out   Output XYZ.
 */
void color_matrix_apply_xyz(const double m[3][3], const cie_xyz_t *in, cie_xyz_t *out);

/**
 * @brief Apply a 3x3 color matrix to an RGB triplet.
 *
 * @param m     Color matrix (3x3).
 * @param in    Input RGB.
 * @param out   Output RGB.
 */
void color_matrix_apply_rgb(const double m[3][3], const hdr_rgb_pixel_t *in, hdr_rgb_pixel_t *out);

/**
 * @brief Compute the determinant of a 3x3 matrix.
 *
 * @param m  Matrix.
 * @return   Determinant.
 */
double color_matrix_determinant(const double m[3][3]);

/**
 * @brief Compute the inverse of a 3x3 color matrix.
 *
 * @param m     Input matrix.
 * @param inv   Output inverse matrix.
 * @return      0 on success, -1 if singular.
 */
int color_matrix_inverse(const double m[3][3], double inv[3][3]);

/**
 * @brief Compute the transpose of a 3x3 matrix.
 *
 * @param m     Input matrix.
 * @param t     Output transpose.
 */
void color_matrix_transpose(const double m[3][3], double t[3][3]);

/* ?? API: Color Space Conversion ?????????????????????????????????????? */

/**
 * @brief Get the RGB-to-XYZ transformation matrix for a primaries set.
 *
 * The matrix converts linear RGB to CIE 1931 XYZ using the
 * chromaticities of the RGB primaries and white point.
 *
 * Derivation: Given (xr,yr), (xg,yg), (xb,yb), (xw,yw), solve for
 * the matrix M such that M * [1,1,1]^T = [Xw,Yw,Zw]^T (up to scale).
 *
 * @param primaries  RGB primary set.
 * @param m          Output 3x3 matrix (row-major).
 */
void color_rgb_to_xyz_matrix(const hdr_primaries_set_t *primaries, double m[3][3]);

/**
 * @brief Convert linear sRGB to CIE XYZ.
 *
 * Uses the BT.709/sRGB primaries and D65 white point.
 *
 * @param rgb  Linear sRGB (0-1 range).
 * @param xyz  Output CIE XYZ.
 */
void color_srgb_to_xyz(const hdr_rgb_pixel_t *rgb, cie_xyz_t *xyz);

/**
 * @brief Convert CIE XYZ to linear sRGB.
 *
 * @param xyz  Input CIE XYZ.
 * @param rgb  Output linear sRGB.
 */
void color_xyz_to_srgb(const cie_xyz_t *xyz, hdr_rgb_pixel_t *rgb);

/**
 * @brief Convert linear sRGB to CIE L*a*b* (CIE 1976).
 *
 * L* = 116 * f(Y/Yn) - 16
 * a* = 500 * (f(X/Xn) - f(Y/Yn))
 * b* = 200 * (f(Y/Yn) - f(Z/Zn))
 *
 * where f(t) = t^(1/3) for t > (6/29)^3, else t/(3*(6/29)^2) + 4/29.
 *
 * @param rgb  Linear sRGB input.
 * @param lab  Output CIE L*a*b*.
 */
void color_srgb_to_lab(const hdr_rgb_pixel_t *rgb, cie_lab_t *lab);

/**
 * @brief Convert CIE L*a*b* to linear sRGB.
 *
 * @param lab  Input L*a*b*.
 * @param rgb  Output linear sRGB.
 */
void color_lab_to_srgb(const cie_lab_t *lab, hdr_rgb_pixel_t *rgb);

/**
 * @brief Convert linear BT.2020 RGB to ICtCp (ITU-R BT.2100).
 *
 * RGB ? LMS (BT.2020) ? L'M'S' (PQ) ? ICtCp matrix.
 *
 * This is the perceptually uniform color space for HDR.
 *
 * @param rgb  Linear BT.2020 RGB.
 * @param ict  Output ICtCp.
 */
void color_bt2020_rgb_to_ictcp(const hdr_rgb_pixel_t *rgb, ictcp_t *ict);

/**
 * @brief Convert ICtCp to linear BT.2020 RGB.
 *
 * @param ict  Input ICtCp.
 * @param rgb  Output linear BT.2020 RGB.
 */
void color_ictcp_to_bt2020_rgb(const ictcp_t *ict, hdr_rgb_pixel_t *rgb);

/**
 * @brief Convert linear RGB in any primary system to CIE XYZ.
 *
 * This is the universal color space conversion function.
 *
 * @param rgb         Linear RGB triplet.
 * @param primaries   RGB primary system.
 * @param xyz         Output CIE XYZ.
 */
void color_rgb_to_xyz_generic(const hdr_rgb_pixel_t *rgb,
                              const hdr_primaries_set_t *primaries,
                              cie_xyz_t *xyz);

/**
 * @brief Convert CIE XYZ to linear RGB in any primary system.
 *
 * @param xyz         Input CIE XYZ.
 * @param primaries   Target RGB primaries.
 * @param rgb         Output linear RGB.
 * @return            0 on success, -1 if XYZ is outside gamut.
 */
int color_xyz_to_rgb_generic(const cie_xyz_t *xyz,
                             const hdr_primaries_set_t *primaries,
                             hdr_rgb_pixel_t *rgb);

/**
 * @brief Compute delta E 2000 color difference.
 *
 * Implements CIE DE2000, the perceptually accurate color difference
 * metric. Accounts for chroma, hue, and luminance dependencies.
 *
 * Reference: Luo, Cui, Rigg (2001). The Development of the CIE 2000
 * Colour-Difference Formula: CIEDE2000.
 *
 * @param lab1  First color in L*a*b*.
 * @param lab2  Second color in L*a*b*.
 * @return      Delta E 2000 value.
 */
double color_delta_e_2000(const cie_lab_t *lab1, const cie_lab_t *lab2);

/* ?? API: Gamut Mapping ??????????????????????????????????????????????? */

/**
 * @brief Initialize gamut mapping configuration.
 *
 * @param config  Configuration to initialize.
 */
void gamut_map_config_init(gamut_map_config_t *config);

/**
 * @brief Compute the xy bounding polygon for a primaries set (gamut boundary).
 *
 * Returns 3 vertices (the RGB primary xy coordinates).
 *
 * @param primaries  RGB primary set.
 * @param polygon    Output array of 3 chromaticities (RGB primaries in xy).
 */
void gamut_get_boundary(const hdr_primaries_set_t *primaries, hdr_chromaticity_t polygon[3]);

/**
 * @brief Test if an xy chromaticity is inside the gamut triangle.
 *
 * Uses barycentric coordinates / signed area method.
 *
 * @param chroma     Chromaticity to test.
 * @param primaries  Gamut to test against.
 * @return           1 if inside, 0 if outside.
 */
int gamut_is_inside(const hdr_chromaticity_t *chroma, const hdr_primaries_set_t *primaries);

/**
 * @brief Map an out-of-gamut color into the target gamut.
 *
 * Supports multiple strategies: clip, soft-clip, desaturate, hue-preserving.
 *
 * @param rgb_in     Input RGB (may be out of gamut).
 * @param src_prim   Source primaries.
 * @param dst_prim   Destination primaries.
 * @param config     Mapping configuration.
 * @param rgb_out    Output RGB (guaranteed in-gamut).
 * @return           0 on success.
 */
int gamut_map_apply(const hdr_rgb_pixel_t *rgb_in,
                    const hdr_primaries_set_t *src_prim,
                    const hdr_primaries_set_t *dst_prim,
                    const gamut_map_config_t *config,
                    hdr_rgb_pixel_t *rgb_out);

/**
 * @brief Compute the gamut volume in CIE L*a*b*.
 *
 * Approximates the gamut by sampling RGB cube vertices and edges,
 * computing the convex hull volume in L*a*b* space.
 *
 * @param primaries  Gamut primaries.
 * @param peak_Y     Peak luminance for the volume computation.
 * @return           Gamut volume (arbitrary units, L*a*b* space).
 */
double gamut_volume_compute(const hdr_primaries_set_t *primaries, double peak_Y);

/**
 * @brief Compute gamut coverage percentage.
 *
 * What fraction of the destination gamut is covered by the source gamut.
 *
 * @param src        Source primaries.
 * @param dst        Destination primaries.
 * @return           Coverage ratio [0.0, 1.0].
 */
double gamut_coverage_ratio(const hdr_primaries_set_t *src, const hdr_primaries_set_t *dst);

/* ?? API: Chroma Subsampling ?????????????????????????????????????????? */

/**
 * @brief Convert 4:4:4 Y'CbCr to 4:2:0 by averaging chroma.
 *
 * Horizontal and vertical 2:1 decimation with optional filter.
 *
 * @param y_444  Full-resolution luma (W x H).
 * @param cb_444 Full-resolution Cb (W x H).
 * @param cr_444 Full-resolution Cr (W x H).
 * @param width  Image width.
 * @param height Image height.
 * @param cb_420 Output Cb (W/2 x H/2).
 * @param cr_420 Output Cr (W/2 x H/2).
 * @param use_filter 1 = apply anti-aliasing filter before decimation.
 */
void chroma_444_to_420(const double *y_444, const double *cb_444, const double *cr_444,
                       int width, int height,
                       double *cb_420, double *cr_420, int use_filter);

/**
 * @brief Convert 4:2:0 Y'CbCr to 4:4:4 by chroma upsampling.
 *
 * Uses bilinear interpolation for chroma reconstruction.
 *
 * @param y_420  Luma (W x H).
 * @param cb_420 Cb (W/2 x H/2).
 * @param cr_420 Cr (W/2 x H/2).
 * @param width  Image width.
 * @param height Image height.
 * @param cb_444 Output Cb (W x H).
 * @param cr_444 Output Cr (W x H).
 */
void chroma_420_to_444(const double *y_420, const double *cb_420, const double *cr_420,
                       int width, int height,
                       double *cb_444, double *cr_444);

/**
 * @brief Convert linear RGB to non-linear Y'CbCr (BT.2020).
 *
 * Applies OETF (PQ or HLG) to luminance, then Y'CbCr matrix.
 *
 * @param rgb       Linear RGB.
 * @param pq        PQ parameters for OETF.
 * @param ycc       Output Y'CbCr triplet (0-1 range).
 */
void color_rgb_to_ycbcr_bt2020(const hdr_rgb_pixel_t *rgb,
                               const hdr_pq_params_t *pq,
                               double ycc[3]);

/**
 * @brief Convert Y'CbCr (BT.2020) to linear RGB.
 *
 * Applies inverse Y'CbCr matrix, then inverse OETF (EOTF).
 *
 * @param ycc       Y'CbCr triplet.
 * @param pq        PQ parameters for EOTF.
 * @param rgb       Output linear RGB.
 */
void color_ycbcr_to_rgb_bt2020(const double ycc[3],
                               const hdr_pq_params_t *pq,
                               hdr_rgb_pixel_t *rgb);

/**
 * @brief Compute luminance Y from linear RGB for BT.2020 primaries.
 *
 * Y = 0.2627*R + 0.6780*G + 0.0593*B  (BT.2020 coefficients).
 *
 * @param rgb  Linear RGB.
 * @return     Luminance Y (cd/m? if RGB is calibrated).
 */
double color_luminance_bt2020(const hdr_rgb_pixel_t *rgb);

/**
 * @brief Compute luminance Y from linear RGB for BT.709/sRGB primaries.
 *
 * Y = 0.2126*R + 0.7152*G + 0.0722*B  (BT.709 coefficients).
 *
 * @param rgb  Linear sRGB.
 * @return     Luminance Y.
 */
double color_luminance_bt709(const hdr_rgb_pixel_t *rgb);

/* ?? API: OKLab (Perceptually Uniform) ???????????????????????????????? */

/**
 * @brief Convert linear sRGB to OKLab (Bjorn Ottosson, 2020).
 *
 * OKLab is a perceptually uniform color space that improves on CIELAB
 * for blue hues and computational efficiency.
 *
 * @param rgb   Linear sRGB.
 * @param lab   Output OKLab (L in [0,1], a and b in [-0.4, 0.4]).
 */
void color_srgb_to_oklab(const hdr_rgb_pixel_t *rgb, cie_lab_t *lab);

/**
 * @brief Convert OKLab to linear sRGB.
 *
 * @param lab   OKLab values.
 * @param rgb   Output linear sRGB.
 */
void color_oklab_to_srgb(const cie_lab_t *lab, hdr_rgb_pixel_t *rgb);

#ifdef __cplusplus
}
#endif

#endif /* HDR_COLOR_H */

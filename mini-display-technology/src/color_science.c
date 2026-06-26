/**
 * color_science.c — Color Science: Space Conversions, Gamma, Colorimetry
 *
 * Implements:
 *   L1: CIE 1931 XYZ, xyY, CIELAB, CIELUV definitions
 *   L2: RGB ↔ XYZ (BT.601/709/2020), RGB ↔ YCbCr
 *   L3: 3×3 color matrices, CMF integration over SPD
 *   L4: Grassmann's additive color mixing, Weber-Fechner gamma basis
 *   L5: DeltaE metrics (1976, 1994, 2000), chromatic adaptation
 *   L6: Gamut coverage computation, gamma calibration
 *
 * Reference:
 *   CIE 15:2018 — Colorimetry
 *   IEC 61966-2-1 — sRGB
 *   ITU-R BT.709, BT.2020, BT.2100
 *   Sharma, "The CIEDE2000 Color-Difference Formula" (2005)
 *   Lindbloom, "RGB/XYZ Matrices" (2003)
 */

#include "color_science.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define PI 3.14159265358979323846

/* ==========================================================================
 * L1: Standard White Points (CIE 1931 xy)
 * ========================================================================== */

static const white_point_t WP_D65   = {0.3127, 0.3290, "D65"};
static const white_point_t WP_D50   = {0.3457, 0.3585, "D50"};
static const white_point_t WP_DCI   = {0.3140, 0.3510, "DCI-P3"};
static const white_point_t WP_E     = {1.0/3.0, 1.0/3.0, "E"};
static const white_point_t WP_D55   = {0.3324, 0.3474, "D55"};
static const white_point_t WP_D75   = {0.2990, 0.3150, "D75"};
static const white_point_t WP_D93   = {0.2831, 0.2971, "D93"};

int white_point_get(const char *name, white_point_t *wp)
{
    if (!name || !wp) return -1;
    if (strcmp(name, "D65") == 0)      { *wp = WP_D65; return 0; }
    if (strcmp(name, "D50") == 0)      { *wp = WP_D50; return 0; }
    if (strcmp(name, "DCI-P3") == 0)   { *wp = WP_DCI; return 0; }
    if (strcmp(name, "E") == 0)        { *wp = WP_E; return 0; }
    if (strcmp(name, "D55") == 0)      { *wp = WP_D55; return 0; }
    if (strcmp(name, "D75") == 0)      { *wp = WP_D75; return 0; }
    if (strcmp(name, "D93") == 0)      { *wp = WP_D93; return 0; }
    return -1;
}

/* ==========================================================================
 * L5: Color Temperature ↔ Chromaticity (Planckian locus approximation)
 * ========================================================================== */

/**
 * McCamy's cubic approximation for correlated color temperature → xy.
 * Valid for 2000K to 20000K.
 *
 * Reference: McCamy, "Correlated Color Temperature as an Explicit Function
 *            of Chromaticity Coordinates" (1992)
 */
cie_xy_t color_temperature_to_xy(double kelvin)
{
    cie_xy_t xy;
    if (kelvin <= 0.0) { xy.x = WP_D65.x; xy.y = WP_D65.y; return xy; }

    /* Kim et al. 2002 improved formula */
    double T3 = kelvin;
    double x, y;

    if (T3 <= 4000.0) {
        x = -0.2661239e9 / (T3 * T3 * T3) - 0.2343589e6 / (T3 * T3) + 0.8776956e3 / T3 + 0.179910;
    } else if (T3 <= 25000.0) {
        x = -3.0258469e9 / (T3 * T3 * T3) + 2.1070379e6 / (T3 * T3) + 0.2226347e3 / T3 + 0.240390;
    } else {
        x = WP_D65.x;
    }

    /* y from parabolic fit to Planckian locus */
    if (T3 <= 2222.0) {
        y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
    } else if (T3 <= 4000.0) {
        y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
    } else {
        y = 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
    }

    xy.x = x;
    xy.y = y;
    return xy;
}

/**
 * McCamy's formula for xy chromaticity → CCT.
 * Reference: McCamy (1992)
 */
double xy_to_cct(const cie_xy_t *xy)
{
    if (!xy) return 6500.0;
    double n = (xy->x - 0.3320) / (xy->y - 0.1858);
    double cct = -449.0 * n * n * n + 3525.0 * n * n - 6823.3 * n + 5520.33;
    if (cct < 1000.0) cct = 1000.0;
    if (cct > 25000.0) cct = 25000.0;
    return cct;
}

/* ==========================================================================
 * L2: RGB ↔ XYZ Conversion Matrices
 * ========================================================================== */

/* BT.709 primaries (HD): linear RGB → CIE XYZ */
static const double bt709_rgb2xyz_mat[3][3] = {
    {0.4123907992659595, 0.357584339383878,  0.1804807884018343},
    {0.2126390058715103, 0.715168678767756,  0.0721923153607337},
    {0.0193308187155919, 0.119194779794626,  0.9505321522496610}
};

static const double bt709_xyz2rgb_mat[3][3] = {
    { 3.240969941904523,  -1.537383177570094, -0.4986107602930034},
    {-0.9692436362808796,  1.8759675015077202, 0.0415550574071756},
    { 0.0556300796969937, -0.203976960993465,  1.0569715142428784}
};

/* BT.2020 primaries (UHD) */
static const double bt2020_rgb2xyz_mat[3][3] = {
    {0.6369580483012914, 0.1446169035862083, 0.1688809751641721},
    {0.2627002120112671, 0.6779980715188708, 0.0593017164698619},
    {0.0000000000000000, 0.0280726930490874, 1.0609850577107909}
};

static const double bt2020_xyz2rgb_mat[3][3] = {
    { 1.716651187971268, -0.355670783776392, -0.253366281373660},
    {-0.666684351832489,  1.616481236634939,  0.015768545813911},
    { 0.017639857445311, -0.042770612972904,  0.942103121235474}
};

/* sRGB primaries (same as BT.709) */

/* BT.601 (SD NTSC) primaries */
static const double bt601_n_rgb_to_xyz[3][3] = {
    {0.3935891, 0.3652497, 0.1916313},
    {0.2124132, 0.7010437, 0.0865432},
    {0.0187423, 0.1119313, 0.9581563}
};

/* ==========================================================================
 * L2: sRGB ↔ XYZ
 * ========================================================================== */

cie_xyz_t srgb_to_xyz(const pixel_rgb_t *srgb)
{
    cie_xyz_t xyz = {0.0, 0.0, 0.0};
    if (!srgb || srgb->max_val == 0) return xyz;

    double rf = (double)srgb->r / srgb->max_val;
    double gf = (double)srgb->g / srgb->max_val;
    double bf = (double)srgb->b / srgb->max_val;

    /* sRGB inverse gamma (linearize) */
    double rl = transfer_srgb_to_linear(rf);
    double gl = transfer_srgb_to_linear(gf);
    double bl = transfer_srgb_to_linear(bf);

    xyz.X = rl * bt709_rgb2xyz_mat[0][0] + gl * bt709_rgb2xyz_mat[0][1] + bl * bt709_rgb2xyz_mat[0][2];
    xyz.Y = rl * bt709_rgb2xyz_mat[1][0] + gl * bt709_rgb2xyz_mat[1][1] + bl * bt709_rgb2xyz_mat[1][2];
    xyz.Z = rl * bt709_rgb2xyz_mat[2][0] + gl * bt709_rgb2xyz_mat[2][1] + bl * bt709_rgb2xyz_mat[2][2];

    return xyz;
}

pixel_rgb_t xyz_to_srgb(const cie_xyz_t *xyz)
{
    pixel_rgb_t out;
    out.max_val = 255;
    if (!xyz) { out.r = out.g = out.b = out.a = 0; return out; }

    double rl = xyz->X * bt709_xyz2rgb_mat[0][0] + xyz->Y * bt709_xyz2rgb_mat[0][1] + xyz->Z * bt709_xyz2rgb_mat[0][2];
    double gl = xyz->X * bt709_xyz2rgb_mat[1][0] + xyz->Y * bt709_xyz2rgb_mat[1][1] + xyz->Z * bt709_xyz2rgb_mat[1][2];
    double bl = xyz->X * bt709_xyz2rgb_mat[2][0] + xyz->Y * bt709_xyz2rgb_mat[2][1] + xyz->Z * bt709_xyz2rgb_mat[2][2];

    /* Clamp and gamma-encode */
    double rf = transfer_srgb_from_linear(rl > 0 ? rl : 0);
    double gf = transfer_srgb_from_linear(gl > 0 ? gl : 0);
    double bf = transfer_srgb_from_linear(bl > 0 ? bl : 0);

    out.r = (uint16_t)(rf * 255.0 + 0.5);
    out.g = (uint16_t)(gf * 255.0 + 0.5);
    out.b = (uint16_t)(bf * 255.0 + 0.5);
    out.a = 255;
    if (out.r > 255) out.r = 255;
    if (out.g > 255) out.g = 255;
    if (out.b > 255) out.b = 255;
    return out;
}

/* ==========================================================================
 * L2: BT.709 RGB ↔ XYZ
 * ========================================================================== */

cie_xyz_t bt709_rgb_to_xyz(const pixel_float_t *rgb)
{
    cie_xyz_t xyz = {0.0, 0.0, 0.0};
    if (!rgb) return xyz;
    xyz.X = rgb->r * bt709_rgb2xyz_mat[0][0] + rgb->g * bt709_rgb2xyz_mat[0][1] + rgb->b * bt709_rgb2xyz_mat[0][2];
    xyz.Y = rgb->r * bt709_rgb2xyz_mat[1][0] + rgb->g * bt709_rgb2xyz_mat[1][1] + rgb->b * bt709_rgb2xyz_mat[1][2];
    xyz.Z = rgb->r * bt709_rgb2xyz_mat[2][0] + rgb->g * bt709_rgb2xyz_mat[2][1] + rgb->b * bt709_rgb2xyz_mat[2][2];
    return xyz;
}

pixel_float_t xyz_to_bt709_rgb(const cie_xyz_t *xyz)
{
    pixel_float_t out = {0.0, 0.0, 0.0, 1.0};
    if (!xyz) return out;
    out.r = xyz->X * bt709_xyz2rgb_mat[0][0] + xyz->Y * bt709_xyz2rgb_mat[0][1] + xyz->Z * bt709_xyz2rgb_mat[0][2];
    out.g = xyz->X * bt709_xyz2rgb_mat[1][0] + xyz->Y * bt709_xyz2rgb_mat[1][1] + xyz->Z * bt709_xyz2rgb_mat[1][2];
    out.b = xyz->X * bt709_xyz2rgb_mat[2][0] + xyz->Y * bt709_xyz2rgb_mat[2][1] + xyz->Z * bt709_xyz2rgb_mat[2][2];
    return out;
}

/* ==========================================================================
 * L2: BT.2020 RGB ↔ XYZ
 * ========================================================================== */

cie_xyz_t bt2020_rgb_to_xyz(const pixel_float_t *rgb)
{
    cie_xyz_t xyz = {0.0, 0.0, 0.0};
    if (!rgb) return xyz;
    xyz.X = rgb->r * bt2020_rgb2xyz_mat[0][0] + rgb->g * bt2020_rgb2xyz_mat[0][1] + rgb->b * bt2020_rgb2xyz_mat[0][2];
    xyz.Y = rgb->r * bt2020_rgb2xyz_mat[1][0] + rgb->g * bt2020_rgb2xyz_mat[1][1] + rgb->b * bt2020_rgb2xyz_mat[1][2];
    xyz.Z = rgb->r * bt2020_rgb2xyz_mat[2][0] + rgb->g * bt2020_rgb2xyz_mat[2][1] + rgb->b * bt2020_rgb2xyz_mat[2][2];
    return xyz;
}

pixel_float_t xyz_to_bt2020_rgb(const cie_xyz_t *xyz)
{
    pixel_float_t out = {0.0, 0.0, 0.0, 1.0};
    if (!xyz) return out;
    out.r = xyz->X * bt2020_xyz2rgb_mat[0][0] + xyz->Y * bt2020_xyz2rgb_mat[0][1] + xyz->Z * bt2020_xyz2rgb_mat[0][2];
    out.g = xyz->X * bt2020_xyz2rgb_mat[1][0] + xyz->Y * bt2020_xyz2rgb_mat[1][1] + xyz->Z * bt2020_xyz2rgb_mat[1][2];
    out.b = xyz->X * bt2020_xyz2rgb_mat[2][0] + xyz->Y * bt2020_xyz2rgb_mat[2][1] + xyz->Z * bt2020_xyz2rgb_mat[2][2];
    return out;
}

/* ==========================================================================
 * L2: RGB ↔ YCbCr (BT.601 / BT.709)
 * ========================================================================== */

pixel_ycbcr_t rgb_to_ycbcr_bt709(const pixel_float_t *rgb)
{
    pixel_ycbcr_t ycc = {0.0, 0.0, 0.0};
    if (!rgb) return ycc;
    /* ITU-R BT.709 */
    ycc.y  = 0.2126 * rgb->r + 0.7152 * rgb->g + 0.0722 * rgb->b;
    ycc.cb = (rgb->b - ycc.y) / (2.0 * 0.9278); /* 1 - 0.0722 */
    ycc.cr = (rgb->r - ycc.y) / (2.0 * 0.7874); /* 1 - 0.2126 */
    return ycc;
}

pixel_float_t ycbcr_to_rgb_bt709(const pixel_ycbcr_t *ycc)
{
    pixel_float_t rgb = {0.0, 0.0, 0.0, 1.0};
    if (!ycc) return rgb;
    rgb.r = ycc->y + 2.0 * 0.7874 * ycc->cr;
    rgb.g = ycc->y - 2.0 * 0.9278 * 0.0722 / 0.7152 * ycc->cb
                  - 2.0 * 0.7874 * 0.2126 / 0.7152 * ycc->cr;
    rgb.b = ycc->y + 2.0 * 0.9278 * ycc->cb;
    return rgb;
}

pixel_ycbcr_t rgb_to_ycbcr_bt601(const pixel_float_t *rgb)
{
    pixel_ycbcr_t ycc = {0.0, 0.0, 0.0};
    if (!rgb) return ycc;
    /* ITU-R BT.601 */
    ycc.y  = 0.299 * rgb->r + 0.587 * rgb->g + 0.114 * rgb->b;
    ycc.cb = (rgb->b - ycc.y) / (2.0 * 0.886);
    ycc.cr = (rgb->r - ycc.y) / (2.0 * 0.701);
    return ycc;
}

pixel_float_t ycbcr_to_rgb_bt601(const pixel_ycbcr_t *ycc)
{
    pixel_float_t rgb = {0.0, 0.0, 0.0, 1.0};
    if (!ycc) return rgb;
    rgb.r = ycc->y + 2.0 * 0.701 * ycc->cr;
    rgb.g = ycc->y - 2.0 * 0.886 * 0.114 / 0.587 * ycc->cb
                  - 2.0 * 0.701 * 0.299 / 0.587 * ycc->cr;
    rgb.b = ycc->y + 2.0 * 0.886 * ycc->cb;
    return rgb;
}

/* ==========================================================================
 * L2: XYZ ↔ CIELAB / CIELUV
 * ========================================================================== */

cie_lab_t xyz_to_lab(const cie_xyz_t *xyz)
{
    cie_lab_t lab = {0.0, 0.0, 0.0};
    if (!xyz) return lab;

    /* Reference: D65 white point */
    double Xn = 0.95047, Yn = 1.00000, Zn = 1.08883;

    /* CIE lab forward function f(t) */
    double delta_lab = 6.0 / 29.0;
    double delta3 = delta_lab * delta_lab * delta_lab;

    double fx = (xyz->X / Xn) > delta3 ? cbrt(xyz->X / Xn)
               : (xyz->X / Xn) / (3.0 * delta_lab * delta_lab) + 4.0 / 29.0;
    double fy = (xyz->Y / Yn) > delta3 ? cbrt(xyz->Y / Yn)
               : (xyz->Y / Yn) / (3.0 * delta_lab * delta_lab) + 4.0 / 29.0;
    double fz = (xyz->Z / Zn) > delta3 ? cbrt(xyz->Z / Zn)
               : (xyz->Z / Zn) / (3.0 * delta_lab * delta_lab) + 4.0 / 29.0;

    lab.L = 116.0 * fy - 16.0;
    lab.a = 500.0 * (fx - fy);
    lab.b = 200.0 * (fy - fz);
    return lab;
}

cie_xyz_t lab_to_xyz(const cie_lab_t *lab)
{
    cie_xyz_t xyz = {0.0, 0.0, 0.0};
    if (!lab) return xyz;

    double Xn = 0.95047, Yn = 1.00000, Zn = 1.08883;
    double delta = 6.0 / 29.0;

    double fy = (lab->L + 16.0) / 116.0;
    double fx = lab->a / 500.0 + fy;
    double fz = fy - lab->b / 200.0;

    /* Inverse forward function */
    xyz.X = Xn * ((fx > delta) ? fx * fx * fx
                            : 3.0 * delta * delta * (fx - 4.0 / 29.0));
    xyz.Y = Yn * ((fy > delta) ? fy * fy * fy
                            : 3.0 * delta * delta * (fy - 4.0 / 29.0));
    xyz.Z = Zn * ((fz > delta) ? fz * fz * fz
                            : 3.0 * delta * delta * (fz - 4.0 / 29.0));
    return xyz;
}

cie_luv_t xyz_to_luv(const cie_xyz_t *xyz)
{
    cie_luv_t luv = {0.0, 0.0, 0.0};
    if (!xyz) return luv;

    double Xn = 0.95047, Yn = 1.00000, Zn = 1.08883;
    double yr = xyz->Y / Yn;

    double delta = 6.0 / 29.0;
    if (yr > delta * delta * delta)
        luv.L = 116.0 * cbrt(yr) - 16.0;
    else
        luv.L = (29.0 / 3.0) * (29.0 / 3.0) * (29.0 / 3.0) * yr;

    double denom = xyz->X + 15.0 * xyz->Y + 3.0 * xyz->Z;
    if (denom <= 0.0) { luv.u = 0.0; luv.v = 0.0; return luv; }

    double up = 4.0 * xyz->X / denom;
    double vp = 9.0 * xyz->Y / denom;

    double upn = 4.0 * Xn / (Xn + 15.0 * Yn + 3.0 * Zn);
    double vpn = 9.0 * Yn / (Xn + 15.0 * Yn + 3.0 * Zn);

    luv.u = 13.0 * luv.L * (up - upn);
    luv.v = 13.0 * luv.L * (vp - vpn);
    return luv;
}

cie_xyz_t luv_to_xyz(const cie_luv_t *luv)
{
    cie_xyz_t xyz = {0.0, 0.0, 0.0};
    if (!luv || luv->L <= 0.0) return xyz;

    double Xn = 0.95047, Yn = 1.00000, Zn = 1.08883;
    double upn = 4.0 * Xn / (Xn + 15.0 * Yn + 3.0 * Zn);
    double vpn = 9.0 * Yn / (Xn + 15.0 * Yn + 3.0 * Zn);

    double delta = 6.0 / 29.0;
    if (luv->L > 8.0) {
        xyz.Y = Yn * ((luv->L + 16.0) / 116.0) * ((luv->L + 16.0) / 116.0) * ((luv->L + 16.0) / 116.0);
    } else {
        xyz.Y = Yn * luv->L * (3.0 / 29.0) * (3.0 / 29.0) * (3.0 / 29.0);
    }

    double up = (luv->L > 0.0) ? luv->u / (13.0 * luv->L) + upn : upn;
    double vp = (luv->L > 0.0) ? luv->v / (13.0 * luv->L) + vpn : vpn;

    if (vp == 0.0) return xyz;
    xyz.X = xyz.Y * (9.0 * up) / (4.0 * vp);
    xyz.Z = xyz.Y * (12.0 - 3.0 * up - 20.0 * vp) / (4.0 * vp);
    return xyz;
}

/* ==========================================================================
 * L2: XYZ ↔ xyY
 * ========================================================================== */

cie_xyy_t xyz_to_xyy(const cie_xyz_t *xyz)
{
    cie_xyy_t xyy = {WP_D65.x, WP_D65.y, 0.0};
    if (!xyz) return xyy;
    double sum = xyz->X + xyz->Y + xyz->Z;
    if (sum <= 0.0) return xyy;
    xyy.x = xyz->X / sum;
    xyy.y = xyz->Y / sum;
    xyy.Y = xyz->Y;
    return xyy;
}

cie_xyz_t xyy_to_xyz(const cie_xyy_t *xyy)
{
    cie_xyz_t xyz = {0.0, 0.0, 0.0};
    if (!xyy || xyy->y <= 0.0) return xyz;
    xyz.X = xyy->x * xyy->Y / xyy->y;
    xyz.Y = xyy->Y;
    xyz.Z = (1.0 - xyy->x - xyy->y) * xyy->Y / xyy->y;
    return xyz;
}

/* ==========================================================================
 * L5: Delta-E Color Difference Metrics
 * ========================================================================== */

double delta_e_1976(const cie_lab_t *a, const cie_lab_t *b)
{
    if (!a || !b) return 0.0;
    double dL = a->L - b->L;
    double da = a->a - b->a;
    double db = a->b - b->b;
    return sqrt(dL * dL + da * da + db * db);
}

/**
 * CIE 1994 ΔE (CIE94).
 * Uses chromatic weighting factors kL, kC, kH.
 * Graphics arts: kL=1, kC=1, kH=1. Textiles: kL=2, kC=1, kH=1.
 */
double delta_e_1994(const cie_lab_t *ref, const cie_lab_t *sample,
                    int is_textiles)
{
    if (!ref || !sample) return 0.0;

    double kL = is_textiles ? 2.0 : 1.0;
    double kC = 1.0, kH = 1.0;
    double k1 = is_textiles ? 0.048 : 0.045;
    double k2 = is_textiles ? 0.014 : 0.015;

    double dL = ref->L - sample->L;
    double C1 = sqrt(ref->a * ref->a + ref->b * ref->b);
    double C2 = sqrt(sample->a * sample->a + sample->b * sample->b);
    double dC = C1 - C2;
    double da = ref->a - sample->a;
    double db = ref->b - sample->b;
    double dH2 = da * da + db * db - dC * dC;
    double dH = (dH2 > 0.0) ? sqrt(dH2) : 0.0;

    double SL = 1.0;
    double SC = 1.0 + k1 * C1;
    double SH = 1.0 + k2 * C1;

    double termL = dL / (kL * SL);
    double termC = dC / (kC * SC);
    double termH = dH / (kH * SH);

    return sqrt(termL * termL + termC * termC + termH * termH);
}

/**
 * CIE 2000 ΔE (CIEDE2000) — the most accurate color difference formula.
 *
 * Reference: Sharma, Wu, & Dalal, "The CIEDE2000 Color-Difference Formula"
 *            Color Research & Application, 30(1), 2005.
 *
 * Complexity: O(1). Uses hue rotation term, compensation for neutral
 * colors, and lightness/chroma/hue weighting functions.
 */
double delta_e_2000(const cie_lab_t *ref, const cie_lab_t *sample)
{
    if (!ref || !sample) return 0.0;

    double Lp_avg = (ref->L + sample->L) / 2.0;
    double C1 = sqrt(ref->a * ref->a + ref->b * ref->b);
    double C2 = sqrt(sample->a * sample->a + sample->b * sample->b);
    double Cp_avg = (C1 + C2) / 2.0;

    double G = 0.5 * (1.0 - sqrt(pow(Cp_avg, 7) / (pow(Cp_avg, 7) + pow(25.0, 7))));
    double a1p = ref->a * (1.0 + G);
    double a2p = sample->a * (1.0 + G);

    double C1p = sqrt(a1p * a1p + ref->b * ref->b);
    double C2p = sqrt(a2p * a2p + sample->b * sample->b);

    double h1p = atan2(ref->b, a1p);
    if (h1p < 0.0) h1p += 2.0 * PI;
    double h2p = atan2(sample->b, a2p);
    if (h2p < 0.0) h2p += 2.0 * PI;

    double dLp = sample->L - ref->L;
    double dCp = C2p - C1p;

    double dhp;
    if (C1p * C2p == 0.0) {
        dhp = 0.0;
    } else {
        dhp = h2p - h1p;
        if (dhp > PI) dhp -= 2.0 * PI;
        if (dhp < -PI) dhp += 2.0 * PI;
    }
    double dHp = 2.0 * sqrt(C1p * C2p) * sin(dhp / 2.0);

    double Lp_avg2 = (ref->L + sample->L) / 2.0;
    double Cp_avg2 = (C1p + C2p) / 2.0;

    double hp_avg;
    if (C1p * C2p == 0.0) {
        hp_avg = h1p + h2p;
    } else {
        hp_avg = (h1p + h2p) / 2.0;
        if (fabs(h1p - h2p) > PI) hp_avg -= PI;
    }
    if (hp_avg < 0.0) hp_avg += 2.0 * PI;

    double T = 1.0 - 0.17 * cos(hp_avg - PI / 6.0)
                  + 0.24 * cos(2.0 * hp_avg)
                  + 0.32 * cos(3.0 * hp_avg + PI / 30.0)
                  - 0.20 * cos(4.0 * hp_avg - 63.0 * PI / 180.0);

    double dTheta = 30.0 * PI / 180.0 * exp(-((hp_avg - 275.0 * PI / 180.0) / (25.0 * PI / 180.0)) * ((hp_avg - 275.0 * PI / 180.0) / (25.0 * PI / 180.0)));
    double RC = 2.0 * sqrt(pow(Cp_avg2, 7) / (pow(Cp_avg2, 7) + pow(25.0, 7)));
    double SL = 1.0 + (0.015 * (Lp_avg2 - 50.0) * (Lp_avg2 - 50.0)) / sqrt(20.0 + (Lp_avg2 - 50.0) * (Lp_avg2 - 50.0));
    double SC = 1.0 + 0.045 * Cp_avg2;
    double SH = 1.0 + 0.015 * Cp_avg2 * T;
    double RT = -sin(2.0 * dTheta) * RC;

    double dE = sqrt(pow(dLp / SL, 2) + pow(dCp / SC, 2) + pow(dHp / SH, 2) + RT * (dCp / SC) * (dHp / SH));
    return dE;
}

/* ==========================================================================
 * L2: Gamut Operations
 * ========================================================================== */

int gamut_contains_xy(const rgb_primaries_t *primaries, const cie_xy_t *xy)
{
    if (!primaries || !xy) return 0;
    /* Point-in-triangle test using barycentric coordinates */
    double x = xy->x, y = xy->y;
    double x1 = primaries->red.x, y1 = primaries->red.y;
    double x2 = primaries->green.x, y2 = primaries->green.y;
    double x3 = primaries->blue.x, y3 = primaries->blue.y;

    double denom = ((y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3));
    if (fabs(denom) < 1e-12) return 0;
    double a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / denom;
    double b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / denom;
    double c = 1.0 - a - b;
    return (a >= -1e-9 && b >= -1e-9 && c >= -1e-9);
}

/** Area of triangle in CIE xy plane */
static double triangle_area_xy(const cie_xy_t *a, const cie_xy_t *b, const cie_xy_t *c)
{
    return 0.5 * fabs(a->x * (b->y - c->y) + b->x * (c->y - a->y) + c->x * (a->y - b->y));
}

double gamut_coverage(const rgb_primaries_t *measured,
                      const rgb_primaries_t *reference)
{
    if (!measured || !reference) return 0.0;

    double area_ref = triangle_area_xy(&reference->red, &reference->green, &reference->blue);
    if (area_ref <= 0.0) return 0.0;

    /* Approximate intersection area via sampling (simple approach) */
    int hits = 0;
    int total = 500;
    for (int i = 0; i < total; i++) {
        double t1 = (double)rand() / RAND_MAX;
        double t2 = (double)rand() / RAND_MAX;
        if (t1 + t2 > 1.0) { t1 = 1.0 - t1; t2 = 1.0 - t2; }
        double t3 = 1.0 - t1 - t2;
        cie_xy_t pt;
        pt.x = t1 * reference->red.x + t2 * reference->green.x + t3 * reference->blue.x;
        pt.y = t1 * reference->red.y + t2 * reference->green.y + t3 * reference->blue.y;
        if (gamut_contains_xy(measured, &pt)) hits++;
    }
    return (double)hits / (double)total;
}

color_matrix_t primaries_to_matrix(const rgb_primaries_t *p)
{
    color_matrix_t m = {{{0}}};
    if (!p) return m;
    /* Solve for XYZ-to-RGB matrix using primaries and white point */
    double xr = p->red.x, yr = p->red.y;
    double xg = p->green.x, yg = p->green.y;
    double xb = p->blue.x, yb = p->blue.y;
    double xw = p->white.x, yw = p->white.y;

    /* Use the linear system method */
    double Xr = xr / yr, Yr = 1.0, Zr = (1.0 - xr - yr) / yr;
    double Xg = xg / yg, Yg = 1.0, Zg = (1.0 - xg - yg) / yg;
    double Xb = xb / yb, Yb = 1.0, Zb = (1.0 - xb - yb) / yb;
    double Xw = xw / yw, Yw = 1.0, Zw = (1.0 - xw - yw) / yw;

    /* Solve for S */
    double detA = Xr*(Yg*Zb - Zg*Yb) - Yr*(Xg*Zb - Zg*Xb) + Zr*(Xg*Yb - Yg*Xb);
    if (fabs(detA) < 1e-12) return m;
    double Sr = (Xw*(Yg*Zb - Zg*Yb) - Yw*(Xg*Zb - Zg*Xb) + Zw*(Xg*Yb - Yg*Xb)) / detA;
    double Sg = (Xr*(Yw*Zb - Zw*Yb) - Yr*(Xw*Zb - Zw*Xb) + Zr*(Xw*Yb - Yw*Xb)) / detA;
    double Sb = (Xr*(Yg*Zw - Zg*Yw) - Yr*(Xg*Zw - Zg*Xw) + Zr*(Xg*Yw - Yg*Xw)) / detA;

    /* XYZ → RGB matrix */
    m.m[0][0] = Sr * Xr; m.m[0][1] = Sg * Xg; m.m[0][2] = Sb * Xb;
    m.m[1][0] = Sr * Yr; m.m[1][1] = Sg * Yg; m.m[1][2] = Sb * Yb;
    m.m[2][0] = Sr * Zr; m.m[2][1] = Sg * Zg; m.m[2][2] = Sb * Zb;

    return m;
}

color_matrix_t primaries_to_matrix_inv(const rgb_primaries_t *p)
{
    color_matrix_t m = primaries_to_matrix(p);
    /* Invert 3×3 matrix using cofactor expansion */
    double a = m.m[0][0], b = m.m[0][1], c = m.m[0][2];
    double d = m.m[1][0], e = m.m[1][1], f = m.m[1][2];
    double g = m.m[2][0], h = m.m[2][1], i = m.m[2][2];

    double det = a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);
    if (fabs(det) < 1e-12) {
        color_matrix_t zero = {{{0}}};
        return zero;
    }
    color_matrix_t inv;
    inv.m[0][0] = (e*i - f*h) / det;
    inv.m[0][1] = (c*h - b*i) / det;
    inv.m[0][2] = (b*f - c*e) / det;
    inv.m[1][0] = (f*g - d*i) / det;
    inv.m[1][1] = (a*i - c*g) / det;
    inv.m[1][2] = (c*d - a*f) / det;
    inv.m[2][0] = (d*h - e*g) / det;
    inv.m[2][1] = (b*g - a*h) / det;
    inv.m[2][2] = (a*e - b*d) / det;
    return inv;
}

void color_matrix_vec_mul(const color_matrix_t *mat,
                          double x, double y, double z,
                          double *rx, double *ry, double *rz)
{
    if (!mat) { if(rx)*rx=0; if(ry)*ry=0; if(rz)*rz=0; return; }
    double mx = mat->m[0][0]*x + mat->m[0][1]*y + mat->m[0][2]*z;
    double my = mat->m[1][0]*x + mat->m[1][1]*y + mat->m[1][2]*z;
    double mz = mat->m[2][0]*x + mat->m[2][1]*y + mat->m[2][2]*z;
    if (rx) *rx = mx; if (ry) *ry = my; if (rz) *rz = mz;
}

color_matrix_t color_matrix_mul(const color_matrix_t *a,
                                const color_matrix_t *b)
{
    color_matrix_t c = {{{0}}};
    if (!a || !b) return c;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                c.m[i][j] += a->m[i][k] * b->m[k][j];
    return c;
}

/* ==========================================================================
 * L5: Chromatic Adaptation (Bradford)
 * ========================================================================== */

color_matrix_t chromatic_adaptation_bradford(const white_point_t *src,
                                              const white_point_t *dst)
{
    color_matrix_t ident;
    memset(&ident, 0, sizeof(ident));
    ident.m[0][0] = 1.0; ident.m[1][1] = 1.0; ident.m[2][2] = 1.0;
    if (!src || !dst) return ident;

    /* Bradford LMS matrix */
    double M[3][3] = {
        { 0.8951000,  0.2664000, -0.1614000},
        {-0.7502000,  1.7135000,  0.0367000},
        { 0.0389000, -0.0685000,  1.0296000}
    };
    double M_inv[3][3] = {
        { 0.9869929, -0.1470543,  0.1599627},
        { 0.4323053,  0.5183603,  0.0492912},
        {-0.0085287,  0.0400428,  0.9684867}
    };

    /* Convert source/dest white to XYZ */
    double Xs = src->x / src->y;
    double Ys = 1.0;
    double Zs = (1.0 - src->x - src->y) / src->y;
    double Xd = dst->x / dst->y;
    double Yd = 1.0;
    double Zd = (1.0 - dst->x - dst->y) / dst->y;

    /* LMSs = M * XYZs */
    double Ls = M[0][0]*Xs + M[0][1]*Ys + M[0][2]*Zs;
    double Ms = M[1][0]*Xs + M[1][1]*Ys + M[1][2]*Zs;
    double Ss = M[2][0]*Xs + M[2][1]*Ys + M[2][2]*Zs;
    double Ld = M[0][0]*Xd + M[0][1]*Yd + M[0][2]*Zd;
    double Md = M[1][0]*Xd + M[1][1]*Yd + M[1][2]*Zd;
    double Sd = M[2][0]*Xd + M[2][1]*Yd + M[2][2]*Zd;

    if (Ls == 0 || Ms == 0 || Ss == 0) return ident;

    /* Build diagonal scale matrix D */
    double D_L = Ld / Ls, D_M = Md / Ms, D_S = Sd / Ss;

    /* Adaptation matrix = M^-1 * D * M */
    color_matrix_t adapt = {{{0}}};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double sum = 0.0;
            for (int k = 0; k < 3; k++) {
                double Dk = (k == 0) ? D_L : (k == 1) ? D_M : D_S;
                sum += M_inv[i][k] * Dk * M[k][j];
            }
            adapt.m[i][j] = sum;
        }
    }
    return adapt;
}

cie_xyz_t adapt_xyz(const cie_xyz_t *xyz, const color_matrix_t *adapt)
{
    cie_xyz_t out = {0.0, 0.0, 0.0};
    if (!xyz || !adapt) return out;
    out.X = adapt->m[0][0]*xyz->X + adapt->m[0][1]*xyz->Y + adapt->m[0][2]*xyz->Z;
    out.Y = adapt->m[1][0]*xyz->X + adapt->m[1][1]*xyz->Y + adapt->m[1][2]*xyz->Z;
    out.Z = adapt->m[2][0]*xyz->X + adapt->m[2][1]*xyz->Y + adapt->m[2][2]*xyz->Z;
    return out;
}

/* ==========================================================================
 * L2: Transfer Functions (Gamma)
 * ========================================================================== */

double transfer_srgb_from_linear(double linear)
{
    if (linear <= 0.0031308)
        return 12.92 * linear;
    return 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

double transfer_srgb_to_linear(double srgb)
{
    if (srgb <= 0.04045)
        return srgb / 12.92;
    return pow((srgb + 0.055) / 1.055, 2.4);
}

double transfer_power_encode(double linear, double gamma)
{
    if (linear <= 0.0) return 0.0;
    if (gamma <= 0.0) return linear;
    return pow(linear, 1.0 / gamma);
}

double transfer_power_decode(double encoded, double gamma)
{
    if (encoded <= 0.0) return 0.0;
    if (gamma <= 0.0) return encoded;
    return pow(encoded, gamma);
}

/**
 * SMPTE ST 2084 Perceptual Quantizer (PQ) OETF.
 * Maps linear light [0, peak_nits] to PQ [0, 1].
 *
 * Parameters: m1 = 1305/8192, m2 = 2523/32, c1 = 107/128, c2 = 2413/128, c3 = 2392/128
 */
double transfer_pq_from_linear(double linear_nits, double peak_nits)
{
    if (linear_nits <= 0.0 || peak_nits <= 0.0) return 0.0;
    double L = linear_nits / peak_nits; /* Normalize to [0, 1] */
    if (L <= 0.0) return 0.0;

    const double m1 = 0.1593017578125;  /* 1305 / 8192 */
    const double m2 = 78.84375;          /* 2523 / 32 */
    const double c1 = 0.8359375;         /* 107 / 128 */
    const double c2 = 18.8515625;        /* 2413 / 128 */
    const double c3 = 18.6875;           /* 2392 / 128 */

    double L_pow = pow(L, m1);
    double numerator = c1 + c2 * L_pow;
    double denominator = 1.0 + c3 * L_pow;
    return pow(numerator / denominator, m2);
}

double transfer_pq_to_linear(double pq_code, double peak_nits)
{
    if (pq_code <= 0.0 || peak_nits <= 0.0) return 0.0;
    const double m1 = 0.1593017578125;
    const double m2 = 78.84375;
    const double c1 = 0.8359375;
    const double c2 = 18.8515625;
    const double c3 = 18.6875;

    double pq_pow = pow(pq_code, 1.0 / m2);
    double numerator = pq_pow - c1;
    if (numerator < 0.0) numerator = 0.0;
    double denominator = c2 - c3 * pq_pow;
    if (denominator <= 0.0) return 0.0;
    double L = pow(numerator / denominator, 1.0 / m1);
    return L * peak_nits;
}

/**
 * HLG (Hybrid Log-Gamma) OETF per ARIB STD-B67 / BT.2100.
 * Maps scene linear light [0, ∞) to HLG signal [0, 1].
 */
double transfer_hlg_from_linear(double scene_linear)
{
    if (scene_linear <= 0.0) return 0.0;
    const double a = 0.17883277;
    const double b = 0.28466892;  /* 1 - 4*a */
    const double c = 0.55991073;

    if (scene_linear <= 1.0 / 12.0) {
        return sqrt(3.0 * scene_linear);
    }
    return a * log(12.0 * scene_linear - b) + c;
}

double transfer_hlg_to_linear(double hlg_signal, double peak_nits)
{
    if (hlg_signal <= 0.0) return 0.0;
    const double a = 0.17883277;
    const double b = 0.28466892;
    const double c = 0.55991073;

    double linear;
    if (hlg_signal <= 0.5) {
        linear = hlg_signal * hlg_signal / 3.0;
    } else {
        linear = (exp((hlg_signal - c) / a) + b) / 12.0;
    }
    return linear * peak_nits / 1000.0; /* OOTF applied with peak luminance */
}

/**
 * BT.1886 EOTF — reference display electro-optical transfer function.
 *
 * L = (V + b)^γ  where b = black_level and γ = 2.4
 * V is normalized [0, 1].
 */
double transfer_bt1886_to_linear(double encoded, double black_level,
                                 double peak_white)
{
    if (encoded <= 0.0) return black_level;
    if (peak_white <= 0.0) peak_white = 1.0;
    double gamma = 2.4;
    double L = pow(encoded + black_level, gamma);
    return L * peak_white;
}

/* ==========================================================================
 * L1/L2: Color Depth / Range Utilities
 * ========================================================================== */

void color_depth_init(color_depth_t *cd, uint8_t bits, int full_range)
{
    if (!cd) return;
    cd->bits_per_channel = bits;
    cd->uses_full_range = full_range;
    if (bits == 8) {
        cd->code_max = full_range ? 255 : 235;
        cd->code_min = full_range ? 0 : 16;
    } else if (bits == 10) {
        cd->code_max = full_range ? 1023 : 940;
        cd->code_min = full_range ? 0 : 64;
    } else if (bits == 12) {
        cd->code_max = full_range ? 4095 : 3760;
        cd->code_min = full_range ? 0 : 256;
    } else {
        cd->code_max = (1U << bits) - 1;
        cd->code_min = 0;
    }
}

uint16_t float_to_code(double val, const color_depth_t *cd)
{
    if (!cd) return 0;
    if (val < 0.0) val = 0.0;
    if (val > 1.0) val = 1.0;
    uint16_t range = cd->code_max - cd->code_min;
    return cd->code_min + (uint16_t)(val * (double)range + 0.5);
}

double code_to_float(uint16_t code, const color_depth_t *cd)
{
    if (!cd) return 0.0;
    uint16_t range = cd->code_max - cd->code_min;
    if (range == 0) return 0.0;
    double val = (double)(code - cd->code_min) / (double)range;
    if (val < 0.0) val = 0.0;
    if (val > 1.0) val = 1.0;
    return val;
}

double contrast_ratio_calc(double white_lum, double black_lum)
{
    if (black_lum <= 0.0) return INFINITY;
    return white_lum / black_lum;
}

void luminance_spec_init(luminance_spec_t *spec, double black, double white,
                         double gamma, double wx, double wy)
{
    if (!spec) return;
    spec->min_luminance_cdm2 = black;
    spec->max_luminance_cdm2 = white;
    spec->gamma_value = gamma;
    spec->white_point_x = wx;
    spec->white_point_y = wy;
    spec->contrast_ratio = contrast_ratio_calc(white, black);
}

double estimate_gamma_from_ramp(const double *codes, const double *luminances,
                                int n_samples)
{
    if (!codes || !luminances || n_samples < 3) return 2.2;
    /* Linear regression on log-log scale: log(L) = γ * log(V) + log(k) */
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    int count = 0;
    for (int i = 0; i < n_samples; i++) {
        if (codes[i] <= 0.0 || luminances[i] <= 0.0) continue;
        double lx = log(codes[i]);
        double ly = log(luminances[i]);
        sum_x += lx;
        sum_y += ly;
        sum_xy += lx * ly;
        sum_xx += lx * lx;
        count++;
    }
    if (count < 2) return 2.2;
    double denom = count * sum_xx - sum_x * sum_x;
    if (fabs(denom) < 1e-12) return 2.2;
    double gamma = (count * sum_xy - sum_x * sum_y) / denom;
    if (gamma < 0.5) gamma = 0.5;
    if (gamma > 5.0) gamma = 5.0;
    return gamma;
}

/* ==========================================================================
 * L3: Spectral Power Distribution Operations
 * ========================================================================== */

static const cie_cmf_1931_t cmf_1931_data = {
    { /* x̄ */
     0.001368, 0.002236, 0.004243, 0.007650, 0.014310, 0.023190, 0.043510,
     0.077630, 0.134380, 0.214770, 0.283900, 0.328500, 0.348280, 0.348060,
     0.336200, 0.318700, 0.290800, 0.251100, 0.195360, 0.142100, 0.095640,
     0.058010, 0.032010, 0.014700, 0.004900, 0.002400, 0.009300, 0.029100,
     0.063270, 0.109600, 0.165500, 0.225750, 0.290400, 0.359700, 0.433450,
     0.512050, 0.594500, 0.678400, 0.762100, 0.842500, 0.916300, 0.978600,
     1.026300, 1.056700, 1.062200, 1.045600, 1.002600, 0.938400, 0.854450,
     0.751400, 0.642400, 0.541900, 0.447900, 0.360800, 0.283500, 0.218700,
     0.164900, 0.121200, 0.087400, 0.063600, 0.046770, 0.032900, 0.022700,
     0.015840, 0.011360, 0.008111, 0.005790, 0.004109, 0.002899, 0.002049,
     0.001440, 0.001000, 0.000690, 0.000476, 0.000332, 0.000235, 0.000166,
     0.000117, 0.000083, 0.000059, 0.000042
    },
    { /* ȳ */
     0.000039, 0.000064, 0.000120, 0.000217, 0.000396, 0.000640, 0.001210,
     0.002180, 0.004000, 0.007300, 0.011600, 0.016840, 0.023000, 0.029800,
     0.038000, 0.048000, 0.060000, 0.073900, 0.090980, 0.112600, 0.139020,
     0.169300, 0.208020, 0.258600, 0.323000, 0.407300, 0.503000, 0.608200,
     0.710000, 0.793200, 0.862000, 0.914850, 0.954000, 0.980300, 0.994950,
     1.000000, 0.995000, 0.978600, 0.952000, 0.915400, 0.870000, 0.816300,
     0.757000, 0.694900, 0.631000, 0.566800, 0.503000, 0.441200, 0.381000,
     0.321000, 0.265000, 0.217000, 0.175000, 0.138200, 0.107000, 0.081600,
     0.061000, 0.044580, 0.032000, 0.023200, 0.017000, 0.011920, 0.008210,
     0.005723, 0.004102, 0.002929, 0.002091, 0.001484, 0.001047, 0.000740,
     0.000520, 0.000361, 0.000249, 0.000172, 0.000120, 0.000085, 0.000060,
     0.000042, 0.000030, 0.000021, 0.000015
    },
    { /* z̄ */
     0.006450, 0.010550, 0.020050, 0.036210, 0.067850, 0.110200, 0.207400,
     0.371300, 0.645600, 1.039050, 1.385600, 1.622960, 1.747060, 1.782600,
     1.772110, 1.744100, 1.669200, 1.528100, 1.287640, 1.041900, 0.812950,
     0.616200, 0.465180, 0.353300, 0.272000, 0.212300, 0.158200, 0.111700,
     0.078250, 0.057250, 0.042160, 0.029840, 0.020300, 0.013400, 0.008750,
     0.005750, 0.003900, 0.002750, 0.002100, 0.001800, 0.001650, 0.001400,
     0.001100, 0.001000, 0.000800, 0.000600, 0.000340, 0.000240, 0.000190,
     0.000100, 0.000050, 0.000030, 0.000020, 0.000010, 0.000000, 0.000000,
     0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
     0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
     0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
     0.000000, 0.000000, 0.000000, 0.000000
    }
};

const cie_cmf_1931_t *cie_cmf_1931_get(void)
{
    return &cmf_1931_data;
}

cie_xyz_t spd_to_xyz(const spectral_power_t *spd)
{
    cie_xyz_t xyz = {0.0, 0.0, 0.0};
    if (!spd) return xyz;
    double k = 0.0;
    for (int i = 0; i < SPD_SAMPLES; i++) {
        k += cmf_1931_data.y_bar[i] * SPD_STEP_NM;
    }
    if (k <= 0.0) k = 1.0;
    k = 100.0 / k; /* Normalize so Y=100 for perfect diffuser */

    for (int i = 0; i < SPD_SAMPLES; i++) {
        double spd_val = spd->values[i];
        xyz.X += k * spd_val * cmf_1931_data.x_bar[i] * SPD_STEP_NM;
        xyz.Y += k * spd_val * cmf_1931_data.y_bar[i] * SPD_STEP_NM;
        xyz.Z += k * spd_val * cmf_1931_data.z_bar[i] * SPD_STEP_NM;
    }
    return xyz;
}

cie_xy_t spd_to_xy(const spectral_power_t *spd)
{
    cie_xyz_t xyz = spd_to_xyz(spd);
    return (cie_xy_t){xyz.X / (xyz.X + xyz.Y + xyz.Z),
                      xyz.Y / (xyz.X + xyz.Y + xyz.Z)};
}

double spd_to_luminance(const spectral_power_t *spd)
{
    if (!spd) return 0.0;
    double Y = 0.0;
    /* V(λ) = ȳ(λ) for the 2° standard observer */
    for (int i = 0; i < SPD_SAMPLES; i++) {
        Y += spd->values[i] * cmf_1931_data.y_bar[i] * SPD_STEP_NM;
    }
    return Y * 683.0; /* Km = 683 lm/W at 555 nm */
}


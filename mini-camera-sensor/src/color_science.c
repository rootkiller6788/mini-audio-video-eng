/**
 * @file color_science.c
 * @brief Color science: spaces, WB, CCM, CCT, chromatic adaptation
 *
 * Implements:
 *   L3: CIE XYZ↔sRGB, XYZ↔xy, XYZ↔Lab, color difference formulas
 *   L5: Gray World, White Patch, Shades of Gray WB; CCM via LS;
 *       CCT estimation (McCamy, Robertson); Bradford/Von Kries CAT
 *   L7: ColorChecker reference data, camera color calibration
 *
 * Reference: Poynton (2012); Wyszecki & Stiles (1982); McCamy (1992)
 */
#include "color_science.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* sRGB primary → XYZ matrix (IEC 61966-2-1, D65) */
static const double SRGB_TO_XYZ[9] = {
    0.4124564, 0.3575761, 0.1804375,
    0.2126729, 0.7151522, 0.0721750,
    0.0193339, 0.1191920, 0.9503041
};

/* XYZ → sRGB linear matrix */
static const double XYZ_TO_SRGB[9] = {
     3.2404542, -1.5371385, -0.4985314,
    -0.9692660,  1.8760108,  0.0415560,
     0.0556434, -0.2040259,  1.0572252
};

/* CIE Standard Illuminant XYZ (normalized Y=1) */
static const double ILLUMINANT_XYZ[][3] = {
    {1.09850, 1.00000, 0.35585},  /* A */
    {0.99072, 1.00000, 0.85223},  /* B */
    {0.98074, 1.00000, 1.18232},  /* C */
    {0.96422, 1.00000, 0.82521},  /* D50 */
    {0.95682, 1.00000, 0.92149},  /* D55 */
    {0.95047, 1.00000, 1.08883},  /* D65 */
    {0.94972, 1.00000, 1.22638},  /* D75 */
    {1.00000, 1.00000, 1.00000},  /* E */
    {0.99186, 1.00000, 0.67393},  /* F2 */
    {0.95041, 1.00000, 1.08747},  /* F7 */
    {1.00962, 1.00000, 0.64350}   /* F11 */
};

cie_xyz_t illuminant_get_xyz(illuminant_t ill)
{
    cie_xyz_t r = {0, 0, 0};
    if (ill <= ILLUMINANT_F11) {
        r.X = ILLUMINANT_XYZ[ill][0];
        r.Y = ILLUMINANT_XYZ[ill][1];
        r.Z = ILLUMINANT_XYZ[ill][2];
    }
    return r;
}

cie_xy_t illuminant_get_xy(illuminant_t ill)
{
    cie_xyz_t xyz = illuminant_get_xyz(ill);
    return xyz_to_xy(xyz);
}

/*===========================================================================
 * L3+L5: Color space conversions
 *===========================================================================*/

rgb_linear_t srgb_to_linear(rgb_linear_t srgb)
{
    rgb_linear_t r = {0, 0, 0};
    double vals[3] = {srgb.r, srgb.g, srgb.b};
    int i;
    for (i = 0; i < 3; i++) {
        if (vals[i] <= 0.04045)
            vals[i] = vals[i] / 12.92;
        else
            vals[i] = pow((vals[i] + 0.055) / 1.055, 2.4);
    }
    r.r = vals[0]; r.g = vals[1]; r.b = vals[2];
    return r;
}

rgb_linear_t linear_to_srgb(rgb_linear_t linear)
{
    rgb_linear_t r = {0, 0, 0};
    double vals[3] = {linear.r, linear.g, linear.b};
    int i;
    for (i = 0; i < 3; i++) {
        if (vals[i] <= 0.0031308)
            vals[i] = 12.92 * vals[i];
        else
            vals[i] = 1.055 * pow(vals[i], 1.0/2.4) - 0.055;
    }
    r.r = vals[0]; r.g = vals[1]; r.b = vals[2];
    return r;
}

cie_xyz_t srgb_linear_to_xyz(rgb_linear_t rgb)
{
    cie_xyz_t xyz;
    xyz.X = SRGB_TO_XYZ[0]*rgb.r + SRGB_TO_XYZ[1]*rgb.g + SRGB_TO_XYZ[2]*rgb.b;
    xyz.Y = SRGB_TO_XYZ[3]*rgb.r + SRGB_TO_XYZ[4]*rgb.g + SRGB_TO_XYZ[5]*rgb.b;
    xyz.Z = SRGB_TO_XYZ[6]*rgb.r + SRGB_TO_XYZ[7]*rgb.g + SRGB_TO_XYZ[8]*rgb.b;
    return xyz;
}

rgb_linear_t xyz_to_srgb_linear(cie_xyz_t xyz)
{
    rgb_linear_t r;
    r.r = XYZ_TO_SRGB[0]*xyz.X + XYZ_TO_SRGB[1]*xyz.Y + XYZ_TO_SRGB[2]*xyz.Z;
    r.g = XYZ_TO_SRGB[3]*xyz.X + XYZ_TO_SRGB[4]*xyz.Y + XYZ_TO_SRGB[5]*xyz.Z;
    r.b = XYZ_TO_SRGB[6]*xyz.X + XYZ_TO_SRGB[7]*xyz.Y + XYZ_TO_SRGB[8]*xyz.Z;
    if (r.r < 0) r.r = 0;
    if (r.g < 0) r.g = 0;
    if (r.b < 0) r.b = 0;
    return r;
}

cie_xy_t xyz_to_xy(cie_xyz_t xyz)
{
    cie_xy_t xy = {0, 0};
    double sum = xyz.X + xyz.Y + xyz.Z;
    if (sum > 0.0) {
        xy.x = xyz.X / sum;
        xy.y = xyz.Y / sum;
    }
    return xy;
}

cie_xyz_t xy_to_xyz(cie_xy_t xy, double Y)
{
    cie_xyz_t xyz = {0, 0, 0};
    if (xy.y > 0.0) {
        xyz.X = xy.x * Y / xy.y;
        xyz.Y = Y;
        xyz.Z = (1.0 - xy.x - xy.y) * Y / xy.y;
    }
    return xyz;
}

/* CIELAB conversion with reference white */
static double lab_f(double t)
{
    const double delta = 6.0/29.0;
    if (t > delta*delta*delta)
        return cbrt(t);
    else
        return t / (3.0*delta*delta) + 4.0/29.0;
}

static double lab_f_inv(double t)
{
    const double delta = 6.0/29.0;
    if (t > delta)
        return t*t*t;
    else
        return 3.0*delta*delta*(t - 4.0/29.0);
}

cie_lab_t xyz_to_lab(cie_xyz_t xyz, cie_xyz_t white)
{
    cie_lab_t lab = {0, 0, 0};
    double Xr = xyz.X / (white.X > 0 ? white.X : 1.0);
    double Yr = xyz.Y / (white.Y > 0 ? white.Y : 1.0);
    double Zr = xyz.Z / (white.Z > 0 ? white.Z : 1.0);

    double fX = lab_f(Xr), fY = lab_f(Yr), fZ = lab_f(Zr);
    lab.L = 116.0 * fY - 16.0;
    lab.a = 500.0 * (fX - fY);
    lab.b = 200.0 * (fY - fZ);
    return lab;
}

cie_xyz_t lab_to_xyz(cie_lab_t lab, cie_xyz_t white)
{
    double fY = (lab.L + 16.0) / 116.0;
    double fX = lab.a / 500.0 + fY;
    double fZ = fY - lab.b / 200.0;

    cie_xyz_t xyz;
    xyz.X = white.X * lab_f_inv(fX);
    xyz.Y = white.Y * lab_f_inv(fY);
    xyz.Z = white.Z * lab_f_inv(fZ);
    return xyz;
}

/*===========================================================================
 * L5: CCT estimation
 *===========================================================================*/

/**
 * McCamy cubic approximation for CCT from CIE xy.
 *
 * n = (x - 0.3320) / (y - 0.1858)
 * CCT = -449*n^3 + 3525*n^2 - 6823.3*n + 5520.33
 *
 * Valid for 2000-10000 K. Accuracy ±2 K in this range.
 * Reference: McCamy, Color Res. Appl. 17:142-144 (1992)
 */
double color_xy_to_cct_mccamy(cie_xy_t xy)
{
    double n = (xy.x - 0.3320) / (xy.y - 0.1858);
    double n2 = n * n;
    double n3 = n2 * n;
    return -449.0*n3 + 3525.0*n2 - 6823.3*n + 5520.33;
}

/**
 * CCT from XYZ via xy chromaticity using McCamy.
 */
double color_xyz_to_cct(cie_xyz_t xyz)
{
    cie_xy_t xy = xyz_to_xy(xyz);
    return color_xy_to_cct_mccamy(xy);
}

/**
 * Planckian locus CIE xy at temperature T.
 *
 * Uses approximation from Kim et al. (2002):
 * x_c = function of T in 10^3/T.
 */
cie_xy_t color_planckian_xy(double temp_k)
{
    cie_xy_t xy = {0, 0};
    if (temp_k < 1000.0) return xy;

    double invT = 1000.0 / temp_k;  /* 10^3 / T */
    double invT2 = invT * invT;
    double invT3 = invT2 * invT;

    /* CIE 1931 Planckian locus approximation */
    if (temp_k <= 4000.0) {
        xy.x = 0.179910*invT3 - 0.2661239*invT2 + 0.8776956*invT + 0.179910;
    } else if (temp_k <= 25000.0) {
        xy.x = -0.0238499*invT3 - 0.3060478*invT2 + 1.0832282*invT + 0.162578;
    } else {
        xy.x = -0.0034405*invT3 - 0.3547578*invT2 + 1.1945030*invT + 0.153799;
    }

    /* y from quadratic: y = -3.0*x^2 + 2.87*x - 0.275 */
    xy.y = -3.0*xy.x*xy.x + 2.87*xy.x - 0.275;

    return xy;
}

/*===========================================================================
 * L5: White balance algorithms
 *===========================================================================*/

/**
 * Gray World: assumes average scene reflectance is gray.
 * gains = G_avg / channel_avg
 */
void color_gray_world(const rgb_linear_t *samples, uint32_t n, wb_gains_t *g)
{
    if (samples == NULL || g == NULL || n == 0) return;

    double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        sum_r += samples[i].r;
        sum_g += samples[i].g;
        sum_b += samples[i].b;
    }

    double avg_r = sum_r / n, avg_g = sum_g / n, avg_b = sum_b / n;
    if (avg_g <= 0.0) { g->r_gain = g->g_gain = g->b_gain = 1.0; return; }
    g->r_gain = (avg_r > 0.0) ? avg_g / avg_r : 1.0;
    g->g_gain = 1.0;
    g->b_gain = (avg_b > 0.0) ? avg_g / avg_b : 1.0;
}

/**
 * White Patch (Retinex): brightest pixel → white.
 */
void color_white_patch(const rgb_linear_t *samples, uint32_t n, wb_gains_t *g)
{
    if (samples == NULL || g == NULL || n == 0) return;

    double max_r = 0.0, max_g = 0.0, max_b = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (samples[i].r > max_r) max_r = samples[i].r;
        if (samples[i].g > max_g) max_g = samples[i].g;
        if (samples[i].b > max_b) max_b = samples[i].b;
    }

    double white = (max_r + max_g + max_b) / 3.0;
    if (white <= 0.0) { g->r_gain = g->g_gain = g->b_gain = 1.0; return; }
    g->r_gain = (max_r > 0.0) ? white / max_r : 1.0;
    g->g_gain = (max_g > 0.0) ? white / max_g : 1.0;
    g->b_gain = (max_b > 0.0) ? white / max_b : 1.0;
}

/**
 * Shades of Gray (Finlayson & Trezzi, CIC 2004).
 *
 * Generalizes Gray World (p=1) and White Patch (p→∞).
 * Uses p-norm: avg = (Σ I^p / n)^(1/p)
 */
void color_shades_of_gray(const rgb_linear_t *samples, uint32_t n,
                           double p, wb_gains_t *g)
{
    if (samples == NULL || g == NULL || n == 0) return;
    if (p < 1.0) p = 1.0;

    double sum_rp = 0.0, sum_gp = 0.0, sum_bp = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        sum_rp += pow(samples[i].r, p);
        sum_gp += pow(samples[i].g, p);
        sum_bp += pow(samples[i].b, p);
    }

    double avg_r = pow(sum_rp / n, 1.0/p);
    double avg_g = pow(sum_gp / n, 1.0/p);
    double avg_b = pow(sum_bp / n, 1.0/p);

    if (avg_g <= 0.0) { g->r_gain = g->g_gain = g->b_gain = 1.0; return; }
    g->r_gain = (avg_r > 0.0) ? avg_g / avg_r : 1.0;
    g->g_gain = 1.0;
    g->b_gain = (avg_b > 0.0) ? avg_g / avg_b : 1.0;
}

rgb_linear_t color_apply_wb(rgb_linear_t rgb, const wb_gains_t *g)
{
    rgb_linear_t r;
    r.r = rgb.r * g->r_gain;
    r.g = rgb.g * g->g_gain;
    r.b = rgb.b * g->b_gain;
    return r;
}

/*===========================================================================
 * L5+L7: CCM calibration
 *===========================================================================*/

/**
 * CCM calibration via linear least squares.
 *
 * Minimize: || S * CCM^T - T ||_F^2
 * Solution via normal equations: CCM^T = (S^T S)^(-1) S^T T
 */
int color_ccm_calibrate(const double *sensor_rgb, const double *target_rgb,
                         uint32_t n, double ccm[9])
{
    if (n < 3 || sensor_rgb == NULL || target_rgb == NULL || ccm == NULL)
        return -1;

    double A[9] = {0};  /* S^T S */
    double B[9] = {0};  /* S^T T */
    uint32_t i;

    for (i = 0; i < n; i++) {
        double sr = sensor_rgb[i*3], sg = sensor_rgb[i*3+1], sb = sensor_rgb[i*3+2];
        double tr = target_rgb[i*3], tg = target_rgb[i*3+1], tb = target_rgb[i*3+2];

        A[0] += sr*sr; A[1] += sr*sg; A[2] += sr*sb;
        A[3] += sg*sr; A[4] += sg*sg; A[5] += sg*sb;
        A[6] += sb*sr; A[7] += sb*sg; A[8] += sb*sb;

        B[0] += sr*tr; B[1] += sg*tr; B[2] += sb*tr;
        B[3] += sr*tg; B[4] += sg*tg; B[5] += sb*tg;
        B[6] += sr*tb; B[7] += sg*tb; B[8] += sb*tb;
    }

    /* Solve A * CCM^T = B via Cramer's rule */
    double det = A[0]*(A[4]*A[8]-A[5]*A[7]) -
                 A[1]*(A[3]*A[8]-A[5]*A[6]) +
                 A[2]*(A[3]*A[7]-A[4]*A[6]);
    if (fabs(det) < 1e-20) return -1;

    double inv = 1.0 / det;

    /* Column 0 of CCM^T (row 0 of CCM) */
    ccm[0] = (B[0]*(A[4]*A[8]-A[5]*A[7])-A[1]*(B[3]*A[8]-A[5]*B[6])+A[2]*(B[3]*A[7]-A[4]*B[6]))*inv;
    ccm[3] = (A[0]*(B[3]*A[8]-A[5]*B[6])-B[0]*(A[3]*A[8]-A[5]*A[6])+A[2]*(A[3]*B[6]-B[3]*A[6]))*inv;
    ccm[6] = (A[0]*(A[4]*B[6]-B[3]*A[7])-A[1]*(A[3]*B[6]-B[3]*A[6])+B[0]*(A[3]*A[7]-A[4]*A[6]))*inv;

    /* Column 1 */
    ccm[1] = (B[1]*(A[4]*A[8]-A[5]*A[7])-A[1]*(B[4]*A[8]-A[5]*B[7])+A[2]*(B[4]*A[7]-A[4]*B[7]))*inv;
    ccm[4] = (A[0]*(B[4]*A[8]-A[5]*B[7])-B[1]*(A[3]*A[8]-A[5]*A[6])+A[2]*(A[3]*B[7]-B[4]*A[6]))*inv;
    ccm[7] = (A[0]*(A[4]*B[7]-B[4]*A[7])-A[1]*(A[3]*B[7]-B[4]*A[6])+B[1]*(A[3]*A[7]-A[4]*A[6]))*inv;

    /* Column 2 */
    ccm[2] = (B[2]*(A[4]*A[8]-A[5]*A[7])-A[1]*(B[5]*A[8]-A[5]*B[8])+A[2]*(B[5]*A[7]-A[4]*B[8]))*inv;
    ccm[5] = (A[0]*(B[5]*A[8]-A[5]*B[8])-B[2]*(A[3]*A[8]-A[5]*A[6])+A[2]*(A[3]*B[8]-B[5]*A[6]))*inv;
    ccm[8] = (A[0]*(A[4]*B[8]-B[5]*A[7])-A[1]*(A[3]*B[8]-B[5]*A[6])+B[2]*(A[3]*A[7]-A[4]*A[6]))*inv;

    return 0;
}

rgb_linear_t color_apply_ccm(rgb_linear_t rgb, const double ccm[9])
{
    rgb_linear_t r;
    r.r = ccm[0]*rgb.r + ccm[1]*rgb.g + ccm[2]*rgb.b;
    r.g = ccm[3]*rgb.r + ccm[4]*rgb.g + ccm[5]*rgb.b;
    r.b = ccm[6]*rgb.r + ccm[7]*rgb.g + ccm[8]*rgb.b;
    if (r.r < 0) r.r = 0;
    if (r.g < 0) r.g = 0;
    if (r.b < 0) r.b = 0;
    return r;
}

/*===========================================================================
 * L5: Chromatic adaptation transforms
 *===========================================================================*/

/**
 * Bradford chromatic adaptation (Lam 1985).
 *
 * Converts colors from source illuminant to destination illuminant.
 * Uses LMS cone response domain for diagonal scaling.
 */
static const double BRADFORD_M[9] = {
     0.8951,  0.2664, -0.1614,
    -0.7502,  1.7135,  0.0367,
     0.0389, -0.0685,  1.0296
};
static const double BRADFORD_MI[9] = {
     0.9869929, -0.1470543,  0.1599627,
     0.4323053,  0.5183603,  0.0492912,
    -0.0085287,  0.0400428,  0.9684867
};

rgb_linear_t color_bradford_cat(rgb_linear_t rgb, illuminant_t src,
                                 illuminant_t dst)
{
    cie_xyz_t src_w = illuminant_get_xyz(src);
    cie_xyz_t dst_w = illuminant_get_xyz(dst);

    /* Convert white points to LMS */
    double src_lms[3], dst_lms[3];
    src_lms[0] = BRADFORD_M[0]*src_w.X + BRADFORD_M[1]*src_w.Y + BRADFORD_M[2]*src_w.Z;
    src_lms[1] = BRADFORD_M[3]*src_w.X + BRADFORD_M[4]*src_w.Y + BRADFORD_M[5]*src_w.Z;
    src_lms[2] = BRADFORD_M[6]*src_w.X + BRADFORD_M[7]*src_w.Y + BRADFORD_M[8]*src_w.Z;

    dst_lms[0] = BRADFORD_M[0]*dst_w.X + BRADFORD_M[1]*dst_w.Y + BRADFORD_M[2]*dst_w.Z;
    dst_lms[1] = BRADFORD_M[3]*dst_w.X + BRADFORD_M[4]*dst_w.Y + BRADFORD_M[5]*dst_w.Z;
    dst_lms[2] = BRADFORD_M[6]*dst_w.X + BRADFORD_M[7]*dst_w.Y + BRADFORD_M[8]*dst_w.Z;

    /* Convert RGB to XYZ */
    cie_xyz_t xyz = srgb_linear_to_xyz(rgb);
    double pix_lms[3];
    pix_lms[0] = BRADFORD_M[0]*xyz.X + BRADFORD_M[1]*xyz.Y + BRADFORD_M[2]*xyz.Z;
    pix_lms[1] = BRADFORD_M[3]*xyz.X + BRADFORD_M[4]*xyz.Y + BRADFORD_M[5]*xyz.Z;
    pix_lms[2] = BRADFORD_M[6]*xyz.X + BRADFORD_M[7]*xyz.Y + BRADFORD_M[8]*xyz.Z;

    /* Diagonal transform */
    if (src_lms[0] > 0 && src_lms[1] > 0 && src_lms[2] > 0) {
        pix_lms[0] *= dst_lms[0] / src_lms[0];
        pix_lms[1] *= dst_lms[1] / src_lms[1];
        pix_lms[2] *= dst_lms[2] / src_lms[2];
    }

    /* LMS → XYZ */
    xyz.X = BRADFORD_MI[0]*pix_lms[0] + BRADFORD_MI[1]*pix_lms[1] + BRADFORD_MI[2]*pix_lms[2];
    xyz.Y = BRADFORD_MI[3]*pix_lms[0] + BRADFORD_MI[4]*pix_lms[1] + BRADFORD_MI[5]*pix_lms[2];
    xyz.Z = BRADFORD_MI[6]*pix_lms[0] + BRADFORD_MI[7]*pix_lms[1] + BRADFORD_MI[8]*pix_lms[2];

    return xyz_to_srgb_linear(xyz);
}

rgb_linear_t color_von_kries_cat(rgb_linear_t rgb, const wb_gains_t *src_lms,
                                  const wb_gains_t *dst_lms)
{
    /* Convert RGB to LMS using Hunt-Pointer-Estevez matrix */
    double lms_r =  0.38971*rgb.r + 0.68898*rgb.g - 0.07868*rgb.b;
    double lms_g = -0.22981*rgb.r + 1.18340*rgb.g + 0.04641*rgb.b;
    double lms_b =  0.0*rgb.r      + 0.0*rgb.g      + 1.0*rgb.b;

    /* Diagonal scaling */
    double r_gain = (src_lms->r_gain > 0) ? dst_lms->r_gain / src_lms->r_gain : 1.0;
    double g_gain = (src_lms->g_gain > 0) ? dst_lms->g_gain / src_lms->g_gain : 1.0;
    double b_gain = (src_lms->b_gain > 0) ? dst_lms->b_gain / src_lms->b_gain : 1.0;

    lms_r *= r_gain;
    lms_g *= g_gain;
    lms_b *= b_gain;

    /* LMS → RGB */
    rgb_linear_t result;
    result.r =  1.91019*lms_r - 1.11214*lms_g + 0.20195*lms_b;
    result.g =  0.37095*lms_r + 0.62905*lms_g - 0.0*lms_b;
    result.b =  0.0*lms_r     + 0.0*lms_g     + 1.0*lms_b;

    return result;
}

/*===========================================================================
 * L3: Color difference metrics
 *===========================================================================*/

double color_delta_e_1976(cie_lab_t lab1, cie_lab_t lab2)
{
    double dL = lab1.L - lab2.L;
    double da = lab1.a - lab2.a;
    double db = lab1.b - lab2.b;
    return sqrt(dL*dL + da*da + db*db);
}

/**
 * CIEDE2000 — perceptually uniform color difference.
 * Reference: Luo, Cui, Rigg, CIE Pub. 142 (2001)
 */
double color_delta_e_2000(cie_lab_t lab1, cie_lab_t lab2)
{
    double L1 = lab1.L, a1 = lab1.a, b1 = lab1.b;
    double L2 = lab2.L, a2 = lab2.a, b2 = lab2.b;

    double C1 = sqrt(a1*a1 + b1*b1);
    double C2 = sqrt(a2*a2 + b2*b2);
    double Cbar = (C1 + C2) / 2.0;
    double Cbar7 = Cbar*Cbar*Cbar*Cbar*Cbar*Cbar*Cbar;
    double G = 0.5*(1.0 - sqrt(Cbar7/(Cbar7 + 6103515625.0))); /* 25^7 = 6103515625 */

    double a1p = a1*(1.0 + G);
    double a2p = a2*(1.0 + G);

    double C1p = sqrt(a1p*a1p + b1*b1);
    double C2p = sqrt(a2p*a2p + b2*b2);

    double h1p = atan2(b1, a1p) * 180.0 / M_PI;
    if (h1p < 0) h1p += 360.0;
    double h2p = atan2(b2, a2p) * 180.0 / M_PI;
    if (h2p < 0) h2p += 360.0;

    double dLp = L2 - L1;
    double dCp = C2p - C1p;
    double dhp;
    if (C1p*C2p == 0) {
        dhp = 0;
    } else {
        double dh = h2p - h1p;
        if (dh > 180) dh -= 360;
        if (dh < -180) dh += 360;
        dhp = 2.0*sqrt(C1p*C2p)*sin(dh*M_PI/360.0);
    }

    double Lbar = (L1 + L2)/2.0;
    double Cpbar = (C1p + C2p)/2.0;
    double hpbar;
    if (C1p*C2p == 0) {
        hpbar = h1p + h2p;
    } else {
        double dh = fabs(h2p - h1p);
        if (dh <= 180) hpbar = (h1p + h2p)/2.0;
        else { hpbar = (h1p + h2p + 360)/2.0; if (hpbar > 360) hpbar -= 360; }
    }

    double T = 1.0 - 0.17*cos((hpbar-30)*M_PI/180) + 0.24*cos((2*hpbar)*M_PI/180) +
               0.32*cos((3*hpbar+6)*M_PI/180) - 0.20*cos((4*hpbar-63)*M_PI/180);
    double dTheta = 30.0*exp(-pow((hpbar-275)/25.0, 2));
    double RC = 2.0*sqrt(Cpbar*Cpbar*Cpbar*Cpbar*Cpbar*Cpbar*Cpbar /
                         (Cpbar*Cpbar*Cpbar*Cpbar*Cpbar*Cpbar*Cpbar + 6103515625.0));
    double SL = 1.0 + 0.015*pow(Lbar-50, 2)/sqrt(20+pow(Lbar-50,2));
    double SC = 1.0 + 0.045*Cpbar;
    double SH = 1.0 + 0.015*Cpbar*T;
    double RT = -sin(2*dTheta*M_PI/180)*RC;

    double dE = sqrt(pow(dLp/SL, 2) + pow(dCp/SC, 2) + pow(dhp/SH, 2) +
                     RT*(dCp/SC)*(dhp/SH));
    return dE;
}

double color_delta_e_cmc(cie_lab_t ref, cie_lab_t sample, double l, double c)
{
    double dL = ref.L - sample.L;
    double da = ref.a - sample.a;
    double db = ref.b - sample.b;

    double C1 = sqrt(ref.a*ref.a + ref.b*ref.b);
    double C2 = sqrt(sample.a*sample.a + sample.b*sample.b);
    double dC = C1 - C2;
    double dH = sqrt(da*da + db*db - dC*dC);
    if (dH != dH) dH = 0; /* NaN guard */

    double SL = (ref.L < 16) ? 0.511 : 0.040975*ref.L/(1+0.01765*ref.L);
    double SC = 0.0638*C1/(1+0.0131*C1) + 0.638;
    double F = sqrt(C1*C1*C1*C1/(C1*C1*C1*C1+1900.0));
    double h = atan2(ref.b, ref.a) * 180.0 / M_PI;
    if (h < 0) h += 360;
    double T = (h >= 164 && h <= 345) ?
               0.56 + fabs(0.2*cos((h+168)*M_PI/180)) :
               0.36 + fabs(0.4*cos((h+35)*M_PI/180));
    double SH = SC*(F*T + 1 - F);

    return sqrt(pow(dL/(l*SL), 2) + pow(dC/(c*SC), 2) + pow(dH/SH, 2));
}

/*===========================================================================
 * L7: ColorChecker reference data
 *===========================================================================*/

/* ColorChecker classic sRGB values (gamma-encoded, 0-255) */
static const uint8_t CC_SRGB[24][3] = {
    {115, 82, 68},    {194,150,130},  {98,122,157},   {87,108,67},
    {133,128,177},    {103,189,170},  {214,126,44},   {80,91,166},
    {193,90,99},      {94,60,108},    {157,188,64},   {224,163,46},
    {56,61,150},      {70,148,73},    {175,54,60},    {231,199,31},
    {187,86,149},     {8,133,161},    {243,243,242},  {200,200,200},
    {160,160,160},    {122,122,121},  {85,85,85},     {52,52,52}
};

void colorchecker_get_srgb(colorchecker_patch_t p, uint8_t *r, uint8_t *g,
                            uint8_t *b)
{
    if (r == NULL || g == NULL || b == NULL) return;
    if (p >= CC_COUNT) { *r = *g = *b = 0; return; }
    *r = CC_SRGB[p][0];
    *g = CC_SRGB[p][1];
    *b = CC_SRGB[p][2];
}

cie_xyz_t colorchecker_get_xyz(colorchecker_patch_t p)
{
    cie_xyz_t xyz = {0, 0, 0};
    if (p >= CC_COUNT) return xyz;
    /* Convert sRGB → XYZ */
    rgb_linear_t linear;
    linear.r = (double)CC_SRGB[p][0] / 255.0;
    linear.g = (double)CC_SRGB[p][1] / 255.0;
    linear.b = (double)CC_SRGB[p][2] / 255.0;
    linear = srgb_to_linear(linear);
    return srgb_linear_to_xyz(linear);
}

cie_lab_t colorchecker_get_lab(colorchecker_patch_t p)
{
    cie_xyz_t xyz = colorchecker_get_xyz(p);
    cie_xyz_t d65 = illuminant_get_xyz(ILLUMINANT_D65);
    return xyz_to_lab(xyz, d65);
}

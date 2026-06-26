#include "hdr_color.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ?? D65 White Point (CIE 1931 XYZ) ??????????????????????????????????? */
static const cie_xyz_t D65_XYZ = { 0.95047, 1.0, 1.08883 };

/* ?? 3x3 Matrix Operations ???????????????????????????????????????????? */

void color_matrix_multiply(const double a[3][3], const double b[3][3], double result[3][3])
{
    if (!a || !b || !result) return;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            result[i][j] = 0.0;
            for (int k = 0; k < 3; k++)
                result[i][j] += a[i][k] * b[k][j];
        }
}

void color_matrix_apply_xyz(const double m[3][3], const cie_xyz_t *in, cie_xyz_t *out)
{
    if (!in || !out) return;
    out->X = m[0][0] * in->X + m[0][1] * in->Y + m[0][2] * in->Z;
    out->Y = m[1][0] * in->X + m[1][1] * in->Y + m[1][2] * in->Z;
    out->Z = m[2][0] * in->X + m[2][1] * in->Y + m[2][2] * in->Z;
}

void color_matrix_apply_rgb(const double m[3][3], const hdr_rgb_pixel_t *in, hdr_rgb_pixel_t *out)
{
    if (!in || !out) return;
    out->r = m[0][0] * in->r + m[0][1] * in->g + m[0][2] * in->b;
    out->g = m[1][0] * in->r + m[1][1] * in->g + m[1][2] * in->b;
    out->b = m[2][0] * in->r + m[2][1] * in->g + m[2][2] * in->b;
}

double color_matrix_determinant(const double m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

int color_matrix_inverse(const double m[3][3], double inv[3][3])
{
    double det = color_matrix_determinant(m);
    if (fabs(det) < 1e-16) return -1;

    double det_inv = 1.0 / det;
    inv[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * det_inv;
    inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * det_inv;
    inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * det_inv;
    inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * det_inv;
    inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * det_inv;
    inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * det_inv;
    inv[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * det_inv;
    inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * det_inv;
    inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * det_inv;
    return 0;
}

void color_matrix_transpose(const double m[3][3], double t[3][3])
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            t[j][i] = m[i][j];
}

/* ?? RGB to XYZ Matrix Construction ??????????????????????????????????? */

void color_rgb_to_xyz_matrix(const hdr_primaries_set_t *primaries, double m[3][3])
{
    if (!primaries) return;
    memset(m, 0, 9 * sizeof(double));

    /* Compute XYZ coordinates of each primary using xyY -> XYZ */
    double Xr, Yr, Zr, Xg, Yg, Zg, Xb, Yb, Zb;
    hdr_chromaticity_to_xyz(primaries->red,   1.0, &Xr, &Zr); Yr = 1.0;
    hdr_chromaticity_to_xyz(primaries->green, 1.0, &Xg, &Zg); Yg = 1.0;
    hdr_chromaticity_to_xyz(primaries->blue,  1.0, &Xb, &Zb); Yb = 1.0;

    /* Solve for scaling factors Sr, Sg, Sb such that
     * [Xr Xg Xb] [Sr]   [Xw]
     * [Yr Yg Yb] [Sg] = [Yw]
     * [Zr Zg Zb] [Sb]   [Zw] */

    double Xw, Yw = 1.0, Zw;
    hdr_chromaticity_to_xyz(primaries->white, 1.0, &Xw, &Zw);

    double det = Xr * (Yg * Zb - Zg * Yb)
               - Xg * (Yr * Zb - Zr * Yb)
               + Xb * (Yr * Zg - Zr * Yg);

    if (fabs(det) < 1e-16) return;

    double det_inv = 1.0 / det;
    double Sr = (Xw * (Yg * Zb - Zg * Yb) - Xg * (Yw * Zb - Zw * Yb) + Xb * (Yw * Zg - Zw * Yg)) * det_inv;
    double Sg = (Xr * (Yw * Zb - Zw * Yb) - Xw * (Yr * Zb - Zr * Yb) + Xb * (Yr * Zw - Zr * Yw)) * det_inv;
    double Sb = (Xr * (Yg * Zw - Zg * Yw) - Xg * (Yr * Zw - Zr * Yw) + Xw * (Yr * Zg - Zr * Yg)) * det_inv;

    m[0][0] = Sr * Xr; m[0][1] = Sg * Xg; m[0][2] = Sb * Xb;
    m[1][0] = Sr * Yr; m[1][1] = Sg * Yg; m[1][2] = Sb * Yb;
    m[2][0] = Sr * Zr; m[2][1] = Sg * Zg; m[2][2] = Sb * Zb;
}

/* ?? BT.709/sRGB <-> CIE XYZ ?????????????????????????????????????????? */

static double srgb_linearize(double c)
{
    if (c <= 0.04045)
        return c / 12.92;
    else
        return pow((c + 0.055) / 1.055, 2.4);
}

static double srgb_gamma_encode(double c)
{
    if (c <= 0.0031308)
        return 12.92 * c;
    else
        return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

void color_srgb_to_xyz(const hdr_rgb_pixel_t *rgb, cie_xyz_t *xyz)
{
    if (!rgb || !xyz) return;

    /* Linearize sRGB */
    double r_lin = srgb_linearize(rgb->r);
    double g_lin = srgb_linearize(rgb->g);
    double b_lin = srgb_linearize(rgb->b);

    /* BT.709/sRGB to XYZ matrix (D65):
     * R 0.4124564  0.3575761  0.1804375
     * G 0.2126729  0.7151522  0.0721750
     * B 0.0193339  0.1191920  0.9503041 */
    xyz->X = 0.4124564 * r_lin + 0.3575761 * g_lin + 0.1804375 * b_lin;
    xyz->Y = 0.2126729 * r_lin + 0.7151522 * g_lin + 0.0721750 * b_lin;
    xyz->Z = 0.0193339 * r_lin + 0.1191920 * g_lin + 0.9503041 * b_lin;
}

void color_xyz_to_srgb(const cie_xyz_t *xyz, hdr_rgb_pixel_t *rgb)
{
    if (!xyz || !rgb) return;

    /* XYZ to linear sRGB matrix (inverse of above):
     * R  3.2404542 -1.5371385 -0.4985314
     * G -0.9692660  1.8760108  0.0415560
     * B  0.0556434 -0.2040259  1.0572252 */
    double r_lin =  3.2404542 * xyz->X - 1.5371385 * xyz->Y - 0.4985314 * xyz->Z;
    double g_lin = -0.9692660 * xyz->X + 1.8760108 * xyz->Y + 0.0415560 * xyz->Z;
    double b_lin =  0.0556434 * xyz->X - 0.2040259 * xyz->Y + 1.0572252 * xyz->Z;

    rgb->r = srgb_gamma_encode(r_lin);
    rgb->g = srgb_gamma_encode(g_lin);
    rgb->b = srgb_gamma_encode(b_lin);
}

/* ?? sRGB <-> CIE L*a*b* ?????????????????????????????????????????????? */

static double lab_f(double t)
{
    double delta = 6.0 / 29.0;
    if (t > delta * delta * delta)
        return pow(t, 1.0 / 3.0);
    else
        return t / (3.0 * delta * delta) + 4.0 / 29.0;
}

static double lab_f_inv(double t)
{
    double delta = 6.0 / 29.0;
    if (t > delta)
        return t * t * t;
    else
        return 3.0 * delta * delta * (t - 4.0 / 29.0);
}

void color_srgb_to_lab(const hdr_rgb_pixel_t *rgb, cie_lab_t *lab)
{
    if (!rgb || !lab) return;
    cie_xyz_t xyz;
    color_srgb_to_xyz(rgb, &xyz);

    double fx = lab_f(xyz.X / D65_XYZ.X);
    double fy = lab_f(xyz.Y / D65_XYZ.Y);
    double fz = lab_f(xyz.Z / D65_XYZ.Z);

    lab->L = 116.0 * fy - 16.0;
    lab->a = 500.0 * (fx - fy);
    lab->b = 200.0 * (fy - fz);
}

void color_lab_to_srgb(const cie_lab_t *lab, hdr_rgb_pixel_t *rgb)
{
    if (!lab || !rgb) return;

    double fy = (lab->L + 16.0) / 116.0;
    double fx = lab->a / 500.0 + fy;
    double fz = fy - lab->b / 200.0;

    cie_xyz_t xyz;
    xyz.X = lab_f_inv(fx) * D65_XYZ.X;
    xyz.Y = lab_f_inv(fy) * D65_XYZ.Y;
    xyz.Z = lab_f_inv(fz) * D65_XYZ.Z;

    color_xyz_to_srgb(&xyz, rgb);
}

/* ?? BT.2020 RGB <-> ICtCp (ITU-R BT.2100) ??????????????????????????? */

void color_bt2020_rgb_to_ictcp(const hdr_rgb_pixel_t *rgb, ictcp_t *ict)
{
    if (!rgb || !ict) return;

    /* Step 1: RGB to LMS (BT.2020 cone response matrix) */
    double L =  0.412109375 * rgb->r + 0.52392578125 * rgb->g + 0.06396484375 * rgb->b;
    double M =  0.166748046875 * rgb->r + 0.720458984375 * rgb->g + 0.11279296875 * rgb->b;
    double S =  0.024169921875 * rgb->r + 0.075439453125 * rgb->g + 0.900390625 * rgb->b;

    /* Step 2: LMS' = PQ_EOTF^-1(LMS) ? apply PQ inverse to each */
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    double Lp = hdr_pq_oetf(L * 10000.0, &pq);
    double Mp = hdr_pq_oetf(M * 10000.0, &pq);
    double Sp = hdr_pq_oetf(S * 10000.0, &pq);

    /* Step 3: ICTCP matrix */
    ict->I  =  0.5 * Lp + 0.5 * Mp;
    ict->Ct =  1.61376953125 * Lp - 3.323486328125 * Mp + 1.709716796875 * Sp;
    ict->Cp =  4.378173828125 * Lp - 4.24560546875 * Mp + -0.132568359375 * Sp;
}

void color_ictcp_to_bt2020_rgb(const ictcp_t *ict, hdr_rgb_pixel_t *rgb)
{
    if (!ict || !rgb) return;

    /* Inverse ICtCp matrix */
    double Lp = ict->I + 0.008609037037 * ict->Ct + 0.111029625003 * ict->Cp;
    double Mp = ict->I - 0.008609037037 * ict->Ct - 0.111029625003 * ict->Cp;
    double Sp = ict->I + 0.560031335710 * ict->Ct - 0.320627174987 * ict->Cp;

    /* Inverse PQ: LMS = PQ_EOTF(LMS') */
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    double L = hdr_pq_eotf(Lp, &pq) / 10000.0;
    double M = hdr_pq_eotf(Mp, &pq) / 10000.0;
    double S = hdr_pq_eotf(Sp, &pq) / 10000.0;

    /* LMS to RGB (inverse BT.2020 cone response) */
    rgb->r =  3.241003232 * L - 2.148035921 * M + 0.320092640 * S;
    rgb->g = -0.969266000 * L + 1.876010800 * M - 0.041556000 * S;
    rgb->b =  0.055643400 * L - 0.204025900 * M + 1.057225200 * S;
}

/* ?? Generic RGB <-> XYZ ?????????????????????????????????????????????? */

void color_rgb_to_xyz_generic(const hdr_rgb_pixel_t *rgb, const hdr_primaries_set_t *primaries, cie_xyz_t *xyz)
{
    if (!rgb || !primaries || !xyz) return;
    double m[3][3];
    color_rgb_to_xyz_matrix(primaries, m);
    xyz->X = m[0][0] * rgb->r + m[0][1] * rgb->g + m[0][2] * rgb->b;
    xyz->Y = m[1][0] * rgb->r + m[1][1] * rgb->g + m[1][2] * rgb->b;
    xyz->Z = m[2][0] * rgb->r + m[2][1] * rgb->g + m[2][2] * rgb->b;
}

int color_xyz_to_rgb_generic(const cie_xyz_t *xyz, const hdr_primaries_set_t *primaries, hdr_rgb_pixel_t *rgb)
{
    if (!xyz || !primaries || !rgb) return -1;
    double m[3][3], inv[3][3];
    color_rgb_to_xyz_matrix(primaries, m);
    if (color_matrix_inverse(m, inv) != 0) return -1;

    double r = inv[0][0] * xyz->X + inv[0][1] * xyz->Y + inv[0][2] * xyz->Z;
    double g = inv[1][0] * xyz->X + inv[1][1] * xyz->Y + inv[1][2] * xyz->Z;
    double b = inv[2][0] * xyz->X + inv[2][1] * xyz->Y + inv[2][2] * xyz->Z;

    /* Check if in-gamut (all channels >= small negative tolerance) */
    int in_gamut = (r >= -0.001 && g >= -0.001 && b >= -0.001
                 && r <= 1.001 && g <= 1.001 && b <= 1.001);

    rgb->r = (r < 0.0) ? 0.0 : ((r > 1.0) ? 1.0 : r);
    rgb->g = (g < 0.0) ? 0.0 : ((g > 1.0) ? 1.0 : g);
    rgb->b = (b < 0.0) ? 0.0 : ((b > 1.0) ? 1.0 : b);

    return in_gamut ? 0 : -1;
}

double color_delta_e_2000(const cie_lab_t *lab1, const cie_lab_t *lab2)
{
    if (!lab1 || !lab2) return 0.0;
    double L1 = lab1->L, a1 = lab1->a, b1 = lab1->b;
    double L2 = lab2->L, a2 = lab2->a, b2 = lab2->b;
    double C1 = sqrt(a1 * a1 + b1 * b1);
    double C2 = sqrt(a2 * a2 + b2 * b2);
    double C_bar = (C1 + C2) / 2.0;
    double C_bar_7 = C_bar * C_bar * C_bar * C_bar * C_bar * C_bar * C_bar;
    double G = 0.5 * (1.0 - sqrt(C_bar_7 / (C_bar_7 + 6103515625.0)));
    double a1_prime = a1 * (1.0 + G);
    double a2_prime = a2 * (1.0 + G);
    double C1_prime = sqrt(a1_prime * a1_prime + b1 * b1);
    double C2_prime = sqrt(a2_prime * a2_prime + b2 * b2);
    double h1_prime = atan2(b1, a1_prime);
    if (h1_prime < 0.0) h1_prime += 2.0 * M_PI;
    double h2_prime = atan2(b2, a2_prime);
    if (h2_prime < 0.0) h2_prime += 2.0 * M_PI;
    double delta_L_prime = L2 - L1;
    double delta_C_prime = C2_prime - C1_prime;
    double delta_h_prime;
    if (C1_prime * C2_prime == 0.0) {
        delta_h_prime = 0.0;
    } else {
        double h_diff = h2_prime - h1_prime;
        if (fabs(h_diff) <= M_PI) delta_h_prime = h_diff;
        else if (h2_prime <= h1_prime) delta_h_prime = h_diff + 2.0 * M_PI;
        else delta_h_prime = h_diff - 2.0 * M_PI;
    }
    double delta_H_prime = 2.0 * sqrt(C1_prime * C2_prime) * sin(delta_h_prime / 2.0);
    double L_bar_prime = (L1 + L2) / 2.0;
    double C_bar_prime = (C1_prime + C2_prime) / 2.0;
    double h_bar_prime;
    if (C1_prime * C2_prime == 0.0) {
        h_bar_prime = h1_prime + h2_prime;
    } else {
        double h_sum = h1_prime + h2_prime;
        if (fabs(h1_prime - h2_prime) <= M_PI) h_bar_prime = h_sum / 2.0;
        else if (h_sum < 2.0 * M_PI) h_bar_prime = (h_sum + 2.0 * M_PI) / 2.0;
        else h_bar_prime = (h_sum - 2.0 * M_PI) / 2.0;
    }
    double T = 1.0 - 0.17 * cos(h_bar_prime - M_PI / 6.0)
        + 0.24 * cos(2.0 * h_bar_prime) + 0.32 * cos(3.0 * h_bar_prime + M_PI / 30.0)
        - 0.20 * cos(4.0 * h_bar_prime - 7.0 * M_PI / 20.0);
    double L_bar_50 = L_bar_prime - 50.0;
    double S_L = 1.0 + (0.015 * L_bar_50 * L_bar_50) / sqrt(20.0 + L_bar_50 * L_bar_50);
    double S_C = 1.0 + 0.045 * C_bar_prime;
    double S_H = 1.0 + 0.015 * C_bar_prime * T;
    double C_bar_7_prime = C_bar_prime * C_bar_prime * C_bar_prime * C_bar_prime * C_bar_prime * C_bar_prime * C_bar_prime;
    double R_T_angle = 30.0 * M_PI / 180.0;
    double delta_theta = R_T_angle * exp(-((h_bar_prime * 180.0 / M_PI - 275.0) / 25.0) * ((h_bar_prime * 180.0 / M_PI - 275.0) / 25.0));
    double R_C = 2.0 * sqrt(C_bar_7_prime / (C_bar_7_prime + 6103515625.0));
    double R_T = -R_C * sin(2.0 * delta_theta);
    double k_L = 1.0, k_C = 1.0, k_H = 1.0;
    double delta_L_sq = delta_L_prime / (k_L * S_L);
    double delta_C_sq = delta_C_prime / (k_C * S_C);
    double delta_H_sq = delta_H_prime / (k_H * S_H);
    return sqrt(delta_L_sq * delta_L_sq + delta_C_sq * delta_C_sq + delta_H_sq * delta_H_sq + R_T * delta_C_sq * delta_H_sq);
}

void gamut_map_config_init(gamut_map_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->method = GAMUT_MAP_HUE_PRESERVING;
    config->soft_clip_knee = 0.05;
    config->preserve_luminance = 1;
    config->chroma_compression = 1.0;
}

void gamut_get_boundary(const hdr_primaries_set_t *primaries, hdr_chromaticity_t polygon[3])
{
    if (!primaries || !polygon) return;
    polygon[0] = primaries->red;
    polygon[1] = primaries->green;
    polygon[2] = primaries->blue;
}

static double tri_area(const hdr_chromaticity_t *a, const hdr_chromaticity_t *b, const hdr_chromaticity_t *c)
{
    return fabs(a->x * (b->y - c->y) + b->x * (c->y - a->y) + c->x * (a->y - b->y)) / 2.0;
}

int gamut_is_inside(const hdr_chromaticity_t *chroma, const hdr_primaries_set_t *primaries)
{
    if (!chroma || !primaries) return 0;
    hdr_chromaticity_t boundary[3];
    gamut_get_boundary(primaries, boundary);
    double total_area = tri_area(&boundary[0], &boundary[1], &boundary[2]);
    double area1 = tri_area(chroma, &boundary[1], &boundary[2]);
    double area2 = tri_area(&boundary[0], chroma, &boundary[2]);
    double area3 = tri_area(&boundary[0], &boundary[1], chroma);
    return (fabs(area1 + area2 + area3 - total_area) < 1e-10) ? 1 : 0;
}

int gamut_map_apply(const hdr_rgb_pixel_t *rgb_in, const hdr_primaries_set_t *src_prim, const hdr_primaries_set_t *dst_prim, const gamut_map_config_t *config, hdr_rgb_pixel_t *rgb_out)
{
    if (!rgb_in || !dst_prim || !rgb_out) return -1;
    cie_xyz_t xyz;
    const hdr_primaries_set_t *src = src_prim ? src_prim : dst_prim;
    color_rgb_to_xyz_generic(rgb_in, src, &xyz);
    int result = color_xyz_to_rgb_generic(&xyz, dst_prim, rgb_out);
    if (result != 0 && config) {
        cie_lab_t lab;
        color_xyz_to_srgb(&xyz, rgb_out);
        color_srgb_to_lab(rgb_out, &lab);
        if (config->method == GAMUT_MAP_DESATURATE) {
            double L = lab.L;
            for (int iter = 0; iter < 20; iter++) {
                double scale = (iter + 1) * 0.05;
                cie_lab_t lab_scaled;
                lab_scaled.L = L + (50.0 - L) * scale;
                lab_scaled.a = lab.a * (1.0 - scale);
                lab_scaled.b = lab.b * (1.0 - scale);
                hdr_rgb_pixel_t test;
                color_lab_to_srgb(&lab_scaled, &test);
                cie_xyz_t test_xyz;
                color_srgb_to_xyz(&test, &test_xyz);
                if (color_xyz_to_rgb_generic(&test_xyz, dst_prim, rgb_out) == 0) { result = 0; break; }
            }
        } else if (config->method == GAMUT_MAP_HUE_PRESERVING) {
            double C = sqrt(lab.a * lab.a + lab.b * lab.b);
            if (C > 0.0) {
                for (int iter = 0; iter < 20; iter++) {
                    double scale = (iter + 1) * 0.05;
                    cie_lab_t lab_scaled;
                    lab_scaled.L = lab.L;
                    lab_scaled.a = lab.a * (1.0 - scale);
                    lab_scaled.b = lab.b * (1.0 - scale);
                    hdr_rgb_pixel_t test;
                    color_lab_to_srgb(&lab_scaled, &test);
                    cie_xyz_t test_xyz;
                    color_srgb_to_xyz(&test, &test_xyz);
                    if (color_xyz_to_rgb_generic(&test_xyz, dst_prim, rgb_out) == 0) { result = 0; break; }
                }
            }
        }
    }
    return result;
}

double gamut_volume_compute(const hdr_primaries_set_t *primaries, double peak_Y)
{
    if (!primaries) return 0.0;
    double vol = 0.0;
    int steps = 4;
    for (int ri = 0; ri <= steps; ri++)
        for (int gi = 0; gi <= steps; gi++)
            for (int bi = 0; bi <= steps; bi++) {
                hdr_rgb_pixel_t rgb;
                rgb.r = (double)ri / steps;
                rgb.g = (double)gi / steps;
                rgb.b = (double)bi / steps;
                cie_lab_t lab;
                color_srgb_to_lab(&rgb, &lab);
                vol += lab.L * fabs(lab.a) * fabs(lab.b);
            }
    return vol * peak_Y / 100.0;
}

double gamut_coverage_ratio(const hdr_primaries_set_t *src, const hdr_primaries_set_t *dst)
{
    if (!src || !dst) return 0.0;
    double vs = gamut_volume_compute(src, 100.0);
    double vd = gamut_volume_compute(dst, 100.0);
    if (vd <= 0.0) return 0.0;
    double r = vs / vd;
    return (r > 1.0) ? 1.0 : r;
}

void chroma_444_to_420(const double *y_444, const double *cb_444, const double *cr_444, int width, int height, double *cb_420, double *cr_420, int use_filter)
{
    if (!y_444 || !cb_444 || !cr_444 || !cb_420 || !cr_420 || width < 2 || height < 2) return;
    int hw = width / 2, hh = height / 2;
    for (int j = 0; j < hh; j++) {
        for (int i = 0; i < hw; i++) {
            int x0 = i * 2, y0 = j * 2;
            double sum_cb = 0.0, sum_cr = 0.0;
            int count = 0;
            if (use_filter) {
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int sx = i * 2 + dx;
                        int sy = j * 2 + dy;
                        if (sx < width && sy < height) {
                            double w = 1.0;
                            if (dx == 0 && dy == 0) w = 0.25;
                            sum_cb += w * cb_444[sy * width + sx];
                            sum_cr += w * cr_444[sy * width + sx];
                            count++;
                        }
                    }
                }
            } else {
                sum_cb = cb_444[y0 * width + x0];
                sum_cr = cr_444[y0 * width + x0];
                count = 1;
            }
            cb_420[j * hw + i] = (count > 0) ? sum_cb / (double)count : 0.0;
            cr_420[j * hw + i] = (count > 0) ? sum_cr / (double)count : 0.0;
        }
    }
}

void chroma_420_to_444(const double *y_420, const double *cb_420, const double *cr_420, int width, int height, double *cb_444, double *cr_444)
{
    if (!y_420 || !cb_420 || !cr_420 || !cb_444 || !cr_444 || width < 2 || height < 2) return;
    int hw = width / 2, hh = height / 2;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double fx = (double)x / 2.0;
            double fy = (double)y / 2.0;
            int ix = (int)fx;
            int iy = (int)fy;
            if (ix >= hw - 1) ix = hw - 2;
            if (iy >= hh - 1) iy = hh - 2;
            if (ix < 0) ix = 0;
            if (iy < 0) iy = 0;
            double tx = fx - (double)ix;
            double ty = fy - (double)iy;
            double cb00 = cb_420[iy * hw + ix];
            double cb10 = cb_420[iy * hw + ix + 1];
            double cb01 = cb_420[(iy + 1) * hw + ix];
            double cb11 = cb_420[(iy + 1) * hw + ix + 1];
            cb_444[y * width + x] = (1.0 - tx) * (1.0 - ty) * cb00 + tx * (1.0 - ty) * cb10 + (1.0 - tx) * ty * cb01 + tx * ty * cb11;
            double cr00 = cr_420[iy * hw + ix];
            double cr10 = cr_420[iy * hw + ix + 1];
            double cr01 = cr_420[(iy + 1) * hw + ix];
            double cr11 = cr_420[(iy + 1) * hw + ix + 1];
            cr_444[y * width + x] = (1.0 - tx) * (1.0 - ty) * cr00 + tx * (1.0 - ty) * cr10 + (1.0 - tx) * ty * cr01 + tx * ty * cr11;
        }
    }
}

void color_rgb_to_ycbcr_bt2020(const hdr_rgb_pixel_t *rgb, const hdr_pq_params_t *pq, double ycc[3])
{
    if (!rgb || !pq || !ycc) return;
    double R_oetf = hdr_pq_oetf(rgb->r, pq);
    double G_oetf = hdr_pq_oetf(rgb->g, pq);
    double B_oetf = hdr_pq_oetf(rgb->b, pq);
    ycc[0] = 0.2627 * R_oetf + 0.6780 * G_oetf + 0.0593 * B_oetf;
    ycc[1] = (B_oetf - ycc[0]) / 1.8814;
    ycc[2] = (R_oetf - ycc[0]) / 1.4746;
}

void color_ycbcr_to_rgb_bt2020(const double ycc[3], const hdr_pq_params_t *pq, hdr_rgb_pixel_t *rgb)
{
    if (!ycc || !pq || !rgb) return;
    double R_oetf = ycc[0] + 1.4746 * ycc[2];
    double B_oetf = ycc[0] + 1.8814 * ycc[1];
    double G_oetf = (ycc[0] - 0.2627 * R_oetf - 0.0593 * B_oetf) / 0.6780;
    rgb->r = hdr_pq_eotf(R_oetf, pq);
    rgb->g = hdr_pq_eotf(G_oetf, pq);
    rgb->b = hdr_pq_eotf(B_oetf, pq);
}

double color_luminance_bt2020(const hdr_rgb_pixel_t *rgb)
{
    if (!rgb) return 0.0;
    return 0.2627 * rgb->r + 0.6780 * rgb->g + 0.0593 * rgb->b;
}

double color_luminance_bt709(const hdr_rgb_pixel_t *rgb)
{
    if (!rgb) return 0.0;
    return 0.2126 * rgb->r + 0.7152 * rgb->g + 0.0722 * rgb->b;
}

void color_srgb_to_oklab(const hdr_rgb_pixel_t *rgb, cie_lab_t *lab)
{
    if (!rgb || !lab) return;
    double r = srgb_linearize(rgb->r);
    double g = srgb_linearize(rgb->g);
    double b = srgb_linearize(rgb->b);
    double l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    double m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    double s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;
    double l_ = cbrt(l), m_ = cbrt(m), s_ = cbrt(s);
    lab->L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_;
    lab->a = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_;
    lab->b = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_;
}

void color_oklab_to_srgb(const cie_lab_t *lab, hdr_rgb_pixel_t *rgb)
{
    if (!lab || !rgb) return;
    double l_ = lab->L + 0.3963377774 * lab->a + 0.2158037573 * lab->b;
    double m_ = lab->L - 0.1055613458 * lab->a - 0.0638541728 * lab->b;
    double s_ = lab->L - 0.0894841775 * lab->a - 1.2914855480 * lab->b;
    double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    double r_lin =  4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    double g_lin = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    double b_lin = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    rgb->r = srgb_gamma_encode(r_lin);
    rgb->g = srgb_gamma_encode(g_lin);
    rgb->b = srgb_gamma_encode(b_lin);
}

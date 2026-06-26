#include "hdr_advanced.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ??????????????????????????????????????????????????????????????????????
 * JzAzBz Color Space (ITU-R BT.2124)
 * ?????????????????????????????????????????????????????????????????????? */

static const double JZAZBZ_c1 = 3424.0 / 4096.0;
static const double JZAZBZ_c2 = 2413.0 / 4096.0 * 32.0;
static const double JZAZBZ_n  = 2610.0 / 16384.0;
static const double JZAZBZ_p  = 1.7 * 2523.0 / 4096.0 * 32.0;

void color_xyz_to_jzazbz(const cie_xyz_t *xyz, double lum_max, jzazbz_t *jzaz)
{
    if (!xyz || !jzaz || lum_max <= 0.0) return;

    /* Step 1: Normalize XYZ by peak luminance */
    double Xp = xyz->X / lum_max;
    double Yp = xyz->Y / lum_max;
    double Zp = xyz->Z / lum_max;

    /* Step 2: XYZ to LMS (modified matrix from Safdar et al. 2017) */
    double Lp =  0.41478972 * Xp + 0.579999 * Yp + 0.0146480 * Zp;
    double Mp = -0.2015100  * Xp + 1.120649 * Yp + 0.0531008 * Zp;
    double Sp = -0.0166008  * Xp + 0.264800 * Yp + 0.6684799 * Zp;

    /* Step 3: Apply perceptual quantizer nonlinearity */
    double Lp_abs = fabs(Lp);
    double Mp_abs = fabs(Mp);
    double Sp_abs = fabs(Sp);

    double Lpp = pow((JZAZBZ_c1 + JZAZBZ_c2 * pow(Lp_abs, JZAZBZ_n)) /
                     (1.0 + JZAZBZ_c2 * 0.1 * pow(Lp_abs, JZAZBZ_n)), JZAZBZ_p);
    double Mpp = pow((JZAZBZ_c1 + JZAZBZ_c2 * pow(Mp_abs, JZAZBZ_n)) /
                     (1.0 + JZAZBZ_c2 * 0.1 * pow(Mp_abs, JZAZBZ_n)), JZAZBZ_p);
    double Spp = pow((JZAZBZ_c1 + JZAZBZ_c2 * pow(Sp_abs, JZAZBZ_n)) /
                     (1.0 + JZAZBZ_c2 * 0.1 * pow(Sp_abs, JZAZBZ_n)), JZAZBZ_p);

    /* Restore sign */
    if (Lp < 0.0) Lpp = -Lpp;
    if (Mp < 0.0) Mpp = -Mpp;
    if (Sp < 0.0) Spp = -Spp;

    /* Step 4: LMS' to JzAzBz */
    jzaz->Jz = 0.5 * Lpp + 0.5 * Mpp;
    jzaz->Az = 3.524000 * Lpp - 4.066708 * Mpp + 0.542708 * Spp;
    jzaz->Bz = 0.199076 * Lpp + 1.096799 * Mpp - 1.295875 * Spp;
}

void color_jzazbz_to_xyz(const jzazbz_t *jzaz, double lum_max, cie_xyz_t *xyz)
{
    if (!jzaz || !xyz || lum_max <= 0.0) return;

    /* Step 1: JzAzBz to LMS' */
    double Lpp = jzaz->Jz + 0.005826 * jzaz->Az + 0.057204 * jzaz->Bz;
    double Mpp = jzaz->Jz - 0.005826 * jzaz->Az - 0.057204 * jzaz->Bz;
    double Spp = jzaz->Jz + 0.563698 * jzaz->Az - 0.322900 * jzaz->Bz;

    /* Step 2: Inverse perceptual quantizer */
    double Lp_abs = fabs(Lpp);
    double Mp_abs = fabs(Mpp);
    double Sp_abs = fabs(Spp);

    double Lp = pow((JZAZBZ_c1 - pow(Lp_abs, 1.0 / JZAZBZ_p)) /
                    (JZAZBZ_c2 * 0.1 * pow(Lp_abs, 1.0 / JZAZBZ_p) - JZAZBZ_c2), 1.0 / JZAZBZ_n);
    double Mp = pow((JZAZBZ_c1 - pow(Mp_abs, 1.0 / JZAZBZ_p)) /
                    (JZAZBZ_c2 * 0.1 * pow(Mp_abs, 1.0 / JZAZBZ_p) - JZAZBZ_c2), 1.0 / JZAZBZ_n);
    double Sp = pow((JZAZBZ_c1 - pow(Sp_abs, 1.0 / JZAZBZ_p)) /
                    (JZAZBZ_c2 * 0.1 * pow(Sp_abs, 1.0 / JZAZBZ_p) - JZAZBZ_c2), 1.0 / JZAZBZ_n);

    /* Restore sign */
    if (Lpp < 0.0) Lp = -Lp;
    if (Mpp < 0.0) Mp = -Mp;
    if (Spp < 0.0) Sp = -Sp;

    /* Step 3: LMS to XYZ (inverse matrix) */
    double Xp =  1.9242264357876069  * Lp - 1.0047923125953657 * Mp + 0.037651404030618  * Sp;
    double Yp =  0.3503167620949991  * Lp + 0.7264811939316552 * Mp - 0.065384422948085  * Sp;
    double Zp = -0.09098281098284752 * Lp - 0.3127282905230739 * Mp + 1.5227665613052603 * Sp;

    xyz->X = Xp * lum_max;
    xyz->Y = Yp * lum_max;
    xyz->Z = Zp * lum_max;
}

void color_bt2020_to_jzazbz(const hdr_rgb_pixel_t *rgb, double lum_max, jzazbz_t *jzaz)
{
    if (!rgb || !jzaz) return;
    const hdr_primaries_set_t *bt2020 = hdr_primaries_get(HDR_PRIMARIES_BT2020);
    cie_xyz_t xyz;
    color_rgb_to_xyz_generic(rgb, bt2020, &xyz);
    color_xyz_to_jzazbz(&xyz, lum_max, jzaz);
}

/* ??????????????????????????????????????????????????????????????????????
 * Bradford Chromatic Adaptation Transform
 * ?????????????????????????????????????????????????????????????????????? */

/* Bradford cone response matrix (normalized for D65) */
static const double BRADFORD_M_A[3][3] = {
    { 0.8951,  0.2664, -0.1614},
    {-0.7502,  1.7135,  0.0367},
    { 0.0389, -0.0685,  1.0296}
};

static const double BRADFORD_M_A_INV[3][3] = {
    { 0.9869929, -0.1470543,  0.1599627},
    { 0.4323053,  0.5183603,  0.0492912},
    {-0.0085287,  0.0400428,  0.9684867}
};

void color_bradford_matrix(const cie_xyz_t *white_src, const cie_xyz_t *white_dst,
                           double matrix[3][3])
{
    if (!white_src || !white_dst || !matrix) return;

    /* Convert white points to cone responses: rho = M_A * XYZ */
    double rho_s = BRADFORD_M_A[0][0] * white_src->X + BRADFORD_M_A[0][1] * white_src->Y + BRADFORD_M_A[0][2] * white_src->Z;
    double gam_s = BRADFORD_M_A[1][0] * white_src->X + BRADFORD_M_A[1][1] * white_src->Y + BRADFORD_M_A[1][2] * white_src->Z;
    double bet_s = BRADFORD_M_A[2][0] * white_src->X + BRADFORD_M_A[2][1] * white_src->Y + BRADFORD_M_A[2][2] * white_src->Z;

    double rho_d = BRADFORD_M_A[0][0] * white_dst->X + BRADFORD_M_A[0][1] * white_dst->Y + BRADFORD_M_A[0][2] * white_dst->Z;
    double gam_d = BRADFORD_M_A[1][0] * white_dst->X + BRADFORD_M_A[1][1] * white_dst->Y + BRADFORD_M_A[1][2] * white_dst->Z;
    double bet_d = BRADFORD_M_A[2][0] * white_dst->X + BRADFORD_M_A[2][1] * white_dst->Y + BRADFORD_M_A[2][2] * white_dst->Z;

    /* Diagonal scaling matrix */
    double s_rho = (rho_s > 1e-12) ? rho_d / rho_s : 1.0;
    double s_gam = (gam_s > 1e-12) ? gam_d / gam_s : 1.0;
    double s_bet = (bet_s > 1e-12) ? bet_d / bet_s : 1.0;

    /* M_cat = M_A^-1 * diag(s_rho, s_gam, s_bet) * M_A */
    double temp[3][3];
    for (int i = 0; i < 3; i++) {
        temp[i][0] = BRADFORD_M_A_INV[i][0] * s_rho;
        temp[i][1] = BRADFORD_M_A_INV[i][1] * s_gam;
        temp[i][2] = BRADFORD_M_A_INV[i][2] * s_bet;
    }
    color_matrix_multiply(temp, BRADFORD_M_A, matrix);
}

void color_bradford_cat(const cie_xyz_t *xyz_src, const cie_xyz_t *white_src,
                        const cie_xyz_t *white_dst, cie_xyz_t *xyz_dst)
{
    if (!xyz_src || !white_src || !white_dst || !xyz_dst) return;
    double m[3][3];
    color_bradford_matrix(white_src, white_dst, m);
    color_matrix_apply_xyz(m, xyz_src, xyz_dst);
}

void color_illuminant_xyz(char illuminant, cie_xyz_t *xyz)
{
    if (!xyz) return;
    switch (illuminant) {
    case 'D': /* D65 */
        xyz->X = 0.95047; xyz->Y = 1.0; xyz->Z = 1.08883;
        break;
    case '5': /* D50 */
        xyz->X = 0.96422; xyz->Y = 1.0; xyz->Z = 0.82521;
        break;
    case 'A': /* Illuminant A (tungsten) */
        xyz->X = 1.09850; xyz->Y = 1.0; xyz->Z = 0.35585;
        break;
    case 'E': /* Equal energy */
    default:
        xyz->X = 1.0; xyz->Y = 1.0; xyz->Z = 1.0;
        break;
    }
}

/* ??????????????????????????????????????????????????????????????????????
 * HDR Perceptually Uniform (PU) Encoding & Quality Metrics
 * ?????????????????????????????????????????????????????????????????????? */

/**
 * PU encoding constants from Aydin et al. (2008).
 * The PU curve maps luminance to approximately JND-scaled units.
 */
static double pu_band_lum(double L)
{
    /* Simplified PU encoding based on Barten CSF integration */
    double logL = log10(L + 1e-10);
    /* Piecewise polynomial fit to the integral of CSF */
    if (logL < -2.0) return 0.01 * (logL + 2.0);
    if (logL < 0.0)  return 0.02 + 0.15 * (logL + 2.0);
    if (logL < 2.0)  return 0.32 + 0.40 * logL;
    if (logL < 4.0)  return 1.12 + 0.25 * (logL - 2.0);
    return 1.62 + 0.10 * (logL - 4.0);
}

double hdr_pu_encode(double luminance)
{
    if (luminance <= 0.0) return 0.0;
    return pu_band_lum(luminance);
}

double hdr_pu_decode(double pu_value)
{
    if (pu_value <= 0.0) return 0.0;
    /* Approximate inverse of the PU band function */
    if (pu_value < 0.02)  return pow(10.0, pu_value / 0.01 - 2.0);
    if (pu_value < 0.32)  return pow(10.0, (pu_value - 0.02) / 0.15 - 2.0);
    if (pu_value < 1.12)  return pow(10.0, (pu_value - 0.32) / 0.40);
    if (pu_value < 1.62)  return pow(10.0, (pu_value - 1.12) / 0.25 + 2.0);
    return pow(10.0, (pu_value - 1.62) / 0.10 + 4.0);
}

void hdr_quality_assess(const hdr_image_buffer_t *ref, const hdr_image_buffer_t *test,
                        hdr_quality_metrics_t *metrics)
{
    if (!ref || !test || !metrics || !ref->pixels || !test->pixels) return;
    if (ref->width != test->width || ref->height != test->height) return;
    memset(metrics, 0, sizeof(*metrics));

    int total = ref->width * ref->height;
    double sum_linear_se = 0.0, sum_pu_se = 0.0, sum_log_se = 0.0, sum_de = 0.0;
    double max_linear = 0.0;

    for (int i = 0; i < total; i++) {
        double r_ref = ref->pixels[i].r;
        double g_ref = ref->pixels[i].g;
        double b_ref = ref->pixels[i].b;
        double r_tst = test->pixels[i].r;
        double g_tst = test->pixels[i].g;
        double b_tst = test->pixels[i].b;

        /* Linear PSNR components */
        double dr = r_ref - r_tst, dg = g_ref - g_tst, db = b_ref - b_tst;
        sum_linear_se += dr * dr + dg * dg + db * db;

        /* Per-channel maximum for PSNR */
        double max_ch = r_ref; if (g_ref > max_ch) max_ch = g_ref; if (b_ref > max_ch) max_ch = b_ref;
        if (max_ch > max_linear) max_linear = max_ch;

        /* PU-encoded PSNR */
        double pu_rr = hdr_pu_encode(r_ref), pu_rt = hdr_pu_encode(r_tst);
        double pu_gr = hdr_pu_encode(g_ref), pu_gt = hdr_pu_encode(g_tst);
        double pu_br = hdr_pu_encode(b_ref), pu_bt = hdr_pu_encode(b_tst);
        sum_pu_se += (pu_rr - pu_rt) * (pu_rr - pu_rt)
                   + (pu_gr - pu_gt) * (pu_gr - pu_gt)
                   + (pu_br - pu_bt) * (pu_br - pu_bt);

        /* Log RMSE */
        double log_r_ref = log10(r_ref + 1e-6), log_r_tst = log10(r_tst + 1e-6);
        double log_g_ref = log10(g_ref + 1e-6), log_g_tst = log10(g_tst + 1e-6);
        double log_b_ref = log10(b_ref + 1e-6), log_b_tst = log10(b_tst + 1e-6);
        sum_log_se += (log_r_ref - log_r_tst) * (log_r_ref - log_r_tst)
                    + (log_g_ref - log_g_tst) * (log_g_ref - log_g_tst)
                    + (log_b_ref - log_b_tst) * (log_b_ref - log_b_tst);

        /* Delta E approximation in ICtCp */
        hdr_rgb_pixel_t ref_px = {r_ref, g_ref, b_ref};
        hdr_rgb_pixel_t tst_px = {r_tst, g_tst, b_tst};
        ictcp_t ict_ref, ict_tst;
        color_bt2020_rgb_to_ictcp(&ref_px, &ict_ref);
        color_bt2020_rgb_to_ictcp(&tst_px, &ict_tst);
        double dI = ict_ref.I - ict_tst.I;
        double dCt = ict_ref.Ct - ict_tst.Ct;
        double dCp = ict_ref.Cp - ict_tst.Cp;
        sum_de += 720.0 * sqrt(dI * dI + 0.5 * dCt * dCt + dCp * dCp);
    }

    double n = (double)(total * 3);
    double mse_linear = sum_linear_se / n;
    metrics->psnr_linear = (mse_linear > 0.0) ? 10.0 * log10(max_linear * max_linear / mse_linear) : 100.0;

    double mse_pu = sum_pu_se / n;
    metrics->psnr_pu = (mse_pu > 0.0) ? 10.0 * log10(10.0 / mse_pu) : 100.0;

    metrics->log_rmse = sqrt(sum_log_se / n);
    metrics->delta_e_itp = sum_de / (double)total;

    /* Simplified HDR-VDP score: 100 = perfect, 0 = terrible */
    double pu_factor = (metrics->psnr_pu > 40.0) ? 1.0 : metrics->psnr_pu / 40.0;
    double de_factor = (metrics->delta_e_itp < 1.0) ? 1.0 : 1.0 / (1.0 + metrics->delta_e_itp);
    metrics->hdr_vdp_score = 100.0 * pu_factor * de_factor;

    /* Simplified PU-SSIM */
    double pu_ssim = 1.0 - metrics->log_rmse / 3.0;
    if (pu_ssim < 0.0) pu_ssim = 0.0;
    metrics->pu_ssim = pu_ssim;
}

void hdr_hdr_vdp_map(const hdr_image_buffer_t *ref, const hdr_image_buffer_t *test,
                     double *prob_map, int width, int height)
{
    if (!ref || !test || !prob_map || !ref->pixels || !test->pixels) return;
    if (ref->width != width || ref->height != height) return;
    if (test->width != width || test->height != height) return;

    int total = width * height;
    for (int i = 0; i < total; i++) {
        /* Compute PU-encoded difference */
        double pu_r_diff = fabs(hdr_pu_encode(ref->pixels[i].r) - hdr_pu_encode(test->pixels[i].r));
        double pu_g_diff = fabs(hdr_pu_encode(ref->pixels[i].g) - hdr_pu_encode(test->pixels[i].g));
        double pu_b_diff = fabs(hdr_pu_encode(ref->pixels[i].b) - hdr_pu_encode(test->pixels[i].b));

        /* Combine channels (luminance-weighted) */
        double Y_ref = color_luminance_bt2020(&ref->pixels[i]);
        double adaptation = (Y_ref > 0.0) ? Y_ref : 1.0;

        /* Psychometric function: probability of detection increases with PU difference */
        double pu_diff = 0.299 * pu_r_diff + 0.587 * pu_g_diff + 0.114 * pu_b_diff;
        double threshold = 0.01 * pow(adaptation / 100.0, -0.2);
        double scale = pu_diff / threshold;
        prob_map[i] = 1.0 - exp(-scale * scale);
    }
}

/* ??????????????????????????????????????????????????????????????????????
 * ACES Support
 * ?????????????????????????????????????????????????????????????????????? */

/* AP0 to AP1 conversion matrix (ACES 1.0) */
static const double AP0_TO_AP1[3][3] = {
    { 1.4514393161, -0.2365107469, -0.2149285693},
    {-0.0765537734,  1.1762296998, -0.0996759264},
    { 0.0083161484, -0.0060324498,  0.9977163014}
};

static const double AP1_TO_AP0[3][3] = {
    { 0.6954522414,  0.1406786965,  0.1638690622},
    { 0.0447945634,  0.8596711185,  0.0955343182},
    {-0.0055258826,  0.0040252103,  1.0015006723}
};

void color_aces_ap0_to_ap1(const hdr_rgb_pixel_t *ap0, hdr_rgb_pixel_t *ap1)
{
    if (!ap0 || !ap1) return;
    ap1->r = AP0_TO_AP1[0][0] * ap0->r + AP0_TO_AP1[0][1] * ap0->g + AP0_TO_AP1[0][2] * ap0->b;
    ap1->g = AP0_TO_AP1[1][0] * ap0->r + AP0_TO_AP1[1][1] * ap0->g + AP0_TO_AP1[1][2] * ap0->b;
    ap1->b = AP0_TO_AP1[2][0] * ap0->r + AP0_TO_AP1[2][1] * ap0->g + AP0_TO_AP1[2][2] * ap0->b;
}

void color_aces_ap1_to_ap0(const hdr_rgb_pixel_t *ap1, hdr_rgb_pixel_t *ap0)
{
    if (!ap1 || !ap0) return;
    ap0->r = AP1_TO_AP0[0][0] * ap1->r + AP1_TO_AP0[0][1] * ap1->g + AP1_TO_AP0[0][2] * ap1->b;
    ap0->g = AP1_TO_AP0[1][0] * ap1->r + AP1_TO_AP0[1][1] * ap1->g + AP1_TO_AP0[1][2] * ap1->b;
    ap0->b = AP1_TO_AP0[2][0] * ap1->r + AP1_TO_AP0[2][1] * ap1->g + AP1_TO_AP0[2][2] * ap1->b;
}

/**
 * Simplified ACES RRT (Reference Rendering Transform).
 *
 * Applies a sigmoidal tone curve with a filmic shoulder and toe,
 * approximating the full ACES 1.0 RRT + ODT pipeline.
 */
void color_aces_rrt_approximate(const hdr_rgb_pixel_t *scene_linear, hdr_rgb_pixel_t *display)
{
    if (!scene_linear || !display) return;

    /* Per-channel sigmoid-based tone mapping */
    double r = scene_linear->r;
    double g = scene_linear->g;
    double b = scene_linear->b;

    /* Filmic S-curve: x * (a*x + b) / (x * (a*x + c) + d) */
    /* Parameters from Narkowicz ACES approximation */
    double a = 2.51, b_val = 0.03, c = 2.43, d = 0.59, e = 0.14;

    /* Sigmoid helper */
    double ac[3] = {r, g, b};
    for (int ch = 0; ch < 3; ch++) {
        double x = ac[ch];
        if (x <= 0.0) x = 0.0;
        else {
            double xa = x * (a * x + b_val);
            double xb = x * (c * x + d) + e;
            x = xa / xb;
            if (x < 0.0) x = 0.0;
            if (x > 1.0) x = 1.0;
        }
        ac[ch] = x;
    }
    display->r = ac[0];
    display->g = ac[1];
    display->b = ac[2];
}

/* ??????????????????????????????????????????????????????????????????????
 * Stevens' Power Law & Surround Effects
 * ?????????????????????????????????????????????????????????????????????? */

double hdr_stevens_brightness(double luminance)
{
    if (luminance <= 0.0) return 0.0;
    /* Stevens (1961): perceived brightness follows L^0.33 for point sources
     * and L^0.5 for extended sources. We use L^0.42 as a compromise. */
    return 10.0 * pow(luminance / 100.0, 0.42);
}

double hdr_surround_contrast_factor(double luminance, double surround_lum)
{
    if (luminance <= 0.0) return 0.0;

    /* Bartleson-Breneman effect:
     * Perceived contrast increases with darker surrounds.
     * The factor ranges from ~0.7 (dark surround) to ~1.3 (bright surround). */
    double ratio = surround_lum / (luminance + 1e-6);
    /* Sigmoid mapping: center at ratio=1, range [0.5, 2.0] */
    double log_ratio = log10(ratio + 1e-6);
    double factor = 1.0 + 0.5 * tanh(log_ratio);
    return factor;
}

double hdr_optimal_peak_for_ambient(double ambient_lux, double black_level)
{
    if (ambient_lux <= 0.0) ambient_lux = 5.0;
    if (black_level <= 0.0) black_level = 0.005;

    /* The required peak luminance increases with ambient light
     * to maintain the same perceptual dynamic range.
     * Model: peak = k * ambient^0.5 * black_level^(-0.2) */
    double peak = 100.0 * pow(ambient_lux / 5.0, 0.5) * pow(0.005 / black_level, 0.2);
    if (peak < 80.0) peak = 80.0;
    if (peak > 10000.0) peak = 10000.0;
    return peak;
}

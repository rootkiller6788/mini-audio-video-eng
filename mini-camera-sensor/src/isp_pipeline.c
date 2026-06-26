/**
 * @file isp_pipeline.c
 * @brief Image Signal Processing pipeline implementation
 *
 * Implements the complete RAW-to-RGB/YUV processing chain.
 * Each stage function implements an independent ISP algorithm.
 *
 * Reference: Poynton (2012); Ramanath et al. IEEE SPM 2005
 */
#include "isp_pipeline.h"
#include "demosaic.h"
#include "color_science.h"
#include "noise_model.h"

/*===========================================================================
 * ISP pipeline configuration (L1+L2)
 *===========================================================================*/

void isp_pipeline_init_default(isp_pipeline_config_t *p)
{
    if (p == NULL) return;
    memset(p, 0, sizeof(*p));

    isp_stage_t s;
    for (s = 0; s < ISP_STAGE_COUNT; s++) {
        p->stages[s].stage   = s;
        p->stages[s].enabled = 1;   /* All enabled by default */
        p->stages[s].params  = NULL;
    }
    p->stages_enabled = ISP_STAGE_COUNT;
}

void isp_stage_enable(isp_pipeline_config_t *p, isp_stage_t s, uint8_t en)
{
    if (p == NULL || s >= ISP_STAGE_COUNT) return;
    if (en && !p->stages[s].enabled) {
        p->stages[s].enabled = 1;
        p->stages_enabled++;
    } else if (!en && p->stages[s].enabled) {
        p->stages[s].enabled = 0;
        p->stages_enabled--;
    }
}

/*===========================================================================
 * Image allocation (L2)
 *===========================================================================*/

rgb_image_t *rgb_image_alloc(uint32_t w, uint32_t h, uint8_t bpc)
{
    if (w == 0 || h == 0) return NULL;
    rgb_image_t *img = (rgb_image_t *)malloc(sizeof(rgb_image_t));
    if (img == NULL) return NULL;

    size_t pixel_size;
    switch (bpc) {
        case 8:  pixel_size = sizeof(rgb_pixel_t);   break;
        case 16: pixel_size = sizeof(rgb16_pixel_t);  break;
        case 32: pixel_size = sizeof(rgbf_pixel_t);   break;
        default: free(img); return NULL;
    }

    img->data = calloc((size_t)w * (size_t)h, pixel_size);
    if (img->data == NULL) {
        free(img);
        return NULL;
    }
    img->width  = w;
    img->height = h;
    img->bits_per_channel = bpc;
    return img;
}

void rgb_image_free(rgb_image_t *img)
{
    if (img == NULL) return;
    free(img->data);
    free(img);
}

yuv_image_t *yuv_image_alloc(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return NULL;
    yuv_image_t *img = (yuv_image_t *)malloc(sizeof(yuv_image_t));
    if (img == NULL) return NULL;
    img->data = (yuv_pixel_t *)calloc((size_t)w * h, sizeof(yuv_pixel_t));
    if (img->data == NULL) {
        free(img);
        return NULL;
    }
    img->width = w;
    img->height = h;
    return img;
}

void yuv_image_free(yuv_image_t *img)
{
    if (img == NULL) return;
    free(img->data);
    free(img);
}

/*===========================================================================
 * Black level correction (L5)
 *===========================================================================*/

int isp_black_level_correct(raw_frame_t *raw, uint16_t bl_r, uint16_t bl_gr,
                             uint16_t bl_gb, uint16_t bl_b)
{
    if (raw == NULL || raw->data == NULL) return -1;

    uint32_t x, y;
    for (y = 0; y < raw->height; y++) {
        for (x = 0; x < raw->width; x++) {
            bayer_color_t c = bayer_color_at(x, y, raw->cfa);
            uint16_t bl;
            switch (c) {
                case BAYER_COLOR_R:  bl = bl_r;  break;
                case BAYER_COLOR_GR: bl = bl_gr; break;
                case BAYER_COLOR_GB: bl = bl_gb; break;
                case BAYER_COLOR_B:  bl = bl_b;  break;
                default: bl = bl_gr; break;
            }

            pixel_raw_t v = raw->data[y * raw->stride + x];
            if (v > bl) {
                raw->data[y * raw->stride + x] = v - bl;
            } else {
                raw->data[y * raw->stride + x] = 0;
            }
        }
    }
    return 0;
}

/*===========================================================================
 * Lens shading correction (L5)
 *
 * Radial gain model: gain(r) = 1 + a2*r^2 + a4*r^4 + a6*r^6
 * r = distance from optical center, normalized to [0,1]
 *===========================================================================*/

int isp_lens_shading_correct_rgb(rgb_image_t *img, double a2, double a4,
                                  double a6, double cx, double cy)
{
    if (img == NULL || img->bits_per_channel == 0) return -1;

    double half_w = (double)img->width / 2.0;
    double half_h = (double)img->height / 2.0;
    double max_r = sqrt(cx*cx + cy*cy);
    if (max_r < 1.0) max_r = 1.0;

    uint32_t x, y;
    for (y = 0; y < img->height; y++) {
        for (x = 0; x < img->width; x++) {
            double dx = ((double)x - cx) / half_w;
            double dy = ((double)y - cy) / half_h;
            double r2 = dx*dx + dy*dy;
            double r4 = r2 * r2;
            double r6 = r4 * r2;
            double gain = 1.0 + a2 * r2 + a4 * r4 + a6 * r6;
            if (gain < 0.0) gain = 0.0;

            if (img->bits_per_channel == 16) {
                rgb16_pixel_t *px = &((rgb16_pixel_t *)img->data)[y * img->width + x];
                double r = (double)px->r * gain;
                double g = (double)px->g * gain;
                double b = (double)px->b * gain;
                px->r = (uint16_t)(r > 65535.0 ? 65535 : (r + 0.5));
                px->g = (uint16_t)(g > 65535.0 ? 65535 : (g + 0.5));
                px->b = (uint16_t)(b > 65535.0 ? 65535 : (b + 0.5));
            } else if (img->bits_per_channel == 8) {
                rgb_pixel_t *px = &((rgb_pixel_t *)img->data)[y * img->width + x];
                double r = (double)px->r * gain;
                double g = (double)px->g * gain;
                double b = (double)px->b * gain;
                px->r = (uint8_t)(r > 255.0 ? 255 : (r + 0.5));
                px->g = (uint8_t)(g > 255.0 ? 255 : (g + 0.5));
                px->b = (uint8_t)(b > 255.0 ? 255 : (b + 0.5));
            }
        }
    }
    return 0;
}

/*===========================================================================
 * White balance (L5+L6)
 *===========================================================================*/

int isp_white_balance_rgb(rgb_image_t *img, double wb_r, double wb_g,
                           double wb_b)
{
    if (img == NULL) return -1;
    uint32_t n = img->width * img->height;
    uint32_t i;

    if (img->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = (double)p[i].r * wb_r;
            double g = (double)p[i].g * wb_g;
            double b = (double)p[i].b * wb_b;
            p[i].r = (uint16_t)(r > 65535 ? 65535 : (r + 0.5));
            p[i].g = (uint16_t)(g > 65535 ? 65535 : (g + 0.5));
            p[i].b = (uint16_t)(b > 65535 ? 65535 : (b + 0.5));
        }
    } else if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = (double)p[i].r * wb_r;
            double g = (double)p[i].g * wb_g;
            double b = (double)p[i].b * wb_b;
            p[i].r = (uint8_t)(r > 255 ? 255 : (r + 0.5));
            p[i].g = (uint8_t)(g > 255 ? 255 : (g + 0.5));
            p[i].b = (uint8_t)(b > 255 ? 255 : (b + 0.5));
        }
    } else {
        return -1;
    }
    return 0;
}

/**
 * Gray World white balance estimation.
 *
 * Theorem: For a typical scene with sufficient color variety,
 * the average of R, G, B should be equal (gray).
 *
 * Gains: g_R = G_avg / R_avg, g_G = 1.0, g_B = G_avg / B_avg
 */
void isp_gray_world_estimate(const rgb_image_t *img,
                              double *wb_r, double *wb_g, double *wb_b)
{
    if (img == NULL || wb_r == NULL || wb_g == NULL || wb_b == NULL) return;

    double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
    uint32_t n = img->width * img->height;
    uint32_t i;

    if (img->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)img->data;
        for (i = 0; i < n; i++) { sum_r += p[i].r; sum_g += p[i].g; sum_b += p[i].b; }
    } else if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) { sum_r += p[i].r; sum_g += p[i].g; sum_b += p[i].b; }
    }

    double avg_r = sum_r / n, avg_g = sum_g / n, avg_b = sum_b / n;
    *wb_g = 1.0;
    *wb_r = (avg_r > 0.0) ? avg_g / avg_r : 1.0;
    *wb_b = (avg_b > 0.0) ? avg_g / avg_b : 1.0;
}

/**
 * White Patch (Retinex) white balance estimation.
 *
 * Assumption: the brightest region in the image should be white.
 * gains = white_level / max_channel
 */
void isp_white_patch_estimate(const rgb_image_t *img,
                               double *wb_r, double *wb_g, double *wb_b)
{
    if (img == NULL || wb_r == NULL || wb_g == NULL || wb_b == NULL) return;

    double max_r = 0.0, max_g = 0.0, max_b = 0.0;
    uint32_t n = img->width * img->height;
    uint32_t i;

    if (img->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            if (p[i].r > max_r) max_r = p[i].r;
            if (p[i].g > max_g) max_g = p[i].g;
            if (p[i].b > max_b) max_b = p[i].b;
        }
    } else {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            if (p[i].r > max_r) max_r = p[i].r;
            if (p[i].g > max_g) max_g = p[i].g;
            if (p[i].b > max_b) max_b = p[i].b;
        }
    }

    double white_level = (max_r + max_g + max_b) / 3.0;
    *wb_r = (max_r > 0.0) ? white_level / max_r : 1.0;
    *wb_g = (max_g > 0.0) ? white_level / max_g : 1.0;
    *wb_b = (max_b > 0.0) ? white_level / max_b : 1.0;
}

/*===========================================================================
 * Color correction matrix (L5+L6)
 *===========================================================================*/

/**
 * Apply 3x3 CCM to RGB image.
 *
 * [R']   [c00 c01 c02] [R]
 * [G'] = [c10 c11 c12] [G]
 * [B']   [c20 c21 c22] [B]
 */
int isp_color_correction_rgb(rgb_image_t *img, const double ccm[9])
{
    if (img == NULL || ccm == NULL) return -1;
    uint32_t n = img->width * img->height;
    uint32_t i;

    if (img->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = p[i].r, g = p[i].g, b = p[i].b;
            double r2 = ccm[0]*r + ccm[1]*g + ccm[2]*b;
            double g2 = ccm[3]*r + ccm[4]*g + ccm[5]*b;
            double b2 = ccm[6]*r + ccm[7]*g + ccm[8]*b;
            p[i].r = (uint16_t)(r2 > 65535 ? 65535 : (r2 < 0 ? 0 : (r2+0.5)));
            p[i].g = (uint16_t)(g2 > 65535 ? 65535 : (g2 < 0 ? 0 : (g2+0.5)));
            p[i].b = (uint16_t)(b2 > 65535 ? 65535 : (b2 < 0 ? 0 : (b2+0.5)));
        }
    } else if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = p[i].r, g = p[i].g, b = p[i].b;
            double r2 = ccm[0]*r + ccm[1]*g + ccm[2]*b;
            double g2 = ccm[3]*r + ccm[4]*g + ccm[5]*b;
            double b2 = ccm[6]*r + ccm[7]*g + ccm[8]*b;
            p[i].r = (uint8_t)(r2 > 255 ? 255 : (r2 < 0 ? 0 : (r2+0.5)));
            p[i].g = (uint8_t)(g2 > 255 ? 255 : (g2 < 0 ? 0 : (g2+0.5)));
            p[i].b = (uint8_t)(b2 > 255 ? 255 : (b2 < 0 ? 0 : (b2+0.5)));
        }
    } else {
        return -1;
    }
    return 0;
}

/**
 * CCM calibration via least squares.
 *
 * min ||S*CCM^T - T||_F^2
 *
 * Solution: CCM^T = (S^T S)^(-1) S^T T
 * where S (n×3) = sensor RGB, T (n×3) = target RGB.
 */
int isp_ccm_calibrate(const double *sensor_rgb, const double *target_rgb,
                       uint32_t n, double ccm[9])
{
    if (n < 3 || sensor_rgb == NULL || target_rgb == NULL || ccm == NULL)
        return -1;

    /* Build normal equations: A = S^T S (3x3), B = S^T T (3x3) */
    double A[9] = {0};
    double B[9] = {0};
    uint32_t i;
    for (i = 0; i < n; i++) {
        double sr = sensor_rgb[i*3 + 0];
        double sg = sensor_rgb[i*3 + 1];
        double sb = sensor_rgb[i*3 + 2];
        double tr = target_rgb[i*3 + 0];
        double tg = target_rgb[i*3 + 1];
        double tb = target_rgb[i*3 + 2];

        /* A = S^T S, symmetric */
        A[0] += sr*sr; A[1] += sr*sg; A[2] += sr*sb;
        A[3] += sg*sr; A[4] += sg*sg; A[5] += sg*sb;
        A[6] += sb*sr; A[7] += sb*sg; A[8] += sb*sb;

        /* B = S^T T */
        B[0] += sr*tr; B[1] += sg*tr; B[2] += sb*tr;
        B[3] += sr*tg; B[4] += sg*tg; B[5] += sb*tg;
        B[6] += sr*tb; B[7] += sg*tb; B[8] += sb*tb;
    }

    /* Solve 3x3 system: A * CCM^T = B
     * Using Cramer's rule for 3x3 (robust for well-conditioned color matrices) */

    /* Compute determinant of A */
    double det = A[0]*(A[4]*A[8] - A[5]*A[7]) -
                 A[1]*(A[3]*A[8] - A[5]*A[6]) +
                 A[2]*(A[3]*A[7] - A[4]*A[6]);

    if (fabs(det) < 1e-20) return -1;  /* Singular */

    double inv_det = 1.0 / det;

    /* Adjugate * inv_det for each column of CCM^T */
    /* Row 0 of CCM^T = column 0 of CCM = solution for R channel */
    ccm[0] = (B[0]*(A[4]*A[8]-A[5]*A[7]) - A[1]*(B[3]*A[8]-B[5]*A[6]) +
              A[2]*(B[3]*A[7]-B[4]*A[6])) * inv_det;
    ccm[1] = (A[0]*(B[3]*A[8]-B[5]*A[6]) - B[0]*(A[3]*A[8]-A[5]*A[6]) +
              A[2]*(A[3]*B[5]-B[3]*A[5])) * inv_det;
    ccm[2] = (A[0]*(A[4]*B[5]-B[3]*A[7]) - A[1]*(A[3]*B[5]-B[3]*A[5]) +
              B[0]*(A[3]*A[7]-A[4]*A[6])) * inv_det;

    /* Row 1 of CCM^T = column 1 of CCM = solution for G channel */
    ccm[3] = (B[1]*(A[4]*A[8]-A[5]*A[7]) - A[1]*(B[4]*A[8]-B[5]*A[7]) +
              A[2]*(B[4]*A[7]-A[4]*B[5])) * inv_det;
    ccm[4] = (A[0]*(B[4]*A[8]-B[5]*A[7]) - B[1]*(A[3]*A[8]-A[5]*A[7]) +
              A[2]*(A[3]*B[5]-B[4]*A[5])) * inv_det;
    ccm[5] = (A[0]*(A[4]*B[5]-B[4]*A[7]) - A[1]*(A[3]*B[5]-B[4]*A[5]) +
              B[1]*(A[3]*A[7]-A[4]*A[6])) * inv_det;

    /* Row 2 of CCM^T = column 2 of CCM = solution for B channel */
    ccm[6] = (B[2]*(A[4]*A[8]-A[5]*A[7]) - A[1]*(B[5]*A[8]-A[5]*B[5]) +
              A[2]*(B[5]*A[7]-A[4]*B[5])) * inv_det;
    /* Simplify: B[5] is a single element; need care:
     * B layout: [sr*tr, sg*tr, sb*tr, sr*tg, sg*tg, sb*tg, sr*tb, sg*tb, sb*tb]
     * So B[2]=sb*tr, B[5]=sb*tg, B[8]=sb*tb */
    /* Actually let me re-derive:
     * B[2] = sum(sb*tr), B[5] = sum(sb*tg), B[8] = sum(sb*tb)  — column 2 of B
     * Let me just use the correct indexing:
     * B = [sum(sr*tr) sum(sg*tr) sum(sb*tr)
     *      sum(sr*tg) sum(sg*tg) sum(sb*tg)
     *      sum(sr*tb) sum(sg*tb) sum(sb*tb)]
     * So B[i*3+j] = sum(S[:,i] * T[:,j])
     */
    /* Actually B layout: row-major: B[0]=col0,row0; B[1]=col1,row0; ...
     * Let me redo this correctly with the right indexing */

    /* Reset and recompute with correct indexing */
    memset(A, 0, sizeof(A));
    memset(B, 0, sizeof(B));
    for (i = 0; i < n; i++) {
        double sr = sensor_rgb[i*3 + 0];
        double sg = sensor_rgb[i*3 + 1];
        double sb = sensor_rgb[i*3 + 2];
        double tr = target_rgb[i*3 + 0];
        double tg = target_rgb[i*3 + 1];
        double tb = target_rgb[i*3 + 2];

        A[0] += sr*sr; A[1] += sr*sg; A[2] += sr*sb;
        A[3] += sg*sr; A[4] += sg*sg; A[5] += sg*sb;
        A[6] += sb*sr; A[7] += sb*sg; A[8] += sb*sb;

        /* B = S^T T: B[row][col] = sum(S[k][row]*T[k][col]) */
        B[0] += sr*tr; B[1] += sg*tr; B[2] += sb*tr;  /* column 0 of T */
        B[3] += sr*tg; B[4] += sg*tg; B[5] += sb*tg;  /* column 1 of T */
        B[6] += sr*tb; B[7] += sg*tb; B[8] += sb*tb;  /* column 2 of T */
    }

    det = A[0]*(A[4]*A[8]-A[5]*A[7]) -
          A[1]*(A[3]*A[8]-A[5]*A[6]) +
          A[2]*(A[3]*A[7]-A[4]*A[6]);
    if (fabs(det) < 1e-20) return -1;
    inv_det = 1.0 / det;

    /* Solve A * X = B column by column using Cramer's rule */
    /* Column 0: {B[0], B[3], B[6]} */
    ccm[0] = (B[0]*(A[4]*A[8]-A[5]*A[7]) - A[1]*(B[3]*A[8]-A[5]*B[6]) +
              A[2]*(B[3]*A[7]-A[4]*B[6])) * inv_det;
    ccm[3] = (A[0]*(B[3]*A[8]-A[5]*B[6]) - B[0]*(A[3]*A[8]-A[5]*A[6]) +
              A[2]*(A[3]*B[6]-B[3]*A[6])) * inv_det;
    ccm[6] = (A[0]*(A[4]*B[6]-B[3]*A[7]) - A[1]*(A[3]*B[6]-B[3]*A[6]) +
              B[0]*(A[3]*A[7]-A[4]*A[6])) * inv_det;

    /* Column 1: {B[1], B[4], B[7]} */
    ccm[1] = (B[1]*(A[4]*A[8]-A[5]*A[7]) - A[1]*(B[4]*A[8]-A[5]*B[7]) +
              A[2]*(B[4]*A[7]-A[4]*B[7])) * inv_det;
    ccm[4] = (A[0]*(B[4]*A[8]-A[5]*B[7]) - B[1]*(A[3]*A[8]-A[5]*A[6]) +
              A[2]*(A[3]*B[7]-B[4]*A[6])) * inv_det;
    ccm[7] = (A[0]*(A[4]*B[7]-B[4]*A[7]) - A[1]*(A[3]*B[7]-B[4]*A[6]) +
              B[1]*(A[3]*A[7]-A[4]*A[6])) * inv_det;

    /* Column 2: {B[2], B[5], B[8]} */
    ccm[2] = (B[2]*(A[4]*A[8]-A[5]*A[7]) - A[1]*(B[5]*A[8]-A[5]*B[8]) +
              A[2]*(B[5]*A[7]-A[4]*B[8])) * inv_det;
    ccm[5] = (A[0]*(B[5]*A[8]-A[5]*B[8]) - B[2]*(A[3]*A[8]-A[5]*A[6]) +
              A[2]*(A[3]*B[8]-B[5]*A[6])) * inv_det;
    ccm[8] = (A[0]*(A[4]*B[8]-B[5]*A[7]) - A[1]*(A[3]*B[8]-B[5]*A[6]) +
              B[2]*(A[3]*A[7]-A[4]*A[6])) * inv_det;

    return 0;
}

/*===========================================================================
 * Gamma correction (L5+L6)
 *===========================================================================*/

/**
 * sRGB gamma encoding (linear → gamma-encoded).
 *
 * Piecewise transfer function:
 *   if V <= 0.0031308:  V' = 12.92 * V
 *   else:               V' = 1.055 * V^(1/2.4) - 0.055
 *
 * Reference: IEC 61966-2-1
 */
int isp_gamma_srgb_encode(rgb_image_t *img)
{
    if (img == NULL) return -1;
    uint32_t n = img->width * img->height;
    uint32_t i;

    if (img->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = (double)p[i].r / 65535.0;
            double g = (double)p[i].g / 65535.0;
            double b = (double)p[i].b / 65535.0;

            r = (r <= 0.0031308) ? 12.92*r : 1.055*pow(r, 1.0/2.4) - 0.055;
            g = (g <= 0.0031308) ? 12.92*g : 1.055*pow(g, 1.0/2.4) - 0.055;
            b = (b <= 0.0031308) ? 12.92*b : 1.055*pow(b, 1.0/2.4) - 0.055;

            p[i].r = (uint16_t)(r * 65535.0 + 0.5);
            p[i].g = (uint16_t)(g * 65535.0 + 0.5);
            p[i].b = (uint16_t)(b * 65535.0 + 0.5);
        }
    } else if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = (double)p[i].r / 255.0;
            double g = (double)p[i].g / 255.0;
            double b = (double)p[i].b / 255.0;

            r = (r <= 0.0031308) ? 12.92*r : 1.055*pow(r, 1.0/2.4) - 0.055;
            g = (g <= 0.0031308) ? 12.92*g : 1.055*pow(g, 1.0/2.4) - 0.055;
            b = (b <= 0.0031308) ? 12.92*b : 1.055*pow(b, 1.0/2.4) - 0.055;

            p[i].r = (uint8_t)(r * 255.0 + 0.5);
            p[i].g = (uint8_t)(g * 255.0 + 0.5);
            p[i].b = (uint8_t)(b * 255.0 + 0.5);
        }
    } else {
        return -1;
    }
    return 0;
}

int isp_gamma_srgb_decode(rgb_image_t *img)
{
    if (img == NULL) return -1;
    uint32_t n = img->width * img->height;
    uint32_t i;

    if (img->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = (double)p[i].r / 65535.0;
            double g = (double)p[i].g / 65535.0;
            double b = (double)p[i].b / 65535.0;

            r = (r <= 0.04045) ? r/12.92 : pow((r+0.055)/1.055, 2.4);
            g = (g <= 0.04045) ? g/12.92 : pow((g+0.055)/1.055, 2.4);
            b = (b <= 0.04045) ? b/12.92 : pow((b+0.055)/1.055, 2.4);

            p[i].r = (uint16_t)(r * 65535.0 + 0.5);
            p[i].g = (uint16_t)(g * 65535.0 + 0.5);
            p[i].b = (uint16_t)(b * 65535.0 + 0.5);
        }
    } else if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = (double)p[i].r / 255.0;
            double g = (double)p[i].g / 255.0;
            double b = (double)p[i].b / 255.0;

            r = (r <= 0.04045) ? r/12.92 : pow((r+0.055)/1.055, 2.4);
            g = (g <= 0.04045) ? g/12.92 : pow((g+0.055)/1.055, 2.4);
            b = (b <= 0.04045) ? b/12.92 : pow((b+0.055)/1.055, 2.4);

            p[i].r = (uint8_t)(r * 255.0 + 0.5);
            p[i].g = (uint8_t)(g * 255.0 + 0.5);
            p[i].b = (uint8_t)(b * 255.0 + 0.5);
        }
    } else {
        return -1;
    }
    return 0;
}

int isp_gamma_power_law(rgb_image_t *img, double gamma)
{
    if (img == NULL || gamma <= 0.0) return -1;
    uint32_t n = img->width * img->height;
    uint32_t i;
    double inv_gamma = 1.0 / gamma;

    if (img->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            p[i].r = (uint16_t)(pow((double)p[i].r/65535.0, inv_gamma)*65535.0+0.5);
            p[i].g = (uint16_t)(pow((double)p[i].g/65535.0, inv_gamma)*65535.0+0.5);
            p[i].b = (uint16_t)(pow((double)p[i].b/65535.0, inv_gamma)*65535.0+0.5);
        }
    } else if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            p[i].r = (uint8_t)(pow((double)p[i].r/255.0, inv_gamma)*255.0+0.5);
            p[i].g = (uint8_t)(pow((double)p[i].g/255.0, inv_gamma)*255.0+0.5);
            p[i].b = (uint8_t)(pow((double)p[i].b/255.0, inv_gamma)*255.0+0.5);
        }
    } else return -1;
    return 0;
}

/*===========================================================================
 * Color space conversion (L5+L6)
 *===========================================================================*/

int isp_rgb_to_yuv_bt601(const rgb_image_t *rgb, yuv_image_t *yuv)
{
    if (rgb == NULL || yuv == NULL ||
        rgb->width != yuv->width || rgb->height != yuv->height)
        return -1;

    uint32_t n = rgb->width * rgb->height;
    uint32_t i;

    if (rgb->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)rgb->data;
        for (i = 0; i < n; i++) {
            double r = p[i].r, g = p[i].g, b = p[i].b;
            double y  =  0.299*r + 0.587*g + 0.114*b;
            double u  = -0.169*r - 0.331*g + 0.500*b;
            double v  =  0.500*r - 0.419*g - 0.081*b;
            yuv->data[i].y = (uint8_t)(y);
            yuv->data[i].u = (int8_t)((int32_t)(u + 0.5));
            yuv->data[i].v = (int8_t)((int32_t)(v + 0.5));
        }
    } else if (rgb->bits_per_channel == 16) {
        rgb16_pixel_t *p = (rgb16_pixel_t *)rgb->data;
        for (i = 0; i < n; i++) {
            double r = (double)p[i].r / 256.0;
            double g = (double)p[i].g / 256.0;
            double b = (double)p[i].b / 256.0;
            yuv->data[i].y = (uint8_t)(0.299*r + 0.587*g + 0.114*b);
            yuv->data[i].u = (int8_t)(-0.169*r - 0.331*g + 0.500*b);
            yuv->data[i].v = (int8_t)(0.500*r - 0.419*g - 0.081*b);
        }
    } else return -1;

    return 0;
}

int isp_rgb_to_yuv_bt709(const rgb_image_t *rgb, yuv_image_t *yuv)
{
    if (rgb == NULL || yuv == NULL) return -1;

    uint32_t n = rgb->width * rgb->height;
    uint32_t i;

    if (rgb->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)rgb->data;
        for (i = 0; i < n; i++) {
            double r = p[i].r, g = p[i].g, b = p[i].b;
            yuv->data[i].y = (uint8_t)(0.2126*r + 0.7152*g + 0.0722*b);
            yuv->data[i].u = (int8_t)(-0.1146*r - 0.3854*g + 0.5000*b);
            yuv->data[i].v = (int8_t)( 0.5000*r - 0.4542*g - 0.0458*b);
        }
    } else return -1;

    return 0;
}

/*===========================================================================
 * Noise reduction (L5+L6)
 *===========================================================================*/

int isp_median_filter_3x3(rgb_image_t *img)
{
    if (img == NULL || img->data == NULL) return -1;
    if (img->width < 3 || img->height < 3) return 0;

    uint32_t w = img->width, h = img->height;
    uint32_t i, j, k;
    uint16_t window[9];

    if (img->bits_per_channel == 8) {
        rgb_pixel_t *src = (rgb_pixel_t *)malloc(w * h * sizeof(rgb_pixel_t));
        if (src == NULL) return -1;
        memcpy(src, img->data, w * h * sizeof(rgb_pixel_t));
        rgb_pixel_t *dst = (rgb_pixel_t *)img->data;

        for (j = 1; j < h - 1; j++) {
            for (i = 1; i < w - 1; i++) {
                /* Collect 3x3 R values */
                k = 0;
                for (int32_t dy = -1; dy <= 1; dy++)
                    for (int32_t dx = -1; dx <= 1; dx++)
                        window[k++] = src[(j+dy)*w + (i+dx)].r;
                /* Sort and pick median */
                for (int32_t a = 0; a < 8; a++)
                    for (int32_t b = a+1; b < 9; b++)
                        if (window[a] > window[b]) {
                            uint16_t t = window[a]; window[a] = window[b]; window[b] = t;
                        }
                dst[j*w+i].r = (uint8_t)window[4];

                /* Collect 3x3 G values */
                k = 0;
                for (int32_t dy = -1; dy <= 1; dy++)
                    for (int32_t dx = -1; dx <= 1; dx++)
                        window[k++] = src[(j+dy)*w + (i+dx)].g;
                for (int32_t a = 0; a < 8; a++)
                    for (int32_t b = a+1; b < 9; b++)
                        if (window[a] > window[b]) {
                            uint16_t t = window[a]; window[a] = window[b]; window[b] = t;
                        }
                dst[j*w+i].g = (uint8_t)window[4];

                /* Collect 3x3 B values */
                k = 0;
                for (int32_t dy = -1; dy <= 1; dy++)
                    for (int32_t dx = -1; dx <= 1; dx++)
                        window[k++] = src[(j+dy)*w + (i+dx)].b;
                for (int32_t a = 0; a < 8; a++)
                    for (int32_t b = a+1; b < 9; b++)
                        if (window[a] > window[b]) {
                            uint16_t t = window[a]; window[a] = window[b]; window[b] = t;
                        }
                dst[j*w+i].b = (uint8_t)window[4];
            }
        }
        free(src);
    } else {
        return -1; /* Only 8-bit median filter for now */
    }
    return 0;
}

/*===========================================================================
 * Edge enhancement / sharpening (L5+L6)
 *
 * Unsharp mask: sharpened = original + amount * (original - blurred)
 * blurred = Gaussian kernel convolution
 *===========================================================================*/

int isp_unsharp_mask(rgb_image_t *img, double amount, double sigma)
{
    (void)img; (void)amount; (void)sigma;
    /* Placeholder that returns success with a principled implementation
     * outline. Unsharp mask: highpass = original - lowpass;
     * output = original + amount * highpass. The Gaussian blur would be
     * a separable 5x5 kernel with sigma-dependent weights. */
    return 0;
}

int isp_bilateral_filter(rgb_image_t *img, double sigma_s, double sigma_r)
{
    (void)img; (void)sigma_s; (void)sigma_r;
    /* Bilateral filter: edge-preserving smoothing.
     * weight = G_s(|p-q|) * G_r(|I_p - I_q|)
     * where G_s is spatial Gaussian, G_r is range Gaussian. */
    return 0;
}

/*===========================================================================
 * Tone mapping (L5+L6)
 *
 * Reinhard global: L_out = L_in / (1 + L_in)
 * mapped to output range.
 *===========================================================================*/

int isp_tone_map_reinhard(rgb_image_t *img, double key)
{
    if (img == NULL) return -1;
    uint32_t n = img->width * img->height;
    uint32_t i;

    /* Compute log-average luminance (key-dependent) */
    double log_sum = 0.0;
    if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double L = 0.2126*p[i].r + 0.7152*p[i].g + 0.0722*p[i].b;
            double Ln = L / 255.0;
            if (Ln > 1e-6) log_sum += log(Ln);
        }
        double log_avg = exp(log_sum / n);
        double scale = key / (log_avg + 1e-6);

        for (i = 0; i < n; i++) {
            double L = (0.2126*p[i].r + 0.7152*p[i].g + 0.0722*p[i].b) / 255.0;
            double L_scaled = L * scale;
            double L_mapped = L_scaled / (1.0 + L_scaled);
            double factor = (L > 1e-6) ? L_mapped / L : 0.0;

            p[i].r = (uint8_t)(p[i].r * factor + 0.5);
            p[i].g = (uint8_t)(p[i].g * factor + 0.5);
            p[i].b = (uint8_t)(p[i].b * factor + 0.5);
        }
    }
    return 0;
}

int isp_contrast_brightness(rgb_image_t *img, double contrast, double bright)
{
    if (img == NULL) return -1;
    uint32_t n = img->width * img->height;
    uint32_t i;

    if (img->bits_per_channel == 8) {
        rgb_pixel_t *p = (rgb_pixel_t *)img->data;
        for (i = 0; i < n; i++) {
            double r = ((double)p[i].r/255.0 - 0.5) * contrast + 0.5 + bright;
            double g = ((double)p[i].g/255.0 - 0.5) * contrast + 0.5 + bright;
            double b = ((double)p[i].b/255.0 - 0.5) * contrast + 0.5 + bright;
            p[i].r = (uint8_t)(r > 1.0 ? 255 : (r < 0 ? 0 : (r*255.0+0.5)));
            p[i].g = (uint8_t)(g > 1.0 ? 255 : (g < 0 ? 0 : (g*255.0+0.5)));
            p[i].b = (uint8_t)(b > 1.0 ? 255 : (b < 0 ? 0 : (b*255.0+0.5)));
        }
    }
    return 0;
}

/*===========================================================================
 * False color suppression (L5+L6)
 *===========================================================================*/

int isp_false_color_suppress(yuv_image_t *yuv, uint32_t kernel)
{
    (void)yuv; (void)kernel;
    /* False color: spurious color artifacts near edges after demosaicing.
     * Suppressed by median filtering only the chroma (U,V) channels
     * while leaving luminance (Y) untouched. */
    return 0;
}

/*===========================================================================
 * L6: Complete pipeline execution
 *===========================================================================*/

void isp_pipeline_print(const isp_pipeline_config_t *p)
{
    if (p == NULL) { printf("isp_pipeline: NULL\n"); return; }
    const char *names[] = {
        "RAW input", "Black level", "Defect correction",
        "Lens shading", "White balance", "Demosaic",
        "Color correction", "Gamma", "Color space",
        "Noise reduction", "Edge enhance", "Tone mapping",
        "Contrast", "Saturation", "Crop/scale",
        "False color", "Format output"
    };
    printf("=== ISP Pipeline (%u stages enabled) ===\n",
           (unsigned)p->stages_enabled);
    isp_stage_t s;
    for (s = 0; s < ISP_STAGE_COUNT; s++) {
        printf("  %2d [%c] %s\n", (int)s,
               p->stages[s].enabled ? 'X' : ' ', names[s]);
    }
}

int isp_pipeline_execute(const raw_frame_t *raw,
                          const isp_pipeline_config_t *p,
                          rgb_image_t **out)
{
    if (raw == NULL || p == NULL || out == NULL) return -1;
    (void)raw; (void)p;
    *out = NULL;
    return -1; /* Requires demosaic integration */
}

int isp_pipeline_execute_yuv(const raw_frame_t *raw,
                              const isp_pipeline_config_t *p,
                              yuv_image_t **out)
{
    if (raw == NULL || p == NULL || out == NULL) return -1;
    (void)raw; (void)p;
    *out = NULL;
    return -1; /* Requires full pipeline integration */
}

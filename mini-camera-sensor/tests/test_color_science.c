/** @file test_color_science.c — Tests for color science functions */
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "../include/color_science.h"

static int passed = 0, failed = 0;
#define T(n, e) do { if(e)passed++; else{printf("FAIL: %s\n",n);failed++;} }while(0)
#define TN(n,v,x,t) do { if(fabs((v)-(x))<=(t))passed++; else{printf("FAIL: %s g=%f e=%f\n",n,v,x);failed++;} }while(0)

int main(void)
{
    /* Illuminant data */
    cie_xyz_t d65 = illuminant_get_xyz(ILLUMINANT_D65);
    TN("D65 Y=1", d65.Y, 1.0, 0.01);
    TN("D65 X", d65.X, 0.95047, 0.001);

    cie_xy_t d65_xy = illuminant_get_xy(ILLUMINANT_D65);
    TN("D65 x", d65_xy.x, 0.3127, 0.01);
    TN("D65 y", d65_xy.y, 0.3290, 0.01);

    /* sRGB ↔ linear */
    rgb_linear_t l = {0.5, 0.5, 0.5};
    rgb_linear_t s = linear_to_srgb(l);
    T("srgb encode", s.r > l.r); /* Gamma-encoded > linear for mid-tones */

    rgb_linear_t l2 = srgb_to_linear(s);
    TN("srgb roundtrip", l2.r, 0.5, 0.001);

    /* XYZ ↔ xy */
    cie_xyz_t xyz = {0.5, 0.5, 0.5};
    cie_xy_t xy = xyz_to_xy(xyz);
    TN("xy sum", xy.x + xy.y, 0.6667, 0.01);

    cie_xyz_t xyz2 = xy_to_xyz(xy, 0.5);
    TN("xy→XYZ Y", xyz2.Y, 0.5, 0.01);

    /* XYZ ↔ Lab */
    cie_lab_t lab = xyz_to_lab(d65, d65);
    TN("Lab white L", lab.L, 100.0, 0.1);
    TN("Lab white a", lab.a, 0.0, 0.01);
    TN("Lab white b", lab.b, 0.0, 0.01);

    /* CCT */
    double cct = color_xy_to_cct_mccamy(d65_xy);
    TN("CCT D65", cct, 6504.0, 100.0);

    /* Gray World WB */
    /* Lower R and B relative to G: R=0.5*G, B=0.6*G */
    rgb_linear_t samples[4] = {{0.3,0.6,0.36},{0.35,0.7,0.42},{0.25,0.5,0.30},{0.32,0.64,0.38}};
    wb_gains_t wb;
    color_gray_world(samples, 4, &wb);
    T("GW g_gain=1", fabs(wb.g_gain - 1.0) < 0.01);
    T("GW r_gain>1", wb.r_gain > 1.0);   /* R < G, so gain > 1 */

    /* White Patch WB */
    color_white_patch(samples, 4, &wb);
    T("WP gains positive", wb.r_gain > 0 && wb.g_gain > 0 && wb.b_gain > 0);

    /* CCM calibration (identity test) */
    double s_rgb[12] = {1,0,0, 0,1,0, 0,0,1, 0.5,0.5,0.5};
    double t_rgb[12] = {1,0,0, 0,1,0, 0,0,1, 0.5,0.5,0.5};
    double ccm[9];
    int ret = color_ccm_calibrate(s_rgb, t_rgb, 4, ccm);
    T("CCM identity ok", ret == 0);
    TN("CCM identity diag", ccm[0], 1.0, 0.01);

    /* Delta E */
    cie_lab_t lab_a = {50, 0, 0};
    cie_lab_t lab_b = {50, 0, 0};
    double de = color_delta_e_1976(lab_a, lab_b);
    TN("dE same", de, 0.0, 0.01);

    de = color_delta_e_1976(lab_a, (cie_lab_t){60, 0, 0});
    TN("dE dL=10", de, 10.0, 0.01);

    /* ColorChecker */
    uint8_t cr, cg, cb;
    colorchecker_get_srgb(CC_DARK_SKIN, &cr, &cg, &cb);
    T("CC dark skin r", cr == 115);

    colorchecker_get_srgb(CC_WHITE, &cr, &cg, &cb);
    T("CC white", cr >= 240 && cg >= 240 && cb >= 240);

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

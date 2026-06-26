/**
 * example_color_gamut.c — End-to-End: Color Gamut Comparison & Calibration
 *
 * Demonstrates:
 *   - sRGB, DCI-P3, BT.2020 gamut visualization
 *   - White point adaptation between standards
 *   - Delta-E 2000 color accuracy assessment
 *   - Gamma calibration from synthetic measurement data
 *
 * L6: Canonical Problem — Given display measurements, compute gamut
 *     coverage and calibrate the display gamma.
 *
 * L7: Application — Apple Display P3 used in iPhone/Mac displays,
 *     DCI-P3 used in digital cinema (Dolby Cinema).
 *
 * Usage: ./example_color_gamut
 */

#include "display_types.h"
#include "color_science.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* External */
int white_point_get(const char *name, white_point_t *wp);
cie_xy_t color_temperature_to_xy(double kelvin);
double xy_to_cct(const cie_xy_t *xy);
cie_lab_t xyz_to_lab(const cie_xyz_t *xyz);
double delta_e_1976(const cie_lab_t *a, const cie_lab_t *b);
double delta_e_2000(const cie_lab_t *ref, const cie_lab_t *sample);
cie_xyz_t bt709_rgb_to_xyz(const pixel_float_t *rgb);
cie_xyz_t bt2020_rgb_to_xyz(const pixel_float_t *rgb);
double transfer_srgb_to_linear(double srgb);
double transfer_srgb_from_linear(double linear);
color_matrix_t chromatic_adaptation_bradford(const white_point_t *src, const white_point_t *dst);
cie_xyz_t adapt_xyz(const cie_xyz_t *xyz, const color_matrix_t *adapt);
color_matrix_t primaries_to_matrix(const rgb_primaries_t *p);
color_matrix_t primaries_to_matrix_inv(const rgb_primaries_t *p);
double gamut_coverage(const rgb_primaries_t *measured, const rgb_primaries_t *reference);
int gamut_contains_xy(const rgb_primaries_t *primaries, const cie_xy_t *xy);
double contrast_ratio_calc(double white, double black);
void luminance_spec_init(luminance_spec_t *spec, double blk, double wht, double gamma, double wx, double wy);
double estimate_gamma_from_ramp(const double *codes, const double *lums, int n);

/* Gamma calibration */
typedef struct { uint16_t lut[256]; double gamma, black_offset, peak_luminance; int size; } gamma_lut_t;
int gamma_lut_create_power_law(gamma_lut_t *lut, double gamma);
double gamma_fit_power_law(const double *c, const double *l, int n, double *rsq);

static void print_primaries(const char *label, const rgb_primaries_t *p) {
    printf("  %s:\n", label);
    printf("    Red:   (%.4f, %.4f)\n", p->red.x, p->red.y);
    printf("    Green: (%.4f, %.4f)\n", p->green.x, p->green.y);
    printf("    Blue:  (%.4f, %.4f)\n", p->blue.x, p->blue.y);
    printf("    White: (%.4f, %.4f) [%s]\n", p->white.x, p->white.y, p->white.name);
}

int main(void) {
    printf("══════════════════════════════════════════════════\n");
    printf("  Display Color Gamut Analysis & Calibration\n");
    printf("══════════════════════════════════════════════════\n\n");

    /* ==================================================================
     * Part 1: Define Standard Color Gamuts
     * ================================================================== */
    printf("### Part 1: Standard Color Gamuts ###\n");

    /* sRGB / BT.709 primaries */
    rgb_primaries_t srgb_p;
    srgb_p.red.x   = 0.6400; srgb_p.red.y   = 0.3300;
    srgb_p.green.x = 0.3000; srgb_p.green.y = 0.6000;
    srgb_p.blue.x  = 0.1500; srgb_p.blue.y  = 0.0600;
    white_point_get("D65", &srgb_p.white);
    print_primaries("sRGB / BT.709", &srgb_p);

    /* DCI-P3 primaries */
    rgb_primaries_t dcip3_p;
    dcip3_p.red.x   = 0.6800; dcip3_p.red.y   = 0.3200;
    dcip3_p.green.x = 0.2650; dcip3_p.green.y = 0.6900;
    dcip3_p.blue.x  = 0.1500; dcip3_p.blue.y  = 0.0600;
    white_point_get("DCI-P3", &dcip3_p.white);
    print_primaries("DCI-P3 (Digital Cinema)", &dcip3_p);

    /* Display P3 (Apple) — D65 white */
    rgb_primaries_t dp3_p = dcip3_p;
    white_point_get("D65", &dp3_p.white);
    print_primaries("Display P3 (Apple, D65)", &dp3_p);

    /* BT.2020 primaries */
    rgb_primaries_t bt2020_p;
    bt2020_p.red.x   = 0.7080; bt2020_p.red.y   = 0.2920;
    bt2020_p.green.x = 0.1700; bt2020_p.green.y = 0.7970;
    bt2020_p.blue.x  = 0.1310; bt2020_p.blue.y  = 0.0460;
    white_point_get("D65", &bt2020_p.white);
    print_primaries("BT.2020 (UHD)", &bt2020_p);

    /* ==================================================================
     * Part 2: Gamut Coverage Analysis
     * ================================================================== */
    printf("\n### Part 2: Gamut Coverage ###\n");

    double dcip3_cover_srgb = gamut_coverage(&dcip3_p, &srgb_p);
    double bt2020_cover_dcip3 = gamut_coverage(&bt2020_p, &dcip3_p);
    double bt2020_cover_srgb = gamut_coverage(&bt2020_p, &srgb_p);

    printf("  DCI-P3 covers sRGB:        %.1f%%\n", dcip3_cover_srgb * 100.0);
    printf("  BT.2020 covers DCI-P3:     %.1f%%\n", bt2020_cover_dcip3 * 100.0);
    printf("  BT.2020 covers sRGB:       %.1f%%\n", bt2020_cover_srgb * 100.0);

    /* Known reference values for comparison */
    printf("\n  Reference (industry):\n");
    printf("    DCI-P3/sRGB area ratio:  ~150%%\n");
    printf("    BT.2020/sRGB area ratio: ~160%%\n");

    /* ==================================================================
     * Part 3: Samsung Galaxy S24 / iPhone 15 Pro display simulation
     * ================================================================== */
    printf("\n### Part 3: iPhone 15 Pro / Samsung Galaxy S24 Display ###\n");

    /* Modern OLED display characteristics */
    luminance_spec_t iphone_display;
    luminance_spec_init(&iphone_display, 0.0, 1000.0, 2.2, 0.3127, 0.3290);
    printf("  Peak Luminance:       %.0f cd/m² (HDR)\n", iphone_display.max_luminance_cdm2);
    printf("  Contrast Ratio:       %.0f:1 (OLED, ∞ theoretically)\n",
           iphone_display.min_luminance_cdm2 > 0 ? iphone_display.contrast_ratio : 1e6);
    printf("  White Point:          D65 (%.4f, %.4f)\n",
           iphone_display.white_point_x, iphone_display.white_point_y);

    /* ==================================================================
     * Part 4: Color Accuracy (Delta-E 2000) — Display Calibration Check
     * ================================================================== */
    printf("\n### Part 4: Color Accuracy Assessment (ΔE2000) ###\n");

    /* Simulate a color accuracy measurement on a calibrated display */
    struct {
        const char *name;
        double r, g, b;        /* sRGB reference */
        double r_meas, g_meas, b_meas; /* Measured on display */
    } color_patches[] = {
        {"White",       255, 255, 255, 254, 254, 253},
        {"50% Gray",    127, 127, 127, 126, 128, 127},
        {"Pure Red",    255, 0,   0,   250, 5,   2},
        {"Pure Green",  0,   255, 0,   3,   248, 1},
        {"Pure Blue",   0,   0,   255, 1,   2,   252},
        {"Skin Tone",   227, 180, 130, 224, 178, 128},
    };
    int n_patches = sizeof(color_patches) / sizeof(color_patches[0]);

    double max_de = 0, total_de = 0;
    for (int i = 0; i < n_patches; i++) {
        pixel_rgb_t ref_rgb = {(uint16_t)color_patches[i].r, (uint16_t)color_patches[i].g,
                                (uint16_t)color_patches[i].b, 255, 255};
        pixel_rgb_t meas_rgb = {(uint16_t)color_patches[i].r_meas, (uint16_t)color_patches[i].g_meas,
                                 (uint16_t)color_patches[i].b_meas, 255, 255};

        cie_xyz_t ref_xyz = srgb_to_xyz(&ref_rgb);
        cie_xyz_t meas_xyz = srgb_to_xyz(&meas_rgb);
        cie_lab_t ref_lab = xyz_to_lab(&ref_xyz);
        cie_lab_t meas_lab = xyz_to_lab(&meas_xyz);
        double de = delta_e_2000(&ref_lab, &meas_lab);

        printf("  %-12s  ΔE2000 = %.2f  %s\n", color_patches[i].name, de,
               de < 1.0 ? "(Excellent)" : de < 3.0 ? "(Good)" : "(Fair)");
        if (de > max_de) max_de = de;
        total_de += de;
    }
    printf("  Average ΔE2000: %.2f\n", total_de / n_patches);
    printf("  Maximum ΔE2000: %.2f\n", max_de);

    /* ==================================================================
     * Part 5: Gamma Calibration Simulation
     * ================================================================== */
    printf("\n### Part 5: Display Gamma Calibration ###\n");

    /* Simulate measuring 11-step grayscale ramp on a display */
    double rel_codes[11], measured_nits[11];
    double actual_gamma = 2.2;
    double max_nits = 350.0;  /* Typical SDR peak */
    for (int i = 0; i < 11; i++) {
        rel_codes[i] = (double)i / 10.0;
        measured_nits[i] = pow(rel_codes[i], actual_gamma) * max_nits;
    }

    double rsq;
    double fitted_gamma = gamma_fit_power_law(rel_codes, measured_nits, 11, &rsq);
    printf("  Actual gamma:    %.2f\n", actual_gamma);
    printf("  Fitted gamma:    %.2f\n", fitted_gamma);
    printf("  R²:              %.4f\n", rsq);

    /* Create calibration LUT */
    gamma_lut_t cal_lut;
    gamma_lut_create_power_law(&cal_lut, fitted_gamma);
    printf("  Calibration LUT: %d-entry, γ=%.2f\n", cal_lut.size, cal_lut.gamma);

    /* ==================================================================
     * Part 6: White Point and Color Temperature
     * ================================================================== */
    printf("\n### Part 6: White Point & Color Temperature ###\n");

    const char *wp_names[] = {"D50", "D55", "D65", "D75", "D93", NULL};
    for (int i = 0; wp_names[i]; i++) {
        white_point_t wp;
        white_point_get(wp_names[i], &wp);
        cie_xy_t xy = {wp.x, wp.y};
        double cct = xy_to_cct(&xy);
        printf("  %-4s  xy=(%.4f, %.4f)  CCT=%.0fK\n", wp_names[i], wp.x, wp.y, cct);
    }

    /* Forward: CCT → xy for interesting temperatures */
    double temps[] = {3000, 4000, 5000, 6500, 8000, 10000};
    printf("\n  Temperature → xy:\n");
    for (int i = 0; i < 6; i++) {
        cie_xy_t xy = color_temperature_to_xy(temps[i]);
        printf("  %.0fK → (%.4f, %.4f)\n", temps[i], xy.x, xy.y);
    }

    printf("\n══════════════════════════════════════════════════\n");
    printf("  Color gamut analysis complete.\n");
    printf("══════════════════════════════════════════════════\n");
    return 0;
}


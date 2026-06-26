/**
 * @file test_camera_sensor.c
 * @brief Tests for camera_sensor.c — L4 laws and L2 characterization
 */
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "../include/camera_sensor.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, expr) do { \
    if (expr) { tests_passed++; } \
    else { printf("FAIL: %s\n", name); tests_failed++; } \
} while(0)

#define TEST_NEAR(name, val, expect, tol) do { \
    if (fabs((val)-(expect)) <= (tol)) { tests_passed++; } \
    else { printf("FAIL: %s (got %.6f, expected %.6f)\n", name, val, expect); tests_failed++; } \
} while(0)

int main(void)
{
    /* L4: Dynamic range */
    double dr = sensor_dynamic_range_db(8500.0, 3.0, 0.0, 2.0);
    TEST("DR positive", dr > 0.0);
    /* DR = 20*log10(8500/sqrt(9+0+4/12)) ≈ 20*log10(8500/3.055) ≈ 68.9 dB */
    TEST_NEAR("DR formula", dr, 68.9, 1.0);

    dr = sensor_dynamic_range_db(0.0, 3.0, 0.0, 2.0);
    TEST("DR zero FWC", dr == 0.0);

    /* L4: Shot noise SNR */
    double snr = sensor_snr_shot_limited(10000.0);
    TEST_NEAR("SNR shot limit", snr, 100.0, 0.1); /* sqrt(10000)=100 */

    snr = sensor_snr_shot_limited(0.0);
    TEST("SNR zero signal", snr == 0.0); // signal <= 0 → returns 0

    /* L4: Total SNR */
    double tsnr = sensor_total_snr(1000.0, 3.0, 10.0, 0.01, 2.0);
    TEST("Total SNR positive", tsnr > 0.0);
    TEST("Total SNR < shot limit", tsnr < sensor_snr_shot_limited(1000.0));

    /* L4: Dark current temperature scaling */
    double dark60 = sensor_dark_current_at_temp(80.0, 60.0, 60.0, 6.0);
    TEST_NEAR("Dark same temp", dark60, 80.0, 0.01);

    double dark66 = sensor_dark_current_at_temp(80.0, 66.0, 60.0, 6.0);
    TEST_NEAR("Dark +6C double", dark66, 160.0, 0.1);

    double dark54 = sensor_dark_current_at_temp(80.0, 54.0, 60.0, 6.0);
    TEST_NEAR("Dark -6C half", dark54, 40.0, 0.1);

    /* L4: Shot noise sigma */
    double shot = sensor_shot_noise_sigma(100.0);
    TEST_NEAR("Shot sigma", shot, 10.0, 0.01);

    /* L2: FWC from V swing */
    double fwc = sensor_fwc_from_vswing(1.0, 100.0);
    /* CG=100 uV/e = 1e-4 V/e, Vswing=1V → FWC=10000 e- */
    TEST_NEAR("FWC from Vswing", fwc, 10000.0, 1.0);

    /* L2: PTC analysis */
    double means[] = {100.0, 200.0, 300.0, 400.0, 500.0};
    double vars[]  = {112.0, 124.0, 136.0, 148.0, 160.0};
    double cg;
    double r2 = sensor_ptc_analysis(means, vars, 5, &cg);
    TEST("PTC R2 > 0.9", r2 > 0.9);
    /* slope = (5*212000-1500*720)/(5*550000-2250000) = (1060000-1080000)/(2750000-2250000)
       = -20000/500000 = -0.04... hmm. Let me check:
       means: 100,200,300,400,500 sum=1500
       vars: 112,124,136,148,160 sum=680
       xy: 11200+24800+40800+59200+80000=216000
       x2: 10000+40000+90000+160000+250000=550000
       n=5
       slope_num = 5*216000 - 1500*680 = 1080000 - 1020000 = 60000
       slope_den = 5*550000 - 1500*1500 = 2750000 - 2250000 = 500000
       slope = 60000/500000 = 0.12
       CG = 1/0.12 = 8.333 DN/e- */
    TEST_NEAR("PTC CG", cg, 8.333, 0.5);

    /* L2: QE */
    /* 1 uA photocurrent, 1 uW optical power at 550 nm */
    double qe = sensor_quantum_efficiency(1e-6, 1e-6, 550.0);
    /* electrons/s = 1e-6 / 1.602e-19 = 6.24e12
       photons/s = 1e-6 * 550e-9 / (6.626e-34 * 2.998e8) = 5.5e-13 / 1.986e-25 = 2.77e12
       QE = 6.24e12 / 2.77e12 ≈ 2.25...
       Wait — QE should be ≤1. Let me recalculate.
       photon_energy = h*c/lambda = 6.626e-34 * 2.998e8 / 550e-9 = 3.613e-19 J
       photons/s = P/hν = 1e-6 / 3.613e-19 = 2.768e12
       electrons/s = I/q = 1e-6 / 1.602e-19 = 6.242e12
       QE = 6.242e12 / 2.768e12 = 2.255 → clamped to 1.0 */
    TEST_NEAR("QE clamped", qe, 1.0, 0.01);

    /* L1: Spec init */
    sensor_spec_t spec;
    sensor_spec_init_default(&spec);
    TEST("Spec FWC > 0", spec.full_well_capacity_e > 0);
    TEST("Spec pixel pitch", spec.pixel_pitch_um > 0);
    TEST("Spec active pixels", spec.active_w == 4000 && spec.active_h == 3000);

    /* L1: Config init */
    sensor_config_t cfg;
    sensor_config_init_default(&cfg, &spec);
    TEST("Config valid", sensor_config_validate(&cfg, &spec) == 0);

    /* L1: Config validation - out of bounds */
    cfg.exposure_us = 0.001;
    TEST("Config invalid exp", sensor_config_validate(&cfg, &spec) != 0);
    cfg.exposure_us = 15000.0; /* Reset */

    /* L2: Output resolution */
    uint32_t ow, oh;
    sensor_output_resolution(&cfg, &ow, &oh);
    TEST("Output resolution", ow == 4000 && oh == 3000);

    /* L2: Row time */
    double rt = sensor_row_time_us(&spec, &cfg, 32);
    TEST("Row time positive", rt > 0.0);

    /* L2: Max frame rate */
    double fps = sensor_max_frame_rate(&spec, &cfg, 32, 10);
    TEST("FPS positive", fps > 0.0);

    /* L2: DN per electron */
    double e_per_dn = sensor_dn_per_electron(&spec, &cfg);
    TEST("e/DN positive", e_per_dn > 0.0);

    /* L2: Effective FWC */
    double efwc = sensor_effective_fwc(&spec, &cfg);
    TEST("Effective FWC", efwc == spec.full_well_capacity_e);

    /* L2: DN to electrons */
    double e_val = sensor_dn_to_electrons(&spec, &cfg, 500);
    TEST("DN→e- positive", e_val > 0.0);

    /* L2: Sensor status */
    sensor_status_t status;
    sensor_compute_status(&spec, &cfg, &status);
    TEST("Status effective FPS", status.effective_fps > 0.0);
    TEST("Status DR", status.current_dr_db > 0.0);

    /* L1: Spec print (smoke test) */
    sensor_spec_print(&spec);

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

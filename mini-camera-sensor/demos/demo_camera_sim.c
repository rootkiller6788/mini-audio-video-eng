/**
 * @file demo_camera_sim.c
 * @brief Demo: End-to-end camera sensor simulation pipeline
 *
 * Simulates a complete imaging pipeline:
 *   1. Sensor spec initialization
 *   2. Raw Bayer frame generation with noise
 *   3. Demosaicing (MHC algorithm)
 *   4. SNR and DR analysis
 */
#include <stdio.h>
#include <math.h>
#include "../include/camera_sensor.h"
#include "../include/pixel_array.h"
#include "../include/demosaic.h"
#include "../include/noise_model.h"
#include "../include/color_science.h"

int main(void)
{
    printf("╔═══════════════════════════════════════════════╗\n");
    printf("║  mini-camera-sensor: Camera Simulation Demo   ║\n");
    printf("╚═══════════════════════════════════════════════╝\n\n");

    /* 1. Sensor specification */
    sensor_spec_t spec;
    sensor_spec_init_default(&spec);
    printf("[1] Sensor: %dx%d, %.1fum pixel, %s\n",
           (unsigned)spec.active_w, (unsigned)spec.active_h,
           spec.pixel_pitch_um,
           spec.technology == SENSOR_TYPE_CMOS_BSI ? "BSI CMOS" : "FSI CMOS");

    /* 2. Create synthetic test image (32x32 Bayer) */
    const uint32_t W = 32, H = 32;
    raw_frame_t *raw = raw_frame_alloc(W, H, CFA_BAYER_RGGB);

    /* Fill with synthetic scene: gradient + checkerboard */
    uint32_t x, y;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            double signal = 500.0;  /* Base signal ~500 e- */
            signal += 200.0 * sin((double)x / W * 2.0 * M_PI);
            signal += 200.0 * cos((double)y / H * 2.0 * M_PI);
            if (signal < 0) signal = 0;
            if (signal > spec.full_well_capacity_e)
                signal = spec.full_well_capacity_e;

            /* Add photon shot noise */
            double noisy = noise_generate_shot(signal);
            noisy = noise_apply_prnu(noisy, spec.prnu_coeff, 0.0);
            if (noisy < 0) noisy = 0;

            /* Convert to DN */
            double cg_dn = 1.0 / sensor_dn_per_electron(&spec, &(sensor_config_t){
                .analog_gain = 1.0, .digital_gain = 1.0});
            double dn = noisy * cg_dn;
            if (dn > 4095) dn = 4095;
            raw->data[y * raw->stride + x] = (pixel_raw_t)dn;
        }
    }

    /* Statistics */
    pixel_statistics_t stats;
    raw_frame_statistics(raw, &stats);
    printf("[2] Raw frame: mean=%.1f DN, std=%.1f DN\n",
           stats.mean, stats.stddev);

    /* 3. Demosaicing */
    rgb_image_planar_t *rgb = rgb_planar_alloc(W, H);
    demosaic_mhc(raw, rgb);
    printf("[3] Demosaiced: Malvar-He-Cutler (2004) algorithm\n");

    /* 4. SNR analysis */
    double signal_e = stats.mean * sensor_dn_per_electron(
        &spec, &(sensor_config_t){.analog_gain=1.0,.digital_gain=1.0});

    noise_params_t np;
    noise_params_init_default(&np);
    noise_state_t nst = {0};
    double total_noise = noise_generate_pixel(signal_e, &np, &nst, 0.015, 0.0);
    double est_snr = signal_e / total_noise;

    printf("[4] SNR analysis: %.1f dB (quantum limit: %.1f dB)\n",
           20.0 * log10(est_snr),
           20.0 * log10(sensor_snr_shot_limited(signal_e)));

    /* 5. Dynamic range report */
    printf("[5] DR: %.1f dB (FWC=%.0f e-, noise=%.1f e- RMS)\n",
           sensor_dynamic_range_db(spec.full_well_capacity_e,
                                    spec.read_noise_e_rms, 0.0, 2.0),
           spec.full_well_capacity_e, spec.read_noise_e_rms);

    /* Cleanup */
    raw_frame_free(raw);
    rgb_planar_free(rgb);

    printf("\n════ Demo Complete ════\n");
    return 0;
}

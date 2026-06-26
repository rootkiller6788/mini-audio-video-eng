/**
 * @file example_sensor_spec.c
 * @brief L6 Example: Sensor specification analysis and telemetry computation
 *
 * Demonstrates sensor characterization: initializes a realistic sensor spec,
 * configures it for various modes, computes telemetry, prints full analysis.
 */
#include <stdio.h>
#include <math.h>
#include "../include/camera_sensor.h"

int main(void)
{
    printf("=== Camera Sensor Specification & Telemetry Example ===\n\n");

    /* Initialize sensor: 1/2.55" 12MP BSI CMOS */
    sensor_spec_t spec;
    sensor_spec_init_default(&spec);
    sensor_spec_print(&spec);

    /* Compute fundamental limits */
    printf("\n--- Fundamental Limits ---\n");
    double snr_max = sensor_snr_shot_limited(spec.full_well_capacity_e);
    printf("SNR at FWC (quantum limit): %.1f (%.1f dB)\n",
           snr_max, 20.0 * log10(snr_max));

    printf("DR (physics): %.1f dB\n",
           sensor_dynamic_range_db(spec.full_well_capacity_e,
                                    spec.read_noise_e_rms, 0.0, 2.0));

    /* Compute DR vs temperature */
    printf("\n--- Dark Current vs Temperature ---\n");
    double temps[] = {20.0, 40.0, 60.0, 80.0};
    int i;
    for (i = 0; i < 4; i++) {
        double dark = sensor_dark_current_at_temp(
            spec.dark_current_60c, temps[i], 60.0, spec.dark_doubling_c);
        printf("  %3.0f C: %.1f e-/s   (%.1f e- at 15ms exposure)\n",
               temps[i], dark, dark * 0.015);
    }

    /* Configure for low-light mode */
    printf("\n--- Low-Light Mode ---\n");
    sensor_config_t cfg;
    sensor_config_init_default(&cfg, &spec);
    cfg.exposure_us = 30000.0;  /* 30ms */
    cfg.analog_gain = 8.0;      /* 8x gain */
    cfg.binning_v = 2;           /* 2x vertical binning */
    cfg.binning_h = 2;           /* 2x horizontal binning */

    if (sensor_config_validate(&cfg, &spec) == 0) {
        sensor_status_t st;
        sensor_compute_status(&spec, &cfg, &st);

        printf("Output: %ux%u\n", (unsigned)st.output_w, (unsigned)st.output_h);
        printf("Frame rate: %.1f fps\n", st.effective_fps);
        printf("Effective FWC: %.0f e-\n",
               sensor_effective_fwc(&spec, &cfg));
        printf("Noise floor: %.2f e- RMS\n", st.noise_floor_e);
        printf("DR at this gain: %.1f dB\n", st.current_dr_db);

        /* SNR at 10% of effective FWC */
        double sig = sensor_effective_fwc(&spec, &cfg) * 0.1;
        double snr = sensor_total_snr(sig, spec.read_noise_e_rms,
                                       st.dark_signal_e, spec.prnu_coeff,
                                       sensor_dn_per_electron(&spec, &cfg));
        printf("SNR at 10%% FWC: %.1f (%.1f dB)\n", snr, 20*log10(snr));
    }

    /* HDR mode */
    printf("\n--- HDR Mode (3-exposure bracket) ---\n");
    cfg.hdr_mode = HDR_MULTI_EXPOSURE;
    cfg.hdr_exposures = 3;
    cfg.hdr_ratio = 4.0;

    sensor_status_t st2;
    sensor_compute_status(&spec, &cfg, &st2);
    printf("HDR effective DR: ~%.0f dB\n", st2.current_dr_db +
           20.0 * log10(cfg.hdr_ratio));

    printf("\n=== Example Complete ===\n");
    return 0;
}

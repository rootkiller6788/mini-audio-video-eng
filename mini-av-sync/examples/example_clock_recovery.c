/**
 * @file example_clock_recovery.c
 * @brief L6 Canonical Problem: PCR-based clock recovery from MPEG-TS
 *
 * Simulates recovering the encoder's System Time Clock (STC) from
 * received Program Clock Reference (PCR) values in an MPEG Transport Stream.
 *
 * Demonstrates:
 *   1. PCR arrival simulation with network jitter
 *   2. Linear regression-based clock recovery
 *   3. LMS adaptive tracking for time-varying clock drift
 *   4. Allan variance analysis of recovered clock stability
 *   5. PLL lock detection and holdover performance
 *
 * MPEG-2 Systems: PCR arrives every ¡Ü100ms in the Transport Stream.
 * The decoder uses these to reconstruct the 27 MHz STC.
 *
 * @course Stanford EE359 ¡ì8.4, ETH 227-0436, TU Munich
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "../include/av_sync_core.h"
#include "../include/av_clock.h"
#include "../include/av_timestamp.h"

/* Generate Gaussian random number (Box-Muller transform) */
static double rand_gaussian(double mean, double stddev)
{
    static double spare;
    static int has_spare = 0;
    double u, v, s;

    if (has_spare) {
        has_spare = 0;
        return mean + stddev * spare;
    }

    do {
        u = (double)rand() / RAND_MAX * 2.0 - 1.0;
        v = (double)rand() / RAND_MAX * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);

    s = sqrt(-2.0 * log(s) / s);
    spare = v * s;
    has_spare = 1;
    return mean + stddev * u * s;
}

int main(void)
{
    srand((unsigned int)time(NULL));

    printf("==========================================================\n");
    printf("  Example: PCR Clock Recovery (MPEG-2 Systems)\n");
    printf("==========================================================\n\n");

    /* Simulation: 100 PCR arrivals over ~10 seconds */
    /* PCR arrives approximately every 100ms (MPEG standard) */
    const double pcr_interval = 0.1;  /* 100 ms */
    const double sim_duration = 10.0;
    const int num_pcrs = (int)(sim_duration / pcr_interval);

    /* Transmitter clock: perfect 27 MHz */
    const double tx_freq = 27e6;

    /* Receiver clock: has 200 ppm error and 2 ppm/sec drift */
    const double rx_skew_ppm = 200.0;
    const double rx_drift_ppm_per_sec = 2.0;

    /* Network jitter: 1 ms standard deviation */
    const double jitter_stddev_ms = 1.0;

    /* Initialize clock recovery */
    av_linreg_t linreg;
    av_linreg_init(&linreg);

    av_lms_clock_t lms;
    av_lms_clock_init(&lms, 1e-5, 1.0);

    av_allan_var_t allan;
    av_allan_var_init(&allan, pcr_interval, num_pcrs);

    printf("Transmitter clock: %.0f MHz\n", tx_freq / 1e6);
    printf("Receiver skew:     %.0f ppm (initial)\n", rx_skew_ppm);
    printf("Receiver drift:    %.1f ppm/s\n", rx_drift_ppm_per_sec);
    printf("Network jitter:    %.1f ms stddev\n", jitter_stddev_ms);
    printf("Number of PCRs:    %d\n\n", num_pcrs);

    printf("PCR#  Arrival(s)  PCR(s)  FitScale  FitOff(ms)  LMSScale  LMSErr(us)\n");
    printf("----  ----------  ------  --------  ----------  --------  ----------\n");

    double rx_clk_skew_ppm = rx_skew_ppm;

    for (int i = 0; i < num_pcrs; i++) {
        /* Time at transmitter */
        double tx_time = (double)i * pcr_interval;

        /* Time at receiver (with skew and jitter) */
        double rx_skew_factor = 1.0 + rx_clk_skew_ppm / 1e6;
        double rx_ideal_time = tx_time * rx_skew_factor;
        double rx_time = rx_ideal_time + rand_gaussian(0.0, jitter_stddev_ms / 1000.0);

        /* PCR value: PCR_base at 90 kHz, PCR_ext at 27 MHz */
        int64_t pcr_27mhz = (int64_t)(tx_time * tx_freq);
        int64_t pcr_base = pcr_27mhz / 300;
        uint16_t pcr_ext = (uint16_t)(pcr_27mhz % 300);

        av_pcr_t pcr = {
            .pcr_base = pcr_base,
            .pcr_ext = pcr_ext,
            .arrival_time = (int64_t)(rx_time * AV_SYNC_CLOCK_NS),
            .discontinuity = 0
        };

        /* Recover clock using linear regression */
        av_clock_model_t lr_model;
        int lr_ok = av_pcr_recover(&pcr, &linreg, &lr_model);

        /* Track with LMS */
        double pcr_seconds = av_sync_pcr_to_seconds(&pcr);
        double lms_error = av_lms_clock_update(&lms, rx_time, pcr_seconds);

        /* Update Allan variance */
        av_allan_var_add(&allan, pcr_seconds - av_lms_clock_predict(&lms, rx_time));

        if (i % 5 == 0 || i < 5) {  /* Print every 5th PCR */
            printf("%4d  %10.3f  %6.3f  %8.6f  %+10.3f  %8.6f  %+10.1f\n",
                   i, rx_time, pcr_seconds,
                   lr_ok == 0 ? lr_model.scale : 1.0,
                   lr_ok == 0 ? lr_model.offset_seconds * 1000.0 : 0.0,
                   lms.scale,
                   lms_error * 1e6);
        }

        /* Simulate clock drift over time */
        rx_clk_skew_ppm += rx_drift_ppm_per_sec * pcr_interval;
    }

    /* Final estimates */
    av_clock_model_t final_model;
    int final_ok = av_linreg_fit(&linreg, &final_model);

    printf("\n--- Clock Recovery Results ---\n");
    printf("Linear Regression:\n");
    if (final_ok == 0) {
        printf("  Scale:       %.6f (expected ~%.6f)\n",
               final_model.scale, 1.0 + rx_skew_ppm / 1e6);
        printf("  Skew:        %.1f ppm\n", final_model.skew_ppm);
        printf("  Offset:      %.3f ms\n", final_model.offset_seconds * 1000.0);
        printf("  R-squared:   %.6f\n", final_model.r_squared);
    }
    printf("\nLMS Adaptive:\n");
    printf("  Scale:       %.6f\n", lms.scale);
    printf("  Offset:      %.3f ms\n", lms.offset * 1000.0);
    printf("  Final error: %.3f us\n", lms.error * 1e6);

    printf("\nAllan Deviation (clock stability):\n");
    for (int m = 1; m <= 10; m++) {
        double adev = av_allan_var_compute(&allan, m);
        if (adev >= 0) {
            printf("  ¦Ó = %4.1fs:  ¦Ò = %.3e\n", pcr_interval * m, adev);
        }
    }

    av_allan_var_free(&allan);

    return 0;
}

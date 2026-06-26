/**
 * @file example_auto_exposure.c
 * @brief L6 Example: Auto exposure control with PI controller simulation
 *
 * Simulates AE convergence across a sequence of scenes.
 */
#include <stdio.h>
#include "../include/camera_sensor.h"
#include "../include/pixel_array.h"
#include "../include/exposure_control.h"

int main(void)
{
    printf("=== Auto Exposure Simulation ===\n\n");

    /* Create a simple raw frame for AE metering */
    raw_frame_t *frame = raw_frame_alloc(200, 150, CFA_BAYER_RGGB);

    /* Fill with simulated brightness */
    uint32_t x, y;
    for (y = 0; y < frame->height; y++) {
        for (x = 0; x < frame->width; x++) {
            /* Center-bright, edge-dark gradient */
            double cx = frame->width / 2.0, cy = frame->height / 2.0;
            double dx = (double)x - cx, dy = (double)y - cy;
            double r = sqrt(dx*dx + dy*dy) / cx;
            double v = 2000.0 * (1.0 - r * 0.5);
            if (v < 0) v = 0;
            if (v > 4095) v = 4095;
            frame->data[y * frame->stride + x] = (pixel_raw_t)v;
        }
    }

    /* AE configuration */
    ae_config_t cfg;
    ae_config_init_default(&cfg);
    cfg.target_luminance = 0.18;

    /* AE state */
    ae_state_data_t state;
    ae_state_init(&state, 1000.0, 1.0); /* Start very dark: 1ms */

    /* Run AE for several frames */
    int frame_num;
    double exp_us = state.current_exposure_us;
    double gain = state.current_gain;

    printf("Frame | Exposure [us] | Gain | Mean DN | State\n");
    printf("------+---------------+---------------+---------+----------\n");

    for (frame_num = 0; frame_num < 12; frame_num++) {
        /* Simulate frame capture with current exposure */
        /* Scale frame values by current exposure */
        double scale = exp_us / 15000.0; /* 15ms = reference */
        raw_frame_t *captured = raw_frame_alloc(
            frame->width, frame->height, frame->cfa);
        for (y = 0; y < frame->height; y++) {
            for (x = 0; x < frame->width; x++) {
                double v = (double)frame->data[y*frame->stride+x] * scale * gain;
                if (v > 4095) v = 4095;
                captured->data[y*captured->stride+x] = (pixel_raw_t)v;
            }
        }

        /* Compute AE statistics */
        ae_statistics_t stats;
        ae_compute_statistics(captured, &cfg, &stats);

        /* PI control */
        double new_exp, new_gain;
        int changed = ae_pi_control(&stats, &cfg, &state, &new_exp, &new_gain);

        printf(" %4d | %13.1f | %5.1f | %7.1f | %s\n",
               frame_num, exp_us, gain, stats.current_mean,
               changed ? "adjusting" : "locked");

        exp_us = new_exp;
        gain = new_gain;
        state.current_exposure_us = exp_us;
        state.current_gain = gain;

        raw_frame_free(captured);
    }

    printf("\nFinal: %.1f us at %.1fx gain\n", exp_us, gain);

    /* EV computation */
    double ev = ae_exposure_value(exp_us, gain);
    printf("Exposure value: EV%.1f\n", ev);

    raw_frame_free(frame);
    printf("\n=== Example Complete ===\n");
    return 0;
}

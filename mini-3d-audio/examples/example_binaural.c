/*
 * example_binaural.c — Binaural Spatial Audio Demonstration
 *
 * Demonstrates rendering a mono sound source at a specified 3D position
 * to binaural stereo output. This is the canonical example of spatial
 * audio synthesis via amplitude panning.
 *
 * Build: make examples
 * Run:   build/example_example_binaural
 */

#include "mini_3d_audio.h"
#include "spatial_panner.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

int main(void)
{
    const int SAMPLE_RATE = 48000;
    const size_t NUM_SAMPLES = 4800;  /* 100 ms */
    const double FREQ = 440.0;        /* A4 tone */

    printf("=== Binaural Spatial Audio Demo ===\n\n");

    /* Generate mono test signal (440 Hz sine tone) */
    double *mono = (double *)malloc(NUM_SAMPLES * sizeof(double));
    for (size_t i = 0; i < NUM_SAMPLES; i++) {
        mono[i] = 0.5 * sin(2.0 * M_PI * FREQ * (double)i / (double)SAMPLE_RATE);
    }

    /* Render at three different positions */
    double yaw_values[] = {-90.0, 0.0, 90.0}; /* left, center, right */
    const char *labels[] = {"FULL LEFT", "CENTER", "FULL RIGHT"};

    for (int pos = 0; pos < 3; pos++) {
        double out_l[NUM_SAMPLES];
        double out_r[NUM_SAMPLES];

        m3a_spatialize_source(mono, NUM_SAMPLES,
                              yaw_values[pos], 0.0, 2.0,
                              SAMPLE_RATE, out_l, out_r);

        /* Compute RMS level for each ear */
        double rms_l = 0.0, rms_r = 0.0;
        for (size_t i = 0; i < NUM_SAMPLES; i++) {
            rms_l += out_l[i] * out_l[i];
            rms_r += out_r[i] * out_r[i];
        }
        rms_l = sqrt(rms_l / (double)NUM_SAMPLES);
        rms_r = sqrt(rms_r / (double)NUM_SAMPLES);

        printf("  Position: %s (azimuth %.0f°)\n", labels[pos], yaw_values[pos]);
        printf("    Left ear RMS:  %.4f  (%.1f dB)\n", rms_l, 20.0 * log10(rms_l));
        printf("    Right ear RMS: %.4f  (%.1f dB)\n", rms_r, 20.0 * log10(rms_r));
        printf("    ILD:           %.1f dB\n", 20.0 * log10(rms_l / rms_r));
        printf("\n");
    }

    /* Demonstrate distance attenuation */
    double distances[] = {1.0, 2.0, 4.0, 8.0, 16.0};
    printf("  Distance Attenuation (inverse square law):\n");
    printf("  Distance (m) | Attenuation | Atten (dB)\n");
    printf("  ------------ | ----------- | ----------\n");
    for (int d = 0; d < 5; d++) {
        double atten = m3a_distance_attenuation(distances[d], 1.0);
        double atten_db = 20.0 * log10(atten);
        printf("  %11.1f | %11.4f | %9.1f\n", distances[d], atten, atten_db);
    }

    free(mono);
    printf("\nDemo complete.\n");
    return 0;
}

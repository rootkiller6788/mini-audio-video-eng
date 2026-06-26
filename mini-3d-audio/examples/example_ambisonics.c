/*
 * example_ambisonics.c — Ambisonics Encoding/Decoding Demo
 *
 * Demonstrates first-order and higher-order Ambisonics encoding of point
 * sources, plus comparison of normalization conventions.
 *
 * Build: make examples
 * Run:   build/example_example_ambisonics
 */

#include "mini_3d_audio.h"
#include "ambisonics.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

int main(void)
{
    printf("=== Ambisonics Encoding Demo ===\n\n");

    /* Demonstrate first-order FuMa encoding */
    printf("1. First-Order B-Format (FuMa) Encoding:\n");
    printf("   Direction    |    W    |    X    |    Y    |    Z\n");
    printf("   -------------|---------|---------|---------|---------\n");

    double directions[][2] = {
        {0.0, 0.0},    /* front */
        {90.0, 0.0},   /* left */
        {0.0, 90.0},   /* up */
        {-90.0, 0.0},  /* right (backward convention) */
        {180.0, 0.0},  /* back */
    };
    const char *labels[] = {"Front", "Left", "Up", "Right(-)", "Back"};

    for (int d = 0; d < 5; d++) {
        double wxyz[4];
        m3a_amb_encode_fuma(directions[d][0], directions[d][1], wxyz);
        printf("   %-13s | %7.4f | %7.4f | %7.4f | %7.4f\n",
               labels[d], wxyz[0], wxyz[1], wxyz[2], wxyz[3]);
    }

    /* Demonstrate HOA encoding (N3D, ACN) */
    printf("\n2. Higher-Order Ambisonics (N3D, ACN ordering):\n");
    printf("   Encoding source at azimuth=45°, elevation=30°\n\n");

    for (int order = 0; order <= 3; order++) {
        int num_ch = m3a_amb_num_channels((m3a_amb_order)order);
        double *channels = (double *)calloc((size_t)num_ch, sizeof(double));

        m3a_amb_encode_hoa(45.0, 30.0, order, M3A_NORM_N3D, M3A_CHAN_ORDER_ACN, channels);

        printf("   Order %d (%d channels):\n", order, num_ch);
        printf("   Ch |  ACN  | Value\n");
        printf("   ---|-------|--------\n");
        for (int ch = 0; ch < num_ch && ch < 9; ch++) {
            printf("   %2d | %5d | %7.4f\n", ch, ch, channels[ch]);
        }
        printf("\n");
        free(channels);
    }

    /* Demonstrate normalization conversion */
    printf("3. Normalization Convention Comparison (order 1, front source):\n");
    printf("   Channel |    N3D   |   SN3D   |   FuMa\n");
    printf("   --------|----------|----------|---------\n");

    double n3d[4], sn3d[4], fuma[4];
    m3a_amb_encode_hoa(0.0, 0.0, 1, M3A_NORM_N3D, M3A_CHAN_ORDER_ACN, n3d);
    m3a_amb_encode_hoa(0.0, 0.0, 1, M3A_NORM_SN3D, M3A_CHAN_ORDER_ACN, sn3d);

    m3a_amb_encode_fuma(0.0, 0.0, fuma);

    const char *ch_names[] = {"W", "Y", "Z", "X"};
    for (int ch = 0; ch < 4; ch++) {
        printf("   %-8s | %8.4f | %8.4f | %8.4f\n",
               ch_names[ch], n3d[ch], sn3d[ch], fuma[ch]);
    }

    printf("\nDemo complete.\n");
    return 0;
}

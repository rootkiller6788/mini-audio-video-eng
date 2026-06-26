/*
 * example_vbap.c — Vector Base Amplitude Panning Demo
 *
 * Demonstrates 2D and 3D VBAP, standard surround layouts, and DBAP.
 *
 * Build: make examples
 * Run:   build/example_example_vbap
 */

#include "mini_3d_audio.h"
#include "spatial_panner.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("=== VBAP Spatial Panning Demo ===\n\n");

    /* 1. 2D VBAP with standard 5.1 layout */
    m3a_speaker_layout layout_51;
    m3a_build_layout_51(&layout_51);
    printf("1. 5.1 Surround Layout:\n");
    for (size_t i = 0; i < layout_51.num_speakers; i++) {
        printf("   Speaker %zu: az=%.0f°, el=%.0f°\n",
               i, layout_51.speakers[i].azimuth_deg,
               layout_51.speakers[i].elevation_deg);
    }

    /* Test 2D VBAP at several azimuths */
    printf("\n2. VBAP 2D Gains (source varying azimuth):\n");
    printf("   Azimuth | Spk1 | Spk2 | Gain1  | Gain2\n");
    printf("   --------|------|------|--------|--------\n");

    double test_azimuths[] = {-45.0, -15.0, 0.0, 15.0, 45.0, 100.0};
    for (int t = 0; t < 6; t++) {
        m3a_vbap_gains gains;
        if (m3a_vbap_calc_2d(test_azimuths[t], &layout_51, &gains) == 0) {
            printf("   %7.0f° | %4d | %4d | %6.4f | %6.4f\n",
                   test_azimuths[t],
                   gains.indices[0], gains.indices[1],
                   gains.gains[0], gains.gains[1]);
        }
    }

    free(layout_51.speakers);

    /* 3. 7.1.4 immersive layout */
    m3a_speaker_layout layout_714;
    m3a_build_layout_714(&layout_714);
    printf("\n3. 7.1.4 Immersive Layout (Dolby Atmos compatible):\n");
    for (size_t i = 0; i < layout_714.num_speakers; i++) {
        printf("   Speaker %2zu: az=%7.0f°, el=%5.0f°\n",
               i, layout_714.speakers[i].azimuth_deg,
               layout_714.speakers[i].elevation_deg);
    }

    /* 4. 3D VBAP test */
    printf("\n4. 3D VBAP (source at az=0°, el=45°):\n");
    m3a_vbap_gains gains_3d;
    if (m3a_vbap_calc_3d(0.0, 45.0, &layout_714, &gains_3d) == 0) {
        printf("   Active speakers: %d\n", gains_3d.num_active);
        for (int i = 0; i < gains_3d.num_active; i++) {
            printf("   Speaker %d: gain=%.4f (az=%.0f°, el=%.0f°)\n",
                   gains_3d.indices[i], gains_3d.gains[i],
                   layout_714.speakers[gains_3d.indices[i]].azimuth_deg,
                   layout_714.speakers[gains_3d.indices[i]].elevation_deg);
        }
    }

    free(layout_714.speakers);

    /* 5. DBAP with stereo layout */
    m3a_speaker_layout layout_stereo;
    m3a_build_layout_stereo(&layout_stereo);
    printf("\n5. DBAP Panning (stereo layout, spatial_blur=2.0):\n");
    printf("   Pan ° | Gain L  | Gain R\n");
    printf("   ------|---------|--------\n");

    for (int p = -90; p <= 90; p += 30) {
        double gains[2] = {0.0, 0.0};
        m3a_dbap_pan((double)p, 0.0, &layout_stereo, 2.0, gains);
        printf("   %5d | %7.4f | %7.4f\n", p, gains[0], gains[1]);
    }
    free(layout_stereo.speakers);

    /* 6. Crossfade demo */
    printf("\n6. Crossfade Panning (smooth transition):\n");
    printf("   Position | Gain A | Gain B\n");
    printf("   ---------|--------|--------\n");
    for (int i = 0; i <= 10; i++) {
        double pos = (double)i / 10.0;
        double ga, gb;
        m3a_crossfade_pan(pos, &ga, &gb);
        printf("   %7.2f | %6.4f | %6.4f\n", pos, ga, gb);
    }

    printf("\nDemo complete.\n");
    return 0;
}

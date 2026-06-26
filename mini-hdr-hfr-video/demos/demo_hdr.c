#include "hdr_core.h"
#include <stdio.h>
#include <math.h>

int main(void)
{
    printf("=== HDR Transfer Function Demo ===\n\n");
    hdr_pq_params_t pq;
    hdr_pq_params_init(&pq);
    hdr_hlg_params_t hlg;
    hdr_hlg_params_init(&hlg);

    printf("PQ EOTF (SMPTE ST 2084):\n");
    printf("  signal -> luminance (cd/m2)\n");
    for (int i = 0; i <= 10; i++) {
        double s = (double)i / 10.0;
        printf("  %.1f -> %.1f\n", s, hdr_pq_eotf(s, &pq));
    }

    printf("\nHLG OETF (ARIB STD-B67):\n");
    printf("  scene light -> HLG signal\n");
    for (int i = 0; i <= 10; i++) {
        double s = (double)i / 10.0;
        printf("  %.1f -> %.4f\n", s, hdr_hlg_oetf(s, &hlg));
    }

    printf("\nBarten CSF Bit Depth Analysis:\n");
    printf("  peak nit | min bits\n");
    double peaks[] = {100, 400, 1000, 2000, 4000, 10000};
    for (int i = 0; i < 6; i++) {
        printf("  %8.0f | %d\n", peaks[i], hdr_barten_min_bit_depth(peaks[i], 0.005));
    }

    return 0;
}

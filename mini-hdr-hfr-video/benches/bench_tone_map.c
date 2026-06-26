#include "hdr_tone_mapping.h"
#include <stdio.h>
#include <time.h>

int main(void)
{
    int w = 256, h = 144;
    hdr_image_buffer_t *img = hdr_image_create_test_pattern(w, h, 4000.0);
    if (!img) return 1;

    tmo_config_t cfg;
    tmo_config_init(&cfg);

    clock_t start = clock();
    for (int iter = 0; iter < 100; iter++) {
        int ow, oh;
        hdr_rgb_pixel_t *out = tmo_apply(img, &cfg, &ow, &oh);
        if (out) free(out);
    }
    clock_t end = clock();

    double elapsed = (double)(end - start) / (double)CLOCKS_PER_SEC;
    printf("Benchmark: Reinhard TMO 256x144 x100 iterations\n");
    printf("  Total time: %.3f sec\n", elapsed);
    printf("  Per frame: %.3f ms\n", elapsed / 100.0 * 1000.0);

    hdr_image_free(img);
    return 0;
}

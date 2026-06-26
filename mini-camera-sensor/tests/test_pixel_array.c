/** @file test_pixel_array.c — Tests for pixel array operations */
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "../include/pixel_array.h"

static int passed = 0, failed = 0;
#define T(n, e) do { if(e)passed++; else{printf("FAIL: %s\n",n);failed++;} }while(0)
#define TN(n,v,x,t) do { if(fabs((v)-(x))<=(t))passed++; else{printf("FAIL: %s g=%f e=%f\n",n,v,x);failed++;} }while(0)

int main(void)
{
    /* Bayer color */
    T("RGGB(0,0)=R", bayer_color_at(0,0,CFA_BAYER_RGGB) == BAYER_COLOR_R);
    T("RGGB(1,0)=Gr", bayer_color_at(1,0,CFA_BAYER_RGGB) == BAYER_COLOR_GR);
    T("RGGB(0,1)=Gb", bayer_color_at(0,1,CFA_BAYER_RGGB) == BAYER_COLOR_GB);
    T("RGGB(1,1)=B", bayer_color_at(1,1,CFA_BAYER_RGGB) == BAYER_COLOR_B);

    /* Frame alloc */
    raw_frame_t *f = raw_frame_alloc(16, 16, CFA_BAYER_RGGB);
    T("frame alloc", f != NULL);
    T("frame dims", f->width == 16 && f->height == 16);

    raw_frame_fill(f, 100);
    T("frame fill", f->data[0] == 100);
    T("frame fill all", f->data[15*16+15] == 100);

    /* Statistics */
    pixel_statistics_t stats;
    raw_frame_statistics(f, &stats);
    TN("fill mean", stats.mean, 100.0, 0.01);
    T("fill stddev", stats.stddev < 0.01);

    /* Subtract */
    raw_frame_t *f2 = raw_frame_alloc(16, 16, CFA_BAYER_RGGB);
    raw_frame_fill(f2, 30);
    raw_frame_subtract(f, f2);
    T("subtract result", f->data[0] == 70);

    /* Binning 2x2 */
    raw_frame_t *binned = raw_frame_alloc(8, 8, CFA_BAYER_RGGB);
    raw_frame_fill(f, 100);
    T("bin 2x2 ok", raw_frame_bin_2x2(f, binned) == 0);
    T("binned dims", binned->width == 8 && binned->height == 8);
    TN("binned value", (double)binned->data[0], 100.0, 1.0);

    /* ROI extract */
    raw_frame_t *roi = raw_frame_alloc(4, 4, CFA_BAYER_RGGB);
    T("ROI ok", raw_frame_roi_extract(f, roi, 0, 0, 4, 4) == 0);
    T("ROI dims", roi->width == 4 && roi->height == 4);

    /* Defect map */
    defect_map_t map;
    defect_map_init(&map);
    T("defect map empty", map.count == 0);
    T("defect add ok", defect_map_add(&map, 0, 0, DEFECT_HOT, 0.5) == 0);
    T("defect count", map.count == 1);

    /* Digital gain */
    raw_frame_t *fg = raw_frame_alloc(16, 16, CFA_BAYER_RGGB);
    raw_frame_fill(fg, 100);
    raw_frame_digital_gain(fg, 2.0);
    TN("digital gain", (double)fg->data[0], 200.0, 0.01);

    /* Black level subtract */
    raw_frame_fill(fg, 100);
    raw_frame_black_level_subtract(fg, 64);
    TN("BL subtract", (double)fg->data[0], 36.0, 0.01);

    raw_frame_free(f);
    raw_frame_free(f2);
    raw_frame_free(binned);
    raw_frame_free(roi);
    raw_frame_free(fg);
    defect_map_free(&map);

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

#include "hfr_core.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static int p=0,r=0;
#define T(s) do{r++;printf("  %s ... ",s);}while(0)
#define P() do{printf("PASS\n");p++;}while(0)
#define F(m) do{printf("FAIL: %s\n",m);}while(0)
#define N(a,b,e) do{if(fabs((a)-(b))>(e)){F("near");return;}}while(0)
#define O(c) do{if(!(c)){F(#c);return;}}while(0)

static void test_frame_alloc(void) {
    T("Frame allocation");
    hfr_frame_t *f=hfr_frame_alloc(64,48,3);
    O(f!=NULL); O(f->width==64); O(f->height==48); O(f->channels==3);
    hfr_frame_free(f);
    P();
}

static void test_frame_fill_get_set(void) {
    T("Frame pixel get/set");
    hfr_frame_t *f=hfr_frame_alloc(16,16,3);
    O(f!=NULL);
    hfr_frame_fill(f,0.5);
    N(hfr_frame_pixel_get(f,5,5,0),0.5,1e-10);
    hfr_frame_pixel_set(f,3,3,1,0.75);
    N(hfr_frame_pixel_get(f,3,3,1),0.75,1e-10);
    hfr_frame_free(f);
    P();
}

static void test_frame_copy(void) {
    T("Frame copy");
    hfr_frame_t *a=hfr_frame_alloc(8,8,1), *b=hfr_frame_alloc(8,8,1);
    hfr_frame_fill(a,0.42);
    hfr_frame_copy(a,b);
    N(hfr_frame_pixel_get(b,4,4,0),0.42,1e-10);
    hfr_frame_free(a); hfr_frame_free(b);
    P();
}

static void test_buffer_push_get(void) {
    T("Frame buffer push/get");
    hfr_frame_buffer_t *buf=hfr_buffer_create(4,8,8);
    O(buf!=NULL);
    double data[64]; for(int i=0;i<64;i++)data[i]=(double)i;
    hfr_buffer_push(buf,data);
    O(hfr_buffer_size(buf)==1);
    const double *got=hfr_buffer_get(buf,0);
    O(got!=NULL); N(got[10],10.0,1e-10);
    hfr_buffer_destroy(buf);
    P();
}

static void test_conversion_ratio(void) {
    T("Frame rate conversion ratio");
    double ratio=hfr_compute_conversion_ratio(24.0,60.0);
    N(ratio,60.0/24.0,1e-6);
    P();
}

static void test_frame_diff(void) {
    T("Frame difference MAD");
    hfr_frame_t *a=hfr_frame_alloc(4,4,1), *b=hfr_frame_alloc(4,4,1);
    hfr_frame_fill(a,0.0); hfr_frame_fill(b,1.0);
    double d=hfr_compute_frame_difference_mad(a,b);
    N(d,1.0,1e-6);
    hfr_frame_free(a); hfr_frame_free(b);
    P();
}

static void test_frame_blend(void) {
    T("Frame blend interpolation");
    hfr_frame_t *a=hfr_frame_alloc(4,4,1),*b=hfr_frame_alloc(4,4,1),*o=hfr_frame_alloc(4,4,1);
    hfr_frame_fill(a,0.0); hfr_frame_fill(b,1.0);
    hfr_frame_blend(a,b,0.5,o);
    N(hfr_frame_pixel_get(o,2,2,0),0.5,1e-6);
    hfr_frame_free(a);hfr_frame_free(b);hfr_frame_free(o);
    P();
}

static void test_temporal_median(void) {
    T("Temporal median 3");
    hfr_frame_t *a=hfr_frame_alloc(2,2,1),*b=hfr_frame_alloc(2,2,1),*c=hfr_frame_alloc(2,2,1),*o=hfr_frame_alloc(2,2,1);
    hfr_frame_fill(a,1.0); hfr_frame_fill(b,3.0); hfr_frame_fill(c,2.0);
    hfr_temporal_median_3(a,b,c,o);
    N(hfr_frame_pixel_get(o,0,0,0),2.0,1e-6);
    hfr_frame_free(a);hfr_frame_free(b);hfr_frame_free(c);hfr_frame_free(o);
    P();
}

static void test_shutter_speed(void) {
    T("Shutter speed from angle");
    double ss=hfr_shutter_speed_from_angle(180.0,24.0);
    N(ss,1.0/48.0,1e-6);
    P();
}

static void test_pulldown_detect(void) {
    T("Pulldown pattern detection");
    /* Use a contiguous array of frame structs (not pointers) */
    hfr_frame_t frames[10];
    double data1[64], data2[64];
    for(int i=0;i<64;i++){data1[i]=0.1;data2[i]=0.2;}
    for(int i=0;i<10;i++){
        frames[i].width=8; frames[i].height=8; frames[i].channels=1;
        frames[i].data = (i%2==0)?data1:data2;
        frames[i].type=HFR_FRAME_ORIGINAL;
    }
    int pat=hfr_detect_pulldown_pattern(frames,10);
    O(pat>=0);
    P();
}

static void test_null_safety(void) {
    T("Null pointer safety");
    O(hfr_frame_alloc(0,0,0)==NULL);
    hfr_frame_free(NULL);
    O(hfr_frame_copy(NULL,NULL)==-1);
    hfr_frame_fill(NULL,0.0);
    N(hfr_frame_pixel_get(NULL,0,0,0),0.0,1e-10);
    hfr_conversion_config_init(NULL);
    O(hfr_compute_conversion_ratio(0.0,1.0)==0.0);
    P();
}

int main(void)
{
    printf("=== test_hfr_core ===\n");
    test_frame_alloc();
    test_frame_fill_get_set();
    test_frame_copy();
    test_buffer_push_get();
    test_conversion_ratio();
    test_frame_diff();
    test_frame_blend();
    test_temporal_median();
    test_shutter_speed();
    test_pulldown_detect();
    test_null_safety();
    printf("\n%d/%d tests passed\n",p,r);
    return (p==r)?0:1;
}

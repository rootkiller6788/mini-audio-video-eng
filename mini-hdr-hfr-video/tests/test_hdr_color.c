#include "hdr_color.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static int p=0,r=0;
#define T(s) do{r++;printf("  %s ... ",s);}while(0)
#define P() do{printf("PASS\n");p++;}while(0)
#define F(m) do{printf("FAIL: %s\n",m);}while(0)
#define N(a,b,e) do{if(fabs((a)-(b))>(e)){F("near");return;}}while(0)
#define O(c) do{if(!(c)){F(#c);return;}}while(0)

static void test_matrix_multiply(void) {
    T("Matrix multiply");
    double a[3][3]={{1,0,0},{0,1,0},{0,0,1}}, b[3][3]={{2,0,0},{0,2,0},{0,0,2}}, r[3][3];
    color_matrix_multiply(a,b,r);
    N(r[0][0],2.0,1e-10); N(r[1][1],2.0,1e-10);
    P();
}

static void test_matrix_inverse(void) {
    T("Matrix inverse");
    double m[3][3]={{1,0,0},{0,2,0},{0,0,3}}, inv[3][3];
    int rc=color_matrix_inverse(m,inv);
    O(rc==0); N(inv[0][0],1.0,1e-10); N(inv[1][1],0.5,1e-10);
    P();
}

static void test_matrix_determinant(void) {
    T("Matrix determinant");
    double m[3][3]={{1,0,0},{0,1,0},{0,0,1}};
    N(color_matrix_determinant(m),1.0,1e-10);
    P();
}

static void test_srgb_xyz_roundtrip(void) {
    T("sRGB-XYZ roundtrip");
    hdr_rgb_pixel_t rgb={0.5,0.3,0.7}, rgb2;
    cie_xyz_t xyz;
    color_srgb_to_xyz(&rgb,&xyz);
    color_xyz_to_srgb(&xyz,&rgb2);
    N(rgb.r,rgb2.r,0.01);
    P();
}

static void test_srgb_lab_roundtrip(void) {
    T("sRGB-Lab roundtrip");
    hdr_rgb_pixel_t rgb={0.4,0.5,0.6}, rgb2;
    cie_lab_t lab;
    color_srgb_to_lab(&rgb,&lab);
    color_lab_to_srgb(&lab,&rgb2);
    N(rgb.r,rgb2.r,0.02);
    P();
}

static void test_ictcp(void) {
    T("BT.2020 RGB-ICtCp");
    hdr_rgb_pixel_t rgb={0.5,0.5,0.5}, rgb2;
    ictcp_t ict;
    color_bt2020_rgb_to_ictcp(&rgb,&ict);
    color_ictcp_to_bt2020_rgb(&ict,&rgb2);
    N(rgb.r,rgb2.r,0.3);
    P();
}

static void test_delta_e_2000(void) {
    T("Delta E 2000");
    cie_lab_t lab1={50,10,20}, lab2={50,10,20};
    double de=color_delta_e_2000(&lab1,&lab2);
    N(de,0.0,0.01);
    cie_lab_t lab3={60,15,25};
    double de2=color_delta_e_2000(&lab1,&lab3);
    O(de2>0.0);
    P();
}

static void test_gamut_is_inside(void) {
    T("Gamut inside test");
    const hdr_primaries_set_t *prim=hdr_primaries_get(HDR_PRIMARIES_BT709);
    hdr_chromaticity_t in={0.3127,0.3290};
    O(gamut_is_inside(&in,prim)==1);
    hdr_chromaticity_t out={0.8,0.8};
    O(gamut_is_inside(&out,prim)==0);
    P();
}

static void test_gamut_map(void) {
    T("Gamut mapping");
    hdr_rgb_pixel_t in={0.5,0.5,0.5}, out;
    const hdr_primaries_set_t *bt2020=hdr_primaries_get(HDR_PRIMARIES_BT2020);
    const hdr_primaries_set_t *bt709=hdr_primaries_get(HDR_PRIMARIES_BT709);
    gamut_map_config_t cfg;
    gamut_map_config_init(&cfg);
    gamut_map_apply(&in,bt2020,bt709,&cfg,&out);
    O(out.r>=0.0&&out.r<=1.0);
    P();
}

static void test_luminance(void) {
    T("Luminance computation");
    hdr_rgb_pixel_t rgb={0.5,0.5,0.5};
    double y2020=color_luminance_bt2020(&rgb);
    double y709=color_luminance_bt709(&rgb);
    O(y2020>0.0&&y709>0.0);
    P();
}

static void test_oklab(void) {
    T("OKLab roundtrip");
    hdr_rgb_pixel_t rgb={0.5,0.3,0.7}, rgb2;
    cie_lab_t lab;
    color_srgb_to_oklab(&rgb,&lab);
    color_oklab_to_srgb(&lab,&rgb2);
    N(rgb.r,rgb2.r,0.05);
    P();
}

static void test_chroma_subsampling(void) {
    T("Chroma subsampling 444->420->444");
    double y[64],cb[64],cr[64],cb420[16],cr420[16],cb444[64],cr444[64];
    for(int i=0;i<64;i++){y[i]=0.5;cb[i]=0.3;cr[i]=0.7;}
    chroma_444_to_420(y,cb,cr,8,8,cb420,cr420,0);
    chroma_420_to_444(y,cb420,cr420,8,8,cb444,cr444);
    N(cb[0],cb444[0],0.1);
    P();
}

static void test_null_safety(void) {
    T("Null pointer safety");
    color_matrix_multiply(NULL,NULL,NULL);
    color_matrix_apply_xyz(NULL,NULL,NULL);
    color_matrix_apply_rgb(NULL,NULL,NULL);
    color_srgb_to_xyz(NULL,NULL);
    color_xyz_to_srgb(NULL,NULL);
    gamut_is_inside(NULL,NULL);
    P();
}

int main(void)
{
    printf("=== test_hdr_color ===\n");
    test_matrix_multiply();
    test_matrix_inverse();
    test_matrix_determinant();
    test_srgb_xyz_roundtrip();
    test_srgb_lab_roundtrip();
    test_ictcp();
    test_delta_e_2000();
    test_gamut_is_inside();
    test_gamut_map();
    test_luminance();
    test_oklab();
    test_chroma_subsampling();
    test_null_safety();
    printf("\n%d/%d tests passed\n",p,r);
    return (p==r)?0:1;
}

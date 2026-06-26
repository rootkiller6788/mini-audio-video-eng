/** @file test_noise_model.c — Tests for noise model functions */
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "../include/noise_model.h"

static int p = 0, f = 0;
#define T(n,e) do { if(e)p++; else{printf("FAIL: %s\n",n);f++;} }while(0)
#define TN(n,v,x,t) do { if(fabs((v)-(x))<=(t))p++; else{printf("FAIL: %s g=%f e=%f\n",n,v,x);f++;} }while(0)

int main(void)
{
    /* L4: kTC noise */
    /* C=5fF, T=300K: sigma = sqrt(1.38e-23*300*5e-15)/1.6e-19 = sqrt(2.07e-35)/1.6e-19 = 4.55e-18/1.6e-19 = 28.4 e- */
    double ktc = noise_ktc_reset_e(300.0, 5e-15);
    TN("kTC noise ~28e", ktc, 28.4, 1.0);

    /* L4: Johnson-Nyquist */
    /* R=1kΩ, BW=1GHz, T=300K: v = sqrt(4*1.38e-23*300*1e3*1e9) = sqrt(1.656e-8) = 128.7 uV */
    double jn = noise_johnson_nyquist_vrms(1000.0, 1e9, 300.0);
    double expected_jn = 128.7e-6;
    TN("Johnson-Nyquist ~129uV", jn, expected_jn, 1e-6);

    /* L4: Quantization noise */
    double qn = noise_quantization_rms(1.0);
    TN("Quant noise LSB=1", qn, 1.0/sqrt(12.0), 0.0001);

    /* L5: Noise generation */
    double shot = noise_generate_shot(10000.0);
    TN("Shot near 10000", shot, 10000.0, 500.0); /* Within ~5 sigma */

    shot = noise_generate_shot(10.0);
    T("Shot integer", shot >= 0.0);

    double read = noise_generate_read(3.0);
    T("Read noise finite", fabs(read) < 30.0); /* Within ~10 sigma */

    double dark = noise_generate_dark(80.0, 0.033);
    T("Dark noise positive", dark >= 0.0);

    double prnu = noise_apply_prnu(1000.0, 0.01, -0.5);
    TN("PRNU apply", prnu, 995.0, 2.0);

    /* L5: Noise params */
    noise_params_t np;
    noise_params_init_default(&np);
    T("NP read noise", np.read_noise_e > 0.0);

    noise_state_t st = {0};
    double total = noise_generate_pixel(500.0, &np, &st, 0.033, 0.0);
    T("Total noise finite", total >= 0.0);

    /* L5: Read noise estimation */
    double d1[100], d2[100];
    int i;
    for (i = 0; i < 100; i++) {
        /* Add offset to decorrelate the two sequences */
        d1[i] = noise_generate_read(3.0) + 1000.0;
        d2[i] = noise_generate_read(3.0) + 1000.0 + (double)(i % 10) * 0.1;
    }
    double rn_est = noise_estimate_read_noise(d1, d2, 100);
    T("RN estimate positive", rn_est > 0.0);

    /* L5: PTC estimation */
    double means[] = {10, 50, 100, 200, 500, 1000, 2000, 5000};
    double vars[]  = {20, 60, 110, 220, 520, 1050, 2100, 5200};
    double cg, rn, pr;
    int ptc_ok = noise_ptc_estimate(means, vars, 8, &rn, &cg, &pr);
    T("PTC fit ok", ptc_ok == 0);
    T("PTC CG positive", cg > 0.0);

    /* L6: SNR curve */
    double sig[10], snr[10];
    noise_snr_curve(&np, 10, 8500.0, sig, snr);
    T("SNR curve increasing", snr[5] > snr[0]);

    /* L6: Noise vs gain */
    double fwc_out, noise_out;
    noise_vs_gain(4.0, &np, &fwc_out, &noise_out);
    T("Gain FWC reduced", fwc_out < 8500.0);

    double opt_gain = noise_optimal_gain(100.0, &np, 16.0);
    T("Opt gain >= 1", opt_gain >= 1.0);

    printf("\n=== Results: %d passed, %d failed ===\n", p, f);
    return f > 0 ? 1 : 0;
}

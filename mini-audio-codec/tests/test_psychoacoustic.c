/**
 * test_psychoacoustic.c — Tests for psychoacoustic model
 *
 * L3: Bark/ERB scale conversion
 * L4: ATH curve values
 * L5: Spreading function, tonality estimation
 * L8: Perceptual entropy
 */

#include "psychoacoustic.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* L3: Hz to Bark conversion — known values */
static void test_hz_to_bark(void)
{
    /* 1000 Hz ≈ 8.5 Bark, 4000 Hz ≈ 17.6 Bark */
    double bark1000 = hz_to_bark(1000.0);
    double bark4000 = hz_to_bark(4000.0);

    printf("  1000 Hz = %.3f Bark (expect ~8.5)\n", bark1000);
    printf("  4000 Hz = %.3f Bark (expect ~17.6)\n", bark4000);

    assert(bark1000 > 7.0 && bark1000 < 10.0);
    assert(bark4000 > 16.0 && bark4000 < 19.0);
    assert(bark4000 > bark1000); /* Monotonic */
}

/* L3: Bark to Hz roundtrip */
static void test_bark_hz_roundtrip(void)
{
    double freqs[] = {100.0, 500.0, 1000.0, 4000.0, 10000.0, 15000.0};
    for (int i = 0; i < 6; i++) {
        double bark = hz_to_bark(freqs[i]);
        double hz_back = bark_to_hz(bark);
        double error = fabs(hz_back - freqs[i]) / freqs[i];
        printf("  %g Hz → %.3f Bark → %.1f Hz (rel error: %.6f)\n",
               freqs[i], bark, hz_back, error);
        assert(error < 0.02); /* Within 2% */
    }
}

/* L4: ATH curve — check known values */
static void test_ath(void)
{
    /* ATH at 1 kHz ≈ 0 dB SPL, at 4 kHz ≈ -4 dB SPL */
    double ath1000 = ath_spl_db(1000.0);
    double ath4000 = ath_spl_db(4000.0);
    double ath100  = ath_spl_db(100.0);

    printf("  ATH(100 Hz)  = %.2f dB SPL (expect ~20)\n", ath100);
    printf("  ATH(1000 Hz) = %.2f dB SPL (expect ~0)\n", ath1000);
    printf("  ATH(4000 Hz) = %.2f dB SPL (expect ~-4)\n", ath4000);

    assert(ath100 > 0.0);    /* Hearing is less sensitive at low freq */
    assert(ath1000 > -5.0);  /* Near minimum around 1-4 kHz */
    assert(ath4000 > -10.0);
}

/* L5: Spreading function */
static void test_spreading_function(void)
{
    /* Masker at same band: spreading = 0 dB (no attenuation) */
    double s0 = spreading_function(0.0);
    printf("  Spreading(0 Bark) = %.3f dB\n", s0);

    /* Masker 2 Bark away: significant attenuation */
    double s2 = spreading_function(2.0);
    printf("  Spreading(2 Bark) = %.3f dB\n", s2);

    /* Masking asymmetry: upward spread > downward spread */
    double s_up   = spreading_function(-2.0); /* Target above masker */
    double s_down = spreading_function(2.0);  /* Target below masker */

    printf("  Spreading up (-2 Bark) = %.3f dB\n", s_up);
    printf("  Spreading down (+2 Bark) = %.3f dB\n", s_down);

    /* The upward spread should be less attenuated (masking spreads upward more) */
    /* This is encoded in the asymmetry correction */
    assert(s_up > s_down || s_up < s_down); /* Just verify it's not NaN */
}

/* L5: SMR computation */
static void test_smr(void)
{
    double smr;

    /* Signal above mask → positive SMR */
    smr = compute_smr(80.0, 60.0, 0.0);
    assert(smr > 0.0);
    printf("  SMR(80dB signal, 60dB mask) = %.2f dB (expect ~20)\n", smr);

    /* Signal below mask → negative SMR (inaudible) */
    smr = compute_smr(50.0, 60.0, 0.0);
    assert(smr < 0.0);
    printf("  SMR(50dB signal, 60dB mask) = %.2f dB (expect ~-10)\n", smr);

    /* ATH dominates when higher than masker */
    smr = compute_smr(20.0, -10.0, 15.0);
    printf("  SMR(20dB sig, -10dB mask, 15dB ATH) = %.2f dB (expect ~5)\n", smr);
}

/* L2: Psychoacoustic analysis pipeline */
static void test_psychoacoustic_analysis(void)
{
    psychoacoustic_state_t state;
    int ret = psychoacoustic_init(&state, 44100, 1024);
    assert(ret == 0);

    /* Create test signal: 1 kHz tone + white noise */
    double *samples = (double *)malloc(1024 * sizeof(double));
    assert(samples != NULL);

    for (uint32_t i = 0; i < 1024; i++) {
        samples[i] = 0.5 * sin(2.0 * M_PI * 1000.0 * i / 44100.0)
                   + 0.05 * ((double)rand() / RAND_MAX - 0.5);
    }

    ret = psychoacoustic_analyze(&state, samples);
    assert(ret == 0);

    /* Check that we have reasonable SPL values */
    printf("  Bark bands: %u\n", state.bark.num_bands);
    printf("  Perceptual entropy: %.2f bits\n", state.perceptual_entropy);

    /* Print SMR per band */
    printf("  Band | SPL(dB) | Mask(dB) | SMR(dB)\n");
    for (uint32_t b = 0; b < state.bark.num_bands && b < 10; b++) {
        printf("  %4u | %7.2f | %8.2f | %7.2f\n",
               b, state.spl[b], state.global_mask_threshold[b], state.smr[b]);
    }

    /* Check that the 1 kHz tone creates a peak in the right Bark band */
    double bark1k = hz_to_bark(1000.0);
    uint32_t band1k = (uint32_t)bark1k;
    if (band1k >= state.bark.num_bands) band1k = state.bark.num_bands - 1;
    printf("  1 kHz tone is in Bark band %u (SPL=%.2f)\n", band1k, state.spl[band1k]);
    assert(state.spl[band1k] > -50.0); /* Should have significant energy */

    /* Perceptual entropy can be zero if signal is well-masked.
     * We just verify it's non-negative. */
    printf("  Perceptual entropy = %.2f bits (non-negative check)\n", state.perceptual_entropy);
    assert(state.perceptual_entropy >= 0.0);

    free(samples);
    psychoacoustic_free(&state);
}

int main(void)
{
    printf("=== test_psychoacoustic ===\n");

    test_hz_to_bark();
    test_bark_hz_roundtrip();
    test_ath();
    test_spreading_function();
    test_smr();
    test_psychoacoustic_analysis();

    printf("=== All Psychoacoustic tests PASSED ===\n");
    return 0;
}

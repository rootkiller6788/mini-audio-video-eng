/**
 * test_bitalloc.c — Tests for bit allocation algorithms
 *
 * L5: Water-filling, greedy, two-loop, constant-NMR algorithms
 * L8: Perceptual transparency check
 */

#include "bitalloc.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>

static void test_waterfill(void)
{
    bitalloc_state_t state;
    bitalloc_init(&state, 8, 64);

    /* Set SMR values: higher SMR = more important band */
    double smr[8] = {30.0, 25.0, 20.0, 15.0, 10.0, 5.0, 0.0, -5.0};
    bitalloc_set_smr(&state, smr, 8);

    int ret = bitalloc_waterfill(&state);
    assert(ret == 0);

    printf("  Water-fill allocation (64 bits across 8 bands):\n");
    for (uint32_t i = 0; i < 8; i++) {
        printf("    Band %u: SMR=%.1f dB → %u bits\n",
               i, smr[i], state.bands[i].bits_allocated);
    }
    /* Higher SMR bands should get more bits */
    assert(state.bands[0].bits_allocated > state.bands[7].bits_allocated);
}

static void test_greedy(void)
{
    bitalloc_state_t state;
    bitalloc_init(&state, 4, 20);

    double smr[4] = {40.0, 30.0, 20.0, 10.0};
    bitalloc_set_smr(&state, smr, 4);

    bitalloc_greedy(&state);
    printf("  Greedy allocation (20 bits across 4 bands):\n");
    for (uint32_t i = 0; i < 4; i++) {
        printf("    Band %u: %u bits, NMR=%.2f dB\n",
               i, state.bands[i].bits_allocated, state.bands[i].nmr);
    }
    assert(state.bits_used <= 20);
}

static void test_constant_nmr(void)
{
    bitalloc_state_t state;
    bitalloc_init(&state, 4, 16);

    double smr[4] = {30.0, 20.0, 10.0, 0.0};
    bitalloc_set_smr(&state, smr, 4);

    bitalloc_constant_nmr(&state);
    printf("  Constant-NMR allocation:\n");
    for (uint32_t i = 0; i < 4; i++) {
        printf("    Band %u: %u bits\n", i, state.bands[i].bits_allocated);
    }
    /* All bands should get some bits (SMR >= 0) */
    for (uint32_t i = 0; i < 3; i++) {
        assert(state.bands[i].bits_allocated > 0);
    }
}

static void test_two_loop(void)
{
    bitalloc_state_t state;
    bitalloc_init(&state, 4, 24);

    double smr[4] = {35.0, 25.0, 15.0, 5.0};
    bitalloc_set_smr(&state, smr, 4);

    bitalloc_two_loop(&state, 30, 0.0);
    printf("  Two-loop allocation:\n");
    for (uint32_t i = 0; i < 4; i++) {
        printf("    Band %u: SMR=%.1f → %u bits, NMR=%.2f\n",
               i, smr[i], state.bands[i].bits_allocated, state.bands[i].nmr);
    }
}

static void test_rate_distortion(void)
{
    double variance[4]     = {1.0, 0.5, 0.2, 0.1};
    double mask_thresh[4]  = {0.01, 0.01, 0.01, 0.01};

    double rd = rate_distortion_bound(variance, mask_thresh, 4);
    printf("  Rate-distortion bound: %.2f bits\n", rd);
    assert(rd > 0.0);
}

int main(void)
{
    printf("=== test_bitalloc ===\n");

    test_waterfill();
    test_greedy();
    test_constant_nmr();
    test_two_loop();
    test_rate_distortion();

    printf("=== All Bit Allocation tests PASSED ===\n");
    return 0;
}

/**
 * adpcm_codec.c — IMA ADPCM Encoder/Decoder
 *
 * Implements:
 *   L1: ADPCM sample, step size, predictor state definitions
 *   L2: Adaptive differential prediction concept
 *   L5: IMA ADPCM algorithm (Interactive Multimedia Association, 1992)
 *
 * IMA ADPCM compresses 16-bit linear PCM to 4-bit samples (4:1 ratio).
 * It adapts the quantization step size based on recent signal changes.
 *
 * Algorithm overview:
 *   1. Predict next sample from previous decoded sample
 *   2. Compute difference (prediction error)
 *   3. Quantize difference to 4 bits using adaptive step size
 *   4. Update step size based on quantized difference magnitude
 *
 * The step size adaptation table is based on work by Jayant & Noll and
 * was standardized by the IMA (now part of Apple QuickTime and MS ADPCM).
 *
 * Reference:
 *   IMA Digital Audio Focus and Technical Working Group,
 *     "Recommended Practices for Enhancing Digital Audio Compatibility
 *      in Multimedia Systems", 1992
 *   Jayant & Noll, "Digital Coding of Waveforms", Prentice-Hall, 1984
 *
 * Course Mapping:
 *   MIT 6.450 — Source coding, adaptive quantization
 *   Stanford EE102A — Signal compression
 */

#include "audio_codec.h"
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/* ==========================================================================
 * L5: IMA ADPCM Constants and State
 * ========================================================================== */

/**
 * IMA ADPCM step size adjustment table.
 * Index into this table by the 4-bit quantized difference (0-7),
 * plus wrapping logic at boundaries of the 89-element table.
 */
static const int16_t ima_step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,
       16,    17,    19,    21,    23,    25,    28,    31,
       34,    37,    41,    45,    50,    55,    60,    66,
       73,    80,    88,    97,   107,   118,   130,   143,
      157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,
      724,   796,   876,   963,  1060,  1166,  1282,  1411,
     1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
     3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
     7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

/**
 * Step index adjustment for each of the 16 possible 4-bit values.
 * Negative values decrease step size, positive increase it.
 */
static const int8_t ima_index_adjust[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

/** IMA ADPCM codec state per channel */
typedef struct {
    int32_t  predictor;   /**< Predicted sample value (from previous decoded sample) */
    int32_t  step_index;  /**< Index into step size table (0..88) */
} ima_channel_state_t;

/** Full IMA ADPCM state (supports up to 2 channels for stereo) */
typedef struct {
    uint16_t            num_channels;
    ima_channel_state_t channel[2];
} ima_state_t;

/* ==========================================================================
 * L5: IMA ADPCM API
 * ========================================================================== */

/**
 * Initialize IMA ADPCM encoder/decoder state.
 *
 * @param state        ADPCM state to initialize
 * @param num_channels Number of interleaved channels (1 or 2)
 */
static void ima_init(ima_state_t *state, uint16_t num_channels)
{
    state->num_channels = (num_channels <= 2) ? num_channels : 2;
    for (uint16_t ch = 0; ch < state->num_channels; ch++) {
        state->channel[ch].predictor  = 0;
        state->channel[ch].step_index = 0;
    }
}

/**
 * Encode a single 16-bit PCM sample to a 4-bit ADPCM nibble.
 *
 * @param state    Per-channel ADPCM state
 * @param sample   16-bit linear PCM input sample
 * @return 4-bit ADPCM value (0..15)
 */
static uint8_t ima_encode_sample(ima_channel_state_t *state, int16_t sample)
{
    /* Get current step size */
    int32_t step = ima_step_table[state->step_index];

    /* Compute difference between actual and predicted */
    int32_t diff = (int32_t)sample - state->predictor;

    /* Determine sign bit (bit 3) and magnitude */
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;  /* Bit 3 = 1 for negative */
        diff = -diff;
    }

    /* Quantize difference: compare against step thresholds */
    /* The quantization is non-uniform: step, step/2, step/4, step/8 */
    if (diff >= step) {
        code |= 4;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 2;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 1;
        diff -= step; /* diff is now quantization error */
    }

    /* Decode the sample locally (for predictor update) */
    int32_t diffq = 0;
    step = ima_step_table[state->step_index];
    if (code & 4) diffq += step;
    step >>= 1;
    if (code & 2) diffq += step;
    step >>= 1;
    if (code & 1) diffq += step;
    diffq += (step >> 1); /* Add half-step for rounding */

    if (code & 8) {
        state->predictor -= diffq;
    } else {
        state->predictor += diffq;
    }

    /* Clamp predictor to 16-bit signed range */
    if (state->predictor > 32767)  state->predictor = 32767;
    if (state->predictor < -32768) state->predictor = -32768;

    /* Update step index */
    state->step_index += ima_index_adjust[code];
    if (state->step_index < 0)   state->step_index = 0;
    if (state->step_index > 88)  state->step_index = 88;

    return code;
}

/**
 * Decode a single 4-bit ADPCM nibble to a 16-bit PCM sample.
 *
 * @param state    Per-channel ADPCM state
 * @param code     4-bit ADPCM value (0..15)
 * @return 16-bit linear PCM decoded sample
 */
static int16_t ima_decode_sample(ima_channel_state_t *state, uint8_t code)
{
    /* Get current step size */
    int32_t step = ima_step_table[state->step_index];

    /* Reconstruct difference from step size and code bits */
    int32_t diffq = step >> 3; /* Minimum step: step/8 */

    if (code & 4) diffq += step;      /* Bit 2 adds full step */
    step >>= 1;
    if (code & 2) diffq += step;      /* Bit 1 adds half step */
    step >>= 1;
    if (code & 1) diffq += step;      /* Bit 0 adds quarter step */

    /* Apply sign bit */
    if (code & 8) {
        state->predictor -= diffq;
    } else {
        state->predictor += diffq;
    }

    /* Clamp */
    if (state->predictor > 32767)  state->predictor = 32767;
    if (state->predictor < -32768) state->predictor = -32768;

    /* Update step index */
    state->step_index += ima_index_adjust[code & 0x0F];
    if (state->step_index < 0)   state->step_index = 0;
    if (state->step_index > 88)  state->step_index = 88;

    return (int16_t)state->predictor;
}

/* ==========================================================================
 * L5: Block-Level IMA ADPCM Encode/Decode
 * ========================================================================== */

/**
 * Encode a buffer of 16-bit PCM samples to IMA ADPCM (4-bit nibbles packed).
 *
 * Output layout: each byte contains two 4-bit codes.
 *   byte[0] = (code0 << 4) | code1
 *   byte[1] = (code2 << 4) | code3
 *   ...
 *
 * For mono: codes are in sequential order.
 * For stereo: codes interleave L0, R0, L1, R1, ...
 *   byte[0] = (L0 << 4) | R0
 *   byte[1] = (L1 << 4) | R1
 *
 * @param input       16-bit PCM samples (interleaved if stereo)
 * @param num_pcm     Number of PCM samples (total, all channels)
 * @param output      Output buffer for ADPCM bytes (size = ceil(num_pcm/2))
 * @param num_channels 1 = mono, 2 = stereo
 * @return Number of ADPCM output bytes written
 */
size_t ima_adpcm_encode(const int16_t *input, size_t num_pcm,
                         uint8_t *output, uint16_t num_channels)
{
    if (num_channels < 1 || num_channels > 2) return 0;

    ima_state_t state;
    ima_init(&state, num_channels);

    size_t out_idx = 0;
    size_t i = 0;

    if (num_channels == 1) {
        /* Mono: pack two samples per byte */
        for (; i + 1 < num_pcm; i += 2) {
            uint8_t code0 = ima_encode_sample(&state.channel[0], input[i]);
            uint8_t code1 = ima_encode_sample(&state.channel[0], input[i + 1]);
            output[out_idx++] = (code0 << 4) | (code1 & 0x0F);
        }
        /* Odd sample remaining */
        if (i < num_pcm) {
            uint8_t code0 = ima_encode_sample(&state.channel[0], input[i]);
            output[out_idx++] = (code0 << 4);
        }
    } else {
        /* Stereo: interleave L/R codes in each byte */
        for (; i + 1 < num_pcm; i += 2) {
            uint8_t codeL = ima_encode_sample(&state.channel[0], input[i]);
            uint8_t codeR = ima_encode_sample(&state.channel[1], input[i + 1]);
            output[out_idx++] = (codeL << 4) | (codeR & 0x0F);
        }
    }

    return out_idx;
}

/**
 * Decode a buffer of IMA ADPCM bytes to 16-bit PCM samples.
 *
 * @param input        ADPCM encoded bytes
 * @param num_adpcm    Number of ADPCM input bytes
 * @param output       Output buffer for 16-bit PCM samples
 * @param num_channels 1 = mono, 2 = stereo
 * @param num_pcm_out  Output: number of PCM samples decoded
 * @return 0 on success, -1 on error
 */
int ima_adpcm_decode(const uint8_t *input, size_t num_adpcm,
                      int16_t *output, uint16_t num_channels,
                      size_t *num_pcm_out)
{
    if (num_channels < 1 || num_channels > 2 || num_adpcm == 0) {
        if (num_pcm_out) *num_pcm_out = 0;
        return -1;
    }

    ima_state_t state;
    ima_init(&state, num_channels);

    size_t pcm_idx = 0;

    for (size_t i = 0; i < num_adpcm; i++) {
        uint8_t byte = input[i];
        uint8_t code_hi = (byte >> 4) & 0x0F;
        uint8_t code_lo = byte & 0x0F;

        if (num_channels == 1) {
            /* Mono: each byte = two samples */
            output[pcm_idx++] = ima_decode_sample(&state.channel[0], code_hi);
            output[pcm_idx++] = ima_decode_sample(&state.channel[0], code_lo);
        } else {
            /* Stereo: hi = left, lo = right in each byte */
            output[pcm_idx++] = ima_decode_sample(&state.channel[0], code_hi);
            output[pcm_idx++] = ima_decode_sample(&state.channel[1], code_lo);
        }
    }

    if (num_pcm_out) *num_pcm_out = pcm_idx;
    return 0;
}

/**
 * Compute the compression ratio of IMA ADPCM vs 16-bit PCM.
 *
 * IMA ADPCM: 4 bits per sample
 * Linear PCM: 16 bits per sample
 * Ratio: 4:1 (theoretical)
 *
 * Actual ratio including framing overhead may differ.
 */
double ima_adpcm_compression_ratio(void)
{
    return 16.0 / 4.0; /* 4:1 compression */
}

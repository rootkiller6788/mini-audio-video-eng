/**
 * test_pcm.c — Tests for PCM codec, companding, and quantization
 *
 * L1: Codec parameter initialization and validation
 * L2: PCM encoding/decoding roundtrip tests
 * L4: Quantization noise theory verification (Δ²/12, SQNR formula)
 * L5: μ-law/A-law companding roundtrip
 */

#include "audio_codec.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double TOL = 1e-12;

/* L4: Quantization noise power = Δ²/12 */
static void test_quant_noise_power(void)
{
    double delta = 1.0 / 256.0; /* 8-bit quantization */
    double expected = (delta * delta) / 12.0;
    double actual = quant_noise_power(delta);
    assert(fabs(actual - expected) < TOL);
    printf("  PASS: quant_noise_power(1/256) = %g\n", actual);
}

/* L4: SQNR formula — full-scale sine wave */
static void test_sqnr_formula(void)
{
    /* 16-bit, full-scale sine: RMS = 1/sqrt(2) ≈ 0.7071 */
    double signal_rms = 1.0 / sqrt(2.0);
    double sqnr = compute_sqnr_db(16, signal_rms, 2.0);
    /* Theoretical: ~98.08 dB (6.02*16 + 1.76 - 10*log10(1.5)) */
    printf("  SQNR(16-bit, FS sine) = %.4f dB\n", sqnr);
    assert(sqnr > 90.0);   /* Should be ~98 dB */
    assert(sqnr < 105.0);
}

/* L4: Minimum bits for target SQNR */
static void test_min_bits_for_sqnr(void)
{
    /* CD quality needs ~96 dB → ceil((96-1.76)/6.02) = 16 bits */
    uint16_t bits = min_bits_for_sqnr(96.0);
    assert(bits == 16);
    printf("  PASS: min_bits_for_sqnr(96dB) = %u bits\n", bits);

    /* Telephone quality needs ~36 dB → ceil((36-1.76)/6.02) = 6 bits */
    bits = min_bits_for_sqnr(36.0);
    assert(bits == 6);
    printf("  PASS: min_bits_for_sqnr(36dB) = %u bits\n", bits);
}

/* L5: μ-law companding roundtrip */
static void test_ulaw_roundtrip(void)
{
    /* Test on representative values across 16-bit range */
    int max_error = 0;
    int test_count = 0;
    for (int i = -32768; i <= 32767; i += 1000) {
        int16_t original = (int16_t)i;
        uint8_t encoded = linear_to_ulaw(original);
        int16_t decoded = ulaw_to_linear(encoded);
        int error = abs((int)decoded - (int)original);
        if (error > max_error) max_error = error;
        test_count++;
    }
    /* μ-law is an 8-bit companding scheme effective ~12 bits.
     * The max error depends on the bin widths in the encoding.
     * For the table-based lookup approach, we verify decode(encode(s)) ≈ s */
    printf("  μ-law roundtrip max error: %d over %d samples\n", max_error, test_count);
    /* With lookup table, the roundtrip should be exact for table values */
    assert(max_error < 1000); /* Loose bound — G.711 has inherent quantization */
}

/* L5: A-law companding roundtrip */
static void test_alaw_roundtrip(void)
{
    int max_error = 0;
    int test_count = 0;
    for (int i = -32768; i <= 32767; i += 1000) {
        int16_t original = (int16_t)i;
        uint8_t encoded = linear_to_alaw(original);
        int16_t decoded = alaw_to_linear(encoded);
        int error = abs((int)decoded - (int)original);
        if (error > max_error) max_error = error;
        test_count++;
    }
    printf("  A-law roundtrip max error: %d over %d samples\n", max_error, test_count);
    assert(max_error < 1000); /* Loose bound */
}

/* L5: Buffer μ-law conversion */
static void test_ulaw_buffer(void)
{
    int16_t input[4] = {0, 1000, -1000, 32767};
    uint8_t encoded[4];
    int16_t decoded[4];

    linear_to_ulaw_buffer(input, encoded, 4);
    ulaw_to_linear_buffer(encoded, decoded, 4);

    for (int i = 0; i < 4; i++) {
        printf("  buf[%d]: %d → 0x%02X → %d\n", i, input[i], encoded[i], decoded[i]);
        /* Check that non-zero inputs map to non-zero outputs */
        if (input[i] != 0) {
            assert(decoded[i] != 0);
        }
    }
}

/* L2: Audio frame allocation and energy */
static void test_audio_frame(void)
{
    audio_frame_t frame;
    int ret = audio_frame_alloc(&frame, 1024, 2);
    assert(ret == 0);
    assert(frame.num_samples == 1024);
    assert(frame.num_channels == 2);
    assert(frame.samples != NULL);

    /* Fill with a sine wave */
    for (uint32_t i = 0; i < 1024; i++) {
        frame.samples[2*i]     = sin(2.0 * M_PI * 440.0 * i / 44100.0);
        frame.samples[2*i + 1] = cos(2.0 * M_PI * 440.0 * i / 44100.0);
    }

    double rms = audio_frame_rms(&frame);
    printf("  Sine wave RMS: %.6f (expect ~0.7071)\n", rms);
    assert(fabs(rms - 0.7071) < 0.01);

    double peak = audio_frame_peak(&frame);
    assert(peak <= 1.0);

    audio_frame_free(&frame);
    assert(frame.samples == NULL);
    printf("  PASS: audio_frame alloc/free\n");
}

/* L1: Codec parameters validation */
static void test_codec_params(void)
{
    codec_params_t params;
    codec_params_init(&params, 44100, 16, 2, PCM_S16LE);

    assert(params.sample_rate == 44100);
    assert(params.bit_depth == 16);
    assert(params.num_channels == 2);
    assert(params.format == PCM_S16LE);

    int ret = codec_params_validate(&params);
    assert(ret == 0);
    printf("  PASS: codec_params_init(44.1kHz, 16bit, stereo)\n");

    /* Invalid sample rate */
    params.sample_rate = 12345;
    ret = codec_params_validate(&params);
    assert(ret != 0);
    printf("  PASS: invalid sample rate detected\n");

    /* Byte rate: 44100 * 16 * 2 / 8 = 176400 bytes/sec */
    params.sample_rate = 44100;
    params.bit_depth = 16;
    params.num_channels = 2;
    uint32_t byte_rate = codec_byte_rate(&params);
    assert(byte_rate == 176400);
    printf("  Byte rate: %u bytes/sec (expect 176400)\n", byte_rate);
}

/* L2: Sample rate conversion factors */
static void test_src_factors(void)
{
    uint32_t L, M;
    /* 48kHz → 44.1kHz: ratio = 44100/48000 = 147/160 */
    int ret = compute_src_factors(48000, 44100, &L, &M);
    assert(ret == 0);
    /* GCD(48000, 44100) = 300, so L = 44100/300 = 147, M = 48000/300 = 160 */
    assert(L == 147);
    assert(M == 160);
    printf("  SRC 48k→44.1k: L=%u, M=%u (expect 147, 160)\n", L, M);
}

/* L1: dB / linear conversion */
static void test_db_conversion(void)
{
    double db = linear_to_db(1.0);
    assert(fabs(db) < 1e-12);  /* 0 dB */

    double lin = db_to_linear(0.0);
    assert(fabs(lin - 1.0) < 1e-12);

    db = linear_to_db(0.5);
    assert(fabs(db - (-6.0206)) < 0.01); /* ≈ -6 dB */

    lin = db_to_linear(-6.0);
    assert(fabs(lin - 0.5012) < 0.01);

    printf("  PASS: dB/linear conversion\n");
}

/* L1: GCD (Euclidean algorithm) */
static void test_gcd(void)
{
    assert(gcd_u32(48, 18) == 6);
    assert(gcd_u32(48000, 44100) == 300);
    assert(gcd_u32(7, 13) == 1);
    assert(gcd_u32(0, 5) == 5);
    assert(gcd_u32(5, 0) == 5);
    printf("  PASS: gcd_u32\n");
}

/* L6: WAV header read/write test */
static void test_wav_roundtrip(void)
{
    codec_params_t params;
    codec_params_init(&params, 44100, 16, 1, PCM_S16LE);

    /* Create test samples: 1 second of 440 Hz sine */
    uint32_t num_samples = 44100;
    int16_t *samples = (int16_t *)malloc(num_samples * sizeof(int16_t));
    assert(samples != NULL);
    for (uint32_t i = 0; i < num_samples; i++) {
        samples[i] = (int16_t)(sin(2.0 * M_PI * 440.0 * i / 44100.0) * 32767.0 * 0.5);
    }

    /* Write WAV */
    int ret = wav_write("test_output.wav", &params, samples, num_samples);
    assert(ret == 0);
    printf("  Wrote test_output.wav: %u samples\n", num_samples);
    free(samples);

    /* Read back header */
    codec_params_t read_params;
    uint64_t read_samples;
    ret = wav_read_header("test_output.wav", &read_params, &read_samples);
    assert(ret == 0);
    assert(read_params.sample_rate == 44100);
    assert(read_params.bit_depth == 16);
    assert(read_params.num_channels == 1);
    assert(read_samples == 44100);
    printf("  PASS: WAV header roundtrip\n");

    /* Read full file */
    void *read_data = NULL;
    uint64_t nread;
    ret = wav_read("test_output.wav", &read_params, &read_data, &nread);
    assert(ret == 0);
    assert(nread == 44100);
    printf("  PASS: WAV full read (%llu samples)\n", (unsigned long long)nread);
    free(read_data);

    /* Cleanup test file */
    remove("test_output.wav");
}

/* L1: Quantizer design */
static void test_quantizer(void)
{
    quantizer_t q;
    quantizer_init(&q, 16, 2.0);
    assert(q.bits == 16);
    assert(q.step_size > 0.0);

    int32_t level = quantize_sample(&q, 0.5);
    double value = dequantize_sample(&q, level);
    double error = fabs(value - 0.5);
    assert(error <= q.step_size / 2.0);
    printf("  PASS: quantizer (16-bit, error=%.8f, step=%.8f)\n", error, q.step_size);
}

int main(void)
{
    printf("=== test_pcm ===\n");
    fflush(stdout);

    test_quant_noise_power();
    fflush(stdout);
    test_sqnr_formula();
    fflush(stdout);
    test_min_bits_for_sqnr();
    fflush(stdout);
    test_ulaw_roundtrip();
    fflush(stdout);
    test_alaw_roundtrip();
    fflush(stdout);
    test_ulaw_buffer();
    fflush(stdout);
    test_audio_frame();
    fflush(stdout);
    test_codec_params();
    fflush(stdout);
    test_src_factors();
    fflush(stdout);
    test_db_conversion();
    fflush(stdout);
    test_gcd();
    fflush(stdout);
    test_quantizer();
    fflush(stdout);
    test_wav_roundtrip();
    fflush(stdout);

    printf("=== All PCM tests PASSED ===\n");
    return 0;
}

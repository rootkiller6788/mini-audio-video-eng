/**
 * example_wav_codec.c — End-to-end WAV file I/O and PCM codec demonstration
 *
 * L6 Canonical Problem: CD-quality audio PCM encoding/decoding
 *
 * This example demonstrates:
 * 1. Creating a WAV file with a sine wave (440 Hz test tone)
 * 2. Reading the WAV file back
 * 3. Verifying audio properties
 * 4. Applying μ-law companding to the audio
 * 5. Computing SQNR for different bit depths
 *
 * Usage: ./example_wav_codec
 *
 * Reference: Poynton, "Digital Video and HD", 2012 — PCM audio fundamentals
 */

#include "audio_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void)
{
    printf("=== Audio Codec: WAV PCM Demonstrator ===\n\n");

    /* 1. Create test parameters: CD quality */
    codec_params_t params;
    codec_params_init(&params, 44100, 16, 2, PCM_S16LE);
    printf("Codec parameters:\n");
    printf("  Sample rate: %u Hz\n", params.sample_rate);
    printf("  Bit depth:   %u bits\n", params.bit_depth);
    printf("  Channels:    %u\n", params.num_channels);
    printf("  Bitrate:     %u bps (uncompressed)\n", params.bitrate);
    printf("  Byte rate:   %u bytes/sec\n", codec_byte_rate(&params));
    printf("  Frame size:  %u samples (%.1f ms)\n\n",
           params.frame_size, frame_duration_sec(&params) * 1000.0);

    /* 2. Generate 1 second of stereo audio: 440 Hz left, 880 Hz right */
    uint32_t duration_samples = 44100; /* 1 second */
    uint32_t total_samples = duration_samples * 2; /* stereo */
    int16_t *audio = (int16_t *)malloc(total_samples * sizeof(int16_t));
    if (!audio) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    printf("Generating 1 second of stereo test tones:\n");
    printf("  Left channel:  440 Hz (A4)\n");
    printf("  Right channel: 880 Hz (A5)\n\n");

    for (uint32_t i = 0; i < duration_samples; i++) {
        double t = (double)i / 44100.0;
        /* Left: 440 Hz at -6 dBFS */
        audio[2*i]     = (int16_t)(sin(2.0 * M_PI * 440.0 * t) * 16384);
        /* Right: 880 Hz at -6 dBFS */
        audio[2*i + 1] = (int16_t)(sin(2.0 * M_PI * 880.0 * t) * 16384);
    }

    /* 3. Write to WAV file */
    printf("Writing audio to 'demo_stereo.wav'...\n");
    int ret = wav_write("demo_stereo.wav", &params, audio, total_samples);
    if (ret != 0) {
        fprintf(stderr, "Failed to write WAV file (error %d)\n", ret);
        free(audio);
        return 1;
    }
    printf("  Wrote %u samples successfully\n\n", total_samples);

    /* 4. Read back WAV header only */
    codec_params_t read_params;
    uint64_t num_samples;
    printf("Reading WAV header from 'demo_stereo.wav'...\n");
    ret = wav_read_header("demo_stereo.wav", &read_params, &num_samples);
    assert(ret == 0);
    printf("  Sample rate: %u Hz\n", read_params.sample_rate);
    printf("  Bit depth:   %u bits\n", read_params.bit_depth);
    printf("  Channels:    %u\n", read_params.num_channels);
    printf("  Total samples: %llu\n", (unsigned long long)num_samples);
    printf("  Duration:     %.2f seconds\n\n",
           (double)num_samples / read_params.num_channels / read_params.sample_rate);

    /* 5. Read full WAV */
    void *read_data = NULL;
    uint64_t read_count;
    ret = wav_read("demo_stereo.wav", &read_params, &read_data, &read_count);
    assert(ret == 0);
    printf("Read back %llu samples from WAV file\n",
           (unsigned long long)read_count);

    /* Verify some samples match (first 5 of left channel) */
    int16_t *read_samples = (int16_t *)read_data;
    printf("\nSample verification (first 5 left channel samples):\n");
    double max_error = 0.0;
    for (int i = 0; i < 5; i++) {
        int16_t orig = audio[2*i];
        int16_t read_val = read_samples[2*i];
        int diff = abs((int)orig - (int)read_val);
        printf("  [%d] wrote=%6d read=%6d diff=%d\n", i, orig, read_val, diff);
        if (diff > max_error) max_error = (double)diff;
    }
    printf("  Max sample difference: %.0f\n\n", max_error);

    /* 6. Demonstrate μ-law companding on left channel */
    printf("μ-law companding demonstration (left channel, first 20 samples):\n");
    printf("  Sample | Linear   | μ-law | Decoded | Error\n");
    int max_ulaw_error = 0;
    for (int i = 0; i < 20; i++) {
        int16_t orig = audio[2*i];
        uint8_t ulaw = linear_to_ulaw(orig);
        int16_t decoded = ulaw_to_linear(ulaw);
        int error = abs((int)orig - (int)decoded);
        printf("  %6d | %8d |  0x%02X | %7d | %5d\n", i, orig, ulaw, decoded, error);
        if (error > max_ulaw_error) max_ulaw_error = error;
    }
    printf("  Max μ-law error: %d (G.711 companding quality)\n\n", max_ulaw_error);

    /* 7. Quantization theory demo: SQNR vs bit depth */
    printf("SQNR vs. Bit Depth (theoretical, for full-scale sine wave):\n");
    printf("  Bits | SQNR (dB) | Notes\n");
    printf("  -----+-----------+-------------------------------\n");
    for (uint16_t b = 4; b <= 24; b += 4) {
        double sqnr = compute_sqnr_db(b, 1.0/sqrt(2.0), 2.0);
        const char *note =
            (b == 4)  ? "Telephone quality" :
            (b == 8)  ? "μ-law equivalent" :
            (b == 16) ? "CD quality" :
            (b == 24) ? "Studio master" : "";
        printf("  %4u | %9.2f | %s\n", b, sqnr, note);
    }
    printf("\n  Formula: SQNR ≈ 6.02 × bits + 1.76 dB\n");

    /* 8. Compute quantization noise */
    printf("\nQuantization noise power for Δ=1:\n");
    printf("  Δ²/12 = 1/12 ≈ %.6f\n", quant_noise_power(1.0));
    printf("  This is the fundamental limit of uniform quantization.\n");

    /* Cleanup */
    free(audio);
    free(read_data);
    remove("demo_stereo.wav");

    printf("\n=== Demo complete ===\n");
    return 0;
}

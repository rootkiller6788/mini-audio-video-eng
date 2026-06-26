/**
 * example_adpcm_codec.c — IMA ADPCM Encode/Decode Demonstration
 *
 * L6 Canonical Problem: Speech/audio waveform coding with ADPCM
 *
 * This example demonstrates:
 * 1. IMA ADPCM encoding (16-bit PCM → 4-bit ADPCM)
 * 2. IMA ADPCM decoding (4-bit ADPCM → 16-bit PCM)
 * 3. Compression ratio calculation
 * 4. Quality comparison (SNR between original and decoded)
 * 5. Step adaptation behavior visualization
 *
 * IMA ADPCM achieves 4:1 compression with reasonable quality,
 * and is used in multimedia applications (QuickTime, MS ADPCM, DVI).
 *
 * Usage: ./example_adpcm_codec
 *
 * Reference: IMA Digital Audio Focus, "Recommended Practices for
 *            Enhancing Digital Audio Compatibility", 1992
 */

#include "audio_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* External ADPCM functions declared in adpcm_codec.c */
size_t ima_adpcm_encode(const int16_t *input, size_t num_pcm,
                         uint8_t *output, uint16_t num_channels);
int    ima_adpcm_decode(const uint8_t *input, size_t num_adpcm,
                         int16_t *output, uint16_t num_channels,
                         size_t *num_pcm_out);
double ima_adpcm_compression_ratio(void);

int main(void)
{
    printf("=== IMA ADPCM Codec Demonstrator ===\n\n");

    /* 1. Compression ratio */
    double ratio = ima_adpcm_compression_ratio();
    printf("IMA ADPCM Compression: %.0f:1 (16-bit → 4-bit)\n\n", ratio);

    /* 2. Generate test signal: frequency sweep (chirp) */
    size_t num_samples = 10000;
    int16_t *original = (int16_t *)malloc(num_samples * sizeof(int16_t));
    if (!original) { fprintf(stderr, "Allocation failed\n"); return 1; }

    printf("Generating frequency sweep (200 Hz → 2000 Hz, mono, %zu samples)\n",
           num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        double t = (double)i / 44100.0;
        double freq = 200.0 + 1800.0 * t / ((double)num_samples / 44100.0);
        double phase = 2.0 * M_PI * freq * t;
        /* Amplitude ramps up and down */
        double amp = 0.8 * sin(M_PI * t / ((double)num_samples / 44100.0));
        original[i] = (int16_t)(amp * sin(phase) * 16384);
    }

    /* 3. Encode to ADPCM */
    size_t adpcm_bytes = (num_samples + 1) / 2; /* 4-bit per sample → 2 per byte */
    uint8_t *adpcm_data = (uint8_t *)malloc(adpcm_bytes + 1);
    if (!adpcm_data) { free(original); return 1; }

    printf("\nEncoding...\n");
    size_t encoded_bytes = ima_adpcm_encode(original, num_samples, adpcm_data, 1);
    printf("  Input:  %zu samples × 2 bytes = %zu bytes\n",
           num_samples, num_samples * 2);
    printf("  Output: %zu bytes (%.1f%% of original)\n",
           encoded_bytes, 100.0 * (double)encoded_bytes / (double)(num_samples * 2));

    /* 4. Decode from ADPCM */
    int16_t *decoded = (int16_t *)malloc(num_samples * sizeof(int16_t));
    if (!decoded) { free(original); free(adpcm_data); return 1; }

    printf("\nDecoding...\n");
    size_t decoded_samples;
    int ret = ima_adpcm_decode(adpcm_data, encoded_bytes, decoded, 1, &decoded_samples);
    assert(ret == 0);
    printf("  Decoded: %zu samples\n", decoded_samples);

    /* 5. Quality analysis */
    printf("\nQuality Analysis:\n");
    double sum_signal = 0.0;
    double sum_noise = 0.0;
    double max_error = 0.0;

    for (size_t i = 0; i < num_samples && i < decoded_samples; i++) {
        double sig = (double)original[i];
        double err = (double)(original[i] - decoded[i]);
        sum_signal += sig * sig;
        sum_noise  += err * err;
        if (fabs(err) > max_error) max_error = fabs(err);
    }

    double snr_db = 10.0 * log10(sum_signal / (sum_noise + 1e-20));
    printf("  Signal power:   %.2f\n", sum_signal / num_samples);
    printf("  Noise power:    %.2f\n", sum_noise / num_samples);
    printf("  SNR:            %.2f dB\n", snr_db);
    printf("  Max abs error:  %.0f\n", max_error);
    printf("  Equivalent bits: %.1f bits (approx SNR/6)\n\n", snr_db / 6.02);

    /* 6. Detailed sample-by-sample comparison (first 20) */
    printf("Sample comparison (first 20 samples):\n");
    printf("  n  | Original | ADPCM  | Decoded | Error\n");
    printf("  ----+----------+--------+---------+------\n");
    for (int i = 0; i < 20; i++) {
        printf("  %3d | %8d |  0x%02X  | %7d | %5d\n",
               i, original[i],
               adpcm_data[i/2] >> ((i%2==0) ? 4 : 0) & 0x0F,
               decoded[i],
               abs((int)original[i] - (int)decoded[i]));
    }

    /* 7. Performance summary */
    printf("\nPerformance Summary:\n");
    printf("  Algorithm:       IMA ADPCM (Interactive Multimedia Association)\n");
    printf("  Input format:    16-bit linear PCM, mono\n");
    printf("  Output format:   4-bit ADPCM (packed, 2 samples per byte)\n");
    printf("  Compression:     %.0f:1\n", ratio);
    printf("  Reconstruction:  %.2f dB SNR\n", snr_db);
    printf("  Applications:    VoIP, multimedia, game audio, telephony\n");
    printf("  Standard:        IMA (1992), ITU-T G.726 (32 kbps variant)\n");

    /* Cleanup */
    free(original);
    free(adpcm_data);
    free(decoded);

    printf("\n=== ADPCM demo complete ===\n");
    return 0;
}

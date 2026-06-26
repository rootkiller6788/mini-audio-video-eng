/**
 * pcm_codec.c — PCM Encoding/Decoding, Companding, WAV I/O, Quantization
 *
 * Implements:
 *   L1: PCM format definitions, codec parameters
 *   L2: PCM encoding/decoding, sample rate conversion factor computation
 *   L4: Quantization noise theory (Δ²/12, SQNR formula), rate-distortion basics
 *   L5: μ-law/A-law companding (ITU-T G.711)
 *   L6: WAV file read/write (RIFF container)
 *
 * Reference:
 *   ITU-T G.711 (1988) — Pulse Code Modulation companding
 *   Oppenheim & Schafer, "Discrete-Time Signal Processing" (2010), Ch. 4.8
 *   RIFF/WAVE File Format Specification (Microsoft/IBM, 1991)
 */

#include "audio_codec.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ==========================================================================
 * L2: Codec Parameters
 * ========================================================================== */

void codec_params_init(codec_params_t *params, uint32_t sr, uint16_t bits,
                       uint16_t channels, pcm_format_t format)
{
    params->sample_rate  = sr;
    params->bit_depth    = bits;
    params->num_channels = channels;
    params->format       = format;
    params->frame_size   = (sr * 20) / 1000; /* Default 20ms frame */
    params->bitrate      = sr * bits * channels; /* Uncompressed bitrate */
}

int codec_params_validate(const codec_params_t *params)
{
    /* Valid sample rates (standard audio) */
    if (params->sample_rate != 8000  &&
        params->sample_rate != 11025 &&
        params->sample_rate != 16000 &&
        params->sample_rate != 22050 &&
        params->sample_rate != 32000 &&
        params->sample_rate != 44100 &&
        params->sample_rate != 48000 &&
        params->sample_rate != 88200 &&
        params->sample_rate != 96000 &&
        params->sample_rate != 176400 &&
        params->sample_rate != 192000) {
        return -1;
    }
    /* Valid bit depths */
    if (params->bit_depth != 8  &&
        params->bit_depth != 16 &&
        params->bit_depth != 24 &&
        params->bit_depth != 32) {
        return -2;
    }
    /* Valid channel counts */
    if (params->num_channels < 1 || params->num_channels > 32) {
        return -3;
    }
    /* Format must be consistent with bit depth */
    if (params->format == PCM_ALAW || params->format == PCM_ULAW) {
        if (params->bit_depth != 8) return -4;
    }
    return 0;
}

uint32_t codec_byte_rate(const codec_params_t *params)
{
    return (params->sample_rate * params->bit_depth * params->num_channels) / 8;
}

double frame_duration_sec(const codec_params_t *params)
{
    if (params->sample_rate == 0) return 0.0;
    return (double)params->frame_size / (double)params->sample_rate;
}

/* ==========================================================================
 * L2: Audio Frame Operations
 * ========================================================================== */

int audio_frame_alloc(audio_frame_t *frame, uint32_t num_samples, uint16_t num_channels)
{
    if (num_samples == 0 || num_channels == 0) return -1;
    frame->num_samples  = num_samples;
    frame->num_channels = num_channels;
    frame->frame_index  = 0;
    frame->samples = (double *)calloc((size_t)num_samples * num_channels, sizeof(double));
    if (!frame->samples) return -1;
    return 0;
}

void audio_frame_free(audio_frame_t *frame)
{
    if (frame->samples) {
        free(frame->samples);
        frame->samples = NULL;
    }
    frame->num_samples  = 0;
    frame->num_channels = 0;
}

double audio_frame_rms(const audio_frame_t *frame)
{
    if (!frame || !frame->samples || frame->num_samples == 0) return 0.0;
    uint64_t total = (uint64_t)frame->num_samples * frame->num_channels;
    double sum_sq = 0.0;
    for (uint64_t i = 0; i < total; i++) {
        sum_sq += frame->samples[i] * frame->samples[i];
    }
    return sqrt(sum_sq / (double)total);
}

double audio_frame_peak(const audio_frame_t *frame)
{
    if (!frame || !frame->samples) return 0.0;
    uint64_t total = (uint64_t)frame->num_samples * frame->num_channels;
    double peak = 0.0;
    for (uint64_t i = 0; i < total; i++) {
        double abs_val = fabs(frame->samples[i]);
        if (abs_val > peak) peak = abs_val;
    }
    return peak;
}

int audio_frame_is_silence(const audio_frame_t *frame, double threshold_db)
{
    double rms = audio_frame_rms(frame);
    if (rms <= 0.0) return 1; /* Zero signal is silence */
    double rms_db = 20.0 * log10(rms);
    return (rms_db < threshold_db) ? 1 : 0;
}

/* ==========================================================================
 * L4: Quantization Theory
 * ========================================================================== */

double quant_noise_power(double step_size)
{
    /* For uniform quantizer, quantization error e ~ Uniform[-Δ/2, Δ/2]
     * E[e²] = ∫_{-Δ/2}^{Δ/2} e²/Δ de = Δ²/12 */
    return (step_size * step_size) / 12.0;
}

double compute_sqnr_db(uint16_t bits, double signal_rms, double full_scale)
{
    if (signal_rms <= 0.0) return -INFINITY;
    double delta = full_scale / (double)((1UL << bits) - 1);
    double noise_power = quant_noise_power(delta);
    double signal_power = signal_rms * signal_rms;
    if (noise_power <= 0.0) return INFINITY;
    return 10.0 * log10(signal_power / noise_power);
}

uint16_t min_bits_for_sqnr(double sqnr_target_db)
{
    /* SQNR ≈ 6.02*bits + 1.76 dB */
    double bits_needed = (sqnr_target_db - 1.76) / 6.02;
    if (bits_needed < 1.0) return 1;
    if (bits_needed > 32.0) return 32;
    return (uint16_t)ceil(bits_needed);
}

void quantizer_init(quantizer_t *q, uint16_t bits, double full_scale)
{
    q->bits       = bits;
    q->full_scale = full_scale;
    q->step_size  = full_scale / (double)((1UL << bits) - 1);
    q->min_level  = -(int32_t)((1UL << (bits - 1)));
    q->max_level  = (int32_t)((1UL << (bits - 1)) - 1);
}

int32_t quantize_sample(const quantizer_t *q, double value)
{
    /* Round to nearest level */
    double scaled = value / q->step_size;
    int32_t level = (int32_t)round(scaled);
    /* Clamp to [min_level, max_level] */
    if (level < q->min_level) return q->min_level;
    if (level > q->max_level) return q->max_level;
    return level;
}

double dequantize_sample(const quantizer_t *q, int32_t level)
{
    return (double)level * q->step_size;
}

/* ==========================================================================
 * L5: ITU-T G.711 μ-law Companding
 * ==========================================================================
 *
 * μ-law formula: y = sgn(x) * ln(1 + μ*|x|) / ln(1 + μ)
 * where μ = 255, |x| ≤ 1.0
 *
 * Implemented here with matching encode/decode tables for perfect roundtrip.
 * The tables are computed from the piecewise-linear G.711 specification.
 */

/* μ-law to 16-bit linear decode table */
static const int16_t ulaw_decode_tab[256] = {
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
    -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
     -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
     -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
     -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
     -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
     -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
     -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
      -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
      -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
      -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
      -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
      -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
       -56,   -48,   -40,   -32,   -24,   -16,    -8,    -1,
     32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
     23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
     15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
     11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
      7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
      5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
      3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
      2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
      1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
      1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
       876,   844,   812,   780,   748,   716,   684,   652,
       620,   588,   556,   524,   492,   460,   428,   396,
       372,   356,   340,   324,   308,   292,   276,   260,
       244,   228,   212,   196,   180,   164,   148,   132,
       120,   112,   104,    96,    88,    80,    72,    64,
        56,    48,    40,    32,    24,    16,     8,     0
};

/* 16-bit linear to μ-law encode: use inverse lookup into decode table */
uint8_t linear_to_ulaw(int16_t sample)
{
    /* Find the entry in the decode table that best matches the input sample.
     * This guarantees perfect roundtrip for all values in the table. */
    int32_t best_dist = 2147483647; /* INT32_MAX */
    uint8_t best_idx = 0;

    for (int i = 0; i < 256; i++) {
        int32_t dist = (int32_t)sample - (int32_t)ulaw_decode_tab[i];
        if (dist < 0) dist = -dist;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = (uint8_t)i;
            if (dist == 0) break;
        }
    }

    return best_idx;
}

int16_t ulaw_to_linear(uint8_t ulaw_byte)
{
    return ulaw_decode_tab[ulaw_byte];
}

void linear_to_ulaw_buffer(const int16_t *buf, uint8_t *out, size_t num_samples)
{
    for (size_t i = 0; i < num_samples; i++) {
        out[i] = linear_to_ulaw(buf[i]);
    }
}

void ulaw_to_linear_buffer(const uint8_t *buf, int16_t *out, size_t num_samples)
{
    for (size_t i = 0; i < num_samples; i++) {
        out[i] = ulaw_to_linear(buf[i]);
    }
}

/* ==========================================================================
 * L5: ITU-T G.711 A-law Companding
 * ========================================================================== */

/* A-law to 16-bit linear decode table (matching encode algorithm) */
static const int16_t alaw_decode_tab[256] = {
     -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
     -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
     -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
     -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
    -22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944,
    -30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136,
    -11008,-10496,-12032,-11520, -8960, -8448, -9984, -9472,
    -15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568,
      -344,  -328,  -376,  -360,  -280,  -264,  -312,  -296,
      -472,  -456,  -504,  -488,  -408,  -392,  -440,  -424,
       -88,   -72,  -120,  -104,   -24,    -8,   -56,   -40,
      -216,  -200,  -248,  -232,  -152,  -136,  -184,  -168,
     -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
     -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
      -688,  -656,  -752,  -720,  -560,  -528,  -624,  -592,
      -944,  -912, -1008,  -976,  -816,  -784,  -880,  -848,
      5504,  5248,  6016,  5760,  4480,  4224,  4992,  4736,
      7552,  7296,  8064,  7808,  6528,  6272,  7040,  6784,
      2752,  2624,  3008,  2880,  2240,  2112,  2496,  2368,
      3776,  3648,  4032,  3904,  3264,  3136,  3520,  3392,
     22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
     30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
     11008, 10496, 12032, 11520,  8960,  8448,  9984,  9472,
     15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
       344,   328,   376,   360,   280,   264,   312,   296,
       472,   456,   504,   488,   408,   392,   440,   424,
        88,    72,   120,   104,    24,     8,    56,    40,
       216,   200,   248,   232,   152,   136,   184,   168,
      1376,  1312,  1504,  1440,  1120,  1056,  1248,  1184,
      1888,  1824,  2016,  1952,  1632,  1568,  1760,  1696,
       688,   656,   752,   720,   560,   528,   624,   592,
       944,   912,  1008,   976,   816,   784,   880,   848
};

uint8_t linear_to_alaw(int16_t sample)
{
    /* Use inverse lookup into decode table for guaranteed roundtrip */
    int32_t best_dist = 2147483647;
    uint8_t best_idx = 0;

    for (int i = 0; i < 256; i++) {
        int32_t dist = (int32_t)sample - (int32_t)alaw_decode_tab[i];
        if (dist < 0) dist = -dist;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = (uint8_t)i;
            if (dist == 0) break;
        }
    }

    return best_idx;
}

int16_t alaw_to_linear(uint8_t alaw_byte)
{
    return alaw_decode_tab[alaw_byte];
}

/* ==========================================================================
 * L6: WAV File I/O (RIFF/WAVE container)
 * ========================================================================== */

/* WAV format structures — standard alignment, written field-by-field */
typedef struct {
    uint8_t  chunk_id[4];     /* "RIFF" */
    uint32_t chunk_size;      /* File size - 8 */
    uint8_t  format[4];       /* "WAVE" */
} riff_header_t;

typedef struct {
    uint8_t  subchunk_id[4];  /* "fmt " */
    uint32_t subchunk_size;   /* 16 for PCM */
    uint16_t audio_format;    /* 1 = PCM */
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_chunk_t;

typedef struct {
    uint8_t  subchunk_id[4];  /* "data" */
    uint32_t subchunk_size;   /* data size */
} wav_data_chunk_t;

int wav_write(const char *filename, const codec_params_t *params,
              const void *samples, uint64_t num_samples)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;

    uint16_t bits_per_sample = params->bit_depth;
    uint16_t num_channels    = params->num_channels;
    uint32_t sample_rate     = params->sample_rate;
    uint16_t block_align     = (bits_per_sample * num_channels) / 8;
    uint32_t byte_rate       = sample_rate * block_align;
    uint32_t data_size       = (uint32_t)(num_samples * block_align);
    uint32_t file_size       = 36 + data_size;

    /* RIFF header */
    fwrite("RIFF", 1, 4, fp);
    fwrite(&file_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    uint16_t audio_fmt = 1; /* PCM */
    fwrite(&audio_fmt, 2, 1, fp);
    fwrite(&num_channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits_per_sample, 2, 1, fp);

    /* data chunk */
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);

    /* Write samples */
    if (params->format == PCM_S16LE) {
        fwrite(samples, sizeof(int16_t), (size_t)num_samples, fp);
    } else if (params->format == PCM_FLOAT32) {
        /* Convert float [-1,+1] to int16 */
        const float *fsamples = (const float *)samples;
        for (uint64_t i = 0; i < num_samples; i++) {
            float clamped = fsamples[i];
            if (clamped < -1.0f) clamped = -1.0f;
            if (clamped >  1.0f) clamped =  1.0f;
            int16_t s16 = (int16_t)(clamped * 32767.0f);
            fwrite(&s16, sizeof(int16_t), 1, fp);
        }
    } else if (params->format == PCM_S32LE) {
        const int32_t *s32 = (const int32_t *)samples;
        for (uint64_t i = 0; i < num_samples; i++) {
            /* Convert 32-bit to 16-bit output */
            int16_t s16 = (int16_t)(s32[i] >> 16);
            fwrite(&s16, sizeof(int16_t), 1, fp);
        }
    } else {
        /* Unsupported format for WAV output */
        fclose(fp);
        return -2;
    }

    fclose(fp);
    return 0;
}

int wav_read_header(const char *filename, codec_params_t *params,
                    uint64_t *num_samples_out)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;

    char riff_id[5] = {0};
    uint32_t file_size;
    if (fread(riff_id, 1, 4, fp) != 4) { fclose(fp); return -2; }
    if (fread(&file_size, 4, 1, fp) != 1) { fclose(fp); return -2; }

    char wave_id[5] = {0};
    if (fread(wave_id, 1, 4, fp) != 4) { fclose(fp); return -2; }

    if (strncmp(riff_id, "RIFF", 4) != 0 || strncmp(wave_id, "WAVE", 4) != 0) {
        fclose(fp); return -3;
    }

    /* Find fmt chunk */
    uint16_t audio_format = 0, num_channels = 0, bits_per_sample = 0, block_align = 0;
    uint32_t sample_rate = 0, byte_rate = 0;
    int fmt_found = 0;
    char chunk_id[5] = {0};
    uint32_t chunk_size;

    while (fread(chunk_id, 1, 4, fp) == 4) {
        chunk_id[4] = '\0';
        if (fread(&chunk_size, 4, 1, fp) != 1) break;

        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            fread(&audio_format, 2, 1, fp);
            fread(&num_channels, 2, 1, fp);
            fread(&sample_rate, 4, 1, fp);
            fread(&byte_rate, 4, 1, fp);
            fread(&block_align, 2, 1, fp);
            fread(&bits_per_sample, 2, 1, fp);
            /* Skip remaining fmt bytes */
            if (chunk_size > 16) fseek(fp, (long)(chunk_size - 16), SEEK_CUR);
            fmt_found = 1;
        } else if (strncmp(chunk_id, "data", 4) == 0) {
            if (!fmt_found) { fclose(fp); return -4; }

            params->sample_rate  = sample_rate;
            params->bit_depth    = bits_per_sample;
            params->num_channels = num_channels;
            params->format       = (bits_per_sample == 32) ? PCM_S32LE : PCM_S16LE;
            params->frame_size   = (sample_rate * 20) / 1000;
            params->bitrate      = byte_rate * 8;

            uint16_t ba = (block_align == 0) ? (bits_per_sample * num_channels) / 8 : block_align;
            if (num_samples_out) {
                *num_samples_out = chunk_size / ba;
            }
            fclose(fp);
            return 0;
        } else {
            fseek(fp, (long)chunk_size, SEEK_CUR);
        }
    }

    fclose(fp);
    return -5;
}

int wav_read(const char *filename, codec_params_t *params,
             void **samples_out, uint64_t *num_samples_out)
{
    uint64_t num_samples = 0;
    int ret = wav_read_header(filename, params, &num_samples);
    if (ret < 0) return ret;

    FILE *fp = fopen(filename, "rb");
    if (!fp) return -2;

    /* Skip RIFF header */
    fseek(fp, 12, SEEK_SET);

    /* Skip to data chunk */
    char cid[5] = {0};
    uint32_t csize;
    uint32_t data_size = 0;
    long data_offset = 0;

    while (fread(cid, 1, 4, fp) == 4) {
        cid[4] = '\0';
        if (fread(&csize, 4, 1, fp) != 1) break;
        if (strncmp(cid, "data", 4) == 0) {
            data_size = csize;
            data_offset = ftell(fp);
            break;
        }
        fseek(fp, (long)csize, SEEK_CUR);
    }

    if (data_size == 0 || data_offset == 0) {
        fclose(fp); return -5;
    }

    uint16_t block_align = params->num_channels * (params->bit_depth / 8);
    if (block_align == 0) block_align = 2;

    num_samples = data_size / block_align;

    /* Allocate and read */
    if (params->bit_depth == 16) {
        int16_t *buf = (int16_t *)malloc((size_t)num_samples * sizeof(int16_t));
        if (!buf) { fclose(fp); return -6; }
        fseek(fp, data_offset, SEEK_SET);
        size_t nread = fread(buf, sizeof(int16_t), (size_t)num_samples, fp);
        fclose(fp);
        if (nread != num_samples) { free(buf); return -7; }
        *samples_out = buf;
    } else if (params->bit_depth == 32) {
        int32_t *buf = (int32_t *)malloc((size_t)num_samples * sizeof(int32_t));
        if (!buf) { fclose(fp); return -6; }
        fseek(fp, data_offset, SEEK_SET);
        size_t nread = fread(buf, sizeof(int32_t), (size_t)num_samples, fp);
        fclose(fp);
        if (nread != num_samples) { free(buf); return -7; }
        *samples_out = buf;
    } else if (params->bit_depth == 24) {
        /* Read 24-bit packed and expand to 32-bit */
        int32_t *buf = (int32_t *)malloc((size_t)num_samples * sizeof(int32_t));
        if (!buf) { fclose(fp); return -6; }
        fseek(fp, data_offset, SEEK_SET);
        uint8_t raw[3];
        for (uint64_t i = 0; i < num_samples; i++) {
            if (fread(raw, 3, 1, fp) != 1) break;
            /* Little-endian 24-bit to 32-bit sign-extended */
            int32_t val = (int32_t)((uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16));
            /* Sign extend from bit 23 */
            if (val & 0x800000) val |= 0xFF000000;
            buf[i] = val;
        }
        fclose(fp);
        *samples_out = buf;
    } else {
        fclose(fp);
        return -8; /* Unsupported bit depth */
    }

    *num_samples_out = num_samples;
    return 0;
}

/* ==========================================================================
 * L2: Sample Rate Conversion Factors
 * ========================================================================== */

int compute_src_factors(uint32_t fs_in, uint32_t fs_out,
                        uint32_t *L_out, uint32_t *M_out)
{
    if (fs_in == 0 || fs_out == 0) return -1;
    if (fs_in == fs_out) {
        *L_out = 1;
        *M_out = 1;
        return 0;
    }
    /* Reduce ratio L/M = fs_out / fs_in */
    uint32_t g = gcd_u32(fs_in, fs_out);
    if (g == 0) return -1;
    *L_out = fs_out / g;
    *M_out = fs_in / g;
    /* Check that factors are reasonable (avoid huge up/down sampling) */
    if (*L_out > 1000 || *M_out > 1000) return -2;
    return 0;
}

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

double db_to_linear(double db)
{
    return pow(10.0, db / 20.0);
}

double linear_to_db(double linear)
{
    if (linear <= 0.0) return -INFINITY;
    return 20.0 * log10(linear);
}

double clamp_sample(double value)
{
    if (value < -1.0) return -1.0;
    if (value >  1.0) return  1.0;
    return value;
}

uint32_t gcd_u32(uint32_t a, uint32_t b)
{
    /* Euclidean algorithm */
    while (b != 0) {
        uint32_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

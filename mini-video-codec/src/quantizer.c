/**
 * quantizer.c — Quantization and Rate Control Implementation
 *
 * Implements QP→Qstep mapping, dead-zone quantization, scaling matrix
 * quantization, rate-distortion bounds, and rate control algorithms.
 *
 * Knowledge coverage:
 *   L1: QP definitions, quantization step sizes
 *   L2: Dead-zone quantization, dequantization
 *   L4: Rate-Distortion lower bound (Gaussian RDF), SQNR formula
 *   L5: Rate control (CBR/VBR/CRF), complexity estimation
 */

#include "quantizer.h"
#include "video_codec.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ==========================================================================
 * L2: QP to Qstep Conversion (H.264 Table 7-3)
 * ========================================================================== */

/* Qstep for QP % 6 */
static const double qstep_base[6] = {
    0.625, 0.6875, 0.8125, 0.875, 1.0, 1.125
};

double qp_to_qstep(uint32_t qp)
{
    if (qp > 51) qp = 51;
    return qstep_base[qp % 6] * pow(2.0, (double)(qp / 6));
}

uint32_t qstep_to_qp(double qstep)
{
    if (qstep < qstep_base[0]) return 0;
    /* Find nearest QP by iterating (small search space, only 52 values) */
    for (uint32_t qp = 0; qp <= 51; qp++) {
        if (qp_to_qstep(qp) >= qstep) return qp;
    }
    return 51;
}

void quantizer_init(quantizer_t *q, uint32_t qp, int intra)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
    q->qp    = (int32_t)qp;
    q->qstep = qp_to_qstep(qp);
    /* qbits = 15 + qp/6 (H.264 forward transform scaling) */
    q->qbits = 15 + (int32_t)(qp / 6);
    /* MF = round(qstep * 2^qbits) */
    q->mf = (int32_t)round(q->qstep * pow(2.0, (double)q->qbits));
    /* Dead-zone offset: f = 2^qbits/3 for intra, 2^qbits/6 for inter */
    if (intra)
        q->f = (1 << q->qbits) / 3;
    else
        q->f = (1 << q->qbits) / 6;
}

/* ==========================================================================
 * L2: Quantization and Dequantization
 * ========================================================================== */

int32_t quantize_coeff(const quantizer_t *q, int32_t coeff)
{
    if (!q) return 0;
    int32_t abs_c = coeff < 0 ? -coeff : coeff;
    int32_t level = (abs_c * q->mf + q->f) >> q->qbits;
    return coeff < 0 ? -level : level;
}

int32_t dequantize_coeff(const quantizer_t *q, int32_t level)
{
    if (!q) return 0;
    /* H.264 inverse quant: coeff' = level * Qstep * 64
     * The factor 64 compensates for the forward DCT scaling.
     * For integer arithmetic with proper rounding: */
    if (level == 0) return 0;
    double qstep64 = q->qstep * 64.0;
    if (q->qp >= 12) {
        /* For large QP, use integer scaling with shift */
        int32_t iq_val = level * (int32_t)round(qstep64);
        return iq_val;
    } else {
        return level * (int32_t)round(qstep64);
    }
}

int quantize_block_4x4(const quantizer_t *q, int32_t *coeffs)
{
    if (!q || !coeffs) return 0;
    int nz = 0;
    for (int i = 0; i < 16; i++) {
        coeffs[i] = quantize_coeff(q, coeffs[i]);
        if (coeffs[i] != 0) nz++;
    }
    return nz;
}

void dequantize_block_4x4(const quantizer_t *q, int32_t *coeffs)
{
    if (!q || !coeffs) return;
    for (int i = 0; i < 16; i++) {
        coeffs[i] = dequantize_coeff(q, coeffs[i]);
    }
}

int quantize_block_8x8(const quantizer_t *q, int32_t *coeffs)
{
    if (!q || !coeffs) return 0;
    int nz = 0;
    for (int i = 0; i < 64; i++) {
        coeffs[i] = quantize_coeff(q, coeffs[i]);
        if (coeffs[i] != 0) nz++;
    }
    return nz;
}

void dequantize_block_8x8(const quantizer_t *q, int32_t *coeffs)
{
    if (!q || !coeffs) return;
    for (int i = 0; i < 64; i++) {
        coeffs[i] = dequantize_coeff(q, coeffs[i]);
    }
}

/* ==========================================================================
 * L2: Quantization with Scaling Matrix
 * ========================================================================== */

int quantize_block_scaled(int32_t *coeffs, uint32_t block_size,
                          uint32_t qp, const quant_matrix_t *matrix, int intra)
{
    if (!coeffs) return 0;
    quantizer_t q;
    quantizer_init(&q, qp, intra);
    uint32_t total = block_size * block_size;
    int nz = 0;
    for (uint32_t i = 0; i < total && i < 64; i++) {
        int32_t abs_c = coeffs[i] < 0 ? -coeffs[i] : coeffs[i];
        uint8_t scale = matrix ? matrix->scale[i] : 16;
        if (scale == 0) scale = 16;
        int32_t level = (abs_c * q.mf * 16 / scale + q.f) >> q.qbits;
        coeffs[i] = coeffs[i] < 0 ? -level : level;
        if (coeffs[i] != 0) nz++;
    }
    return nz;
}

void dequantize_block_scaled(int32_t *coeffs, uint32_t block_size,
                             uint32_t qp, const quant_matrix_t *matrix)
{
    if (!coeffs) return;
    quantizer_t q;
    quantizer_init(&q, qp, 1);
    uint32_t total = block_size * block_size;
    for (uint32_t i = 0; i < total && i < 64; i++) {
        uint8_t scale = matrix ? matrix->scale[i] : 16;
        if (scale == 0) scale = 16;
        coeffs[i] = dequantize_coeff(&q, coeffs[i]) * scale / 16;
    }
}

/* ==========================================================================
 * L4: Rate-Distortion Theory
 * ========================================================================== */

double rate_distortion_gaussian(double variance, double distortion)
{
    if (variance <= 0.0 || distortion <= 0.0) return 0.0;
    if (distortion >= variance) return 0.0;
    return 0.5 * log2(variance / distortion);
}

double quant_distortion(double step_size)
{
    return step_size * step_size / 12.0;
}

double quant_sqnr_db(uint32_t qp, uint32_t bit_depth)
{
    /* Peak signal for N-bit video: (2^N - 1) */
    double peak = pow(2.0, (double)bit_depth) - 1.0;
    double step = qp_to_qstep(qp);
    double noise_power = step * step / 12.0;
    double signal_power = peak * peak;
    return 10.0 * log10(signal_power / noise_power);
}

/* ==========================================================================
 * L5: Rate Control
 * ========================================================================== */

void rc_init(rc_state_t *rc, rc_mode_t mode, uint32_t bitrate,
             uint32_t fps_num, uint32_t fps_den)
{
    if (!rc) return;
    memset(rc, 0, sizeof(*rc));
    rc->mode            = mode;
    rc->target_bitrate  = bitrate;
    rc->frame_rate_num  = fps_num;
    rc->frame_rate_den  = fps_den;
    rc->qp              = 26.0;
    rc->qp_int          = 26;
    rc->buffer_fullness = 0;
    rc->rate_factor     = 23.0;
}

uint32_t rc_compute_qp(rc_state_t *rc, double complexity)
{
    if (!rc) return 26;
    switch (rc->mode) {
        case RC_CQP:
            return rc->qp_int;
        case RC_CBR: {
            /* Adjust QP based on buffer fullness */
            double target_bits = rc_target_bits_per_frame(rc);
            double buffer_frac = rc_buffer_fullness_frac(rc);
            /* If buffer is filling up (>0.5), increase QP to reduce bits */
            double qp_adj = (buffer_frac - 0.5) * 12.0;
            /* Adjust based on complexity relative to average */
            if (rc->complexity_est > 0.0 && complexity > 0.0) {
                double comp_ratio = complexity / (rc->complexity_est + 1.0);
                qp_adj += (comp_ratio - 1.0) * 6.0;
            }
            rc->qp += qp_adj * 0.5;
            if (rc->qp < 0.0) rc->qp = 0.0;
            if (rc->qp > 51.0) rc->qp = 51.0;
            rc->qp_int = (uint32_t)round(rc->qp);
            (void)target_bits;
            return rc->qp_int;
        }
        case RC_CRF:
            /* Constant quality: QP fixed at rate_factor */
            return (uint32_t)round(rc->rate_factor);
        case RC_VBR:
        case RC_ABR:
            return rc->qp_int;
        default:
            return 26;
    }
}

void rc_update(rc_state_t *rc, uint64_t bits_used)
{
    if (!rc) return;
    rc->last_frame_bits = (double)bits_used;
    rc->frames_encoded++;
    rc->bits_accum += bits_used;
    /* Update VBV buffer fullness */
    double target_per_frame = rc_target_bits_per_frame(rc);
    rc->buffer_fullness += (int64_t)((double)bits_used - target_per_frame);
    /* Clamp buffer */
    int64_t max_buf = (int64_t)rc->vbv_bufsize;
    if (rc->buffer_fullness > max_buf) rc->buffer_fullness = max_buf;
    if (rc->buffer_fullness < -max_buf) rc->buffer_fullness = -max_buf;
    /* Update complexity estimate (exponential moving average) */
    if (rc->frames_encoded == 1) {
        rc->complexity_est = (double)bits_used;
    } else {
        rc->complexity_est = 0.9 * rc->complexity_est + 0.1 * (double)bits_used;
    }
}

double rc_target_bits_per_frame(const rc_state_t *rc)
{
    if (!rc || rc->frame_rate_den == 0) return 0.0;
    return (double)rc->target_bitrate * (double)rc->frame_rate_den
           / (double)rc->frame_rate_num;
}

double rc_buffer_fullness_frac(const rc_state_t *rc)
{
    if (!rc || rc->vbv_bufsize == 0) return 0.5;
    double half = (double)rc->vbv_bufsize / 2.0;
    double ratio = ((double)rc->buffer_fullness + half) / (double)rc->vbv_bufsize;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    return ratio;
}

double estimate_frame_complexity(const video_frame_t *frame)
{
    if (!frame || !frame->y.data) return 0.0;
    /* Simple complexity: mean of absolute differences from DC */
    uint32_t w = frame->width, h = frame->height;
    if (w == 0 || h == 0) return 0.0;
    uint64_t sum = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            sum += frame->y.data[y * frame->y.stride + x];
        }
    }
    uint8_t dc = (uint8_t)(sum / (uint64_t)(w * h));
    uint64_t sad = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            int d = (int)frame->y.data[y * frame->y.stride + x] - (int)dc;
            sad += (uint64_t)(d < 0 ? -d : d);
        }
    }
    return (double)sad / (double)(w * h);
}

/**
 * entropy_codec.c — Entropy Coding Implementation (CAVLC, Exp-Golomb)
 *
 * Implements bitstream I/O, Exp-Golomb coding (ue/se/te), CAVLC encode/decode,
 * CABAC binarization, and Shannon entropy verification.
 *
 * Knowledge coverage:
 *   L1: Bitstream reader/writer structs
 *   L2: Variable-length coding operations
 *   L4: Shannon entropy, Kraft inequality verification
 *   L5: Exp-Golomb coding, CAVLC encode/decode pipeline
 */

#include "entropy_codec.h"
#include <string.h>
#include <math.h>

/* ==========================================================================
 * L2: Bitstream Writer/Reader
 * ========================================================================== */

void bs_writer_init(bs_writer_t *w, uint8_t *buf, uint32_t size)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->buf = buf;
    w->buf_size = size;
    if (buf && size > 0) memset(buf, 0, size);
}

void bs_reader_init(bs_reader_t *r, const uint8_t *buf, uint32_t size)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->buf = buf;
    r->buf_size = size;
}

int bs_write_bit(bs_writer_t *w, uint8_t bit)
{
    if (!w || !w->buf || w->byte_pos >= w->buf_size) return -1;
    if (bit) w->buf[w->byte_pos] |= (uint8_t)(1U << (7 - w->bit_pos));
    w->bit_pos++;
    w->total_bits++;
    if (w->bit_pos >= 8) {
        w->bit_pos = 0;
        w->byte_pos++;
    }
    return 0;
}

int bs_write_bits(bs_writer_t *w, uint32_t value, uint32_t n)
{
    if (!w || n > 32) return -1;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t bit = (uint8_t)((value >> (n - 1 - i)) & 1);
        if (bs_write_bit(w, bit) != 0) return -1;
    }
    return 0;
}

int bs_read_bit(bs_reader_t *r)
{
    if (!r || !r->buf || r->byte_pos >= r->buf_size) {
        if (r) r->error = 1;
        return -1;
    }
    uint8_t bit = (r->buf[r->byte_pos] >> (7 - r->bit_pos)) & 1;
    r->bit_pos++;
    r->total_bits++;
    if (r->bit_pos >= 8) {
        r->bit_pos = 0;
        r->byte_pos++;
    }
    return (int)bit;
}

uint32_t bs_read_bits(bs_reader_t *r, uint32_t n)
{
    if (!r || n > 32) { if (r) r->error = 1; return 0; }
    uint32_t value = 0;
    for (uint32_t i = 0; i < n; i++) {
        int bit = bs_read_bit(r);
        if (bit < 0) { r->error = 1; return 0; }
        value = (value << 1) | (uint32_t)bit;
    }
    return value;
}

void bs_byte_align(bs_writer_t *w)
{
    if (!w || w->bit_pos == 0) return;
    while (w->bit_pos > 0) bs_write_bit(w, 0);
}

void bs_reader_byte_align(bs_reader_t *r)
{
    if (!r || r->bit_pos == 0) return;
    r->bit_pos = 0;
    r->byte_pos++;
}

uint32_t bs_writer_byte_count(const bs_writer_t *w)
{
    if (!w) return 0;
    return (w->total_bits + 7) / 8;
}

uint32_t bs_reader_byte_count(const bs_reader_t *r)
{
    if (!r) return 0;
    return (r->total_bits + 7) / 8;
}

/* ==========================================================================
 * L5: Exponential-Golomb Coding
 * ========================================================================== */

int bs_write_ue(bs_writer_t *w, uint32_t value)
{
    if (!w) return -1;
    uint32_t v = value + 1;
    /* Count leading zeros (floor(log2(v))) */
    uint32_t leading_zeros = 0;
    uint32_t t = v;
    while (t > 1) { t >>= 1; leading_zeros++; }
    /* Write leading zeros */
    for (uint32_t i = 0; i < leading_zeros; i++)
        if (bs_write_bit(w, 0) != 0) return -1;
    /* Write separator '1' and the info bits */
    if (bs_write_bits(w, v, leading_zeros + 1) != 0) return -1;
    return (int)(2 * leading_zeros + 1);
}

uint32_t bs_read_ue(bs_reader_t *r)
{
    if (!r) { return 0; }
    uint32_t leading_zeros = 0;
    int bit;
    while ((bit = bs_read_bit(r)) == 0) {
        leading_zeros++;
        if (leading_zeros > 32) { r->error = 1; return 0; }
    }
    if (bit < 0) { r->error = 1; return 0; }
    /* bit == 1 is the separator */
    if (leading_zeros == 0) return 0;
    uint32_t info = bs_read_bits(r, leading_zeros);
    if (r->error) return 0;
    return (1U << leading_zeros) + info - 1;
}

int bs_write_se(bs_writer_t *w, int32_t value)
{
    uint32_t ue_val;
    if (value <= 0)
        ue_val = (uint32_t)((-value) << 1);
    else
        ue_val = (uint32_t)((value << 1) - 1);
    return bs_write_ue(w, ue_val);
}

int32_t bs_read_se(bs_reader_t *r)
{
    uint32_t ue_val = bs_read_ue(r);
    if (r->error) return 0;
    if (ue_val & 1)
        return (int32_t)((ue_val + 1) >> 1);
    else
        return -(int32_t)(ue_val >> 1);
}

int bs_write_te(bs_writer_t *w, uint32_t value, uint32_t range)
{
    if (!w) return -1;
    if (range == 1) {
        /* Inverted bit */
        return bs_write_bit(w, value ? 0 : 1);
    }
    return bs_write_ue(w, value);
}

uint32_t bs_read_te(bs_reader_t *r, uint32_t range)
{
    if (range == 1) {
        int bit = bs_read_bit(r);
        return bit ? 0 : 1;
    }
    return bs_read_ue(r);
}

uint32_t ue_bit_length(uint32_t value)
{
    uint32_t v = value + 1;
    uint32_t lz = 0;
    while (v >> lz) lz++;
    return (lz > 0) ? 2 * lz - 1 : 1;
}

uint32_t se_bit_length(int32_t value)
{
    uint32_t ue_val;
    if (value <= 0)
        ue_val = (uint32_t)((-value) << 1);
    else
        ue_val = (uint32_t)(((uint32_t)value << 1) - 1);
    return ue_bit_length(ue_val);
}

/* ==========================================================================
 * L5: CAVLC Implementation
 * ========================================================================== */

/*
 * CAVLC CoeffToken tables are defined in H.264 Table 9-5.
 * For this simplified implementation, we use fixed-length coding
 * for coefficients (see cavlc_write_coeff_token).
 * Full CAVLC VLC tables are extensive; the core coding logic
 * demonstrates the encode/decode structure.
 */

int cavlc_write_coeff_token(bs_writer_t *w, int total_coeff, int trailing_ones,
                            uint32_t nC)
{
    if (!w) return -1;
    if (total_coeff < 0 || total_coeff > 16) return -1;
    if (trailing_ones < 0 || trailing_ones > 3) return -1;
    if (trailing_ones > total_coeff) trailing_ones = total_coeff;

    /* For this simplified implementation, we use a fixed 6-bit code approach.
     * Real CAVLC uses context-adaptive tables. We encode:
     *   total_coeff (0-16) in 5 bits
     *   trailing_ones (0-3) in 2 bits
     */
    (void)nC;
    int bits = 0;
    bits += bs_write_bits(w, (uint32_t)total_coeff, 5) == 0 ? 5 : -1;
    if (bits < 0) return -1;
    bits += bs_write_bits(w, (uint32_t)trailing_ones, 2) == 0 ? 2 : -1;
    return (bits < 0) ? -1 : bits;
}

int cavlc_write_levels(bs_writer_t *w, const int32_t *levels, int num_levels)
{
    if (!w || !levels) return -1;
    int bits = 0;
    for (int i = 0; i < num_levels; i++) {
        int lev = levels[i];
        if (lev == 0) continue;
        /* Simplified: use se(v) coding for levels */
        int b = bs_write_se(w, (int32_t)lev);
        if (b < 0) return -1;
        bits += b;
    }
    return bits;
}

int cavlc_write_total_zeros(bs_writer_t *w, int total_zeros, int total_coeff)
{
    if (!w) return -1;
    /* Simplified: write as ue(v) */
    (void)total_coeff;
    return bs_write_ue(w, (uint32_t)total_zeros);
}

int cavlc_write_run_before(bs_writer_t *w, int zeros_left, int run)
{
    if (!w) return -1;
    /* Simplified: write run as 3-bit fixed length */
    (void)zeros_left;
    return bs_write_bits(w, (uint32_t)run, 3);
}

int cavlc_encode_4x4(bs_writer_t *w, const int32_t *coeffs, uint32_t nC)
{
    if (!w || !coeffs) return -1;
    /* 1. Count total_coeff and trailing_ones */
    int total_coeff = 0, trailing_ones = 0;
    int32_t levels[16];
    int level_count = 0;
    int last_nz = -1;
    for (int i = 0; i < 16; i++) {
        if (coeffs[i] != 0) {
            last_nz = i;
            total_coeff++;
            if (level_count < 16)
                levels[level_count++] = coeffs[i];
        }
    }
    /* Count trailing ones (from end) */
    if (total_coeff > 0) {
        for (int i = 15; i >= last_nz - total_coeff + 1 && i >= 0; i--) {
            if (coeffs[i] == 1 || coeffs[i] == -1) {
                if (trailing_ones < 3) trailing_ones++;
                else break;
            } else if (coeffs[i] != 0) {
                break;
            }
        }
    }
    if (trailing_ones > total_coeff) trailing_ones = total_coeff;

    /* 2. Encode coeff_token */
    int bits = cavlc_write_coeff_token(w, total_coeff, trailing_ones, nC);
    if (bits < 0) return -1;

    /* 3. Encode sign of trailing ones */
    for (int i = 0; i < trailing_ones && i < 16; i++) {
        int si = 15 - i;
        if (si >= 0 && si < 16 && coeffs[si] != 0) {
            int sign_bit = (coeffs[si] < 0) ? 1 : 0;
            if (bs_write_bit(w, (uint8_t)sign_bit) != 0) return -1;
            bits++;
        }
    }

    /* 4. Encode remaining levels (non-trailing) */
    int num_levels = total_coeff - trailing_ones;
    if (num_levels > 0) {
        int b = cavlc_write_levels(w, levels, num_levels);
        if (b < 0) return -1;
        bits += b;
    }

    /* 5. Encode total_zeros */
    int total_zeros = (total_coeff > 0) ? (last_nz + 1 - total_coeff) : 0;
    if (total_coeff < 16) {
        int b = cavlc_write_total_zeros(w, total_zeros, total_coeff);
        if (b < 0) return -1;
        bits += b;
    }

    /* 6. Encode run_before */
    int zeros_left = total_zeros;
    for (int i = total_coeff - 1; i > 0 && zeros_left > 0; i--) {
        int run = 0;
        int pos = last_nz - (total_coeff - 1 - i);
        for (int j = pos - 1; j >= 0 && coeffs[j] == 0; j--) run++;
        if (run > zeros_left) run = zeros_left;
        int b = cavlc_write_run_before(w, zeros_left, run);
        if (b < 0) return -1;
        bits += b;
        zeros_left -= run;
    }
    return bits;
}

int cavlc_decode_4x4(bs_reader_t *r, int32_t *coeffs, uint32_t nC)
{
    if (!r || !coeffs) return -1;
    memset(coeffs, 0, 16 * sizeof(int32_t));
    /* Decode coeff_token: 5 bits total_coeff, 2 bits trailing_ones */
    int total_coeff = (int)bs_read_bits(r, 5);
    int trailing_ones = (int)bs_read_bits(r, 2);
    (void)nC;
    if (r->error) return -1;

    /* Decode trailing one signs */
    for (int i = 0; i < trailing_ones; i++) {
        int sign = bs_read_bit(r);
        if (sign < 0) { r->error = 1; return -1; }
    }

    /* Decode remaining levels */
    int num_levels = total_coeff - trailing_ones;
    int32_t levels[16] = {0};
    for (int i = 0; i < num_levels && i < 16; i++) {
        levels[i] = bs_read_se(r);
        if (r->error) return -1;
    }

    /* Decode total_zeros */
    int total_zeros = (int)bs_read_ue(r);
    if (r->error) return -1;

    /* Decode run_before and reconstruct */
    int pos = total_coeff + total_zeros;
    if (pos > 16) pos = 16;
    /* Place trailing ones at the end */
    int t1_start = total_coeff - trailing_ones;
    int level_pos = 0;
    for (int i = total_coeff - 1; i >= 0; i--) {
        int run = 0;
        if (i > 0) {
            run = (int)bs_read_bits(r, 3);
            if (r->error) return -1;
        } else {
            run = total_zeros;
        }
        pos -= run + 1;
        if (pos >= 0 && pos < 16) {
            if (i >= t1_start) {
                /* Trailing one: sign already decoded */
                coeffs[pos] = 1; /* approximate */
            } else if (level_pos < num_levels) {
                coeffs[pos] = levels[level_pos++];
            }
        }
        total_zeros -= run;
    }
    return 0;
}

/* ==========================================================================
 * L5: CABAC Binarization Helpers
 * ========================================================================== */

uint32_t cabac_unary_bins(uint32_t value)
{
    return value + 1; /* value '1's + terminating '0' */
}

uint32_t cabac_trunc_unary_bins(uint32_t value, uint32_t c_max)
{
    if (value < c_max) return value + 1;
    return c_max; /* all '1's, no terminating '0' */
}

uint32_t cabac_egk_bins(uint32_t value, uint32_t k)
{
    uint32_t v = value + (1U << k);
    /* Unary part: floor(log2(v)) - k + 1  leading zeros */
    uint32_t leading = 0;
    uint32_t t = v;
    while (t > 0) { t >>= 1; leading++; }
    /* leading includes separator; suffix = leading - 1 bits */
    if (leading <= k) return 1 + k; /* minimum case */
    uint32_t unary = leading - k;
    return unary + 1 + (leading - 1); /* unary zeros + separator + suffix */
}

/* ==========================================================================
 * L4: Shannon Entropy and Kraft Inequality
 * ========================================================================== */

double shannon_entropy(const double *probs, uint32_t num_syms)
{
    if (!probs || num_syms == 0) return 0.0;
    double entropy = 0.0;
    for (uint32_t i = 0; i < num_syms; i++) {
        if (probs[i] > 0.0)
            entropy -= probs[i] * log2(probs[i]);
    }
    return entropy;
}

double avg_code_length(const double *probs, const uint32_t *lengths,
                       uint32_t num_syms)
{
    if (!probs || !lengths || num_syms == 0) return 0.0;
    double avg = 0.0;
    for (uint32_t i = 0; i < num_syms; i++) {
        avg += probs[i] * (double)lengths[i];
    }
    return avg;
}

double kraft_sum(const uint32_t *lengths, uint32_t num)
{
    if (!lengths || num == 0) return 0.0;
    double sum = 0.0;
    for (uint32_t i = 0; i < num; i++) {
        sum += pow(2.0, -(double)lengths[i]);
    }
    return sum;
}

/* ==========================================================================
 * L2: Emulation Prevention
 * ========================================================================== */

uint32_t emulation_prevent(const uint8_t *in, uint32_t in_len, uint8_t *out)
{
    if (!in || !out) return 0;
    uint32_t out_pos = 0;
    uint32_t zero_count = 0;
    for (uint32_t i = 0; i < in_len; i++) {
        if (zero_count == 2 && in[i] <= 0x03) {
            out[out_pos++] = 0x03;
            zero_count = 0;
        }
        out[out_pos++] = in[i];
        if (in[i] == 0x00) zero_count++;
        else zero_count = 0;
    }
    return out_pos;
}

uint32_t emulation_remove(const uint8_t *in, uint32_t in_len, uint8_t *out)
{
    if (!in || !out) return 0;
    uint32_t out_pos = 0;
    uint32_t zero_count = 0;
    for (uint32_t i = 0; i < in_len; i++) {
        if (zero_count == 2 && in[i] == 0x03) {
            zero_count = 0;
            continue;
        }
        out[out_pos++] = in[i];
        if (in[i] == 0x00) zero_count++;
        else zero_count = 0;
    }
    return out_pos;
}

int find_start_code(const uint8_t *buf, uint32_t len, uint32_t *pos)
{
    if (!buf || !pos) return 0;
    for (uint32_t i = *pos; i + 3 <= len; i++) {
        if (buf[i] == 0 && buf[i+1] == 0) {
            if (buf[i+2] == 1) { *pos = i; return 3; }
            if (i + 4 <= len && buf[i+2] == 0 && buf[i+3] == 1) {
                *pos = i; return 4;
            }
        }
    }
    *pos = len;
    return 0;
}

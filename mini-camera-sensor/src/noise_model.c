/**
 * @file noise_model.c
 * @brief Sensor noise model — physics-based noise generation and estimation
 *
 * Implements:
 *   L4: kTC reset noise, Johnson-Nyquist, quantization noise, Poisson
 *   L5: noise synthesis (shot, read, dark, PRNU, FPN, RTS, 1/f),
 *       noise estimation from measurements, PTC fitting
 *   L6: SNR curve analysis, optimal gain selection
 *
 * Reference: Janesick "Photon Transfer" (2007); Holst & Lomheim (2011)
 */
#include "noise_model.h"

/*===========================================================================
 * L4: Fundamental noise law functions
 *===========================================================================*/

/**
 * kTC reset noise.
 * sigma_reset = sqrt(k * T * C) / q   [electrons RMS]
 *
 * k = Boltzmann, T = temperature [K], C = sense node capacitance [F],
 * q = elementary charge [C]
 */
double noise_ktc_reset_e(double temp_k, double cap_f)
{
    if (temp_k <= 0.0 || cap_f <= 0.0) return 0.0;
    /* sigma_e = sqrt(k*T*C) / q */
    double kTC = 1.380649e-23 * temp_k * cap_f;
    return sqrt(kTC) / 1.602176634e-19;
}

/**
 * Johnson-Nyquist thermal noise.
 * v_n_rms = sqrt(4 * k * T * R * BW)   [V RMS]
 */
double noise_johnson_nyquist_vrms(double res_ohm, double bw_hz, double temp_k)
{
    if (res_ohm <= 0.0 || bw_hz <= 0.0 || temp_k <= 0.0) return 0.0;
    return sqrt(4.0 * 1.380649e-23 * temp_k * res_ohm * bw_hz);
}

/**
 * Quantization noise RMS (uniform distribution).
 * sigma = LSB / sqrt(12)
 */
double noise_quantization_rms(double lsb)
{
    if (lsb <= 0.0) return 0.0;
    return lsb / sqrt(12.0);
}

/*===========================================================================
 * L5: Noise synthesis
 *===========================================================================*/

void noise_params_init_default(noise_params_t *np)
{
    if (np == NULL) return;
    memset(np, 0, sizeof(*np));
    np->shot_coeff = 1.0;
    np->read_noise_e = 3.0;
    np->dark_current_e = 80.0;
    np->prnu_coeff = 0.008;
    np->fpn_col_e = 2.0;
    np->fpn_row_e = 1.0;
    np->ktc_noise_e = 5.0;
    np->quant_lsb_e = 2.0;
    np->flicker_coeff = 0.5;
    np->rts_amplitude_e = 1.0;
    np->rts_time_constant = 1.0;
}

/**
 * Generate Poisson-distributed shot noise.
 *
 * For signal < 100 e-, use true Poisson (Knuth algorithm).
 * For signal >= 100 e-, use Gaussian approximation N(signal, sqrt(signal)).
 */
double noise_generate_shot(double signal_e)
{
    if (signal_e <= 0.0) return 0.0;

    if (signal_e < 100.0) {
        /* Knuth Poisson generator for small means */
        double L = exp(-signal_e);
        double p = 1.0;
        int32_t k = 0;
        /* Simple PRNG — in production, use a proper RNG */
        uint32_t seed = (uint32_t)(signal_e * 1234567.0 + 1.0);
        do {
            k++;
            seed = seed * 1103515245 + 12345;
            p *= (double)(seed & 0x7FFFFFFF) / 2147483648.0;
        } while (p > L);
        return (double)(k - 1);
    } else {
        /* Gaussian approximation for large mean */
        /* Box-Muller transform for N(mean, sigma) */
        double u1 = (double)((uint32_t)(signal_e * 7654321.0 + 1.0) &
                             0x7FFFFFFF) / 2147483648.0;
        double u2 = (double)((uint32_t)(signal_e * 9876543.0 + 42.0) &
                             0x7FFFFFFF) / 2147483648.0;
        if (u1 < 1e-10) u1 = 1e-10;
        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
        double val = signal_e + z * sqrt(signal_e);
        if (val < 0.0) val = 0.0;
        return val;
    }
}

double noise_generate_read(double sigma_e)
{
    if (sigma_e <= 0.0) return 0.0;
    /* Box-Muller for N(0, sigma) */
    double u1 = (double)((uint32_t)(sigma_e * 11223344.0 + 99.0) &
                         0x7FFFFFFF) / 2147483648.0;
    double u2 = (double)((uint32_t)(sigma_e * 55667788.0 + 77.0) &
                         0x7FFFFFFF) / 2147483648.0;
    if (u1 < 1e-10) u1 = 1e-10;
    return sigma_e * sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

double noise_generate_dark(double dark_rate_e_s, double exposure_s)
{
    double mean_dark = dark_rate_e_s * exposure_s;
    return noise_generate_shot(mean_dark); /* Dark current is Poisson */
}

double noise_apply_prnu(double signal_e, double coeff, double prnu_fixed)
{
    if (signal_e <= 0.0) return 0.0;
    /* PRNU is multiplicative: signal * (1 + coeff * prnu_fixed) */
    return signal_e * (1.0 + coeff * prnu_fixed);
}

/**
 * Generate total noise for a single pixel.
 *
 * Full noise budget: combines shot, read, dark shot, PRNU, kTC, quantization,
 * column FPN, row FPN, 1/f, and RTS.
 */
double noise_generate_pixel(double signal_e, const noise_params_t *np,
                             noise_state_t *state, double exposure_s,
                             double prnu_fixed)
{
    if (np == NULL || state == NULL) return signal_e;

    /* 1. Photon shot noise */
    double s = noise_generate_shot(signal_e);

    /* 2. Read noise (Gaussian, independent per read) */
    s += noise_generate_read(np->read_noise_e);

    /* 3. Dark current shot noise */
    s += noise_generate_dark(np->dark_current_e, exposure_s);

    /* 4. kTC reset noise (adds once per frame) */
    s += noise_generate_read(np->ktc_noise_e);

    /* 5. PRNU (multiplicative, fixed per pixel across frames) */
    s = noise_apply_prnu(s, np->prnu_coeff, prnu_fixed);

    /* 6. Quantization noise (adds after ADC) */
    s += np->quant_lsb_e * ((double)(state->sample_count % 1000) / 500.0 - 1.0)
         / sqrt(12.0);

    /* 7. 1/f noise (pink noise, correlated in time) */
    state->flicker_state = 0.95 * state->flicker_state +
                           np->flicker_coeff * noise_generate_read(1.0);
    s += state->flicker_state;

    /* 8. RTS noise (random telegraph signal) */
    {
        double rts_rand = (double)((state->sample_count * 7919 + 1213) &
                                   0xFFFF) / 65536.0;
        if (rts_rand < np->rts_time_constant * 0.01) {
            state->rts_state = -state->rts_state; /* Flip */
        }
        s += state->rts_state * np->rts_amplitude_e;
    }

    state->sample_count++;

    if (s < 0.0) s = 0.0;
    return s;
}

/**
 * Generate noisy raw frame from an ideal signal frame.
 */
void noise_generate_frame(uint16_t *ideal, uint32_t w, uint32_t h,
                           const noise_params_t *np, noise_state_t *states,
                           double exposure_s, const double *prnu_map,
                           uint16_t *noisy)
{
    if (ideal == NULL || noisy == NULL || np == NULL || states == NULL) return;

    uint32_t i, n = w * h;
    for (i = 0; i < n; i++) {
        double prnu_fixed = (prnu_map != NULL) ? prnu_map[i] : 0.0;
        double s = noise_generate_pixel((double)ideal[i], np,
                                         &states[i], exposure_s, prnu_fixed);
        if (s > 65535.0) s = 65535.0;
        if (s < 0.0) s = 0.0;
        noisy[i] = (uint16_t)(s + 0.5);
    }
}

/*===========================================================================
 * L5: Noise estimation from measurements
 *===========================================================================*/

/**
 * Estimate read noise from two dark frames.
 * read_noise = stddev(dark1 - dark2) / sqrt(2)
 *
 * Subtracting two dark frames cancels FPN (which is temporally fixed),
 * leaving only temporal noise. Dividing by sqrt(2) gives single-frame noise.
 */
double noise_estimate_read_noise(const double *dark1, const double *dark2,
                                  uint32_t n)
{
    if (dark1 == NULL || dark2 == NULL || n == 0) return 0.0;

    double sum = 0.0, sum2 = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double diff = dark1[i] - dark2[i];
        sum  += diff;
        sum2 += diff * diff;
    }

    double mean = sum / n;
    double var = sum2 / n - mean * mean;
    if (var < 0.0) var = 0.0;

    return sqrt(var) / sqrt(2.0);
}

/**
 * Estimate total temporal + fixed noise from multi-frame flat-field.
 *
 * For N frames: compute per-pixel temporal stddev → temporal noise.
 * Compute spatial stddev of per-pixel means → fixed noise (includes PRNU+FPN).
 */
void noise_estimate_total(const double **flats, uint32_t n_frames,
                           uint32_t n_pix, double *total, double *fixed)
{
    if (flats == NULL || n_frames == 0 || n_pix == 0 ||
        total == NULL || fixed == NULL) return;

    /* Per-pixel mean and temporal variance */
    double sum_temp = 0.0;
    double sum_fixed = 0.0;
    double grand_mean = 0.0;

    uint32_t p;
    for (p = 0; p < n_pix; p++) {
        double px_mean = 0.0;
        double px_var = 0.0;
        uint32_t f;
        for (f = 0; f < n_frames; f++) {
            px_mean += flats[f][p];
        }
        px_mean /= n_frames;

        for (f = 0; f < n_frames; f++) {
            double diff = flats[f][p] - px_mean;
            px_var += diff * diff;
        }
        px_var /= n_frames;

        sum_temp += px_var;
        grand_mean += px_mean;
    }
    grand_mean /= n_pix;

    /* Fixed noise = spatial stddev of per-pixel means */
    for (p = 0; p < n_pix; p++) {
        double px_mean = 0.0;
        uint32_t f;
        for (f = 0; f < n_frames; f++) px_mean += flats[f][p];
        px_mean /= n_frames;
        double diff = px_mean - grand_mean;
        sum_fixed += diff * diff;
    }

    *total = sqrt(sum_temp / n_pix + sum_fixed / n_pix);
    *fixed = sqrt(sum_fixed / n_pix);
}

/**
 * PTC estimation from mean/variance pairs.
 *
 * Model: variance = read_noise^2 + mean/CG + (PRNU*mean)^2
 *
 * Performs 3-parameter least-squares fit.
 * Simplified: linear fit in shot-noise region for CG,
 * then estimate read noise from intercept, PRNU from residuals.
 */
int noise_ptc_estimate(const double *means, const double *vars, uint32_t n,
                        double *read_noise, double *cg, double *prnu)
{
    if (means == NULL || vars == NULL || n < 3 ||
        read_noise == NULL || cg == NULL || prnu == NULL) return -1;

    /* Linear regression: var = a + b*mean where b=1/CG */
    double sx = 0, sy = 0, sxy = 0, sx2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        sx  += means[i];
        sy  += vars[i];
        sxy += means[i] * vars[i];
        sx2 += means[i] * means[i];
    }

    double denom = n * sx2 - sx * sx;
    if (fabs(denom) < 1e-20) return -1;

    double slope = (n * sxy - sx * sy) / denom;
    double intercept = (sy - slope * sx) / n;

    if (slope <= 0.0) return -1;
    *cg = 1.0 / slope;
    *read_noise = sqrt(intercept > 0.0 ? intercept : 0.0);

    /* Estimate PRNU from high-signal deviation */
    double prnu_sum = 0.0;
    uint32_t prnu_count = 0;
    for (i = 0; i < n; i++) {
        double predicted = intercept + means[i] / (*cg);
        if (means[i] > (*read_noise) * 3.0) { /* Use high-SNR points */
            double excess = vars[i] - predicted;
            if (excess > 0.0) {
                prnu_sum += sqrt(excess) / (means[i] + 1.0);
                prnu_count++;
            }
        }
    }
    *prnu = (prnu_count > 0) ? prnu_sum / prnu_count : 0.0;

    return 0;
}

/**
 * Compute SNR curve across full signal range.
 */
void noise_snr_curve(const noise_params_t *np, uint32_t n_pts, double fwc,
                      double *signal_out, double *snr_out)
{
    if (np == NULL || signal_out == NULL || snr_out == NULL || n_pts == 0)
        return;

    uint32_t i;
    for (i = 0; i < n_pts; i++) {
        double signal = fwc * (double)(i + 1) / (double)n_pts;
        signal_out[i] = signal;

        /* Total noise */
        double shot = signal;
        double read = np->read_noise_e * np->read_noise_e;
        double dark = np->dark_current_e * 0.033; /* ~30ms exposure */
        double prnu = (np->prnu_coeff * signal) * (np->prnu_coeff * signal);
        double quant = np->quant_lsb_e * np->quant_lsb_e / 12.0;

        double noise = sqrt(shot + read + dark + prnu + quant);
        snr_out[i] = (noise > 0) ? signal / noise : 0.0;
    }
}

/*===========================================================================
 * L6: SNR vs gain
 *===========================================================================*/

void noise_vs_gain(double gain, const noise_params_t *np,
                    double *fwc_out, double *noise_out)
{
    if (np == NULL || fwc_out == NULL || noise_out == NULL) return;
    if (gain < 1.0) gain = 1.0;

    /* FWC scales inversely with gain */
    *fwc_out = 8500.0 / gain; /* Assume base FWC = 8500 e- */

    /* Input-referred noise: read noise + quantization divided by gain,
     * plus constant terms */
    *noise_out = sqrt(
        np->read_noise_e * np->read_noise_e +
        np->ktc_noise_e * np->ktc_noise_e +
        (np->quant_lsb_e * np->quant_lsb_e) / (12.0 * gain * gain)
    );
}

double noise_optimal_gain(double target_signal_e, const noise_params_t *np,
                           double max_gain)
{
    if (np == NULL) return 1.0;

    /* Find gain that minimizes input-referred noise while keeping
     * signal below FWC. Iterate over gain range. */
    double best_gain = 1.0;
    double best_quality = 0.0;

    double gain;
    for (gain = 1.0; gain <= max_gain; gain *= 1.2) {
        double fwc, noise;
        noise_vs_gain(gain, np, &fwc, &noise);

        if (target_signal_e * gain > fwc) continue; /* Would saturate */

        /* Quality metric: SNR at this gain */
        double snr = target_signal_e * gain / noise;
        if (snr > best_quality) {
            best_quality = snr;
            best_gain = gain;
        }
    }

    return best_gain;
}

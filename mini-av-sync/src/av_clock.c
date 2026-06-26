/**
 * @file av_clock.c
 * @brief Clock Recovery Implementation ¡ª PLL, Linear Regression, EWMA, LMS, Allan Variance, PCR
 *
 * Implements L3-L5: Mathematical structures and algorithms for clock recovery.
 *
 * Key algorithms:
 *   - Second-order PLL (Type II) with PI loop filter (L5)
 *   - Incremental linear regression with Welford's algorithm (L3, L5)
 *   - EWMA filter for jitter smoothing (L3, L5)
 *   - LMS adaptive clock tracking (L5)
 *   - Allan variance for clock stability (L4, L5)
 *   - PCR recovery per MPEG-2 Systems (L5, L6)
 *
 * @course MIT 6.003 ¡ì9 (Feedback), Berkeley EE123 ¡ì8 (Adaptive Filters),
 *         Stanford EE359 ¡ì8.4, ETH 227-0436, Cambridge (UK)
 */

#include "av_clock.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================================================================
 * L5: Second-Order PLL (Type II) Implementation
 * ================================================================
 *
 * A Phase-Locked Loop locks a local oscillator to an incoming reference.
 * In A/V sync, the "reference" is the master clock and the "oscillator"
 * is the slave clock's speed adjustment.
 *
 * Continuous-time transfer function (second-order, Type II):
 *
 *   H(s) = (2¦Î¦Øn¡¤s + ¦Øn2) / (s2 + 2¦Î¦Øn¡¤s + ¦Øn2)
 *
 * Discrete-time PI loop filter (bilinear transform approximation):
 *
 *   y[n] = Kp * e[n] + Ki * ¦²e[k]
 *
 * where:
 *   Kp ¡Ö 2¦Î¦Øn¡¤T  (proportional ¡ª corrects current phase error)
 *   Ki ¡Ö ¦Øn2¡¤T2   (integral  ¡ª corrects frequency offset / steady-state)
 *
 * Loop bandwidth BL ¡Ö ¦Øn¡¤(¦Î + 1/(4¦Î)) / 2¦Ð
 *
 * For A/V sync, typical parameters:
 *   ¦Øn = 0.1 to 1.0 Hz (very slow loop)
 *   ¦Î  = 0.707 (Butterworth / critically damped response)
 *
 * Slow loop ensures that the PLL filters out jitter but tracks slow drift.
 *
 * @reference Gardner, "Phaselock Techniques" (2005) Ch. 3
 * @reference Best, "Phase-Locked Loops" (2007) ¡ì3.2
 */

int av_pll_init(av_pll_params_t *pll, double natural_freq_hz,
                double damping_factor, double update_period)
{
    if (!pll) return -1;
    if (natural_freq_hz <= 0.0 || damping_factor <= 0.0 || update_period <= 0.0)
        return -1;

    memset(pll, 0, sizeof(*pll));

    pll->natural_freq_hz  = natural_freq_hz;
    pll->damping_factor   = damping_factor;
    pll->update_period    = update_period;

    /* Discrete PI gains derived from continuous-time parameters */
    double wnT = natural_freq_hz * update_period;
    pll->kp = 2.0 * damping_factor * wnT;
    pll->ki = wnT * wnT;

    pll->phase_error     = 0.0;
    pll->frequency_error = 0.0;
    pll->integrator_state = 0.0;

    return 0;
}

double av_pll_update(av_pll_params_t *pll, double measured_error)
{
    if (!pll) return 0.0;

    pll->phase_error = measured_error;

    /* PI loop filter */
    pll->integrator_state += pll->ki * measured_error;

    /* Integrator anti-windup: clamp to ¡À10% frequency range */
    double integrator_limit = 0.1;
    if (pll->integrator_state > integrator_limit)
        pll->integrator_state = integrator_limit;
    else if (pll->integrator_state < -integrator_limit)
        pll->integrator_state = -integrator_limit;

    /* Output = proportional + integral terms */
    double output = pll->kp * measured_error + pll->integrator_state;

    /* Update frequency error estimate */
    pll->frequency_error = output;

    /* Return frequency correction: 1.0 + output */
    /* Positive output ¡ú speed up (frequency was too slow) */
    return 1.0 + output;
}

void av_pll_reset(av_pll_params_t *pll)
{
    if (!pll) return;
    pll->phase_error      = 0.0;
    pll->frequency_error  = 0.0;
    pll->integrator_state = 0.0;
}

/* ================================================================
 * L5: EWMA Filter Implementation
 * ================================================================
 *
 * Exponentially Weighted Moving Average:
 *
 *   y[n] = ¦Á ¡¤ x[n] + (1-¦Á) ¡¤ y[n-1]
 *
 * Properties:
 *   - Equivalent to 1st-order IIR low-pass filter
 *   - Impulse response: h[n] = ¦Á(1-¦Á)^n ¡¤ u[n]
 *   - 3dB cutoff: fc ¡Ö ¦Á / (2¦ÐT) for small ¦Á
 *   - Time constant: ¦Ó = T / ¦Á  (time to reach 63% of step)
 *   - Steady-state: E[y] = E[x]
 *
 * Variance estimation uses a separate EWMA on squared deviations:
 *   s2[n] = ¦Â ¡¤ (x[n] - y[n])2 + (1-¦Â) ¡¤ s2[n-1]
 *
 * @reference Hunter, J.S. "The Exponentially Weighted Moving Average"
 *            Journal of Quality Technology, 1986
 * @reference Roberts, "Control Chart Tests Based on Geometric Moving Averages"
 *            Technometrics, 1959
 */

int av_ewma_init(av_ewma_filter_t *ewma, double alpha)
{
    if (!ewma) return -1;
    if (alpha <= 0.0 || alpha > 1.0) return -1;

    ewma->alpha       = alpha;
    ewma->current_value = 0.0;
    ewma->variance    = 0.0;
    ewma->initialized = 0;

    return 0;
}

double av_ewma_update(av_ewma_filter_t *ewma, double sample)
{
    if (!ewma) return 0.0;

    if (!ewma->initialized) {
        ewma->current_value = sample;
        ewma->variance      = 0.0;
        ewma->initialized   = 1;
        return sample;
    }

    double old_value = ewma->current_value;

    /* Update EWMA */
    ewma->current_value = ewma->alpha * sample
                        + (1.0 - ewma->alpha) * old_value;

    /* Update variance estimate using separate EWMA */
    double deviation = sample - ewma->current_value;
    double beta = 0.1;  /* Slower smoothing for variance */
    ewma->variance = beta * (deviation * deviation)
                   + (1.0 - beta) * ewma->variance;

    return ewma->current_value;
}

void av_ewma_reset(av_ewma_filter_t *ewma)
{
    if (!ewma) return;
    ewma->current_value = 0.0;
    ewma->variance      = 0.0;
    ewma->initialized   = 0;
}

double av_ewma_time_constant(const av_ewma_filter_t *ewma, double sample_period)
{
    if (!ewma || ewma->alpha <= 0.0) return 0.0;
    /* ¦Ó = T/¦Á (time for step response to reach 1 - 1/e ¡Ö 63%) */
    return sample_period / ewma->alpha;
}

/* ================================================================
 * L5: Incremental Linear Regression (Welford's Algorithm)
 * ================================================================
 *
 * Estimates parameters ¦Á, ¦Â of the affine clock model:
 *   t_slave = ¦Á ¡¤ t_master + ¦Â
 *
 * Using linear regression:
 *   ¦Á = (N¡¤¦²xy - ¦²x¡¤¦²y) / (N¡¤¦²x2 - (¦²x)2)
 *   ¦Â = (¦²y - ¦Á¡¤¦²x) / N
 *
 * Welford's incremental algorithm avoids catastrophic cancellation
 * and provides O(1) per-update complexity.
 *
 * The incremental formulas:
 *   ¦Ä = x - mean_x  (before updating mean)
 *   mean_x' = mean_x + ¦Ä/N
 *   mean_y' = mean_y + (y - mean_y)/N
 *   M2_x' = M2_x + ¦Ä * (x - mean_x')
 *   C_xy' = C_xy + ¦Ä * (y - mean_y')
 *
 * Then:
 *   ¦Á = C_xy / M2_x
 *   ¦Â = mean_y - ¦Á * mean_x
 *
 * @reference Welford, B.P. "Note on a Method for Calculating Corrected
 *            Sums of Squares and Products", Technometrics, 1962
 * @reference Chan, Golub, LeVeque, "Algorithms for Computing the Sample
 *            Variance", American Statistician, 1983
 */

int av_linreg_init(av_linreg_t *lr)
{
    if (!lr) return -1;
    memset(lr, 0, sizeof(*lr));
    return 0;
}

void av_linreg_add_sample(av_linreg_t *lr, double master_time, double slave_time)
{
    if (!lr) return;

    lr->count++;
    double n = (double)lr->count;

    /* Welford's incremental update for mean and covariance */
    if (lr->count == 1) {
        lr->mean_x = master_time;
        lr->mean_y = slave_time;
        lr->m2_x   = 0.0;
        lr->m2_y   = 0.0;
        lr->cov_xy = 0.0;

        /* Also update batch sums for direct formula fallback */
        lr->sum_x  = master_time;
        lr->sum_y  = slave_time;
        lr->sum_xx = master_time * master_time;
        lr->sum_xy = master_time * slave_time;
        lr->sum_yy = slave_time * slave_time;
    } else {
        double delta_x = master_time - lr->mean_x;
        double delta_y = slave_time - lr->mean_y;

        lr->mean_x += delta_x / n;
        lr->mean_y += delta_y / n;

        lr->m2_x   += delta_x * (master_time - lr->mean_x);
        lr->m2_y   += delta_y * (slave_time - lr->mean_y);
        lr->cov_xy += delta_x * (slave_time - lr->mean_y);

        /* Batch sums (for verification and batch computation) */
        lr->sum_x  += master_time;
        lr->sum_y  += slave_time;
        lr->sum_xx += master_time * master_time;
        lr->sum_xy += master_time * slave_time;
        lr->sum_yy += slave_time * slave_time;
    }
}

int av_linreg_fit(const av_linreg_t *lr, av_clock_model_t *model)
{
    if (!lr || !model) return -1;
    if (lr->count < 2) return -1;

    double n = (double)lr->count;
    double scale, offset;

    /* Use incremental results for better numerical stability */
    if (fabs(lr->m2_x) > 1e-15) {
        /* Slope: ¦Á = cov_xy / m2_x */
        scale  = lr->cov_xy / lr->m2_x;
        offset = lr->mean_y - scale * lr->mean_x;
    } else {
        /* Degenerate case: all x values identical (no variance in master time) */
        scale  = 1.0;
        offset = lr->sum_y / n - lr->sum_x / n;
    }

    /* Compute R-squared (coefficient of determination) */
    /* R2 = (cov_xy)2 / (m2_x * m2_y) */
    double r_squared = 0.0;
    if (fabs(lr->m2_x * lr->m2_y) > 1e-30) {
        r_squared = (lr->cov_xy * lr->cov_xy) / (lr->m2_x * lr->m2_y);
        if (r_squared > 1.0) r_squared = 1.0;
        if (r_squared < 0.0) r_squared = 0.0;
    }

    memset(model, 0, sizeof(*model));
    model->scale         = scale;
    model->offset_seconds = offset;
    model->skew_ppm      = (scale - 1.0) * 1e6;
    model->drift_ppm_per_s = 0.0;   /* Not computed from single regression */
    model->r_squared     = r_squared;
    model->num_samples   = lr->count;

    return 0;
}

void av_linreg_reset(av_linreg_t *lr)
{
    if (!lr) return;
    memset(lr, 0, sizeof(*lr));
}

/* ================================================================
 * L5: Allan Variance Implementation
 * ================================================================
 *
 * Allan variance ¦Ò2y(¦Ó) measures clock stability as a function
 * of averaging time ¦Ó.
 *
 * For N fractional frequency samples y[i]:
 *   ¦Ò2y(¦Ó = m¡¤¦Ó?) = 1/(2(N-2m+1)) ¡¤ ¦²_{i=1}^{N-2m+1} (?_{i+m} - ?_i)2
 *
 * where ?_i is the average over m samples starting at i.
 *
 * Using phase data x[i] (seconds):
 *   ¦Ò2y(¦Ó) = 1/(2¡¤¦Ó2¡¤(N-2m)) ¡¤ ¦² (x[i+2m] - 2¡¤x[i+m] + x[i])2
 *
 * Noise type identification by slope of log ¦Ò(¦Ó) vs log ¦Ó:
 *   White PM:        ¦Ò ¡Ø ¦Ó?1    (slope = -1)
 *   Flicker PM:      ¦Ò ¡Ø ¦Ó??¡¤?  (slope = -0.5)
 *   White FM:        ¦Ò ¡Ø ¦Ó??¡¤?  (slope = -0.5)
 *   Flicker FM:      ¦Ò ¡Ø ¦Ó?     (slope = 0, "flicker floor")
 *   Random Walk FM:  ¦Ò ¡Ø ¦Ó?¡¤?   (slope = +0.5)
 *
 * @reference Allan, D.W. "Statistics of Atomic Frequency Standards"
 *            Proc. IEEE, 1966
 * @reference IEEE Std 1139-2008 "Standard Definitions of Physical
 *            Quantities for Fundamental Frequency and Time Metrology"
 */

int av_allan_var_init(av_allan_var_t *av, double tau0, int buffer_size)
{
    if (!av || tau0 <= 0.0 || buffer_size < 4) return -1;

    av->phase_data = (double *)calloc((size_t)buffer_size, sizeof(double));
    if (!av->phase_data) return -1;

    av->tau0        = tau0;
    av->buffer_size = buffer_size;
    av->write_index = 0;
    av->num_valid   = 0;

    return 0;
}

void av_allan_var_free(av_allan_var_t *av)
{
    if (!av) return;
    free(av->phase_data);
    av->phase_data = NULL;
    av->buffer_size = 0;
    av->num_valid = 0;
}

void av_allan_var_add(av_allan_var_t *av, double phase_seconds)
{
    if (!av || !av->phase_data) return;

    av->phase_data[av->write_index] = phase_seconds;
    av->write_index = (av->write_index + 1) % av->buffer_size;
    if (av->num_valid < av->buffer_size) {
        av->num_valid++;
    }
}

double av_allan_var_compute(const av_allan_var_t *av, int m)
{
    if (!av || !av->phase_data) return -1.0;
    if (m < 1) return -1.0;

    int N = av->num_valid;
    /* Need at least 3*m samples: N >= 2*m for the sum + m for indexing */
    if (N < 2 * m + 1) return -1.0;

    double tau = av->tau0 * (double)m;
    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < N - 2 * m; i++) {
        /* Get x[i], x[i+m], x[i+2*m] from ring buffer */
        /* The buffer stores samples in order; handle ring offset */
        int idx_start = (av->write_index - N + av->buffer_size) % av->buffer_size;

        double x_i   = av->phase_data[(idx_start + i) % av->buffer_size];
        double x_im  = av->phase_data[(idx_start + i + m) % av->buffer_size];
        double x_i2m = av->phase_data[(idx_start + i + 2 * m) % av->buffer_size];

        double diff = x_i2m - 2.0 * x_im + x_i;
        sum += diff * diff;
        count++;
    }

    if (count == 0) return -1.0;

    /* ¦Ò2y = sum / (2 * ¦Ó2 * count) ¡ú ¦Òy = sqrt(sum / (2 * ¦Ó2 * count)) */
    double variance = sum / (2.0 * tau * tau * (double)count);
    return sqrt(variance);
}

/* ================================================================
 * L5: LMS Adaptive Clock Tracking
 * ================================================================
 *
 * LMS (Least Mean Squares) adaptively tracks the affine clock
 * parameters in real time.
 *
 * Model: t_slave = offset + scale * t_master
 *
 * LMS update (stochastic gradient descent):
 *   pred = offset + scale * t_master
 *   error = t_slave - pred
 *   offset = offset + ¦Ì * error * 1.0
 *   scale  = scale  + ¦Ì * error * t_master
 *
 * This is a 2-tap adaptive filter. It converges to the Wiener
 * solution (optimal linear MMSE estimate) for stationary processes.
 *
 * Step size ¦Ì:
 *   - Too large: fast convergence but large misadjustment (noisy)
 *   - Too small: slow convergence but small misadjustment (smooth)
 *   - Stability: 0 < ¦Ì < 2/¦Ëmax where ¦Ëmax ¡Ö E[x2]
 *
 * For clock tracking, typical ¦Ì ¡Ö 1e-6 to 1e-4.
 *
 * @reference Widrow & Hoff, "Adaptive Switching Circuits" (1960)
 * @reference Haykin, "Adaptive Filter Theory" (2002) Ch. 6
 */

int av_lms_clock_init(av_lms_clock_t *lms, double mu, double init_scale)
{
    if (!lms) return -1;
    if (mu <= 0.0) return -1;

    lms->offset          = 0.0;
    lms->scale           = init_scale;
    lms->mu              = mu;
    lms->last_master_time = 0.0;
    lms->last_slave_time  = 0.0;
    lms->error           = 0.0;
    lms->initialized     = 0;

    return 0;
}

double av_lms_clock_update(av_lms_clock_t *lms, double master_time,
                            double slave_time)
{
    if (!lms) return 0.0;

    if (!lms->initialized) {
        lms->last_master_time = master_time;
        lms->last_slave_time  = slave_time;
        lms->initialized      = 1;
        lms->error            = 0.0;
        return 0.0;
    }

    /* Prediction: y_hat = offset + scale * x */
    double prediction = lms->offset + lms->scale * master_time;

    /* Error */
    lms->error = slave_time - prediction;

    /* LMS weight update */
    /* ¦Ì normalized by (1 + x2) to improve conditioning */
    double norm_factor = 1.0 + master_time * master_time;
    double mu_norm = lms->mu / norm_factor;

    lms->offset += mu_norm * lms->error * 1.0;
    lms->scale  += mu_norm * lms->error * master_time;

    /* Clamp scale to reasonable range (0.999 to 1.001 ¡ú ¡À1000 ppm) */
    if (lms->scale > 1.001)  lms->scale = 1.001;
    if (lms->scale < 0.999)  lms->scale = 0.999;

    lms->last_master_time = master_time;
    lms->last_slave_time  = slave_time;

    return lms->error;
}

double av_lms_clock_predict(const av_lms_clock_t *lms, double master_time)
{
    if (!lms) return 0.0;
    return lms->offset + lms->scale * master_time;
}

/* ================================================================
 * L5: PCR Recovery (MPEG-2 Systems)
 * ================================================================
 *
 * PCR recovery is the process of reconstructing the encoder's STC
 * at the decoder using transmitted PCR values.
 *
 * Algorithm (ISO/IEC 13818-1 Annex D):
 *   1. When a PCR arrives at local time t_local, record (t_local, PCR_time)
 *   2. Use linear regression on recent pairs to estimate the clock model
 *   3. STC(t) = scale * t + offset
 *
 * PCR arrival jitter is filtered by:
 *   - Rejecting outliers (large residuals after initial fit)
 *   - Using only PCRs without discontinuity indicator
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.3.4, Annex D
 */

int av_pcr_recover(const av_pcr_t *pcr, av_linreg_t *lr, av_clock_model_t *model)
{
    if (!pcr || !lr || !model) return -1;

    /* Reject discontinuous PCRs (e.g., after a stream splice) */
    if (pcr->discontinuity) {
        /* Reset regression state on discontinuity */
        av_linreg_reset(lr);
        return -1;
    }

    /* Convert PCR to seconds and local arrival to seconds */
    double pcr_seconds = av_sync_pcr_to_seconds(pcr);
    double local_seconds = (double)pcr->arrival_time / (double)AV_SYNC_CLOCK_NS;

    /* Add sample to regression */
    av_linreg_add_sample(lr, local_seconds, pcr_seconds);

    /* Fit model */
    return av_linreg_fit(lr, model);
}

/**
 * STC interpolation: given a clock model and a local time, estimate the STC.
 *
 * STC(local_time) = model->scale * local_time + model->offset_seconds
 *
 * This allows the decoder to estimate the encoder's clock for any local time
 * between or after PCR arrivals.
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.2
 */
double av_stc_interpolate(const av_clock_model_t *model, double local_time)
{
    if (!model) return local_time;  /* Passthrough if no model */
    return model->scale * local_time + model->offset_seconds;
}

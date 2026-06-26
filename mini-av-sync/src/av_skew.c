/**
 * @file av_skew.c
 * @brief Clock Skew Detection and Compensation
 *
 * Implements L2-L5:
 *   - Multi-method skew estimation (PTS pair, linear regression,
 *     Theil-Sen robust, Kalman, LMS) (L5)
 *   - Kalman filter for clock state tracking (L4, L5)
 *   - Direct pair skew computation (L2)
 *   - Theil-Sen robust estimator (L5)
 *
 * @course Stanford EE359, ETH 227-0436, Michigan EECS 411
 */

#include "av_skew.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ================================================================
 * L5: Skew State Management
 * ================================================================ */

int av_skew_init(av_skew_state_t *skew, av_skew_method_t method)
{
    if (!skew) return -1;
    if (method < AV_SKEW_METHOD_PTS_PAIR || method > AV_SKEW_METHOD_LMS)
        return -1;

    memset(skew, 0, sizeof(*skew));
    skew->method     = method;
    skew->skew_ppm   = 0.0;
    skew->offset_ms  = 0.0;
    skew->confidence = 0.0;

    av_linreg_init(&skew->linreg);
    av_ewma_init(&skew->ewma_skew, 0.1);
    av_ewma_init(&skew->ewma_offset, 0.1);

    if (method == AV_SKEW_METHOD_LMS) {
        av_lms_clock_init(&skew->lms, 1e-5, 1.0);
    }

    return 0;
}

/**
 * Add a (master_pts, slave_pts) measurement pair to the skew estimator.
 *
 * Depending on the estimation method:
 *
 * PTS_PAIR:   Stores two pairs and computes direct skew via
 *             av_skew_direct_pair(). Fast but noisy.
 *
 * LINEAR_REG: Feeds pairs into an incremental linear regression.
 *             Accurate after sufficient samples (N > 10).
 *
 * THEIL_SEN:  Feeds pairs into Theil-Sen robust estimator.
 *             Robust to outliers but O(N，log N) per update.
 *
 * KALMAN:     Not directly used here (see av_kalman_clock_update).
 *             Requires time-stamped offset measurements.
 *
 * LMS:        Updates LMS adaptive filter weights.
 *             Tracks slowly varying skew in real time.
 *
 * @return Current skew estimate in ppm
 */
double av_skew_add_measurement(av_skew_state_t *skew,
                               int64_t master_pts, int64_t slave_pts)
{
    if (!skew) return 0.0;

    double master_sec = av_sync_pts_to_seconds(master_pts);
    double slave_sec  = av_sync_pts_to_seconds(slave_pts);

    skew->sample_count++;

    switch (skew->method) {
    case AV_SKEW_METHOD_PTS_PAIR:
    {
        /* Direct pair: use current and previous measurement */
        if (skew->sample_count >= 2) {
            double sk_ppm;
            if (av_skew_direct_pair(skew->last_master_pts, skew->last_slave_pts,
                                    master_pts, slave_pts, &sk_ppm) == 0) {
                /* EWMA-smooth the raw measurement */
                skew->skew_ppm = av_ewma_update(&skew->ewma_skew, sk_ppm);
            }
        }
        /* Compute offset from most recent pair */
        skew->offset_ms = (slave_sec - master_sec) * 1000.0;
        break;
    }

    case AV_SKEW_METHOD_LINEAR_REG:
    {
        av_linreg_add_sample(&skew->linreg, master_sec, slave_sec);
        if (skew->sample_count >= 3) {
            av_clock_model_t model;
            if (av_linreg_fit(&skew->linreg, &model) == 0) {
                skew->skew_ppm  = model.skew_ppm;
                skew->offset_ms = model.offset_seconds * 1000.0;
                skew->confidence = model.r_squared;
            }
        }
        break;
    }

    case AV_SKEW_METHOD_LMS:
    {
        double error = av_lms_clock_update(&skew->lms, master_sec, slave_sec);
        skew->skew_ppm  = (skew->lms.scale - 1.0) * 1e6;
        skew->offset_ms = skew->lms.offset * 1000.0;
        /* Confidence improves as error decreases */
        if (fabs(error) < 1e-6) {
            skew->confidence = 0.99;
        } else {
            skew->confidence = 1.0 / (1.0 + fabs(error) * 1000.0);
            if (skew->confidence > 0.99) skew->confidence = 0.99;
        }
        break;
    }

    case AV_SKEW_METHOD_THEIL_SEN:
    case AV_SKEW_METHOD_KALMAN:
    default:
        /* These require setup via their respective APIs */
        /* Fallback: compute simple offset */
        skew->offset_ms = (slave_sec - master_sec) * 1000.0;
        break;
    }

    /* Store last values for next iteration */
    skew->last_master_pts  = master_pts;
    skew->last_slave_pts   = slave_pts;
    skew->last_master_sec  = master_sec;
    skew->last_slave_sec   = slave_sec;

    /* Smooth the skew estimate */
    skew->skew_ppm = av_ewma_update(&skew->ewma_skew, skew->skew_ppm);
    skew->offset_ms = av_ewma_update(&skew->ewma_offset, skew->offset_ms);

    return skew->skew_ppm;
}

double av_skew_get_ppm(const av_skew_state_t *skew)
{
    if (!skew) return 0.0;
    return skew->skew_ppm;
}

double av_skew_get_offset_ms(const av_skew_state_t *skew)
{
    if (!skew) return 0.0;
    return skew->offset_ms;
}

double av_skew_get_confidence(const av_skew_state_t *skew)
{
    if (!skew) return 0.0;
    return skew->confidence;
}

void av_skew_reset(av_skew_state_t *skew)
{
    if (!skew) return;
    av_skew_method_t method = skew->method;
    memset(skew, 0, sizeof(*skew));
    skew->method = method;
    av_linreg_init(&skew->linreg);
    av_ewma_init(&skew->ewma_skew, 0.1);
    av_ewma_init(&skew->ewma_offset, 0.1);
    if (method == AV_SKEW_METHOD_LMS) {
        av_lms_clock_init(&skew->lms, 1e-5, 1.0);
    }
}

/* ================================================================
 * L5: Direct Pair Skew Computation
 * ================================================================
 *
 * Simplest skew estimator: compare two (master, slave) pairs.
 *
 * Master clock: Cm(t)
 * Slave clock:  Cs(t) = α，Cm(t) + β
 *
 * Given (Cm1, Cs1) and (Cm2, Cs2):
 *   α = (Cs2 - Cs1) / (Cm2 - Cm1)
 *   skew_ppm = (α - 1) * 1e6
 *
 * Limitation: Sensitive to measurement jitter.
 * Use EWMA filtering after this for practical use.
 *
 * @reference Moon, Skelly, Towsley (1999) ′2
 */

int av_skew_direct_pair(int64_t master1, int64_t slave1,
                        int64_t master2, int64_t slave2,
                        double *skew_ppm_out)
{
    if (!skew_ppm_out) return -1;

    int64_t dm = master2 - master1;
    int64_t ds = slave2 - slave1;

    /* Guard against zero or negative master delta */
    if (dm <= 0) {
        *skew_ppm_out = 0.0;
        return -1;
    }

    double alpha = (double)ds / (double)dm;
    *skew_ppm_out = (alpha - 1.0) * 1e6;

    return 0;
}

/* ================================================================
 * L4, L5: Kalman Filter for Clock State Estimation
 * ================================================================
 *
 * Tracks clock offset and skew using a 2-state Kalman filter.
 *
 * State: x = [offset, skew]?
 *
 * Process model (constant velocity = constant skew):
 *   offset[k+1] = offset[k] + skew[k] * Δt + w_offset
 *   skew[k+1]   = skew[k]                    + w_skew
 *
 * In matrix form: x[k+1] = F[k] ， x[k] + w[k]
 *   F = [1  Δt]
 *       [0   1 ]
 *   Q = E[w，w?] = process noise covariance
 *
 * Measurement model:
 *   z[k] = offset[k] + v[k]     (measure offset directly)
 *   H = [1  0]                   (only offset is measured)
 *   R = E[v2] = measurement noise variance
 *
 * Kalman filter equations (predict + update):
 *
 * Predict:
 *   x?? = F ， x?
 *   P? = F ， P ， F? + Q
 *
 * Update:
 *   y = z - H ， x??                (innovation)
 *   S = H ， P? ， H? + R           (innovation covariance)
 *   K = P? ， H? / S               (Kalman gain)
 *   x? = x?? + K ， y               (state update)
 *   P = (I - K ， H) ， P?          (covariance update)
 *
 * The scalar formulation (since H = [1 0]) simplifies to:
 *   K[0] = P?[0,0] / (P?[0,0] + R)
 *   K[1] = P?[1,0] / (P?[0,0] + R)
 *
 * @reference Kalman, R.E. "A New Approach to Linear Filtering
 *            and Prediction Problems", Trans. ASME, 1960
 * @reference Brown & Hwang, "Introduction to Random Signals
 *            and Applied Kalman Filtering" (2012) Ch. 5
 */

int av_kalman_clock_init(av_kalman_clock_t *kf,
                         double q_offset, double q_skew, double r_meas)
{
    if (!kf) return -1;
    if (q_offset < 0.0 || q_skew < 0.0 || r_meas <= 0.0) return -1;

    memset(kf, 0, sizeof(*kf));

    /* Initial state: assume perfect sync */
    kf->offset = 0.0;
    kf->skew   = 0.0;

    /* Initial covariance: high uncertainty */
    kf->p00 = 1.0;       /* 1 second2 offset uncertainty */
    kf->p01 = 0.0;
    kf->p10 = 0.0;
    kf->p11 = 1e-10;     /* Skew uncertainty (dimensionless2) */

    /* Noise parameters */
    kf->q_offset     = q_offset;
    kf->q_skew       = q_skew;
    kf->r_measurement = r_meas;

    kf->last_time   = 0.0;
    kf->initialized = 0;

    return 0;
}

double av_kalman_clock_update(av_kalman_clock_t *kf,
                               double measured_offset, double current_time)
{
    if (!kf) return 0.0;

    if (!kf->initialized) {
        kf->offset       = measured_offset;
        kf->skew         = 0.0;
        kf->last_time    = current_time;
        kf->initialized  = 1;
        return measured_offset;
    }

    double dt = current_time - kf->last_time;
    if (dt <= 0.0) {
        /* Time not advancing: return current estimate */
        return kf->offset;
    }

    /* =========================================
     * Predict step
     * ========================================= */

    /* State prediction: x?? = F ， x? */
    double offset_pred = kf->offset + kf->skew * dt;
    double skew_pred   = kf->skew;

    /* Covariance prediction: P? = F，P，F? + Q */
    /* F = [1 dt; 0 1] */
    /* F，P，F? = [p00 + 2*dt*p01 + dt2*p11,  p01 + dt*p11]
     *          [p10 + dt*p11,              p11           ] */
    double p00_pred = kf->p00 + 2.0 * dt * kf->p01 + dt * dt * kf->p11
                      + kf->q_offset * dt;
    double p01_pred = kf->p01 + dt * kf->p11;
    double p10_pred = p01_pred;
    double p11_pred = kf->p11 + kf->q_skew * dt;

    /* =========================================
     * Update step
     * ========================================= */

    /* Innovation: y = z - H，x?? */
    double innovation = measured_offset - offset_pred;

    /* Innovation covariance: S = H，P?，H? + R = p00_pred + R */
    double S = p00_pred + kf->r_measurement;

    /* Kalman gain: K = P?，H? / S */
    double K0 = p00_pred / S;    /* Gain for offset */
    double K1 = p10_pred / S;    /* Gain for skew */

    /* State update: x? = x?? + K，y */
    kf->offset = offset_pred + K0 * innovation;
    kf->skew   = skew_pred   + K1 * innovation;

    /* Covariance update: P = (I - K，H)，P? */
    kf->p00 = (1.0 - K0) * p00_pred;
    kf->p01 = (1.0 - K0) * p01_pred;
    kf->p10 = p10_pred - K1 * p00_pred;
    kf->p11 = p11_pred - K1 * p01_pred;

    kf->last_time = current_time;

    return kf->offset;
}

void av_kalman_clock_predict(const av_kalman_clock_t *kf, double future_time,
                              double *offset_out, double *skew_out)
{
    if (!kf) {
        if (offset_out) *offset_out = 0.0;
        if (skew_out)   *skew_out   = 0.0;
        return;
    }

    double dt = future_time - kf->last_time;
    if (offset_out) *offset_out = kf->offset + kf->skew * dt;
    if (skew_out)   *skew_out   = kf->skew;
}

/* ================================================================
 * L5: Theil-Sen Robust Estimator
 * ================================================================
 *
 * Theil-Sen is a robust linear regression method that computes
 * the median of all pairwise slopes.
 *
 * For N points: O(N2) pairwise slopes; median via sorting O(N2，log N2).
 *
 * This implementation maintains a ring buffer of recent (master, slave)
 * pairs and recomputes the median slope on each addition.
 *
 * Advantages over least squares:
 *   - Breakdown point of ~29% (can tolerate up to 29% outliers)
 *   - Non-parametric: no distribution assumption
 *   - Robust to gross errors in timestamp measurements
 *
 * @reference Theil, H. "A Rank-Invariant Method of Linear and Polynomial
 *            Regression Analysis", Proc. Royal Netherlands Academy, 1950
 * @reference Sen, P.K. "Estimates of the Regression Coefficient Based on
 *            Kendall's Tau", J. American Statistical Association, 1968
 */

static int ts_compare_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

int av_theil_sen_init(av_theil_sen_t *ts, int buffer_size)
{
    if (!ts || buffer_size < 3) return -1;

    ts->master_times = (double *)calloc((size_t)buffer_size, sizeof(double));
    ts->slave_times  = (double *)calloc((size_t)buffer_size, sizeof(double));
    /* Max pairwise slopes: N*(N-1)/2 */
    ts->slopes       = (double *)calloc((size_t)(buffer_size * (buffer_size - 1) / 2),
                                        sizeof(double));
    if (!ts->master_times || !ts->slave_times || !ts->slopes) {
        free(ts->master_times);
        free(ts->slave_times);
        free(ts->slopes);
        return -1;
    }

    ts->buffer_size     = buffer_size;
    ts->count           = 0;
    ts->write_idx       = 0;
    ts->slope_median    = 1.0;
    ts->intercept_median = 0.0;

    return 0;
}

void av_theil_sen_free(av_theil_sen_t *ts)
{
    if (!ts) return;
    free(ts->master_times);
    free(ts->slave_times);
    free(ts->slopes);
    memset(ts, 0, sizeof(*ts));
}

void av_theil_sen_add(av_theil_sen_t *ts, double master_time, double slave_time)
{
    if (!ts || !ts->master_times || !ts->slave_times) return;

    /* Add to ring buffer */
    ts->master_times[ts->write_idx] = master_time;
    ts->slave_times[ts->write_idx]  = slave_time;
    ts->write_idx = (ts->write_idx + 1) % ts->buffer_size;
    if (ts->count < ts->buffer_size) {
        ts->count++;
    }

    if (ts->count < 2) {
        ts->slope_median = 1.0;
        ts->intercept_median = slave_time - master_time;
        return;
    }

    /* Compute all pairwise slopes */
    int n = ts->count;
    int slope_count = 0;

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double dx = ts->master_times[j] - ts->master_times[i];
            if (fabs(dx) > 1e-15) {
                double dy = ts->slave_times[j] - ts->slave_times[i];
                ts->slopes[slope_count++] = dy / dx;
            }
        }
    }

    if (slope_count == 0) {
        ts->slope_median = 1.0;
    } else {
        /* Sort slopes and take median */
        qsort(ts->slopes, (size_t)slope_count, sizeof(double), ts_compare_double);

        if (slope_count % 2 == 1) {
            ts->slope_median = ts->slopes[slope_count / 2];
        } else {
            ts->slope_median = (ts->slopes[slope_count / 2 - 1]
                               + ts->slopes[slope_count / 2]) * 0.5;
        }

        /* Intercept: median of (y_i - slope * x_i) */
        double intercept = slave_time - ts->slope_median * master_time;
        ts->intercept_median = intercept;
    }
}

double av_theil_sen_get_slope(const av_theil_sen_t *ts)
{
    if (!ts) return 1.0;
    return ts->slope_median;
}

double av_theil_sen_get_intercept(const av_theil_sen_t *ts)
{
    if (!ts) return 0.0;
    return ts->intercept_median;
}

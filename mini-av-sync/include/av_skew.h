/**
 * @file av_skew.h
 * @brief Clock Skew Detection and Compensation
 *
 * Covers L2 Concepts: Clock skew (rate mismatch), clock offset,
 * frequency drift, temperature-induced drift.
 *
 * Covers L4 Fundamental Laws:
 *   - Affine clock model: T_slave = ¦Á¡¤T_master + ¦Â
 *   - Skew-varies-slowly assumption for filtering
 *   - Clock discipline principles (RFC 5905 / NTP)
 *
 * Covers L5 Algorithms:
 *   - Direct skew computation from PTS pairs
 *   - Adaptive skew tracking with bounded drift rate
 *   - Sample-and-hold skew compensator
 *   - Kalman filter for clock state estimation
 *   - Multi-sample robust skew estimation (Theil-Sen)
 *
 * References:
 * - Mills, "Computer Network Time Synchronization" (2011) ¡ì3-4
 * - RFC 5905 (NTPv4) clock discipline algorithm
 * - Moon, Skelly, Towsley, "Estimation and Removal of Clock Skew" (1999)
 *
 * @course Stanford EE359 ¡ì8.4, ETH 227-0436 Communications,
 *         Michigan EECS 411 Microwave
 */

#ifndef AV_SKEW_H
#define AV_SKEW_H

#include <stdint.h>
#include <stddef.h>
#include "av_sync_core.h"
#include "av_clock.h"

/* ================================================================
 * L2: Skew Detection Methods
 * ================================================================ */

/** @enum av_skew_method_t
 * @brief Algorithm for estimating clock skew between two streams.
 */
typedef enum {
    AV_SKEW_METHOD_PTS_PAIR   = 0,  /**< Direct PTS pair comparison */
    AV_SKEW_METHOD_LINEAR_REG = 1,  /**< Linear regression over window */
    AV_SKEW_METHOD_THEIL_SEN  = 2,  /**< Theil-Sen robust estimator */
    AV_SKEW_METHOD_KALMAN     = 3,  /**< Kalman filter tracking */
    AV_SKEW_METHOD_LMS        = 4   /**< LMS adaptive tracking */
} av_skew_method_t;

/* ================================================================
 * L2: Skew State
 * ================================================================ */

/** @struct av_skew_state_t
 * @brief Clock skew estimation and compensation state.
 *
 * Maintains the estimated affine relationship between two clocks
 * and provides corrected timestamps for the slave stream.
 */
typedef struct {
    av_skew_method_t  method;         /**< Estimation method */
    double            skew_ppm;       /**< Current skew estimate (ppm) */
    double            offset_ms;      /**< Current offset estimate (ms) */
    double            skew_rate_ppm_per_s; /**< Rate of skew change */
    double            confidence;     /**< Estimation confidence (0-1) */
    av_linreg_t       linreg;         /**< Linear regression accumulator */
    av_ewma_filter_t  ewma_skew;      /**< EWMA-smoothed skew */
    av_ewma_filter_t  ewma_offset;    /**< EWMA-smoothed offset */
    av_lms_clock_t    lms;            /**< LMS tracker */
    uint64_t          sample_count;   /**< Number of measurements taken */
    int64_t           last_master_pts; /**< Last master PTS */
    int64_t           last_slave_pts;  /**< Last slave PTS */
    double            last_master_sec; /**< Last master time (seconds) */
    double            last_slave_sec;  /**< Last slave time (seconds) */
} av_skew_state_t;

/* ================================================================
 * L4: Kalman Filter for Clock State Estimation
 * ================================================================ */

/** @struct av_kalman_clock_t
 * @brief Kalman filter for tracking clock offset and skew.
 *
 * State vector: x = [offset, skew]?
 *
 * Process model (constant skew, varying offset):
 *   x[k+1] = F¡¤x[k] + w[k]
 *   F = [1  ¦¤t; 0  1]
 *   w ~ N(0, Q)  (process noise)
 *
 * Measurement model:
 *   z[k] = H¡¤x[k] + v[k]
 *   H = [1  0]
 *   v ~ N(0, R)  (measurement noise)
 *
 * Kalman gain K balances process noise Q vs measurement noise R.
 * Large Q/R ¡ú trusts measurements more (fast tracking, noisy estimate)
 * Small Q/R ¡ú trusts model more (smooth, but slow to respond)
 *
 * @reference Kalman, "A New Approach to Linear Filtering" (1960)
 * @reference Brown & Hwang, "Introduction to Random Signals" (2012) Ch. 5
 */
typedef struct {
    double offset;          /**< Estimated clock offset (seconds) */
    double skew;            /**< Estimated clock skew (dimensionless) */
    double p00;             /**< Covariance P[0,0] (offset variance) */
    double p01;             /**< Covariance P[0,1] (offset-skew covariance) */
    double p10;             /**< Covariance P[1,0] (skew-offset covariance) */
    double p11;             /**< Covariance P[1,1] (skew variance) */
    double q_offset;        /**< Process noise: offset (seconds2) */
    double q_skew;          /**< Process noise: skew (dimensionless2) */
    double r_measurement;   /**< Measurement noise (seconds2) */
    double last_time;       /**< Last update time (seconds) */
    int    initialized;     /**< Whether filter has been initialized */
} av_kalman_clock_t;

/* ================================================================
 * L5: Skew Estimation API
 * ================================================================ */

int av_skew_init(av_skew_state_t *skew, av_skew_method_t method);

/**
 * @brief Feed a (master, slave) PTS pair into the skew estimator.
 *
 * Depending on the chosen method, this updates the internal
 * linear regression, LMS filter, or direct pair tracker.
 *
 * @param skew        Skew state
 * @param master_pts  Master stream PTS (90 kHz)
 * @param slave_pts   Slave stream PTS (90 kHz)
 * @return Current skew estimate in ppm, or 0.0 if insufficient data
 *
 * @complexity O(1) for all methods
 */
double av_skew_add_measurement(av_skew_state_t *skew,
                               int64_t master_pts, int64_t slave_pts);

/**
 * @brief Get the current skew estimate.
 * @param skew Skew state
 * @return Skew in ppm (positive = slave clock is faster)
 */
double av_skew_get_ppm(const av_skew_state_t *skew);

/**
 * @brief Get the current offset estimate.
 * @param skew Skew state
 * @return Offset in milliseconds
 */
double av_skew_get_offset_ms(const av_skew_state_t *skew);

/**
 * @brief Get estimation confidence (0.0 = none, 1.0 = fully converged).
 * @param skew Skew state
 * @return Confidence value
 */
double av_skew_get_confidence(const av_skew_state_t *skew);

/**
 * @brief Reset the skew estimator.
 * @param skew Skew state
 */
void av_skew_reset(av_skew_state_t *skew);

/* ================================================================
 * L5: Kalman Filter API
 * ================================================================ */

int av_kalman_clock_init(av_kalman_clock_t *kf,
                         double q_offset, double q_skew, double r_meas);

/**
 * @brief Kalman filter update step with a new offset measurement.
 *
 * Predict step (time update):
 *   x??[k] = F¡¤x?[k-1]
 *   P?[k] = F¡¤P[k-1]¡¤F? + Q
 *
 * Update step (measurement update):
 *   K = P?¡¤H? / (H¡¤P?¡¤H? + R)
 *   x?[k] = x??[k] + K¡¤(z[k] - H¡¤x??[k])
 *   P[k] = (I - K¡¤H)¡¤P?[k]
 *
 * @param kf            Kalman filter state
 * @param measured_offset Measured clock offset (seconds)
 * @param current_time  Current time (seconds)
 * @return Filtered offset estimate (seconds)
 *
 * @complexity O(1)
 * @reference Kalman (1960)
 */
double av_kalman_clock_update(av_kalman_clock_t *kf,
                               double measured_offset, double current_time);

/**
 * @brief Predict clock state at a future time (without measurement).
 *
 * @param kf           Kalman filter state
 * @param future_time  Future time (seconds)
 * @param offset_out   Output: predicted offset at future_time
 * @param skew_out     Output: predicted skew
 */
void av_kalman_clock_predict(const av_kalman_clock_t *kf, double future_time,
                              double *offset_out, double *skew_out);

/* ================================================================
 * L5: Direct Skew Computation
 * ================================================================ */

/**
 * @brief Compute clock skew from two PTS pairs using direct comparison.
 *
 * Given two pairs (master1, slave1) and (master2, slave2):
 *   skew = (slave2 - slave1) / (master2 - master1) - 1.0
 *
 * This is the simplest skew estimator, sensitive to jitter.
 *
 * @param master1 First master PTS (90 kHz)
 * @param slave1  First slave PTS (90 kHz)
 * @param master2 Second master PTS (90 kHz)
 * @param slave2  Second slave PTS (90 kHz)
 * @param skew_ppm_out Output skew in ppm
 * @return 0 on success, -1 if division by near-zero
 *
 * @complexity O(1)
 */
int av_skew_direct_pair(int64_t master1, int64_t slave1,
                        int64_t master2, int64_t slave2,
                        double *skew_ppm_out);

/* ================================================================
 * L5: Robust Skew Estimation (Theil-Sen)
 * ================================================================ */

/** @struct av_theil_sen_t
 * @brief Theil-Sen robust estimator for clock skew.
 *
 * Unlike least-squares, Theil-Sen is robust to outliers.
 * It computes the median of all pairwise slopes, which gives
 * a breakdown point of ~29%.
 *
 * For N sample pairs, there are N*(N-1)/2 pairwise slopes.
 * The median of these slopes is the Theil-Sen estimator.
 *
 * @reference Theil, "A Rank-Invariant Method of Linear and Polynomial
 *            Regression Analysis" (1950)
 * @reference Sen, "Estimates of the Regression Coefficient Based on
 *            Kendall's Tau" (1968)
 */
typedef struct {
    double   *master_times;   /**< Ring buffer of master timestamps (seconds) */
    double   *slave_times;    /**< Ring buffer of slave timestamps (seconds) */
    double   *slopes;         /**< Workspace for pairwise slopes */
    int       buffer_size;    /**< Maximum samples to retain */
    int       count;          /**< Current number of samples */
    int       write_idx;      /**< Write index for ring buffer */
    double    slope_median;   /**< Current median slope estimate */
    double    intercept_median; /**< Current median intercept estimate */
} av_theil_sen_t;

int av_theil_sen_init(av_theil_sen_t *ts, int buffer_size);
void av_theil_sen_free(av_theil_sen_t *ts);

/**
 * @brief Add a (master, slave) time pair to the Theil-Sen estimator.
 *
 * @param ts          Theil-Sen state
 * @param master_time Master timestamp (seconds)
 * @param slave_time  Slave timestamp (seconds)
 *
 * @complexity O(N¡¤log N) per addition (sorting pairwise slopes)
 */
void av_theil_sen_add(av_theil_sen_t *ts, double master_time, double slave_time);

/**
 * @brief Get current Theil-Sen slope estimate.
 * @param ts Theil-Sen state
 * @return Estimated ¦Á (slope), or 1.0 if insufficient data
 */
double av_theil_sen_get_slope(const av_theil_sen_t *ts);

/**
 * @brief Get current Theil-Sen intercept estimate.
 * @param ts Theil-Sen state
 * @return Estimated ¦Â (intercept) in seconds
 */
double av_theil_sen_get_intercept(const av_theil_sen_t *ts);

#endif /* AV_SKEW_H */

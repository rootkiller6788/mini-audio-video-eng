/**
 * @file av_clock.h
 * @brief Clock Recovery and Synchronization Algorithms
 *
 * Covers L2 Concepts: PLL clock recovery, linear regression recovery,
 * PCR interpolation, EWMA filtering, LMS adaptive clock tracking.
 *
 * Covers L4 Fundamental Laws:
 *   - Linear clock model: T_slave = a * T_master + b (affine clock relation)
 *   - Allan variance for clock stability characterization
 *   - PLL transfer function: H(s) = (2¦Î¦Øn¡¤s + ¦Øn2)/(s2 + 2¦Î¦Øn¡¤s + ¦Øn2)
 *
 * Covers L5 Algorithms:
 *   - Second-order PLL clock recovery (PI loop filter)
 *   - Linear least-squares regression for robust clock fitting
 *   - EWMA jitter filtering
 *   - LMS adaptive filter for time-varying clock drift
 *   - PCR interpolation and extrapolation
 *
 * References:
 * - Gardner, "Phaselock Techniques" (2005), Ch. 2-4
 * - ISO/IEC 13818-1 ¡ì2.4.3.4 (PCR recovery)
 * - Proakis & Salehi, "Digital Communications" (2008) ¡ì6.4
 * - Oppenheim & Schafer, "Discrete-Time Signal Processing" (2010) Ch. 6
 *
 * @course MIT 6.003 ¡ì9 (Feedback & PLL), Berkeley EE123 ¡ì8 (Adaptive Filters),
 *         Stanford EE359 ¡ì8.4 (Synchronization)
 */

#ifndef AV_CLOCK_H
#define AV_CLOCK_H

#include <stdint.h>
#include <stddef.h>
#include "av_sync_core.h"

/* ================================================================
 * L4: Linear Clock Model Parameters
 * ================================================================ */

/** @struct av_clock_model_t
 * @brief Affine clock model: T_slave = scale * T_master + offset
 *
 * Two physical clocks do not run at exactly the same frequency.
 * The relationship between two clocks is modeled as:
 *
 *   C_slave(t) = ¦Á * C_master(t) + ¦Â
 *
 * where:
 *   ¦Á = f_slave / f_master  (clock rate ratio, ideally 1.0)
 *   ¦Â = initial phase offset (seconds)
 *
 * Clock skew = ¦Á - 1.0 (expressed in ppm when multiplied by 1e6)
 * Clock drift = rate of change of skew (ppm/second)
 *
 * @reference Mills, "Computer Network Time Synchronization" (2011) ¡ì3.2
 */
typedef struct {
    double scale;           /**< Clock rate ratio ¦Á = f_slave / f_master */
    double offset_seconds;  /**< Phase offset ¦Â in seconds */
    double skew_ppm;        /**< Clock skew in parts per million */
    double drift_ppm_per_s; /**< Clock drift rate (ppm per second) */
    double r_squared;       /**< Coefficient of determination (fit quality) */
    int    num_samples;     /**< Number of samples used in fit */
} av_clock_model_t;

/* ================================================================
 * L3: Mathematical Structures ¡ª PLL Parameters
 * ================================================================ */

/** @struct av_pll_params_t
 * @brief Second-order PLL (Type II) loop filter parameters.
 *
 * Transfer function of a second-order PLL:
 *
 *   H(s) = (2¡¤¦Î¡¤¦Øn¡¤s + ¦Øn2) / (s2 + 2¡¤¦Î¡¤¦Øn¡¤s + ¦Øn2)
 *
 * Discrete-time equivalent (bilinear transform):
 *
 *   LF(z) = Kp + Ki / (1 - z?1)
 *
 * where:
 *   Kp = 2¡¤¦Î¡¤¦Øn¡¤T   (proportional gain)
 *   Ki = ¦Øn2¡¤T2      (integral gain)
 *   T = update period
 *
 * Damping factor ¦Î: 0.707 (critically damped) to 1.0 (overdamped)
 * Natural frequency ¦Øn: determines loop bandwidth
 *
 * @reference Gardner, "Phaselock Techniques" (2005) Ch. 3
 */
typedef struct {
    double natural_freq_hz;     /**< ¦Øn ¡ª natural frequency (Hz) */
    double damping_factor;      /**< ¦Î ¡ª damping factor (0.5-2.0, typically 0.707) */
    double kp;                  /**< Proportional gain */
    double ki;                  /**< Integral gain */
    double update_period;       /**< Loop update period (seconds) */
    double phase_error;         /**< Current phase error estimate */
    double frequency_error;     /**< Current frequency error estimate */
    double integrator_state;    /**< Integrator accumulator */
} av_pll_params_t;

/* ================================================================
 * L3: EWMA Filter State
 * ================================================================ */

/** @struct av_ewma_filter_t
 * @brief Exponentially Weighted Moving Average filter state.
 *
 * EWMA: y[n] = ¦Á¡¤x[n] + (1-¦Á)¡¤y[n-1]
 *
 * This is equivalent to a first-order low-pass IIR filter.
 * Cutoff frequency: fc = ¦Á / (2¦Ð¡¤T¡¤(1-¦Á))  for small ¦Á
 * Time constant: ¦Ó = T / ¦Á
 *
 * Applications:
 *   - Jitter filtering on clock measurements
 *   - Smoothing PTS differences
 *   - Low-pass filtering for servo control
 *
 * @reference Hunter, "The Exponentially Weighted Moving Average" (1986)
 */
typedef struct {
    double alpha;           /**< Smoothing factor (0 < ¦Á ¡Ü 1) */
    double current_value;   /**< Current filtered value */
    double variance;        /**< Running variance estimate */
    int    initialized;     /**< Whether first sample has been processed */
} av_ewma_filter_t;

/* ================================================================
 * L3: Linear Regression State for Clock Recovery
 * ================================================================ */

/** @struct av_linreg_t
 * @brief Incremental linear regression for clock parameter estimation.
 *
 * Given pairs (t_master[i], t_slave[i]), estimate ¦Á and ¦Â in:
 *   t_slave = ¦Á * t_master + ¦Â
 *
 * Uses Welford's incremental algorithm for numerical stability.
 *
 * Normal equations (batch):
 *   ¦Á = (N¡¤¦²(x¡¤y) - ¦²x¡¤¦²y) / (N¡¤¦²(x2) - (¦²x)2)
 *   ¦Â = (¦²y - ¦Á¡¤¦²x) / N
 *
 * @reference Welford, "Note on a Method for Calculating Corrected Sums
 *            of Squares and Products" (1962)
 * @complexity O(1) per update
 */
typedef struct {
    double sum_x;           /**< ¦² t_master */
    double sum_y;           /**< ¦² t_slave */
    double sum_xx;          /**< ¦² (t_master)2 */
    double sum_xy;          /**< ¦² (t_master * t_slave) */
    double sum_yy;          /**< ¦² (t_slave)2 */
    double mean_x;          /**< Mean of x (incremental Welford) */
    double mean_y;          /**< Mean of y (incremental Welford) */
    double m2_x;            /**< Second moment of x */
    double m2_y;            /**< Second moment of y */
    double cov_xy;          /**< Covariance of x,y */
    uint64_t count;         /**< Number of samples N */
} av_linreg_t;

/* ================================================================
 * L5: PLL Clock Recovery API
 * ================================================================ */

int av_pll_init(av_pll_params_t *pll, double natural_freq_hz,
                double damping_factor, double update_period);

/**
 * @brief Update PLL with a new phase measurement.
 *
 * Given the measured clock phase error, compute the loop filter output
 * (frequency correction). Implements a Type II (PI) loop filter in
 * discrete time:
 *
 *   error[n] = measured_error
 *   integral[n] = integral[n-1] + Ki * error[n]
 *   output[n] = Kp * error[n] + integral[n]
 *
 * @param pll      PLL state
 * @param measured_error Phase error in seconds
 * @return Frequency correction (dimensionless, 1.0 = nominal)
 *
 * @complexity O(1)
 */
double av_pll_update(av_pll_params_t *pll, double measured_error);

/**
 * @brief Reset PLL integrator state.
 * @param pll PLL state
 */
void av_pll_reset(av_pll_params_t *pll);

/* ================================================================
 * L5: EWMA Filter API
 * ================================================================ */

int av_ewma_init(av_ewma_filter_t *ewma, double alpha);

/**
 * @brief Update EWMA filter with new sample and return filtered value.
 *
 * @param ewma   EWMA state
 * @param sample New input sample
 * @return Filtered output value
 *
 * @complexity O(1)
 * @reference Hunter (1986)
 */
double av_ewma_update(av_ewma_filter_t *ewma, double sample);

/**
 * @brief Reset EWMA filter state.
 * @param ewma EWMA state
 */
void av_ewma_reset(av_ewma_filter_t *ewma);

/**
 * @brief Compute effective time constant of the EWMA filter.
 * @param ewma        EWMA state
 * @param sample_period Period between samples (seconds)
 * @return Time constant in seconds (¦Ó = T/¦Á)
 */
double av_ewma_time_constant(const av_ewma_filter_t *ewma, double sample_period);

/* ================================================================
 * L5: Linear Regression Clock Recovery API
 * ================================================================ */

int av_linreg_init(av_linreg_t *lr);

/**
 * @brief Add a (master_time, slave_time) sample pair to the regression.
 *
 * @param lr         Linear regression state
 * @param master_time Master clock time (seconds)
 * @param slave_time  Slave clock time (seconds)
 *
 * @complexity O(1) using Welford's incremental algorithm
 */
void av_linreg_add_sample(av_linreg_t *lr, double master_time, double slave_time);

/**
 * @brief Fit the clock model from accumulated samples.
 *
 * Estimates ¦Á (scale) and ¦Â (offset) using least-squares.
 * Handles edge cases: N < 2, degenerate (zero variance) cases.
 *
 * @param lr    Regression state
 * @param model Output clock model (filled by this function)
 * @return 0 on success, -1 if insufficient data
 *
 * @complexity O(1)
 */
int av_linreg_fit(const av_linreg_t *lr, av_clock_model_t *model);

void av_linreg_reset(av_linreg_t *lr);

/* ================================================================
 * L5: Allan Variance ¡ª Clock Stability Characterization
 * ================================================================ */

/** @struct av_allan_var_t
 * @brief Allan variance estimator for clock stability analysis.
 *
 * Allan variance ¦Ò2(¦Ó) characterizes clock noise as a function
 * of observation interval ¦Ó.
 *
 * For N samples y[i] at interval ¦Ó?:
 *   ¦Ò2(¦Ó = m¡¤¦Ó?) = 1/(2¡¤(N-2m)) ¡¤ ¦² (y[i+2m] - 2¡¤y[i+m] + y[i])2 / (m¡¤¦Ó?)2
 *
 * White FM noise: ¦Ò2(¦Ó) ¡Ø 1/¦Ó    (¦Ò ¡Ø 1/¡Ì¦Ó)
 * Flicker FM:     ¦Ò2(¦Ó) ¡Ø const  (¦Ò ¡Ø constant)
 * Random walk FM: ¦Ò2(¦Ó) ¡Ø ¦Ó      (¦Ò ¡Ø ¡Ì¦Ó)
 *
 * @reference Allan, "Statistics of Atomic Frequency Standards" (1966)
 * @reference IEEE Std 1139-2008
 */
typedef struct {
    double  *phase_data;        /**< Phase measurements (seconds), ring buffer */
    double   tau0;              /**< Minimum observation interval (seconds) */
    int      buffer_size;       /**< Maximum number of phase samples */
    int      write_index;       /**< Current write position */
    int      num_valid;         /**< Number of valid samples in buffer */
} av_allan_var_t;

int av_allan_var_init(av_allan_var_t *av, double tau0, int buffer_size);

void av_allan_var_free(av_allan_var_t *av);

/**
 * @brief Add a phase measurement to the Allan variance estimator.
 * @param av             Allan variance state
 * @param phase_seconds  Phase measurement (seconds)
 */
void av_allan_var_add(av_allan_var_t *av, double phase_seconds);

/**
 * @brief Compute Allan deviation ¦Ò(¦Ó) for a given observation interval.
 * @param av Allan variance state
 * @param m  Observation interval as multiple of ¦Ó? (m ¡Ý 1)
 * @return ¦Ò(¦Ó) in seconds, or -1.0 on error/insufficient data
 */
double av_allan_var_compute(const av_allan_var_t *av, int m);

/* ================================================================
 * L5: PCR Recovery
 * ================================================================ */

/**
 * @brief Recover STC from a PCR value using local arrival time.
 *
 * Implements the standard MPEG-2 PCR recovery algorithm:
 *   1. Compute PCR time in seconds
 *   2. Compute local arrival time
 *   3. Estimate clock skew via linear regression over N PCR samples
 *   4. Interpolate/extrapolate STC for any given local time
 *
 * @param pcr      PCR value
 * @param lr       Linear regression state (accumulates PCR samples)
 * @param model    Output: recovered clock model
 * @return 0 on success, -1 on discontinuity
 *
 * @reference ISO/IEC 13818-1 ¡ì2.4.3.4, Annex D
 */
int av_pcr_recover(const av_pcr_t *pcr, av_linreg_t *lr, av_clock_model_t *model);

/**
 * @brief Compute STC value for a given local time using recovered clock model.
 *
 * STC(local_time) = model.scale * local_time + model.offset_seconds
 *
 * @param model      Clock model from PCR recovery
 * @param local_time Local time in seconds
 * @return Estimated STC in seconds
 */
double av_stc_interpolate(const av_clock_model_t *model, double local_time);

/* ================================================================
 * L5: LMS Adaptive Clock Tracking
 * ================================================================ */

/** @struct av_lms_clock_t
 * @brief LMS (Least Mean Squares) adaptive filter for time-varying clock drift.
 *
 * Unlike batch linear regression, LMS tracks slowly varying clock parameters
 * in real time. This is useful when clock skew changes due to temperature
 * or aging effects.
 *
 * LMS update:
 *   w[n+1] = w[n] + ¦Ì * e[n] * x[n]
 *
 * where:
 *   w = [offset, scale] (2-tap adaptive filter)
 *   x = [1, t_master]  (input vector)
 *   e[n] = t_slave[n] - w[n]?¡¤x[n]  (prediction error)
 *   ¦Ì = step size parameter
 *
 * @reference Haykin, "Adaptive Filter Theory" (2002) Ch. 6
 * @reference Widrow & Hoff, "Adaptive Switching Circuits" (1960)
 */
typedef struct {
    double offset;              /**< Estimated clock offset (¦Â) */
    double scale;               /**< Estimated clock scale (¦Á) */
    double mu;                  /**< Step size parameter (0 < ¦Ì < 2/¦Ëmax) */
    double last_master_time;    /**< Last master time for update */
    double last_slave_time;     /**< Last slave time for update */
    double error;               /**< Last prediction error */
    int    initialized;         /**< Whether initial values are set */
} av_lms_clock_t;

int av_lms_clock_init(av_lms_clock_t *lms, double mu, double init_scale);

/**
 * @brief Update LMS clock tracker with a new observation pair.
 *
 * @param lms         LMS state
 * @param master_time Master clock timestamp (seconds)
 * @param slave_time  Slave clock timestamp (seconds)
 * @return Prediction error (slave_time - predicted_slave_time) in seconds
 *
 * @complexity O(1)
 */
double av_lms_clock_update(av_lms_clock_t *lms, double master_time,
                            double slave_time);

/**
 * @brief Predict slave time for a given master time.
 * @param lms         LMS state
 * @param master_time Master clock time (seconds)
 * @return Predicted slave time (seconds)
 */
double av_lms_clock_predict(const av_lms_clock_t *lms, double master_time);

#endif /* AV_CLOCK_H */

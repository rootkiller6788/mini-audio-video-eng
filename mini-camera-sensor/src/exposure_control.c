/**
 * @file exposure_control.c
 * @brief Auto Exposure (AE) and Auto Gain Control (AGC) algorithms
 *
 * Implements:
 *   L5: AE metering (average, center-weighted, spot, matrix, percentile),
 *       PI-based AE control, flicker detection and avoidance
 *   L6: scene change detection, HDR bracket computation, exposure value
 *
 * Reference: Kuno et al. IEEE T-CE 1998; Poynton (2012) Ch.4
 */
#include "exposure_control.h"

/*===========================================================================
 * L5: AE statistics computation
 *===========================================================================*/

void ae_compute_statistics(const raw_frame_t *f, const ae_config_t *cfg,
                            ae_statistics_t *s)
{
    if (f == NULL || s == NULL) return;
    memset(s, 0, sizeof(*s));

    /* Compute luminance proxy from Bayer: average of all pixels */
    double sum = 0.0;
    uint32_t count = 0;
    uint32_t x, y;
    uint32_t histogram[256] = {0};
    uint32_t bin_scale = (f->max_value > 0) ? f->max_value / 256 + 1 : 1;

    for (y = 0; y < f->height; y++) {
        for (x = 0; x < f->width; x++) {
            uint32_t v = f->data[y * f->stride + x];
            sum += v;
            count++;

            if (v >= f->max_value) s->saturated_pixels++;
            if (v <= f->black_level) s->dark_pixels++;

            uint32_t bin = v / bin_scale;
            if (bin >= 256) bin = 255;
            histogram[bin]++;
        }
    }

    if (count > 0) {
        s->current_mean = sum / count;
    }

    /* Median: find 50th percentile from histogram */
    uint32_t cum = 0;
    uint32_t half = count / 2;
    for (x = 0; x < 256; x++) {
        cum += histogram[x];
        if (cum >= half) {
            s->current_median = (double)(x * bin_scale + bin_scale/2);
            break;
        }
    }

    /* 5th percentile */
    cum = 0;
    uint32_t p5 = count * 5 / 100;
    for (x = 0; x < 256; x++) {
        cum += histogram[x];
        if (cum >= p5) {
            s->pct_05 = (double)(x * bin_scale + bin_scale/2);
            break;
        }
    }

    /* 50th percentile */
    s->pct_50 = s->current_median;

    /* 95th percentile */
    cum = 0;
    uint32_t p95 = count * 95 / 100;
    for (x = 0; x < 256; x++) {
        cum += histogram[x];
        if (cum >= p95) {
            s->pct_95 = (double)(x * bin_scale + bin_scale/2);
            break;
        }
    }

    /* Scene luminance relative to target */
    if (cfg != NULL) {
        s->target_mean = cfg->target_luminance * f->max_value;
        if (s->target_mean > 0.0) {
            s->error_pct = (s->current_mean - s->target_mean) /
                           s->target_mean * 100.0;
        }
        s->scene_luminance = s->current_mean / f->max_value;
    }
}

/*===========================================================================
 * L5: Metering algorithms
 *===========================================================================*/

double ae_meter_average(const raw_frame_t *f)
{
    if (f == NULL || f->data == NULL) return 0.0;
    double sum = 0.0;
    uint32_t i, n = f->width * f->height;
    for (i = 0; i < n; i++) sum += f->data[i];
    return sum / n;
}

double ae_meter_center_weighted(const raw_frame_t *f)
{
    if (f == NULL) return 0.0;

    double cx = f->width / 2.0, cy = f->height / 2.0;
    double sigma = 0.3 * (f->width < f->height ? f->width : f->height);

    double sum = 0.0, weight_sum = 0.0;
    uint32_t x, y;
    for (y = 0; y < f->height; y++) {
        for (x = 0; x < f->width; x++) {
            double dx = (double)x - cx;
            double dy = (double)y - cy;
            double r2 = dx*dx + dy*dy;
            double w = exp(-r2 / (2.0 * sigma * sigma));
            sum += (double)f->data[y*f->stride+x] * w;
            weight_sum += w;
        }
    }
    return (weight_sum > 0.0) ? sum / weight_sum : 0.0;
}

double ae_meter_spot(const raw_frame_t *f, double spot_ratio)
{
    if (f == NULL || spot_ratio <= 0.0) return 0.0;
    if (spot_ratio > 1.0) spot_ratio = 1.0;

    uint32_t sw = (uint32_t)(f->width * spot_ratio);
    uint32_t sh = (uint32_t)(f->height * spot_ratio);
    if (sw < 2) sw = 2;
    if (sh < 2) sh = 2;

    uint32_t sx = (f->width - sw) / 2;
    uint32_t sy = (f->height - sh) / 2;

    double sum = 0.0;
    uint32_t count = 0;
    uint32_t x, y;
    for (y = sy; y < sy + sh; y++) {
        for (x = sx; x < sx + sw; x++) {
            sum += f->data[y*f->stride+x];
            count++;
        }
    }
    return (count > 0) ? sum / count : 0.0;
}

double ae_meter_matrix(const raw_frame_t *f, const ae_metering_zone_t *zones,
                        uint32_t n_zones)
{
    if (f == NULL || zones == NULL || n_zones == 0) return 0.0;

    double total_luma = 0.0, total_weight = 0.0;
    uint32_t z;
    for (z = 0; z < n_zones; z++) {
        double sum = 0.0;
        uint32_t count = 0;
        uint32_t x, y;
        for (y = zones[z].y; y < zones[z].y + zones[z].h && y < f->height; y++) {
            for (x = zones[z].x; x < zones[z].x + zones[z].w && x < f->width; x++) {
                sum += f->data[y*f->stride+x];
                count++;
            }
        }
        if (count > 0) {
            total_luma += (sum / count) * zones[z].weight;
            total_weight += zones[z].weight;
        }
    }
    return (total_weight > 0.0) ? total_luma / total_weight : 0.0;
}

double ae_meter_percentile(const raw_frame_t *f, double pct)
{
    if (f == NULL) return 0.0;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;

    /* Build 256-bin histogram */
    uint32_t hist[256] = {0};
    uint32_t max_val = f->max_value > 0 ? f->max_value : 4095;
    uint32_t i, n = f->width * f->height;

    for (i = 0; i < n; i++) {
        uint32_t bin = (uint32_t)f->data[i] * 256 / (max_val + 1);
        if (bin >= 256) bin = 255;
        hist[bin]++;
    }

    uint32_t target = (uint32_t)((double)n * pct / 100.0);
    uint32_t cum = 0;
    uint32_t b;
    for (b = 0; b < 256; b++) {
        cum += hist[b];
        if (cum >= target) {
            return (double)(b * (max_val + 1)) / 256.0;
        }
    }
    return (double)max_val;
}

/*===========================================================================
 * L5: PI-based AE control
 *===========================================================================*/

void ae_config_init_default(ae_config_t *cfg)
{
    if (cfg == NULL) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->metering = AE_METERING_CENTER_WEIGHTED;
    cfg->speed = AE_SPEED_NORMAL;
    cfg->flicker = FLICKER_OFF;
    cfg->target_luminance = 0.18;        /* 18% gray */
    cfg->exposure_compensation = 0.0;
    cfg->max_saturated_pct = 5;
    cfg->min_exposure_us = 10.0;
    cfg->max_exposure_us = 33000.0;
    cfg->min_gain = 1.0;
    cfg->max_gain = 16.0;
    cfg->use_gain_first = 0;             /* Adjust exposure before gain */
    cfg->anti_flicker_enabled = 0;
    cfg->ae_kp = 0.3;                    /* Proportional gain */
    cfg->ae_ki = 0.05;                   /* Integral gain */
}

void ae_state_init(ae_state_data_t *s, double init_exp, double init_gain)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->state = AE_STATE_SEARCHING;
    s->current_exposure_us = init_exp;
    s->current_gain = init_gain;
    s->error_integral = 0.0;
}

/**
 * PI control: computes exposure adjustment from luminance error.
 *
 * error = (target_luminance - current_luminance) / target_luminance
 *
 * If underexposed (error > 0):
 *   Increase exposure time first, then gain
 * If overexposed (error < 0):
 *   Decrease gain first, then exposure time
 *
 * PI controller: adjustment = Kp * error + Ki * integral(error)
 *
 * Reference: Kuno et al., IEEE T-CE 1998
 */
int ae_pi_control(const ae_statistics_t *s, ae_config_t *cfg,
                   ae_state_data_t *state, double *new_exp, double *new_gain)
{
    if (s == NULL || cfg == NULL || state == NULL ||
        new_exp == NULL || new_gain == NULL) return 0;

    double target = cfg->target_luminance;
    double current = s->current_mean / 4095.0; /* Assume 12-bit max */
    if (target <= 0.0) target = 0.18;

    double error = (target - current) / target;

    /* Integral term with anti-windup */
    state->error_integral += error;
    double max_integral = 5.0 / cfg->ae_ki;
    if (state->error_integral > max_integral) state->error_integral = max_integral;
    if (state->error_integral < -max_integral) state->error_integral = -max_integral;

    double adjustment = cfg->ae_kp * error + cfg->ae_ki * state->error_integral;

    double exp = state->current_exposure_us;
    double gain = state->current_gain;

    /* Apply adjustment */
    if (fabs(adjustment) < 0.02) {
        /* Within tolerance */
        state->stable_count++;
        if (state->stable_count > 3) {
            state->state = AE_STATE_LOCKED;
        }
        *new_exp = exp;
        *new_gain = gain;
        return 0;
    }

    state->stable_count = 0;
    state->state = AE_STATE_CONVERGING;

    if (error > 0) {
        /* Too dark: increase exposure or gain */
        if (!cfg->use_gain_first && exp < cfg->max_exposure_us) {
            exp *= (1.0 + adjustment);
            if (exp > cfg->max_exposure_us) exp = cfg->max_exposure_us;
        } else if (gain < cfg->max_gain) {
            gain *= (1.0 + adjustment);
            if (gain > cfg->max_gain) gain = cfg->max_gain;
        } else if (exp < cfg->max_exposure_us) {
            exp *= (1.0 + adjustment);
            if (exp > cfg->max_exposure_us) exp = cfg->max_exposure_us;
        }
    } else {
        /* Too bright: decrease gain or exposure */
        if (gain > cfg->min_gain) {
            gain /= (1.0 - adjustment); /* adjustment is negative */
            if (gain < cfg->min_gain) gain = cfg->min_gain;
        } else if (exp > cfg->min_exposure_us) {
            exp /= (1.0 - adjustment);
            if (exp < cfg->min_exposure_us) exp = cfg->min_exposure_us;
        }
    }

    /* Clamp */
    if (exp < cfg->min_exposure_us) exp = cfg->min_exposure_us;
    if (exp > cfg->max_exposure_us) exp = cfg->max_exposure_us;
    if (gain < cfg->min_gain) gain = cfg->min_gain;
    if (gain > cfg->max_gain) gain = cfg->max_gain;

    /* Update state */
    state->current_exposure_us = exp;
    state->current_gain = gain;
    state->prev_luminance = current;
    state->frame_count++;

    *new_exp = exp;
    *new_gain = gain;

    return 1; /* Changed */
}

/**
 * Exposure search using binary search.
 *
 * Starts from extremes and narrows down to find acceptable exposure.
 */
void ae_search_exposure(ae_state_data_t *s, ae_config_t *cfg,
                         double *exp, double *gain, uint8_t *done)
{
    if (s == NULL || cfg == NULL || exp == NULL || gain == NULL || done == NULL)
        return;

    /* Simple coarse search: try mid range */
    if (s->frame_count == 0) {
        *exp = (cfg->min_exposure_us + cfg->max_exposure_us) / 2.0;
        *gain = 1.0;
        *done = 0;
        return;
    }

    /* Binary search based on luminance error */
    double current = s->stats.current_mean / 4095.0;
    double target = cfg->target_luminance;
    double error = (target - current) / target;

    if (fabs(error) < 0.1) {
        *done = 1;
        *exp = s->current_exposure_us;
        *gain = s->current_gain;
        return;
    }

    double step = 0.5;
    if (error > 0) {
        *exp = s->current_exposure_us * (1.0 + step);
    } else {
        *exp = s->current_exposure_us * (1.0 - step);
    }

    if (*exp < cfg->min_exposure_us) *exp = cfg->min_exposure_us;
    if (*exp > cfg->max_exposure_us) *exp = cfg->max_exposure_us;
    *gain = 1.0;
    *done = 0;
}

int ae_detect_scene_change(const ae_statistics_t *prev,
                            const ae_statistics_t *curr, double threshold)
{
    if (prev == NULL || curr == NULL) return 0;

    double delta = fabs(curr->current_mean - prev->current_mean);
    double ref = (prev->current_mean + curr->current_mean) / 2.0;

    if (ref > 0.0 && delta / ref > threshold) return 1;
    return 0;
}

/*===========================================================================
 * L6: Flicker detection and avoidance
 *===========================================================================*/

flicker_mode_t flicker_detect(const double *means, uint32_t n,
                               double interval_s)
{
    if (means == NULL || n < 10) return FLICKER_OFF;

    /* Compute frame-to-frame differences */
    double max_var_50hz = 0.0; (void)max_var_50hz;
    /* Look for periodic patterns at 100 Hz and 120 Hz */
    double period_50hz = 0.01;  /* 10 ms */
    double period_60hz = 0.008333; /* 8.333 ms */

    /* Simple detection: if interval is close to multiple of flicker period */
    double mod_50 = fmod(interval_s, period_50hz);
    double mod_60 = fmod(interval_s, period_60hz);

    /* Check energy at flicker frequencies by analyzing variations */
    double var_total = 0.0;
    uint32_t i;
    for (i = 1; i < n; i++) {
        double diff = means[i] - means[i-1];
        var_total += diff * diff;
    }
    var_total /= (n - 1);

    if (var_total > 0.01) { /* Significant flicker present */
        if (mod_50 < mod_60) {
            return FLICKER_50HZ;
        } else {
            return FLICKER_60HZ;
        }
    }

    return FLICKER_OFF;
}

double flicker_safe_exposure(double desired_us, flicker_mode_t f,
                              uint32_t *multiples)
{
    if (multiples == NULL) return desired_us;

    double period_us = 0.0;
    if (f == FLICKER_50HZ) {
        period_us = 10000.0; /* 10 ms = 100 Hz */
    } else if (f == FLICKER_60HZ) {
        period_us = 8333.33; /* 8.333 ms = 120 Hz */
    } else {
        *multiples = 1;
        return desired_us;
    }

    /* Find nearest integer multiple of flicker period */
    *multiples = (uint32_t)(desired_us / period_us + 0.5);
    if (*multiples < 1) *multiples = 1;

    return (double)(*multiples) * period_us;
}

/*===========================================================================
 * L6: HDR bracket computation
 *===========================================================================*/

void ae_hdr_bracket(double base_us, double ratio, uint32_t n, double *bracket_exp)
{
    if (bracket_exp == NULL || n == 0) return;

    uint32_t i;
    for (i = 0; i < n; i++) {
        /* Shorter exposures for highlights, longer for shadows */
        if (n == 3) {
            switch (i) {
                case 0: bracket_exp[i] = base_us / ratio; break;       /* Short */
                case 1: bracket_exp[i] = base_us; break;               /* Mid */
                case 2: bracket_exp[i] = base_us * ratio; break;       /* Long */
            }
        } else if (n == 2) {
            bracket_exp[i] = (i == 0) ? base_us / ratio : base_us;
        } else {
            /* General: logarithmic spacing */
            double log_base = log(base_us / ratio);
            double log_top = log(base_us * ratio);
            double step = (n > 1) ? (log_top - log_base) / (double)(n - 1) : 0.0;
            bracket_exp[i] = exp(log_base + step * (double)i);
        }
    }
}

int ae_determine_hdr_bracket(const raw_frame_t *f, double *exp,
                              uint32_t *n, double *ratio)
{
    if (f == NULL || exp == NULL || n == NULL || ratio == NULL) return -1;

    /* Analyze scene DR from histogram */
    ae_statistics_t stats;
    ae_config_t cfg;
    ae_config_init_default(&cfg);
    ae_compute_statistics(f, &cfg, &stats);

    double dr = stats.pct_95 / (stats.pct_05 + 1.0);
    double dr_db = 20.0 * log10(dr);

    if (dr_db < 65.0) {
        *n = 1; /* Single exposure sufficient */
        exp[0] = 15000.0;
        *ratio = 1.0;
    } else if (dr_db < 80.0) {
        *n = 2;
        *ratio = 4.0;
        exp[0] = 15000.0 / (*ratio);
        exp[1] = 15000.0;
    } else {
        *n = 3;
        *ratio = 4.0;
        ae_hdr_bracket(15000.0, *ratio, *n, exp);
    }

    return 0;
}

/*===========================================================================
 * L6: Utilities
 *===========================================================================*/

double ae_exposure_value(double exp_us, double gain_lin)
{
    if (exp_us <= 0.0) return 0.0;
    /* EV at ISO 100, f/1.0: EV = log2(1 / (t * gain)) */
    double t_sec = exp_us / 1.0e6;
    return log2(1.0 / (t_sec * gain_lin));
}

void ae_statistics_print(const ae_statistics_t *s)
{
    if (s == NULL) { printf("AE stats: NULL\n"); return; }
    printf("=== AE Statistics ===\n");
    printf("Mean:      %.1f DN\n", s->current_mean);
    printf("Median:    %.1f DN\n", s->current_median);
    printf("Target:    %.1f DN\n", s->target_mean);
    printf("Error:     %.1f%%\n", s->error_pct);
    printf("Saturated: %u px\n", (unsigned)s->saturated_pixels);
    printf("Dark:      %u px\n", (unsigned)s->dark_pixels);
    printf("P5:  %.1f  P50: %.1f  P95: %.1f\n",
           s->pct_05, s->pct_50, s->pct_95);
    printf("Luminance: %.3f\n", s->scene_luminance);
}

int ae_is_acceptable(const ae_statistics_t *s, const ae_config_t *cfg)
{
    if (s == NULL || cfg == NULL) return 0;

    /* Check luminance error within tolerance */
    if (fabs(s->error_pct) > 20.0) return 0;  /* >20% error */

    /* Check not too many saturated pixels */
    uint32_t total_pixels = 4000 * 3000; /* Approximate */
    if (total_pixels > 0) {
        uint32_t sat_pct = s->saturated_pixels * 100 / total_pixels;
        if (sat_pct > (uint32_t)cfg->max_saturated_pct) return 0;
    }

    return 1;
}

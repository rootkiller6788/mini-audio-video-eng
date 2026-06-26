/**
 * @file sensor_calibration.c
 * @brief Sensor calibration: defect maps, flat-field, lens shading, PTC, color
 *
 * Implements L5 calibration algorithms:
 *   - Black level calibration from dark frame
 *   - Defect map generation (hot/dead pixel detection)
 *   - Lens shading calibration (radial polynomial fit)
 *   - PTC characterization (read noise, CG, PRNU, FWC, DR)
 *   - Color calibration (CCM + WB via LS)
 *
 * L7: Full factory calibration pipeline
 *
 * Reference: EMVA 1288; Janesick "Photon Transfer" (2007);
 *            ISO 14524 (OECF); ISO 15739 (noise)
 */
#include "sensor_calibration.h"
#include "noise_model.h"
#include "color_science.h"

/*===========================================================================
 * L1+L5: Calibration initialization
 *===========================================================================*/

void calibration_init(sensor_calibration_t *cal)
{
    if (cal == NULL) return;
    memset(cal, 0, sizeof(*cal));

    /* Default black level */
    cal->black_level.bl_r  = 64;
    cal->black_level.bl_gr = 64;
    cal->black_level.bl_gb = 64;
    cal->black_level.bl_b  = 64;

    /* Default identity color calibration */
    cal->color.ccm[0] = 1.0; cal->color.ccm[1] = 0.0; cal->color.ccm[2] = 0.0;
    cal->color.ccm[3] = 0.0; cal->color.ccm[4] = 1.0; cal->color.ccm[5] = 0.0;
    cal->color.ccm[6] = 0.0; cal->color.ccm[7] = 0.0; cal->color.ccm[8] = 1.0;
    cal->color.wb_r = 1.0;
    cal->color.wb_g = 1.0;
    cal->color.wb_b = 1.0;

    defect_map_init(&cal->defect_map);
    cal->temperature_c = 25.0;
}

void calibration_free(sensor_calibration_t *cal)
{
    if (cal == NULL) return;
    defect_map_free(&cal->defect_map);
}

/*===========================================================================
 * L5: Black level calibration
 *
 * Measures the average of optically black pixels (OB pixels are shielded
 * from light; any signal is dark current + ADC offset).
 *===========================================================================*/

void calibrate_black_level(const raw_frame_t *dark, black_level_calib_t *bl)
{
    if (dark == NULL || bl == NULL) return;

    double sum_r = 0, sum_gr = 0, sum_gb = 0, sum_b = 0;
    uint32_t cnt_r = 0, cnt_gr = 0, cnt_gb = 0, cnt_b = 0;

    uint32_t x, y;
    for (y = 0; y < dark->height; y++) {
        for (x = 0; x < dark->width; x++) {
            bayer_color_t c = bayer_color_at(x, y, dark->cfa);
            pixel_raw_t v = dark->data[y * dark->stride + x];

            switch (c) {
                case BAYER_COLOR_R:  sum_r  += v; cnt_r++;  break;
                case BAYER_COLOR_GR: sum_gr += v; cnt_gr++; break;
                case BAYER_COLOR_GB: sum_gb += v; cnt_gb++; break;
                case BAYER_COLOR_B:  sum_b  += v; cnt_b++;  break;
            }
        }
    }

    bl->bl_r  = (cnt_r  > 0) ? (uint16_t)(sum_r  / cnt_r  + 0.5) : 0;
    bl->bl_gr = (cnt_gr > 0) ? (uint16_t)(sum_gr / cnt_gr + 0.5) : 0;
    bl->bl_gb = (cnt_gb > 0) ? (uint16_t)(sum_gb / cnt_gb + 0.5) : 0;
    bl->bl_b  = (cnt_b  > 0) ? (uint16_t)(sum_b  / cnt_b  + 0.5) : 0;
}

/*===========================================================================
 * L5: Defect map generation
 *
 * Combines multiple test conditions:
 *   1. Short dark frame → hot pixels (high dark current detectable quickly)
 *   2. Long dark frame → mild hot pixels (lower dark rate, accumulated)
 *   3. Flat-field frame → dead pixels (low response to uniform illumination)
 *===========================================================================*/

int calibrate_defect_map(const raw_frame_t *dark_short,
                          const raw_frame_t *dark_long,
                          const raw_frame_t *flat,
                          defect_map_t *map)
{
    if (map == NULL) return -1;

    uint32_t total = 0;

    /* Detect hot pixels in short dark frame (very hot pixels) */
    if (dark_short != NULL) {
        total += detect_hot_pixels(dark_short, 8.0, map);
    }

    /* Detect warmer pixels in long dark frame (mild hot pixels) */
    if (dark_long != NULL) {
        total += detect_hot_pixels(dark_long, 5.0, map);
    }

    /* Detect dead pixels in flat field (very low response) */
    if (flat != NULL) {
        total += detect_dead_pixels(flat, 6.0, map);
    }

    return (int)total;
}

/*===========================================================================
 * L5: Lens shading calibration
 *
 * Fits radial polynomial: gain(r) = 1 + a2*r^2 + a4*r^4 + a6*r^6
 *
 * Method:
 *   1. Capture flat-field (uniform illumination) raw frame
 *   2. Compute average luminance per radial annulus
 *   3. Fit polynomial to radial falloff data
 *
 * This models cos^4(theta) optical vignetting + microlens shading.
 *===========================================================================*/

int calibrate_lens_shading(const raw_frame_t *flat,
                            lens_shading_coeffs_t *ls)
{
    if (flat == NULL || ls == NULL) return -1;

    /* Compute radial profile */
    double cx = flat->width / 2.0;
    double cy = flat->height / 2.0;
    double max_r = sqrt(cx*cx + cy*cy);

    /* Radial bins */
    uint32_t n_bins = 20;
    double bin_sum[20] = {0};
    uint32_t bin_cnt[20] = {0};
    double bin_r[20] = {0}; (void)bin_r;

    uint32_t x, y, b;
    for (y = 0; y < flat->height; y++) {
        for (x = 0; x < flat->width; x++) {
            double dx = (double)x - cx;
            double dy = (double)y - cy;
            double r = sqrt(dx*dx + dy*dy) / max_r;

            b = (uint32_t)(r * (double)(n_bins - 1));
            if (b >= n_bins) b = n_bins - 1;

            bin_sum[b] += flat->data[y * flat->stride + x];
            bin_cnt[b]++;
            bin_r[b] = r;
        }
    }

    /* Compute per-bin average and fit polynomial */
    double y_vals[20], x_vals[20];
    uint32_t valid = 0;
    for (b = 0; b < n_bins; b++) {
        if (bin_cnt[b] > 0) {
            y_vals[valid] = bin_sum[b] / bin_cnt[b];
            x_vals[valid] = (double)b / (double)(n_bins - 1);
            valid++;
        }
    }

    if (valid < 4) return -1;

    /* Normalize by center brightness */
    double center = y_vals[0];
    if (center <= 0.0) { ls->a2=0; ls->a4=0; ls->a6=0; ls->cx=cx; ls->cy=cy; return 0; }

    /* Convert to gain: gain(r) = center / brightness(r) */
    double gain[20];
    for (b = 0; b < valid; b++) {
        gain[b] = center / y_vals[b] - 1.0; /* gain(r) - 1 = a2*r^2 + ... */
    }

    /* Fit polynomial a2*r^2 + a4*r^4 + a6*r^6 to gain-1 data
     * Simplified: fit a2 only for now (quadratic approximation) */
    double sum_r2 = 0, sum_g = 0, sum_r2g = 0, sum_r4 = 0;
    for (b = 0; b < valid; b++) {
        double r2 = x_vals[b] * x_vals[b];
        sum_r2  += r2;
        sum_r4  += r2 * r2;
        sum_g   += gain[b];
        sum_r2g += r2 * gain[b];
    }

    double denom = valid * sum_r4 - sum_r2 * sum_r2;
    if (fabs(denom) > 1e-10) {
        ls->a2 = (valid * sum_r2g - sum_r2 * sum_g) / denom;
    } else {
        ls->a2 = 0.0;
    }
    /* Higher order coefficients from a2 estimate */
    ls->a4 = ls->a2 * 0.1;
    ls->a6 = ls->a2 * 0.01;
    ls->cx = cx;
    ls->cy = cy;

    return 0;
}

int calibrate_generate_gain_map(const lens_shading_coeffs_t *ls,
                                 uint32_t w, uint32_t h,
                                 cfa_pattern_t cfa, gain_map_t *map)
{
    if (ls == NULL || map == NULL || w == 0 || h == 0) return -1;
    (void)cfa;

    if (map->width != w || map->height != h || map->data == NULL) return -1;

    double max_r = sqrt(ls->cx*ls->cx + ls->cy*ls->cy);
    if (max_r < 1.0) max_r = 1.0;

    uint32_t x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            double dx = ((double)x - ls->cx) / max_r;
            double dy = ((double)y - ls->cy) / max_r;
            double r2 = dx*dx + dy*dy;
            double r4 = r2 * r2;
            double r6 = r4 * r2;
            map->data[y * w + x] = 1.0 + ls->a2 * r2 + ls->a4 * r4 + ls->a6 * r6;
        }
    }

    return 0;
}

/*===========================================================================
 * L5: PTC characterization
 *
 * For each exposure level:
 *   - Compute per-pixel temporal mean and variance
 *   - Fit: variance = read_noise^2 + mean/CG + (PRNU*mean)^2
 *===========================================================================*/

int calibrate_ptc(const uint16_t **frames, uint32_t n_levels,
                   uint32_t n_frames_per_level, uint32_t n_pixels,
                   ptc_calibration_t *ptc)
{
    if (frames == NULL || ptc == NULL || n_levels < 3) return -1;
    memset(ptc, 0, sizeof(*ptc));

    double *means = (double *)malloc(n_levels * sizeof(double));
    double *vars  = (double *)malloc(n_levels * sizeof(double));
    if (means == NULL || vars == NULL) {
        free(means); free(vars); return -1;
    }

    uint32_t level;
    for (level = 0; level < n_levels; level++) {
        /* Compute per-pixel mean and temporal variance */
        double sum_mean = 0.0, sum_var = 0.0;

        uint32_t p;
        for (p = 0; p < n_pixels; p++) {
            double px_mean = 0.0;
            uint32_t f;
            for (f = 0; f < n_frames_per_level; f++) {
                px_mean += frames[level * n_frames_per_level + f][p];
            }
            px_mean /= n_frames_per_level;

            double px_var = 0.0;
            for (f = 0; f < n_frames_per_level; f++) {
                double diff = frames[level * n_frames_per_level + f][p] - px_mean;
                px_var += diff * diff;
            }
            px_var /= n_frames_per_level;

            sum_mean += px_mean;
            sum_var  += px_var;
        }

        means[level] = sum_mean / n_pixels;
        vars[level]  = sum_var  / n_pixels;
    }

    /* Fit PTC model */
    double read_noise, cg, prnu;
    if (noise_ptc_estimate(means, vars, n_levels, &read_noise, &cg, &prnu) == 0) {
        ptc->read_noise_e = read_noise;
        ptc->conversion_gain_dn_e = cg;
        ptc->prnu_coeff = prnu;
    }

    /* Estimate FWC from saturation point */
    /* Find level where variance decreases (saturation) */
    double fwc_est = 0.0;
    double prev_var = 0.0;
    for (level = 0; level < n_levels; level++) {
        if (level > 0 && vars[level] < prev_var * 0.5) {
            /* Variance drop indicates saturation */
            fwc_est = means[level];
            break;
        }
        prev_var = vars[level];
    }

    if (fwc_est <= 0.0) {
        /* No saturation found; use max signal */
        fwc_est = means[n_levels - 1];
    }
    ptc->fwc_e = fwc_est;

    /* Dynamic range */
    double noise_floor = sqrt(read_noise * read_noise + (cg > 0 ? cg/12.0 : 0));
    if (noise_floor > 0.0) {
        ptc->dr_db = 20.0 * log10(fwc_est / noise_floor);
    }

    /* SNR max */
    ptc->snr_max_db = 20.0 * log10(sqrt(fwc_est));

    free(means); free(vars);
    return 0;
}

/*===========================================================================
 * L5+L7: Color calibration
 *
 * CCM + WB calibration from ColorChecker measurements.
 *===========================================================================*/

int calibrate_color(const double *sensor_rgb, const double *target_rgb,
                     uint32_t n_patches, color_calibration_t *cc)
{
    if (sensor_rgb == NULL || target_rgb == NULL || cc == NULL ||
        n_patches < 4) return -1;

    /* 1. White balance from neutral patches (white, gray series) */
    /* Use brightest neutral patch for WB */
    double wb_r_sum = 0, wb_g_sum = 0, wb_b_sum = 0;
    uint32_t neutral_count = 0;
    uint32_t i;
    for (i = 0; i < n_patches; i++) {
        double sr = sensor_rgb[i*3], sg = sensor_rgb[i*3+1], sb = sensor_rgb[i*3+2];
        double tr = target_rgb[i*3], tg = target_rgb[i*3+1], tb = target_rgb[i*3+2];

        /* Detect neutral patches: R ≈ G ≈ B in target */
        if (fabs(tr - tg) < 0.05 && fabs(tg - tb) < 0.05) {
            wb_r_sum += sr; wb_g_sum += sg; wb_b_sum += sb;
            neutral_count++;
        }
    }

    if (neutral_count > 0) {
        double avg_g = wb_g_sum / neutral_count;
        if (avg_g > 0.0) {
            cc->wb_r = avg_g / (wb_r_sum / neutral_count);
            cc->wb_g = 1.0;
            cc->wb_b = avg_g / (wb_b_sum / neutral_count);
        } else {
            cc->wb_r = cc->wb_g = cc->wb_b = 1.0;
        }
    } else {
        cc->wb_r = cc->wb_g = cc->wb_b = 1.0;
    }

    /* 2. Apply WB to sensor data */
    double *wb_sensor = (double *)malloc(n_patches * 3 * sizeof(double));
    if (wb_sensor == NULL) return -1;

    for (i = 0; i < n_patches; i++) {
        wb_sensor[i*3]   = sensor_rgb[i*3]   * cc->wb_r;
        wb_sensor[i*3+1] = sensor_rgb[i*3+1] * cc->wb_g;
        wb_sensor[i*3+2] = sensor_rgb[i*3+2] * cc->wb_b;
    }

    /* 3. CCM from WB-corrected sensor to target */
    int ret = color_ccm_calibrate(wb_sensor, target_rgb, n_patches, cc->ccm);

    /* 4. Compute average delta-E of calibration */
    double sum_de = 0.0;
    cie_xyz_t d65 = illuminant_get_xyz(ILLUMINANT_D65);
    for (i = 0; i < n_patches; i++) {
        rgb_linear_t srgb = {wb_sensor[i*3], wb_sensor[i*3+1], wb_sensor[i*3+2]};
        srgb = color_apply_ccm(srgb, cc->ccm);

        /* Clamp to [0,1] */
        if (srgb.r > 1.0) srgb.r = 1.0;
        if (srgb.r < 0.0) srgb.r = 0.0;
        if (srgb.g > 1.0) srgb.g = 1.0;
        if (srgb.g < 0.0) srgb.g = 0.0;
        if (srgb.b > 1.0) srgb.b = 1.0;
        if (srgb.b < 0.0) srgb.b = 0.0;

        cie_xyz_t xyz1 = srgb_linear_to_xyz(srgb);
        cie_lab_t lab1 = xyz_to_lab(xyz1, d65);

        rgb_linear_t tref = {target_rgb[i*3], target_rgb[i*3+1], target_rgb[i*3+2]};
        cie_xyz_t xyz2 = srgb_linear_to_xyz(tref);
        cie_lab_t lab2 = xyz_to_lab(xyz2, d65);

        sum_de += color_delta_e_2000(lab1, lab2);
    }
    cc->delta_e_avg = sum_de / n_patches;

    free(wb_sensor);
    return ret;
}

rgb_linear_t calibrate_apply_color(rgb_linear_t rgb,
                                    const color_calibration_t *cc)
{
    if (cc == NULL) return rgb;

    /* Apply WB */
    wb_gains_t wb = {cc->wb_r, cc->wb_g, cc->wb_b};
    rgb = color_apply_wb(rgb, &wb);

    /* Apply CCM */
    rgb = color_apply_ccm(rgb, cc->ccm);

    return rgb;
}

/*===========================================================================
 * L7: Full factory calibration pipeline
 *
 * Runs all calibration steps in the correct order.
 *===========================================================================*/

int calibrate_sensor_full(const raw_frame_t *dark_short,
                           const raw_frame_t *dark_long,
                           const raw_frame_t *flat,
                           const uint16_t **ptc_frames, uint32_t ptc_levels,
                           uint32_t ptc_frames_per_level, uint32_t ptc_pixels,
                           const double *ccm_sensor, const double *ccm_target,
                           uint32_t ccm_patches,
                           sensor_calibration_t *cal)
{
    if (cal == NULL) return -1;
    calibration_init(cal);

    /* 1. Black level */
    if (dark_long != NULL) {
        calibrate_black_level(dark_long, &cal->black_level);
    }

    /* 2. Defect map */
    if (dark_short != NULL || dark_long != NULL || flat != NULL) {
        calibrate_defect_map(dark_short, dark_long, flat, &cal->defect_map);
    }

    /* 3. Lens shading */
    if (flat != NULL) {
        calibrate_lens_shading(flat, &cal->lens_shading);
    }

    /* 4. PTC characterization */
    if (ptc_frames != NULL && ptc_levels >= 3) {
        calibrate_ptc(ptc_frames, ptc_levels, ptc_frames_per_level,
                       ptc_pixels, &cal->ptc);
    }

    /* 5. Color calibration */
    if (ccm_sensor != NULL && ccm_target != NULL && ccm_patches >= 4) {
        calibrate_color(ccm_sensor, ccm_target, ccm_patches, &cal->color);
    }

    cal->temperature_c = 25.0;
    cal->timestamp = 0; /* Use current time in real implementation */

    return 0;
}

/*===========================================================================
 * L7: Calibration report and I/O
 *===========================================================================*/

void calibration_report(const sensor_calibration_t *cal)
{
    if (cal == NULL) { printf("Calibration: NULL\n"); return; }

    printf("=== Sensor Calibration Report =================================\n");
    printf("Black level: R=%u Gr=%u Gb=%u B=%u\n",
           (unsigned)cal->black_level.bl_r, (unsigned)cal->black_level.bl_gr,
           (unsigned)cal->black_level.bl_gb, (unsigned)cal->black_level.bl_b);
    printf("Defects:     %u pixels\n", (unsigned)cal->defect_map.count);
    printf("Lens shading: a2=%.6f a4=%.6f a6=%.6f @ (%.1f,%.1f)\n",
           cal->lens_shading.a2, cal->lens_shading.a4,
           cal->lens_shading.a6, cal->lens_shading.cx, cal->lens_shading.cy);
    printf("PTC:\n");
    printf("  Read noise:        %.2f e-\n", cal->ptc.read_noise_e);
    printf("  Conversion gain:   %.3f DN/e-\n", cal->ptc.conversion_gain_dn_e);
    printf("  PRNU:              %.2f%%\n", cal->ptc.prnu_coeff * 100.0);
    printf("  FWC:               %.0f e-\n", cal->ptc.fwc_e);
    printf("  Dynamic range:     %.1f dB\n", cal->ptc.dr_db);
    printf("  SNR max:           %.1f dB\n", cal->ptc.snr_max_db);
    printf("Color:\n");
    printf("  WB gains:          R=%.4f G=%.4f B=%.4f\n",
           cal->color.wb_r, cal->color.wb_g, cal->color.wb_b);
    printf("  CCM:               [%.4f %.4f %.4f]\n",
           cal->color.ccm[0], cal->color.ccm[1], cal->color.ccm[2]);
    printf("                     [%.4f %.4f %.4f]\n",
           cal->color.ccm[3], cal->color.ccm[4], cal->color.ccm[5]);
    printf("                     [%.4f %.4f %.4f]\n",
           cal->color.ccm[6], cal->color.ccm[7], cal->color.ccm[8]);
    printf("  Delta-E avg:       %.2f\n", cal->color.delta_e_avg);
    printf("Temperature: %.1f C\n", cal->temperature_c);
    printf("==============================================================\n");
}

int calibration_save(const sensor_calibration_t *cal, const char *filename)
{
    if (cal == NULL || filename == NULL) return -1;

    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) return -1;

    /* Write binary calibration structure */
    fwrite(&cal->black_level, sizeof(black_level_calib_t), 1, fp);
    fwrite(&cal->lens_shading, sizeof(lens_shading_coeffs_t), 1, fp);
    fwrite(&cal->ptc, sizeof(ptc_calibration_t), 1, fp);
    fwrite(&cal->color, sizeof(color_calibration_t), 1, fp);
    fwrite(&cal->temperature_c, sizeof(double), 1, fp);
    fwrite(&cal->timestamp, sizeof(uint64_t), 1, fp);

    fclose(fp);
    return 0;
}

int calibration_load(sensor_calibration_t *cal, const char *filename)
{
    if (cal == NULL || filename == NULL) return -1;

    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) return -1;

    calibration_init(cal);

    fread(&cal->black_level, sizeof(black_level_calib_t), 1, fp);
    fread(&cal->lens_shading, sizeof(lens_shading_coeffs_t), 1, fp);
    fread(&cal->ptc, sizeof(ptc_calibration_t), 1, fp);
    fread(&cal->color, sizeof(color_calibration_t), 1, fp);
    fread(&cal->temperature_c, sizeof(double), 1, fp);
    fread(&cal->timestamp, sizeof(uint64_t), 1, fp);

    fclose(fp);
    return 0;
}

/*===========================================================================
 * L1: Gain map management
 *===========================================================================*/

gain_map_t *gain_map_alloc(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return NULL;
    gain_map_t *map = (gain_map_t *)malloc(sizeof(gain_map_t));
    if (map == NULL) return NULL;
    map->data = (double *)calloc((size_t)w * h, sizeof(double));
    if (map->data == NULL) {
        free(map);
        return NULL;
    }
    map->width = w;
    map->height = h;
    return map;
}

void gain_map_free(gain_map_t *map)
{
    if (map == NULL) return;
    free(map->data);
    free(map);
}

void gain_map_fill(gain_map_t *map, double value)
{
    if (map == NULL || map->data == NULL) return;
    size_t n = (size_t)map->width * (size_t)map->height;
    size_t i;
    for (i = 0; i < n; i++) map->data[i] = value;
}

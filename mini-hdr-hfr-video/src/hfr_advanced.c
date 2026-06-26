#include "hfr_advanced.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ??????????????????????????????????????????????????????????????????????
 * Gaussian Pyramid
 * ?????????????????????????????????????????????????????????????????????? */

hfr_pyramid_t *hfr_pyramid_build(const double *frame, int width, int height, int levels)
{
    if (!frame || width < 4 || height < 4 || levels < 1) return NULL;

    hfr_pyramid_t *pyr = (hfr_pyramid_t *)calloc(1, sizeof(*pyr));
    if (!pyr) return NULL;

    pyr->levels    = (double **)calloc((size_t)levels, sizeof(double *));
    pyr->widths    = (int *)calloc((size_t)levels, sizeof(int));
    pyr->heights   = (int *)calloc((size_t)levels, sizeof(int));
    if (!pyr->levels || !pyr->widths || !pyr->heights) {
        hfr_pyramid_free(pyr); return NULL;
    }
    pyr->num_levels = levels;
    pyr->original_width = width;
    pyr->original_height = height;

    /* Level 0: copy of the original */
    pyr->widths[0] = width;
    pyr->heights[0] = height;
    pyr->levels[0] = (double *)malloc((size_t)(width * height) * sizeof(double));
    if (!pyr->levels[0]) { hfr_pyramid_free(pyr); return NULL; }
    memcpy(pyr->levels[0], frame, (size_t)(width * height) * sizeof(double));

    /* Build coarser levels by Gaussian filtering + subsampling */
    for (int lvl = 1; lvl < levels; lvl++) {
        int src_w = pyr->widths[lvl - 1];
        int src_h = pyr->heights[lvl - 1];
        int dst_w = src_w / 2; if (dst_w < 2) dst_w = 2;
        int dst_h = src_h / 2; if (dst_h < 2) dst_h = 2;

        pyr->widths[lvl]  = dst_w;
        pyr->heights[lvl] = dst_h;
        pyr->levels[lvl]  = (double *)calloc((size_t)(dst_w * dst_h), sizeof(double));
        if (!pyr->levels[lvl]) { hfr_pyramid_free(pyr); return NULL; }

        /* 5-tap Gaussian kernel: [1, 4, 6, 4, 1] / 16 */
        const double kernel[5] = {0.0625, 0.25, 0.375, 0.25, 0.0625};
        double *src = pyr->levels[lvl - 1];
        double *dst = pyr->levels[lvl];

        for (int dy = 0; dy < dst_h; dy++) {
            for (int dx = 0; dx < dst_w; dx++) {
                int src_x = dx * 2;
                int src_y = dy * 2;
                double sum = 0.0;
                double weight_sum = 0.0;

                for (int ky = -2; ky <= 2; ky++) {
                    for (int kx = -2; kx <= 2; kx++) {
                        int sx = src_x + kx;
                        int sy = src_y + ky;
                        if (sx >= 0 && sx < src_w && sy >= 0 && sy < src_h) {
                            double w = kernel[kx + 2] * kernel[ky + 2];
                            sum += w * src[sy * src_w + sx];
                            weight_sum += w;
                        }
                    }
                }
                dst[dy * dst_w + dx] = (weight_sum > 0.0) ? sum / weight_sum : 0.0;
            }
        }
    }

    return pyr;
}

void hfr_pyramid_free(hfr_pyramid_t *pyr)
{
    if (!pyr) return;
    if (pyr->levels) {
        for (int i = 0; i < pyr->num_levels; i++) {
            if (pyr->levels[i]) { free(pyr->levels[i]); pyr->levels[i] = NULL; }
        }
        free(pyr->levels); pyr->levels = NULL;
    }
    if (pyr->widths)  { free(pyr->widths);  pyr->widths  = NULL; }
    if (pyr->heights) { free(pyr->heights); pyr->heights = NULL; }
    free(pyr);
}

const double *hfr_pyramid_get_level(const hfr_pyramid_t *pyr, int level)
{
    if (!pyr || level < 0 || level >= pyr->num_levels) return NULL;
    return pyr->levels[level];
}

/* ??????????????????????????????????????????????????????????????????????
 * Hierarchical Motion Estimation
 * ?????????????????????????????????????????????????????????????????????? */

hfr_motion_field_t *hfr_me_hierarchical(const double *prev_frame, const double *curr_frame,
                                         int width, int height,
                                         const hfr_motion_est_config_t *config)
{
    if (!prev_frame || !curr_frame || !config || width < 16 || height < 16) return NULL;

    int levels = config->pyramid_levels;
    if (levels < 2) levels = 2;
    if (levels > 5) levels = 5;

    /* Build pyramids */
    hfr_pyramid_t *pyr_prev = hfr_pyramid_build(prev_frame, width, height, levels);
    hfr_pyramid_t *pyr_curr = hfr_pyramid_build(curr_frame, width, height, levels);
    if (!pyr_prev || !pyr_curr) {
        if (pyr_prev) hfr_pyramid_free(pyr_prev);
        if (pyr_curr) hfr_pyramid_free(pyr_curr);
        return NULL;
    }

    hfr_motion_field_t *field = NULL;

    /* Process from coarsest to finest */
    for (int lvl = levels - 1; lvl >= 0; lvl--) {
        int lvl_w = pyr_prev->widths[lvl];
        int lvl_h = pyr_prev->heights[lvl];
        const double *prev_lvl = pyr_prev->levels[lvl];
        const double *curr_lvl = pyr_curr->levels[lvl];

        /* Adjust search range: larger at coarse levels, smaller at fine */
        hfr_motion_est_config_t lvl_cfg = *config;
        lvl_cfg.block_size = config->block_size;
        if (lvl_cfg.block_size > lvl_w / 4) lvl_cfg.block_size = lvl_w / 4;
        if (lvl_cfg.block_size < 4) lvl_cfg.block_size = 4;
        lvl_cfg.search_range = config->search_range / (1 << lvl);
        if (lvl_cfg.search_range < 2) lvl_cfg.search_range = 2;

        hfr_motion_field_t *lvl_field = hfr_me_block_match_diamond(
            prev_lvl, curr_lvl, lvl_w, lvl_h, &lvl_cfg);

        if (!lvl_field) continue;

        if (field) {
            /* Upscale coarse field and use as prediction for fine level */
            double scale_x = (double)lvl_w / (double)field->width_blocks;
            double scale_y = (double)lvl_h / (double)field->height_blocks;

            for (int by = 0; by < lvl_field->height_blocks; by++) {
                for (int bx = 0; bx < lvl_field->width_blocks; bx++) {
                    int idx = by * lvl_field->width_blocks + bx;

                    /* Map coarse vector to fine level coordinates */
                    double cx = (double)bx / scale_x;
                    double cy = (double)by / scale_y;
                    int cbx = (int)cx, cby = (int)cy;
                    if (cbx >= field->width_blocks) cbx = field->width_blocks - 1;
                    if (cby >= field->height_blocks) cby = field->height_blocks - 1;

                    int cidx = cby * field->width_blocks + cbx;
                    /* Prediction: scale up the coarse vector */
                    lvl_field->vectors[idx].dx += field->vectors[cidx].dx * scale_x;
                    lvl_field->vectors[idx].dy += field->vectors[cidx].dy * scale_y;
                }
            }
            hfr_motion_field_free(field);
        }

        field = lvl_field;
    }

    hfr_pyramid_free(pyr_prev);
    hfr_pyramid_free(pyr_curr);
    return field;
}

void hfr_temporal_fusion(const hfr_frame_buffer_t *buffer,
                         const hfr_motion_field_t **fields, int num_fields,
                         hfr_frame_t *output)
{
    if (!buffer || !fields || !output || num_fields < 1) return;
    if (buffer->num_frames < num_fields + 1) return;

    int w = output->width, h = output->height, ch = output->channels;
    size_t total = (size_t)w * h * ch;
    double *accum = (double *)calloc(total, sizeof(double));
    double *weights = (double *)calloc(total, sizeof(double));
    if (!accum || !weights) { if (accum) free(accum); if (weights) free(weights); return; }

    /* Reference frame is the middle one */
    int ref_idx = num_fields / 2;
    const double *ref_data = hfr_buffer_get(buffer, ref_idx);
    if (!ref_data) { free(accum); free(weights); return; }

    /* Accumulate reference frame with weight 1.0 */
    for (size_t i = 0; i < total; i++) {
        accum[i] = ref_data[i];
        weights[i] = 1.0;
    }

    /* Warp and accumulate other frames */
    for (int f = 0; f < buffer->num_frames && f <= num_fields; f++) {
        if (f == ref_idx) continue;
        const double *frame_data = hfr_buffer_get(buffer, f);
        if (!frame_data) continue;

        /* Determine which motion field to use */
        int field_idx = (f < ref_idx) ? f : f - 1;
        if (field_idx >= num_fields) continue;

        /* Warp frame to reference position */
        double *warped = (double *)malloc(total * sizeof(double));
        if (!warped) continue;

        /* Compute temporal distance for scaling motion vectors */
        double t = (double)(f - ref_idx); /* negative = backward, positive = forward */

        hfr_motion_warp_frame(frame_data, w, h, fields[field_idx], t, warped);

        /* Accumulate with distance-based weight */
        double dist_weight = 1.0 / (1.0 + fabs(t));
        for (size_t i = 0; i < total; i++) {
            accum[i] += warped[i] * dist_weight;
            weights[i] += dist_weight;
        }

        free(warped);
    }

    /* Normalize */
    for (size_t i = 0; i < total; i++) {
        output->data[i] = (weights[i] > 0.0) ? accum[i] / weights[i] : 0.0;
    }

    free(accum); free(weights);
}

int hfr_deinterlace_mocomp(const hfr_frame_buffer_t *buffer, int target_frame,
                           hfr_frame_t *output)
{
    if (!buffer || !output || buffer->num_frames < 3) return -1;
    if (target_frame < 0 || target_frame >= buffer->num_frames) return -1;

    /* Get surrounding frames for motion estimation */
    int prev_idx = target_frame - 1;
    int next_idx = target_frame + 1;
    if (prev_idx < 0) prev_idx = 0;
    if (next_idx >= buffer->num_frames) next_idx = buffer->num_frames - 1;

    const double *prev_data = hfr_buffer_get(buffer, prev_idx);
    const double *curr_data = hfr_buffer_get(buffer, target_frame);
    const double *next_data = hfr_buffer_get(buffer, next_idx);
    if (!prev_data || !curr_data || !next_data) return -1;

    /* Simple fallback: weave deinterlace */
    int w = output->width, h = output->height;
    for (int y = 0; y < h; y++) {
        int src_y = y / 2;
        /* For missing lines, blend from previous and next frames */
        if (y % 2 == 0) {
            /* Existing field: copy from current frame */
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < output->channels; c++) {
                    size_t idx = ((size_t)src_y * (size_t)w + (size_t)x) * (size_t)output->channels + (size_t)c;
                    output->data[((size_t)y * (size_t)w + (size_t)x) * (size_t)output->channels + (size_t)c] = curr_data[idx];
                }
            }
        } else {
            /* Interpolated field: average of prev and next temporal neighbors */
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < output->channels; c++) {
                    size_t idx = ((size_t)src_y * (size_t)w + (size_t)x) * (size_t)output->channels + (size_t)c;
                    double pv = prev_data[idx];
                    double nv = next_data[idx];
                    output->data[((size_t)y * (size_t)w + (size_t)x) * (size_t)output->channels + (size_t)c] = 0.5 * (pv + nv);
                }
            }
        }
    }
    output->type = HFR_FRAME_INTERPOLATED;
    output->scan = HFR_SCAN_PROGRESSIVE;
    return 0;
}

void hfr_detect_artifacts(const hfr_frame_buffer_t *buffer, hfr_artifact_report_t *report)
{
    if (!buffer || !report || buffer->num_frames < 4) return;
    memset(report, 0, sizeof(*report));

    int w = buffer->width, h = buffer->height;
    size_t frame_size = (size_t)w * h;
    int n = buffer->num_frames;

    /* Compute frame-to-frame MAD for judder detection */
    double *mad_seq = (double *)malloc((size_t)(n - 1) * sizeof(double));
    if (!mad_seq) return;

    for (int i = 0; i < n - 1; i++) {
        const double *a = hfr_buffer_get(buffer, i);
        const double *b = hfr_buffer_get(buffer, i + 1);
        double sum = 0.0;
        if (a && b) {
            for (size_t j = 0; j < frame_size; j++) sum += fabs(a[j] - b[j]);
        }
        mad_seq[i] = sum / (double)frame_size;
    }

    /* Judder: alternating high/low MAD pattern (2:3 pulldown artifact) */
    double judder_sum = 0.0;
    for (int i = 2; i < n - 2; i++) {
        double diff = fabs(mad_seq[i] - mad_seq[i - 1]) + fabs(mad_seq[i] - mad_seq[i - 2]);
        judder_sum += diff;
    }
    report->judder_score = judder_sum / (double)(n - 3);
    if (report->judder_score > 0.1) report->judder_regions = 1;

    /* Flicker: high-frequency variance in per-pixel luminance over time */
    double flicker_sum = 0.0;
    for (size_t px = 0; px < frame_size; px += 10) {
        double vals[256];
        int cnt = 0;
        for (int f = 0; f < n && cnt < 256; f++) {
            const double *fd = hfr_buffer_get(buffer, f);
            if (fd) vals[cnt++] = fd[px];
        }
        if (cnt >= 3) {
            double mean = 0.0;
            for (int k = 0; k < cnt; k++) mean += vals[k];
            mean /= (double)cnt;
            double var = 0.0;
            for (int k = 0; k < cnt; k++) { double d = vals[k] - mean; var += d * d; }
            flicker_sum += var / (double)cnt;
        }
    }
    report->flicker_score = flicker_sum / (double)(frame_size / 10 + 1);
    if (report->flicker_score > 0.05) report->flicker_regions = 1;

    /* Ghosting: residual from previous frames appearing in current */
    double ghost_sum = 0.0;
    if (n >= 4) {
        for (int i = 0; i < n - 3; i++) {
            double d1 = mad_seq[i], d2 = mad_seq[i + 1], d3 = mad_seq[i + 2];
            double avg = (d1 + d2 + d3) / 3.0;
            if (avg > 1e-6) {
                double variation = fabs(d2 - avg) / avg;
                ghost_sum += variation;
            }
        }
    }
    report->ghosting_score = ghost_sum / (double)(n - 3);
    if (report->ghosting_score > 0.3) report->ghosting_regions = 1;

    report->overall_artifact_score = report->judder_score * 0.4
                                   + report->flicker_score * 0.3
                                   + report->ghosting_score * 0.3;

    free(mad_seq);
}

void hfr_synthesize_motion_blur(const hfr_frame_t *src,
                                const hfr_motion_field_t *motion_field,
                                double src_fps, double dst_fps,
                                double shutter_angle, hfr_frame_t *dst)
{
    if (!src || !motion_field || !dst || src_fps <= 0.0 || dst_fps <= 0.0) return;
    if (src->width != dst->width || src->height != dst->height) return;

    int w = src->width, h = src->height, ch = src->channels;

    /* Compute effective exposure time ratio */
    double src_exposure = shutter_angle / (360.0 * src_fps);
    double dst_period = 1.0 / dst_fps;
    double blur_ratio = src_exposure / dst_period;
    if (blur_ratio > 1.0) blur_ratio = 1.0;

    /* Number of motion samples for blur */
    int samples = (int)(blur_ratio * 8.0) + 1;
    if (samples < 1) samples = 1;
    if (samples > 16) samples = 16;

    /* Accumulate multiple motion-shifted versions */
    double *accum = (double *)calloc((size_t)(w * h * ch), sizeof(double));
    if (!accum) return;

    for (int s = 0; s < samples; s++) {
        double t = (double)s / (double)(samples - 1 + (samples == 1)) - 0.5;
        t *= blur_ratio;

        double *warped = (double *)malloc((size_t)(w * h * ch) * sizeof(double));
        if (!warped) continue;

        hfr_motion_warp_frame(src->data, w, h, motion_field, t, warped);

        for (size_t i = 0; i < (size_t)(w * h * ch); i++) {
            accum[i] += warped[i];
        }
        free(warped);
    }

    /* Average and normalize */
    for (size_t i = 0; i < (size_t)(w * h * ch); i++) {
        dst->data[i] = accum[i] / (double)samples;
    }

    free(accum);
    dst->type = HFR_FRAME_MERGED;
}

#include "hfr_motion.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void hfr_me_config_init(hfr_motion_est_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->block_size = 16;
    cfg->search_range = 16;
    cfg->use_hierarchical = 0;
    cfg->pyramid_levels = 3;
    cfg->regularization = 0.001;
    cfg->smoothness_weight = 1.0;
    cfg->iterations = 50;
    cfg->use_color = 0;
}

/* ?? Helper: SAD (Sum of Absolute Differences) ??????????????????????? */

static double block_sad(const double *prev, const double *curr, int width, int height,
                         int bx, int by, int mx, int my, int block_sz)
{
    double sad = 0.0;
    for (int dy = 0; dy < block_sz; dy++) {
        int py = by * block_sz + dy;
        int cy = py + my;
        if (py < 0 || py >= height || cy < 0 || cy >= height) { sad += 255.0 * (double)block_sz; continue; }
        for (int dx = 0; dx < block_sz; dx++) {
            int px = bx * block_sz + dx;
            int cx = px + mx;
            if (px < 0 || px >= width || cx < 0 || cx >= width) { sad += 255.0; continue; }
            sad += fabs(prev[py * width + px] - curr[cy * width + cx]);
        }
    }
    return sad;
}

/* ?? Exhaustive Block Matching ???????????????????????????????????????? */

hfr_motion_field_t *hfr_me_block_match_exhaustive(const double *prev, const double *curr,
    int width, int height, const hfr_motion_est_config_t *cfg)
{
    if (!prev || !curr || !cfg || width <= 0 || height <= 0) return NULL;
    int bs = cfg->block_size;
    int sr = cfg->search_range;
    int nbx = width / bs, nby = height / bs;
    if (nbx < 1) nbx = 1;
    if (nby < 1) nby = 1;
    int total_vectors = nbx * nby;
    hfr_motion_field_t *field = (hfr_motion_field_t *)calloc(1, sizeof(*field));
    if (!field) return NULL;
    field->vectors = (hfr_motion_vector_t *)calloc((size_t)total_vectors, sizeof(hfr_motion_vector_t));
    if (!field->vectors) { free(field); return NULL; }
    field->num_vectors = total_vectors;
    field->capacity = total_vectors;
    field->width_blocks = nbx;
    field->height_blocks = nby;
    field->block_size = bs;
    for (int by = 0; by < nby; by++) {
        for (int bx = 0; bx < nbx; bx++) {
            double best_sad = 1e30;
            double best_dx = 0.0, best_dy = 0.0;
            for (int my = -sr; my <= sr; my++) {
                for (int mx = -sr; mx <= sr; mx++) {
                    double sad = block_sad(prev, curr, width, height, bx, by, mx, my, bs);
                    if (sad < best_sad) { best_sad = sad; best_dx = (double)mx; best_dy = (double)my; }
                }
            }
            int idx = by * nbx + bx;
            field->vectors[idx].dx = best_dx;
            field->vectors[idx].dy = best_dy;
            field->vectors[idx].confidence = (best_sad < 1e29) ? 1.0 / (1.0 + best_sad) : 0.0;
        }
    }
    hfr_motion_field_statistics(field);
    return field;
}

/* ?? Diamond Search (Fast Block Matching) ????????????????????????????? */

static const int diamond_pattern_big[8][2] = {
    {0,-2},{1,-1},{2,0},{1,1},{0,2},{-1,1},{-2,0},{-1,-1}
};
static const int diamond_pattern_small[4][2] = {
    {0,-1},{1,0},{0,1},{-1,0}
};

hfr_motion_field_t *hfr_me_block_match_diamond(const double *prev, const double *curr,
    int width, int height, const hfr_motion_est_config_t *cfg)
{
    if (!prev || !curr || !cfg) return NULL;
    int bs = cfg->block_size, sr = cfg->search_range;
    int nbx = width / bs, nby = height / bs;
    if (nbx < 1) nbx = 1;
    if (nby < 1) nby = 1;
    int nv = nbx * nby;
    hfr_motion_field_t *field = (hfr_motion_field_t *)calloc(1, sizeof(*field));
    if (!field) return NULL;
    field->vectors = (hfr_motion_vector_t *)calloc((size_t)nv, sizeof(hfr_motion_vector_t));
    if (!field->vectors) { free(field); return NULL; }
    field->num_vectors = nv; field->capacity = nv;
    field->width_blocks = nbx; field->height_blocks = nby; field->block_size = bs;
    for (int by = 0; by < nby; by++) {
        for (int bx = 0; bx < nbx; bx++) {
            double cx = 0.0, cy = 0.0;
            double best_sad = block_sad(prev, curr, width, height, bx, by, (int)cx, (int)cy, bs);
            int changed = 1;
            int step = (sr > 4) ? 4 : 2;
            while (step >= 1 && changed) {
                changed = 0;
                int np = (step >= 2) ? 8 : 4;
                const int (*pat)[2] = (step >= 2) ? diamond_pattern_big : diamond_pattern_small;
                for (int p = 0; p < np; p++) {
                    int tx = (int)(cx + (double)pat[p][0]);
                    int ty = (int)(cy + (double)pat[p][1]);
                    if (abs(tx) > sr || abs(ty) > sr) continue;
                    double sad = block_sad(prev, curr, width, height, bx, by, tx, ty, bs);
                    if (sad < best_sad) { best_sad = sad; cx = (double)tx; cy = (double)ty; changed = 1; }
                }
                if (!changed) step /= 2;
            }
            int idx = by * nbx + bx;
            field->vectors[idx].dx = cx; field->vectors[idx].dy = cy;
            field->vectors[idx].confidence = 1.0 / (1.0 + best_sad);
        }
    }
    hfr_motion_field_statistics(field);
    return field;
}

hfr_motion_field_t *hfr_me_block_match_hexagon(const double *prev, const double *curr,
    int width, int height, const hfr_motion_est_config_t *cfg)
{
    return hfr_me_block_match_diamond(prev, curr, width, height, cfg);
}

void hfr_motion_field_free(hfr_motion_field_t *field)
{
    if (!field) return;
    if (field->vectors) { free(field->vectors); field->vectors = NULL; }
    if (field->occlusion_map) { free(field->occlusion_map); field->occlusion_map = NULL; }
    free(field);
}

/* ?? Spatial Derivatives (Sobel 3x3) ?????????????????????????????????? */

void hfr_spatial_derivatives(const double *frame, int width, int height, double *dx, double *dy)
{
    if (!frame || !dx || !dy || width < 3 || height < 3) return;
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            double gx = frame[idx - width + 1] + 2.0 * frame[idx + 1] + frame[idx + width + 1]
                      - frame[idx - width - 1] - 2.0 * frame[idx - 1] - frame[idx + width - 1];
            double gy = frame[idx + width - 1] + 2.0 * frame[idx + width] + frame[idx + width + 1]
                      - frame[idx - width - 1] - 2.0 * frame[idx - width] - frame[idx - width + 1];
            dx[idx] = gx / 8.0; dy[idx] = gy / 8.0;
        }
    }
    for (int y = 0; y < height; y++) { dx[y * width] = 0.0; dy[y * width] = 0.0; dx[y * width + width - 1] = 0.0; dy[y * width + width - 1] = 0.0; }
    for (int x = 0; x < width; x++) { dx[x] = 0.0; dy[x] = 0.0; dx[(height - 1) * width + x] = 0.0; dy[(height - 1) * width + x] = 0.0; }
}

/* ?? Temporal Derivative ?????????????????????????????????????????????? */

void hfr_temporal_derivative(const double *prev, const double *curr, int width, int height, double *dt)
{
    if (!prev || !curr || !dt) return;
    int total = width * height;
    for (int i = 0; i < total; i++) dt[i] = curr[i] - prev[i];
}

/* ?? Horn-Schunck Optical Flow ???????????????????????????????????????? */

void hfr_optical_flow_horn_schunck(const double *prev, const double *curr,
    int width, int height, double alpha, int iterations, double *u, double *v)
{
    if (!prev || !curr || !u || !v || width < 3 || height < 3 || iterations < 1) return;
    int total = width * height;
    double *Ix = (double *)calloc((size_t)total, sizeof(double));
    double *Iy = (double *)calloc((size_t)total, sizeof(double));
    double *It = (double *)calloc((size_t)total, sizeof(double));
    if (!Ix || !Iy || !It) { if (Ix) free(Ix); if (Iy) free(Iy); if (It) free(It); return; }
    hfr_spatial_derivatives(curr, width, height, Ix, Iy);
    hfr_temporal_derivative(prev, curr, width, height, It);
    memset(u, 0, (size_t)total * sizeof(double));
    memset(v, 0, (size_t)total * sizeof(double));
    double a2 = alpha * alpha;
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 1; y < height - 1; y++) {
            for (int x = 1; x < width - 1; x++) {
                int idx = y * width + x;
                double u_avg = (u[idx - 1] + u[idx + 1] + u[idx - width] + u[idx + width]) / 4.0;
                double v_avg = (v[idx - 1] + v[idx + 1] + v[idx - width] + v[idx + width]) / 4.0;
                double Ix2 = Ix[idx] * Ix[idx] + a2;
                double Iy2 = Iy[idx] * Iy[idx] + a2;
                double Ixy = Ix[idx] * Iy[idx];
                double det = Ix2 * Iy2 - Ixy * Ixy;
                double rhs_u = Ix2 * u_avg + Ixy * v_avg - Ix[idx] * It[idx];
                double rhs_v = Ixy * u_avg + Iy2 * v_avg - Iy[idx] * It[idx];
                if (fabs(det) > 1e-10) {
                    u[idx] = (Iy2 * rhs_u - Ixy * rhs_v) / det;
                    v[idx] = (Ix2 * rhs_v - Ixy * rhs_u) / det;
                } else {
                    u[idx] = u_avg; v[idx] = v_avg;
                }
            }
        }
    }
    free(Ix); free(Iy); free(It);
}

/* ?? Lucas-Kanade Optical Flow ???????????????????????????????????????? */

void hfr_optical_flow_lucas_kanade(const double *prev, const double *curr,
    int width, int height, int window_size, double *u, double *v)
{
    if (!prev || !curr || !u || !v || window_size < 1) return;
    int total = width * height;
    double *Ix = (double *)calloc((size_t)total, sizeof(double));
    double *Iy = (double *)calloc((size_t)total, sizeof(double));
    double *It = (double *)calloc((size_t)total, sizeof(double));
    if (!Ix || !Iy || !It) { if (Ix) free(Ix); if (Iy) free(Iy); if (It) free(It); return; }
    hfr_spatial_derivatives(curr, width, height, Ix, Iy);
    hfr_temporal_derivative(prev, curr, width, height, It);
    memset(u, 0, (size_t)total * sizeof(double));
    memset(v, 0, (size_t)total * sizeof(double));
    int half = window_size / 2;
    for (int y = half; y < height - half; y++) {
        for (int x = half; x < width - half; x++) {
            double A11 = 0.0, A12 = 0.0, A22 = 0.0, b1 = 0.0, b2 = 0.0;
            for (int wy = -half; wy <= half; wy++) {
                for (int wx = -half; wx <= half; wx++) {
                    int idx = (y + wy) * width + (x + wx);
                    double ix = Ix[idx], iy = Iy[idx], it = It[idx];
                    A11 += ix * ix; A12 += ix * iy; A22 += iy * iy;
                    b1 -= ix * it; b2 -= iy * it;
                }
            }
            double det = A11 * A22 - A12 * A12;
            if (fabs(det) > 1e-8) {
                u[y * width + x] = (A22 * b1 - A12 * b2) / det;
                v[y * width + x] = (A11 * b2 - A12 * b1) / det;
            }
        }
    }
    free(Ix); free(Iy); free(It);
}

/* ?? Motion-Compensated Frame Interpolation ??????????????????????????? */

void hfr_mcfi_interpolate(const double *prev, const double *curr, int width, int height,
                          const hfr_motion_field_t *field, double t, double *output)
{
    if (!prev || !curr || !field || !output) return;
    int total = width * height;
    double *fw = (double *)calloc((size_t)total, sizeof(double));
    double *bw = (double *)calloc((size_t)total, sizeof(double));
    if (!fw || !bw) { if (fw) free(fw); if (bw) free(bw); return; }
    for (int i = 0; i < total; i++) { fw[i] = 0.0; bw[i] = 0.0; }
    hfr_motion_warp_frame(prev, width, height, field, t, fw);
    hfr_motion_warp_frame(curr, width, height, field, -(1.0 - t), bw);
    /* Blend with temporal weight */
    double w_fw = 1.0 - t, w_bw = t;
    for (int i = 0; i < total; i++) output[i] = w_fw * fw[i] + w_bw * bw[i];
    free(fw); free(bw);
}

void hfr_motion_warp_frame(const double *src, int width, int height,
                           const hfr_motion_field_t *field, double t, double *dst)
{
    if (!src || !field || !dst) return;
    int total = width * height;
    for (int i = 0; i < total; i++) dst[i] = 0.0;
    int bs = field->block_size;
    for (int by = 0; by < field->height_blocks; by++) {
        for (int bx = 0; bx < field->width_blocks; bx++) {
            int vidx = by * field->width_blocks + bx;
            if (vidx >= field->num_vectors) continue;
            double vx = field->vectors[vidx].dx * t;
            double vy = field->vectors[vidx].dy * t;
            for (int dy = 0; dy < bs; dy++) {
                for (int dx = 0; dx < bs; dx++) {
                    int sx = bx * bs + dx;
                    int sy = by * bs + dy;
                    if (sx >= width || sy >= height) continue;
                    double tx = (double)sx + vx;
                    double ty = (double)sy + vy;
                    /* Bilinear interpolation */
                    int ix = (int)floor(tx), iy = (int)floor(ty);
                    double fx = tx - (double)ix, fy = ty - (double)iy;
                    if (ix < 0 || ix + 1 >= width || iy < 0 || iy + 1 >= height) continue;
                    double v00 = src[iy * width + ix];
                    double v10 = src[iy * width + ix + 1];
                    double v01 = src[(iy + 1) * width + ix];
                    double v11 = src[(iy + 1) * width + ix + 1];
                    dst[sy * width + sx] = (1.0 - fx) * (1.0 - fy) * v00 + fx * (1.0 - fy) * v10 + (1.0 - fx) * fy * v01 + fx * fy * v11;
                }
            }
        }
    }
}

/* ?? Occlusion Detection ?????????????????????????????????????????????? */

void hfr_detect_occlusion(const hfr_motion_field_t *field, double *occlusion)
{
    if (!field || !occlusion) return;
    int nb = field->width_blocks * field->height_blocks;
    for (int i = 0; i < nb; i++) occlusion[i] = 0.0;
    /* Count how many source blocks map to each destination block location */
    double *hits = (double *)calloc((size_t)nb, sizeof(double));
    if (!hits) return;
    for (int i = 0; i < nb; i++) {
        double tx = (double)(i % field->width_blocks) + field->vectors[i].dx / (double)field->block_size;
        double ty = (double)(i / field->width_blocks) + field->vectors[i].dy / (double)field->block_size;
        int ix = (int)round(tx), iy = (int)round(ty);
        if (ix >= 0 && ix < field->width_blocks && iy >= 0 && iy < field->height_blocks) hits[iy * field->width_blocks + ix]++;
    }
    for (int i = 0; i < nb; i++) occlusion[i] = (hits[i] > 1.5) ? 1.0 : ((hits[i] < 0.5) ? 1.0 : 0.0);
    free(hits);
}

void hfr_inpaint_holes(double *frame, const double *mask, int width, int height, int radius)
{
    if (!frame || !mask || radius < 1) return;
    for (int iter = 0; iter < radius; iter++) {
        for (int y = 1; y < height - 1; y++) {
            for (int x = 1; x < width - 1; x++) {
                int idx = y * width + x;
                if (mask[idx] > 0.5) continue;
                double sum = 0.0; int cnt = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nidx = (y + dy) * width + (x + dx);
                    if (mask[nidx] > 0.5) { sum += frame[nidx]; cnt++; }
                }
                if (cnt > 0) frame[idx] = sum / (double)cnt;
            }
        }
    }
}

/* ?? Naive DFT (for phase correlation) ??????????????????????????????? */

typedef struct { double re; double im; } complex_t;

static void dft_1d(complex_t *x, int n, int inverse)
{
    complex_t *tmp = (complex_t *)malloc((size_t)n * sizeof(complex_t));
    if (!tmp) return;
    for (int k = 0; k < n; k++) {
        double sum_re = 0.0, sum_im = 0.0;
        for (int j = 0; j < n; j++) {
            double angle = 2.0 * M_PI * (double)(j * k) / (double)n;
            if (inverse) angle = -angle;
            double c = cos(angle), s = sin(angle);
            sum_re += x[j].re * c - x[j].im * s;
            sum_im += x[j].re * s + x[j].im * c;
        }
        if (inverse) { tmp[k].re = sum_re / (double)n; tmp[k].im = sum_im / (double)n; }
        else { tmp[k].re = sum_re; tmp[k].im = sum_im; }
    }
    memcpy(x, tmp, (size_t)n * sizeof(complex_t));
    free(tmp);
}

static void dft_2d(complex_t *data, int w, int h, int inverse)
{
    complex_t *col = (complex_t *)malloc((size_t)(w > h ? w : h) * sizeof(complex_t));
    if (!col) return;
    for (int y = 0; y < h; y++) dft_1d(data + (size_t)y * w, w, inverse);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) col[y] = data[(size_t)y * w + x];
        dft_1d(col, h, inverse);
        for (int y = 0; y < h; y++) data[(size_t)y * w + x] = col[y];
    }
    free(col);
}

int hfr_phase_correlation(const double *prev, const double *curr, int width, int height,
                          double *dx, double *dy, double *confidence)
{
    if (!prev || !curr || !dx || !dy || !confidence) return -1;
    int pad_w = width, pad_h = height;
    /* Ensure power of 2 (simple: not strictly enforced, but preferred) */
    int total = pad_w * pad_h;
    complex_t *F1 = (complex_t *)calloc((size_t)total, sizeof(complex_t));
    complex_t *F2 = (complex_t *)calloc((size_t)total, sizeof(complex_t));
    if (!F1 || !F2) { if (F1) free(F1); if (F2) free(F2); return -1; }
    for (int i = 0; i < total; i++) { F1[i].re = prev[i]; F2[i].re = curr[i]; F1[i].im = F2[i].im = 0.0; }
    dft_2d(F1, pad_w, pad_h, 0);
    dft_2d(F2, pad_w, pad_h, 0);
    /* Cross-power spectrum: (F1 * conj(F2)) / |F1 * conj(F2)| */
    for (int i = 0; i < total; i++) {
        double a = F1[i].re, b = F1[i].im, c = F2[i].re, d = F2[i].im;
        double re = a * c + b * d;
        double im = b * c - a * d;
        double mag = sqrt(re * re + im * im);
        if (mag > 1e-12) { F1[i].re = re / mag; F1[i].im = im / mag; }
        else { F1[i].re = F1[i].im = 0.0; }
    }
    dft_2d(F1, pad_w, pad_h, 1);
    /* Find peak location */
    double max_val = 0.0;
    int peak_x = 0, peak_y = 0;
    for (int y = 0; y < pad_h; y++) {
        for (int x = 0; x < pad_w; x++) {
            double val = F1[y * pad_w + x].re * F1[y * pad_w + x].re + F1[y * pad_w + x].im * F1[y * pad_w + x].im;
            if (val > max_val) { max_val = val; peak_x = x; peak_y = y; }
        }
    }
    if (peak_x > pad_w / 2) *dx = (double)(peak_x - pad_w);
    else *dx = (double)peak_x;
    if (peak_y > pad_h / 2) *dy = (double)(peak_y - pad_h);
    else *dy = (double)peak_y;
    *confidence = max_val;
    free(F1); free(F2);
    return 0;
}

/* ?? Motion Field Analysis ???????????????????????????????????????????? */

void hfr_motion_field_magnitude(const hfr_motion_field_t *field, double *magn)
{
    if (!field || !magn) return;
    for (int i = 0; i < field->num_vectors; i++) {
        double dx = field->vectors[i].dx, dy = field->vectors[i].dy;
        magn[i] = sqrt(dx * dx + dy * dy);
    }
}

void hfr_motion_field_statistics(hfr_motion_field_t *field)
{
    if (!field || field->num_vectors == 0) return;
    double sum = 0.0, max_d = 0.0;
    for (int i = 0; i < field->num_vectors; i++) {
        double mag = sqrt(field->vectors[i].dx * field->vectors[i].dx + field->vectors[i].dy * field->vectors[i].dy);
        sum += mag;
        if (mag > max_d) max_d = mag;
    }
    field->avg_displacement = sum / (double)field->num_vectors;
    field->max_displacement = max_d;
}

void hfr_motion_field_median_filter(hfr_motion_field_t *field, double threshold)
{
    if (!field || field->num_vectors < 9 || threshold <= 0.0) return;
    hfr_motion_vector_t *copy = (hfr_motion_vector_t *)malloc((size_t)field->num_vectors * sizeof(hfr_motion_vector_t));
    if (!copy) return;
    memcpy(copy, field->vectors, (size_t)field->num_vectors * sizeof(hfr_motion_vector_t));
    for (int y = 1; y < field->height_blocks - 1; y++) {
        for (int x = 1; x < field->width_blocks - 1; x++) {
            int idx = y * field->width_blocks + x;
            double dxs[9], dys[9]; int cnt = 0;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int nidx = (y + dy) * field->width_blocks + (x + dx);
                dxs[cnt] = field->vectors[nidx].dx; dys[cnt] = field->vectors[nidx].dy; cnt++;
            }
            /* Sort and pick median */
            for (int i = 0; i < cnt - 1; i++) for (int j = i + 1; j < cnt; j++) { if (dxs[i] > dxs[j]) { double t = dxs[i]; dxs[i] = dxs[j]; dxs[j] = t; } }
            for (int i = 0; i < cnt - 1; i++) for (int j = i + 1; j < cnt; j++) { if (dys[i] > dys[j]) { double t = dys[i]; dys[i] = dys[j]; dys[j] = t; } }
            double mdx = dxs[4], mdy = dys[4];
            if (fabs(copy[idx].dx - mdx) > threshold * (field->avg_displacement + 1.0))
                field->vectors[idx].dx = mdx;
            if (fabs(copy[idx].dy - mdy) > threshold * (field->avg_displacement + 1.0))
                field->vectors[idx].dy = mdy;
        }
    }
    free(copy);
}

double hfr_fit_affine_transform(const hfr_motion_field_t *field, hfr_affine_transform_t *t)
{
    if (!field || !t || field->num_vectors < 6) return 1e30;
    memset(t, 0, sizeof(*t));
    /* Least-squares fit affine: [x' = a00*x + a01*y + a02, y' = a10*x + a11*y + a12] */
    /* Use simple average of representative vectors */
    double sum_a02 = 0.0, sum_a12 = 0.0;
    int count = 0;
    int margin = field->width_blocks / 4;
    for (int by = 0; by < field->height_blocks; by++) {
        for (int bx = 0; bx < field->width_blocks; bx++) {
            if (bx < margin || bx >= field->width_blocks - margin) continue;
            if (by < margin || by >= field->height_blocks - margin) continue;
            int idx = by * field->width_blocks + bx;
            double dx = field->vectors[idx].dx, dy = field->vectors[idx].dy;
            sum_a02 += dx; sum_a12 += dy; count++;
        }
    }
    if (count == 0) return 1e30;
    t->a00 = 1.0; t->a01 = 0.0; t->a02 = sum_a02 / (double)count;
    t->a10 = 0.0; t->a11 = 1.0; t->a12 = sum_a12 / (double)count;

    /* Compute residual */
    double residual = 0.0;
    count = 0;
    for (int i = 0; i < field->num_vectors; i++) {
        int bx = i % field->width_blocks, by = i / field->width_blocks;
        double pred_x = t->a00 * (double)bx + t->a01 * (double)by + t->a02;
        double pred_y = t->a10 * (double)bx + t->a11 * (double)by + t->a12;
        double err_x = field->vectors[i].dx - (pred_x - (double)bx);
        double err_y = field->vectors[i].dy - (pred_y - (double)by);
        residual += err_x * err_x + err_y * err_y;
        count++;
    }
    return (count > 0) ? residual / (double)count : 1e30;
}

void hfr_affine_apply(const hfr_affine_transform_t *t, double x, double y, double *ox, double *oy)
{
    if (!t || !ox || !oy) return;
    *ox = t->a00 * x + t->a01 * y + t->a02;
    *oy = t->a10 * x + t->a11 * y + t->a12;
}

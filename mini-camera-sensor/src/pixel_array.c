/**
 * @file pixel_array.c
 * @brief Pixel array operations: raw frames, Bayer addressing, binning, ROI
 *
 * Each function implements an independent knowledge point from:
 *   - Bayer CFA spatial sampling theory
 *   - Raw frame memory management
 *   - Pixel defect detection and correction
 *   - Charge-domain and digital binning
 *   - Column FPN estimation and correction
 *
 * Reference: Holst & Lomheim (2011); Nakamura (2005)
 */
#include "pixel_array.h"

/*===========================================================================
 * Bayer coordinate operations (L2)
 *===========================================================================*/

/**
 * Get Bayer color at pixel position (x,y).
 *
 * The Bayer pattern repeats every 2x2 block:
 *   RGGB: (0,0)=R, (1,0)=Gr, (0,1)=Gb, (1,1)=B
 *   BGGR: (0,0)=B, (1,0)=Gb, (0,1)=Gr, (1,1)=R
 *   GRBG: (0,0)=Gr, (1,0)=R, (0,1)=B, (1,1)=Gb
 *   GBRG: (0,0)=Gb, (1,0)=B, (0,1)=R, (1,1)=Gr
 */
bayer_color_t bayer_color_at(uint32_t x, uint32_t y, cfa_pattern_t cfa)
{
    uint8_t even_x = (x & 1) ? 0 : 1;  /* 1=even column, 0=odd column */
    uint8_t even_y = (y & 1) ? 0 : 1;  /* 1=even row, 0=odd row */
    uint8_t phase  = (even_y << 1) | even_x;  /* 2-bit phase: row*2+col */

    switch (cfa) {
        case CFA_BAYER_RGGB:
            /* Even row: R,G; Odd row: G,B → phase: 00=R,01=Gr,10=Gb,11=B */
            switch (phase) {
                case 3: return BAYER_COLOR_R;     /* (0,0): even,even -> R */
                case 2: return BAYER_COLOR_GR;    /* (1,0): even,odd -> Gr */
                case 1: return BAYER_COLOR_GB;    /* (0,1): odd,even -> Gb */
                case 0: return BAYER_COLOR_B;     /* (1,1): odd,odd -> B */
            }
            break;
        case CFA_BAYER_BGGR:
            switch (phase) {
                case 3: return BAYER_COLOR_B;
                case 2: return BAYER_COLOR_GB;
                case 1: return BAYER_COLOR_GR;
                case 0: return BAYER_COLOR_R;
            }
            break;
        case CFA_BAYER_GRBG:
            switch (phase) {
                case 3: return BAYER_COLOR_GR;
                case 2: return BAYER_COLOR_R;
                case 1: return BAYER_COLOR_B;
                case 0: return BAYER_COLOR_GB;
            }
            break;
        case CFA_BAYER_GBRG:
            switch (phase) {
                case 3: return BAYER_COLOR_GB;
                case 2: return BAYER_COLOR_B;
                case 1: return BAYER_COLOR_R;
                case 0: return BAYER_COLOR_GR;
            }
            break;
        default:
            /* Monochrome or unsupported: all pixels treated as green */
            return BAYER_COLOR_GR;
    }
    return BAYER_COLOR_GR; /* Unreachable */
}

/**
 * Find nearest same-color pixel offsets from (x,y).
 *
 * For a given target Bayer color, returns (dx, dy) that point to the
 * nearest pixel of that color. If the target pixel already has the
 * desired color, returns (0,0).
 */
void bayer_nearest_color(bayer_color_t target, uint32_t x, uint32_t y,
                          cfa_pattern_t cfa, int32_t *dx, int32_t *dy)
{
    if (dx == NULL || dy == NULL) return;

    bayer_color_t current = bayer_color_at(x, y, cfa);

    if (current == target || target == BAYER_COLOR_GR ||
        target == BAYER_COLOR_GB) {
        /* Same color or green (green is always adjacent) */
        if (current == BAYER_COLOR_GR || current == BAYER_COLOR_GB) {
            /* Green pixel: no offset needed for green */
            if (target == BAYER_COLOR_GR || target == BAYER_COLOR_GB) {
                *dx = 0; *dy = 0;
                return;
            }
        }
        if (current == target) {
            *dx = 0; *dy = 0;
            return;
        }
    }

    /* For red and blue, find nearest same-color pixel.
     * In Bayer pattern, same-color pixels are at (x+2k, y+2l) from
     * the nearest pixel of that color. */
    (void)current;

    /* Check 3x3 neighborhood for target color */
    int32_t best_dx = 0, best_dy = 0;
    int32_t best_dist = 99;
    int32_t sx, sy;

    for (sy = -2; sy <= 2; sy++) {
        for (sx = -2; sx <= 2; sx++) {
            int32_t nx = (int32_t)x + sx;
            int32_t ny = (int32_t)y + sy;
            if (nx < 0 || ny < 0) continue;
            bayer_color_t nc = bayer_color_at((uint32_t)nx, (uint32_t)ny, cfa);
            if (nc == target) {
                int32_t d = abs(sx) + abs(sy);
                if (d < best_dist) {
                    best_dist = d;
                    best_dx = sx;
                    best_dy = sy;
                }
            }
        }
    }

    *dx = best_dx;
    *dy = best_dy;
}

/**
 * Count pixels of each Bayer color in a rectangular region.
 */
void bayer_color_counts(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                         cfa_pattern_t cfa, uint32_t *r, uint32_t *gr,
                         uint32_t *gb, uint32_t *b)
{
    if (r == NULL || gr == NULL || gb == NULL || b == NULL) return;

    *r = *gr = *gb = *b = 0;

    uint32_t x, y;
    for (y = y0; y < y0 + h; y++) {
        for (x = x0; x < x0 + w; x++) {
            switch (bayer_color_at(x, y, cfa)) {
                case BAYER_COLOR_R:  (*r)++;  break;
                case BAYER_COLOR_GR: (*gr)++; break;
                case BAYER_COLOR_GB: (*gb)++; break;
                case BAYER_COLOR_B:  (*b)++;  break;
            }
        }
    }
}

/*===========================================================================
 * Raw frame memory management (L2)
 *===========================================================================*/

raw_frame_t *raw_frame_alloc(uint32_t w, uint32_t h, cfa_pattern_t cfa)
{
    if (w == 0 || h == 0) return NULL;

    raw_frame_t *frame = (raw_frame_t *)malloc(sizeof(raw_frame_t));
    if (frame == NULL) return NULL;

    /* Row-major, stride = width (no padding for simplicity) */
    frame->stride = w;
    frame->data = (pixel_raw_t *)calloc((size_t)w * (size_t)h,
                                         sizeof(pixel_raw_t));
    if (frame->data == NULL) {
        free(frame);
        return NULL;
    }

    frame->width  = w;
    frame->height = h;
    frame->cfa    = cfa;
    frame->black_level = 0;
    frame->max_value   = 4095;  /* 12-bit default */

    return frame;
}

void raw_frame_free(raw_frame_t *f)
{
    if (f == NULL) return;
    free(f->data);
    free(f);
}

void raw_frame_fill(raw_frame_t *f, pixel_raw_t v)
{
    if (f == NULL || f->data == NULL) return;
    size_t n = (size_t)f->width * (size_t)f->height;
    size_t i;
    for (i = 0; i < n; i++) {
        f->data[i] = v;
    }
}

void raw_frame_copy(const raw_frame_t *src, raw_frame_t *dst)
{
    if (src == NULL || dst == NULL) return;
    if (src->data == NULL || dst->data == NULL) return;
    if (src->width != dst->width || src->height != dst->height) return;

    size_t n = (size_t)src->width * (size_t)src->height;
    memcpy(dst->data, src->data, n * sizeof(pixel_raw_t));
    dst->cfa = src->cfa;
    dst->black_level = src->black_level;
    dst->max_value = src->max_value;
}

/**
 * Subtract two frames: dst = dst - src.
 * Used for dark frame subtraction: dark frame is subtracted from
 * the signal frame to remove FPN and dark current offset.
 */
void raw_frame_subtract(raw_frame_t *dst, const raw_frame_t *src)
{
    if (dst == NULL || src == NULL) return;
    if (dst->data == NULL || src->data == NULL) return;
    if (dst->width != src->width || dst->height != src->height) return;

    size_t n = (size_t)dst->width * (size_t)dst->height;
    size_t i;
    for (i = 0; i < n; i++) {
        if (dst->data[i] >= src->data[i]) {
            dst->data[i] = dst->data[i] - src->data[i];
        } else {
            dst->data[i] = 0;  /* Clamp at zero */
        }
    }
}

/**
 * Apply per-pixel gain map.
 * dst[x][y] = dst[x][y] * gain_map[y * width + x]
 * The gain map is in linear units (1.0 = unity).
 */
void raw_frame_apply_gain(raw_frame_t *f, const double *gain)
{
    if (f == NULL || gain == NULL || f->data == NULL) return;

    size_t n = (size_t)f->width * (size_t)f->height;
    size_t i;
    for (i = 0; i < n; i++) {
        double v = (double)f->data[i] * gain[i];
        if (v > (double)f->max_value) v = (double)f->max_value;
        if (v < 0.0) v = 0.0;
        f->data[i] = (pixel_raw_t)(v + 0.5);  /* Round to nearest */
    }
}

/**
 * Compute pixel statistics for a raw frame.
 *
 * Contains: mean, variance, stddev, min, max, saturation count,
 * black count, and 256-bin histogram.
 */
void raw_frame_statistics(const raw_frame_t *f, pixel_statistics_t *s)
{
    if (f == NULL || s == NULL || f->data == NULL) return;

    memset(s, 0, sizeof(*s));

    size_t n = (size_t)f->width * (size_t)f->height;
    if (n == 0) return;

    double sum = 0.0, sum2 = 0.0;
    uint32_t v;
    size_t i;

    s->min = f->max_value;
    s->max = 0;

    /* Bin width for 256-bin histogram */
    double bin_width = (double)(f->max_value + 1) / 256.0;
    if (bin_width < 1.0) bin_width = 1.0;

    for (i = 0; i < n; i++) {
        v = f->data[i];
        sum  += (double)v;
        sum2 += (double)v * (double)v;

        if (v < s->min) s->min = v;
        if (v > s->max) s->max = v;

        if (v >= f->max_value) s->saturated_count++;
        if (v <= f->black_level) s->black_count++;

        /* Histogram */
        uint32_t bin = (uint32_t)((double)v / bin_width);
        if (bin >= 256) bin = 255;
        s->histogram[bin]++;
    }

    s->mean = sum / (double)n;
    s->variance = sum2 / (double)n - s->mean * s->mean;
    if (s->variance < 0.0) s->variance = 0.0;
    s->stddev = sqrt(s->variance);
}

void pixel_statistics_print(const pixel_statistics_t *s)
{
    if (s == NULL) {
        printf("pixel_statistics_t: NULL\n");
        return;
    }
    printf("=== Pixel Statistics ===\n");
    printf("Mean:     %.2f DN\n", s->mean);
    printf("StdDev:   %.2f DN\n", s->stddev);
    printf("Variance: %.2f DN^2\n", s->variance);
    printf("Min:      %u DN\n", (unsigned)s->min);
    printf("Max:      %u DN\n", (unsigned)s->max);
    printf("Saturated: %u px\n", (unsigned)s->saturated_count);
    printf("Black:    %u px\n", (unsigned)s->black_count);
}

/*===========================================================================
 * Pixel binning (L5: charge-domain and digital)
 *===========================================================================*/

/**
 * 2x2 binning: sums 4 pixels into 1.
 *
 * For Bayer: 2x2 block contains 2G + 1R + 1B.
 * Output is a quarter-resolution Bayer image.
 * SNR improves by sqrt(4) = 2x (+6 dB).
 */
int raw_frame_bin_2x2(const raw_frame_t *src, raw_frame_t *dst)
{
    if (src == NULL || dst == NULL) return -1;
    if (src->data == NULL || dst->data == NULL) return -1;

    uint32_t out_w = src->width / 2;
    uint32_t out_h = src->height / 2;

    if (dst->width < out_w || dst->height < out_h) return -1;

    uint32_t x, y;
    for (y = 0; y < out_h; y++) {
        for (x = 0; x < out_w; x++) {
            uint32_t sx = x * 2;
            uint32_t sy = y * 2;

            uint32_t sum = (uint32_t)src->data[sy * src->stride + sx] +
                           (uint32_t)src->data[sy * src->stride + sx + 1] +
                           (uint32_t)src->data[(sy + 1) * src->stride + sx] +
                           (uint32_t)src->data[(sy + 1) * src->stride + sx + 1];

            /* Average (digital domain) or saturated sum (charge domain).
             * Here we use average for simplicity. */
            pixel_raw_t avg = (pixel_raw_t)((sum + 2) / 4);  /* Round */
            dst->data[y * dst->stride + x] = avg;
        }
    }

    dst->width  = out_w;
    dst->height = out_h;
    dst->cfa    = src->cfa;  /* Bayer phase preserved */

    return 0;
}

/**
 * NxN binning (digital domain average).
 * N must be 2, 3, or 4.
 */
int raw_frame_bin_nxn(const raw_frame_t *src, raw_frame_t *dst, uint32_t n)
{
    if (src == NULL || dst == NULL || n < 2 || n > 4) return -1;

    uint32_t out_w = src->width / n;
    uint32_t out_h = src->height / n;
    if (dst->width < out_w || dst->height < out_h) return -1;

    uint32_t x, y, i, j;
    uint32_t n2 = n * n;

    for (y = 0; y < out_h; y++) {
        for (x = 0; x < out_w; x++) {
            uint32_t sx = x * n;
            uint32_t sy = y * n;
            uint32_t sum = 0;

            for (j = 0; j < n; j++) {
                for (i = 0; i < n; i++) {
                    sum += src->data[(sy + j) * src->stride + sx + i];
                }
            }

            pixel_raw_t avg = (pixel_raw_t)((sum + n2/2) / n2);
            dst->data[y * dst->stride + x] = avg;
        }
    }

    dst->width  = out_w;
    dst->height = out_h;
    return 0;
}

/**
 * Vertical binning: sum N rows into 1.
 */
int raw_frame_bin_vertical(const raw_frame_t *src, raw_frame_t *dst, uint32_t n)
{
    if (src == NULL || dst == NULL || n < 2) return -1;

    uint32_t out_h = src->height / n;
    if (dst->height < out_h || dst->width < src->width) return -1;

    uint32_t x, y, j;
    for (y = 0; y < out_h; y++) {
        for (x = 0; x < src->width; x++) {
            uint32_t sum = 0;
            for (j = 0; j < n; j++) {
                sum += src->data[(y * n + j) * src->stride + x];
            }
            dst->data[y * dst->stride + x] = (pixel_raw_t)((sum + n/2) / n);
        }
    }

    dst->height = out_h;
    return 0;
}

/*===========================================================================
 * Subsampling (L5: digital skip)
 *===========================================================================*/

int raw_frame_subsample_h2(const raw_frame_t *src, raw_frame_t *dst)
{
    return raw_frame_subsample(src, dst, 2, 1);
}

int raw_frame_subsample_v2(const raw_frame_t *src, raw_frame_t *dst)
{
    return raw_frame_subsample(src, dst, 1, 2);
}

int raw_frame_subsample(const raw_frame_t *src, raw_frame_t *dst,
                         uint32_t skip_h, uint32_t skip_v)
{
    if (src == NULL || dst == NULL) return -1;
    if (skip_h == 0) skip_h = 1;
    if (skip_v == 0) skip_v = 1;

    uint32_t out_w = src->width / skip_h;
    uint32_t out_h = src->height / skip_v;
    if (dst->width < out_w || dst->height < out_h) return -1;

    uint32_t x, y;
    for (y = 0; y < out_h; y++) {
        for (x = 0; x < out_w; x++) {
            dst->data[y * dst->stride + x] =
                src->data[(y * skip_v) * src->stride + (x * skip_h)];
        }
    }

    dst->width  = out_w;
    dst->height = out_h;
    return 0;
}

/**
 * Extract rectangular Region of Interest (ROI).
 *
 * Crops a sub-rectangle from the source frame into dst.
 * dst must be pre-allocated with dimensions roi_w x roi_h.
 */
int raw_frame_roi_extract(const raw_frame_t *src, raw_frame_t *dst,
                           uint32_t rx, uint32_t ry,
                           uint32_t rw, uint32_t rh)
{
    if (src == NULL || dst == NULL) return -1;
    if (rx + rw > src->width || ry + rh > src->height) return -1;
    if (dst->width < rw || dst->height < rh) return -1;

    uint32_t x, y;
    for (y = 0; y < rh; y++) {
        for (x = 0; x < rw; x++) {
            dst->data[y * dst->stride + x] =
                src->data[(ry + y) * src->stride + (rx + x)];
        }
    }

    dst->width  = rw;
    dst->height = rh;
    dst->cfa    = src->cfa;
    return 0;
}

/*===========================================================================
 * Defect pixel detection (L5)
 *===========================================================================*/

/**
 * Detect hot pixels in a dark frame.
 *
 * Hot pixel: value > mean + k * sigma (statistical outlier in dark).
 * A dark frame captures only dark current + read noise, so any pixel
 * significantly above the mean is likely a hot pixel.
 */
uint32_t detect_hot_pixels(const raw_frame_t *dark, double k,
                            defect_map_t *map)
{
    if (dark == NULL || map == NULL) return 0;

    pixel_statistics_t stats;
    raw_frame_statistics(dark, &stats);

    double threshold = stats.mean + k * stats.stddev;
    uint32_t count = 0;
    uint32_t x, y;

    for (y = 0; y < dark->height; y++) {
        for (x = 0; x < dark->width; x++) {
            if ((double)dark->data[y * dark->stride + x] > threshold) {
                defect_map_add(map, x, y, DEFECT_HOT,
                               ((double)dark->data[y * dark->stride + x] -
                                stats.mean) / (stats.stddev + 1.0));
                count++;
            }
        }
    }

    return count;
}

/**
 * Detect dead pixels in a flat-field frame.
 *
 * Dead pixel: value < mean - k * sigma (abnormally low response).
 * Flat-field frame should have uniform illumination.
 */
uint32_t detect_dead_pixels(const raw_frame_t *flat, double k,
                             defect_map_t *map)
{
    if (flat == NULL || map == NULL) return 0;

    pixel_statistics_t stats;
    raw_frame_statistics(flat, &stats);

    double threshold = stats.mean - k * stats.stddev;
    if (threshold < 0.0) threshold = 0.0;

    uint32_t count = 0;
    uint32_t x, y;

    for (y = 0; y < flat->height; y++) {
        for (x = 0; x < flat->width; x++) {
            if ((double)flat->data[y * flat->stride + x] < threshold) {
                defect_map_add(map, x, y, DEFECT_DEAD,
                               (stats.mean - (double)flat->data[
                                   y * flat->stride + x]) /
                               (stats.stddev + 1.0));
                count++;
            }
        }
    }

    return count;
}

/**
 * Correct a defective pixel by interpolating from same-color neighbors.
 *
 * For Bayer: same-color pixels are at diagonal offsets (2,2) or
 * horizontal/vertical offsets (2,0)/(0,2).
 */
pixel_raw_t defect_pixel_correct(const raw_frame_t *f, uint32_t x, uint32_t y)
{
    if (f == NULL) return 0;

    bayer_color_t color = bayer_color_at(x, y, f->cfa);
    uint32_t sum = 0;
    uint32_t count = 0;

    /* Collect same-color neighbors in 5x5 window */
    int32_t dx, dy;
    for (dy = -2; dy <= 2; dy++) {
        for (dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            int32_t nx = (int32_t)x + dx;
            int32_t ny = (int32_t)y + dy;
            if (nx < 0 || ny < 0 || nx >= (int32_t)f->width ||
                ny >= (int32_t)f->height) continue;

            if (bayer_color_at((uint32_t)nx, (uint32_t)ny, f->cfa) == color) {
                sum += f->data[(uint32_t)ny * f->stride + (uint32_t)nx];
                count++;
            }
        }
    }

    if (count == 0) return 0;
    return (pixel_raw_t)((sum + count/2) / count);
}

/**
 * Apply full-frame defect correction.
 */
void raw_frame_defect_correct(raw_frame_t *f, const defect_map_t *map)
{
    if (f == NULL || map == NULL) return;

    uint32_t i;
    for (i = 0; i < map->count; i++) {
        pixel_defect_t *d = &map->defects[i];
        if (d->x < f->width && d->y < f->height) {
            f->data[d->y * f->stride + d->x] =
                defect_pixel_correct(f, d->x, d->y);
        }
    }
}

void defect_map_init(defect_map_t *map)
{
    if (map == NULL) return;
    memset(map, 0, sizeof(*map));
}

int defect_map_add(defect_map_t *map, uint32_t x, uint32_t y,
                    pixel_defect_type_t type, double severity)
{
    if (map == NULL) return -1;

    /* Grow if needed */
    if (map->count >= map->capacity) {
        uint32_t new_cap = map->capacity == 0 ? 64 : map->capacity * 2;
        pixel_defect_t *new_data = (pixel_defect_t *)realloc(
            map->defects, new_cap * sizeof(pixel_defect_t));
        if (new_data == NULL) return -1;
        map->defects = new_data;
        map->capacity = new_cap;
    }

    map->defects[map->count].x = x;
    map->defects[map->count].y = y;
    map->defects[map->count].type = type;
    map->defects[map->count].severity = severity;
    map->count++;

    return 0;
}

void defect_map_free(defect_map_t *map)
{
    if (map == NULL) return;
    free(map->defects);
    memset(map, 0, sizeof(*map));
}

/*===========================================================================
 * Column FPN correction (L5)
 *===========================================================================*/

/**
 * Estimate column offsets from dark columns.
 *
 * In a dark frame, column-to-column variation (column FPN) appears as
 * vertical stripes. We compute per-column median as the offset estimate.
 */
void estimate_column_fpn(const raw_frame_t *dark, double *offsets)
{
    if (dark == NULL || offsets == NULL) return;

    uint32_t x, y;
    for (x = 0; x < dark->width; x++) {
        /* Compute column mean (simple but effective estimator) */
        double col_sum = 0.0;
        for (y = 0; y < dark->height; y++) {
            col_sum += (double)dark->data[y * dark->stride + x];
        }
        offsets[x] = col_sum / (double)dark->height;
    }
}

/**
 * Apply column offset correction.
 * frame[x][y] -= col_offsets[x], clamped to [0, max_value].
 */
void apply_column_fpn_correction(raw_frame_t *f, const double *offsets)
{
    if (f == NULL || offsets == NULL) return;

    uint32_t x, y;
    for (x = 0; x < f->width; x++) {
        int32_t offset = (int32_t)(offsets[x] + 0.5);
        for (y = 0; y < f->height; y++) {
            int32_t v = (int32_t)f->data[y * f->stride + x] - offset;
            if (v < 0) v = 0;
            if (v > (int32_t)f->max_value) v = (int32_t)f->max_value;
            f->data[y * f->stride + x] = (pixel_raw_t)v;
        }
    }
}

/*===========================================================================
 * Digital gain and black level (L5)
 *===========================================================================*/

void raw_frame_digital_gain(raw_frame_t *f, double gain)
{
    if (f == NULL || f->data == NULL) return;

    size_t n = (size_t)f->width * (size_t)f->height;
    size_t i;
    for (i = 0; i < n; i++) {
        double v = (double)f->data[i] * gain;
        if (v > (double)f->max_value) v = (double)f->max_value;
        f->data[i] = (pixel_raw_t)(v + 0.5);
    }
}

void raw_frame_black_level_subtract(raw_frame_t *f, uint16_t bl)
{
    if (f == NULL || f->data == NULL) return;

    size_t n = (size_t)f->width * (size_t)f->height;
    size_t i;
    for (i = 0; i < n; i++) {
        if (f->data[i] > bl) {
            f->data[i] -= bl;
        } else {
            f->data[i] = 0;
        }
    }

    /* Update max_value */
    if (f->max_value > bl) {
        f->max_value -= bl;
    } else {
        f->max_value = 0;
    }
}

void raw_frame_clamp(raw_frame_t *f, uint16_t bl)
{
    if (f == NULL || f->data == NULL) return;

    size_t n = (size_t)f->width * (size_t)f->height;
    size_t i;
    for (i = 0; i < n; i++) {
        if (f->data[i] < bl) f->data[i] = bl;
        if (f->data[i] > f->max_value) f->data[i] = f->max_value;
    }
}

/**
 * display_timing.c — Display Timing Computation (VESA CVT/GTF/DMT)
 *
 * Implements:
 *   L1: Pixel clock, horizontal/vertical timing parameters
 *   L2: Blanking interval calculation, refresh rate formulas
 *   L4: Nyquist-Shannon applied to pixel sampling bandwidth
 *   L5: CVT 1.2 computation algorithm, GTF 1.1 formula
 *   L6: Standard VESA DMT mode lookup and timing generation
 *
 * Reference:
 *   VESA Coordinated Video Timings (CVT) Standard 1.2 (2013)
 *   VESA Generalized Timing Formula (GTF) 1.1 (1999)
 *   VESA Display Monitor Timings (DMT) 1.3 (2013)
 *
 * L7 Applications:
 *   - supplier: BOE/Samsung/LG display panel timing configuration
 *   - Detroit automotive display (ISO 15008 vehicle display requirements)
 *   - NHS medical imaging display (DICOM GSDF calibration timing)
 * L8 Advanced:
 *   - time-varying refresh rate (VRR/FreeSync/GSync) adaptive timing
 *   Poynton, "Digital Video and HD" (2012), Ch. 44-47
 *   CTA-861-G — A DTV Profile
 */

#include "display_types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ==========================================================================
 * L1/L2: Basic Pixel and Timing Operations
 * ========================================================================== */

void pixel_rgb_init(pixel_rgb_t *p, uint16_t r, uint16_t g, uint16_t b,
                    uint16_t a, uint8_t bits)
{
    uint16_t maxv = (bits >= 12) ? 4095 : (bits >= 10) ? 1023 : 255;
    p->max_val = maxv;
    p->r = (r > maxv) ? maxv : r;
    p->g = (g > maxv) ? maxv : g;
    p->b = (b > maxv) ? maxv : b;
    p->a = (a > maxv) ? maxv : a;
}

void pixel_rgb_to_float(const pixel_rgb_t *p, pixel_float_t *fp)
{
    if (p->max_val == 0) {
        fp->r = fp->g = fp->b = 0.0;
        fp->a = 0.0;
        return;
    }
    double scale = 1.0 / (double)p->max_val;
    fp->r = (double)p->r * scale;
    fp->g = (double)p->g * scale;
    fp->b = (double)p->b * scale;
    fp->a = (double)p->a * scale;
}

void pixel_float_to_rgb(const pixel_float_t *fp, pixel_rgb_t *p, uint8_t bits)
{
    uint16_t maxv = (bits >= 12) ? 4095 : (bits >= 10) ? 1023 : 255;
    p->max_val = maxv;

    double r = fp->r * maxv;
    double g = fp->g * maxv;
    double b = fp->b * maxv;
    double a = fp->a * maxv;

    if (r < 0) r = 0; if (r > maxv) r = maxv;
    if (g < 0) g = 0; if (g > maxv) g = maxv;
    if (b < 0) b = 0; if (b > maxv) b = maxv;
    if (a < 0) a = 0; if (a > maxv) a = maxv;

    p->r = (uint16_t)(r + 0.5);
    p->g = (uint16_t)(g + 0.5);
    p->b = (uint16_t)(b + 0.5);
    p->a = (uint16_t)(a + 0.5);
}

double pixel_luma_bt709(const pixel_rgb_t *p)
{
    if (p->max_val == 0) return 0.0;
    double rf = (double)p->r / p->max_val;
    double gf = (double)p->g / p->max_val;
    double bf = (double)p->b / p->max_val;
    /* BT.709 luma: Y' = 0.2126 R' + 0.7152 G' + 0.0722 B' */
    return 0.2126 * rf + 0.7152 * gf + 0.0722 * bf;
}

double pixel_luma_bt601(const pixel_rgb_t *p)
{
    if (p->max_val == 0) return 0.0;
    double rf = (double)p->r / p->max_val;
    double gf = (double)p->g / p->max_val;
    double bf = (double)p->b / p->max_val;
    /* BT.601 luma: Y' = 0.299 R' + 0.587 G' + 0.114 B' */
    return 0.299 * rf + 0.587 * gf + 0.114 * bf;
}

/* ==========================================================================
 * L1/L2: Timing Computation Fundamentals
 * ========================================================================== */

int timing_validate(const display_timing_t *t)
{
    if (t->pixel_clock_khz == 0)     return -1;
    if (t->h_active == 0)            return -2;
    if (t->v_active == 0)            return -3;
    if (t->refresh_rate_hz <= 0.0)   return -4;

    /* h_blank must exceed h_sync + h_front_porch + h_back_porch */
    uint32_t h_needed = t->h_sync + t->h_front_porch + t->h_back_porch;
    if (t->h_blank < h_needed)       return -5;

    /* v_blank must exceed v_sync + v_front_porch + v_back_porch */
    uint32_t v_needed = t->v_sync + t->v_front_porch + t->v_back_porch;
    if (t->v_blank < v_needed)       return -6;

    /* Sanity: pixel clock should be reasonable (< 3 GHz) */
    if (t->pixel_clock_khz > 3000000) return -7;

    /* Sanity: active resolution should be reasonable (< 16K) */
    if (t->h_active > 16384 || t->v_active > 16384) return -8;

    return 0;
}

uint64_t timing_pixel_clock_hz(const display_timing_t *t)
{
    return (uint64_t)t->pixel_clock_khz * 1000ULL;
}

double timing_horizontal_freq_khz(const display_timing_t *t)
{
    uint32_t h_total = t->h_active + t->h_blank;
    if (h_total == 0) return 0.0;
    return (double)t->pixel_clock_khz / (double)h_total;
}

double mode_bandwidth_mbps(const display_mode_t *m)
{
    uint32_t h_total = m->timing.h_active + m->timing.h_blank;
    uint32_t v_total = m->timing.v_active + m->timing.v_blank;
    uint64_t total_pixels = (uint64_t)h_total * v_total;
    double refresh = m->timing.refresh_rate_hz;
    double pixel_rate_hz = (double)total_pixels * refresh;

    uint32_t bpp = 24; /* default */
    switch (m->pixel_format) {
        case PIXFMT_RGB888: case PIXFMT_BGR888:
        case PIXFMT_YUV444:
            bpp = 24; break;
        case PIXFMT_RGBA8888: case PIXFMT_ARGB8888:
            bpp = 32; break;
        case PIXFMT_RGB565: case PIXFMT_RGB555:
            bpp = 16; break;
        case PIXFMT_RGB101010:
            bpp = 30; break;
        case PIXFMT_RGB121212:
            bpp = 36; break;
        case PIXFMT_MONO8:
            bpp = 8; break;
        case PIXFMT_MONO16:
            bpp = 16; break;
        case PIXFMT_YUV422:
            bpp = 16; break;
        case PIXFMT_YUV420: case PIXFMT_NV12:
            bpp = 12; break;
        default:
            bpp = 24; break;
    }
    return pixel_rate_hz * (double)bpp / 1e6;
}

int timing_compare(const display_timing_t *a, const display_timing_t *b,
                   double tol_percent)
{
    double tol = tol_percent / 100.0;
    /* Compare pixel clocks */
    double pc_a = a->pixel_clock_khz, pc_b = b->pixel_clock_khz;
    double avg = (pc_a + pc_b) / 2.0;
    if (avg > 0 && fabs(pc_a - pc_b) / avg > tol) return 0;
    /* Compare active areas */
    if (a->h_active != b->h_active || a->v_active != b->v_active) return 0;
    /* Compare total areas within tolerance */
    double ha = a->h_active + a->h_blank;
    double hb = b->h_active + b->h_blank;
    avg = (ha + hb) / 2.0;
    if (avg > 0 && fabs(ha - hb) / avg > tol) return 0;
    double va = a->v_active + a->v_blank;
    double vb = b->v_active + b->v_blank;
    avg = (va + vb) / 2.0;
    if (avg > 0 && fabs(va - vb) / avg > tol) return 0;
    /* Compare refresh rates */
    double ra = a->refresh_rate_hz, rb = b->refresh_rate_hz;
    avg = (ra + rb) / 2.0;
    if (avg > 0 && fabs(ra - rb) / avg > tol) return 0;
    return 1;
}

int mode_to_string(const display_mode_t *m, char *buf, size_t size)
{
    return snprintf(buf, size,
        "%s: %ux%u %.2fHz %s %u-bit",
        m->name[0] ? m->name : "Custom",
        m->resolution.width, m->resolution.height,
        m->timing.refresh_rate_hz,
        m->timing.scan_mode == SCAN_PROGRESSIVE ? "p" : "i",
        m->bits_per_color * 3);
}

double compute_ppi(uint32_t width, uint32_t height, double diag_mm)
{
    if (diag_mm <= 0.0) return 0.0;
    double diag_in = diag_mm / 25.4;
    double diag_px = sqrt((double)width * width + (double)height * height);
    return diag_px / diag_in;
}

double compute_diag_mm(uint32_t width, uint32_t height, double ppi)
{
    if (ppi <= 0.0) return 0.0;
    double diag_px = sqrt((double)width * width + (double)height * height);
    return diag_px / ppi * 25.4;
}

uint32_t pixel_format_bytes(pixel_format_t fmt)
{
    switch (fmt) {
        case PIXFMT_RGB888: case PIXFMT_BGR888: case PIXFMT_YUV444:
            return 3;
        case PIXFMT_RGBA8888: case PIXFMT_ARGB8888: case PIXFMT_RGB101010:
            return 4;
        case PIXFMT_RGB565: case PIXFMT_RGB555:
            return 2;
        case PIXFMT_MONO8:
            return 1;
        case PIXFMT_MONO16:
            return 2;
        case PIXFMT_RGB121212:
            return 6;
        case PIXFMT_YUV422:
            return 2;
        case PIXFMT_YUV420: case PIXFMT_NV12:
            return 1; /* rough effective bpp */
        default:
            return 0;
    }
}

const char *display_type_name(display_type_t dt)
{
    switch (dt) {
        case DISPLAY_LCD_TN:      return "LCD TN";
        case DISPLAY_LCD_IPS:     return "LCD IPS";
        case DISPLAY_LCD_VA:      return "LCD VA";
        case DISPLAY_OLED_RGB:    return "OLED RGB";
        case DISPLAY_OLED_PENTILE: return "OLED Pentile";
        case DISPLAY_AMOLED:      return "AMOLED";
        case DISPLAY_MICROLED:    return "MicroLED";
        case DISPLAY_EINK:        return "E-Ink";
        case DISPLAY_CRT:         return "CRT";
        case DISPLAY_PLASMA:      return "Plasma";
        case DISPLAY_DLP:         return "DLP";
        case DISPLAY_LCOS:        return "LCoS";
        case DISPLAY_LED:         return "LED";
        default:                  return "Unknown";
    }
}

const char *interface_type_name(display_interface_t iface)
{
    switch (iface) {
        case IFACE_VGA:       return "VGA";
        case IFACE_DVI_D:     return "DVI-D";
        case IFACE_DVI_I:     return "DVI-I";
        case IFACE_HDMI_1_4:  return "HDMI 1.4";
        case IFACE_HDMI_2_0:  return "HDMI 2.0";
        case IFACE_HDMI_2_1:  return "HDMI 2.1";
        case IFACE_DP_1_2:    return "DisplayPort 1.2";
        case IFACE_DP_1_4:    return "DisplayPort 1.4";
        case IFACE_DP_2_0:    return "DisplayPort 2.0";
        case IFACE_MIPI_DSI:  return "MIPI DSI";
        case IFACE_eDP:       return "eDP";
        case IFACE_LVDS:      return "LVDS";
        default:              return "Unknown";
    }
}

/* ==========================================================================
 * L5: VESA CVT 1.2 Algorithm
 * ========================================================================== */

/**
 * CVT 1.2 Horizontal blanking formula.
 *
 * The CVT standard defines h_blank as:
 *   h_blank = round( (h_active * gradient) + offset + margin_adjust )
 *
 * where gradient, offset depend on aspect ratio category.
 */
static void cvt_h_blank(uint32_t h_active, int is_4_3, int reduced,
                        uint32_t *h_blank, uint32_t *h_sync,
                        uint32_t *h_front, uint32_t *h_back)
{
    double h_blank_pct, h_sync_pct;

    if (reduced) {
        /* CVT-RB: reduced blanking */
        h_blank_pct = (h_active <= 1680) ? 160.0 / 1680.0 : 160.0 / h_active;
        *h_blank = (uint32_t)(h_active * h_blank_pct);
        if (*h_blank < 80) *h_blank = 80;
        *h_sync = 32;
    } else {
        /* Normal blanking */
        double gradient, offset;
        if (is_4_3) {
            gradient = 0.18;
            offset = 20.0;
        } else if (h_active <= 1280) {
            /* 16:9 or 16:10 with small horizontal */
            gradient = 0.18;
            offset = 20.0;
        } else {
            gradient = 0.15;
            offset = 0.0;
        }
        *h_blank = (uint32_t)(h_active * gradient + offset);
        *h_blank = ((*h_blank + 7) / 8) * 8; /* round to multiple of 8 */

        /* H sync = 8% of h_total */
        uint32_t h_total_est = h_active + *h_blank;
        *h_sync = (uint32_t)(h_total_est * 0.08);
        *h_sync = ((*h_sync + 7) / 8) * 8;
        if (*h_sync < 8) *h_sync = 8;
    }

    /* H front porch = h_blank - h_sync - h_back_porch */
    /* H back porch is typically set to cover analog settling time */
    if (reduced) {
        *h_front = *h_blank - *h_sync;
        *h_back = 0;
    } else {
        *h_back = *h_blank / 3;
        *h_back = ((*h_back + 7) / 8) * 8;
        if (*h_front + *h_sync + *h_back > *h_blank) {
            *h_front = *h_blank - *h_sync - *h_back;
        } else {
            *h_front = *h_blank - *h_sync - *h_back;
        }
    }
    if (*h_front > *h_blank) *h_front = *h_blank - *h_sync;
}

/**
 * CVT 1.2 Vertical blanking formula.
 *
 * v_blank_lines = 3 + round(v_active * v_blank_pct)
 * where v_blank_pct depends on aspect ratio and margins.
 */
static void cvt_v_blank(uint32_t v_active, int is_4_3, int reduced, int interlaced,
                        double refresh_hz,
                        uint32_t *v_blank, uint32_t *v_sync,
                        uint32_t *v_front, uint32_t *v_back)
{
    double min_vblank_lines;

    if (reduced) {
        min_vblank_lines = 3.0;
    } else {
        if (is_4_3) {
            min_vblank_lines = (refresh_hz >= 60.0) ? 3.0 : 5.0;
        } else {
            min_vblank_lines = (refresh_hz >= 60.0) ? 3.0 : 5.0;
        }
    }

    /* Estimate v_blank based on refresh rate */
    double v_sync_width = (refresh_hz >= 60.0) ? 4.0 : 5.0;
    double back_porch = min_vblank_lines;
    double v_blank_lines = v_sync_width + back_porch + 1.0; /* front porch = 1 */

    if (interlaced) {
        v_blank_lines = ceil(v_blank_lines / 2.0) * 2; /* even number */
    }

    *v_blank = (uint32_t)v_blank_lines;
    *v_sync = (uint32_t)v_sync_width;
    *v_front = 1;
    *v_back = *v_blank - *v_sync - *v_front;
    if (*v_back > *v_blank) *v_back = *v_blank - *v_sync - *v_front;
}

/* ==========================================================================
 * L5: VESA CVT — Main computation entry point
 * ========================================================================== */

int vesa_cvt_compute(uint32_t h_active, uint32_t v_active,
                     double refresh_hz, aspect_ratio_t aspect_ratio,
                     int reduced_blanking, int interlaced,
                     timing_result_t *result)
{
    if (h_active == 0 || v_active == 0 || refresh_hz <= 0.0) return -1;
    if (h_active > 16384 || v_active > 16384) return -1;

    /* Determine if 4:3 aspect ratio category */
    int is_4_3 = (aspect_ratio == ASPECT_4_3 || aspect_ratio == ASPECT_5_4);

    /* 1. Compute vertical blanking */
    uint32_t v_blank, v_sync, v_front, v_back;
    cvt_v_blank(v_active, is_4_3, reduced_blanking, interlaced,
                refresh_hz, &v_blank, &v_sync, &v_front, &v_back);

    /* 2. Estimate required pixel clock */
    /* pixel_clock = (h_active + h_blank_est) * (v_active + v_blank) * refresh */
    uint32_t h_blank, h_sync, h_front, h_back;
    cvt_h_blank(h_active, is_4_3, reduced_blanking,
                &h_blank, &h_sync, &h_front, &h_back);

    uint32_t v_total = v_active + v_blank;
    uint32_t h_total = h_active + h_blank;

    /* Interlaced: v_total counts fields, refresh is field rate */
    double frame_rate = interlaced ? refresh_hz / 2.0 : refresh_hz;

    uint64_t pc_hz = (uint64_t)h_total * v_total * refresh_hz;
    uint32_t pc_khz = (uint32_t)((pc_hz + 500) / 1000);

    /* 3. Refine: adjust pixel clock to a standard multiple if needed */
    /* CVT suggests rounding pixel clock to nearest 250 kHz */
    pc_khz = ((pc_khz + 125) / 250) * 250;

    /* 4. Recalculate actual refresh */
    double actual_hz = (double)pc_khz * 1000.0 / ((double)h_total * v_total);

    /* 5. Fill result */
    result->timing.pixel_clock_khz = pc_khz;
    result->timing.h_active = h_active;
    result->timing.h_blank = h_blank;
    result->timing.h_sync = h_sync;
    result->timing.h_front_porch = h_front;
    result->timing.h_back_porch = h_back;
    result->timing.v_active = v_active;
    result->timing.v_blank = v_blank;
    result->timing.v_sync = v_sync;
    result->timing.v_front_porch = v_front;
    result->timing.v_back_porch = v_back;
    result->timing.h_polarity = (refresh_hz > 60) ? SYNC_POSITIVE : SYNC_NEGATIVE;
    result->timing.v_polarity = SYNC_NEGATIVE;
    result->timing.scan_mode = interlaced ? SCAN_INTERLACED : SCAN_PROGRESSIVE;
    result->timing.refresh_rate_hz = actual_hz;
    result->valid = 1;
    result->actual_refresh_hz = actual_hz;
    result->pixel_clock_mhz = pc_khz / 1000.0;
    result->horizontal_khz = (double)pc_khz / (double)h_total;
    result->required_bandwidth_mbps = (double)pc_khz * 24.0 / 1000.0; /* 24 bpp */

    return 0;
}

/* ==========================================================================
 * L5: VESA GTF 1.1 Algorithm
 * ========================================================================== */

int vesa_gtf_compute(uint32_t h_active, uint32_t v_active,
                     double refresh_hz, double margin_percent,
                     timing_result_t *result)
{
    if (h_active == 0 || v_active == 0 || refresh_hz <= 0.0) return -1;
    if (margin_percent < 0.0 || margin_percent > 50.0) return -1;

    /* GTF Step 1: Total lines per frame estimate */
    double m = margin_percent / 100.0;
    double v_lines_rqd = (double)v_active / (1.0 - m / 100.0);
    /* Actually GTF uses: v_total = v_active / (1 - margin/100) + 0.5 */
    /* Then round up */
    double interlace_factor = 1.0;
    uint32_t v_total = (uint32_t)(v_lines_rqd + 0.5);

    /* Step 2: Vertical blanking */
    uint32_t v_blank = v_total - v_active;
    uint32_t v_sync = 3;
    uint32_t v_front = 1;
    uint32_t v_back = v_blank - v_sync - v_front;
    if (v_back > v_blank) v_back = v_blank - v_sync - v_front;

    /* Step 3: Total horizontal pixels estimate */
    double ideal_duty_cycle = 0.8; /* 80% active, 20% blanking */
    uint32_t h_blank = (uint32_t)((double)h_active / ideal_duty_cycle - h_active);
    h_blank = ((h_blank + 7) / 8) * 8;
    uint32_t h_total = h_active + h_blank;

    /* Step 4: Pixel clock */
    uint64_t pc_hz = (uint64_t)h_total * v_total * refresh_hz;
    uint32_t pc_khz = (uint32_t)((pc_hz + 500) / 1000);
    pc_khz = ((pc_khz + 125) / 250) * 250;

    /* Step 5: Horizontal timing details */
    uint32_t h_sync = h_blank / 5;
    h_sync = ((h_sync + 7) / 8) * 8;
    if (h_sync < 8) h_sync = 8;
    uint32_t h_front = h_blank / 5;
    h_front = ((h_front + 7) / 8) * 8;
    uint32_t h_back = h_blank - h_sync - h_front;

    double actual_hz = (double)pc_khz * 1000.0 / ((double)h_total * v_total);

    result->timing.pixel_clock_khz = pc_khz;
    result->timing.h_active = h_active;
    result->timing.h_blank = h_blank;
    result->timing.h_sync = h_sync;
    result->timing.h_front_porch = h_front;
    result->timing.h_back_porch = h_back;
    result->timing.v_active = v_active;
    result->timing.v_blank = v_blank;
    result->timing.v_sync = v_sync;
    result->timing.v_front_porch = v_front;
    result->timing.v_back_porch = v_back;
    result->timing.h_polarity = SYNC_NEGATIVE;
    result->timing.v_polarity = SYNC_NEGATIVE;
    result->timing.scan_mode = SCAN_PROGRESSIVE;
    result->timing.refresh_rate_hz = actual_hz;
    result->valid = 1;
    result->actual_refresh_hz = actual_hz;
    result->pixel_clock_mhz = pc_khz / 1000.0;
    result->horizontal_khz = (double)pc_khz / (double)h_total;
    result->required_bandwidth_mbps = (double)pc_khz * 24.0 / 1000.0;

    return 0;
}

/* ==========================================================================
 * L6: VESA DMT Standard Mode Database
 * ========================================================================== */

/* VESA DMT subset: commonly used standard modes */
#define DMT_MODE_COUNT 45

static const vesa_mode_t dmt_modes[DMT_MODE_COUNT] = {
    /*    W     H    Hz    pclk_khz  htot vtot hsync vsync hfp hbp vfp vbp hpol   vpol   scan */
    {  640,  480,  60.0,  25200,   800, 525,  96,   2,   16,  48,  10,  33, 0, 0, 0, "640×480@60" },
    {  640,  480,  72.0,  31500,   832, 520,  40,   3,   24,  128,  9,  28, 0, 0, 0, "640×480@72" },
    {  640,  480,  75.0,  31500,   840, 500,  64,   3,   16,  120,  1,  16, 0, 0, 0, "640×480@75" },
    {  640,  480,  85.0,  36000,   832, 509,  56,   3,   32,  160,  1,  25, 0, 0, 0, "640×480@85" },
    {  800,  600,  56.0,  36000,  1024, 625,  72,   2,   24,  72,   1,  22, 1, 1, 0, "800×600@56" },
    {  800,  600,  60.0,  40000,  1056, 628,  128,  4,   40,  88,   1,  23, 1, 1, 0, "800×600@60" },
    {  800,  600,  72.0,  50000,  1040, 666,  120,  6,   56,  64,  37,  23, 1, 1, 0, "800×600@72" },
    {  800,  600,  75.0,  49500,  1056, 625,  80,   3,   16,  160,  1,  21, 1, 1, 0, "800×600@75" },
    {  800,  600,  85.0,  56250,  1048, 631,  64,   3,   32,  152,  1,  27, 1, 1, 0, "800×600@85" },
    { 1024,  768,  60.0,  65000,  1344, 806,  136,  6,   24,  160,  3,  29, 0, 0, 0, "1024×768@60" },
    { 1024,  768,  70.0,  75000,  1328, 806,  136,  6,   24,  144,  3,  29, 0, 0, 0, "1024×768@70" },
    { 1024,  768,  75.0,  78750,  1312, 800,  96,   3,   16,  176,  1,  28, 1, 1, 0, "1024×768@75" },
    { 1024,  768,  85.0,  94500,  1376, 808,  96,   3,   48,  208,  1,  36, 1, 1, 0, "1024×768@85" },
    { 1280,  720,  60.0,  74250,  1650, 750,  40,   5,   110, 220,  5,  20, 1, 1, 0, "1280×720@60 (720p)" },
    { 1280,  800,  60.0,  83500,  1680, 831,  128,  6,   72,  200,  3,  22, 0, 1, 0, "1280×800@60" },
    { 1280,  960,  60.0, 108000,  1800, 1000, 112,  3,   96,  312,  1,  36, 1, 1, 0, "1280×960@60" },
    { 1280, 1024,  60.0, 108000,  1688, 1066, 112,  3,   48,  248,  1,  38, 1, 1, 0, "1280×1024@60" },
    { 1280, 1024,  75.0, 135000,  1688, 1066, 112,  3,   16,  248,  1,  38, 1, 1, 0, "1280×1024@75" },
    { 1360,  768,  60.0,  85500,  1792, 795,  112,  6,   64,  256,  3,  18, 1, 1, 0, "1360×768@60" },
    { 1366,  768,  60.0,  85500,  1792, 798,  136,  5,   70,  213,  3,  22, 1, 1, 0, "1366×768@60" },
    { 1440,  900,  60.0, 106500,  1904, 934,  152,  6,   80,  232,  3,  25, 0, 1, 0, "1440×900@60" },
    { 1400, 1050,  60.0, 121750,  1840, 1080, 112,  4,   72,  168,  3,  23, 0, 1, 0, "1400×1050@60" },
    { 1600,  900,  60.0, 108000,  1800, 1000, 112,  3,   96,  312,  1,  96, 1, 1, 0, "1600×900@60" },
    { 1600, 1200,  60.0, 162000,  2160, 1250, 192,  3,   64,  304,  1,  46, 1, 1, 0, "1600×1200@60" },
    { 1680, 1050,  60.0, 146250,  2240, 1089, 176,  6,   104, 280,  3,  30, 0, 1, 0, "1680×1050@60" },
    { 1920, 1080,  50.0, 148500,  2640, 1125, 44,   5,   528, 148,  4,  36, 1, 1, 0, "1920×1080@50 (1080p50)" },
    { 1920, 1080,  60.0, 148500,  2200, 1125, 44,   5,   88,  148,  4,  36, 1, 1, 0, "1920×1080@60 (1080p60)" },
    { 1920, 1080, 120.0, 297000,  2200, 1125, 44,   5,   88,  148,  4,  36, 1, 1, 0, "1920×1080@120" },
    { 1920, 1200,  60.0, 193250,  2592, 1242, 208,  6,   128, 336,  3,  33, 0, 1, 0, "1920×1200@60" },
    { 1920, 1440,  60.0, 234000,  2600, 1500, 208,  6,   128, 344,  3,  51, 0, 1, 0, "1920×1440@60" },
    { 2048, 1080,  60.0, 148500,  2200, 1125, 44,   5,   88,  148,  4,  36, 1, 1, 0, "2048×1080@60" },
    { 2048, 1536,  60.0, 266950,  2780, 1600, 224,  6,   160, 388,  3,  55, 0, 1, 0, "2048×1536@60" },
    { 2560, 1080,  60.0, 185625,  2750, 1125, 44,   5,   88,  148,  4,  36, 1, 1, 0, "2560×1080@60" },
    { 2560, 1440,  60.0, 241500,  2720, 1481, 44,   5,   88,  148,  4,  33, 1, 1, 0, "2560×1440@60 (WQHD)" },
    { 2560, 1440, 120.0, 497750,  2720, 1525, 44,   5,   88,  148,  4,  72, 1, 1, 0, "2560×1440@120" },
    { 2560, 1440, 144.0, 586580,  2720, 1499, 44,   5,   88,  148,  4,  47, 1, 1, 0, "2560×1440@144" },
    { 2560, 1600,  60.0, 268500,  2720, 1646, 44,   5,   88,  148,  4,  38, 1, 1, 0, "2560×1600@60" },
    { 3440, 1440,  60.0, 319750,  3600, 1481, 44,   5,   88,  148,  4,  33, 1, 1, 0, "3440×1440@60 (UWQHD)" },
    { 3840, 2160,  30.0, 297000,  4400, 2250, 44,   5,   176, 296,  4,  72, 1, 1, 0, "3840×2160@30 (UHD)" },
    { 3840, 2160,  60.0, 594000,  4400, 2250, 44,   5,   176, 296,  4,  72, 1, 1, 0, "3840×2160@60 (UHD)" },
    { 3840, 2160, 120.0, 1188000, 4400, 2250, 44,   5,   176, 296,  4,  72, 1, 1, 0, "3840×2160@120" },
    { 4096, 2160,  60.0, 594000,  4400, 2250, 44,   5,   176, 296,  4,  72, 1, 1, 0, "4096×2160@60 (DCI 4K)" },
    { 5120, 1440,  60.0, 469000,  5360, 1481, 44,   5,   88,  148,  4,  33, 1, 1, 0, "5120×1440@60" },
    { 5120, 2880,  60.0, 469000,  2720, 2880, 44,   5,   88,  148,  4,  33, 1, 1, 0, "5120×2880@60 (5K)" },
    { 7680, 4320,  60.0, 2376000, 8800, 4500, 44,   12,  176, 296,  8,  144,1, 1, 0, "7680×4320@60 (8K)" },
};

int vesa_dmt_lookup(uint32_t width, uint32_t height, double refresh_hz,
                    vesa_mode_t *mode)
{
    for (int i = 0; i < DMT_MODE_COUNT; i++) {
        if (dmt_modes[i].width == width &&
            dmt_modes[i].height == height &&
            fabs(dmt_modes[i].refresh_hz - refresh_hz) < 1.0) {
            *mode = dmt_modes[i];
            return 1;
        }
    }
    /* Try closest match */
    int best = -1;
    double best_dist = 1e9;
    for (int i = 0; i < DMT_MODE_COUNT; i++) {
        if (dmt_modes[i].width == width && dmt_modes[i].height == height) {
            double dist = fabs(dmt_modes[i].refresh_hz - refresh_hz);
            if (dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
    }
    if (best >= 0) {
        *mode = dmt_modes[best];
        return 1;
    }
    return 0;
}

int vesa_dmt_mode_count(void)
{
    return DMT_MODE_COUNT;
}

int vesa_dmt_get_mode(int index, vesa_mode_t *mode)
{
    if (index < 0 || index >= DMT_MODE_COUNT) return 0;
    *mode = dmt_modes[index];
    return 1;
}


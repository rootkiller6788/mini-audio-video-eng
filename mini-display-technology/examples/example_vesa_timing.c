/**
 * example_vesa_timing.c — End-to-End: VESA Display Timing Computation
 *
 * Demonstrates:
 *   - Computing display timings with CVT and GTF
 *   - Looking up standard DMT modes
 *   - Computing pixel clock, bandwidth requirements
 *   - Comparing display modes for interface selection
 *
 * L6: Canonical Problem — Given a target resolution and refresh rate,
 *     compute the complete display timing parameters and interface bandwidth.
 *
 * Usage: ./example_vesa_timing
 */

#include "display_types.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* External declarations */
int vesa_cvt_compute(uint32_t ha, uint32_t va, double hz, aspect_ratio_t a,
                     int rb, int il, timing_result_t *r);
int vesa_gtf_compute(uint32_t ha, uint32_t va, double hz, double m,
                     timing_result_t *r);
int vesa_dmt_lookup(uint32_t w, uint32_t h, double hz, vesa_mode_t *m);
int vesa_dmt_mode_count(void);
const char *interface_type_name(display_interface_t iface);

typedef struct {
    const char *name;
    uint32_t width, height;
    double refresh;
} mode_request_t;

static void print_timing(const display_timing_t *t, const char *label) {
    printf("  %s:\n", label);
    printf("    Pixel Clock:    %u kHz (%.2f MHz)\n",
           t->pixel_clock_khz, t->pixel_clock_khz / 1000.0);
    printf("    Active:          %u × %u\n", t->h_active, t->v_active);
    printf("    H Total:         %u (blank=%u, sync=%u, front=%u, back=%u)\n",
           t->h_active + t->h_blank, t->h_blank, t->h_sync,
           t->h_front_porch, t->h_back_porch);
    printf("    V Total:         %u (blank=%u, sync=%u, front=%u, back=%u)\n",
           t->v_active + t->v_blank, t->v_blank, t->v_sync,
           t->v_front_porch, t->v_back_porch);
    printf("    Refresh:         %.2f Hz\n", t->refresh_rate_hz);
    printf("    Scan:            %s\n",
           t->scan_mode == SCAN_PROGRESSIVE ? "Progressive" : "Interlaced");
}

static void compute_and_print(const char *name, uint32_t w, uint32_t h,
                              double hz, aspect_ratio_t ar) {
    printf("\n--- %s (%u×%u @ %.0fHz) ---\n", name, w, h, hz);
    timing_result_t r;

    if (vesa_cvt_compute(w, h, hz, ar, 0, 0, &r) == 0) {
        print_timing(&r.timing, "CVT Normal");
        printf("    Bandwidth (24bpp): %.1f Mbps\n", r.required_bandwidth_mbps);
    }

    if (vesa_cvt_compute(w, h, hz, ar, 1, 0, &r) == 0) {
        print_timing(&r.timing, "CVT-RB (Reduced Blanking)");
    }
}

static const char *select_best_interface(double bw_mbps) {
    if (bw_mbps <= 154)       return "HDMI 1.0 (165 MHz)";
    if (bw_mbps <= 340)       return "HDMI 1.4 (340 MHz)";
    if (bw_mbps <= 600)       return "HDMI 2.0 (600 MHz)";
    if (bw_mbps <= 1440)      return "HDMI 2.1 / DP 1.4 HBR3";
    if (bw_mbps <= 2560)      return "DisplayPort 2.0 UHBR10";
    if (bw_mbps <= 5400)      return "DisplayPort 2.0 UHBR20";
    return "Future interface required";
}

int main(void) {
    printf("══════════════════════════════════════════════════\n");
    printf("  VESA Display Timing Computation & Mode Selection\n");
    printf("══════════════════════════════════════════════════\n");

    /* Standard modes to compute */
    mode_request_t modes[] = {
        {"720p HD",     1280, 720,  60.0},
        {"1080p FHD",   1920, 1080, 60.0},
        {"1080p 120Hz", 1920, 1080, 120.0},
        {"1440p WQHD",  2560, 1440, 60.0},
        {"1440p 144Hz", 2560, 1440, 144.0},
        {"2160p UHD",   3840, 2160, 60.0},
        {"2160p 120Hz", 3840, 2160, 120.0},
        {"8K UHD",      7680, 4320, 60.0},
    };
    int n_modes = sizeof(modes) / sizeof(modes[0]);

    for (int i = 0; i < n_modes; i++) {
        compute_and_print(modes[i].name, modes[i].width, modes[i].height,
                         modes[i].refresh,
                         (modes[i].width * 9 == modes[i].height * 16)
                         ? ASPECT_16_9 : ASPECT_16_9);
    }

    /* Bandwidth comparison */
    printf("\n══════════════════════════════════════════════════\n");
    printf("  Interface Bandwidth Requirements (24 bpp RGB)\n");
    printf("══════════════════════════════════════════════════\n");

    double bandwidths[] = {
        74.25,   /* 720p60 */
        148.5,   /* 1080p60 */
        297.0,   /* 1080p120 */
        241.5,   /* 1440p60 */
        586.58,  /* 1440p144 */
        594.0,   /* 2160p60 */
        1188.0,  /* 2160p120 */
        2376.0,  /* 8K60 */
    };
    const char *mode_names[] = {
        "720p60", "1080p60", "1080p120", "1440p60",
        "1440p144", "2160p60", "2160p120", "8K60"
    };

    for (int i = 0; i < 8; i++) {
        const char *iface = select_best_interface(bandwidths[i]);
        printf("  %-12s  %8.1f Mbps  →  %s\n", mode_names[i], bandwidths[i], iface);
    }

    /* DMT database summary */
    printf("\n══════════════════════════════════════════════════\n");
    printf("  VESA DMT Standard Mode Database\n");
    printf("  Total standard modes: %d\n", vesa_dmt_mode_count());

    /* Display all 8K and 4K modes */
    printf("\n  Sample subset:\n");
    vesa_mode_t m;
    if (vesa_dmt_lookup(3840, 2160, 60.0, &m))
        printf("  %-25s  %5ukHz  %5u×%4u\n", m.name, m.pixel_clock_khz, m.h_total, m.v_total);
    if (vesa_dmt_lookup(3840, 2160, 120.0, &m))
        printf("  %-25s  %5ukHz  %5u×%4u\n", m.name, m.pixel_clock_khz, m.h_total, m.v_total);
    if (vesa_dmt_lookup(7680, 4320, 60.0, &m))
        printf("  %-25s  %5ukHz  %5u×%4u\n", m.name, m.pixel_clock_khz, m.h_total, m.v_total);

    printf("\nDONE.\n");
    return 0;
}


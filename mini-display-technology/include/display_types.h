/**
 * display_types.h — Core Display Technology Definitions
 *
 * L1 Definitions: Display device types, pixel formats, resolution,
 *                  refresh/frame rate, color depth, timing parameters
 * L2 Core Concepts: Scanout, frame buffer, blanking intervals,
 *                   pixel clock, port types, mode sets
 *
 * Reference:
 *   Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
 *   VESA Coordinated Video Timings (CVT) Standard 1.2
 *   CTA-861-G — A DTV Profile for Uncompressed High Speed Digital Interfaces
 *   Myers, "Display Interfaces: Fundamentals and Standards" (2003)
 *
 * Course Mapping:
 *   MIT 6.003 — Signal Processing (sampling, bandwidth)
 *   Stanford EE102A — Signal Processing (scanning, rate conversion)
 *   Berkeley EE16B — Circuits (display driver timing)
 *   Georgia Tech ECE 4270 — DSP (image signal fundamentals)
 *   TU Munich — Display Technology (fundamental parameters)
 */

#ifndef DISPLAY_TYPES_H
#define DISPLAY_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Display Technology Fundamentals
 * ========================================================================== */

/** Display panel technology types */
typedef enum {
    DISPLAY_LCD_TN      = 0,   /**< Twisted Nematic LCD */
    DISPLAY_LCD_IPS     = 1,   /**< In-Plane Switching LCD */
    DISPLAY_LCD_VA      = 2,   /**< Vertical Alignment LCD */
    DISPLAY_OLED_RGB    = 3,   /**< RGB-stripe OLED */
    DISPLAY_OLED_PENTILE = 4,  /**< Pentile/Delta subpixel OLED */
    DISPLAY_AMOLED      = 5,   /**< Active-Matrix OLED */
    DISPLAY_MICROLED    = 6,   /**< MicroLED emissive display */
    DISPLAY_EINK        = 7,   /**< Electrophoretic E-Ink */
    DISPLAY_CRT         = 8,   /**< Cathode Ray Tube */
    DISPLAY_PLASMA      = 9,   /**< Plasma Display Panel */
    DISPLAY_DLP         = 10,  /**< Digital Light Processing (MEMS) */
    DISPLAY_LCOS        = 11,  /**< Liquid Crystal on Silicon */
    DISPLAY_LED         = 12   /**< Direct-view LED display */
} display_type_t;

/** Display interface connector standards */
typedef enum {
    IFACE_VGA       = 0,  /**< VGA D-sub 15 (analog RGB) */
    IFACE_DVI_D     = 1,  /**< DVI-D (digital only) */
    IFACE_DVI_I     = 2,  /**< DVI-I (digital + analog) */
    IFACE_HDMI_1_4  = 3,  /**< HDMI 1.4 (up to 4K@30) */
    IFACE_HDMI_2_0  = 4,  /**< HDMI 2.0 (up to 4K@60) */
    IFACE_HDMI_2_1  = 5,  /**< HDMI 2.1 (up to 10K) */
    IFACE_DP_1_2    = 6,  /**< DisplayPort 1.2 (HBR2) */
    IFACE_DP_1_4    = 7,  /**< DisplayPort 1.4 (HBR3) */
    IFACE_DP_2_0    = 8,  /**< DisplayPort 2.0 (UHBR20) */
    IFACE_MIPI_DSI  = 9,  /**< MIPI DSI (mobile) */
    IFACE_eDP       = 10, /**< Embedded DisplayPort */
    IFACE_LVDS      = 11  /**< LVDS (internal panel) */
} display_interface_t;

/** Pixel color component format */
typedef enum {
    PIXFMT_RGB888   = 0,  /**< 24-bit RGB 8:8:8 */
    PIXFMT_BGR888   = 1,  /**< 24-bit BGR 8:8:8 (little-endian) */
    PIXFMT_RGBA8888 = 2,  /**< 32-bit RGBA 8:8:8:8 */
    PIXFMT_ARGB8888 = 3,  /**< 32-bit ARGB 8:8:8:8 */
    PIXFMT_RGB565   = 4,  /**< 16-bit RGB 5:6:5 */
    PIXFMT_RGB555   = 5,  /**< 15-bit RGB 5:5:5 */
    PIXFMT_YUV444   = 6,  /**< YCbCr 4:4:4 (full chroma) */
    PIXFMT_YUV422   = 7,  /**< YCbCr 4:2:2 (half horiz chroma) */
    PIXFMT_YUV420   = 8,  /**< YCbCr 4:2:0 (quarter chroma) */
    PIXFMT_NV12     = 9,  /**< Y + interleaved UV (4:2:0) */
    PIXFMT_RGB101010 = 10, /**< 30-bit RGB 10:10:10 (HDR) */
    PIXFMT_RGB121212 = 11, /**< 36-bit RGB 12:12:12 (Dolby) */
    PIXFMT_MONO8    = 12, /**< 8-bit grayscale */
    PIXFMT_MONO16   = 13  /**< 16-bit grayscale */
} pixel_format_t;

/** Color standard / primaries */
typedef enum {
    COLORSTD_BT601_NTSC   = 0,  /**< ITU-R BT.601 (SD, NTSC primaries) */
    COLORSTD_BT601_PAL    = 1,  /**< ITU-R BT.601 (SD, PAL primaries) */
    COLORSTD_BT709        = 2,  /**< ITU-R BT.709 (HD) */
    COLORSTD_BT2020       = 3,  /**< ITU-R BT.2020 (UHD) */
    COLORSTD_BT2100_PQ    = 4,  /**< ITU-R BT.2100 PQ (HDR) */
    COLORSTD_BT2100_HLG   = 5,  /**< ITU-R BT.2100 HLG (HDR) */
    COLORSTD_SRGB         = 6,  /**< sRGB (IEC 61966-2-1) */
    COLORSTD_ADOBE_RGB    = 7,  /**< Adobe RGB (1998) */
    COLORSTD_DCI_P3       = 8,  /**< DCI-P3 (digital cinema) */
    COLORSTD_DISPLAY_P3   = 9   /**< Display P3 (Apple) */
} color_standard_t;

/** Sync signal polarity (VESA timing) */
typedef enum {
    SYNC_POSITIVE = 0,  /**< Positive polarity (asserted high) */
    SYNC_NEGATIVE = 1   /**< Negative polarity (asserted low) */
} sync_polarity_t;

/** Display aspect ratios */
typedef enum {
    ASPECT_4_3   = 0,  /**< 4:3 (standard SD) */
    ASPECT_5_4   = 1,  /**< 5:4 (old LCD) */
    ASPECT_16_9  = 2,  /**< 16:9 (HD/UHD widescreen) */
    ASPECT_16_10 = 3,  /**< 16:10 (computer) */
    ASPECT_21_9  = 4,  /**< 21:9 (ultrawide cinema) */
    ASPECT_32_9  = 5,  /**< 32:9 (super-ultrawide) */
    ASPECT_1_1   = 6   /**< 1:1 (square) */
} aspect_ratio_t;

/** Scan mode for display refresh */
typedef enum {
    SCAN_PROGRESSIVE = 0,  /**< Progressive scan (all lines per frame) */
    SCAN_INTERLACED  = 1   /**< Interlaced scan (odd/even fields) */
} scan_mode_t;

/* ==========================================================================
 * L1: Core Data Structures
 * ========================================================================== */

/** Single RGB pixel (24-bit/30-bit/36-bit integer storage) */
typedef struct {
    uint16_t r;  /**< Red component   (0 to max_val) */
    uint16_t g;  /**< Green component (0 to max_val) */
    uint16_t b;  /**< Blue component  (0 to max_val) */
    uint16_t a;  /**< Alpha/opacity   (0 to max_val), 0xFF for opaque */
    uint16_t max_val; /**< Maximum component value (255, 1023, 4095) */
} pixel_rgb_t;

/** Floating-point linear-light RGB pixel (for HDR/processing) */
typedef struct {
    double r;  /**< Red linear light   [0.0, ∞) */
    double g;  /**< Green linear light [0.0, ∞) */
    double b;  /**< Blue linear light  [0.0, ∞) */
    double a;  /**< Alpha              [0.0, 1.0] */
} pixel_float_t;

/** YCbCr pixel (digital video standard) */
typedef struct {
    double y;   /**< Luma component  [0, 1]     (BT.601/709 range) */
    double cb;  /**< Blue-difference chroma [-0.5, 0.5] */
    double cr;  /**< Red-difference chroma  [-0.5, 0.5] */
} pixel_ycbcr_t;

/** Display resolution as width × height */
typedef struct {
    uint32_t width;   /**< Horizontal active pixels */
    uint32_t height;  /**< Vertical active lines */
} resolution_t;

/** Display timing parameters (VESA-style) */
typedef struct {
    uint32_t pixel_clock_khz;    /**< Pixel clock in kHz */
    uint32_t h_active;           /**< Horizontal active pixels */
    uint32_t h_blank;            /**< Horizontal blanking pixels */
    uint32_t h_sync;             /**< Horizontal sync width */
    uint32_t h_front_porch;      /**< Horizontal front porch */
    uint32_t h_back_porch;       /**< Horizontal back porch */
    uint32_t v_active;           /**< Vertical active lines */
    uint32_t v_blank;            /**< Vertical blanking lines */
    uint32_t v_sync;             /**< Vertical sync width */
    uint32_t v_front_porch;      /**< Vertical front porch */
    uint32_t v_back_porch;       /**< Vertical back porch */
    sync_polarity_t h_polarity;  /**< Horizontal sync polarity */
    sync_polarity_t v_polarity;  /**< Vertical sync polarity */
    scan_mode_t scan_mode;       /**< Progressive or interlaced */
    double refresh_rate_hz;      /**< Frame refresh rate */
} display_timing_t;

/** Complete display mode descriptor */
typedef struct {
    uint32_t         mode_id;        /**< Unique mode identifier */
    resolution_t     resolution;     /**< Active pixel resolution */
    display_timing_t timing;         /**< Full timing parameters */
    pixel_format_t   pixel_format;   /**< Pixel encoding */
    color_standard_t color_standard; /**< Color primaries */
    aspect_ratio_t   aspect;         /**< Display aspect ratio */
    uint16_t         bits_per_color; /**< Bits per color component (8/10/12) */
    char             name[64];       /**< Human-readable mode name */
} display_mode_t;

/** Standard VESA / CTA mode entry */
typedef struct {
    uint32_t width;
    uint32_t height;
    double refresh_hz;
    uint32_t pixel_clock_khz;
    uint16_t h_total, v_total;
    uint16_t h_sync, v_sync;
    uint16_t h_front_porch, h_back_porch;
    uint16_t v_front_porch, v_back_porch;
    sync_polarity_t h_pol, v_pol;
    scan_mode_t scan;
    const char *name;
} vesa_mode_t;

/** Frame buffer descriptor — pixel storage in memory */
typedef struct {
    uint8_t        *data;         /**< Raw pixel data pointer */
    uint32_t        width;        /**< Width in pixels */
    uint32_t        height;       /**< Height in lines */
    uint32_t        stride_bytes; /**< Row stride (may exceed width*bytes_per_pixel) */
    pixel_format_t  format;       /**< Pixel format */
    uint32_t        bytes_per_pixel; /**< Bytes per pixel */
    size_t          total_bytes;  /**< Total allocated bytes */
} framebuffer_t;

/* ==========================================================================
 * L2: Color depth and display metrics
 * ========================================================================== */

/** Color bit depth range */
typedef struct {
    uint8_t bits_per_channel;  /**< Bits per color channel (8/10/12/16) */
    uint16_t code_min;         /**< Minimum digital code value */
    uint16_t code_max;         /**< Maximum digital code value */
    char     uses_full_range;  /**< 0 = limited/video range, 1 = full range */
} color_depth_t;

/** Luminance specification */
typedef struct {
    double min_luminance_cdm2;  /**< Black level in cd/m² */
    double max_luminance_cdm2;  /**< Peak white in cd/m² */
    double contrast_ratio;       /**< max/min */
    double gamma_value;          /**< Display gamma (approx) */
    double white_point_x;        /**< White point CIE 1931 x */
    double white_point_y;        /**< White point CIE 1931 y */
} luminance_spec_t;

/** Display timing result after computation */
typedef struct {
    display_timing_t timing;
    int valid;               /**< 1 if timing is valid */
    double actual_refresh_hz; /**< Actual achieved refresh rate */
    double pixel_clock_mhz;   /**< Pixel clock in MHz */
    double horizontal_khz;    /**< Horizontal scan rate in kHz */
    double required_bandwidth_mbps; /**< Required interface bandwidth */
} timing_result_t;

/* ==========================================================================
 * L1: Subpixel Geometry & Physical Metrics
 * ========================================================================== */

/** Subpixel layout (for subpixel rendering / anti-aliasing) */
typedef enum {
    SUBPIXEL_RGB_HORIZ  = 0, /**< R-G-B horizontal stripe */
    SUBPIXEL_BGR_HORIZ  = 1, /**< B-G-R horizontal stripe */
    SUBPIXEL_VRGB_VERT  = 2, /**< Vertical stripe */
    SUBPIXEL_PENTILE    = 3, /**< RGBG diamond/pentile */
    SUBPIXEL_NONE       = 4  /**< No subpixels (grayscale) */
} subpixel_layout_t;

/** Physical display dimensions */
typedef struct {
    double width_mm;    /**< Active area width */
    double height_mm;   /**< Active area height */
    double diagonal_mm; /**< Diagonal (calculated) */
    double pixel_pitch_um; /**< Pixel pitch in micrometers */
    uint32_t ppi;       /**< Pixels per inch */
} display_geometry_t;

/* ==========================================================================
 * L2: Core Concept API Declarations
 * ========================================================================== */

void pixel_rgb_init(pixel_rgb_t *p, uint16_t r, uint16_t g, uint16_t b,
                    uint16_t a, uint8_t bits);
void pixel_rgb_to_float(const pixel_rgb_t *p, pixel_float_t *fp);
void pixel_float_to_rgb(const pixel_float_t *fp, pixel_rgb_t *p, uint8_t bits);
double pixel_luma_bt709(const pixel_rgb_t *p);
double pixel_luma_bt601(const pixel_rgb_t *p);
int timing_validate(const display_timing_t *t);
uint64_t timing_pixel_clock_hz(const display_timing_t *t);
double timing_horizontal_freq_khz(const display_timing_t *t);
double mode_bandwidth_mbps(const display_mode_t *m);
int timing_compare(const display_timing_t *a, const display_timing_t *b,
                   double tol_percent);
int mode_to_string(const display_mode_t *m, char *buf, size_t size);

int framebuffer_alloc(framebuffer_t *fb, uint32_t width, uint32_t height,
                      pixel_format_t format);
void framebuffer_free(framebuffer_t *fb);
void framebuffer_clear(framebuffer_t *fb, const pixel_rgb_t *color);
int framebuffer_pixel_write(framebuffer_t *fb, uint32_t x, uint32_t y,
                            const pixel_rgb_t *p);
int framebuffer_pixel_read(const framebuffer_t *fb, uint32_t x, uint32_t y,
                           pixel_rgb_t *p);
void framebuffer_fill_rect(framebuffer_t *fb, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const pixel_rgb_t *color);
void framebuffer_gradient_h(framebuffer_t *fb,
                            double r_start, double r_end,
                            double g_start, double g_end,
                            double b_start, double b_end);
void framebuffer_gray_ramp(framebuffer_t *fb, uint8_t bits);
void framebuffer_test_pattern(framebuffer_t *fb);
int framebuffer_blit(const framebuffer_t *src, framebuffer_t *dst,
                     uint32_t sx, uint32_t sy, uint32_t w, uint32_t h,
                     uint32_t dx, uint32_t dy);
uint32_t framebuffer_checksum(const framebuffer_t *fb);
int framebuffer_verify(const framebuffer_t *fb, uint32_t expected_checksum);

/** Compute pixel density (PPI) from resolution and physical size */
double compute_ppi(uint32_t width, uint32_t height, double diag_mm);

/** Compute diagonal size in mm from resolution and PPI */
double compute_diag_mm(uint32_t width, uint32_t height, double ppi);

/** Return bytes_per_pixel for a given format; 0 if unknown */
uint32_t pixel_format_bytes(pixel_format_t fmt);

/** Return human-readable name for a display type */
const char *display_type_name(display_type_t dt);

/** Return human-readable name for an interface type */
const char *interface_type_name(display_interface_t iface);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_TYPES_H */


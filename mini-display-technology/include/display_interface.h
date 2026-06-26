/**
 * display_interface.h — Display Interface Standards
 *
 * L1 Definitions: TMDS signaling, DP link lanes, MIPI DSI packets,
 *                  EDID block structure, VESA DMT/CVT/GTF timing
 * L2 Core Concepts: High-speed serial link, 8b/10b encoding,
 *                   link training, hot-plug detection, CEC
 * L4 Fundamental Laws: Nyquist-Shannon for link bandwidth,
 *                      channel capacity (Shannon-Hartley applied to cables)
 *
 * Reference:
 *   HDMI 2.1 Specification (HDMI Forum, 2017)
 *   VESA DisplayPort Standard 2.0 (2022)
 *   MIPI DSI Specification v1.3
 *   VESA Enhanced EDID Standard (E-EDID Release A, 2.0)
 *   VESA CVT 1.2 / GTF 1.1
 *   DVI 1.0 Specification (DDWG, 1999)
 *
 * Course Mapping:
 *   MIT 6.450 — Digital Communications (line coding, scrambling)
 *   Stanford EE359 — Wireless (link adaptation, training)
 *   Illinois ECE 459 — Communications (8b/10b, TMDS)
 *   Georgia Tech ECE 6350 — EM (transmission lines, impedance control)
 *   TU Munich — High-Frequency Engineering (signal integrity)
 */

#ifndef DISPLAY_INTERFACE_H
#define DISPLAY_INTERFACE_H

#include <stdint.h>
#include <stddef.h>
#include "display_types.h"
#include "color_science.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * L1: Core Definitions — Interface-Specific Structures
 * ========================================================================== */

/** TMDS (Transition Minimized Differential Signaling) channel state */
typedef struct {
    uint8_t  control[2];   /**< Control bits: HSYNC=bit1, VSYNC=bit2 */
    uint8_t  data[3];      /**< 8-bit pixel data per channel (R, G, B or Y, Cb, Cr) */
    uint8_t  de;           /**< Data Enable: 1 = active video, 0 = blanking */
    uint16_t clock_mhz;    /**< TMDS clock frequency in MHz */
} tmds_channel_t;

/** TMDS encoding state machine */
typedef struct {
    int      disparity;    /**< Running DC disparity accumulator  */
    uint32_t encoded_bits; /**< Current 10-bit encoded word     */
} tmds_encoder_t;

/** DisplayPort main link lane configuration */
typedef enum {
    DP_LANE_1 = 1,  /**< 1 lane  (RBR/HBR) */
    DP_LANE_2 = 2,  /**< 2 lanes (HBR/HBR2) */
    DP_LANE_4 = 4   /**< 4 lanes (HBR2/HBR3/UHBR) */
} dp_lane_count_t;

/** DisplayPort link rate */
typedef enum {
    DP_RATE_RBR    = 162,  /**< 1.62 Gbps/lane */
    DP_RATE_HBR    = 270,  /**< 2.70 Gbps/lane */
    DP_RATE_HBR2   = 540,  /**< 5.40 Gbps/lane */
    DP_RATE_HBR3   = 810,  /**< 8.10 Gbps/lane */
    DP_RATE_UHBR10 = 1000, /**< 10.0 Gbps/lane */
    DP_RATE_UHBR13_5 = 1350, /**< 13.5 Gbps/lane */
    DP_RATE_UHBR20 = 2000 /**< 20.0 Gbps/lane */
} dp_link_rate_t;

/** DisplayPort link training state machine */
typedef enum {
    DP_TRAIN_IDLE       = 0, /**< Not training */
    DP_TRAIN_CLOCK_RECOVERY = 1, /**< Phase 1: CR lock */
    DP_TRAIN_CHANNEL_EQ = 2, /**< Phase 2: Channel equalization */
    DP_TRAIN_DONE       = 3, /**< Training complete */
    DP_TRAIN_FAIL       = 4  /**< Training failed */
} dp_train_state_t;

/** MIPI DSI packet header (4 bytes) */
typedef struct {
    uint8_t  data_type;       /**< DSI data type (0x01–0x3E) */
    uint8_t  virtual_channel; /**< Virtual channel ID (0–3) */
    uint16_t data_length;     /**< Payload length in bytes */
    uint8_t  ecc;             /**< Error Correction Code (header ECC) */
} mipi_dsi_pkt_header_t;

/** MIPI DSI packet (short or long) */
typedef struct {
    mipi_dsi_pkt_header_t header;
    uint8_t   *payload;       /**< Payload data (max 65535 bytes) */
    uint16_t   checksum;      /**< 16-bit payload checksum */
    int        is_long_pkt;   /**< 0 = short (2 byte payload), 1 = long */
} mipi_dsi_packet_t;

/** EDID block (128 bytes) */
typedef struct {
    uint8_t data[128];  /**< Raw EDID block */
    int     valid;      /**< 1 if checksum is valid */
} edid_block_t;

/** EDID standard timing descriptor */
typedef struct {
    uint16_t h_active;     /**< Horizontal active pixels */
    uint16_t v_active;     /**< Vertical active lines */
    double   refresh_hz;   /**< Refresh rate */
    uint8_t  aspect_ratio; /**< Encoded aspect ratio (per EDID spec) */
} edid_std_timing_t;

/** EDID detailed timing descriptor (18 bytes) */
typedef struct {
    uint32_t pixel_clock_10khz; /**< Pixel clock in 10 kHz units */
    uint16_t h_active, h_blank;
    uint16_t v_active, v_blank;
    uint16_t h_sync_offset, h_sync_pulse;
    uint16_t v_sync_offset, v_sync_pulse;
    uint8_t  h_sync_width_mm, v_sync_width_mm;
    uint8_t  h_border, v_border;
    uint8_t  features;          /**< Interlaced, sync type, etc. */
    int      is_used;           /**< 1 if this is a valid timing descriptor */
} edid_dtd_t;

/** Complete parsed EDID information */
typedef struct {
    char     manufacturer[4];    /**< 3-letter PnP ID + null */
    uint16_t product_code;
    uint32_t serial_number;
    uint8_t  week_mfg, year_mfg; /**< Week/year of manufacture */
    uint8_t  edid_version, edid_revision;
    uint8_t  num_extensions;     /**< Number of extension blocks */
    double   gamma_display;      /**< Display gamma (EDID field) */
    cie_xy_t primaries_xy[3];    /**< RGB primaries in CIE xy */
    cie_xy_t white_xy;          /**< White point in CIE xy */
    edid_dtd_t detailed_timings[4]; /**< Up to 4 detailed timing descriptors */
    edid_std_timing_t std_timings[8]; /**< Up to 8 standard timings */
    char     monitor_name[14];   /**< Monitor name from descriptor */
    uint32_t max_h_size_cm, max_v_size_cm; /**< Physical size */
    uint16_t min_h_rate_hz, max_h_rate_hz; /**< Horizontal scan range */
    uint16_t min_v_rate_hz, max_v_rate_hz; /**< Vertical refresh range */
    uint32_t max_pixel_clock_mhz; /**< Max pixel clock */
} edid_info_t;

/** HDMI AVI InfoFrame (Audio Video Interleave) */
typedef struct {
    uint8_t  version;          /**< InfoFrame version (2/3/4) */
    uint8_t  y;                /**< Y0/Y1 color component indicator */
    uint8_t  color_format;     /**< RGB/YUV */
    uint8_t  active_format;    /**< Active format aspect ratio */
    uint8_t  scan_info;        /**< Overscan/underscan */
    uint8_t  colorimetry;      /**< BT.601/709/xvYCC */
    uint8_t  picture_aspect;   /**< Picture aspect ratio */
    uint8_t  quant_range;      /**< Full/limited range */
    uint8_t  ext_colorimetry;  /**< Extended colorimetry (ADB/BT.2020) */
    uint8_t  content_type;     /**< Graphics/photo/cinema/game */
    uint16_t end_top_bar;      /**< Letterbox top bar lines */
    uint16_t start_bottom_bar; /**< Letterbox bottom bar lines */
    uint16_t end_left_bar;     /**< Pillarbox left bar pixels */
    uint16_t start_right_bar;  /**< Pillarbox right bar pixels */
    double   pixel_repeat;     /**< 1×, 2×, …, 10× pixel repetition */
} hdmi_avi_infoframe_t;

/** HDMI CEC (Consumer Electronics Control) command */
typedef struct {
    uint8_t initiator;     /**< Logical address of initiator (0–15) */
    uint8_t destination;   /**< Logical address of destination */
    uint8_t opcode;        /**< CEC opcode */
    uint8_t params[14];    /**< Up to 14 parameter bytes */
    uint8_t param_count;   /**< Number of parameter bytes used */
} hdmi_cec_cmd_t;

/* ==========================================================================
 * L2: VESA Timing Computation (CVT / GTF / DMT)
 * ========================================================================== */

/**
 * Compute display timing using VESA CVT (Coordinated Video Timing) formula.
 *
 * CVT standard 1.2 uses piecewise formulas for horizontal/vertical blanking
 * based on the aspect ratio and whether margins are enabled.
 *
 * @param h_active    Horizontal active pixels
 * @param v_active    Vertical active lines
 * @param refresh_hz  Target refresh rate
 * @param aspect_ratio Aspect ratio category
 * @param reduced_blanking If 1, use CVT-RB (reduced blanking) variant
 * @param interlaced  If 1, compute for interlaced scan
 * @param result      Output timing result
 * @return 0 on success, -1 on invalid parameters
 */
int vesa_cvt_compute(uint32_t h_active, uint32_t v_active,
                     double refresh_hz, aspect_ratio_t aspect_ratio,
                     int reduced_blanking, int interlaced,
                     timing_result_t *result);

/**
 * Compute using VESA GTF (Generalized Timing Formula).
 * GTF uses a simpler formula: margin percentage parameter.
 */
int vesa_gtf_compute(uint32_t h_active, uint32_t v_active,
                     double refresh_hz, double margin_percent,
                     timing_result_t *result);

/**
 * Lookup a VESA DMT (Display Monitor Timing) standard mode.
 * Returns 1 if found, 0 otherwise.
 */
int vesa_dmt_lookup(uint32_t width, uint32_t height, double refresh_hz,
                    vesa_mode_t *mode);

/** Get the number of standard DMT modes */
int vesa_dmt_mode_count(void);

/** Get the nth DMT mode (0-indexed) */
int vesa_dmt_get_mode(int index, vesa_mode_t *mode);

/* ==========================================================================
 * L2: TMDS / HDMI Encoding
 * ========================================================================== */

/**
 * Encode an 8-bit data value using TMDS 8b/10b encoding.
 * Returns the 10-bit encoded word.
 */
uint16_t tmds_encode_8b10b(uint8_t data, tmds_encoder_t *enc);

/**
 * Decode a 10-bit TMDS symbol back to 8-bit data.
 * Returns 0 on success, -1 on invalid symbol.
 */
int tmds_decode_10b8b(uint16_t symbol, uint8_t *data);

/** Initialize a TMDS encoder state */
void tmds_encoder_init(tmds_encoder_t *enc);

/** TMDS Data Island (auxiliary data) packet for audio/infoframe */
typedef struct {
    uint8_t  header[3];      /**< 24-bit header */
    uint8_t  payload[28];    /**< 0–28 bytes of payload */
    uint8_t  payload_len;    /**< Actual payload byte count */
    uint8_t  bch_parity[2];  /**< BCH ECC parity bytes */
} tmds_data_island_t;

/** Build a TMDS data island packet (header + payload + BCH ECC) */
void tmds_data_island_build(const uint8_t *payload, uint8_t len,
                            uint8_t packet_type,
                            tmds_data_island_t *island);

/** Build an HDMI AVI InfoFrame payload packet */
int hdmi_avi_infoframe_build(const hdmi_avi_infoframe_t *infoframe,
                              uint8_t *payload, uint8_t *payload_len);

/** Default AVI InfoFrame for RGB 1080p60 */
void hdmi_avi_infoframe_default(hdmi_avi_infoframe_t *infoframe);

/** CEC frame encode/decode */
int hdmi_cec_encode(const hdmi_cec_cmd_t *cmd, uint8_t *frame, uint8_t *len);
int hdmi_cec_decode(const uint8_t *frame, uint8_t len, hdmi_cec_cmd_t *cmd);

/* ==========================================================================
 * L2: EDID Parsing
 * ========================================================================== */

/** Parse a raw 128-byte EDID block */
int edid_parse_block(const uint8_t *data, edid_info_t *info);

/** Verify EDID checksum (sum of all 128 bytes mod 256 == 0) */
int edid_verify_checksum(const uint8_t *data);

/** Extract the monitor name from EDID descriptor blocks */
int edid_get_monitor_name(const edid_info_t *info, char *name, size_t size);

/** Compute total EDID block count (base + extensions) */
int edid_total_blocks(const edid_info_t *info);

/* ==========================================================================
 * L2: DisplayPort / MIPI DSI
 * ========================================================================== */

/** Compute effective DP link bandwidth in Mbps */
double dp_link_bandwidth_mbps(dp_lane_count_t lanes, dp_link_rate_t rate);

/** Determine minimum DP link rate needed for a given pixel bandwidth */
dp_link_rate_t dp_min_link_rate(double required_mbps, dp_lane_count_t lanes);

/** MIPI DSI: build a DCS (Display Command Set) short write packet */
mipi_dsi_packet_t mipi_dsi_dcs_short_write(uint8_t cmd, uint8_t param);

/** MIPI DSI: build a long write packet */
mipi_dsi_packet_t mipi_dsi_dcs_long_write(uint8_t cmd,
                                           const uint8_t *params,
                                           uint16_t param_len);

/** MIPI DSI: calculate header ECC */
uint8_t mipi_dsi_header_ecc(const mipi_dsi_pkt_header_t *hdr);

/** MIPI DSI: calculate payload checksum */
uint16_t mipi_dsi_checksum(const uint8_t *data, uint16_t len);

/** MIPI DSI: verify header ECC */
int mipi_dsi_verify_ecc(const mipi_dsi_pkt_header_t *hdr);

/* ==========================================================================
 * L2: High-Speed Link Signal Integrity
 * ========================================================================== */

/** Compute maximum cable length for TMDS at given clock rate */
double tmds_max_cable_length_m(double clock_mhz, double cable_atten_db_per_m);

/** Eye diagram opening estimation for given data rate and cable length */
double link_eye_opening_percent(double bit_rate_gbps, double cable_length_m,
                                int equalized);

/** Compute required equalization gain for a given insertion loss */
double link_eq_gain_db(double insertion_loss_db, double target_margin_db);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_INTERFACE_H */


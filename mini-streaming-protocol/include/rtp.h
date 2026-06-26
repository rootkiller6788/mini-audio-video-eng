/**
 * @file rtp.h
 * @brief RTP (Real-time Transport Protocol) — RFC 3550
 *
 * Real-time Transport Protocol for end-to-end network transport of
 * real-time data (audio, video). Provides payload type identification,
 * sequence numbering, timestamping, and delivery monitoring.
 *
 * Covers L1 Definitions: RTP fixed header fields, CSRC, SSRC, payload types
 * Covers L2 Concepts: Packetization, sequence number wrapping, timestamp model
 * Covers L3 Math: Modular arithmetic (mod 2^16), NTP timestamp (64-bit)
 * Covers L4 Laws: RFC 3550 seqnum comparison semantics
 * Covers L5 Algorithms: FU-A fragmentation, STAP-A aggregation, depacketization
 *
 * References:
 * - RFC 3550 — RTP: A Transport Protocol for Real-Time Applications
 * - RFC 6184 — RTP Payload Format for H.264 Video
 * - Schulzrinne et al., "RTP: A Transport Protocol for Real-Time Applications" (1996)
 *
 * @course Stanford EE359, Michigan EECS 455, Georgia Tech ECE 6601
 */

#ifndef RTP_H
#define RTP_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * L1: RTP Fixed Header (RFC 3550 §5.1)
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           timestamp                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           synchronization source (SSRC) identifier            |
 * =-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-==-=
 * |            contributing source (CSRC) identifiers             |
 * |                             ....                              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ========================================================================= */

/** @def RTP_VERSION
 * @brief Current RTP version is 2 (RFC 3550).
 */
#define RTP_VERSION           2

/** @def RTP_HEADER_MIN_SIZE
 * @brief Minimum RTP header size = 12 bytes (fixed header, no CSRC, no extension).
 */
#define RTP_HEADER_MIN_SIZE   12

/** @def RTP_MAX_CSRC
 * @brief Maximum number of CSRC identifiers in one RTP packet.
 */
#define RTP_MAX_CSRC          15

/** @def RTP_SEQ_MOD
 * @brief RTP sequence numbers are 16-bit, modulo 2^16 = 65536.
 */
#define RTP_SEQ_MOD           65536

/** @def RTP_SEQ_HALF
 * @brief Half the sequence number space, used for wrap-around comparison (RFC 3550 A.1).
 */
#define RTP_SEQ_HALF          32768

/**
 * @brief RTP payload types (RFC 3551 — static payload types).
 *
 * Dynamic payload types are in range 96–127.
 */
typedef enum {
    RTP_PT_PCMU       = 0,
    RTP_PT_GSM        = 3,
    RTP_PT_G723       = 4,
    RTP_PT_DVI4_8K    = 5,
    RTP_PT_DVI4_16K   = 6,
    RTP_PT_LPC        = 7,
    RTP_PT_PCMA       = 8,
    RTP_PT_G722       = 9,
    RTP_PT_L16_2CH    = 10,
    RTP_PT_L16_1CH    = 11,
    RTP_PT_MPA        = 14,
    RTP_PT_G728       = 15,
    RTP_PT_G729       = 18,
    RTP_PT_CELB       = 25,
    RTP_PT_JPEG       = 26,
    RTP_PT_NV         = 28,
    RTP_PT_H261       = 31,
    RTP_PT_MPV        = 32,
    RTP_PT_MP2T       = 33,
    RTP_PT_H263       = 34,
    RTP_PT_DYNAMIC_MIN = 96,
    RTP_PT_DYNAMIC_MAX = 127
} rtp_payload_type_t;

/**
 * @brief RTP fixed header fields (RFC 3550 §5.1).
 *
 * All fields are in network byte order (big-endian).
 */
typedef struct {
    uint8_t  version;
    uint8_t  padding;
    uint8_t  extension;
    uint8_t  csrc_count;
    uint8_t  marker;
    uint8_t  payload_type;
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
    uint32_t csrc[RTP_MAX_CSRC];
} rtp_header_t;

/** @brief Complete RTP packet descriptor. */
typedef struct {
    rtp_header_t  header;
    uint16_t      extension_id;
    uint16_t      extension_len;
    const uint8_t *extension_data;
    const uint8_t *payload;
    size_t         payload_len;
    size_t         padding_len;
} rtp_packet_t;

/* =========================================================================
 * L3: RTP Sequence Number Arithmetic (RFC 3550 Appendix A.1)
 * ========================================================================= */

/** @brief Compare two RTP sequence numbers with wrap-around awareness.
 *  @complexity O(1) */
int rtp_seq_lt(uint16_t s1, uint16_t s2);

/** @brief Compare: s1 > s2 in sequence space.
 *  @complexity O(1) */
int rtp_seq_gt(uint16_t s1, uint16_t s2);

/** @brief Signed delta: s2 - s1 accounting for wrap.
 *  @complexity O(1) */
int32_t rtp_seq_delta(uint16_t s1, uint16_t s2);

/* =========================================================================
 * L5: RTP Packet Construction and Parsing
 * ========================================================================= */

void rtp_header_init(rtp_header_t *hdr, uint8_t pt, uint16_t seq,
                     uint32_t timestamp, uint32_t ssrc);

int rtp_parse(const uint8_t *data, size_t len, rtp_packet_t *pkt);

int rtp_serialize(const rtp_packet_t *pkt, uint8_t *buf, size_t buf_size);

size_t rtp_packet_size(const rtp_packet_t *pkt);

/* =========================================================================
 * L2: RTP Timestamp Model
 * ========================================================================= */

#define RTP_VIDEO_CLOCK_RATE   90000
#define RTP_AUDIO_CLOCK_RATE   8000

void rtp_ts_to_ntp(uint32_t rtp_ts, uint32_t clock_rate,
                   uint32_t *ntp_sec, uint32_t *ntp_frac);

uint32_t rtp_pt_clock_rate(uint8_t pt);

/* =========================================================================
 * L5: H.264 NAL Unit Packetization (RFC 6184)
 * ========================================================================= */

#define RTP_H264_FU_A    28
#define RTP_H264_STAP_A  24

/** FU-A indicator byte structure. */
typedef struct {
    uint8_t forbidden_bit;
    uint8_t nri;
} rtp_fua_indicator_t;

/** FU-A header byte structure. */
typedef struct {
    uint8_t start;
    uint8_t end;
    uint8_t nal_type;
} rtp_fua_header_t;

int rtp_h264_fua_fragment(const uint8_t *nal_data, size_t nal_len,
                          uint16_t mtu,
                          uint8_t **fragments, size_t *fragment_lens,
                          int max_fragments);

int rtp_h264_fua_reassemble(const uint8_t *const *fragments,
                            const size_t *fragment_lens,
                            int num_fragments,
                            uint8_t *out_nal, size_t *out_len);

int rtp_h264_stap_a_aggregate(const uint8_t *const *nals,
                              const size_t *nal_lens,
                              int num_nals,
                              uint8_t *out_buf, size_t out_buf_size);

int rtp_h264_stap_a_deaggregate(const uint8_t *stap_data, size_t stap_len,
                                const uint8_t **nals, size_t *nal_lens,
                                int max_nals);

#endif /* RTP_H */

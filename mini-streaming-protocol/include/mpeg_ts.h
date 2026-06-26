/**
 * @file mpeg_ts.h
 * @brief MPEG-2 Transport Stream — ISO/IEC 13818-1
 *
 * MPEG-2 TS is the standard container for digital TV broadcasting
 * (DVB, ATSC, ISDB) and IPTV streaming. 188-byte fixed-size packets
 * carry multiplexed audio, video, and data elementary streams.
 *
 * Covers L1: TS packet, PAT, PMT, PES, PCR, PID, PTS/DTS
 * Covers L2: Multiplexing, PSI, clock recovery
 * Covers L3: PCR 27 MHz clock, 33-bit PTS arithmetic
 * Covers L4: DTS <= PTS invariant, PCR recovery PLL
 * Covers L5: PAT/PMT parsing, PES packetization, PCR extraction
 * Covers L6: TS demultiplex, clock extraction, program selection
 *
 * References: ISO/IEC 13818-1, ETSI TS 101 154, ATSC A/53
 * @course TU Munich HF, ETH 227-0455, Michigan EECS 411
 */

#ifndef MPEG_TS_H
#define MPEG_TS_H

#include <stdint.h>
#include <stddef.h>

/* L1: TS Packet Constants */
#define TS_PACKET_SIZE          188
#define TS_SYNC_BYTE            0x47
#define TS_PAYLOAD_MAX          184
#define TS_NULL_PID             0x1FFF
#define TS_PAT_PID              0x0000
#define TS_NIT_PID              0x0010
#define TS_SDT_PID              0x0011

#define TS_MAX_PIDS             256
#define TS_MAX_PROGRAMS         64

/* L1: TS Packet Header (4 bytes) */
typedef struct {
    uint8_t  sync_byte;
    uint8_t  transport_error;
    uint8_t  payload_start;
    uint8_t  transport_priority;
    uint16_t pid;
    uint8_t  transport_scrambling;
    uint8_t  adaptation_field_ctrl;
    uint8_t  continuity_counter;
} ts_header_t;

/* L1: Adaptation Field */
typedef struct {
    uint8_t  length;
    uint8_t  discontinuity;
    uint8_t  random_access;
    uint8_t  es_priority;
    uint8_t  pcr_flag;
    uint8_t  opcr_flag;
    uint8_t  splicing_point;
    uint8_t  transport_private;
    uint8_t  extension_flag;
    uint64_t pcr_base;
    uint16_t pcr_extension;
    uint64_t opcr_base;
    uint16_t opcr_extension;
    int      pcr_present;
} ts_adaptation_field_t;

/* L1: Complete TS packet */
typedef struct {
    ts_header_t            header;
    ts_adaptation_field_t  adaptation;
    const uint8_t         *payload_data;
    size_t                  payload_len;
    uint8_t                 raw[TS_PACKET_SIZE];
} ts_packet_t;

/* L1: PAT — Program Association Table */
typedef struct {
    uint16_t program_number;
    uint16_t pid;
} ts_pat_program_t;

typedef struct {
    uint8_t          table_id;
    uint8_t          section_syntax;
    uint16_t         section_length;
    uint16_t         transport_stream_id;
    uint8_t          version_number;
    uint8_t          current_next;
    uint8_t          section_number;
    uint8_t          last_section_number;
    ts_pat_program_t programs[TS_MAX_PROGRAMS];
    int              program_count;
    uint32_t         crc32;
} ts_pat_t;

/* L1: PMT — Program Map Table */
typedef struct {
    uint8_t  stream_type;
    uint16_t elementary_pid;
    uint16_t es_info_length;
    char     language[4];
    char     codec_name[32];
} ts_pmt_stream_t;

#define TS_PMT_MAX_STREAMS  16

typedef struct {
    uint8_t          table_id;
    uint8_t          section_syntax;
    uint16_t         section_length;
    uint16_t         program_number;
    uint8_t          version_number;
    uint8_t          current_next;
    uint8_t          section_number;
    uint8_t          last_section_number;
    uint16_t         pcr_pid;
    uint16_t         program_info_length;
    ts_pmt_stream_t  streams[TS_PMT_MAX_STREAMS];
    int              stream_count;
    uint32_t         crc32;
} ts_pmt_t;

/* L1: Stream Types */
#define TS_STREAM_MPEG1_VIDEO  0x01
#define TS_STREAM_MPEG2_VIDEO  0x02
#define TS_STREAM_MPEG1_AUDIO  0x03
#define TS_STREAM_MPEG2_AUDIO  0x04
#define TS_STREAM_AAC_AUDIO    0x0F
#define TS_STREAM_MPEG4_VIDEO  0x10
#define TS_STREAM_H264_VIDEO   0x1B
#define TS_STREAM_H265_VIDEO   0x24
#define TS_STREAM_AC3_AUDIO    0x81

/* L2: PES Packetization */
#define PES_START_CODE_PREFIX  0x000001
#define PES_MAX_SIZE           65536

typedef struct {
    uint32_t start_code;
    uint8_t  stream_id;
    uint16_t packet_length;
    uint8_t  original_or_copy;
    uint8_t  copyright;
    uint8_t  data_alignment;
    uint8_t  pts_dts_flags;
    uint8_t  escr_flag;
    uint8_t  es_rate_flag;
    uint8_t  dsm_trick_mode;
    uint8_t  additional_copy_info;
    uint8_t  crc_flag;
    uint8_t  extension_flag;
    uint8_t  header_length;
    uint64_t pts;
    uint64_t dts;
    int      has_pts;
    int      has_dts;
} pes_header_t;

#define PES_STREAM_AUDIO_MIN  0xC0
#define PES_STREAM_AUDIO_MAX  0xDF
#define PES_STREAM_VIDEO_MIN  0xE0
#define PES_STREAM_VIDEO_MAX  0xEF

/* L5: TS Parsing API */
int ts_parse_header(const uint8_t *data, ts_header_t *hdr);
int ts_parse_adaptation(const uint8_t *data, ts_adaptation_field_t *adapt);
int ts_parse_packet(const uint8_t *data, ts_packet_t *pkt);
int ts_parse_pat(const uint8_t *payload, size_t len, ts_pat_t *pat);
int ts_parse_pmt(const uint8_t *payload, size_t len, ts_pmt_t *pmt);
int ts_parse_pes_header(const uint8_t *data, size_t len, pes_header_t *pes);

double ts_pcr_to_seconds(uint64_t pcr_base, uint16_t pcr_extension);
double ts_pts_to_seconds(uint64_t pts);
const char *ts_stream_type_name(uint8_t stream_type);
int ts_check_dts_pts_invariant(uint64_t dts, uint64_t pts);

#endif /* MPEG_TS_H */

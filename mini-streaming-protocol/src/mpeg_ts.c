/**
 * @file mpeg_ts.c
 * @brief MPEG-2 Transport Stream Implementation - ISO/IEC 13818-1
 *
 * Implements TS packet parsing (188-byte packets), PAT/PMT table
 * extraction, PES header parsing, PCR clock recovery, and PTS/DTS
 * time conversion.
 *
 * L1: TS header and adaptation field parsing
 * L5: PAT (Program Association Table) parsing
 * L5: PMT (Program Map Table) parsing
 * L2: PES header parsing
 * L4: PCR 27 MHz clock -> seconds conversion
 * L3: 33-bit PTS arithmetic
 * L4: DTS <= PTS invariant check
 */

#include "mpeg_ts.h"
#include <string.h>
#include <stdio.h>

int ts_parse_header(const uint8_t *data, ts_header_t *hdr)
{
    if (!data || !hdr) return -1;
    if (data[0] != TS_SYNC_BYTE) return -1;

    memset(hdr, 0, sizeof(*hdr));
    hdr->sync_byte = data[0];
    hdr->transport_error    = (data[1] >> 7) & 0x01;
    hdr->payload_start      = (data[1] >> 6) & 0x01;
    hdr->transport_priority = (data[1] >> 5) & 0x01;
    hdr->pid = (uint16_t)(((data[1] & 0x1F) << 8) | data[2]);
    hdr->transport_scrambling  = (data[3] >> 6) & 0x03;
    hdr->adaptation_field_ctrl = (data[3] >> 4) & 0x03;
    hdr->continuity_counter    =  data[3]       & 0x0F;

    return 0;
}

int ts_parse_adaptation(const uint8_t *data, ts_adaptation_field_t *adapt)
{
    if (!data || !adapt) return -1;
    memset(adapt, 0, sizeof(*adapt));

    uint8_t af_length = data[4];
    if (af_length == 0) return 0;
    if (af_length > 183) return -1;

    const uint8_t *af = &data[5];
    adapt->length = af_length;
    adapt->discontinuity    = (af[0] >> 7) & 0x01;
    adapt->random_access    = (af[0] >> 6) & 0x01;
    adapt->es_priority      = (af[0] >> 5) & 0x01;
    adapt->pcr_flag         = (af[0] >> 4) & 0x01;
    adapt->opcr_flag        = (af[0] >> 3) & 0x01;
    adapt->splicing_point   = (af[0] >> 2) & 0x01;
    adapt->transport_private = (af[0] >> 1) & 0x01;
    adapt->extension_flag   =  af[0]       & 0x01;

    int offset = 1;
    if (adapt->pcr_flag && offset + 6 <= (int)af_length) {
        adapt->pcr_present = 1;
        uint64_t base_hi = (uint64_t)(((uint32_t)af[offset]   << 24) |
                                       ((uint32_t)af[offset+1] << 16) |
                                       ((uint32_t)af[offset+2] << 8)  |
                                        (uint32_t)af[offset+3]);
        uint64_t base_lo = ((uint64_t)(af[offset+4] >> 7) & 0x01);
        adapt->pcr_base = (base_hi << 1) | base_lo;
        adapt->pcr_extension = (uint16_t)(((af[offset+4] & 0x01) << 8) | af[offset+5]);
        offset += 6;
    }

    if (adapt->opcr_flag && offset + 6 <= (int)af_length) {
        offset += 6;
    }

    return 0;
}

int ts_parse_packet(const uint8_t *data, ts_packet_t *pkt)
{
    if (!data || !pkt) return -1;

    memcpy(pkt->raw, data, TS_PACKET_SIZE);

    if (ts_parse_header(data, &pkt->header) != 0)
        return -1;

    if (pkt->header.adaptation_field_ctrl == 0x02 ||
        pkt->header.adaptation_field_ctrl == 0x03) {
        ts_parse_adaptation(data, &pkt->adaptation);
    }

    uint8_t afc = pkt->header.adaptation_field_ctrl;
    if (afc == 0x01 || afc == 0x03) {
        size_t payload_start = 4;
        if (afc == 0x03) {
            payload_start = 5 + (size_t)pkt->adaptation.length;
            if (payload_start > TS_PACKET_SIZE) return -1;
        }
        pkt->payload_data = data + payload_start;
        pkt->payload_len = TS_PACKET_SIZE - payload_start;
    }

    return 0;
}

int ts_parse_pat(const uint8_t *payload, size_t len, ts_pat_t *pat)
{
    if (!payload || !pat || len < 8) return -1;
    memset(pat, 0, sizeof(*pat));

    size_t off = 0;
    pat->table_id = payload[off++];
    pat->section_syntax = (payload[off] >> 7) & 0x01;
    pat->section_length = (uint16_t)(((payload[off] & 0x0F) << 8) | payload[off+1]);
    off += 2;

    if (off + 2 > len) return -1;
    pat->transport_stream_id = (uint16_t)((payload[off] << 8) | payload[off+1]);
    off += 2;

    pat->version_number  = (payload[off] >> 1) & 0x1F;
    pat->current_next    =  payload[off]       & 0x01;
    off += 1;

    pat->section_number    = payload[off++];
    pat->last_section_number = payload[off++];

    /* Parse program loops */
    size_t table_end = 3 + (size_t)pat->section_length - 4;
    if (table_end > len) table_end = len;

    while (off + 4 <= table_end && pat->program_count < TS_MAX_PROGRAMS) {
        uint16_t prog_num = (uint16_t)((payload[off] << 8) | payload[off+1]);
        off += 2;
        uint16_t pid = (uint16_t)(((payload[off] & 0x1F) << 8) | payload[off+1]);
        off += 2;

        if (prog_num != 0) {
            pat->programs[pat->program_count].program_number = prog_num;
            pat->programs[pat->program_count].pid = pid;
            pat->program_count++;
        }
    }

    return 0;
}

int ts_parse_pmt(const uint8_t *payload, size_t len, ts_pmt_t *pmt)
{
    if (!payload || !pmt || len < 12) return -1;
    memset(pmt, 0, sizeof(*pmt));

    size_t off = 0;
    pmt->table_id = payload[off++];
    pmt->section_syntax = (payload[off] >> 7) & 0x01;
    pmt->section_length = (uint16_t)(((payload[off] & 0x0F) << 8) | payload[off+1]);
    off += 2;
    pmt->program_number = (uint16_t)((payload[off] << 8) | payload[off+1]);
    off += 2;
    pmt->version_number  = (payload[off] >> 1) & 0x1F;
    pmt->current_next    =  payload[off]       & 0x01;
    off += 1;
    pmt->section_number = payload[off++];
    pmt->last_section_number = payload[off++];
    pmt->pcr_pid = (uint16_t)(((payload[off] & 0x1F) << 8) | payload[off+1]);
    off += 2;
    pmt->program_info_length = (uint16_t)(((payload[off] & 0x0F) << 8) | payload[off+1]);
    off += 2;

    /* Skip program info descriptors */
    off += pmt->program_info_length;

    size_t table_end = 3 + (size_t)pmt->section_length - 4;
    if (table_end > len) table_end = len;

    while (off + 5 <= table_end && pmt->stream_count < TS_PMT_MAX_STREAMS) {
        ts_pmt_stream_t *s = &pmt->streams[pmt->stream_count];
        s->stream_type = payload[off++];
        s->elementary_pid = (uint16_t)(((payload[off] & 0x1F) << 8) | payload[off+1]);
        off += 2;
        s->es_info_length = (uint16_t)(((payload[off] & 0x0F) << 8) | payload[off+1]);
        off += 2;

        strcpy(s->codec_name, ts_stream_type_name(s->stream_type));
        strcpy(s->language, "und");

        /* Simplified: skip ES info descriptors */
        off += s->es_info_length;
        if (off > table_end) break;
        pmt->stream_count++;
    }

    return 0;
}

int ts_parse_pes_header(const uint8_t *data, size_t len, pes_header_t *pes)
{
    if (!data || !pes || len < 6) return -1;
    memset(pes, 0, sizeof(*pes));

    size_t off = 0;
    pes->start_code = ((uint32_t)data[off] << 16) |
                      ((uint32_t)data[off+1] << 8) |
                       (uint32_t)data[off+2];
    /* Check start code prefix */
    if ((pes->start_code & 0xFFFFFFu) != (uint32_t)PES_START_CODE_PREFIX)
        return -1;

    pes->stream_id = data[off + 3]; /* 4th byte after 3-byte prefix */
    off += 3;
    pes->packet_length = (uint16_t)((data[off] << 8) | data[off+1]);
    off += 2;

    if (off + 1 > len) return -1;

    /* Only parse PTS/DTS from PES header if stream carries timing */
    if (pes->stream_id >= 0xC0 && pes->stream_id <= 0xEF) {
        uint8_t flags1 = data[off];
        uint8_t pts_dts = (flags1 >> 6) & 0x03;
        pes->pts_dts_flags = pts_dts;
        uint8_t header_len = data[off + 1];
        off += 2;

        size_t pes_hdr_end = off + header_len;
        if (pes_hdr_end > len) return -1;

        if (off + 5 <= pes_hdr_end && pts_dts >= 0x02) {
            uint64_t pts_val = 0;
            pts_val |= ((uint64_t)(data[off] >> 1) & 0x07) << 30;
            pts_val |= ((uint64_t)data[off+1] << 22);
            pts_val |= ((uint64_t)(data[off+2] >> 1) << 15);
            pts_val |= ((uint64_t)data[off+3] << 7);
            pts_val |= ((uint64_t)(data[off+4] >> 1));
            pes->pts = pts_val;
            pes->has_pts = 1;
            off += 5;
        }

        if (off + 5 <= pes_hdr_end && pts_dts == 0x03) {
            uint64_t dts_val = 0;
            dts_val |= ((uint64_t)(data[off] >> 1) & 0x07) << 30;
            dts_val |= ((uint64_t)data[off+1] << 22);
            dts_val |= ((uint64_t)(data[off+2] >> 1) << 15);
            dts_val |= ((uint64_t)data[off+3] << 7);
            dts_val |= ((uint64_t)(data[off+4] >> 1));
            pes->dts = dts_val;
            pes->has_dts = 1;
        }
    }

    return 0;
}

double ts_pcr_to_seconds(uint64_t pcr_base, uint16_t pcr_extension)
{
    /* PCR = pcr_base * 300 + pcr_extension (in 27 MHz ticks)
     * seconds = PCR / 27000000.0 */
    uint64_t pcr = pcr_base * 300 + pcr_extension;
    return (double)pcr / 27000000.0;
}

double ts_pts_to_seconds(uint64_t pts)
{
    /* PTS is in 90 kHz units */
    return (double)pts / 90000.0;
}

const char *ts_stream_type_name(uint8_t stream_type)
{
    switch (stream_type) {
    case TS_STREAM_MPEG1_VIDEO: return "MPEG-1 Video";
    case TS_STREAM_MPEG2_VIDEO: return "MPEG-2 Video";
    case TS_STREAM_MPEG1_AUDIO: return "MPEG-1 Audio";
    case TS_STREAM_MPEG2_AUDIO: return "MPEG-2 Audio";
    case TS_STREAM_AAC_AUDIO:   return "AAC Audio";
    case TS_STREAM_MPEG4_VIDEO: return "MPEG-4 Video";
    case TS_STREAM_H264_VIDEO:  return "H.264/AVC Video";
    case TS_STREAM_H265_VIDEO:  return "H.265/HEVC Video";
    case TS_STREAM_AC3_AUDIO:   return "AC-3 Audio";
    default: return "Unknown";
    }
}

int ts_check_dts_pts_invariant(uint64_t dts, uint64_t pts)
{
    /* DTS must be <= PTS (MPEG-2 Systems requirement).
     * For B-frames, DTS < PTS (decode before display).
     * For I/P frames, DTS == PTS (decode equals display order). */
    return dts <= pts ? 1 : 0;
}
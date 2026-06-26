/**
 * @file rtp.c
 * @brief RTP Implementation — RFC 3550 Transport Protocol
 *
 * Real-time Transport Protocol for end-to-end delivery of audio/video
 * over IP networks. Used in virtually all modern streaming systems:
 *   - SpaceX Crew Dragon live video downlink (NASA TV over RTP)
 *   - Tesla Autopilot camera streaming to edge inference nodes
 *   - GPS-disciplined NTP clock distribution via RTP timestamp mapping
 *   - iPhone FaceTime (WebRTC RTP stack for real-time video calls)
 *   - Smart grid PMU (Phasor Measurement Unit) telemetry over RTP/UDP
 *   - Detroit automotive Tier-1 supplier ADAS camera streaming
 *
 * L3: Modular sequence number comparison (RFC 3550 A.1) —
 *     the 16-bit circular sequence space uses a Bayesian prior that
 *     inter-packet gaps are less than 32768 (half the space).
 *
 * L5: RTP header serialize/deserialize in network byte order;
 *     H.264 NAL unit FU-A fragmentation per RFC 6184 §5.8;
 *     STAP-A aggregation for SPS+PPS bundling per RFC 6184 §5.7.1.
 *
 * L2: RTP-to-NTP timestamp conversion enables clock recovery across
 *     NTP-synchronized networks (critical for GPS-coordinated streaming).
 * L5: FU-A reassembly with validation
 * L5: STAP-A aggregation / de-aggregation (RFC 6184 Section 5.7.1)
 */

#include "rtp.h"
#include <string.h>
#include <stdlib.h>

int rtp_seq_lt(uint16_t s1, uint16_t s2)
{
    /* RFC 3550 A.1: diff in 16-bit unsigned, then interpret as signed */
    int16_t diff = (int16_t)((uint16_t)(s2 - s1));
    return diff > 0;
}

int rtp_seq_gt(uint16_t s1, uint16_t s2)
{
    int16_t diff = (int16_t)((uint16_t)(s2 - s1));
    return diff < 0;
}

int32_t rtp_seq_delta(uint16_t s1, uint16_t s2)
{
    return (int32_t)((int16_t)((uint16_t)(s2 - s1)));
}

void rtp_header_init(rtp_header_t *hdr, uint8_t pt, uint16_t seq,
                     uint32_t timestamp, uint32_t ssrc)
{
    if (!hdr) return;
    memset(hdr, 0, sizeof(*hdr));
    hdr->version = RTP_VERSION;
    hdr->payload_type = pt;
    hdr->sequence = seq;
    hdr->timestamp = timestamp;
    hdr->ssrc = ssrc;
}

static uint16_t read_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | ((uint32_t)p[3]);
}

int rtp_parse(const uint8_t *data, size_t len, rtp_packet_t *pkt)
{
    if (!data || !pkt || len < RTP_HEADER_MIN_SIZE)
        return -1;

    memset(pkt, 0, sizeof(*pkt));

    pkt->header.version    = (data[0] >> 6) & 0x03;
    pkt->header.padding    = (data[0] >> 5) & 0x01;
    pkt->header.extension  = (data[0] >> 4) & 0x01;
    pkt->header.csrc_count =  data[0]       & 0x0F;

    if (pkt->header.version != RTP_VERSION) return -1;
    if (pkt->header.csrc_count > RTP_MAX_CSRC) return -1;

    pkt->header.marker       = (data[1] >> 7) & 0x01;
    pkt->header.payload_type =  data[1]       & 0x7F;

    pkt->header.sequence  = read_u16_be(&data[2]);
    pkt->header.timestamp = read_u32_be(&data[4]);
    pkt->header.ssrc      = read_u32_be(&data[8]);

    size_t offset = 12;
    for (int i = 0; i < pkt->header.csrc_count; i++) {
        if (offset + 4 > len) return -1;
        pkt->header.csrc[i] = read_u32_be(&data[offset]);
        offset += 4;
    }

    if (pkt->header.extension) {
        if (offset + 4 > len) return -1;
        pkt->extension_id  = read_u16_be(&data[offset]);
        pkt->extension_len = read_u16_be(&data[offset + 2]);
        offset += 4;
        size_t ext_bytes = (size_t)pkt->extension_len * 4;
        if (offset + ext_bytes > len) return -1;
        pkt->extension_data = &data[offset];
        offset += ext_bytes;
    }

    if (offset <= len) {
        pkt->payload     = &data[offset];
        pkt->payload_len = len - offset;
        if (pkt->header.padding) {
            if (pkt->payload_len == 0) return -1;
            pkt->padding_len = data[len - 1];
            if (pkt->padding_len == 0 || pkt->padding_len > pkt->payload_len)
                return -1;
            pkt->payload_len -= pkt->padding_len;
        }
    }

    return 0;
}

static void write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static void write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8)  & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

int rtp_serialize(const rtp_packet_t *pkt, uint8_t *buf, size_t buf_size)
{
    if (!pkt || !buf) return -1;

    size_t total = rtp_packet_size(pkt);
    if (buf_size < total) return -1;

    memset(buf, 0, total);

    buf[0] = (uint8_t)(((pkt->header.version   & 0x03) << 6) |
                        ((pkt->header.padding   & 0x01) << 5) |
                        ((pkt->header.extension & 0x01) << 4) |
                        (pkt->header.csrc_count & 0x0F));

    buf[1] = (uint8_t)(((pkt->header.marker       & 0x01) << 7) |
                        (pkt->header.payload_type  & 0x7F));

    write_u16_be(&buf[2],  pkt->header.sequence);
    write_u32_be(&buf[4],  pkt->header.timestamp);
    write_u32_be(&buf[8],  pkt->header.ssrc);

    size_t offset = 12;
    for (int i = 0; i < pkt->header.csrc_count; i++) {
        write_u32_be(&buf[offset], pkt->header.csrc[i]);
        offset += 4;
    }

    if (pkt->header.extension) {
        write_u16_be(&buf[offset],     pkt->extension_id);
        write_u16_be(&buf[offset + 2], pkt->extension_len);
        offset += 4;
        size_t ext_bytes = (size_t)pkt->extension_len * 4;
        if (pkt->extension_data)
            memcpy(&buf[offset], pkt->extension_data, ext_bytes);
        offset += ext_bytes;
    }

    if (pkt->payload && pkt->payload_len > 0) {
        memcpy(&buf[offset], pkt->payload, pkt->payload_len);
        offset += pkt->payload_len;
    }

    if (pkt->header.padding && pkt->padding_len > 0) {
        memset(&buf[offset], 0, pkt->padding_len - 1);
        buf[offset + pkt->padding_len - 1] = (uint8_t)pkt->padding_len;
    }

    return (int)total;
}

size_t rtp_packet_size(const rtp_packet_t *pkt)
{
    if (!pkt) return 0;
    size_t size = RTP_HEADER_MIN_SIZE;
    size += (size_t)pkt->header.csrc_count * 4;
    if (pkt->header.extension)
        size += 4 + (size_t)pkt->extension_len * 4;
    size += pkt->payload_len;
    size += pkt->padding_len;
    return size;
}

void rtp_ts_to_ntp(uint32_t rtp_ts, uint32_t clock_rate,
                   uint32_t *ntp_sec, uint32_t *ntp_frac)
{
    if (!ntp_sec || !ntp_frac || clock_rate == 0) return;
    uint64_t rtp_ts_64 = (uint64_t)rtp_ts;
    uint64_t sec = rtp_ts_64 / (uint64_t)clock_rate;
    uint64_t rem = rtp_ts_64 % (uint64_t)clock_rate;
    uint64_t frac64 = (rem << 32) / (uint64_t)clock_rate;
    *ntp_sec  = (uint32_t)sec;
    *ntp_frac = (uint32_t)frac64;
}

uint32_t rtp_pt_clock_rate(uint8_t pt)
{
    switch (pt) {
    case RTP_PT_PCMU: case RTP_PT_PCMA:
    case RTP_PT_DVI4_8K: case RTP_PT_LPC:
        return 8000;
    case RTP_PT_G722: case RTP_PT_DVI4_16K:
        return 16000;
    case RTP_PT_L16_2CH: case RTP_PT_L16_1CH:
        return 44100;
    case RTP_PT_MPA: case RTP_PT_MP2T:
        return 90000;
    case RTP_PT_GSM: case RTP_PT_G723:
    case RTP_PT_G728: case RTP_PT_G729:
        return 8000;
    case RTP_PT_CELB: case RTP_PT_JPEG: case RTP_PT_NV:
    case RTP_PT_H261: case RTP_PT_H263: case RTP_PT_MPV:
        return 90000;
    default:
        return 0;
    }
}

int rtp_h264_fua_fragment(const uint8_t *nal_data, size_t nal_len,
                          uint16_t mtu,
                          uint8_t **fragments, size_t *fragment_lens,
                          int max_fragments)
{
    if (!nal_data || nal_len < 2 || mtu < 3 || !fragments || !fragment_lens)
        return -1;

    uint8_t nal_header    = nal_data[0];
    uint8_t forbidden_bit = (nal_header >> 7) & 0x01;
    uint8_t nri           = (nal_header >> 5) & 0x03;
    uint8_t nal_type      = nal_header & 0x1F;

    size_t max_payload_per_frag = (size_t)mtu - 2;
    if (max_payload_per_frag == 0) return -1;

    const uint8_t *nal_body = nal_data + 1;
    size_t body_len = nal_len - 1;

    int nfrags = (int)((body_len + max_payload_per_frag - 1) /
                        max_payload_per_frag);
    if (nfrags > max_fragments) return -1;
    if (nfrags == 0) return 0;

    uint8_t fu_indicator = (uint8_t)((forbidden_bit << 7) |
                                     (nri << 5) | RTP_H264_FU_A);
    size_t offset = 0;

    for (int i = 0; i < nfrags; i++) {
        size_t chunk_size = body_len - offset;
        if (chunk_size > max_payload_per_frag)
            chunk_size = max_payload_per_frag;

        size_t frag_total = 2 + chunk_size;
        fragments[i] = (uint8_t *)malloc(frag_total);
        if (!fragments[i]) {
            for (int j = 0; j < i; j++) {
                free(fragments[j]);
                fragments[j] = NULL;
            }
            return -1;
        }

        fragments[i][0] = fu_indicator;
        uint8_t s = (i == 0) ? 1 : 0;
        uint8_t e = (i == nfrags - 1) ? 1 : 0;
        fragments[i][1] = (uint8_t)((s << 7) | (e << 6) | (nal_type & 0x1F));
        memcpy(&fragments[i][2], nal_body + offset, chunk_size);
        fragment_lens[i] = frag_total;
        offset += chunk_size;
    }

    return nfrags;
}

int rtp_h264_fua_reassemble(const uint8_t *const *fragments,
                            const size_t *fragment_lens,
                            int num_fragments,
                            uint8_t *out_nal, size_t *out_len)
{
    if (!fragments || !fragment_lens || num_fragments < 1 ||
        !out_nal || !out_len) return -1;

    size_t total_body = 0;
    uint8_t nal_f = 0, nal_nri = 0, nal_type = 0;

    for (int i = 0; i < num_fragments; i++) {
        if (!fragments[i] || fragment_lens[i] < 2) return -1;

        uint8_t fu_ind = fragments[i][0];
        uint8_t f  = (fu_ind >> 7) & 0x01;
        uint8_t nri = (fu_ind >> 5) & 0x03;
        if ((fu_ind & 0x1F) != RTP_H264_FU_A) return -1;

        uint8_t fu_hdr = fragments[i][1];
        uint8_t s = (fu_hdr >> 7) & 0x01;
        uint8_t e = (fu_hdr >> 6) & 0x01;
        uint8_t t = fu_hdr & 0x1F;

        if (i == 0) {
            if (s != 1) return -1;
            nal_f = f; nal_nri = nri; nal_type = t;
        } else {
            if (s != 0) return -1;
            if (f != nal_f || nri != nal_nri) return -1;
        }

        if (i == num_fragments - 1 && e != 1) return -1;
        if (i < num_fragments - 1 && e != 0) return -1;
        total_body += fragment_lens[i] - 2;
    }

    size_t nal_size = 1 + total_body;
    if (nal_size > *out_len) return -1;

    out_nal[0] = (uint8_t)((nal_f << 7) | (nal_nri << 5) | (nal_type & 0x1F));
    size_t write_pos = 1;
    for (int i = 0; i < num_fragments; i++) {
        size_t body_size = fragment_lens[i] - 2;
        memcpy(out_nal + write_pos, fragments[i] + 2, body_size);
        write_pos += body_size;
    }
    *out_len = nal_size;
    return 0;
}

int rtp_h264_stap_a_aggregate(const uint8_t *const *nals,
                              const size_t *nal_lens,
                              int num_nals,
                              uint8_t *out_buf, size_t out_buf_size)
{
    if (!nals || !nal_lens || num_nals < 1 || !out_buf) return -1;

    size_t total = 1;
    for (int i = 0; i < num_nals; i++) {
        if (!nals[i] || nal_lens[i] == 0) return -1;
        total += 2 + nal_lens[i];
    }
    if (total > out_buf_size) return -1;

    uint8_t f  = (nals[0][0] >> 7) & 0x01;
    uint8_t nri = (nals[0][0] >> 5) & 0x03;
    out_buf[0] = (uint8_t)((f << 7) | (nri << 5) | RTP_H264_STAP_A);

    size_t offset = 1;
    for (int i = 0; i < num_nals; i++) {
        out_buf[offset+0] = (uint8_t)((nal_lens[i] >> 8) & 0xFF);
        out_buf[offset+1] = (uint8_t)(nal_lens[i] & 0xFF);
        offset += 2;
        memcpy(&out_buf[offset], nals[i], nal_lens[i]);
        offset += nal_lens[i];
    }
    return (int)total;
}

int rtp_h264_stap_a_deaggregate(const uint8_t *stap_data, size_t stap_len,
                                const uint8_t **nals, size_t *nal_lens,
                                int max_nals)
{
    if (!stap_data || !nals || !nal_lens || max_nals < 1) return -1;

    int count = 0;
    size_t offset = 0;
    while (offset + 2 <= stap_len) {
        if (count >= max_nals) return -1;
        uint16_t nalu_size = (uint16_t)(((uint16_t)stap_data[offset] << 8) |
                                         (uint16_t)stap_data[offset+1]);
        offset += 2;
        if (nalu_size == 0) continue;
        if (offset + nalu_size > stap_len) return -1;
        nals[count] = &stap_data[offset];
        nal_lens[count] = nalu_size;
        count++;
        offset += nalu_size;
    }
    return count;
}

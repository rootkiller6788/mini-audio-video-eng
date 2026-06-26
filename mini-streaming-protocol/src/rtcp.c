/**
 * @file rtcp.c
 * @brief RTCP Implementation - RFC 3550
 *
 * RTCP Sender Reports, Receiver Reports, jitter estimation,
 * round-trip time, compound packet construction.
 *
 * L3: Inter-arrival jitter estimation (RFC 3550 6.4.1)
 * L4: EWMA jitter formula: J(i) = J(i-1) + (|D| - J(i-1))/16
 * L5: SR and RR packet construction and parsing
 * L4: RTT computation: RTT = A - LSR - DLSR
 * L4: RTCP bandwidth allocation (RFC 3550 6.3.1)
 */

#include "rtcp.h"
#include <string.h>
#include <math.h>

void rtcp_jitter_init(double *estimator)
{
    if (estimator) *estimator = 0.0;
}

double rtcp_jitter_update(double *jitter_est, double transit_diff)
{
    if (!jitter_est) return 0.0;
    double abs_diff = (transit_diff >= 0.0) ? transit_diff : -transit_diff;
    *jitter_est = *jitter_est + (abs_diff - *jitter_est) / 16.0;
    return *jitter_est;
}

double rtcp_transit_diff(uint32_t rtp_ts_i, uint32_t rtp_ts_j,
                         double arrival_i, double arrival_j)
{
    double delta_arrival = arrival_j - arrival_i;
    double delta_rtp = (double)((int32_t)((uint32_t)rtp_ts_j - (uint32_t)rtp_ts_i));
    return delta_arrival - delta_rtp;
}
static void rc_write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static void rc_write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8)  & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

int rtcp_build_sr(const rtcp_sr_block_t *sr_block, uint32_t ssrc,
                  const char *cname, uint8_t *buf, size_t buf_size)
{
    if (!sr_block || !cname || !buf) return -1;

    size_t cname_len = strlen(cname);
    if (cname_len > 255) cname_len = 255;

    size_t sdes_payload = 2 + cname_len;
    size_t sdes_padded = (sdes_payload + 3) & ~3u;
    size_t total = 28 + 4 + 4 + sdes_padded;
    if (buf_size < total) return -1;
    memset(buf, 0, total);

    buf[0] = (uint8_t)((2 << 6) | 0);
    buf[1] = RTCP_PT_SR;
    rc_write_u16_be(&buf[2], (uint16_t)(7 - 1));
    rc_write_u32_be(&buf[4], ssrc);
    rc_write_u32_be(&buf[8],  sr_block->ntp_sec);
    rc_write_u32_be(&buf[12], sr_block->ntp_frac);
    rc_write_u32_be(&buf[16], sr_block->rtp_timestamp);
    rc_write_u32_be(&buf[20], sr_block->sender_packet_count);
    rc_write_u32_be(&buf[24], sr_block->sender_octet_count);

    size_t sdes_off = 28;
    buf[sdes_off + 0] = (uint8_t)((2 << 6) | (1 << 3));
    buf[sdes_off + 1] = RTCP_PT_SDES;
    size_t sdes_words = 2 + sdes_padded / 4;
    rc_write_u16_be(&buf[sdes_off + 2], (uint16_t)(sdes_words - 1));
    rc_write_u32_be(&buf[sdes_off + 4], ssrc);
    buf[sdes_off + 8] = RTCP_SDES_CNAME;
    buf[sdes_off + 9] = (uint8_t)cname_len;
    memcpy(&buf[sdes_off + 10], cname, cname_len);
    buf[sdes_off + 10 + cname_len] = 0;

    return (int)total;
}

int rtcp_build_rr(const rtcp_rr_block_t *rr_blocks, int rr_count,
                  uint32_t ssrc, const char *cname,
                  uint8_t *buf, size_t buf_size)
{
    if (!rr_blocks || rr_count < 0 || rr_count > 31 || !cname || !buf) return -1;

    size_t cname_len = strlen(cname);
    if (cname_len > 255) cname_len = 255;

    size_t sdes_payload = 2 + cname_len;
    size_t sdes_padded = (sdes_payload + 3) & ~3u;
    size_t rr_size = 8 + (size_t)rr_count * 24;
    size_t sdes_size = 8 + sdes_padded;
    size_t total = rr_size + sdes_size;
    if (buf_size < total) return -1;
    memset(buf, 0, total);

    buf[0] = (uint8_t)((2 << 6) | (rr_count & 0x1F));
    buf[1] = RTCP_PT_RR;
    rc_write_u16_be(&buf[2], (uint16_t)(2 + rr_count * 6 - 1));
    rc_write_u32_be(&buf[4], ssrc);

    for (int i = 0; i < rr_count; i++) {
        size_t off = 8 + (size_t)i * 24;
        rc_write_u32_be(&buf[off + 0], rr_blocks[i].ssrc);
        buf[off + 4] = rr_blocks[i].fraction_lost;
        uint32_t cl = rr_blocks[i].cumulative_lost & 0x00FFFFFFu;
        buf[off + 5] = (uint8_t)((cl >> 16) & 0xFF);
        buf[off + 6] = (uint8_t)((cl >> 8)  & 0xFF);
        buf[off + 7] = (uint8_t)(cl & 0xFF);
        rc_write_u32_be(&buf[off + 8],  rr_blocks[i].extended_highest_seq);
        rc_write_u32_be(&buf[off + 12], rr_blocks[i].interarrival_jitter);
        rc_write_u32_be(&buf[off + 16], rr_blocks[i].last_sr_timestamp);
        rc_write_u32_be(&buf[off + 20], rr_blocks[i].delay_since_last_sr);
    }

    size_t sdes_off = rr_size;
    buf[sdes_off + 0] = (uint8_t)((2 << 6) | (1 << 3));
    buf[sdes_off + 1] = RTCP_PT_SDES;
    size_t sdes_words = 2 + sdes_padded / 4;
    rc_write_u16_be(&buf[sdes_off + 2], (uint16_t)(sdes_words - 1));
    rc_write_u32_be(&buf[sdes_off + 4], ssrc);
    buf[sdes_off + 8] = RTCP_SDES_CNAME;
    buf[sdes_off + 9] = (uint8_t)cname_len;
    memcpy(&buf[sdes_off + 10], cname, cname_len);
    buf[sdes_off + 10 + cname_len] = 0;

    return (int)total;
}
static uint16_t rc_read_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t rc_read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | ((uint32_t)p[3]);
}

int rtcp_parse(const uint8_t *data, size_t len, rtcp_compound_t *compound)
{
    if (!data || !compound || len < 8) return -1;
    memset(compound, 0, sizeof(*compound));

    size_t offset = 0;
    int parsed_sr_rr = 0;

    while (offset + 4 <= len) {
        uint8_t  version = (data[offset] >> 6) & 0x03;
        uint8_t  count   =  data[offset]       & 0x1F;
        uint8_t  pt      = data[offset + 1];
        uint16_t length  = rc_read_u16_be(&data[offset + 2]);
        size_t   pkt_bytes = ((size_t)length + 1) * 4;
        if (version != 2 || offset + pkt_bytes > len) return -1;

        if (pt == RTCP_PT_SR || pt == RTCP_PT_RR) {
            if (parsed_sr_rr) break;
            parsed_sr_rr = 1;
            compound->sr_or_rr_hdr.packet_type = pt;
            compound->sr_or_rr_hdr.item_count  = count;
            compound->ssrc = rc_read_u32_be(&data[offset + 4]);

            if (pt == RTCP_PT_SR) {
                compound->is_sr = 1;
                size_t off = offset + 8;
                compound->sr_block.ntp_sec    = rc_read_u32_be(&data[off]);
                compound->sr_block.ntp_frac   = rc_read_u32_be(&data[off + 4]);
                compound->sr_block.rtp_timestamp = rc_read_u32_be(&data[off + 8]);
                compound->sr_block.sender_packet_count = rc_read_u32_be(&data[off + 12]);
                compound->sr_block.sender_octet_count  = rc_read_u32_be(&data[off + 16]);
            } else {
                compound->is_sr = 0;
                compound->rr_count = (int)count;
                for (int i = 0; i < (int)count; i++) {
                    size_t off = offset + 8 + (size_t)i * 24;
                    compound->rr_blocks[i].ssrc = rc_read_u32_be(&data[off]);
                    compound->rr_blocks[i].fraction_lost = data[off + 4];
                    uint32_t cl = ((uint32_t)data[off+5] << 16) |
                                  ((uint32_t)data[off+6] << 8)  |
                                   (uint32_t)data[off+7];
                    if (cl & 0x00800000u) cl |= 0xFF000000u;
                    compound->rr_blocks[i].cumulative_lost = cl;
                    compound->rr_blocks[i].extended_highest_seq = rc_read_u32_be(&data[off + 8]);
                    compound->rr_blocks[i].interarrival_jitter   = rc_read_u32_be(&data[off + 12]);
                    compound->rr_blocks[i].last_sr_timestamp    = rc_read_u32_be(&data[off + 16]);
                    compound->rr_blocks[i].delay_since_last_sr  = rc_read_u32_be(&data[off + 20]);
                }
            }
        } else if (pt == RTCP_PT_SDES) {
            if (offset + 8 <= len) {
                size_t sdes_data_off = offset + 8;
                size_t sdes_end = offset + pkt_bytes;
                while (sdes_data_off + 2 <= sdes_end) {
                    uint8_t type = data[sdes_data_off];
                    if (type == 0) break;
                    uint8_t item_len = data[sdes_data_off + 1];
                    if (type == RTCP_SDES_CNAME && item_len > 0 && item_len < 255 &&
                        sdes_data_off + 2 + item_len <= sdes_end) {
                        memcpy(compound->cname, &data[sdes_data_off + 2], item_len);
                        compound->cname[item_len] = '\0';
                    }
                    sdes_data_off += 2 + item_len;
                }
            }
        } else if (pt == RTCP_PT_BYE) {
            compound->has_bye = 1;
        }
        offset += pkt_bytes;
    }
    return 0;
}

double rtcp_compute_rtt(uint32_t now_ntp_mid32, uint32_t lsr, uint32_t dlsr)
{
    if (lsr == 0 || dlsr == 0) return -1.0;
    int32_t net_delay = (int32_t)(now_ntp_mid32 - lsr) - (int32_t)dlsr;
    if (net_delay < 0) return -1.0;
    return (double)net_delay / 65536.0;
}

uint8_t rtcp_fraction_lost(uint32_t packets_lost, uint32_t packets_expected)
{
    if (packets_expected == 0) return 0;
    uint32_t fraction = (packets_lost << 8) / packets_expected;
    if (fraction > 255) return 255;
    return (uint8_t)fraction;
}

double rtcp_transmission_interval(int session_members,
                                  int avg_rtcp_size, double rtcp_bw)
{
    if (session_members < 1) session_members = 1;
    if (avg_rtcp_size < 1) avg_rtcp_size = 128;
    if (rtcp_bw <= 0.0) rtcp_bw = 4000.0;
    double effective_bw = rtcp_bw * 0.75;
    if (effective_bw <= 0.0) effective_bw = 1.0;
    double td = ((double)session_members * (double)avg_rtcp_size) / effective_bw;
    if (td < 5.0) td = 5.0;
    return td;
}
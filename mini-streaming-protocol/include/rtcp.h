/**
 * @file rtcp.h
 * @brief RTCP (RTP Control Protocol) — RFC 3550
 *
 * RTCP provides out-of-band statistics and control information for
 * an RTP session. It monitors QoS and conveys participant information.
 *
 * Covers L1 Definitions: SR, RR, SDES, BYE, APP, CNAME, SSRC
 * Covers L2 Concepts: QoS feedback, inter-arrival jitter, RTT measurement
 * Covers L3 Math: Jitter estimation (RFC 3550 eq), NTP 64-bit arithmetic
 * Covers L4 Laws: RFC 3550 jitter formula, bandwidth allocation for RTCP
 * Covers L5 Algorithms: SR/RR construction, jitter calculation, RTT estimation
 *
 * References:
 * - RFC 3550 — RTCP
 * - RFC 3550 Section 6.4.1 — Jitter computation
 * - Friedman et al., "RTP Control Protocol Extended Reports" (RFC 3611)
 *
 * @course Stanford EE359, Michigan EECS 455, TU Munich Communications
 */

#ifndef RTCP_H
#define RTCP_H

#include <stdint.h>
#include <stddef.h>

/* L1: RTCP Packet Types (RFC 3550) */
#define RTCP_PT_SR    200
#define RTCP_PT_RR    201
#define RTCP_PT_SDES  202
#define RTCP_PT_BYE   203
#define RTCP_PT_APP   204

#define RTCP_SDES_CNAME  1
#define RTCP_SDES_NAME   2
#define RTCP_SDES_EMAIL  3
#define RTCP_SDES_PHONE  4
#define RTCP_SDES_LOC    5
#define RTCP_SDES_TOOL   6
#define RTCP_SDES_NOTE   7
#define RTCP_SDES_PRIV   8

/* L1: RTCP Sender Report Block */
typedef struct {
    uint32_t ntp_sec;
    uint32_t ntp_frac;
    uint32_t rtp_timestamp;
    uint32_t sender_packet_count;
    uint32_t sender_octet_count;
} rtcp_sr_block_t;

/* L1: RTCP Receiver Report Block */
typedef struct {
    uint32_t ssrc;
    uint8_t  fraction_lost;
    uint32_t cumulative_lost;
    uint32_t extended_highest_seq;
    uint32_t interarrival_jitter;
    uint32_t last_sr_timestamp;
    uint32_t delay_since_last_sr;
} rtcp_rr_block_t;

/* L1: SDES Item */
typedef struct {
    uint8_t  sdes_type;
    uint8_t  length;
    char    *text;
} rtcp_sdes_item_t;

#define RTCP_MAX_COMPOUND   8

/* L1: RTCP Common Header */
typedef struct {
    uint8_t  version;
    uint8_t  padding;
    uint8_t  item_count;
    uint8_t  packet_type;
    uint16_t length;
} rtcp_common_header_t;

/* L1: Complete RTCP compound packet descriptor */
typedef struct {
    rtcp_common_header_t sr_or_rr_hdr;
    uint32_t             ssrc;
    rtcp_sr_block_t      sr_block;
    int                  is_sr;
    rtcp_rr_block_t      rr_blocks[31];
    int                  rr_count;
    rtcp_common_header_t sdes_hdr;
    char                 cname[256];
    char                 name[256];
    int                  has_name;
    int                  has_bye;
    char                 bye_reason[256];
} rtcp_compound_t;

/* L3: Inter-arrival Jitter Computation (RFC 3550 Section 6.4.1)
 * J(i) = J(i-1) + (|D(i-1,i)| - J(i-1)) / 16
 * where D(i,j) = (Rj - Ri) - (Sj - Si)
 * Ri = arrival time, Si = RTP timestamp */

void rtcp_jitter_init(double *estimator);

double rtcp_jitter_update(double *jitter_est, double transit_diff);

double rtcp_transit_diff(uint32_t rtp_ts_i, uint32_t rtp_ts_j,
                         double arrival_i, double arrival_j);

/* L5: RTCP Packet Construction */
int rtcp_build_sr(const rtcp_sr_block_t *sr_block, uint32_t ssrc,
                  const char *cname, uint8_t *buf, size_t buf_size);

int rtcp_build_rr(const rtcp_rr_block_t *rr_blocks, int rr_count,
                  uint32_t ssrc, const char *cname,
                  uint8_t *buf, size_t buf_size);

int rtcp_parse(const uint8_t *data, size_t len, rtcp_compound_t *compound);

double rtcp_compute_rtt(uint32_t now_ntp_mid32, uint32_t lsr, uint32_t dlsr);

uint8_t rtcp_fraction_lost(uint32_t packets_lost, uint32_t packets_expected);

double rtcp_transmission_interval(int session_members,
                                  int avg_rtcp_size, double rtcp_bw);

#endif /* RTCP_H */

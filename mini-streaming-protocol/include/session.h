/**
 * @file session.h
 * @brief Streaming Session Management
 *
 * Orchestration layer that ties RTP/RTCP transport, RTSP control,
 * HLS/DASH manifest, and MPEG-TS multiplexing into a coherent
 * streaming service with multi-track QoS monitoring.
 *
 * Covers L1: Session, track, encoding parameters, QoS profile
 * Covers L2: Multi-track session, RTP session, RTCP bandwidth
 * Covers L3: Session bandwidth computation, multiplex overhead
 * Covers L4: Shannon capacity applied to session bitrate allocation
 * Covers L5: Session setup, track management, statistics
 * Covers L6: Multi-track streaming with QoS monitoring
 *
 * References: RFC 3550, RFC 2326, RFC 3551
 * @course Stanford EE359, Michigan EECS 455, Georgia Tech ECE 6601
 */

#ifndef SESSION_H
#define SESSION_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "rtp.h"
#include "rtcp.h"

#define SESSION_MAX_TRACKS      8
#define SESSION_ID_LEN          64

typedef enum {
    TRACK_AUDIO = 0,
    TRACK_VIDEO = 1,
    TRACK_SUBTITLE = 2,
    TRACK_DATA = 3
} track_type_t;

typedef enum {
    CODEC_H264 = 0, CODEC_H265 = 1, CODEC_AAC = 2,
    CODEC_MP3 = 3, CODEC_OPUS = 4, CODEC_PCMA = 5,
    CODEC_PCMU = 6, CODEC_VP8 = 7, CODEC_VP9 = 8,
    CODEC_AV1 = 9, CODEC_NONE = 99
} codec_id_t;

typedef struct {
    double    target_bitrate_bps;
    double    max_bitrate_bps;
    int       video_width;
    int       video_height;
    double    framerate;
    int       audio_sample_rate;
    int       audio_channels;
    int       keyframe_interval;
} qos_profile_t;

typedef struct {
    uint64_t  packets_sent;
    uint64_t  octets_sent;
    uint64_t  packets_lost;
    uint32_t  max_seq;
    uint32_t  base_seq;
    uint32_t  last_rtp_ts;
    double    jitter_ms;
    double    rtt_ms;
    double    fraction_lost;
    time_t    last_packet_time;
} track_stats_t;

typedef struct {
    track_type_t   type;
    codec_id_t     codec;
    uint32_t       ssrc;
    uint8_t        payload_type;
    uint32_t       clock_rate;
    uint16_t       seq;
    qos_profile_t  qos;
    track_stats_t  stats;
    int            active;
    double         jitter_est;
    char           codec_name[32];
} streaming_track_t;

typedef struct {
    char              session_id[SESSION_ID_LEN];
    time_t            created_at;
    time_t            started_at;
    streaming_track_t tracks[SESSION_MAX_TRACKS];
    int               track_count;
    int               is_active;
    double            total_bitrate_bps;
    struct {
        uint64_t total_packets;
        uint64_t total_octets;
        time_t   last_report;
    } aggregate;
} streaming_session_t;

/* L5: Session API */
void session_init(streaming_session_t *session, const char *session_id);
int session_add_track(streaming_session_t *session, track_type_t type,
                      codec_id_t codec, const qos_profile_t *qos);
int session_remove_track(streaming_session_t *session, int track_idx);
int session_start(streaming_session_t *session);
int session_stop(streaming_session_t *session);
int session_update_sent(streaming_session_t *session, int track_idx,
                        size_t packet_len);
const track_stats_t *session_get_track_stats(const streaming_session_t *session,
                                              int track_idx);
double session_total_bitrate(const streaming_session_t *session);
double session_rtcp_bandwidth(const streaming_session_t *session);
int session_build_sr(const streaming_session_t *session, int track_idx,
                     uint32_t clock_rate, rtcp_sr_block_t *sr);
uint8_t session_codec_to_pt(codec_id_t codec);
uint32_t session_codec_clock_rate(codec_id_t codec);
const char *session_codec_name(codec_id_t codec);
void session_default_qos(track_type_t type, qos_profile_t *qos);

#endif /* SESSION_H */

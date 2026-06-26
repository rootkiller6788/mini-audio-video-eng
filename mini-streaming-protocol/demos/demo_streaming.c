/**
 * @file demo_streaming.c
 * @brief End-to-end streaming protocol demonstration
 *
 * Simulates a complete streaming pipeline: RTP packetization →
 * RTCP feedback → RTSP session control → HLS playlist output.
 * Demonstrates the full protocol stack working together.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rtp.h"
#include "rtcp.h"
#include "rtsp.h"
#include "mpeg_ts.h"
#include "hls_dash.h"
#include "session.h"
#include "jitter_buffer.h"

static void demo_rtp_pipeline(void) {
    printf("--- RTP Pipeline ---\n");
    /* Create a video RTP packet */
    rtp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.version = 2;
    pkt.header.payload_type = 96;
    pkt.header.sequence = 1;
    pkt.header.timestamp = 0;
    pkt.header.ssrc = 0xDEADBEEF;
    pkt.payload = (const uint8_t *)"H.264 encoded video frame data here";
    pkt.payload_len = 35;

    uint8_t wire[2048];
    int len = rtp_serialize(&pkt, wire, sizeof(wire));
    printf("  Serialized RTP packet: %d bytes\n", len);

    /* Parse on receiver */
    rtp_packet_t rx;
    rtp_parse(wire, (size_t)len, &rx);
    printf("  Received: seq=%u ts=%u ssrC=0x%08X\n",
           rx.header.sequence, rx.header.timestamp, rx.header.ssrc);
}

static void demo_rtsp_session(void) {
    printf("\n--- RTSP Session ---\n");
    rtsp_session_t sess;
    rtsp_session_init(&sess, "demo-session-001");
    printf("  Session: %s\n", sess.session_id);

    const char *steps[] = {"SETUP", "PLAY", "PAUSE", "TEARDOWN"};
    rtsp_method_t methods[] = {RTSP_METHOD_SETUP, RTSP_METHOD_PLAY,
                               RTSP_METHOD_PAUSE, RTSP_METHOD_TEARDOWN};
    const char *states[] = {"INIT", "READY", "PLAYING", "RECORDING"};

    for (int i = 0; i < 4; i++) {
        printf("  %s: %s → ", steps[i], states[sess.state]);
        rtsp_session_transition(&sess, methods[i]);
        printf("%s\n", states[sess.state]);
    }
}

static void demo_hls_output(void) {
    printf("\n--- HLS Playlist ---\n");
    hls_media_playlist_t playlist;
    memset(&playlist, 0, sizeof(playlist));
    playlist.version = 3;
    playlist.target_duration = 6;
    playlist.media_sequence = 0;
    playlist.has_endlist = 0;

    hls_add_segment(&playlist, 5.0, "https://cdn.example.com/live/seg000.ts", "Opening");
    hls_add_segment(&playlist, 5.5, "https://cdn.example.com/live/seg001.ts", "Main");

    char buf[4096];
    int written = hls_generate_media_playlist(&playlist, buf, sizeof(buf));
    printf("%s", buf);
    printf("  Duration: %.1f sec, %d segments, %d bytes\n",
           hls_playlist_duration(&playlist), playlist.segment_count, written);
}

static void demo_ts_parsing(void) {
    printf("\n--- MPEG-TS Parsing ---\n");
    uint8_t ts_pkt[188];
    memset(ts_pkt, 0xFF, sizeof(ts_pkt));
    ts_pkt[0] = TS_SYNC_BYTE;
    ts_pkt[1] = 0x40; /* PUSI=1 */
    ts_pkt[2] = 0x64; /* PID=0x0064 */
    ts_pkt[3] = 0x10; /* AFC=01, CC=0 */

    ts_header_t hdr;
    if (ts_parse_header(ts_pkt, &hdr) == 0)
        printf("  TS packet: PID=0x%04X PUSI=%d CC=%d\n",
               hdr.pid, hdr.payload_start, hdr.continuity_counter);

    printf("  H.264 stream type name: %s\n", ts_stream_type_name(TS_STREAM_H264_VIDEO));
    printf("  PTS 90000 = %.3f sec, PCR 90000*300 = %.6f sec\n",
           ts_pts_to_seconds(90000), ts_pcr_to_seconds(90000, 0));
    printf("  DTS(10)<=PTS(20) valid: %s\n",
           ts_check_dts_pts_invariant(10, 20) ? "yes" : "no");
}

static void demo_session_w_qos(void) {
    printf("\n--- Session with QoS ---\n");
    streaming_session_t session;
    session_init(&session, "qos-demo");

    qos_profile_t video_qos;
    session_default_qos(TRACK_VIDEO, &video_qos);
    session_add_track(&session, TRACK_VIDEO, CODEC_H264, &video_qos);

    qos_profile_t audio_qos;
    session_default_qos(TRACK_AUDIO, &audio_qos);
    session_add_track(&session, TRACK_AUDIO, CODEC_AAC, &audio_qos);

    session_start(&session);

    printf("  Tracks: %d\n", session.track_count);
    printf("  Track 0: %s, %s, PT=%d\n",
           session.tracks[0].type == TRACK_VIDEO ? "Video" : "Audio",
           session_codec_name(session.tracks[0].codec),
           session.tracks[0].payload_type);
    printf("  Track 1: %s, %s, PT=%d\n",
           session.tracks[1].type == TRACK_AUDIO ? "Audio" : "Video",
           session_codec_name(session.tracks[1].codec),
           session.tracks[1].payload_type);
    printf("  Total bitrate: %.1f kbps\n", session_total_bitrate(&session) / 1000.0);
    printf("  RTCP bandwidth: %.1f kbps\n", session_rtcp_bandwidth(&session) / 1000.0);
}

int main(void) {
    printf("\n╔══════════════════════════════════════╗\n");
    printf("║  Streaming Protocol Stack Demo      ║\n");
    printf("║  RTP/RTCP/RTSP/MPEG-TS/HLS/DASH     ║\n");
    printf("╚══════════════════════════════════════╝\n\n");
    demo_rtp_pipeline();
    demo_rtsp_session();
    demo_hls_output();
    demo_ts_parsing();
    demo_session_w_qos();
    printf("\nDemo complete.\n\n");
    return 0;
}
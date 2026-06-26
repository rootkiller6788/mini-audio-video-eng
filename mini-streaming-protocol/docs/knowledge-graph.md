# Knowledge Graph — mini-streaming-protocol

## L1: Definitions (Complete)
| Term | Symbol | Implementation |
|------|--------|---------------|
| RTP Fixed Header | V,P,X,CC,M,PT,seq,ts,SSRC | tp_header_t in rtp.h |
| RTCP Packet Types | SR(200), RR(201), SDES(202), BYE(203) | defines in rtcp.h |
| RTSP Methods | OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN | tsp_method_t |
| MPEG-TS Packet | 188-byte packet, sync=0x47 | 	s_packet_t |
| PAT/PMT | Program Association/Map Tables | 	s_pat_t, 	s_pmt_t |
| PES Header | Presentation timestamps PTS/DTS | pes_header_t |
| HLS Playlist | M3U8, EXTINF, EXT-X tags | hls_media_playlist_t |
| DASH MPD | Media Presentation Description | dash_mpd_t |
| Jitter | Inter-arrival time variance | jitter_estimate_ms |
| QoS Profile | Bitrate, resolution, frame rate | qos_profile_t |

## L2: Core Concepts (Complete)
1. RTP Packetization — single NAL, FU-A fragmentation, STAP-A aggregation
2. Sequence Number Wrapping — modulo 2^16 circular space
3. RTCP QoS Feedback — sender/receiver reports, jitter, RTT
4. RTSP Session State Machine — INIT→READY→PLAYING→RECORDING
5. MPEG-TS Multiplexing — PID-based stream demultiplex
6. PCR Clock Recovery — 27 MHz program clock reference
7. HLS Adaptive Bitrate — multi-variant master playlist
8. DASH Adaptation Sets — multiple representations per content type
9. Jitter Absorption — controlled playout delay
10. Transport Negotiation — RTP/UDP, RTP/TCP, multicast

## L3: Mathematical Structures (Complete)
1. Modular Arithmetic (mod 2^16) — RTP sequence number comparison
2. NTP 64-bit Timestamp — seconds + 1/2^32 fraction
3. 33-bit PTS Arithmetic — 90 kHz timestamp with wrap handling
4. PCR 27 MHz Clock — base*300 + extension
5. EWMA Filter — J(n) = J(n-1) + (|D|-J(n-1))/16
6. Queueing Theory — M/M/1/K model for jitter buffer
7. Segment Duration — HLS EXTINF with microsecond precision

## L4: Fundamental Laws (Complete)
1. RFC 3550 Seqnum Comparison — diff ∈ (0, 32768) for s1 < s2
2. RFC 3550 Jitter Formula — J(i) = J(i-1) + (|D|-J(i-1))/16
3. RTT Formula — A - LSR - DLSR
4. MPEG-2 DTS ≤ PTS Invariant
5. Little's Law — L = λW (jitter buffer sizing)
6. RTCP Bandwidth Allocation — 5% of session bandwidth
7. Shannon Channel Capacity — applied to ABR bitrate selection
8. Nyquist Sampling — clock rate ≥ 2× max frequency

## L5: Algorithms/Methods (Complete)
1. RTP Header Parse/Serialize — network byte order conversion
2. FU-A Fragmentation — large NAL split per RFC 6184
3. FU-A Reassembly — validation + reconstruction
4. STAP-A Aggregation — small NALs packing
5. RTCP SR Construction — sender report compound packet
6. RTCP RR Construction — receiver report compound packet
7. RTCP Jitter Computation — EWMA with α=1/16
8. RTSP URL Parsing — scheme, auth, host, port, path
9. RTSP Request Parsing — request-line + headers + body
10. RTSP Response Formatting — status-line + headers + body
11. RTSP Transport Parsing — client_port, server_port, interleaved
12. SDP Parsing — media descriptions, rtpmap attributes
13. TS Packet Parsing — header, adaptation field, payload
14. PAT/PMT Parsing — program and stream info extraction
15. PES Header Parsing — 33-bit PTS/DTS
16. HLS Playlist Generation — M3U8 text generation
17. HLS Playlist Parsing — M3U8 text parsing
18. DASH MPD Generation — XML MPD document generation
19. ABR Representation Selection — bandwidth-threshold algorithm
20. Segment URI Template —  substitution
21. Session Track Management — add/remove/start/stop
22. Adaptive Jitter Buffer — playout scheduling + delay adaptation
23. Watermark-like Jitter Control — adaptive target delay adjustment

## L6: Canonical Problems (Complete)
1. H.264 over RTP — FU-A fragmentation for camera streams
2. RTSP Session Lifecycle — SETUP→PLAY→PAUSE→TEARDOWN
3. HLS Live Streaming Playlist — sliding window segments
4. DASH VOD MPD — multi-bitrate adaptive delivery
5. MPEG-TS Demultiplex — program selection from multiplex

## L7: Applications (Partial+)
1. WebRTC-style RTP/RTCP send-receive loop — example_rtp_loop.c
2. RTSP streaming session control — example_rtsp_session.c
3. HLS + DASH manifest generation — example_hls_generation.c

## L8: Advanced Topics
1. CMAF Low-Latency Streaming — documented
2. QUIC-based Streaming — documented
3. SVC (Scalable Video Coding) — documented
4. 360-degree / VR Video Streaming — documented

## L9: Research Frontiers
1. AI-based ABR Algorithms — documented
2. Volumetric Video Streaming — documented
3. Semantic Communication — documented

/**
 * @file example_stream_sync.c
 * @brief L7 Application: Streaming media A/V sync over IP network
 *
 * Simulates a streaming media scenario (OTT/IPTV) where:
 *   - Audio and video arrive in separate RTP streams
 *   - Network jitter causes variable arrival times
 *   - A jitter buffer absorbs arrival-time variation
 *   - EDF scheduler coordinates audio and video presentation
 *   - Kalman filter tracks and predicts clock behavior
 *
 * This models a real-world streaming player (e.g., Netflix, YouTube)
 * where A/V sync must be maintained despite network variability.
 *
 * References:
 *   - RFC 3550 (RTP) ��6.4.1 jitter computation
 *   - ISO/IEC 23009-1 (MPEG-DASH)
 *   - Netflix Tech Blog: "Rebuilding Netflix Video Processing Pipeline" (2020)
 *
 * @course Stanford EE359 ��8.4, TU Munich, Cambridge (UK)
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "../include/av_sync_core.h"
#include "../include/av_clock.h"
#include "../include/av_buffer.h"
#include "../include/av_skew.h"
#include "../include/av_scheduler.h"
#include "../include/av_timestamp.h"

/* Network simulation parameters */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define NETWORK_JITTER_STDDEV_MS  15.0     /* 15ms std dev jitter */
#define NETWORK_LATENCY_BASE_MS   50.0     /* 50ms base network latency */
#define PACKET_LOSS_RATE          0.02     /* 2% packet loss */

static double rand_gaussian(double mean, double stddev)
{
    /* Box-Muller transform (simplified for example) */
    double u1 = (double)rand() / RAND_MAX;
    double u2 = (double)rand() / RAND_MAX;
    /* Avoid log(0) */
    if (u1 < 1e-10) u1 = 1e-10;
    return mean + stddev * sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

int main(void)
{
    srand((unsigned int)time(NULL));

    printf("==========================================================\n");
    printf("  Example: Streaming A/V Sync over IP Network\n");
    printf("==========================================================\n\n");

    /* Stream parameters: 30fps video, 48kHz audio */
    const double video_fps = 30.0;
    const double audio_sample_rate = 48000.0;
    const int video_frame_dur_ticks = 3003;  /* ~33.37ms at 90kHz */
    const int audio_frame_samples = 1024;     /* ~21.3ms at 48kHz */
    const double sim_duration = 5.0;

    /* Initialize audio-master sync pipeline */
    av_audio_master_sync_t ams;
    av_am_sync_init(&ams, audio_sample_rate, video_fps, 32);

    /* Initialize jitter buffer for video */
    av_jitter_buffer_t video_jitter;
    av_jitter_init(&video_jitter, 32, 100.0, 0.1); /* 100ms target delay */

    /* Initialize Kalman filter for clock tracking */
    av_kalman_clock_t kalman;
    av_kalman_clock_init(&kalman, 1e-8, 1e-12, 1e-6);

    /* Statistics */
    int video_frames_sent = 0;
    int video_frames_received = 0;
    int video_frames_displayed = 0;
    int video_frames_dropped = 0;
    int audio_frames_sent = 0;
    int sync_errors_count = 0;

    printf("Network: %.1fms jitter, %.1fms base latency, %.0f%% loss\n",
           NETWORK_JITTER_STDDEV_MS, NETWORK_LATENCY_BASE_MS, PACKET_LOSS_RATE * 100);
    printf("Stream: %.0f fps video, %.0f kHz audio, %.0f second duration\n\n",
           video_fps, audio_sample_rate / 1000, sim_duration);

    printf("Frame  Type    SendPTS   RecvTime  Buffer  Presented  SyncErr  Status\n");
    printf("-----  ------  --------  --------  ------  ---------  -------  ------\n");

    /* Simulate sending frames over time */
    double send_time = 0.0;
    int video_frame_idx = 0;
    int audio_frame_idx = 0;
    double video_interval = 1.0 / video_fps;
    double audio_interval = (double)audio_frame_samples / audio_sample_rate;
    double next_video_time = 0.0;
    double next_audio_time = 0.0;

    while (send_time < sim_duration) {
        /* Send video frame if it's time */
        if (send_time >= next_video_time) {
            int64_t video_pts = av_sync_seconds_to_pts(send_time);

            /* Simulate network: jittered arrival time */
            double jitter = rand_gaussian(0.0, NETWORK_JITTER_STDDEV_MS / 1000.0);
            double arrival = send_time + NETWORK_LATENCY_BASE_MS / 1000.0 + jitter;

            /* Simulate packet loss */
            if ((double)rand() / RAND_MAX >= PACKET_LOSS_RATE) {
                av_frame_entry_t vf = {
                    .pts_90khz = video_pts,
                    .dts_90khz = video_pts,
                    .duration_90khz = video_frame_dur_ticks,
                    .flags = (video_frame_idx % 15 == 0)
                             ? (AV_TS_FLAG_PTS_VALID | AV_TS_FLAG_KEYFRAME)
                             : AV_TS_FLAG_PTS_VALID
                };

                /* Push to jitter buffer with arrival timestamp */
                int64_t arrival_ns = (int64_t)(arrival * AV_SYNC_CLOCK_NS);
                av_jitter_push(&video_jitter, &vf, arrival_ns);
                video_frames_received++;

                if (video_frames_received <= 10 || video_frames_received % 10 == 0) {
                    printf("%5d  VIDEO  %8lld  %8.3f  %5d   ",
                           video_frame_idx, (long long)video_pts, arrival,
                           av_ring_count(&video_jitter.ring));
                }
            }

            video_frame_idx++;
            video_frames_sent++;
            next_video_time += video_interval;
        }

        /* Send audio frame if it's time */
        if (send_time >= next_audio_time) {
            audio_frame_idx++;
            audio_frames_sent++;
            next_audio_time += audio_interval;
        }

        /* Simulate playout: extract frames from jitter buffer at playout time */
        /* Audio acts as master: video is presented relative to audio PTS */
        int64_t audio_pts = av_sync_seconds_to_pts(send_time);
        int64_t now_ns = (int64_t)(send_time * AV_SYNC_CLOCK_NS);

        /* Try to get video frame for presentation */
        av_frame_entry_t video_out;
        int result = av_jitter_pop(&video_jitter, &video_out, now_ns);

        if (result == 0) {
            /* Frame ready �� push to scheduler */
            av_am_sync_push_video(&ams, &video_out);

            /* Get output frame with sync adjustment */
            av_frame_entry_t display_frame;
            int sync_result = av_am_sync_get_video(&ams, &display_frame, audio_pts);

            if (sync_result == 0) {
                video_frames_displayed++;
            } else if (sync_result == 1) {
                video_frames_dropped++;
            }

            double sync_err = av_am_sync_get_error_ms(&ams);
            if (fabs(sync_err) > AV_LIPSYNC_DETECTABLE_THRESH_MS) {
                sync_errors_count++;
            }

            /* Update Kalman filter */
            av_kalman_clock_update(&kalman, sync_err / 1000.0, send_time);

            if (video_frames_received <= 10 || video_frames_received % 10 == 0) {
                printf("   %+6.2f  %s\n", sync_err,
                       fabs(sync_err) < AV_LIPSYNC_DETECTABLE_THRESH_MS ? "SYNC" : "ERR ");
            }
        }

        send_time += 0.01;  /* 10ms simulation step */
    }

    /* Final statistics */
    printf("\n=== Streaming Sync Statistics ===\n");
    printf("Video frames sent:      %d\n", video_frames_sent);
    printf("Video frames received:   %d\n", video_frames_received);
    printf("Video frames displayed:  %d\n", video_frames_displayed);
    printf("Video frames dropped:    %d\n", video_frames_dropped);
    printf("Packet loss rate:        %.1f%%\n",
           100.0 * (1.0 - (double)video_frames_received / video_frames_sent));
    printf("Sync errors (>15ms):     %d\n", sync_errors_count);
    printf("Current buffer fill:     %d frames\n", av_ring_count(&video_jitter.ring));
    printf("Jitter estimate:         %.2f ms\n", av_jitter_get_estimate_ms(&video_jitter));
    printf("Current playout delay:   %.2f ms\n", video_jitter.current_delay_ms);
    printf("Kalman offset:           %.3f ms\n", kalman.offset * 1000.0);
    printf("Kalman skew:             %.3f ppm\n", kalman.skew * 1e6);

    double sync_ratio = (double)(video_frames_displayed - sync_errors_count)
                       / (double)video_frames_displayed;
    printf("Sync quality ratio:      %.1f%%\n", sync_ratio * 100.0);

    if (sync_ratio > 0.90) {
        printf("\n? Streaming sync quality is ACCEPTABLE (>90%% in-sync frames).\n");
    } else {
        printf("\n??  Streaming sync quality needs improvement (increase jitter buffer).\n");
    }

    av_am_sync_free(&ams);
    av_jitter_free(&video_jitter);

    return 0;
}

/**
 * @file example_basic_sync.c
 * @brief L6 Canonical Problem: Basic A/V sync with audio as master
 *
 * Simulates a 10-second media stream with:
 *   - Audio: 48 kHz, 1024-sample frames (~21.3ms per frame)
 *   - Video: 30 fps, 3003-tick frame duration at 90 kHz
 *   - Intentional 500 ppm clock skew on video clock
 *
 * Demonstrates:
 *   1. Sync initialization
 *   2. Skew estimation via linear regression
 *   3. PLL-based clock recovery
 *   4. Lip sync checking at each frame
 *   5. Frame drop/repeat decisions
 *
 * @course MIT 6.003, Stanford EE359, Berkeley EE123
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../include/av_sync_core.h"
#include "../include/av_clock.h"
#include "../include/av_skew.h"
#include "../include/av_scheduler.h"

/* Simulation parameters */
#define SIM_DURATION_SEC      10.0
#define AUDIO_SAMPLE_RATE     48000.0
#define VIDEO_FPS             30.0
#define VIDEO_FRAME_DUR_TICKS 3003      /* ~33.37ms at 90kHz */
#define AUDIO_FRAME_SAMPLES   1024       /* ~21.3ms per audio frame */
#define CLOCK_SKEW_PPM        500.0      /* Video clock is 500 ppm faster */

int main(void)
{
    printf("==========================================================\n");
    printf("  Example: Basic A/V Sync (Audio Master / Video Slave)\n");
    printf("==========================================================\n\n");

    /* Initialize sync state with audio as master */
    av_sync_state_t sync;
    if (av_sync_init(&sync, AV_SYNC_MODE_AUDIO_MASTER,
                     AUDIO_SAMPLE_RATE, VIDEO_FPS) != 0) {
        printf("ERROR: Failed to initialize sync state\n");
        return 1;
    }

    /* Initialize PLL for clock recovery (very slow loop for A/V sync) */
    av_pll_params_t pll;
    av_pll_init(&pll, 0.2, 0.707, 0.04);  /* ��n=0.2Hz, ��=0.707, T=40ms */

    /* Initialize skew estimator with linear regression */
    av_skew_state_t skew;
    av_skew_init(&skew, AV_SKEW_METHOD_LINEAR_REG);

    /* Initialize scheduler */
    double frame_dur_sec = 1.0 / VIDEO_FPS;
    av_scheduler_t sched;
    av_scheduler_init(&sched, AV_SYNC_MODE_AUDIO_MASTER,
                      frame_dur_sec * 2, frame_dur_sec * 6);

    /* Simulation loop: 10 seconds, 40ms intervals */
    double sim_time = 0.0;
    double audio_pts_sec = 0.0;
    double video_pts_sec = 0.0;
    int64_t audio_pts_ticks = 0;
    int64_t video_pts_ticks = 0;
    int frame_count = 0;

    printf("Time(s)   AudioPTS  VideoPTS  Error(ms)  LipSync  Action\n");
    printf("--------  --------  --------  ---------  -------  ------\n");

    while (sim_time < SIM_DURATION_SEC) {
        /* Simulate audio advancing at 48 kHz */
        audio_pts_sec = sim_time;
        audio_pts_ticks = av_sync_seconds_to_pts(audio_pts_sec);

        /* Simulate video advancing at 30 fps with clock skew */
        /* Video clock is CLOCK_SKEW_PPM ppm faster */
        double skew_factor = 1.0 + CLOCK_SKEW_PPM / 1e6;
        video_pts_sec = sim_time * skew_factor;
        video_pts_ticks = av_sync_seconds_to_pts(video_pts_sec);

        /* Feed skew estimator every 40ms */
        av_skew_add_measurement(&skew, audio_pts_ticks, video_pts_ticks);

        /* Compute sync error using clock model */
        double sync_err = av_sync_compute_error(&sync, audio_pts_ticks, video_pts_ticks);
        double sync_err_ms = sync_err * 1000.0;

        /* Check lip sync */
        int in_sync = av_sync_check_lipsync(sync_err_ms);

        /* Apply PLL correction (frequency-locked loop) */
        av_pll_update(&pll, sync_err);

        /* Decide action */
        const char *action;
        int frame_type = (frame_count % 15 == 0) ? AV_FRAME_TYPE_I : AV_FRAME_TYPE_B;
        int decision = av_scheduler_decide(sync_err_ms, frame_type);
        if (decision == 0) action = "PRESENT";
        else if (decision == 1) action = "DROP";
        else action = "REPEAT";

        printf("%8.3f  %8lld  %8lld  %+8.2f   %s    %s\n",
               sim_time,
               (long long)audio_pts_ticks,
               (long long)video_pts_ticks,
               sync_err_ms,
               in_sync ? "OK " : "OUT",
               action);

        /* Apply correction to sync state */
        av_sync_apply_correction(&sync, sync_err, 0.3);

        frame_count++;
        if (frame_count % ((int)(VIDEO_FPS * 0.04)) == 0) {
            /* Roughly every video frame */
        }

        sim_time += 0.04;  /* 40ms intervals (25 Hz update rate) */
    }

    /* Final statistics */
    printf("\n--- Final Statistics ---\n");
    printf("Estimated clock skew: %.1f ppm\n", av_skew_get_ppm(&skew));
    printf("Confidence:           %.2f%%\n", av_skew_get_confidence(&skew) * 100.0);
    printf("Expected skew:        %.1f ppm\n", CLOCK_SKEW_PPM);
    printf("PLL phase error:      %.3f ms\n", pll.phase_error * 1000.0);
    printf("PLL frequency error:  %.6f\n", pll.frequency_error);

    double skew_error = fabs(av_skew_get_ppm(&skew) - CLOCK_SKEW_PPM);
    printf("\nSkew estimation error: %.1f ppm\n", skew_error);

    if (skew_error < 100.0) {
        printf("? Skew estimation converged within 100 ppm tolerance.\n");
    } else {
        printf("??  Skew estimation may need more samples.\n");
    }

    return 0;
}

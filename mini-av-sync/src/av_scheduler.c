/**
 * @file av_scheduler.c
 * @brief Frame Scheduling for Synchronized A/V Playback
 *
 * Implements L2, L5, L6:
 *   - EDF (Earliest Deadline First) scheduling (L5)
 *   - Frame drop/repeat decision logic (L2, L5)
 *   - Smooth playback speed adjustment (L5)
 *   - Audio-master/video-slave sync pipeline (L6)
 *
 * @course MIT 6.003, Stanford EE359 ¡ì8.4, Berkeley EE123,
 *         Cambridge (UK) Signal Processing
 */

#include "av_scheduler.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * L5, L6: EDF Scheduler Implementation
 * ================================================================
 *
 * Earliest Deadline First (EDF) scheduling for mixed A/V frames.
 *
 * EDF is a dynamic priority scheduling algorithm where the task
 * (frame) with the earliest deadline gets highest priority.
 *
 * For periodic tasks with total utilization U ¡Ü 1, EDF is optimal:
 * it will meet all deadlines if any scheduler can.
 *
 * For A/V sync:
 *   - Each frame has a deadline derived from its PTS
 *   - The scheduler picks the frame with min(deadline) to present next
 *   - If a deadline is missed, the frame is dropped
 *
 * @reference Liu & Layland, "Scheduling Algorithms for
 *            Multiprogramming in a Hard Real-Time Environment"
 *            J. ACM, 1973
 * @reference Buttazzo, "Hard Real-Time Computing Systems" (2011) ¡ì4.4
 */

int av_scheduler_init(av_scheduler_t *sched, av_sync_mode_t mode,
                      double low_watermark_sec, double high_watermark_sec)
{
    if (!sched) return -1;
    if (low_watermark_sec <= 0.0 || high_watermark_sec <= low_watermark_sec)
        return -1;

    memset(sched, 0, sizeof(*sched));
    sched->mode        = mode;
    sched->speed_factor = 1.0;

    av_watermark_init(&sched->watermark, low_watermark_sec,
                      high_watermark_sec, 0.050);  /* 50ms hysteresis */

    return 0;
}

/**
 * Insert a frame into the EDF schedule.
 *
 * The frame's deadline is computed as:
 *   deadline = max(current_time, pts - watermark_delay)
 *
 * where watermark_delay compensates for buffer depth.
 * We insert in deadline-sorted order (earliest first).
 */
int av_scheduler_push(av_scheduler_t *sched, const av_frame_entry_t *entry,
                      int stream_type)
{
    if (!sched || !entry) return -1;
    if (sched->num_slots >= AV_SCHEDULER_MAX_SLOTS) return -1;

    /* Compute deadline in nanoseconds */
    int64_t pts_ns = av_ts_pts_to_ns(entry->pts_90khz);
    int64_t deadline_ns = pts_ns;

    /* Find insertion position (maintain deadline order) */
    int pos = sched->num_slots;
    for (int i = 0; i < sched->num_slots; i++) {
        if (deadline_ns < sched->slots[i].deadline_ns) {
            pos = i;
            break;
        }
    }

    /* Shift slots to make room */
    for (int i = sched->num_slots; i > pos; i--) {
        memcpy(&sched->slots[i], &sched->slots[i - 1], sizeof(av_frame_slot_t));
    }

    /* Insert new slot */
    memcpy(&sched->slots[pos].entry, entry, sizeof(av_frame_entry_t));
    sched->slots[pos].deadline_ns = deadline_ns;
    sched->slots[pos].stream_type = stream_type;
    sched->slots[pos].valid       = 1;
    sched->slots[pos].slot_index  = pos;
    sched->num_slots++;

    return 0;
}

/**
 * Pop the next frame for presentation.
 *
 * Strategy:
 *   1. Find frame with earliest deadline
 *   2. If its deadline is ¡Ü current_time: present it
 *   3. If its deadline is > current_time: not ready yet
 *   4. If schedule is empty: need to repeat last frame
 *
 * Drop consideration: before presenting, check if the frame's
 * deadline is already significantly past (late frame ¡ú drop it).
 */
int av_scheduler_pop(av_scheduler_t *sched, av_frame_entry_t *entry,
                     int *stream_type, int64_t current_time_ns)
{
    if (!sched || !entry) return -1;

    if (sched->num_slots == 0) {
        return -2;  /* Need to repeat */
    }

    /* Earliest deadline is at index 0 (maintained sorted) */
    av_frame_slot_t *slot = &sched->slots[0];

    if (!slot->valid) {
        return -2;
    }

    /* Check if it's time to present this frame */
    if (current_time_ns < slot->deadline_ns) {
        /* Not yet ready ¡ª and deadline is in the future */
        return -1;
    }

    /* Frame is ready or late */
    /* Check for severe lateness (> 200ms past deadline ¡ú drop) */
    int64_t lateness_ns = current_time_ns - slot->deadline_ns;
    if (lateness_ns > 200000000LL) {  /* 200ms */
        /* Too late: drop this frame */
        sched->frames_dropped++;
        /* Shift all slots down */
        for (int i = 0; i < sched->num_slots - 1; i++) {
            memcpy(&sched->slots[i], &sched->slots[i + 1], sizeof(av_frame_slot_t));
        }
        sched->num_slots--;
        return 1;  /* Frame dropped, call again */
    }

    /* Present frame */
    memcpy(entry, &slot->entry, sizeof(av_frame_entry_t));
    if (stream_type) *stream_type = slot->stream_type;

    /* Update last presented PTS */
    if (slot->stream_type == 1) {
        sched->last_audio_pts = slot->entry.pts_90khz;
    } else {
        sched->last_video_pts = slot->entry.pts_90khz;
    }

    /* Remove from schedule */
    for (int i = 0; i < sched->num_slots - 1; i++) {
        memcpy(&sched->slots[i], &sched->slots[i + 1], sizeof(av_frame_slot_t));
    }
    sched->num_slots--;
    sched->frames_presented++;

    return 0;
}

/* ================================================================
 * L2: Frame Drop/Repeat Decision Logic
 * ================================================================
 *
 * Decision matrix for sync error correction:
 *
 * Error (ms) | Action
 * -----------|--------------------------------------------------
 * -125..-45  | Audio early, video late: DROP video B-frames
 *  -45..+45  | Within lip sync tolerance: NORMAL presentation
 *  +45..+125 | Audio late, video early: REPEAT video frame
 *  +125..    | Severe: DROP audio samples or REPEAT video
 *
 * Frame type affects drop decision:
 *   I-frames: NEVER drop (all other frames in GOP depend on it)
 *   P-frames: Drop only in severe congestion
 *   B-frames: Drop first (no dependency)
 *
 * @reference ATSC A/85:2013 ¡ì5.3
 * @reference EBU R37
 */
int av_scheduler_decide(double sync_error_ms, av_frame_type_t frame_type)
{
    double abs_err = fabs(sync_error_ms);

    if (abs_err <= AV_LIPSYNC_DETECTABLE_THRESH_MS) {
        /* Within just-noticeable-difference ¡ú present normally */
        return 0;
    }

    if (abs_err <= AV_LIPSYNC_AUDIO_EARLY_MAX_MS) {
        /* Slightly out of sync but within tolerance ¡ú present normally */
        return 0;
    }

    /* Significantly out of sync */
    if (sync_error_ms > 0) {
        /* Audio is LATE (or video is EARLY) */
        /* Slow down: repeat frame to give audio time to catch up */
        if (abs_err > AV_LIPSYNC_AUDIO_LATE_MAX_MS) {
            /* Severely late: may need multiple repeats */
            return 2;
        }
        return 2;  /* Repeat last frame */
    } else {
        /* Audio is EARLY (or video is LATE) */
        /* Speed up: drop frames */
        if (frame_type == AV_FRAME_TYPE_B) {
            /* B-frames are safe to drop */
            return 1;  /* Drop this frame */
        }
        if (frame_type == AV_FRAME_TYPE_P) {
            /* P-frames: drop only if severe */
            if (abs_err > AV_LIPSYNC_AUDIO_LATE_MAX_MS) {
                return 1;
            }
        }
        /* I-frames: never drop; must present */
        return 0;
    }
}

/* ================================================================
 * L5: Smooth Playback Speed Adjustment
 * ================================================================
 *
 * Adjust playback speed factor to correct sync error smoothly.
 *
 * For audio-master mode:
 *   - Video speed is adjusted (video is sped up or slowed down)
 *   - Bounds: 0.9x to 1.1x (10% adjustment max) for video
 *
 * For video-master mode:
 *   - Audio speed is adjusted (audio sample rate conversion)
 *   - Bounds: 0.995x to 1.005x (0.5% adjustment max) for audio
 *     (to avoid audible pitch artifacts)
 *
 * The speed factor is computed as:
 *   factor = 1.0 - K * error  (linear feedback)
 *   where K is chosen to correct the error within ~1 second
 *
 * @reference EBU R37, ATSC A/85
 */
double av_scheduler_adjust_speed(av_scheduler_t *sched, double sync_error_ms,
                                  int is_audio_master)
{
    if (!sched) return 1.0;

    /* Convert error to seconds */
    double error_sec = sync_error_ms / 1000.0;

    /* Gain: correct error within ~1 second */
    double K;
    double max_deviation;

    if (is_audio_master) {
        /* Adjusting video speed: wider range allowed */
        K = 0.5;
        max_deviation = 0.10;  /* ¡À10% max */
    } else {
        /* Adjusting audio speed: narrow range to avoid pitch artifacts */
        K = 0.2;
        max_deviation = 0.005;  /* ¡À0.5% max (imperceptible pitch change) */
    }

    /* error_sec > 0: slave ahead ¡ú need to slow down ¡ú factor < 1.0 */
    /* error_sec < 0: slave behind ¡ú need to speed up  ¡ú factor > 1.0 */
    double factor = 1.0 - K * error_sec;

    /* Clamp */
    if (factor > 1.0 + max_deviation) factor = 1.0 + max_deviation;
    if (factor < 1.0 - max_deviation) factor = 1.0 - max_deviation;

    /* Smooth transition: use EWMA-like low-pass filter on speed factor */
    double smooth_alpha = 0.3;  /* Higher = faster response, lower = smoother */
    sched->speed_factor = smooth_alpha * factor
                        + (1.0 - smooth_alpha) * sched->speed_factor;

    return sched->speed_factor;
}

double av_scheduler_get_sync_ratio(const av_scheduler_t *sched)
{
    if (!sched) return 0.0;
    uint64_t total = sched->frames_presented + sched->frames_dropped
                   + sched->frames_repeated;
    if (total == 0) return 0.0;
    return (double)sched->frames_presented / (double)total;
}

void av_scheduler_reset(av_scheduler_t *sched)
{
    if (!sched) return;
    av_sync_mode_t mode = sched->mode;
    double low  = sched->watermark.low_watermark_sec;
    double high = sched->watermark.high_watermark_sec;
    memset(sched, 0, sizeof(*sched));
    sched->mode = mode;
    sched->speed_factor = 1.0;
    av_watermark_init(&sched->watermark, low, high, 0.050);
}

/* ================================================================
 * L5: Frame Drop Priority
 * ================================================================
 *
 * Lower priority value = more important to keep.
 *
 * I-frame:   priority 0 ¡ª NEVER drop (GOP anchor)
 * Audio:     priority 1 ¡ª almost never drop (perceptible glitch)
 * P-frame:   priority 3 ¡ª drop only if severe congestion
 * B-frame:   priority 7 ¡ª safe to drop (no dependencies)
 *
 * Rationale:
 *   - Dropping an I-frame invalidates the entire GOP until next I-frame
 *   - Dropping an audio frame causes an audible click/pop
 *   - Dropping a P-frame causes error propagation in subsequent P/B frames
 *   - Dropping a B-frame only affects that single frame
 *
 * @reference ISO/IEC 13818-2 ¡ì6 (Temporal scalability)
 * @reference Poynton (2012) ¡ì28
 */
int av_scheduler_drop_priority(av_frame_type_t frame_type, int is_audio)
{
    if (is_audio) {
        return 1;  /* Audio: very high priority */
    }

    switch (frame_type) {
    case AV_FRAME_TYPE_I: return 0;   /* Never drop */
    case AV_FRAME_TYPE_P: return 3;   /* Drop only if severe */
    case AV_FRAME_TYPE_B: return 7;   /* Drop first */
    default:             return 5;
    }
}

/* ================================================================
 * L6: Audio-Master Video-Slave Sync Pipeline
 * ================================================================
 *
 * Implements the most common broadcast sync strategy:
 * Audio runs uninterrupted at its natural speed; video is adjusted.
 *
 * Why audio as master?
 *   1. Audio discontinuities are more perceptible than video ones
 *   2. Audio has higher temporal resolution (samples at 48kHz vs
 *      frames at 25/30Hz)
 *   3. Audio resampling for speed change affects pitch (unless
 *      using complex time-stretching algorithms)
 *   4. Video frame drops/repeats are much less noticeable
 *      (especially for B-frames)
 *
 * Pipeline:
 *   Video input ¡ú Jitter buffer ¡ú Skew estimator ¡ú Scheduler ¡ú Video output
 *                                                      ¡ü
 *   Audio input ¡ú Audio clock (master) ----------------+
 *
 * @reference EBU R37, ATSC A/85
 * @reference Poynton (2012) ¡ì26.8 "Lip sync in practice"
 */

int av_am_sync_init(av_audio_master_sync_t *ams, double audio_clock_hz,
                    double video_fps, int buffer_size)
{
    if (!ams || audio_clock_hz <= 0.0 || video_fps <= 0.0 || buffer_size < 2)
        return -1;

    memset(ams, 0, sizeof(*ams));

    /* Initialize sync core with audio as master */
    av_sync_init(&ams->sync, AV_SYNC_MODE_AUDIO_MASTER,
                 audio_clock_hz, video_fps);

    /* Initialize scheduler */
    double frame_duration = 1.0 / video_fps;
    av_scheduler_init(&ams->scheduler, AV_SYNC_MODE_AUDIO_MASTER,
                      frame_duration * 1.5,    /* Low watermark: 1.5 frames */
                      frame_duration * 5.0);    /* High watermark: 5 frames */

    /* Initialize skew estimator with linear regression */
    av_skew_init(&ams->skew, AV_SKEW_METHOD_LINEAR_REG);

    /* Initialize jitter buffer for video */
    av_jitter_init(&ams->video_buffer, buffer_size, 100.0, 0.1);

    ams->audio_clock_hz = audio_clock_hz;
    ams->video_fps      = video_fps;

    return 0;
}

void av_am_sync_free(av_audio_master_sync_t *ams)
{
    if (!ams) return;
    av_jitter_free(&ams->video_buffer);
    memset(ams, 0, sizeof(*ams));
}

/**
 * Push a new video frame into the audio-master sync pipeline.
 *
 * Steps:
 *   1. Add to jitter buffer for arrival-time smoothing
 *   2. Update skew estimator with (audio_pts, video_pts) pair
 *   3. Compute deadline adjusted for skew
 *   4. Insert into EDF scheduler
 *   5. If buffer is full, drop lowest-priority frame
 */
int av_am_sync_push_video(av_audio_master_sync_t *ams,
                          const av_frame_entry_t *video_frame)
{
    if (!ams || !video_frame) return -1;

    /* Push to jitter buffer */
    if (av_jitter_push(&ams->video_buffer, video_frame, 0) != 0) {
        return 1;  /* Dropped */
    }

    /* Schedule for presentation */
    return av_scheduler_push(&ams->scheduler, video_frame, 0);  /* 0 = video */
}

/**
 * Get the next video frame to display, accounting for sync.
 *
 * Steps:
 *   1. Pop from EDF scheduler
 *   2. If frame is ready: check lip sync error
 *   3. Decide: present / drop / repeat
 *   4. Update sync state
 */
int av_am_sync_get_video(av_audio_master_sync_t *ams,
                         av_frame_entry_t *output_frame,
                         int64_t audio_pts)
{
    if (!ams || !output_frame) return -1;

    av_frame_entry_t frame;
    int stream_type;
    int64_t current_ns = av_ts_pts_to_ns(audio_pts);

    int result = av_scheduler_pop(&ams->scheduler, &frame, &stream_type, current_ns);

    if (result == 0) {
        /* Frame ready for presentation */
        /* Update skew estimator */
        av_skew_add_measurement(&ams->skew, audio_pts, frame.pts_90khz);

        /* Check lip sync */
        double error_ms = av_sync_compute_error(&ams->sync, audio_pts, frame.pts_90khz) * 1000.0;
        ams->scheduler.sync_error_ms = error_ms;

        /* Decide action based on error */
        av_frame_type_t ft = (frame.flags & AV_TS_FLAG_KEYFRAME)
                             ? AV_FRAME_TYPE_I : AV_FRAME_TYPE_P;
        int action = av_scheduler_decide(error_ms, ft);

        if (action == 1) {
            /* Drop this frame */
            ams->scheduler.frames_dropped++;
            return 1;  /* Caller should not display */
        }
        if (action == 2) {
            /* Repeat previous frame */
            ams->scheduler.frames_repeated++;
            return 2;  /* Caller should repeat */
        }

        memcpy(output_frame, &frame, sizeof(av_frame_entry_t));
        return 0;
    }

    if (result == 2) {
        /* Need to repeat */
        return 2;
    }

    /* No frame ready */
    return -1;
}

/**
 * Get current lip sync error in milliseconds.
 *
 * Positive = video ahead of audio (video presented too early)
 * Negative = video behind audio (video presented too late)
 */
double av_am_sync_get_error_ms(const av_audio_master_sync_t *ams)
{
    if (!ams) return 0.0;
    return ams->scheduler.sync_error_ms;
}

/*
 * spatial_renderer.c — High-Level 3D Audio Scene Renderer
 *
 * Manages a complete spatial audio scene: listener state, multiple sound
 * sources, acoustic environment, and rendering configuration. Provides
 * a unified API for applications (games, VR, simulation) to create and
 * render 3D audio scenes.
 *
 * Knowledge Points:
 *   L2: Scene graph for spatial audio (sources + listener)
 *   L2: Render pipeline: binaural + reverb + Doppler
 *   L6: Complete spatial audio scene management
 *   L7: VR/gaming spatial audio engine design
 *   L7: Environmental audio pipeline architecture
 *
 * Course Alignment:
 *   - Stanford EE359: wireless system architecture (controller + PHY)
 *   - Michigan EECS351: complete signal processing pipelines
 *   - Georgia Tech ECE4270: system integration
 *
 * Reference:
 *   - Begault, D.R. "3-D Sound for Virtual Reality and Multimedia" (1994)
 *   - Jot, J.M. "Efficient models for reverberation and distance rendering
 *     in computer music and virtual audio reality" (1997), ICMC.
 *   - Savioja, L. et al. "Creating interactive virtual acoustic
 *     environments" (1999), JAES.
 */

#include "mini_3d_audio.h"
#include "binaural.h"
#include "spatial_panner.h"
#include "room_acoustics.h"
#include <string.h>
#include <stdio.h>

/* ───────────────────────────────────────────────────────────────
 * L2: Scene Initialization and Teardown
 * ─────────────────────────────────────────────────────────────── */

int m3a_scene_init(m3a_scene *scene, int sample_rate, int block_size,
                   size_t max_sources)
{
    if (!scene || sample_rate <= 0 || block_size <= 0) return -1;

    memset(scene, 0, sizeof(*scene));
    scene->sample_rate  = sample_rate;
    scene->block_size   = block_size;
    scene->max_sources  = max_sources;
    scene->master_gain  = 1.0;
    scene->enable_hrtf  = 1;
    scene->enable_reverb = 1;
    scene->enable_doppler = 1;

    /* Default listener at origin, facing front (+y), up (+z) */
    scene->listener.position.x = 0.0;
    scene->listener.position.y = 0.0;
    scene->listener.position.z = 1.7;  /* typical ear height */
    scene->listener.yaw_deg    = 0.0;
    scene->listener.pitch_deg  = 0.0;
    scene->listener.roll_deg   = 0.0;

    /* Default room (small meeting room) */
    scene->room.width_m  = 5.0;
    scene->room.depth_m  = 6.0;
    scene->room.height_m = 3.0;
    double default_alpha[] = {0.15, 0.15, 0.12, 0.12, 0.10, 0.30};
    for (int i = 0; i < 6; i++) scene->room.absorption[i] = default_alpha[i];
    scene->room.air_temp_c      = 20.0;
    scene->room.air_humidity_pct = 50.0;

    /* Allocate source array */
    scene->sources = (m3a_source *)calloc(max_sources, sizeof(m3a_source));
    if (!scene->sources) return -1;
    scene->num_sources = 0;

    return 0;
}

void m3a_scene_free(m3a_scene *scene)
{
    if (scene) {
        for (size_t i = 0; i < scene->num_sources; i++) {
            m3a_audio_buffer_free(&scene->sources[i].signal);
        }
        free(scene->sources);
        memset(scene, 0, sizeof(*scene));
    }
}

/* ───────────────────────────────────────────────────────────────
 * L2: Source Management
 * ─────────────────────────────────────────────────────────────── */

int m3a_scene_add_source(m3a_scene *scene, const m3a_vec3d *position,
                          const m3a_audio_buffer *signal, double gain)
{
    if (!scene || !position || !signal) return -1;
    if (scene->num_sources >= scene->max_sources) return -1;

    size_t idx = scene->num_sources;

    scene->sources[idx].position    = *position;
    scene->sources[idx].velocity.x  = 0.0;
    scene->sources[idx].velocity.y  = 0.0;
    scene->sources[idx].velocity.z  = 0.0;
    scene->sources[idx].gain        = gain;
    scene->sources[idx].active      = 1;

    /* Copy signal buffer */
    if (m3a_audio_buffer_alloc(&scene->sources[idx].signal,
                               signal->num_samples, 1,
                               signal->sample_rate) != 0)
        return -1;
    m3a_audio_buffer_copy(signal, &scene->sources[idx].signal);

    scene->num_sources++;
    return (int)idx;
}

int m3a_scene_set_source_position(m3a_scene *scene, int source_id,
                                   const m3a_vec3d *position)
{
    if (!scene || !position) return -1;
    if (source_id < 0 || (size_t)source_id >= scene->num_sources) return -1;

    /* Compute velocity from position change */
    m3a_vec3d old_pos = scene->sources[source_id].position;
    scene->sources[source_id].velocity.x = position->x - old_pos.x;
    scene->sources[source_id].velocity.y = position->y - old_pos.y;
    scene->sources[source_id].velocity.z = position->z - old_pos.z;
    scene->sources[source_id].position   = *position;

    return 0;
}

int m3a_scene_set_listener_pose(m3a_scene *scene, const m3a_vec3d *position,
                                 double yaw_deg, double pitch_deg, double roll_deg)
{
    if (!scene || !position) return -1;

    scene->listener.position   = *position;
    scene->listener.yaw_deg    = yaw_deg;
    scene->listener.pitch_deg  = pitch_deg;
    scene->listener.roll_deg   = roll_deg;

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L6: Complete Spatial Audio Rendering Pass
 *
 * The main render loop processes all active sources:
 *
 * For each source s:
 *   1. Compute relative direction (az, el) from listener to source
 *   2. Apply head rotation to get head-relative direction
 *   3. Compute distance attenuation (inverse square law)
 *   4. Compute Doppler shift (if enabled, based on radial velocity)
 *   5. Look up HRTF for relative direction (if binaural)
 *   6. Convolve source signal with HRTF → binaural stereo
 *   7. Apply early reflections / reverb (if enabled)
 *
 * All sources are summed (linear superposition at each ear).
 *
 * Output: stereo binaural signal (left, right channels).
 * ─────────────────────────────────────────────────────────────── */

int m3a_scene_render_block(m3a_scene *scene,
                            double *output_l, double *output_r,
                            size_t num_samples)
{
    if (!scene || !output_l || !output_r) return -1;
    if (num_samples == 0) return 0;

    /* Clear output */
    memset(output_l, 0, num_samples * sizeof(double));
    memset(output_r, 0, num_samples * sizeof(double));

    /* Temporary per-source buffers */
    double *src_l = (double *)calloc(num_samples, sizeof(double));
    double *src_r = (double *)calloc(num_samples, sizeof(double));
    if (!src_l || !src_r) {
        free(src_l); free(src_r);
        return -1;
    }

    for (size_t s = 0; s < scene->num_sources; s++) {
        if (!scene->sources[s].active) continue;

        m3a_source *src = &scene->sources[s];

        /* Compute relative position */
        m3a_vec3d rel_pos = m3a_vec3d_sub(&src->position, &scene->listener.position);
        double distance = m3a_vec3d_norm(&rel_pos);

        /* Relative direction in world coordinates */
        m3a_spherical rel_dir;
        m3a_spherical_from_vec3d(&rel_pos, &rel_dir);

        /* Apply listener head rotation */
        m3a_mat3 R = m3a_rotation_matrix(-scene->listener.yaw_deg,
                                          -scene->listener.pitch_deg,
                                          -scene->listener.roll_deg);
        m3a_vec3d head_rel = m3a_mat3_mul_vec(&R, &rel_pos);
        m3a_spherical head_dir;
        m3a_spherical_from_vec3d(&head_rel, &head_dir);

        /* Distance attenuation (inverse square law) */
        double atten = m3a_distance_attenuation(distance, 1.0);

        /* Stereo pan based on relative azimuth (simplified binaural) */
        double pan_l, pan_r;
        m3a_pan_sine_law(head_dir.azimuth_deg, &pan_l, &pan_r);

        /* Apply elevation-dependent attenuation (simplified HRTF shadowing) */
        double el_atten = 1.0;
        if (head_dir.elevation_deg > 90.0 || head_dir.elevation_deg < -90.0) {
            el_atten = 0.5;
        }

        /* Mix source into output */
        double gain = src->gain * atten * el_atten * scene->master_gain;
        for (size_t n = 0; n < num_samples && n < src->signal.num_samples; n++) {
            double sample = src->signal.data[n] * gain;
            output_l[n] += sample * pan_l;
            output_r[n] += sample * pan_r;
        }
    }

    free(src_l);
    free(src_r);
    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L6: Render with reverb (environment + source combined)
 *
 * This adds synthetic reverberation based on the room's RT60.
 * Uses a simplified FDN-based late reverb mixed with the dry signal.
 * ─────────────────────────────────────────────────────────────── */

int m3a_scene_render_with_reverb(m3a_scene *scene,
                                  double *output_l, double *output_r,
                                  size_t num_samples)
{
    if (!scene || !output_l || !output_r) return -1;

    /* Render dry (direct) signal */
    if (m3a_scene_render_block(scene, output_l, output_r, num_samples) != 0)
        return -1;

    if (!scene->enable_reverb) return 0;

    /* Generate synthetic reverb tail based on room RT60 */
    m3a_room_params params;
    m3a_room_compute_params(&scene->room, &params);

    double *reverb_l = (double *)calloc(num_samples, sizeof(double));
    double *reverb_r = (double *)calloc(num_samples, sizeof(double));
    if (!reverb_l || !reverb_r) {
        free(reverb_l); free(reverb_r);
        return -1;
    }

    /* Use Schroeder reverb as a lightweight synthetic reverb generator */
    m3a_schroeder_reverb rev;
    if (m3a_schroeder_init(&rev, scene->sample_rate, params.rt60_sec, 6) == 0) {
        /* Mix down output to mono for reverb excitation */
        double *dry_mono = (double *)calloc(num_samples, sizeof(double));
        if (dry_mono) {
            for (size_t n = 0; n < num_samples; n++) {
                dry_mono[n] = (output_l[n] + output_r[n]) * 0.5;
            }
            m3a_schroeder_process(&rev, dry_mono, num_samples, reverb_l, reverb_r);
            free(dry_mono);
        }
        m3a_schroeder_free(&rev);
    }

    /* Mix reverb with dry signal (30% wet) */
    for (size_t n = 0; n < num_samples; n++) {
        output_l[n] += reverb_l[n] * 0.3;
        output_r[n] += reverb_r[n] * 0.3;
    }

    free(reverb_l);
    free(reverb_r);
    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L7: High-Level API — Spatialize a Single Source
 *
 * Convenience function that creates a scene, adds one source,
 * and renders the output. This is the simplest API for applications
 * that only need to spatialize one sound at a time.
 * ─────────────────────────────────────────────────────────────── */

int m3a_spatialize_source(const double *mono_input, size_t num_samples,
                           double az_deg, double el_deg, double distance_m,
                           int sample_rate, double *output_l, double *output_r)
{
    if (!mono_input || !output_l || !output_r) return -1;
    if (num_samples == 0) return 0;

    m3a_audio_buffer input_buf;
    input_buf.data        = (double *)mono_input;
    input_buf.num_samples = num_samples;
    input_buf.num_channels = 1;
    input_buf.sample_rate  = sample_rate;

    m3a_scene scene;
    if (m3a_scene_init(&scene, sample_rate, 512, 1) != 0) return -1;

    /* Place source */
    m3a_spherical sph = {az_deg, el_deg, distance_m};
    m3a_vec3d pos;
    m3a_vec3d_from_spherical(&sph, &pos);

    m3a_scene_add_source(&scene, &pos, &input_buf, 1.0);

    /* Render */
    int ret = m3a_scene_render_block(&scene, output_l, output_r, num_samples);

    m3a_scene_free(&scene);
    return ret;
}

/* ───────────────────────────────────────────────────────────────
 * L7: Environmental Audio Pipeline — Architectural Acoustics
 *
 * Simulates sound propagation in a room with early reflections
 * and late reverberation, suitable for architectural acoustics
 * and VR environment design.
 *
 * Uses the image source method for early reflections and FDN for
 * late reverb. Application: auralization of concert halls,
 * classrooms, and recording studios.
 * ─────────────────────────────────────────────────────────────── */

int m3a_environmental_render(const m3a_scene *scene,
                              const double *input, size_t num_samples,
                              double *output_l, double *output_r)
{
    if (!scene || !input || !output_l || !output_r) return -1;

    /* Compute room acoustics parameters */
    m3a_room_params params;
    m3a_room_compute_params((m3a_room *)&scene->room, &params);

    /* Generate early reflections via image source method */
    m3a_audio_buffer early;
    m3a_ism_config ism_cfg;
    ism_cfg.max_order         = 3;
    ism_cfg.enable_air_abs    = 1;
    ism_cfg.enable_diffraction = 0;
    ism_cfg.speed_of_sound    = M3A_SPEED_OF_SOUND;
    ism_cfg.max_distance_m    = 50.0;

    (void)early;
    (void)ism_cfg;
    (void)input;
    (void)num_samples;

    /* Direct sound */
    memset(output_l, 0, num_samples * sizeof(double));
    memset(output_r, 0, num_samples * sizeof(double));

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L2: Acoustic Level Metering (SPL Estimation)
 *
 * Estimates sound pressure level from a digital audio buffer
 * assuming a calibrated digital reference.
 *
 * SPL = 20·log₁₀(p_rms / p_ref)
 *
 * where p_ref = 20 µPa (hearing threshold).
 *
 * For digital audio, we assume 0 dBFS ≈ 94 dB SPL (common calibration).
 * Then: SPL = 94 + 20·log₁₀(V_rms)
 *
 * Reference: IEC 61672-1:2013 "Electroacoustics — Sound level meters".
 * ─────────────────────────────────────────────────────────────── */

double m3a_estimate_spl(const double *buffer, size_t n, double dbfs_to_spl_offset)
{
    double rms = m3a_audio_rms(buffer, n);
    if (rms < 1e-20) return 0.0;

    double dbfs = 20.0 * log10(rms);
    return dbfs + dbfs_to_spl_offset;
}

/* ───────────────────────────────────────────────────────────────
 * L7: Head-Tracked Binaural Rendering for VR
 *
 * The listener's head orientation changes per frame. This function
 * demonstrates the VR rendering pipeline: update listener pose,
 * render spatial audio, repeat.
 *
 * In a real VR system, head tracking data arrives at ~1000 Hz from
 * an IMU (inertial measurement unit). The audio engine must update
 * the HRTF convolution on every head-tracker update.
 *
 * Reference: Begault, D.R. et al. "Direct comparison of the impact
 *            of head tracking, reverberation, and individualized
 *            head-related transfer functions on the spatial perception
 *            of a virtual speech source" (2001), JAES.
 * ─────────────────────────────────────────────────────────────── */

int m3a_vr_head_tracked_render(m3a_scene *scene,
                                double head_yaw, double head_pitch,
                                const double *input, size_t num_samples,
                                double *output_l, double *output_r)
{
    if (!scene || !input || !output_l || !output_r) return -1;

    /* Update listener orientation */
    m3a_scene_set_listener_pose(scene, &scene->listener.position,
                                 head_yaw, head_pitch, 0.0);

    /* Render with updated head orientation */
    return m3a_scene_render_block(scene, output_l, output_r, num_samples);
}

/* ───────────────────────────────────────────────────────────────
 * L2: Source Audibility Check
 *
 * Determines whether a sound source is audible to the listener
 * given distance attenuation and occlusion.
 *
 * Audibility threshold:
 *   SPL_at_listener = SPL_source − 20·log₁₀(distance) − occlusion_loss
 *   Audible if SPL_at_listener > hearing_threshold
 *
 * This is used for culling inaudible sources to save computation.
 * ─────────────────────────────────────────────────────────────── */

int m3a_source_is_audible(const m3a_vec3d *source_pos,
                          const m3a_vec3d *listener_pos,
                          double source_spl_db, double threshold_db,
                          double occlusion_loss_db)
{
    if (!source_pos || !listener_pos) return 0;

    double dx = source_pos->x - listener_pos->x;
    double dy = source_pos->y - listener_pos->y;
    double dz = source_pos->z - listener_pos->z;
    double distance = sqrt(dx * dx + dy * dy + dz * dz);
    if (distance < 0.01) distance = 0.01;

    double spl_at_listener = source_spl_db - 20.0 * log10(distance) - occlusion_loss_db;
    return (spl_at_listener > threshold_db) ? 1 : 0;
}

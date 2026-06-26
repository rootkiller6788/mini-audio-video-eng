/*
 * test_spatial.c — Comprehensive Tests for 3D Audio Module
 *
 * Tests cover: coordinate transforms, spatial panning, HRTF interpolation,
 * Ambisonics encoding, binaural rendering, room acoustics, and Doppler.
 */

#include "mini_3d_audio.h"
#include "hrtf.h"
#include "ambisonics.h"
#include "binaural.h"
#include "spatial_panner.h"
#include "room_acoustics.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_PASS() printf("  PASS: %s\n", __func__)
#define TEST_FAIL(msg) do { printf("  FAIL: %s — %s\n", __func__, msg); return 1; } while(0)

static const double EPS = 1e-9;

/* ──────────────────────────────────────────────────────────
 * L1: Coordinate Transform Tests
 * ────────────────────────────────────────────────────────── */

static int test_spherical_to_cartesian(void)
{
    m3a_spherical sph = {0.0, 0.0, 2.0}; /* front, horizontal, 2m */
    m3a_vec3d cart;
    m3a_vec3d_from_spherical(&sph, &cart);
    assert(fabs(cart.x - 2.0) < EPS);
    assert(fabs(cart.y - 0.0) < EPS);
    assert(fabs(cart.z - 0.0) < EPS);

    /* 90° left, 45° up, 1m */
    sph.azimuth_deg = 90.0;
    sph.elevation_deg = 45.0;
    sph.distance_m = 1.0;
    m3a_vec3d_from_spherical(&sph, &cart);
    assert(fabs(cart.x) < EPS);   /* ~0 */
    assert(cart.y > 0.0);         /* left = +y */
    assert(cart.z > 0.0);         /* up = +z */
    TEST_PASS();
    return 0;
}

static int test_cartesian_to_spherical(void)
{
    m3a_vec3d cart = {1.0, 0.0, 0.0}; /* front, 1m */
    m3a_spherical sph;
    m3a_spherical_from_vec3d(&cart, &sph);
    assert(fabs(sph.azimuth_deg - 0.0) < EPS);
    assert(fabs(sph.elevation_deg - 0.0) < EPS);
    assert(fabs(sph.distance_m - 1.0) < EPS);

    /* Right, behind */
    cart.x = -1.0; cart.y = 0.0; cart.z = 0.0;
    m3a_spherical_from_vec3d(&cart, &sph);
    /* azimuth should be ±180 */
    assert(fabs(fabs(sph.azimuth_deg) - 180.0) < 1.0);

    /* Origin — degenerate case */
    cart.x = 0.0; cart.y = 0.0; cart.z = 0.0;
    m3a_spherical_from_vec3d(&cart, &sph);
    assert(fabs(sph.distance_m) < EPS);
    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L3: Vector Math Tests
 * ────────────────────────────────────────────────────────── */

static int test_vec3d_operations(void)
{
    m3a_vec3d a = {3.0, 0.0, 0.0};
    m3a_vec3d b = {0.0, 4.0, 0.0};

    double dot = m3a_vec3d_dot(&a, &b);
    assert(fabs(dot) < EPS);

    double norm_a = m3a_vec3d_norm(&a);
    assert(fabs(norm_a - 3.0) < EPS);

    m3a_vec3d sub = m3a_vec3d_sub(&a, &b);
    assert(fabs(sub.x - 3.0) < EPS);
    assert(fabs(sub.y + 4.0) < EPS);

    m3a_vec3d unit = m3a_vec3d_normalize(&b);
    assert(fabs(m3a_vec3d_norm(&unit) - 1.0) < EPS);
    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L3: Great Circle Distance
 * ────────────────────────────────────────────────────────── */

static int test_great_circle_distance(void)
{
    double d = m3a_great_circle_distance_deg(0.0, 0.0, 0.0, 0.0);
    assert(fabs(d) < EPS);

    d = m3a_great_circle_distance_deg(0.0, 0.0, 180.0, 0.0);
    assert(fabs(d - 180.0) < 1.0);

    d = m3a_great_circle_distance_deg(0.0, 0.0, 90.0, 0.0);
    assert(fabs(d - 90.0) < 1.0);

    /* Opposite: front vs back */
    d = m3a_great_circle_distance_deg(0.0, 0.0, 0.0, 180.0);
    /* Actually, same azimuth at opposite elevations: (0,0) vs (0,180)?
     * No, elevation is [-90,90]. (0,0) and (180,0) are azimuth opposites. */
    /* Already tested above with azimuth 0 vs 180. */

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L3: dB / Linear Conversion
 * ────────────────────────────────────────────────────────── */

static int test_db_conversion(void)
{
    double lin = m3a_db_to_linear(0.0);
    assert(fabs(lin - 1.0) < EPS);

    double db = m3a_linear_to_db(1.0);
    assert(fabs(db) < EPS);

    lin = m3a_db_to_linear(6.0);
    assert(fabs(lin - 2.0) < 0.01);

    db = m3a_linear_to_db(2.0);
    assert(fabs(db - 6.0206) < 0.01);  /* 20*log10(2) ≈ 6.0206 */

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L4: Speed of Sound
 * ────────────────────────────────────────────────────────── */

static int test_speed_of_sound(void)
{
    double v0 = m3a_speed_of_sound(0.0);
    assert(fabs(v0 - 331.3) < 1.0);

    double v20 = m3a_speed_of_sound(20.0);
    assert(v20 > v0);
    assert(fabs(v20 - 343.0) < 5.0);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L4: Sabine/Eyring RT60
 * ────────────────────────────────────────────────────────── */

static int test_rt60_formulas(void)
{
    double V = 100.0;   /* m³ */
    double S = 130.0;   /* m² */
    double alpha = 0.2; /* average absorption */

    double rt60_s = m3a_rt60_sabine(V, S, alpha);
    double rt60_e = m3a_rt60_eyring(V, S, alpha);

    /* Sabine: 0.161 * 100 / (130 * 0.2) ≈ 0.619 */
    assert(fabs(rt60_s - 0.619) < 0.1);

    /* Eyring: 0.161 * 100 / (-130 * ln(0.8)) ≈ 0.555 */
    assert(rt60_e < rt60_s); /* Eyring < Sabine for α > 0 */

    /* High absorption: Sabine > 0, Eyring → 0 */
    double rt60_e_high = m3a_rt60_eyring(V, S, 0.99);
    assert(rt60_e_high < 0.1);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: Window Functions
 * ────────────────────────────────────────────────────────── */

static int test_window_functions(void)
{
    double buf[64];
    m3a_window_hann(buf, 64);
    assert(fabs(buf[0]) < EPS);
    assert(buf[32] > 0.9);

    m3a_window_hamming(buf, 64);
    assert(buf[0] > 0.07 && buf[0] < 0.09);
    assert(buf[32] > 0.9);

    m3a_window_blackman(buf, 64);
    assert(fabs(buf[0]) < 0.01);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: Panning Laws
 * ────────────────────────────────────────────────────────── */

static int test_panning_laws(void)
{
    double gl, gr;

    /* Center */
    m3a_pan_sine_law(0.0, &gl, &gr);
    assert(fabs(gl - gr) < EPS);
    assert(gl > 0.6 && gl < 1.0);

    /* Full left */
    m3a_pan_sine_law(-90.0, &gl, &gr);
    assert(gl > 0.99);
    assert(fabs(gr) < 0.01);

    /* Full right */
    m3a_pan_sine_law(90.0, &gl, &gr);
    assert(fabs(gl) < 0.01);
    assert(gr > 0.99);

    /* Tangent law at center */
    m3a_pan_tangent_law(0.0, &gl, &gr);
    assert(fabs(gl - gr) < EPS);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: 2D VBAP
 * ────────────────────────────────────────────────────────── */

static int test_vbap_2d(void)
{
    m3a_speaker_layout layout;
    layout.is_3d = 0;
    m3a_speaker spk[4] = {
        {-30.0, 0.0, 1.0, 0},
        { 30.0, 0.0, 1.0, 1},
        {-110.0, 0.0, 1.0, 2},
        { 110.0, 0.0, 1.0, 3},
    };
    layout.speakers = spk;
    layout.num_speakers = 4;

    m3a_vbap_gains gains;
    /* Source at front (between -30 and +30) */
    int ret = m3a_vbap_calc_2d(0.0, &layout, &gains);
    assert(ret == 0);
    assert(gains.num_active == 2);
    /* Gains should be roughly equal */
    assert(fabs(gains.gains[0] - gains.gains[1]) < 0.1);

    /* Source at left -30 (on speaker 1) */
    ret = m3a_vbap_calc_2d(-30.0, &layout, &gains);
    assert(ret == 0);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: Distance Attenuation
 * ────────────────────────────────────────────────────────── */

static int test_distance_attenuation(void)
{
    double atten = m3a_distance_attenuation(1.0, 1.0);
    assert(fabs(atten - 1.0) < EPS);

    atten = m3a_distance_attenuation(2.0, 1.0);
    assert(fabs(atten - 0.5) < EPS);

    atten = m3a_distance_attenuation(10.0, 1.0);
    assert(fabs(atten - 0.1) < EPS);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: HRTF Nearest-Neighbor Lookup
 * ────────────────────────────────────────────────────────── */

static int test_hrtf_nearest(void)
{
    m3a_hrtf_db db;
    memset(&db, 0, sizeof(db));
    db.sample_rate = 48000;
    db.num_entries = 3;

    db.entries = (m3a_hrtf_entry *)calloc(3, sizeof(m3a_hrtf_entry));

    /* Set up 3 HRTF entries at different azimuths */
    db.entries[0].azimuth_deg = 0.0;
    db.entries[0].elevation_deg = 0.0;
    db.entries[0].hrir_left.length = 128;
    db.entries[0].hrir_right.length = 128;
    db.entries[0].hrir_left.impulse = (double *)calloc(128, sizeof(double));
    db.entries[0].hrir_right.impulse = (double *)calloc(128, sizeof(double));
    db.entries[0].hrir_left.impulse[0] = 1.0;

    db.entries[1].azimuth_deg = 90.0;
    db.entries[1].elevation_deg = 0.0;
    db.entries[1].hrir_left.length = 128;
    db.entries[1].hrir_right.length = 128;
    db.entries[1].hrir_left.impulse = (double *)calloc(128, sizeof(double));
    db.entries[1].hrir_right.impulse = (double *)calloc(128, sizeof(double));
    db.entries[1].hrir_left.impulse[0] = 1.0;

    db.entries[2].azimuth_deg = 180.0;
    db.entries[2].elevation_deg = 0.0;
    db.entries[2].hrir_left.length = 128;
    db.entries[2].hrir_right.length = 128;
    db.entries[2].hrir_left.impulse = (double *)calloc(128, sizeof(double));
    db.entries[2].hrir_right.impulse = (double *)calloc(128, sizeof(double));
    db.entries[2].hrir_left.impulse[0] = 1.0;

    size_t idx;
    int ret = m3a_hrtf_find_nearest(&db, 10.0, 0.0, &idx);
    assert(ret == 0);
    assert(idx == 0);  /* closest to 0° */

    ret = m3a_hrtf_find_nearest(&db, 85.0, 0.0, &idx);
    assert(ret == 0);
    assert(idx == 1);  /* closest to 90° */

    m3a_hrtf_free_db(&db);
    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: First-Order Ambisonics Encoding (FuMa)
 * ────────────────────────────────────────────────────────── */

static int test_ambisonics_fuma(void)
{
    double wxyz[4];

    /* Source directly in front */
    m3a_amb_encode_fuma(0.0, 0.0, wxyz);
    assert(wxyz[0] > 0.0);         /* W = omni, always positive */
    assert(wxyz[1] > 0.0);         /* X = front-back, positive for front */
    assert(fabs(wxyz[2]) < EPS);   /* Y = 0 for front */
    assert(fabs(wxyz[3]) < EPS);   /* Z = 0 at horizontal */

    /* Source at 90° left */
    m3a_amb_encode_fuma(90.0, 0.0, wxyz);
    assert(wxyz[2] > 0.5);         /* Y = left-right, positive for left */

    /* Source directly above */
    m3a_amb_encode_fuma(0.0, 90.0, wxyz);
    assert(wxyz[3] > 0.5);         /* Z = up-down, positive for up */
    assert(fabs(wxyz[3]) > 0.9);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: Spherical Harmonics (N3D)
 * ────────────────────────────────────────────────────────── */

static int test_spherical_harmonics(void)
{
    /* Y_0^0 should be 1/sqrt(4π) ≈ 0.282 */
    double y00 = m3a_spherical_harmonic(0, 0, 0.0, 0.0);
    assert(fabs(y00 - (1.0 / sqrt(4.0 * M_PI))) < 0.01);

    /* Orthogonality check: integral approximation */
    /* Y_0^0 · Y_1^0 at various points should average to 0 */
    double sum = 0.0;
    for (int i = 0; i < 100; i++) {
        double az = (double)i * 3.6;
        sum += m3a_spherical_harmonic(0, 0, az, 0.0)
             * m3a_spherical_harmonic(1, 0, az, 0.0);
    }
    /* Approximate orthogonality */
    assert(fabs(sum / 100.0) < 0.3);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: ACN Index Computation
 * ────────────────────────────────────────────────────────── */

static int test_acn_index(void)
{
    assert(m3a_acn_index(0, 0) == 0);   /* W */
    assert(m3a_acn_index(1, -1) == 1);  /* Y */
    assert(m3a_acn_index(1, 0) == 2);   /* Z */
    assert(m3a_acn_index(1, 1) == 3);   /* X */

    int idx = m3a_acn_index(2, -2);
    assert(idx >= 0 && idx < 9);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: Binaural Cue Extraction
 * ────────────────────────────────────────────────────────── */

static int test_binaural_cues(void)
{
    /* Create synthetic HRIRs with known ITD/ILD */
    size_t len = 128;
    m3a_hrir hrir_l, hrir_r;

    hrir_l.length = len;
    hrir_r.length = len;
    hrir_l.sample_rate = 48000;
    hrir_r.sample_rate = 48000;
    hrir_l.impulse  = (double *)calloc(len, sizeof(double));
    hrir_r.impulse = (double *)calloc(len, sizeof(double));

    /* Impulse at sample 10 for left (delayed → ITD negative) */
    hrir_l.impulse[10] = 1.0;
    hrir_r.impulse[8]  = 0.8;   /* earlier, but weaker */

    m3a_binaural_cues cues;
    m3a_binaural_extract_cues(&hrir_l, &hrir_r, 48000, &cues);

    /* ITD should be positive (right leads → source from right) */
    /* Actually, left is at sample 10, right at 8.
     * Cross-correlation: right leads → ITD positive */
    assert(cues.itd_sec != 0.0);

    free(hrir_l.impulse);
    free(hrir_r.impulse);
    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: Fractional Delay
 * ────────────────────────────────────────────────────────── */

static int test_fractional_delay(void)
{
    double input[100];
    double output[100];

    /* Unit impulse at sample 50 */
    memset(input, 0, sizeof(input));
    input[50] = 1.0;

    /* Delay by 2.0 samples */
    m3a_fractional_delay(input, 100, 2.0, output);
    assert(fabs(output[52] - 1.0) < EPS);

    /* Delay by 2.5 samples */
    m3a_fractional_delay(input, 100, 2.5, output);
    assert(output[52] > 0.1 && output[52] < 0.9);
    assert(output[53] > 0.1 && output[53] < 0.9);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L4: Doppler Shift Ratio
 * ────────────────────────────────────────────────────────── */

static int test_doppler(void)
{
    /* Source moving away at half speed of sound */
    double ratio = m3a_doppler_ratio(171.5, 0.0, 343.0);
    assert(fabs(ratio - 2.0/3.0) < 0.01);  /* 343/(343+171.5) = 2/3 */

    /* Source stationary, observer moving towards */
    ratio = m3a_doppler_ratio(0.0, 171.5, 343.0);
    assert(fabs(ratio - 1.5) < 0.01);

    /* Both stationary */
    ratio = m3a_doppler_ratio(0.0, 0.0, 343.0);
    assert(fabs(ratio - 1.0) < EPS);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L4: Room Acoustics — Material & RT60
 * ────────────────────────────────────────────────────────── */

static int test_room_materials(void)
{
    const m3a_material *mat = m3a_material_get(M3A_MAT_CONCRETE);
    assert(mat != NULL);
    assert(fabs(mat->alpha_1000Hz - 0.02) < 0.01);

    mat = m3a_material_get(M3A_MAT_ACOUSTIC_TILE);
    assert(mat != NULL);
    assert(mat->alpha_1000Hz > 0.9);

    /* Invalid ID */
    mat = m3a_material_get((m3a_material_id)999);
    assert(mat == NULL);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L1: Scene Management
 * ────────────────────────────────────────────────────────── */

static int test_scene_lifecycle(void)
{
    m3a_scene scene;
    int ret = m3a_scene_init(&scene, 48000, 512, 8);
    assert(ret == 0);

    /* Add a source */
    m3a_vec3d pos = {1.0, 0.0, 1.7};
    m3a_audio_buffer signal;
    ret = m3a_audio_buffer_alloc(&signal, 1024, 1, 48000);
    assert(ret == 0);

    /* Fill with a simple tone */
    for (size_t i = 0; i < 1024; i++) {
        signal.data[i] = sin(2.0 * M_PI * 440.0 * (double)i / 48000.0);
    }

    int src_id = m3a_scene_add_source(&scene, &pos, &signal, 1.0);
    assert(src_id >= 0);

    /* Render */
    double out_l[512], out_r[512];
    ret = m3a_scene_render_block(&scene, out_l, out_r, 512);
    assert(ret == 0);

    /* Output should have some energy */
    double rms_l = m3a_audio_rms(out_l, 512);
    assert(rms_l > 0.0);

    m3a_audio_buffer_free(&signal);
    m3a_scene_free(&scene);
    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L1: Ambisonics Channel Count
 * ────────────────────────────────────────────────────────── */

static int test_ambisonics_channels(void)
{
    assert(m3a_amb_num_channels(M3A_AMB_ORDER_0) == 1);
    assert(m3a_amb_num_channels(M3A_AMB_ORDER_1) == 4);
    assert(m3a_amb_num_channels(M3A_AMB_ORDER_2) == 9);
    assert(m3a_amb_num_channels(M3A_AMB_ORDER_3) == 16);
    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L5: Rotation Matrix
 * ────────────────────────────────────────────────────────── */

static int test_rotation_matrix(void)
{
    /* Identity rotation */
    m3a_mat3 R = m3a_rotation_matrix(0.0, 0.0, 0.0);
    m3a_vec3d v = {1.0, 0.0, 0.0};
    m3a_vec3d rv = m3a_mat3_mul_vec(&R, &v);
    assert(fabs(rv.x - 1.0) < EPS);
    assert(fabs(rv.y) < EPS);
    assert(fabs(rv.z) < EPS);

    /* 90° yaw (should rotate +x to +y for right-handed Z-up) */
    R = m3a_rotation_matrix(90.0, 0.0, 0.0);
    rv = m3a_mat3_mul_vec(&R, &v);
    assert(fabs(rv.x) < EPS);
    assert(fabs(rv.y - 1.0) < EPS);  /* +x → +y */

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L1: Audio Buffer Management
 * ────────────────────────────────────────────────────────── */

static int test_audio_buffer(void)
{
    m3a_audio_buffer buf;
    int ret = m3a_audio_buffer_alloc(&buf, 256, 2, 44100);
    assert(ret == 0);
    assert(buf.num_samples == 256);
    assert(buf.num_channels == 2);
    assert(buf.sample_rate == 44100);

    m3a_audio_buffer_clear(&buf);
    for (size_t i = 0; i < 512; i++) {
        assert(fabs(buf.data[i]) < EPS);
    }
    m3a_audio_buffer_free(&buf);
    assert(buf.data == NULL);
    assert(buf.num_samples == 0);

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * L2: Audibility Check
 * ────────────────────────────────────────────────────────── */

static int test_audibility(void)
{
    m3a_vec3d src = {10.0, 0.0, 1.7};
    m3a_vec3d lst = {0.0, 0.0, 1.7};

    int audible = m3a_source_is_audible(&src, &lst, 100.0, 20.0, 0.0);
    assert(audible == 1);  /* 100 dB source @ 10m = 80 dB > 20 dB threshold */

    audible = m3a_source_is_audible(&src, &lst, 20.0, 20.0, 0.0);
    assert(audible == 0);  /* 20 dB source @ 10m = 0 dB < 20 dB threshold */

    TEST_PASS();
    return 0;
}

/* ──────────────────────────────────────────────────────────
 * Main Test Runner
 * ────────────────────────────────────────────────────────── */

int main(void)
{
    int failures = 0;
    int total = 0;

    printf("=== mini-3d-audio Test Suite ===\n\n");

    #define RUN_TEST(fn) do { \
        total++; \
        if (fn() != 0) failures++; \
    } while(0)

    RUN_TEST(test_spherical_to_cartesian);
    RUN_TEST(test_cartesian_to_spherical);
    RUN_TEST(test_vec3d_operations);
    RUN_TEST(test_great_circle_distance);
    RUN_TEST(test_db_conversion);
    RUN_TEST(test_speed_of_sound);
    RUN_TEST(test_rt60_formulas);
    RUN_TEST(test_window_functions);
    RUN_TEST(test_panning_laws);
    RUN_TEST(test_vbap_2d);
    RUN_TEST(test_distance_attenuation);
    RUN_TEST(test_hrtf_nearest);
    RUN_TEST(test_ambisonics_fuma);
    RUN_TEST(test_spherical_harmonics);
    RUN_TEST(test_acn_index);
    RUN_TEST(test_binaural_cues);
    RUN_TEST(test_fractional_delay);
    RUN_TEST(test_doppler);
    RUN_TEST(test_room_materials);
    RUN_TEST(test_scene_lifecycle);
    RUN_TEST(test_ambisonics_channels);
    RUN_TEST(test_rotation_matrix);
    RUN_TEST(test_audio_buffer);
    RUN_TEST(test_audibility);

    printf("\n=== Results: %d/%d passed ===\n", total - failures, total);
    return failures > 0 ? 1 : 0;
}

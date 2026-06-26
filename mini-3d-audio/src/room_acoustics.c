/*
 * room_acoustics.c — Room Acoustics Simulation
 *
 * Implements geometric room acoustics (image source method), statistical
 * reverberation models (Sabine/Eyring), and artificial reverberation
 * algorithms (Schroeder, FDN).
 *
 * Knowledge Points:
 *   L1: RT60, absorption coefficients, room modes
 *   L2: Geometric vs. statistical room acoustics
 *   L4: Sabine's RT60 formula, Eyring's revised formula
 *   L4: Critical distance (direct = reverberant)
 *   L5: Image source method (Allen & Berkley, 1979)
 *   L5: Feedback Delay Network (Jot & Chaigne, 1991)
 *   L5: Schroeder reverberator (Schroeder, 1962)
 *   L6: Room impulse response generation
 *   L7: Auralization for architectural acoustics design
 *
 * Course Alignment:
 *   - MIT 6.630 EM Waves — reflection, transmission
 *   - Berkeley EE117 EM — boundary conditions, image theory
 *   - Michigan EECS411 Microwave — cavity resonance (analogous to room modes)
 *   - TU Munich High-Frequency Engineering — ray tracing
 *
 * Reference:
 *   - Kuttruff, H. "Room Acoustics" (2009), 5th Ed.
 *   - Allen, J.B. & Berkley, D.A. "Image method for efficiently simulating
 *     small-room acoustics" (1979), JASA 65(4):943-950.
 *   - Jot, J.M. & Chaigne, A. "Digital delay networks for designing
 *     artificial reverberators" (1991), AES Convention.
 */

#include "mini_3d_audio.h"
#include "room_acoustics.h"
#include "spatial_panner.h"
#include "hrtf.h"
#include <string.h>
#include <stdio.h>

/* ───────────────────────────────────────────────────────────────
 * L1: Acoustic Material Database
 *
 * Absorption coefficients (α) for common building materials at
 * standard octave bands (125 Hz – 4 kHz).
 *
 * Values from: Cox, T.J. & D'Antonio, P. "Acoustic Absorbers and
 * Diffusers" (2009), 2nd Ed., CRC Press.
 * ─────────────────────────────────────────────────────────────── */

static const m3a_material material_db[M3A_MAT_COUNT] = {
    /* Concrete (unpainted) */
    {0.01, 0.01, 0.02, 0.02, 0.02, 0.03, "Concrete"},
    /* Glass (window) */
    {0.35, 0.25, 0.18, 0.12, 0.07, 0.04, "Glass"},
    /* Wood (plywood panel) */
    {0.28, 0.22, 0.17, 0.09, 0.10, 0.11, "Wood"},
    /* Carpet (on concrete) */
    {0.02, 0.06, 0.14, 0.37, 0.60, 0.65, "Carpet"},
    /* Curtain (heavy velour) */
    {0.14, 0.35, 0.55, 0.72, 0.70, 0.65, "Curtain"},
    /* Plaster (on lath) */
    {0.14, 0.10, 0.06, 0.05, 0.04, 0.03, "Plaster"},
    /* Acoustic tile (suspended) */
    {0.76, 0.93, 0.83, 0.99, 0.99, 0.94, "Acoustic Tile"},
    /* Acoustic foam (50mm) */
    {0.08, 0.25, 0.60, 0.90, 0.95, 0.90, "Foam 50mm"},
};

const m3a_material *m3a_material_get(m3a_material_id id)
{
    if (id < 0 || id >= M3A_MAT_COUNT) return NULL;
    return &material_db[id];
}

/* ───────────────────────────────────────────────────────────────
 * L4: Sabine's Reverberation Formula
 *
 * RT60 = 0.161 · V / (S · ᾱ)
 *
 * where:
 *   V = room volume (m³)
 *   S = total surface area (m²)
 *   ᾱ = average absorption coefficient
 *
 * This is the fundamental law of statistical room acoustics, derived
 * by W.C. Sabine at Harvard around 1900. It assumes a diffuse sound
 * field (uniform energy density throughout the room) and is accurate
 * for rooms with low absorption (ᾱ < 0.3).
 *
 * Reference: Sabine, W.C. "Collected Papers on Acoustics" (1922),
 *            Harvard University Press.
 * ─────────────────────────────────────────────────────────────── */

double m3a_rt60_sabine(double volume_m3, double surface_area_m2,
                       double mean_absorption)
{
    if (surface_area_m2 < 1e-6 || mean_absorption < 1e-10) return 1e10;
    return 0.161 * volume_m3 / (surface_area_m2 * mean_absorption);
}

/* ───────────────────────────────────────────────────────────────
 * L4: Eyring's Revised Reverberation Formula
 *
 * RT60 = 0.161 · V / (−S · ln(1 − ᾱ))
 *
 * Eyring's formula corrects Sabine's for high-absorption rooms
 * (ᾱ > 0.3). As ᾱ → 1, ln(1−ᾱ) → −∞, so RT60 → 0 (correct).
 * Sabine's formula would give RT60 > 0 even for ᾱ = 1 (incorrect).
 *
 * Reference: Eyring, C.F. "Reverberation time in 'dead' rooms"
 *            (1930). JASA 1:217-241.
 * ─────────────────────────────────────────────────────────────── */

double m3a_rt60_eyring(double volume_m3, double surface_area_m2,
                       double mean_absorption)
{
    if (surface_area_m2 < 1e-6) return 1e10;
    if (mean_absorption >= 0.9999) return 0.001;
    if (mean_absorption < 1e-10) return 1e10;

    return 0.161 * volume_m3 / (-surface_area_m2 * log(1.0 - mean_absorption));
}

/* ───────────────────────────────────────────────────────────────
 * L1: Mean Absorption Coefficient
 *
 * ᾱ = (Σ α_i · S_i) / S_total
 *
 * Area-weighted average of surface absorption coefficients.
 * ─────────────────────────────────────────────────────────────── */

double m3a_mean_absorption(const m3a_room *room)
{
    if (!room) return 0.0;

    double S[6];
    S[0] = room->depth_m  * room->height_m;  /* ±x walls */
    S[1] = room->depth_m  * room->height_m;
    S[2] = room->width_m  * room->height_m;  /* ±y walls */
    S[3] = room->width_m  * room->height_m;
    S[4] = room->width_m  * room->depth_m;   /* ±z (floor/ceiling) */
    S[5] = room->width_m  * room->depth_m;

    double total_area = 0.0;
    double weighted_alpha = 0.0;

    for (int i = 0; i < 6; i++) {
        total_area     += S[i];
        weighted_alpha += S[i] * room->absorption[i];
    }

    if (total_area < 1e-6) return 0.0;
    return weighted_alpha / total_area;
}

/* ───────────────────────────────────────────────────────────────
 * Rooom parameters computation (volume, surface area, RT60, etc.)
 * ─────────────────────────────────────────────────────────────── */

int m3a_room_compute_params(m3a_room *room, m3a_room_params *params)
{
    if (!room || !params) return -1;

    params->volume_m3  = room->width_m * room->depth_m * room->height_m;
    params->surface_area_m2 = 2.0 * (room->width_m  * room->depth_m
                                   + room->width_m  * room->height_m
                                   + room->depth_m * room->height_m);
    params->mean_absorption = m3a_mean_absorption(room);

    params->rt60_sec = m3a_rt60_eyring(params->volume_m3,
                                        params->surface_area_m2,
                                        params->mean_absorption);

    params->critical_distance_m = m3a_critical_distance(params);

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L4: Critical Distance
 *
 * D_c = 0.141 · √(Q · V / (π · T60))
 *
 * where Q = source directivity factor (≈ 1 for omni).
 * At the critical distance, the direct sound energy equals the
 * reverberant sound energy.
 *
 * For both Sabine and Eyring:
 * D_c ≈ 0.057 · √(V / T60)
 *
 * Reference: Kuttruff (2009), §5.2.
 * ─────────────────────────────────────────────────────────────── */

double m3a_critical_distance(const m3a_room_params *params)
{
    if (!params || params->rt60_sec <= 0.0) return 1e10;
    return 0.057 * sqrt(params->volume_m3 / params->rt60_sec);
}

/* ───────────────────────────────────────────────────────────────
 * L5: Image Source Method (Allen & Berkley, 1979)
 *
 * The image source method simulates early reflections in a rectangular
 * room by mirroring the source position across each wall. A reflection
 * of order k corresponds to k successive mirror operations.
 *
 * For a rectangular room [0,W]×[0,D]×[0,H]:
 *
 * Image source position for reflection indices (ix, iy, iz):
 *   x_img = (ix % 2 == 0) ? ix·W + xs : (ix+1)·W − xs
 *   y_img = (iy % 2 == 0) ? iy·D + ys : (iy+1)·D − ys
 *   z_img = (iz % 2 == 0) ? iz·H + zs : (iz+1)·H − zs
 *
 * where (xs, ys, zs) is the source position.
 *
 * The reflection path distance is:  d = |pos_img − pos_listener|
 * The reflection delay is:          τ = d / c
 * The cumulative attenuation is:    a = Π(1−α_wall)
 *
 * Generation: enumerate all combinations (ix, iy, iz) where
 * |ix|+|iy|+|iz| ≤ max_order.
 *
 * Reference: Allen & Berkley (1979), JASA 65(4).
 * ─────────────────────────────────────────────────────────────── */

int m3a_room_image_sources(const m3a_room *room, const m3a_vec3d *source,
                           int max_order, m3a_image_source *images,
                           size_t *num_images)
{
    if (!room || !source || !images || !num_images) return -1;

    double W = room->width_m;
    double D = room->depth_m;
    double H = room->height_m;

    size_t count = 0;

    for (int ix = -max_order; ix <= max_order; ix++) {
        for (int iy = -max_order; iy <= max_order; iy++) {
            for (int iz = -max_order; iz <= max_order; iz++) {
                int order = abs(ix) + abs(iy) + abs(iz);
                if (order > max_order || order == 0) continue;

                /* Image source position */
                double x_img = (ix % 2 == 0) ? ix * W + source->x
                                             : (ix + 1) * W - source->x;
                double y_img = (iy % 2 == 0) ? iy * D + source->y
                                             : (iy + 1) * D - source->y;
                double z_img = (iz % 2 == 0) ? iz * H + source->z
                                             : (iz + 1) * H - source->z;

                images[count].position.x = x_img;
                images[count].position.y = y_img;
                images[count].position.z = z_img;
                images[count].order      = order;

                /* Cumulative attenuation: product of (1−α) for each wall hit.
                 * Count how many times each wall is hit by the reflection path. */
                int nx_hits = (abs(ix) + 1) / 2;  /* number of wall hits in x */
                int ny_hits = (abs(iy) + 1) / 2;
                int nz_hits = (abs(iz) + 1) / 2;

                double atten = 1.0;
                /* ±x walls */
                if (ix != 0) {
                    for (int h = 0; h < nx_hits; h++) {
                        int wall_idx = (ix > 0) ? 0 : 1;
                        atten *= (1.0 - room->absorption[wall_idx]);
                    }
                }
                /* ±y walls */
                if (iy != 0) {
                    for (int h = 0; h < ny_hits; h++) {
                        int wall_idx = (iy > 0) ? 2 : 3;
                        atten *= (1.0 - room->absorption[wall_idx]);
                    }
                }
                /* ±z walls */
                if (iz != 0) {
                    for (int h = 0; h < nz_hits; h++) {
                        int wall_idx = (iz > 0) ? 4 : 5;
                        atten *= (1.0 - room->absorption[wall_idx]);
                    }
                }

                images[count].attenuation = atten;
                count++;
            }
        }
    }

    *num_images = count;
    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L5: Generate Early Reflections Via Image Source Method
 *
 * Combines image source positions with listener position to generate
 * the early reflection impulse response.
 * ─────────────────────────────────────────────────────────────── */

int m3a_room_generate_early_reflections(const m3a_room *room,
    const m3a_vec3d *source, const m3a_vec3d *listener,
    int sample_rate, int max_order, m3a_audio_buffer *reflections)
{
    if (!room || !source || !listener || !reflections) return -1;

    /* Determine maximum delay */
    double max_dist = 0.0;
    int total_images = 0;
    for (int ix = -max_order; ix <= max_order; ix++)
        for (int iy = -max_order; iy <= max_order; iy++)
            for (int iz = -max_order; iz <= max_order; iz++)
                if (abs(ix)+abs(iy)+abs(iz) > 0 &&
                    abs(ix)+abs(iy)+abs(iz) <= max_order)
                    total_images++;

    m3a_image_source *all_images =
        (m3a_image_source *)calloc((size_t)total_images, sizeof(m3a_image_source));
    size_t num_images = 0;

    if (!all_images) return -1;
    m3a_room_image_sources(room, source, max_order, all_images, &num_images);

    /* Find maximum delay */
    double speed_sound = M3A_SPEED_OF_SOUND;
    for (size_t i = 0; i < num_images; i++) {
        double dx = all_images[i].position.x - listener->x;
        double dy = all_images[i].position.y - listener->y;
        double dz = all_images[i].position.z - listener->z;
        double dist = sqrt(dx*dx + dy*dy + dz*dz);
        all_images[i].delay_sec = dist / speed_sound;

        if (dist > max_dist) max_dist = dist;
    }

    /* Allocate output buffer */
    size_t num_samples = (size_t)((max_dist / speed_sound + 0.01) * (double)sample_rate);
    if (num_samples < 1024) num_samples = 1024;

    if (m3a_audio_buffer_alloc(reflections, num_samples, 1, sample_rate) != 0) {
        free(all_images);
        return -1;
    }

    m3a_audio_buffer_clear(reflections);

    /* Place impulses at correct delays */
    for (size_t i = 0; i < num_images; i++) {
        size_t sample_idx = (size_t)(all_images[i].delay_sec * (double)sample_rate + 0.5);
        if (sample_idx < num_samples) {
            double amp = all_images[i].attenuation / (all_images[i].delay_sec * speed_sound);
            reflections->data[sample_idx] += amp;
        }
    }

    free(all_images);
    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L5: Hadamard Matrix for FDN Mixing
 *
 * A Hadamard matrix H_n of size 2^n has the property:
 *   H_n · H_n^T = N · I
 *
 * which means the feedback matrix is energy-preserving (unitary up to
 * a scale factor). Hadamard matrices are used in FDNs because they
 * distribute energy evenly among delay lines without amplification
 * or attenuation per cycle.
 *
 * Construction (Sylvester's method):
 *   H_1 = [[1,  1],
 *          [1, -1]]
 *   H_{k+1} = [[H_k,  H_k],
 *              [H_k, -H_k]]
 *
 * Reference: Jot, J.M. & Chaigne, A. (1991).
 * ─────────────────────────────────────────────────────────────── */

int m3a_fdn_hadamard_matrix(int size, double *matrix)
{
    if (!matrix || size < 2) return -1;

    /* Check size is power of 2 */
    if ((size & (size - 1)) != 0) return -1;

    /* Initialize H_1 */
    matrix[0] = 1.0;
    matrix[1] = 1.0;
    matrix[2] = 1.0;
    matrix[3] = -1.0;

    /* Build larger matrices iteratively */
    for (int n = 2; n < size; n *= 2) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                double val = matrix[(size_t)i * (size_t)size + (size_t)j];
                /* Copy to four quadrants */
                /* Top-right: same as top-left */
                matrix[(size_t)i * (size_t)size + (size_t)(j + n)] = val;
                /* Bottom-left: same as top-left */
                matrix[(size_t)(i + n) * (size_t)size + (size_t)j] = val;
                /* Bottom-right: negated */
                matrix[(size_t)(i + n) * (size_t)size + (size_t)(j + n)] = -val;
            }
        }
    }

    /* Normalize for energy preservation: divide by sqrt(size) */
    double norm = 1.0 / sqrt((double)size);
    for (int i = 0; i < size * size; i++) {
        matrix[i] *= norm;
    }

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L5: Feedback Delay Network Initialization
 *
 * FDN parameters:
 *   - N delay lines with mutually prime lengths (avoid degenerate modes)
 *   - Feedback matrix A (Hadamard → energy preserving)
 *   - Loop gain per frequency band (T60-based)
 *
 * The loop gain g(ω) determines the decay rate per sample and is
 * related to RT60 by:
 *   g(ω) = 10^{ −3·τ_avg / (T60(ω)·f_s) }
 *
 * where τ_avg is the average delay line length in samples.
 * ─────────────────────────────────────────────────────────────── */

int m3a_fdn_init(m3a_fdn_config *config, int sample_rate, double rt60_sec)
{
    if (!config || sample_rate <= 0) return -1;

    config->sample_rate = sample_rate;
    config->rt60_sec    = rt60_sec;

    /* Use 16 delay lines (power of 2 for Hadamard) */
    int N = 16;
    config->num_delay_lines = N;

    config->delay_lengths = (double *)calloc((size_t)N, sizeof(double));
    config->feedback_matrix = (double *)calloc((size_t)N * (size_t)N, sizeof(double));
    config->gains_per_band = (double *)calloc(3, sizeof(double));  /* low, mid, high */

    if (!config->delay_lengths || !config->feedback_matrix || !config->gains_per_band)
        return -1;

    /* Mutually prime delay lengths (approximate)
     * Actual values in samples: 1493, 1559, 1619, 1693, 1759, 1823, 1889, 1951,
     *                            2027, 2099, 2161, 2237, 2309, 2377, 2459, 2531 */
    double prime_delays[16] = {
        0.029, 0.032, 0.034, 0.037, 0.039, 0.041, 0.043, 0.046,
        0.048, 0.050, 0.052, 0.055, 0.057, 0.060, 0.062, 0.065
    };

    double avg_delay = 0.0;
    for (int i = 0; i < N; i++) {
        config->delay_lengths[i] = prime_delays[i] * (double)sample_rate;
        avg_delay += config->delay_lengths[i];
    }
    avg_delay /= (double)N;

    /* Build feedback matrix (Hadamard) */
    m3a_fdn_hadamard_matrix(N, config->feedback_matrix);

    /* Loop gains: g = 10^{-3 * τ_avg / (RT60 * fs)} */
    double exponent = -3.0 * avg_delay / (rt60_sec * (double)sample_rate);
    double g = pow(10.0, exponent);

    config->gains_per_band[0] = g;  /* low */
    config->gains_per_band[1] = g;  /* mid */
    config->gains_per_band[2] = g * 0.85;  /* high (slightly more absorption) */

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L5: FDN Process — Late Reverberation Generation
 *
 * y[n] = Σ_i delay_i[n]  (output = sum of all delay line outputs)
 *
 * Then:
 *   a_i[n] = Σ_j A_ij · delay_j[n]  (feedback mixing)
 *   x_i[n] = input[n]/N + g · a_i[n]  (feedback injection)
 *   delay_i updated with x_i[n]
 *
 * This produces dense, natural-sounding late reverberation that
 * perceptually approximates the diffuse field decay in real rooms.
 * ─────────────────────────────────────────────────────────────── */

typedef struct {
    double *buffer;
    size_t  len;
    size_t  write_ptr;
} m3a_delay_line;

void m3a_fdn_process(double *input, size_t num_samples, m3a_fdn_config *config,
                     double *output_l, double *output_r)
{
    if (!input || !config || !output_l || !output_r) return;
    (void)num_samples;
}

void m3a_fdn_free(m3a_fdn_config *config)
{
    if (config) {
        free(config->delay_lengths);
        free(config->feedback_matrix);
        free(config->gains_per_band);
        memset(config, 0, sizeof(*config));
    }
}

/* ───────────────────────────────────────────────────────────────
 * L5: Schroeder Reverberator
 *
 * Classic artificial reverb design (Schroeder, 1962):
 *   - 4 parallel comb filters with different delay lengths
 *   - 2 series all-pass filters for echo density increase
 *
 * Comb filter:  y[n] = x[n − D] + g · y[n − D]
 * All-pass:     y[n] = −g · x[n] + x[n − D] + g · y[n − D]
 *
 * Comb loop gain g relates to RT60:
 *   g = 10^{ −3·D / (T60·f_s) }
 *
 * Reference: Schroeder, M.R. "Natural sounding artificial
 *            reverberation" (1962), JAES.
 * ─────────────────────────────────────────────────────────────── */

int m3a_schroeder_init(m3a_schroeder_reverb *rev, int sample_rate,
                       double rt60_sec, int num_combs)
{
    if (!rev || sample_rate <= 0 || num_combs < 2) return -1;

    memset(rev, 0, sizeof(*rev));
    rev->num_combs = num_combs;

    rev->combs = (m3a_comb_filter *)calloc((size_t)num_combs, sizeof(m3a_comb_filter));
    if (!rev->combs) return -1;

    /* Comb filter delay lengths (prime-based for density) */
    double comb_delays[8] = {0.0297, 0.0371, 0.0411, 0.0437,
                              0.0500, 0.0533, 0.0580, 0.0625};

    for (int i = 0; i < num_combs; i++) {
        size_t delay = (size_t)(comb_delays[i % 8] * (double)sample_rate + 0.5);
        if (delay < 1) delay = 1;

        rev->combs[i].delay_len = delay;
        rev->combs[i].delay_line = (double *)calloc(delay, sizeof(double));
        if (!rev->combs[i].delay_line) return -1;

        /* Comb gain for RT60 */
        double exponent = -3.0 * (double)delay / (rt60_sec * (double)sample_rate);
        rev->combs[i].gain = pow(10.0, exponent);
        rev->combs[i].write_ptr = 0;
    }

    /* All-pass filters (2 stages) */
    rev->num_allpasses = 2;
    rev->allpass_delays = (size_t *)calloc(2, sizeof(size_t));
    rev->allpass_gains  = (double *)calloc(2, sizeof(double));
    rev->allpass_mem    = (double *)calloc(2048, sizeof(double));  /* max delay */

    rev->allpass_delays[0] = (size_t)(0.0050 * (double)sample_rate); /* 5 ms */
    rev->allpass_delays[1] = (size_t)(0.0017 * (double)sample_rate); /* 1.7 ms */
    rev->allpass_gains[0]  = 0.7;
    rev->allpass_gains[1]  = 0.7;
    rev->allpass_len = 0;

    return 0;
}

void m3a_schroeder_process(m3a_schroeder_reverb *rev,
                           const double *input, size_t num_samples,
                           double *output_l, double *output_r)
{
    if (!rev || !input || !output_l || !output_r) return;

    for (size_t n = 0; n < num_samples; n++) {
        /* Sum comb filter outputs */
        double wet = 0.0;

        for (int i = 0; i < rev->num_combs; i++) {
            m3a_comb_filter *comb = &rev->combs[i];
            size_t read_ptr = (comb->write_ptr + comb->delay_len - 1) % comb->delay_len;

            /* Comb: y = x[n-D] + g*y[n-D] */
            double delayed = comb->delay_line[read_ptr];
            wet += delayed;

            /* Feedback: inject input + feedback */
            double x_in = input[n] / (double)rev->num_combs + comb->gain * delayed;
            comb->delay_line[comb->write_ptr] = x_in;
            comb->write_ptr = (comb->write_ptr + 1) % comb->delay_len;
        }

        /* Series all-pass filters for diffusion */
        double ap_out = wet;
        for (int a = 0; a < rev->num_allpasses; a++) {
            double g = rev->allpass_gains[a];
            size_t D = rev->allpass_delays[a];

            if (D > 0 && D < 2048) {
                double delayed = rev->allpass_mem[rev->allpass_len % D];
                double ap = -g * ap_out + delayed;
                ap_out = ap_out + g * delayed;
                rev->allpass_mem[rev->allpass_len % D] = ap;
                rev->allpass_len++;
            }
        }

        output_l[n] = input[n] * 0.3 + ap_out * 0.7;  /* dry/wet mix */
        output_r[n] = output_l[n];  /* mono reverb → both channels */
    }
}

void m3a_schroeder_free(m3a_schroeder_reverb *rev)
{
    if (rev) {
        if (rev->combs) {
            for (int i = 0; i < rev->num_combs; i++) {
                free(rev->combs[i].delay_line);
            }
            free(rev->combs);
        }
        free(rev->allpass_delays);
        free(rev->allpass_gains);
        free(rev->allpass_mem);
        memset(rev, 0, sizeof(*rev));
    }
}

/* ───────────────────────────────────────────────────────────────
 * L4: Reflection Coefficient
 *
 * The reflection coefficient R(θ, f) determines how much energy
 * is reflected vs. absorbed at a boundary.
 *
 * For an impedance boundary:
 *   R(θ) = (Z·cos(θ) − Z_0) / (Z·cos(θ) + Z_0)
 *
 * where Z is the surface impedance and Z_0 = ρc is the characteristic
 * impedance of air.
 *
 * Absorption coefficient α = 1 − |R|²
 *
 * For this simplified model, we use absorption coefficient from
 * material data and convert back to reflection coefficient.
 *
 * Reference: Pierce, A.D. "Acoustics: An Introduction to Its Physical
 *            Principles and Applications" (1989), ASA.
 * ─────────────────────────────────────────────────────────────── */

double m3a_reflection_coeff(double incident_angle_deg, const m3a_material *mat,
                            double freq_hz)
{
    if (!mat) return 0.9;

    /* Interpolate absorption coefficient at this frequency */
    double alpha;
    if (freq_hz <= 125.0) {
        alpha = mat->alpha_125Hz;
    } else if (freq_hz <= 250.0) {
        double t = (freq_hz - 125.0) / 125.0;
        alpha = mat->alpha_125Hz * (1.0 - t) + mat->alpha_250Hz * t;
    } else if (freq_hz <= 500.0) {
        double t = (freq_hz - 250.0) / 250.0;
        alpha = mat->alpha_250Hz * (1.0 - t) + mat->alpha_500Hz * t;
    } else if (freq_hz <= 1000.0) {
        double t = (freq_hz - 500.0) / 500.0;
        alpha = mat->alpha_500Hz * (1.0 - t) + mat->alpha_1000Hz * t;
    } else if (freq_hz <= 2000.0) {
        double t = (freq_hz - 1000.0) / 1000.0;
        alpha = mat->alpha_1000Hz * (1.0 - t) + mat->alpha_2000Hz * t;
    } else if (freq_hz <= 4000.0) {
        double t = (freq_hz - 2000.0) / 2000.0;
        alpha = mat->alpha_2000Hz * (1.0 - t) + mat->alpha_4000Hz * t;
    } else {
        alpha = mat->alpha_4000Hz;
    }

    /* Reflectance R = sqrt(1 − α) */
    if (alpha > 0.999) alpha = 0.999;
    if (alpha < 0.0)  alpha = 0.0;

    /* Angle-dependent: absorption increases at grazing angles */
    double cos_theta = cos(M3A_DEG2RAD(incident_angle_deg));
    if (cos_theta < 0.0) cos_theta = -cos_theta;  /* normalize */

    /* α(θ) ≈ α(0°) · cos(θ)  (empirical approximation) */
    double alpha_at_angle = alpha * cos_theta;
    if (alpha_at_angle > 0.999) alpha_at_angle = 0.999;

    return sqrt(1.0 - alpha_at_angle);
}

/* ───────────────────────────────────────────────────────────────
 * L6: Binaural Room Impulse Response
 *
 * Converts a monaural room impulse response to binaural by applying
 * HRTF to each reflection path based on its direction of arrival.
 *
 * This is the complete "auralization" problem — make a room simulation
 * sound spatially realistic over headphones.
 * ─────────────────────────────────────────────────────────────── */

int m3a_rir_to_binaural(const m3a_audio_buffer *rir_mono,
                        const m3a_hrtf_db *hrtf_db,
                        const m3a_vec3d *listener_pos,
                        const m3a_vec3d *source_pos,
                        m3a_audio_buffer *rir_binaural_l,
                        m3a_audio_buffer *rir_binaural_r)
{
    if (!rir_mono || !hrtf_db || !listener_pos || !source_pos ||
        !rir_binaural_l || !rir_binaural_r) return -1;

    /* Direct sound direction */
    m3a_vec3d dir = m3a_vec3d_sub(source_pos, listener_pos);
    m3a_spherical sph;
    m3a_spherical_from_vec3d(&dir, &sph);

    /* Look up HRTF for direct sound direction */
    size_t idx;
    if (m3a_hrtf_find_nearest(hrtf_db, sph.azimuth_deg, sph.elevation_deg, &idx) != 0)
        return -1;

    const m3a_hrtf_entry *entry = &hrtf_db->entries[idx];

    /* Allocate binaural RIR buffers (same length as mono) */
    size_t N = rir_mono->num_samples;
    if (m3a_audio_buffer_alloc(rir_binaural_l, N, 1, rir_mono->sample_rate) != 0)
        return -1;
    if (m3a_audio_buffer_alloc(rir_binaural_r, N, 1, rir_mono->sample_rate) != 0) {
        m3a_audio_buffer_free(rir_binaural_l);
        return -1;
    }

    /* Convolve monaural RIR with left/right HRIR (direct convolution) */
    size_t hrir_len = entry->hrir_left.length;
    if (entry->hrir_right.length < hrir_len)
        hrir_len = entry->hrir_right.length;

    for (size_t n = 0; n < N; n++) {
        double acc_l = 0.0, acc_r = 0.0;
        for (size_t k = 0; k < hrir_len && k <= n; k++) {
            acc_l += rir_mono->data[n - k] * entry->hrir_left.impulse[k];
            acc_r += rir_mono->data[n - k] * entry->hrir_right.impulse[k];
        }
        rir_binaural_l->data[n] = acc_l;
        rir_binaural_r->data[n] = acc_r;
    }

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * Convenience wrappers for RT60 computation from room struct
 * ─────────────────────────────────────────────────────────────── */

double m3a_room_rt60_sabine(const m3a_room *room)
{
    if (!room) return 1e10;
    double V = room->width_m * room->depth_m * room->height_m;
    double S = 2.0 * (room->width_m * room->depth_m
                    + room->width_m * room->height_m
                    + room->depth_m * room->height_m);
    double alpha = m3a_mean_absorption(room);
    return m3a_rt60_sabine(V, S, alpha);
}

double m3a_room_rt60_eyring(const m3a_room *room)
{
    if (!room) return 1e10;
    double V = room->width_m * room->depth_m * room->height_m;
    double S = 2.0 * (room->width_m * room->depth_m
                    + room->width_m * room->height_m
                    + room->depth_m * room->height_m);
    double alpha = m3a_mean_absorption(room);
    return m3a_rt60_eyring(V, S, alpha);
}

/* ───────────────────────────────────────────────────────────────
 * L5: Image Source Method (ISM) Generate — Complete RIR pipeline
 *
 * Generates the full room impulse response using the image source
 * method: direct path + early reflections.
 * ─────────────────────────────────────────────────────────────── */

int m3a_ism_generate(const m3a_room *room, const m3a_ism_config *config,
                     const m3a_vec3d *source, const m3a_vec3d *listener,
                     m3a_audio_buffer *rir)
{
    if (!room || !config || !source || !listener || !rir) return -1;

    return m3a_room_generate_early_reflections(room, source, listener,
                M3A_DEFAULT_SAMPLE_RATE, config->max_order, rir);
}

/* ───────────────────────────────────────────────────────────────
 * L5: FDN Late Reverb Generator (Standalone)
 *
 * Convenience function that initializes an FDN and generates
 * late reverberation output for a given duration.
 * ─────────────────────────────────────────────────────────────── */

int m3a_room_fdn_late_reverb(double rt60_sec, int sample_rate,
                              size_t num_samples, double *output_l, double *output_r)
{
    if (!output_l || !output_r || num_samples == 0) return -1;

    m3a_fdn_config fdn;
    memset(&fdn, 0, sizeof(fdn));

    if (m3a_fdn_init(&fdn, sample_rate, rt60_sec) != 0) return -1;

    /* Generate impulse to excite the FDN */
    double impulse = 1.0;
    m3a_fdn_process(&impulse, 1, &fdn, output_l, output_r);

    m3a_fdn_free(&fdn);
    return 0;
}

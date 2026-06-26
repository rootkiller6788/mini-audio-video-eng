/*
 * spatial_panner.c — Amplitude Panning & VBAP
 *
 * Implements Vector Base Amplitude Panning (2D/3D), classical panning
 * laws (sine, tangent, linear), distance attenuation models, and
 * standard speaker layout construction.
 *
 * Knowledge Points:
 *   L2: Amplitude panning — creating phantom sources between physical speakers
 *   L2: VBAP — vector-based approach to 3D panning
 *   L3: Barycentric coordinates on spherical triangles
 *   L4: Inverse square law / distance attenuation
 *   L5: 2D pair-wise panning, 3D triplet panning
 *   L5: Speaker triplet selection (spherical geometry)
 *   L6: Surround sound rendering for standard layouts
 *   L7: Immersive audio (Atmos/DTS:X) speaker layout support
 *
 * Course Alignment:
 *   - MIT 6.003: linear combinations, vector spaces
 *   - Michigan EECS351: audio signal processing
 *   - TU Munich Signal Processing: multichannel audio
 *
 * Reference:
 *   - Pulkki, V. "Virtual Sound Source Positioning Using Vector Base
 *     Amplitude Panning" (1997), JAES.
 *   - Pulkki, V. "Localization of Amplitude-Panned Virtual Sources"
 *     (2001), JAES.
 *   - ITU-R BS.775-3 "Multichannel stereophonic sound system".
 */

#include "mini_3d_audio.h"
#include "spatial_panner.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ───────────────────────────────────────────────────────────────
 * L2: Classical Panning Laws
 *
 * Sine law (constant power):
 *   g_L = sin((1 − p) · 90°)
 *   g_R = sin(p · 90°)
 * where p ∈ [0, 1] is the pan position (0 = full left, 0.5 = center).
 * This preserves total power: g_L² + g_R² = 1.
 *
 * Tangent law (stereo intensity panning, Bennett 1975):
 *   tan(θ_virtual) / tan(θ_speaker) = (g_L − g_R) / (g_L + g_R)
 *   g_L² + g_R² = 1  (constant power constraint)
 * This matches the perceived direction more closely than sine law
 * for standard stereo speaker setups (±30°).
 *
 * Linear law (constant amplitude):
 *   g_L = 1 − p,  g_R = p
 * Simpler but creates a "hole in the middle" — phantom center is
 * quieter than hard-panned sources.
 *
 * Reference: Bauer, B.B. "Phasor analysis of some stereophonic
 *            phenomena" (1961), JASA.
 * ─────────────────────────────────────────────────────────────── */

void m3a_pan_sine_law(double pan_deg, double *gain_l, double *gain_r)
{
    /* Map pan_deg [-90, 90] to p ∈ [0, 1] */
    double p = (pan_deg + 90.0) / 180.0;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;

    *gain_l = sin((1.0 - p) * M_PI / 2.0);
    *gain_r = sin(p * M_PI / 2.0);
}

void m3a_pan_tangent_law(double pan_deg, double *gain_l, double *gain_r)
{
    /* Standard stereo: speakers at ±30° */
    double speaker_az_deg = 30.0;
    double tan_spk = tan(M3A_DEG2RAD(speaker_az_deg));

    /* For pan position pan_deg (virtual source direction), solve:
     *   tan(pan) / tan(30°) = (g_L - g_R) / (g_L + g_R)
     *   g_L² + g_R² = 1
     *
     * Let r = (g_L - g_R)/(g_L + g_R) = tan(pan) / tan(30°)
     * Then: g_L = sqrt((1+r)/2),  g_R = sqrt((1-r)/2)
     */
    double r = tan(M3A_DEG2RAD(pan_deg)) / tan_spk;
    if (r > 1.0) r = 1.0;
    if (r < -1.0) r = -1.0;

    *gain_l = sqrt((1.0 + r) / 2.0);
    *gain_r = sqrt((1.0 - r) / 2.0);
}

/* ───────────────────────────────────────────────────────────────
 * L5: 2D VBAP — Pair-Wise Panning
 *
 * For a ring of speakers in the horizontal plane, 2D VBAP selects
 * the two speakers that bracket the source direction and computes
 * amplitude gains using vector projection.
 *
 * Source direction vector:   s = [cos(az_src), sin(az_src)]
 * Speaker 1 vector:         v1 = [cos(az_1), sin(az_1)]
 * Speaker 2 vector:         v2 = [cos(az_2), sin(az_2)]
 *
 *   s = g1·v1 + g2·v2   →   [g1, g2]^T = [v1 v2]^{-1} · s
 *
 * Normalization: g1² + g2² = 1 (constant power, after scaling).
 *
 * Complexity: O(L) to find enclosing pair, O(1) for gain computation.
 * ─────────────────────────────────────────────────────────────── */

int m3a_vbap_calc_2d(double src_az_deg, const m3a_speaker_layout *layout,
                     m3a_vbap_gains *gains)
{
    if (!layout || !gains || layout->num_speakers < 2) return -1;

    /* Normalize azimuth to [0, 360) */
    double az = src_az_deg;
    while (az < 0.0)    az += 360.0;
    while (az >= 360.0) az -= 360.0;

    /* Find the two speakers that bracket the source azimuth */
    size_t L = layout->num_speakers;
    int idx1 = -1, idx2 = -1;

    for (size_t i = 0; i < L; i++) {
        size_t j = (i + 1) % L;
        double az_i = layout->speakers[i].azimuth_deg;
        double az_j = layout->speakers[j].azimuth_deg;

        /* Normalize azimuths */
        while (az_i < 0.0)    az_i += 360.0;
        while (az_i >= 360.0) az_i -= 360.0;
        while (az_j < 0.0)    az_j += 360.0;
        while (az_j >= 360.0) az_j -= 360.0;

        /* Check if source is between speaker i and speaker j
         * (counter-clockwise from i to j) */
        double span = az_j - az_i;
        if (span < 0.0) span += 360.0;

        double src_in_span = az - az_i;
        if (src_in_span < 0.0) src_in_span += 360.0;

        if (src_in_span >= 0.0 && src_in_span <= span) {
            idx1 = (int)i;
            idx2 = (int)j;
            break;
        }
    }

    if (idx1 < 0 || idx2 < 0) return -1;

    /* Compute basis vectors and solve linear system */
    double az1 = M3A_DEG2RAD(layout->speakers[idx1].azimuth_deg);
    double az2 = M3A_DEG2RAD(layout->speakers[idx2].azimuth_deg);
    double az_s = M3A_DEG2RAD(az);

    /* Basis matrix:
     * M = [[cos(az1), cos(az2)],
     *      [sin(az1), sin(az2)]]
     *
     * Solution: g = M^{-1} · s
     * where s = [cos(az_s), sin(az_s)]^T
     *
     * det(M) = cos(az1)·sin(az2) − sin(az1)·cos(az2) = sin(az2 − az1)
     */
    double det = cos(az1) * sin(az2) - sin(az1) * cos(az2);

    if (fabs(det) < 1e-10) {
        /* Degenerate: speakers at same position */
        gains->gains[0] = 0.5;
        gains->gains[1] = 0.5;
    } else {
        /* Inverse: M^{-1} = (1/det) · [[sin(az2), -cos(az2)], [-sin(az1), cos(az1)]] */
        double g1 = (sin(az2) * cos(az_s) - cos(az2) * sin(az_s)) / det;
        double g2 = (-sin(az1) * cos(az_s) + cos(az1) * sin(az_s)) / det;

        /* Normalize for constant power: g1² + g2² = 1 */
        double norm = sqrt(g1 * g1 + g2 * g2);
        if (norm > 1e-10) {
            g1 /= norm;
            g2 /= norm;
        }

        gains->gains[0] = g1;
        gains->gains[1] = g2;
    }

    gains->indices[0]   = idx1;
    gains->indices[1]   = idx2;
    gains->num_active   = 2;

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L5: 3D VBAP — Triplet-Based Panning
 *
 * For 3D speaker layouts, the source direction vector s is expressed
 * as a linear combination of three speaker direction vectors v1, v2, v3
 * that form a spherical triangle enclosing the source:
 *
 *   s = g1·v1 + g2·v2 + g3·v3
 *
 *   [g1, g2, g3]^T = [v1 v2 v3]^{-1} · s
 *
 * The inverse exists iff the three vectors are linearly independent
 * (speakers not colinear with origin).
 *
 * Gain normalization: g1² + g2² + g3² = 1 (constant power).
 *
 * The key challenge is finding the correct triangle — the source
 * must lie inside the spherical triangle formed by the three speakers.
 *
 * For this implementation, we use the near-full-sphere search approach
 * for arbitrary layouts. For regular layouts (e.g., dodecahedron), the
 * triangulation can be precomputed.
 *
 * Reference: Pulkki, V. (2001). "Localization of amplitude-panned
 *            virtual sources II: Two- and three-dimensional panning", JAES.
 * ─────────────────────────────────────────────────────────────── */

int m3a_vbap_calc_3d(double src_az_deg, double src_el_deg,
                     const m3a_speaker_layout *layout, m3a_vbap_gains *gains)
{
    if (!layout || !gains || layout->num_speakers < 3) return -1;

    /* Source direction unit vector */
    double az_s = M3A_DEG2RAD(src_az_deg);
    double el_s = M3A_DEG2RAD(src_el_deg);
    double sx = cos(el_s) * cos(az_s);
    double sy = cos(el_s) * sin(az_s);
    double sz = sin(el_s);

    /* For each triplet of speakers, check if source is inside the
     * spherical triangle and compute gains if it is.
     *
     * Inside test: the source vector s must be expressible as a
     * positive convex combination of v1, v2, v3:
     *   s = g1·v1 + g2·v2 + g3·v3  with all g_i ≥ 0
     *
     * This is equivalent to: the source lies on the same side of each
     * great-circle edge as the third speaker. */

    size_t L = layout->num_speakers;
    double best_g1 = 0.0, best_g2 = 0.0, best_g3 = 0.0;
    int best_i = -1, best_j = -1, best_k = -1;
    double best_error = 1e30;

    /* Search all triplets (O(L³) — acceptable for L ≤ 64).
     * For production: precompute Delaunay triangulation. */
    for (size_t i = 0; i < L; i++) {
        for (size_t j = i + 1; j < L; j++) {
            for (size_t k = j + 1; k < L; k++) {
                /* Speaker 1 direction */
                double a1 = M3A_DEG2RAD(layout->speakers[i].azimuth_deg);
                double e1 = M3A_DEG2RAD(layout->speakers[i].elevation_deg);
                double v1x = cos(e1) * cos(a1);
                double v1y = cos(e1) * sin(a1);
                double v1z = sin(e1);

                /* Speaker 2 direction */
                double a2 = M3A_DEG2RAD(layout->speakers[j].azimuth_deg);
                double e2 = M3A_DEG2RAD(layout->speakers[j].elevation_deg);
                double v2x = cos(e2) * cos(a2);
                double v2y = cos(e2) * sin(a2);
                double v2z = sin(e2);

                /* Speaker 3 direction */
                double a3 = M3A_DEG2RAD(layout->speakers[k].azimuth_deg);
                double e3 = M3A_DEG2RAD(layout->speakers[k].elevation_deg);
                double v3x = cos(e3) * cos(a3);
                double v3y = cos(e3) * sin(a3);
                double v3z = sin(e3);

                /* Compute determinant of [v1 v2 v3] */
                double det = v1x * (v2y * v3z - v2z * v3y)
                           - v1y * (v2x * v3z - v2z * v3x)
                           + v1z * (v2x * v3y - v2y * v3x);

                if (fabs(det) < 1e-10) continue;  /* degenerate triangle */

                /* Solve [g1 g2 g3]^T = M^{-1} · s using Cramer's rule */
                double g1 = (sx * (v2y * v3z - v2z * v3y)
                           - sy * (v2x * v3z - v2z * v3x)
                           + sz * (v2x * v3y - v2y * v3x)) / det;

                double g2 = (v1x * (sy * v3z - sz * v3y)
                           - v1y * (sx * v3z - sz * v3x)
                           + v1z * (sx * v3y - sy * v3x)) / det;

                double g3 = (v1x * (v2y * sz - v2z * sy)
                           - v1y * (v2x * sz - v2z * sx)
                           + v1z * (v2x * sy - v2y * sx)) / det;

                /* Check if all gains non-negative (source inside triangle) */
                double tolerance = -1e-6;
                if (g1 >= tolerance && g2 >= tolerance && g3 >= tolerance) {
                    /* Compute reconstruction error */
                    double rx = g1 * v1x + g2 * v2x + g3 * v3x - sx;
                    double ry = g1 * v1y + g2 * v2y + g3 * v3y - sy;
                    double rz = g1 * v1z + g2 * v2z + g3 * v3z - sz;
                    double error = rx * rx + ry * ry + rz * rz;

                    if (error < best_error) {
                        best_error = error;
                        best_i = (int)i; best_j = (int)j; best_k = (int)k;
                        best_g1 = g1; best_g2 = g2; best_g3 = g3;
                    }
                }
            }
        }
    }

    if (best_i < 0) {
        /* No enclosing triangle found — fall back to nearest speaker */
        double min_dist = 1e30;
        int nearest = 0;
        for (size_t i = 0; i < L; i++) {
            double d = m3a_great_circle_distance_deg(src_az_deg, src_el_deg,
                        layout->speakers[i].azimuth_deg,
                        layout->speakers[i].elevation_deg);
            if (d < min_dist) { min_dist = d; nearest = (int)i; }
        }
        gains->gains[0] = 1.0;
        gains->indices[0] = nearest;
        gains->num_active = 1;
        return 0;
    }

    /* Normalize gains for constant power */
    double norm = sqrt(best_g1 * best_g1 + best_g2 * best_g2 + best_g3 * best_g3);
    if (norm > 1e-10) {
        best_g1 /= norm;
        best_g2 /= norm;
        best_g3 /= norm;
    }

    gains->gains[0]  = best_g1;
    gains->gains[1]  = best_g2;
    gains->gains[2]  = best_g3;
    gains->indices[0] = best_i;
    gains->indices[1] = best_j;
    gains->indices[2] = best_k;
    gains->num_active = 3;

    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L4: Distance Attenuation
 *
 * Free-field (inverse-square law):
 *   g(r) = r_ref / r
 *
 * Damped inverse-square (avoids singularity at r → 0):
 *   g(r) = 1 / (1 + r/r_ref)
 *
 * Reference distance attenuation:
 *   g(r) = (r_ref / r)^α  where α typ = 1.0
 *
 * For spatial audio, the reference distance is typically 1 meter.
 * ─────────────────────────────────────────────────────────────── */

double m3a_distance_attenuation(double distance_m, double reference_dist_m)
{
    if (distance_m < 0.001) distance_m = 0.001;
    return reference_dist_m / distance_m;
}

void m3a_apply_distance_model(m3a_dist_model model, double distance_m,
                              double ref_distance_m, double *gains, int num_gains)
{
    if (!gains || num_gains <= 0) return;

    double atten;
    switch (model) {
        case M3A_DIST_INV_SQUARE: {
            if (distance_m < 0.001) distance_m = 0.001;
            atten = ref_distance_m / distance_m;
            break;
        }
        case M3A_DIST_INV_SQUARE_DAMPED: {
            atten = 1.0 / (1.0 + distance_m / ref_distance_m);
            break;
        }
        case M3A_DIST_REF_DISTANCE: {
            if (distance_m < ref_distance_m) distance_m = ref_distance_m;
            atten = ref_distance_m / distance_m;
            break;
        }
        case M3A_DIST_CUSTOM:
        default:
            atten = 1.0;
            break;
    }

    for (int i = 0; i < num_gains; i++) {
        gains[i] *= atten;
    }
}

/* ───────────────────────────────────────────────────────────────
 * L5: DBAP — Distance-Based Amplitude Panning
 *
 * Unlike VBAP (which uses only 2-3 speakers), DBAP distributes the
 * signal to all speakers with weights inversely proportional to the
 * angular distance between the source and each speaker.
 *
 *   g_i ∝ 1 / (d_i + ε)^k
 *
 * where d_i is the great-circle distance from source to speaker i,
 * and k is a spatial blur parameter (higher k = sharper localization).
 *
 * DBAP is robust (always works, no triangulation needed) and produces
 * smooth panning, well-suited for irregular speaker layouts.
 *
 * Reference: Lossius, T. et al. "DBAP — Distance-Based Amplitude
 *            Panning" (2009), ICMC.
 * ─────────────────────────────────────────────────────────────── */

void m3a_dbap_pan(double az_deg, double el_deg,
                  const m3a_speaker_layout *layout,
                  double spatial_blur, double *gains)
{
    if (!layout || !gains) return;

    size_t L = layout->num_speakers;
    if (L == 0) return;

    double *distances = (double *)malloc(L * sizeof(double));
    if (!distances) return;

    double sum = 0.0;
    double k = spatial_blur;
    if (k < 1.0) k = 1.0;

    for (size_t i = 0; i < L; i++) {
        double d = m3a_great_circle_distance_deg(az_deg, el_deg,
                     layout->speakers[i].azimuth_deg,
                     layout->speakers[i].elevation_deg);
        distances[i] = pow(d + 1e-6, k);
        sum += 1.0 / distances[i];
    }

    for (size_t i = 0; i < L; i++) {
        gains[i] = (1.0 / distances[i]) / sum;
    }

    free(distances);
}

/* ───────────────────────────────────────────────────────────────
 * L5: Crossfade Panning for Moving Sources
 *
 * Smooth transition between two pan positions using cosine crossfade:
 *   gain_a = cos²(p · π/2),  gain_b = sin²(p · π/2)
 *
 * p ∈ [0, 1] is the fade position.
 * ─────────────────────────────────────────────────────────────── */

void m3a_crossfade_pan(double pan_position, double *gain_a, double *gain_b)
{
    double p = pan_position;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;

    double angle = p * M_PI / 2.0;
    *gain_a = cos(angle);
    *gain_b = sin(angle);
}

/* ───────────────────────────────────────────────────────────────
 * L6: Standard Speaker Layout Construction
 *
 * ITU-R BS.775-3 defines standard multichannel layouts:
 *   Stereo:    L(-30°), R(+30°)
 *   5.1:       L(-30°), R(+30°), C(0°), LFE, LS(-110°), RS(+110°)
 *   7.1:       5.1 + BL(-150°), BR(+150°)
 *   7.1.4:     7.1 with 4 overhead channels
 *
 * These layouts are the foundation for cinema, broadcast, and
 * home theater spatial audio.
 * ─────────────────────────────────────────────────────────────── */

static void alloc_layout_speakers(m3a_speaker_layout *layout, int count)
{
    layout->num_speakers = (size_t)count;
    layout->speakers = (m3a_speaker *)calloc((size_t)count, sizeof(m3a_speaker));
}

static void set_speaker(m3a_speaker_layout *layout, int idx,
                        double az, double el, int ch)
{
    if (!layout->speakers || idx < 0) return;
    layout->speakers[idx].azimuth_deg   = az;
    layout->speakers[idx].elevation_deg = el;
    layout->speakers[idx].distance_m    = 1.0;
    layout->speakers[idx].channel_index = ch;
}

int m3a_build_layout_stereo(m3a_speaker_layout *layout)
{
    if (!layout) return -1;
    alloc_layout_speakers(layout, 2);
    layout->is_3d = 0;
    set_speaker(layout, 0, -30.0, 0.0, 0);  /* L */
    set_speaker(layout, 1,  30.0, 0.0, 1);  /* R */
    return 0;
}

int m3a_build_layout_51(m3a_speaker_layout *layout)
{
    if (!layout) return -1;
    alloc_layout_speakers(layout, 6);
    layout->is_3d = 0;
    set_speaker(layout, 0,   0.0, 0.0, 0);  /* C */
    set_speaker(layout, 1,  -30.0, 0.0, 1);  /* L */
    set_speaker(layout, 2,  30.0, 0.0, 2);  /* R */
    set_speaker(layout, 3, -110.0, 0.0, 3);  /* LS */
    set_speaker(layout, 4,  110.0, 0.0, 4);  /* RS */
    set_speaker(layout, 5,   0.0, 0.0, 5);  /* LFE */
    return 0;
}

int m3a_build_layout_71(m3a_speaker_layout *layout)
{
    if (!layout) return -1;
    alloc_layout_speakers(layout, 8);
    layout->is_3d = 0;
    set_speaker(layout, 0,   0.0, 0.0, 0);  /* C */
    set_speaker(layout, 1,  -30.0, 0.0, 1);  /* L */
    set_speaker(layout, 2,  30.0, 0.0, 2);  /* R */
    set_speaker(layout, 3, -110.0, 0.0, 3);  /* LS */
    set_speaker(layout, 4,  110.0, 0.0, 4);  /* RS */
    set_speaker(layout, 5, -150.0, 0.0, 5);  /* BL */
    set_speaker(layout, 6,  150.0, 0.0, 6);  /* BR */
    set_speaker(layout, 7,   0.0, 0.0, 7);  /* LFE */
    return 0;
}

int m3a_build_layout_714(m3a_speaker_layout *layout)
{
    if (!layout) return -1;
    alloc_layout_speakers(layout, 12);
    layout->is_3d = 1;
    /* 7.1 base layer */
    set_speaker(layout, 0,   0.0, 0.0, 0);  /* C */
    set_speaker(layout, 1,  -30.0, 0.0, 1);  /* L */
    set_speaker(layout, 2,  30.0, 0.0, 2);  /* R */
    set_speaker(layout, 3, -110.0, 0.0, 3);  /* LS */
    set_speaker(layout, 4,  110.0, 0.0, 4);  /* RS */
    set_speaker(layout, 5, -150.0, 0.0, 5);  /* BL */
    set_speaker(layout, 6,  150.0, 0.0, 6);  /* BR */
    set_speaker(layout, 7,   0.0, 0.0, 7);  /* LFE */
    /* 4 overhead speakers (45° elevation) */
    set_speaker(layout, 8,  -45.0, 45.0, 8);  /* TFL */
    set_speaker(layout, 9,   45.0, 45.0, 9);  /* TFR */
    set_speaker(layout, 10, -135.0, 45.0, 10); /* TRL */
    set_speaker(layout, 11,  135.0, 45.0, 11); /* TRR */
    return 0;
}

/* ───────────────────────────────────────────────────────────────
 * L5: VBAP Mesh Construction (3D speaker triangulation)
 *
 * Builds the spherical Delaunay triangulation for 3D VBAP.
 * For simplicity, we use a brute-force approach: enumerate all
 * possible triangles and keep non-degenerate valid ones.
 *
 * Production implementation would use Qhull or similar for
 * true Delaunay triangulation.
 * ─────────────────────────────────────────────────────────────── */

int m3a_vbap_build_mesh(const m3a_speaker_layout *layout, m3a_vbap_mesh *mesh)
{
    if (!layout || !mesh) return -1;

    mesh->layout = *layout;
    mesh->num_triangles = 0;

    /* Count valid triangles */
    size_t L = layout->num_speakers;
    size_t max_triangles = L * (L - 1) * (L - 2) / 6;
    mesh->triangles = (m3a_spherical_triangle *)calloc(max_triangles,
                        sizeof(m3a_spherical_triangle));
    if (!mesh->triangles) return -1;

    size_t tri_count = 0;
    for (size_t i = 0; i < L && tri_count < max_triangles; i++) {
        for (size_t j = i + 1; j < L && tri_count < max_triangles; j++) {
            for (size_t k = j + 1; k < L && tri_count < max_triangles; k++) {
                m3a_spherical_triangle *tri = &mesh->triangles[tri_count];

                tri->idx[0] = (int)i;
                tri->idx[1] = (int)j;
                tri->idx[2] = (int)k;

                /* Compute speaker vectors */
                for (int s = 0; s < 3; s++) {
                    int idx = tri->idx[s];
                    double a = M3A_DEG2RAD(layout->speakers[idx].azimuth_deg);
                    double e = M3A_DEG2RAD(layout->speakers[idx].elevation_deg);
                    tri->vectors[s][0] = cos(e) * cos(a);
                    tri->vectors[s][1] = cos(e) * sin(a);
                    tri->vectors[s][2] = sin(e);
                }

                /* Compute determinant of [v0 v1 v2] */
                double det = tri->vectors[0][0] * (tri->vectors[1][1] * tri->vectors[2][2]
                                                 - tri->vectors[1][2] * tri->vectors[2][1])
                           - tri->vectors[0][1] * (tri->vectors[1][0] * tri->vectors[2][2]
                                                 - tri->vectors[1][2] * tri->vectors[2][0])
                           + tri->vectors[0][2] * (tri->vectors[1][0] * tri->vectors[2][1]
                                                 - tri->vectors[1][1] * tri->vectors[2][0]);

                if (fabs(det) < 1e-8) {
                    tri->valid = 0;
                    continue;
                }

                /* Compute inverse matrix for gain computation */
                double inv_det = 1.0 / det;

                tri->inv_matrix[0][0] =  (tri->vectors[1][1] * tri->vectors[2][2]
                                        - tri->vectors[1][2] * tri->vectors[2][1]) * inv_det;
                tri->inv_matrix[0][1] = -(tri->vectors[0][1] * tri->vectors[2][2]
                                        - tri->vectors[0][2] * tri->vectors[2][1]) * inv_det;
                tri->inv_matrix[0][2] =  (tri->vectors[0][1] * tri->vectors[1][2]
                                        - tri->vectors[0][2] * tri->vectors[1][1]) * inv_det;

                tri->inv_matrix[1][0] = -(tri->vectors[1][0] * tri->vectors[2][2]
                                        - tri->vectors[1][2] * tri->vectors[2][0]) * inv_det;
                tri->inv_matrix[1][1] =  (tri->vectors[0][0] * tri->vectors[2][2]
                                        - tri->vectors[0][2] * tri->vectors[2][0]) * inv_det;
                tri->inv_matrix[1][2] = -(tri->vectors[0][0] * tri->vectors[1][2]
                                        - tri->vectors[0][2] * tri->vectors[1][0]) * inv_det;

                tri->inv_matrix[2][0] =  (tri->vectors[1][0] * tri->vectors[2][1]
                                        - tri->vectors[1][1] * tri->vectors[2][0]) * inv_det;
                tri->inv_matrix[2][1] = -(tri->vectors[0][0] * tri->vectors[2][1]
                                        - tri->vectors[0][1] * tri->vectors[2][0]) * inv_det;
                tri->inv_matrix[2][2] =  (tri->vectors[0][0] * tri->vectors[1][1]
                                        - tri->vectors[0][1] * tri->vectors[1][0]) * inv_det;

                tri->valid = 1;
                tri_count++;
            }
        }
    }

    mesh->num_triangles = tri_count;
    return 0;
}

void m3a_vbap_free_mesh(m3a_vbap_mesh *mesh)
{
    if (mesh) {
        free(mesh->triangles);
        mesh->triangles = NULL;
        mesh->num_triangles = 0;
    }
}

/* ───────────────────────────────────────────────────────────────
 * L5: Find Triangle Containing Source Direction
 *
 * Searches the precomputed mesh for the triangle containing the source.
 * A source is inside a triangle if all three barycentric coordinates
 * (gains) are non-negative.
 * ─────────────────────────────────────────────────────────────── */

int m3a_vbap_find_triangle(const m3a_vbap_mesh *mesh,
                           double az_deg, double el_deg,
                           m3a_spherical_triangle *triangle)
{
    if (!mesh || !triangle) return -1;

    double az_s = M3A_DEG2RAD(az_deg);
    double el_s = M3A_DEG2RAD(el_deg);
    double sx = cos(el_s) * cos(az_s);
    double sy = cos(el_s) * sin(az_s);
    double sz = sin(el_s);

    for (size_t t = 0; t < mesh->num_triangles; t++) {
        if (!mesh->triangles[t].valid) continue;

        /* g = inv_matrix · s */
        double g0 = mesh->triangles[t].inv_matrix[0][0] * sx
                  + mesh->triangles[t].inv_matrix[0][1] * sy
                  + mesh->triangles[t].inv_matrix[0][2] * sz;
        double g1 = mesh->triangles[t].inv_matrix[1][0] * sx
                  + mesh->triangles[t].inv_matrix[1][1] * sy
                  + mesh->triangles[t].inv_matrix[1][2] * sz;
        double g2 = mesh->triangles[t].inv_matrix[2][0] * sx
                  + mesh->triangles[t].inv_matrix[2][1] * sy
                  + mesh->triangles[t].inv_matrix[2][2] * sz;

        if (g0 >= -1e-9 && g1 >= -1e-9 && g2 >= -1e-9) {
            *triangle = mesh->triangles[t];
            return 0;
        }
    }

    return -1;
}

void m3a_vbap_compute_gains(const m3a_spherical_triangle *triangle,
                            double az_deg, double el_deg,
                            m3a_vbap_gains *gains)
{
    if (!triangle || !gains) return;

    double az_s = M3A_DEG2RAD(az_deg);
    double el_s = M3A_DEG2RAD(el_deg);
    double sx = cos(el_s) * cos(az_s);
    double sy = cos(el_s) * sin(az_s);
    double sz = sin(el_s);

    double g0 = triangle->inv_matrix[0][0] * sx
              + triangle->inv_matrix[0][1] * sy
              + triangle->inv_matrix[0][2] * sz;
    double g1 = triangle->inv_matrix[1][0] * sx
              + triangle->inv_matrix[1][1] * sy
              + triangle->inv_matrix[1][2] * sz;
    double g2 = triangle->inv_matrix[2][0] * sx
              + triangle->inv_matrix[2][1] * sy
              + triangle->inv_matrix[2][2] * sz;

    /* Normalize */
    double norm = sqrt(g0 * g0 + g1 * g1 + g2 * g2);
    if (norm > 1e-10) { g0 /= norm; g1 /= norm; g2 /= norm; }

    gains->gains[0] = g0;
    gains->gains[1] = g1;
    gains->gains[2] = g2;
    gains->indices[0] = triangle->idx[0];
    gains->indices[1] = triangle->idx[1];
    gains->indices[2] = triangle->idx[2];
    gains->num_active = 3;
}

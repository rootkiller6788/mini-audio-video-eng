/*
 * spatial_panner.h — Amplitude Panning & VBAP
 *
 * Reference: Pulkki, V. "Virtual Sound Source Positioning Using Vector Base
 * Amplitude Panning" (1997), Journal of the Audio Engineering Society.
 *
 * Knowledge Coverage:
 *   L2:    Amplitude panning (sine law, tangent law, constant power)
 *   L2:    Vector Base Amplitude Panning (VBAP) — 2D and 3D
 *   L3:    Barycentric coordinates on spherical polygon
 *   L4:    Inverse square law for distance attenuation
 *   L5:    Pair-wise panning, triplet-based 3D panning
 *   L5:    Speaker triplet selection via spherical Delaunay triangulation
 *   L6:    2D/3D surround sound rendering (e.g., 5.1, 7.1.4)
 *   L7:    Cinema immersive audio (Atmos/DTS:X speaker layout)
 */

#ifndef SPATIAL_PANNER_H
#define SPATIAL_PANNER_H

#include "mini_3d_audio.h"

/* ──────────────────────────────────────────────────────────────
 * Panning Law Enumerations
 * ────────────────────────────────────────────────────────────── */

/** Panning law type */
typedef enum {
    M3A_PAN_SINE_LAW = 0,      /**< sin²θ + cos²θ = 1 (constant power) */
    M3A_PAN_TANGENT_LAW = 1,   /**< tan-based (stereo pair) */
    M3A_PAN_LINEAR = 2,        /**< linear crossfade (constant amplitude) */
    M3A_PAN_DBAP = 3,          /**< Distance-Based Amplitude Panning */
} m3a_pan_law;

/**
 * Distance attenuation model.
 * L4: Inverse-square law with near-field and far-field adjustments.
 */
typedef enum {
    M3A_DIST_INV_SQUARE = 0,        /**< g = 1/r (point source in free field) */
    M3A_DIST_INV_SQUARE_DAMPED = 1, /**< g = 1/(r + r_ref) (avoids singularity) */
    M3A_DIST_REF_DISTANCE = 2,      /**< g = r_ref / r (reference distance normalization) */
    M3A_DIST_CUSTOM = 3,            /**< user-defined attenuation curve */
} m3a_dist_model;

/* ──────────────────────────────────────────────────────────────
 * Spherical Triangle for 3D VBAP
 * ────────────────────────────────────────────────────────────── */

/**
 * Spherical triangle formed by three loudspeakers.
 * L3: The 3D VBAP problem reduces to expressing the source direction
 * as a convex combination of three speaker direction vectors.
 */
typedef struct {
    int    idx[3];           /**< indices into speaker layout */
    double vectors[3][3];    /**< unit vectors (x,y,z) for the 3 speakers */
    double inv_matrix[3][3]; /**< inverse of [v1 v2 v3] for gain computation */
    int    valid;            /**< 1 if triangle is non-degenerate */
} m3a_spherical_triangle;

/**
 * Triangulated speaker mesh for 3D VBAP.
 * L5: Delaunay triangulation on the sphere provides optimal
 * panning triplets (Pulkki, 2001).
 */
typedef struct {
    m3a_spherical_triangle *triangles;
    size_t                  num_triangles;
    m3a_speaker_layout      layout;
} m3a_vbap_mesh;

/* ──────────────────────────────────────────────────────────────
 * Function Declarations
 * ────────────────────────────────────────────────────────────── */

/** Build the Delaunay triangulation mesh for a 3D speaker layout */
int m3a_vbap_build_mesh(const m3a_speaker_layout *layout, m3a_vbap_mesh *mesh);

/** Free VBAP mesh memory */
void m3a_vbap_free_mesh(m3a_vbap_mesh *mesh);

/** Find the triangle containing a source direction (3D VBAP) */
int m3a_vbap_find_triangle(const m3a_vbap_mesh *mesh,
                           double az_deg, double el_deg,
                           m3a_spherical_triangle *triangle);

/** Compute VBAP gains for a source within a known triangle */
void m3a_vbap_compute_gains(const m3a_spherical_triangle *triangle,
                            double az_deg, double el_deg,
                            m3a_vbap_gains *gains);

/** DBAP (Distance-Based Amplitude Panning) — all-speaker panning */
void m3a_dbap_pan(double az_deg, double el_deg,
                  const m3a_speaker_layout *layout,
                  double spatial_blur, double *gains);

/** Build standard speaker layouts (stereo, 5.1, 7.1, 7.1.4) */
int m3a_build_layout_stereo(m3a_speaker_layout *layout);
int m3a_build_layout_51(m3a_speaker_layout *layout);
int m3a_build_layout_71(m3a_speaker_layout *layout);
int m3a_build_layout_714(m3a_speaker_layout *layout);

/** Apply distance-based attenuation to panning gains */
void m3a_apply_distance_model(m3a_dist_model model, double distance_m,
                              double ref_distance_m, double *gains, int num_gains);

/** Compute constant-power pan gains for a moving source (crossfade) */
void m3a_crossfade_pan(double pan_position, double *gain_a, double *gain_b);

#endif /* SPATIAL_PANNER_H */

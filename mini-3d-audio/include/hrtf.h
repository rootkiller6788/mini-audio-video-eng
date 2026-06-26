/*
 * hrtf.h — Head-Related Transfer Function Data Structures & Interpolation
 *
 * Reference: Xie, B. "Head-Related Transfer Function and Virtual Auditory
 * Display" (2013), J. Ross Publishing.
 *
 * Knowledge Coverage:
 *   L2: HRTF measurement grids, interpolation in spatial domain
 *   L2: Minimum-phase reconstruction
 *   L3: Spherical interpolation, Voronoi tesselation on sphere
 *   L5: Bilinear/nearest-neighbor HRTF interpolation algorithms
 *   L6: ITD extraction via cross-correlation
 */

#ifndef HRTF_H
#define HRTF_H

#include "mini_3d_audio.h"

/* ──────────────────────────────────────────────────────────────
 * Interpolation methods for HRTF lookup
 * ────────────────────────────────────────────────────────────── */

/** HRTF interpolation technique */
typedef enum {
    M3A_HRTF_INTERP_NEAREST = 0,      /**< nearest-neighbor (fastest, lowest quality) */
    M3A_HRTF_INTERP_BILINEAR = 1,     /**< bilinear on (az,el) grid */
    M3A_HRTF_INTERP_BARYCENTRIC = 2,  /**< barycentric on spherical triangle */
    M3A_HRTF_INTERP_TRIANGLE = 3,     /**< Delaunay triangulation based */
} m3a_hrtf_interp_method;

/**
 * HRTF grid topology descriptor.
 * L2: Most HRTF databases use equal-angle or equal-area grids.
 */
typedef enum {
    M3A_GRID_EQUAL_ANGLE = 0,   /**< CIPIC / LISTEN: uniform az/el spacing */
    M3A_GRID_GAUSS = 1,         /**< Gauss-Legendre quadrature nodes */
    M3A_GRID_LEBEDEV = 2,       /**< Lebedev quadrature (equal area) */
    M3A_GRID_IRREGULAR = 3,     /**< arbitrary measurement points */
} m3a_hrtf_grid_type;

/**
 * HRTF interpolation context — holds precomputed neighborhood data.
 * L5 Algorithm: Accelerated spatial HRTF lookup using kd-tree or grid.
 */
typedef struct {
    const m3a_hrtf_db *db;
    m3a_hrtf_interp_method method;
    m3a_hrtf_grid_type    grid_type;
    /* Precomputed grid spacing for fast neighbor lookup */
    double az_step_deg;
    double el_step_deg;
    int    az_count;
    int    el_count;
} m3a_hrtf_interp;

/* ──────────────────────────────────────────────────────────────
 * HRTF Post-Processing
 * ────────────────────────────────────────────────────────────── */

/**
 * HRTF smoothing parameters.
 * L5: Frequency-dependent smoothing reduces measurement noise
 * while preserving perceptually important spectral features.
 */
typedef struct {
    double smoothing_octave;  /**< smoothing bandwidth in octaves (e.g. 1/3) */
    int    enable_itd_smooth; /**< 1 to smooth ITD across directions */
} m3a_hrtf_smoothing;

/* ──────────────────────────────────────────────────────────────
 * Function Declarations
 * ────────────────────────────────────────────────────────────── */

/** Initialize interpolation context from database grid */
int m3a_hrtf_interp_init(m3a_hrtf_interp *ctx, const m3a_hrtf_db *db,
                         m3a_hrtf_interp_method method);

/** Find nearest HRTF entry by angular distance */
int m3a_hrtf_find_nearest(const m3a_hrtf_db *db, double az_deg, double el_deg,
                          size_t *index);

/** HRTF interpolation using barycentric weights on spherical triangle */
int m3a_hrtf_interpolate_barycentric(const m3a_hrtf_interp *ctx,
    double az_deg, double el_deg, m3a_hrir *hrir_left, m3a_hrir *hrir_right);

/** Smooth HRTF magnitude spectrum in 1/N-octave bands */
int m3a_hrtf_smooth_magnitude(m3a_hrtf *hrtf, double octave_fraction);

/** Compute diffuse-field equalization filter from HRTF database */
int m3a_hrtf_diffuse_field_eq(const m3a_hrtf_db *db, double *eq_mag, size_t num_bins);

/** Convert HRIR to HRTF via FFT */
int m3a_hrtf_from_hrir(const m3a_hrir *hrir_l, const m3a_hrir *hrir_r,
                       m3a_hrtf *hrtf);

/** Check if two HRTF entries belong to same spatial cluster */
int m3a_hrtf_same_cluster(const m3a_hrtf_entry *a, const m3a_hrtf_entry *b,
                          double angle_threshold_deg);

#endif /* HRTF_H */

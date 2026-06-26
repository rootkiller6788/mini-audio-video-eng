/*
 * ambisonics.h — Ambisonics Encoding, Decoding, and Manipulation
 *
 * Reference: Zotter, F. & Frank, M. "Ambisonics: A Practical 3D Audio Theory
 * for Recording, Studio Production, Sound Reinforcement, and Virtual Reality"
 * (2019), Springer Topics in Signal Processing.
 *
 * Knowledge Coverage:
 *   L1:    Ambisonics channel definitions (W,X,Y,Z,R,S,T,U,V,...)
 *   L2:    First-order (B-format) encoding — sound field decomposition
 *   L2:    Higher-order Ambisonics (HOA) encoding
 *   L3:    Spherical harmonic basis functions (real-valued), ACN ordering
 *   L3:    Normalization: N3D, SN3D, FuMa — and conversion between them
 *   L4:    Helmholtz equation / wave equation — sound field on sphere
 *   L5:    Mode-matching decoding, AllRAD decoding
 *   L5:    Virtual microphone extraction from B-format
 *   L6:    B-format rotation (listener head rotation compensation)
 *   L6:    Ambisonic panning — direction-dependent gain per HOA channel
 *   L8:    Higher-order (3rd+) encoding with near-field compensation
 *
 * University Alignment:
 *   - ETH 227-0455 Electromagnetic Waves — spherical wave expansion
 *     (same mathematical framework as spherical harmonics for acoustics)
 *   - TU Munich High-Frequency Engineering — multipole expansion
 *   - Michigan EECS411 Microwave — spherical mode theory
 */

#ifndef AMBISONICS_H
#define AMBISONICS_H

#include "mini_3d_audio.h"

/* ──────────────────────────────────────────────────────────────
 * Maximum supported Ambisonics order
 * ────────────────────────────────────────────────────────────── */
#define M3A_AMB_MAX_ORDER  3
#define M3A_AMB_MAX_CHANS  ((M3A_AMB_MAX_ORDER + 1) * (M3A_AMB_MAX_ORDER + 1))  /* 16 */

/* ──────────────────────────────────────────────────────────────
 * Ambisonics Transform Matrix
 * ────────────────────────────────────────────────────────────── */

/**
 * Ambisonics encoding/decoding transform.
 * L3: Matrix representation of spherical harmonic projection.
 */
typedef struct {
    int    order;              /**< Ambisonics order */
    int    num_channels;       /**< = (order+1)^2 */
    int    num_directions;     /**< number of virtual speaker directions */
    double *matrix;            /**< row-major: [num_directions × num_channels] */
    m3a_amb_norm    norm;      /**< normalization convention */
    m3a_amb_chan_order ch_order; /**< channel ordering */
} m3a_amb_matrix;

/**
 * Ambisonics decoder configuration.
 * L5: Decoding from B-format/HOA to loudspeaker feeds.
 */
typedef struct {
    m3a_amb_order    order;
    m3a_amb_norm      norm;
    m3a_amb_chan_order ch_order;
    m3a_speaker_layout layout;
    int   use_allrad;           /**< 1 = AllRAD, 0 = mode-matching */
    int   maxre_weight;         /**< 1 to apply Max-rE weighting */
    double decoder_matrix[M3A_AMB_MAX_CHANS * 64]; /**< precomputed decoder */
} m3a_amb_decoder;

/**
 * Binaural Ambisonics decoder — combines HOA decoding with HRTF.
 * L7 Application: VR/AR spatial audio rendering using Ambisonics + HRTF.
 * L8 Advanced: Binaural rendering of HOA scenes.
 */
typedef struct {
    m3a_amb_order    order;
    m3a_hrtf_db      *hrtf_db;
    int              num_virtual_speakers;
    m3a_speaker_layout virtual_layout;   /**< virtual speaker array for HRTF convolution */
    double           *binaural_decoder_l; /**< [num_channels × HRIR_len × num_virtual] */
    double           *binaural_decoder_r;
    size_t           hrir_len;
} m3a_amb_binaural_decoder;

/* ──────────────────────────────────────────────────────────────
 * Function Declarations
 * ────────────────────────────────────────────────────────────── */

/** Evaluate real spherical harmonic Y_n^m(θ, φ) with specified normalization */
double m3a_sh_eval(int n, int m, double azimuth_deg, double elevation_deg,
                   m3a_amb_norm norm);

/** ACN channel index from degree n and order m */
int m3a_acn_index(int n, int m);

/** Encode a point source to HOA channels (all orders up to max_order) */
void m3a_amb_encode_hoa(double az_deg, double el_deg, int max_order,
                        m3a_amb_norm norm, m3a_amb_chan_order ch_order,
                        double *channels);

/** Build mode-matching decoder matrix for given speaker layout */
int m3a_amb_build_decoder_matrix(m3a_amb_decoder *dec);

/** Decode B-format/HOA to speaker feeds */
void m3a_amb_decode(const m3a_amb_decoder *dec, const double *hoa_channels,
                    double *speaker_feeds);

/** Apply Max-rE weighting to HOA channels (improves localization for non-centric listeners) */
void m3a_amb_maxre_weights(int order, double *weights);

/** Apply near-field compensation (NFC) filter for distance rendering */
int m3a_amb_nfc_filter(double distance_m, int order, int sample_rate,
                       double *filter_coeffs, size_t *filter_len);

/** Convert between normalization conventions */
void m3a_amb_convert_norm(const double *input, int order,
                          m3a_amb_norm from_norm, m3a_amb_norm to_norm,
                          double *output);

/** Virtual microphone: extract signal from direction in B-format */
double m3a_amb_virtual_mic(const double *bformat, int order,
                           double az_deg, double el_deg);

/** Render HOA scene to binaural using HRTF database */
int m3a_amb_binaural_render(const m3a_amb_binaural_decoder *dec,
                            const double *hoa_channels,
                            double *out_left, double *out_right,
                            size_t num_samples);

/** Initialize binaural Ambisonics decoder */
int m3a_amb_binaural_decoder_init(m3a_amb_binaural_decoder *dec,
                                   m3a_amb_order order, m3a_hrtf_db *hrtf_db,
                                   int num_virtual_speakers);

#endif /* AMBISONICS_H */

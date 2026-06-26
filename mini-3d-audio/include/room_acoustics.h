/*
 * room_acoustics.h — Room Acoustics Simulation
 *
 * Reference: Kuttruff, H. "Room Acoustics" (2009), 5th Ed., CRC Press.
 * Reference: Allen, J.B. & Berkley, D.A. "Image method for efficiently
 * simulating small-room acoustics" (1979), JASA.
 *
 * Knowledge Coverage:
 *   L1:    RT60 (reverberation time), Sabine/Eyring formulas
 *   L2:    Geometric room acoustics, early reflections, late reverberation
 *   L4:    Sabine's reverberation formula, Eyring's revised formula
 *   L5:    Image source method for early reflections
 *   L5:    Feedback Delay Network (FDN) for late reverberation
 *   L5:    Schroeder all-pass reverberator
 *   L6:    Room impulse response generation
 *   L6:    Binaural room synthesis
 *   L7:    Auralization for architectural acoustics
 *
 * University Alignment:
 *   - MIT 6.630 EM Waves — wave reflection, boundary conditions
 *   - Berkeley EE117 Electromagnetic Waves — reflection/transmission
 *   - TU Munich High-Frequency Engineering — ray tracing
 */

#ifndef ROOM_ACOUSTICS_H
#define ROOM_ACOUSTICS_H

#include "mini_3d_audio.h"

/* ──────────────────────────────────────────────────────────────
 * Reflection / Absorption
 * ────────────────────────────────────────────────────────────── */

/**
 * Surface material acoustic properties.
 * L1 Definition: Absorption coefficient α (0=perfect reflect, 1=perfect absorb)
 * varies with frequency.
 */
typedef struct {
    double alpha_125Hz;
    double alpha_250Hz;
    double alpha_500Hz;
    double alpha_1000Hz;
    double alpha_2000Hz;
    double alpha_4000Hz;
    char   name[32];
} m3a_material;

/** Predefined materials database lookup */
typedef enum {
    M3A_MAT_CONCRETE = 0,
    M3A_MAT_GLASS = 1,
    M3A_MAT_WOOD = 2,
    M3A_MAT_CARPET = 3,
    M3A_MAT_CURTAIN = 4,
    M3A_MAT_PLASTER = 5,
    M3A_MAT_ACOUSTIC_TILE = 6,
    M3A_MAT_FOAM = 7,
    M3A_MAT_COUNT = 8,
} m3a_material_id;

/* ──────────────────────────────────────────────────────────────
 * Ray-Tracing / Image Source Configuration
 * ────────────────────────────────────────────────────────────── */

/**
 * Image source method configuration.
 * L5: Controls the geometric acoustics simulation.
 */
typedef struct {
    int    max_order;          /**< maximum reflection order (1-5 typical) */
    int    enable_air_abs;     /**< 1 to include air absorption */
    int    enable_diffraction; /**< 1 to include edge diffraction (simple) */
    double speed_of_sound;     /**< m/s */
    double max_distance_m;     /**< culling distance for image sources */
} m3a_ism_config;

/**
 * Feedback Delay Network (FDN) configuration for late reverb.
 * L5: FDN reverb by Jot & Chaigne (1991) — produces dense, natural reverb.
 */
typedef struct {
    int    num_delay_lines;    /**< typically 8-16 */
    double rt60_sec;           /**< target reverberation time */
    int    sample_rate;
    double *delay_lengths;     /**< delay line lengths in samples */
    double *feedback_matrix;   /**< unitary feedback mixing matrix */
    double *gains_per_band;    /**< frequency-dependent loop gains */
} m3a_fdn_config;

/* ──────────────────────────────────────────────────────────────
 * Room Impulse Response Generator
 * ────────────────────────────────────────────────────────────── */

/**
 * Room impulse response (RIR) — complete acoustic response.
 * L6: Room impulse response captures the complete acoustic path
 * from source to receiver, including direct sound, early reflections,
 * and late reverberation.
 */
typedef struct {
    double *direct;              /**< direct path signal */
    size_t  direct_len;
    double *early_reflections;   /**< early reflections (image sources) */
    size_t  early_len;
    double *late_reverb_l;       /**< late diffuse reverb, left */
    double *late_reverb_r;       /**< late diffuse reverb, right */
    size_t  late_len;
    int     sample_rate;
} m3a_rir;

/**
 * Schroeder all-pass reverberator unit.
 * L5: Classic Schroeder (1962) design — cascaded comb filters and all-pass
 * filters produce artificial reverberation.
 */
typedef struct {
    double *delay_line;
    size_t  delay_len;
    size_t  write_ptr;
    double  gain;
} m3a_comb_filter;

typedef struct {
    m3a_comb_filter *combs;     /**< parallel comb filters */
    int               num_combs;
    double           *allpass_mem;  /**< series all-pass filter memories */
    size_t            allpass_len;
    size_t           *allpass_delays;
    double           *allpass_gains;
    int               num_allpasses;
} m3a_schroeder_reverb;

/* ──────────────────────────────────────────────────────────────
 * Function Declarations
 * ────────────────────────────────────────────────────────────── */

/** Look up acoustic material properties by ID */
const m3a_material *m3a_material_get(m3a_material_id id);

/** Sabine's formula: RT60 = 0.161 * V / (S * α_mean) */
double m3a_rt60_sabine(double volume_m3, double surface_area_m2, double mean_absorption);

/** Eyring's formula: RT60 = 0.161 * V / (-S * ln(1-α_mean)) */
double m3a_rt60_eyring(double volume_m3, double surface_area_m2, double mean_absorption);

/** Compute mean absorption coefficient from room surfaces */
double m3a_mean_absorption(const m3a_room *room);

/** Critical distance (where direct = reverberant): D_c = 0.057 * sqrt(V/T60) */
double m3a_critical_distance(const m3a_room_params *params);

/** Generate all image sources up to max_order */
int m3a_ism_generate(const m3a_room *room, const m3a_ism_config *config,
                     const m3a_vec3d *source, const m3a_vec3d *listener,
                     m3a_audio_buffer *rir);

/** Initialize Feedback Delay Network */
int m3a_fdn_init(m3a_fdn_config *config, int sample_rate, double rt60_sec);

/** Process audio through FDN */
void m3a_fdn_process(double *input, size_t num_samples, m3a_fdn_config *config,
                     double *output_l, double *output_r);

/** Free FDN resources */
void m3a_fdn_free(m3a_fdn_config *config);

/** Generate a Hadamard mixing matrix for FDN */
int m3a_fdn_hadamard_matrix(int size, double *matrix);

/** Compute frequency-dependent reflection coefficient */
double m3a_reflection_coeff(double incident_angle_deg, const m3a_material *mat,
                            double freq_hz);

/** Initialize Schroeder reverberator */
int m3a_schroeder_init(m3a_schroeder_reverb *rev, int sample_rate,
                       double rt60_sec, int num_combs);

/** Process audio through Schroeder reverberator */
void m3a_schroeder_process(m3a_schroeder_reverb *rev,
                           const double *input, size_t num_samples,
                           double *output_l, double *output_r);

/** Free Schroeder reverberator */
void m3a_schroeder_free(m3a_schroeder_reverb *rev);

/** Binauralize room impulse response using HRTF per reflection direction */
int m3a_rir_to_binaural(const m3a_audio_buffer *rir_mono,
                        const m3a_hrtf_db *hrtf_db,
                        const m3a_vec3d *listener_pos,
                        const m3a_vec3d *source_pos,
                        m3a_audio_buffer *rir_binaural_l,
                        m3a_audio_buffer *rir_binaural_r);

#endif /* ROOM_ACOUSTICS_H */

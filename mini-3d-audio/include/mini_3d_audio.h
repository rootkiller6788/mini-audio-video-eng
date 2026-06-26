/*
 * mini_3d_audio.h — 3D / Spatial Audio Core Definitions
 *
 * Covers the fundamental data structures, coordinate systems, and type definitions
 * for spatial audio processing: HRTF-based binaural rendering, Ambisonics
 * encoding/decoding, VBAP panning, and room acoustics simulation.
 *
 * Knowledge Coverage:
 *   L1 Definitions:  HRTF, HRIR, ILD, ITD, IPD, SPL, Ambisonics channels,
 *                    spherical harmonics, binaural cues, RT60
 *   L2 Concepts:     binaural hearing, sound localization, HRTF measurement,
 *                    Ambisonics encode/decode, VBAP, distance rendering
 *   L3 Math:         spherical coordinates, spherical harmonics, convolution,
 *                    FIR filters, matrix operations
 *   L4 Laws:         Duplex theory, inverse square law, speed of sound,
 *                    Nyquist-Shannon theorem
 *
 * Reference Textbooks:
 *   - Blauert, J. "Spatial Hearing" (1997), MIT Press
 *   - Xie, B. "Head-Related Transfer Function and Virtual Auditory Display" (2013)
 *   - Zotter, F. & Frank, M. "Ambisonics" (2019), Springer
 *   - Poynton, C. "Digital Video and HD" (2012), Morgan Kaufmann
 *
 * University Course Alignment:
 *   - MIT 6.003 Signal Processing — convolution, Fourier analysis
 *   - Stanford EE359 Wireless — spatial channel modeling
 *   - Berkeley EE123 Digital Signal Processing — FIR filtering, FFT
 *   - Illinois ECE310 DSP — sampling, reconstruction
 *   - Michigan EECS351 DSP — filter design
 *   - Georgia Tech ECE4270 DSP — multi-rate processing
 *   - TU Munich Signal Processing — acoustic signal processing
 *   - ETH 227-0427 Signal Processing — adaptive filtering
 *   - Tsinghua Signal & Systems — transform-domain processing
 */

#ifndef MINI_3D_AUDIO_H
#define MINI_3D_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────────────────────────────────────────
 * L1: Core Definitions — Physical Constants & Types
 * ────────────────────────────────────────────────────────────── */

/** Speed of sound in air at 20°C (m/s) */
#define M3A_SPEED_OF_SOUND      343.0

/** Speed of sound at 0°C (m/s) — reference temperature */
#define M3A_SPEED_OF_SOUND_0C   331.3

/** Reference sound pressure in Pascals (hearing threshold at 1 kHz) */
#define M3A_REF_PRESSURE        2e-5

/** Maximum ITD for an average human head (~660 μs for 90° azimuth) */
#define M3A_MAX_ITD_SEC         6.6e-4

/** Typical human head radius in meters (for spherical head model) */
#define M3A_HEAD_RADIUS         0.0875

/** Default audio sample rate (Hz) */
#define M3A_DEFAULT_SAMPLE_RATE 48000

/** π (as defined in math.h but explicit for clarity) */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** Convert degrees to radians */
#define M3A_DEG2RAD(d)  ((d) * M_PI / 180.0)
/** Convert radians to degrees */
#define M3A_RAD2DEG(r)  ((r) * 180.0 / M_PI)

/* ──────────────────────────────────────────────────────────────
 * L1 Definitions — Coordinate Systems
 * ────────────────────────────────────────────────────────────── */

/**
 * Cartesian 3D coordinate (right-handed: x=right, y=front, z=up)
 * L1 Definition: Cartesian coordinate system for spatial positioning.
 */
typedef struct {
    double x, y, z;
} m3a_vec3d;

/**
 * Spherical coordinate (ISO convention: azimuth 0° = front, 90° = left,
 * elevation 0° = horizontal, +90° = up, distance in meters).
 * L1 Definition: Spherical coordinates for sound source localization.
 */
typedef struct {
    double azimuth_deg;   /**< azimuth in degrees, 0=front, +90=left */
    double elevation_deg; /**< elevation in degrees, +90=zenith */
    double distance_m;    /**< radial distance in meters */
} m3a_spherical;

/**
 * Audio buffer descriptor.
 * L1 Definition: PCM audio buffer with sample rate and channel count.
 */
typedef struct {
    double *data;         /**< sample data (interleaved for multi-channel) */
    size_t  num_samples;  /**< number of samples per channel */
    int     num_channels; /**< channel count */
    int     sample_rate;  /**< sample rate in Hz */
} m3a_audio_buffer;

/* ──────────────────────────────────────────────────────────────
 * L1 Definitions — HRTF / HRIR Data Types
 * ────────────────────────────────────────────────────────────── */

/**
 * Head-Related Impulse Response (HRIR) for one ear at one direction.
 * L1 Definition: HRIR is the time-domain impulse response from a
 * sound source to the ear drum, capturing all spatial cues.
 *
 * Length typically 128–512 samples at 44.1/48 kHz.
 */
typedef struct {
    double *impulse;      /**< HRIR sample values */
    size_t  length;       /**< number of samples */
    int     sample_rate;  /**< sample rate of HRIR measurement */
} m3a_hrir;

/**
 * Head-Related Transfer Function (HRTF) — frequency-domain representation.
 * L1 Definition: HRTF is the Fourier transform of HRIR, containing
 * magnitude and phase spectra for each direction.
 */
typedef struct {
    double *mag_left;     /**< magnitude spectrum, left ear (dB) */
    double *phase_left;   /**< phase spectrum, left ear (radians) */
    double *mag_right;    /**< magnitude spectrum, right ear (dB) */
    double *phase_right;  /**< phase spectrum, right ear (radians) */
    size_t  num_bins;     /**< number of frequency bins */
    int     sample_rate;  /**< sample rate */
} m3a_hrtf;

/**
 * HRTF measurement direction — one spatial location in an HRTF database.
 * L1 Definition: HRTF database entry associating spatial direction
 * with measured impulse responses.
 */
typedef struct {
    double  azimuth_deg;
    double  elevation_deg;
    m3a_hrir hrir_left;
    m3a_hrir hrir_right;
    double  itd_sec;      /**< Interaural Time Difference */
    double  ild_db;       /**< Interaural Level Difference */
} m3a_hrtf_entry;

/**
 * HRTF database — collection of measurements covering the sphere.
 * L1 Definition: Complete HRTF dataset for binaural spatialization.
 */
typedef struct {
    m3a_hrtf_entry *entries;
    size_t          num_entries;
    int             sample_rate;
    double          radius_m;  /**< measurement radius */
    char            subject_id[64];
} m3a_hrtf_db;

/* ──────────────────────────────────────────────────────────────
 * L1 Definitions — Interaural Cues (Binaural Parameters)
 * ────────────────────────────────────────────────────────────── */

/**
 * Binaural cue parameters extracted from HRTF.
 *   - ITD: Interaural Time Difference (seconds). Primary cue below ~1.5 kHz.
 *          L1 Definition: Time difference between sound arrival at two ears.
 *   - ILD: Interaural Level Difference (dB). Primary cue above ~1.5 kHz.
 *          L1 Definition: Level difference caused by head shadowing.
 *   - IPD: Interaural Phase Difference (radians). Redundant with ITD at LF.
 *          L1 Definition: Phase difference between ear signals.
 *   - IACC: Interaural Cross-Correlation. Binaural coherence measure.
 *          L2 Concept: Measure of binaural similarity / spatial impression.
 */
typedef struct {
    double itd_sec;
    double ild_db;
    double ipd_rad;
    double iacc;
} m3a_binaural_cues;

/* ──────────────────────────────────────────────────────────────
 * L1 Definitions — Ambisonics
 * ────────────────────────────────────────────────────────────── */

/** Ambisonics order (0 = zeroth-order W only, 1 = first-order WXYZ, etc.) */
typedef enum {
    M3A_AMB_ORDER_0 = 0,  /**< Mono / omnidirectional */
    M3A_AMB_ORDER_1 = 1,  /**< First-order: 4 channels (W,X,Y,Z) */
    M3A_AMB_ORDER_2 = 2,  /**< Second-order: 9 channels */
    M3A_AMB_ORDER_3 = 3,  /**< Third-order: 16 channels */
} m3a_amb_order;

/** Ambisonics normalization scheme.
 *  L1 Definition: Three standard conventions for scaling spherical harmonics.
 *    N3D (orthonormal):  ∫ Y_nm² dΩ = 1,  used in scientific computing
 *    SN3D (semi-normalized): W component = 1 (omni unit gain), used in production
 *    FuMa (Furse-Malham): legacy BBC convention, maxN normalization
 */
typedef enum {
    M3A_NORM_N3D = 0,
    M3A_NORM_SN3D = 1,
    M3A_NORM_FUMA = 2,
} m3a_amb_norm;

/**
 * Ambisonics channel ordering convention.
 *   ACN: Ambisonics Channel Number (ACN = n*(n+1)+m), modern standard
 *   FuMa: legacy WXYZ ordering for first order
 */
typedef enum {
    M3A_CHAN_ORDER_ACN = 0,
    M3A_CHAN_ORDER_FUMA = 1,
} m3a_amb_chan_order;

/** Number of Ambisonics channels for a given order. Theorem: N_channels = (order+1)² */
static inline int m3a_amb_num_channels(m3a_amb_order order) {
    int n = (int)order + 1;
    return n * n;
}

/* ──────────────────────────────────────────────────────────────
 * L1 Definitions — VBAP (Vector Base Amplitude Panning)
 * ────────────────────────────────────────────────────────────── */

/**
 * Speaker definition for VBAP.
 * L1 Definition: A loudspeaker at a known spatial position.
 */
typedef struct {
    double azimuth_deg;
    double elevation_deg;
    double distance_m;     /**< for distance compensation */
    int    channel_index;  /**< output channel index */
} m3a_speaker;

/**
 * VBAP loudspeaker layout.
 * L1 Definition: Set of speakers forming the listening array.
 */
typedef struct {
    m3a_speaker *speakers;
    size_t       num_speakers;
    int          is_3d;    /**< 1 if 3D layout, 0 if 2D ring */
} m3a_speaker_layout;

/**
 * VBAP gain vector for active speakers (2 or 3 gains depending on 2D/3D).
 * L2 Concept: Pair-wise/triplet amplitude panning gains.
 */
typedef struct {
    double gains[3];       /**< gain for each active speaker */
    int    indices[3];     /**< speaker indices in the layout */
    int    num_active;     /**< 2 for 2D, 3 for 3D */
} m3a_vbap_gains;

/* ──────────────────────────────────────────────────────────────
 * L1 Definitions — Room Acoustics
 * ────────────────────────────────────────────────────────────── */

/**
 * Room dimensions and acoustic properties.
 * L1 Definition: Room geometry and surface materials.
 */
typedef struct {
    double width_m;        /**< x dimension */
    double depth_m;        /**< y dimension */
    double height_m;       /**< z dimension */
    /** Surface absorption coefficients (6 surfaces: ±x, ±y, ±z) */
    double absorption[6];  /**< dimensionless, 0=perfect reflection, 1=perfect absorption */
    double air_temp_c;     /**< air temperature in Celsius */
    double air_humidity_pct; /**< relative humidity percentage */
} m3a_room;

/**
 * Room impulse response parameters.
 * L1 Definition: RT60 (reverberation time for 60 dB decay),
 * derived from Sabine/Eyring formula.
 */
typedef struct {
    double rt60_sec;           /**< reverberation time (T60) */
    double volume_m3;          /**< room volume */
    double surface_area_m2;    /**< total surface area */
    double mean_absorption;    /**< average absorption coefficient */
    double critical_distance_m;/**< distance where direct = reverberant */
} m3a_room_params;

/**
 * Image source descriptor — one virtual source for early reflection.
 * L2 Concept: Mirror source method for geometric room acoustics.
 */
typedef struct {
    m3a_vec3d     position;     /**< virtual source position */
    double        attenuation;  /**< cumulative reflection loss */
    double        delay_sec;    /**< propagation delay for this path */
    int           order;        /**< reflection order (1st, 2nd, ...) */
} m3a_image_source;

/* ──────────────────────────────────────────────────────────────
 * L1 Definitions — Spatial Renderer Configuration
 * ────────────────────────────────────────────────────────────── */

/**
 * Listener state (position and orientation in 3D space).
 * L1 Definition: Listener position determines relative source coordinates.
 */
typedef struct {
    m3a_vec3d position;       /**< listener position (meters) */
    double    yaw_deg;        /**< head yaw (rotation around Z axis) */
    double    pitch_deg;      /**< head pitch (rotation around X axis) */
    double    roll_deg;       /**< head roll (rotation around Y axis) */
} m3a_listener;

/**
 * Sound source definition.
 * L1 Definition: A sound source with position, velocity, and signal.
 */
typedef struct {
    m3a_vec3d     position;    /**< source position in world coordinates */
    m3a_vec3d     velocity;    /**< source velocity for Doppler (m/s) */
    m3a_audio_buffer signal;  /**< source audio signal (mono) */
    double        gain;        /**< source gain (linear multiplier) */
    int           active;      /**< 1 if source is active */
} m3a_source;

/**
 * Spatial audio scene — collection of sources and one listener.
 * L2 Concept: Complete 3D audio scene for rendering.
 */
typedef struct {
    m3a_listener listener;
    m3a_source  *sources;
    size_t       num_sources;
    size_t       max_sources;
    int          sample_rate;
    int          block_size;  /**< processing block size */
    /* Rendering parameters */
    int          enable_doppler;
    int          enable_reverb;
    int          enable_hrtf;
    double       master_gain;
    m3a_room     room;        /**< acoustic environment */
} m3a_scene;

/* ──────────────────────────────────────────────────────────────
 * L3 Mathematical Structures — Vector & Matrix operations
 * ────────────────────────────────────────────────────────────── */

/** 3x3 rotation matrix (for coordinate transforms) */
typedef struct {
    double m[3][3];
} m3a_mat3;

/** 4x4 matrix (for Ambisonics/HOA operations) */
typedef struct {
    double m[4][4];
} m3a_mat4;

/* ──────────────────────────────────────────────────────────────
 * L3 — Spherical Harmonic evaluation indices
 * ────────────────────────────────────────────────────────────── */

/**
 * Real-valued spherical harmonic basis function Y_nm(θ, φ).
 * L3 Definition: Spherical harmonics form a complete orthonormal basis
 * on the sphere, used for Ambisonics encoding.
 *
 * @param n  degree (order), n >= 0
 * @param m  index, -n <= m <= n
 * @param azimuth_deg   azimuth angle in degrees
 * @param elevation_deg elevation angle in degrees
 * @return   Y_nm(θ, φ) real-valued
 */
double m3a_spherical_harmonic(int n, int m, double azimuth_deg, double elevation_deg);

/* ──────────────────────────────────────────────────────────────
 * L5 — Audio Buffer Management
 * ────────────────────────────────────────────────────────────── */

int    m3a_audio_buffer_alloc(m3a_audio_buffer *buf, size_t num_samples,
                              int num_channels, int sample_rate);
void   m3a_audio_buffer_free(m3a_audio_buffer *buf);
void   m3a_audio_buffer_clear(m3a_audio_buffer *buf);
void   m3a_audio_buffer_copy(const m3a_audio_buffer *src, m3a_audio_buffer *dst);
double m3a_audio_peak(const double *buffer, size_t n);
double m3a_audio_rms(const double *buffer, size_t n);
double m3a_audio_peak_db(const double *buffer, size_t n);
double m3a_audio_rms_db(const double *buffer, size_t n);

/* ──────────────────────────────────────────────────────────────
 * L4 — Fundamental Laws: Physical Acoustics
 * ────────────────────────────────────────────────────────────── */

/**
 * Speed of sound as function of temperature (L4: acoustic wave propagation).
 * v = 331.3 * sqrt(1 + T/273.15) where T is temperature in Celsius.
 *
 * @param temp_c  air temperature in Celsius
 * @return speed of sound in m/s
 */
double m3a_speed_of_sound(double temp_c);

/**
 * Air absorption coefficient (ISO 9613-1).
 * L4: Frequency-dependent atmospheric attenuation of sound.
 *
 * @param freq_hz       frequency in Hz
 * @param temp_c        temperature in Celsius
 * @param humidity_pct  relative humidity in percent
 * @return attenuation coefficient in dB/m
 */
double m3a_air_absorption(double freq_hz, double temp_c, double humidity_pct);

/* ──────────────────────────────────────────────────────────────
 * L5 Algorithms — Function declarations (implemented in src/)
 * ────────────────────────────────────────────────────────────── */

/* hrtf.c */
int      m3a_hrtf_load_db(m3a_hrtf_db *db, const char *filename);
void     m3a_hrtf_free_db(m3a_hrtf_db *db);
int      m3a_hrtf_interpolate_bilinear(const m3a_hrtf_db *db,
            double az_deg, double el_deg, m3a_hrir *hrir_left, m3a_hrir *hrir_right);
int      m3a_hrtf_extract_itd(const m3a_hrir *left, const m3a_hrir *right, double *itd_sec);
int      m3a_hrtf_to_minimum_phase(m3a_hrtf_entry *entry);
int      m3a_hrtf_ild(const m3a_hrtf_entry *entry, double *ild_db);

/* ambisonics.c */
void     m3a_amb_encode_fuma(double az_deg, double el_deg, double wxyz[4]);
void     m3a_amb_decode_mode_matching(const double *bformat, int order,
            const m3a_speaker_layout *layout, double *gains);
int      m3a_amb_rotate_bformat(double *bformat, int order,
            double yaw_deg, double pitch_deg, double roll_deg);
double   m3a_amb_norm_factor(int n, int m, m3a_amb_norm norm);

/* binaural.c */
int      m3a_binaural_render_mono(const double *input, size_t num_samples,
            const m3a_hrir *hrir_l, const m3a_hrir *hrir_r,
            double *output_l, double *output_r, int block_size);
int      m3a_binaural_apply_itd(double *left, double *right, size_t num_samples,
            double itd_sec, int sample_rate);
void     m3a_binaural_extract_cues(const m3a_hrir *hrir_l, const m3a_hrir *hrir_r,
            int sample_rate, m3a_binaural_cues *cues);

/* spatial_panner.c */
int      m3a_vbap_calc_2d(double src_az_deg, const m3a_speaker_layout *layout,
            m3a_vbap_gains *gains);
int      m3a_vbap_calc_3d(double src_az_deg, double src_el_deg,
            const m3a_speaker_layout *layout, m3a_vbap_gains *gains);
void     m3a_pan_sine_law(double pan_deg, double *gain_l, double *gain_r);
void     m3a_pan_tangent_law(double pan_deg, double *gain_l, double *gain_r);
double   m3a_distance_attenuation(double distance_m, double reference_dist_m);

/* room_acoustics.c */
int      m3a_room_compute_params(m3a_room *room, m3a_room_params *params);
double   m3a_room_rt60_sabine(const m3a_room *room);
double   m3a_room_rt60_eyring(const m3a_room *room);
int      m3a_room_image_sources(const m3a_room *room, const m3a_vec3d *source,
            int max_order, m3a_image_source *images, size_t *num_images);
int      m3a_room_generate_early_reflections(const m3a_room *room,
            const m3a_vec3d *source, const m3a_vec3d *listener,
            int sample_rate, int max_order, m3a_audio_buffer *reflections);
int      m3a_room_fdn_late_reverb(double rt60_sec, int sample_rate,
            size_t num_samples, double *output_l, double *output_r);

/* doppler.c */
int      m3a_doppler_shift(const double *input, size_t num_samples,
            double source_velocity, double speed_of_sound,
            int sample_rate, double *output, size_t *output_len);
double   m3a_doppler_ratio(double source_velocity_ms, double observer_velocity_ms,
            double speed_of_sound);
int      m3a_doppler_resample(const double *input, size_t input_len,
            double doppler_ratio, double *output, size_t *output_len);

/* spatial_utils.c */
void     m3a_vec3d_from_spherical(const m3a_spherical *sph, m3a_vec3d *cart);
void     m3a_spherical_from_vec3d(const m3a_vec3d *cart, m3a_spherical *sph);
double   m3a_vec3d_dot(const m3a_vec3d *a, const m3a_vec3d *b);
double   m3a_vec3d_norm(const m3a_vec3d *v);
m3a_vec3d m3a_vec3d_sub(const m3a_vec3d *a, const m3a_vec3d *b);
m3a_vec3d m3a_vec3d_normalize(const m3a_vec3d *v);
double   m3a_great_circle_distance_deg(double az1, double el1, double az2, double el2);
double   m3a_db_to_linear(double db);
double   m3a_linear_to_db(double linear);
void     m3a_window_hann(double *buffer, size_t n);
void     m3a_window_hamming(double *buffer, size_t n);
void     m3a_window_blackman(double *buffer, size_t n);
m3a_mat3 m3a_rotation_matrix(double yaw, double pitch, double roll);
m3a_vec3d m3a_mat3_mul_vec(const m3a_mat3 *R, const m3a_vec3d *v);

/* scene management */
int    m3a_scene_init(m3a_scene *scene, int sample_rate, int block_size, size_t max_sources);
void   m3a_scene_free(m3a_scene *scene);
int    m3a_scene_add_source(m3a_scene *scene, const m3a_vec3d *position,
            const m3a_audio_buffer *signal, double gain);
int    m3a_scene_set_source_position(m3a_scene *scene, int source_id, const m3a_vec3d *position);
int    m3a_scene_set_listener_pose(m3a_scene *scene, const m3a_vec3d *position,
            double yaw_deg, double pitch_deg, double roll_deg);
int    m3a_scene_render_block(m3a_scene *scene, double *output_l, double *output_r,
            size_t num_samples);
int    m3a_scene_render_with_reverb(m3a_scene *scene, double *output_l, double *output_r,
            size_t num_samples);
int    m3a_spatialize_source(const double *mono_input, size_t num_samples,
            double az_deg, double el_deg, double distance_m,
            int sample_rate, double *output_l, double *output_r);
int    m3a_source_is_audible(const m3a_vec3d *source_pos, const m3a_vec3d *listener_pos,
            double source_spl_db, double threshold_db, double occlusion_loss_db);
double m3a_estimate_spl(const double *buffer, size_t n, double dbfs_to_spl_offset);

#ifdef __cplusplus
}
#endif

#endif /* MINI_3D_AUDIO_H */

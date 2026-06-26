/*
 * binaural.h — Binaural Rendering Pipeline
 *
 * Reference: Blauert, J. "Spatial Hearing: The Psychophysics of Human Sound
 * Localization" (1997), MIT Press.
 *
 * Knowledge Coverage:
 *   L2:    Binaural rendering chain (convolution, ITD, near-field correction)
 *   L5:    Overlap-add / overlap-save FFT convolution
 *   L5:    Fractional-delay ITD application (Lagrange interpolation)
 *   L5:    Multi-source binaural mixing with head tracker
 *   L6:    HRTF-based virtual auditory display (VAD) rendering
 *   L7:    VR headphone spatialization with head-tracking
 */

#ifndef BINAURAL_H
#define BINAURAL_H

#include "mini_3d_audio.h"

/* ──────────────────────────────────────────────────────────────
 * Binaural Renderer Configuration
 * ────────────────────────────────────────────────────────────── */

/**
 * FFT overlap-add convolution state.
 * L5 Algorithm: Overlap-add method for efficient convolution of long
 * signals with HRIR filters (O(N log N) vs O(N·M) direct).
 */
typedef struct {
    double *fft_buffer;         /**< FFT working buffer */
    double *overlap_l;          /**< overlap save buffer, left ear */
    double *overlap_r;          /**< overlap save buffer, right ear */
    size_t  fft_size;           /**< FFT length (power of 2) */
    size_t  block_size;         /**< processing block size */
    size_t  overlap_len;        /**< overlap length = HRIR_len - 1 */
} m3a_ola_state;

/**
 * Binaural renderer — complete state for binaural synthesis.
 * L6: Virtual auditory display: create spatial sound over headphones.
 */
typedef struct {
    m3a_hrtf_db     *hrtf_db;
    m3a_hrir         current_hrir_l;
    m3a_hrir         current_hrir_r;
    double           itd_sec;
    double           ild_db;
    m3a_ola_state    ola_l;
    m3a_ola_state    ola_r;
    int              sample_rate;
    int              block_size;
    /* Head tracker state */
    double           head_yaw_deg;
    double           head_pitch_deg;
    double           head_roll_deg;
    /* Near-field compensation */
    int              enable_nfc;
    double           source_distance_m;
} m3a_binaural_renderer;

/**
 * HRTF personalization parameters (based on anthropometry).
 * L8 Advanced: Customize HRTF using listener's head/ear measurements
 * (Algazi et al., 2001).
 */
typedef struct {
    double head_width_cm;       /**< bitragion breadth */
    double head_depth_cm;       /**< head depth front-to-back */
    double head_height_cm;      /**< head height */
    double pinna_height_cm;     /**< pinna flange height */
    double pinna_width_cm;      /**< pinna flange width */
    double cavum_concha_height_cm; /**< concha cavity height */
    double cavum_concha_width_cm;  /**< concha cavity width */
} m3a_anthropometry;

/* ──────────────────────────────────────────────────────────────
 * Function Declarations
 * ────────────────────────────────────────────────────────────── */

/** Initialize overlap-add convolution state */
int m3a_ola_init(m3a_ola_state *ola, size_t block_size, size_t hrir_len,
                 int sample_rate);

/** Free overlap-add state */
void m3a_ola_free(m3a_ola_state *ola);

/** Overlap-add convolution of signal with HRIR (single ear) */
void m3a_ola_process(m3a_ola_state *ola, const double *input, size_t num_samples,
                     const double *hrir, size_t hrir_len, double *output);

/** Initialize binaural renderer with HRTF database */
int m3a_binaural_init(m3a_binaural_renderer *renderer,
                      m3a_hrtf_db *hrtf_db, int sample_rate, int block_size);

/** Free binaural renderer resources */
void m3a_binaural_free(m3a_binaural_renderer *renderer);

/** Set listener head orientation (from head tracker) */
void m3a_binaural_set_head_orientation(m3a_binaural_renderer *renderer,
                                       double yaw_deg, double pitch_deg, double roll_deg);

/** Update HRTF for a source direction (relative to listener) */
int m3a_binaural_update_source(m3a_binaural_renderer *renderer,
                               double az_deg, double el_deg, double distance_m);

/** Process one block of mono audio through binaural renderer */
int m3a_binaural_process_block(m3a_binaural_renderer *renderer,
                               const double *input, size_t num_samples,
                               double *output_l, double *output_r);

/** Process multi-source scene to binaural stereo */
int m3a_binaural_mix_sources(m3a_binaural_renderer *renderer,
                             const double *input, size_t num_samples,
                             int num_sources,
                             const double *source_azimuths,
                             const double *source_elevations,
                             const double *source_gains,
                             double *output_l, double *output_r);

/** Apply fractional delay (for fine ITD adjustment) using 4-point Lagrange interpolation */
void m3a_fractional_delay(const double *input, size_t num_samples,
                          double delay_samples, double *output);

/** Personalized HRTF scaling using anthropometry model */
int m3a_hrtf_personalize(const m3a_hrtf_db *generic_db,
                         const m3a_anthropometry *anthro,
                         m3a_hrir *hrir_l, m3a_hrir *hrir_r,
                         double az_deg, double el_deg);

#endif /* BINAURAL_H */

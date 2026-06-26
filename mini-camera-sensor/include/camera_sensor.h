/**
 * @file camera_sensor.h
 * @brief CMOS/CCD solid-state image sensor core definitions
 *
 * Knowledge Domain: Solid-State Image Sensors (SSIS)
 *
 * L1 Coverage (15+ independent type/struct definitions):
 *   Sensor technology (FSI/BSI CMOS, CCD variants, sCMOS, SPAD, Event)
 *   Shutter mechanism (global, rolling, electronic-global, mechanical)
 *   Pixel architecture (3T, 4T PPD, 5T GS, 6T DCG, shared)
 *   ADC architecture (single-slope, SAR, cyclic, delta-sigma, dual-gain)
 *   HDR mode (multi-exposure, dual conversion, split-pixel, staggered, DOL)
 *   CFA pattern (Bayer RGGB/BGGR/GRBG/GBRG, mono, RCCB, RCCC, Quad, RGBW)
 *   Quantum efficiency, full-well capacity, conversion gain
 *   Dark current, read noise, PRNU, FPN, crosstalk, blooming
 *   Dynamic range, SNR, fill factor, pixel pitch, linearity
 *
 * L2 Core Concepts:
 *   Photoelectric conversion, charge accumulation, correlated double sampling
 *   Photon transfer curve analysis, QE measurement methodology
 *   Sensor configuration management, register programming model
 *
 * L4 Fundamental Laws:
 *   Poisson photon statistics → shot noise = sqrt(signal)
 *   Dynamic range formula: DR = 20*log10(FWC / noise_floor)
 *   Arrhenius dark current: I ∝ 2^(ΔT / ΔT_doubling)
 *   Total SNR: S / sqrt(S + N_read^2 + N_dark + (pS)^2 + N_quant^2)
 *
 * Reference Textbooks:
 *   - Nakamura "Image Sensors and Signal Processing for Digital Still Cameras" (2005)
 *   - Holst & Lomheim "CMOS/CCD Sensors and Camera Systems" (2011)
 *   - Janesick "Photon Transfer" (2007); "Scientific Charge-Coupled Devices" (2001)
 *   - Poynton "Digital Video and HD: Algorithms and Interfaces" (2012)
 *   - Theuwissen "Solid-State Imaging with Charge-Coupled Devices" (1995)
 *
 * University Curriculum Alignment:
 *   MIT 6.630 EM — photodetection physics, semiconductor optical sensors
 *   Stanford EE247 — optical/imaging sensor design
 *   Berkeley EE117 — optoelectronics, photodiodes
 *   Michigan EECS411 — microwave/detector semiconductor physics
 *   ETH 227-0455 — semiconductor photodetectors, EM sensors
 *   Tsinghua EM Fields — photoelectric detection and sensor physics
 */

#ifndef CAMERA_SENSOR_H
#define CAMERA_SENSOR_H

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*===========================================================================
 * L1: ENUM TYPE DEFINITIONS (7 independent enum types)
 *===========================================================================*/

/** Sensor silicon technology type. Determines architecture and performance
 *  envelope: FSI has lower QE (metal stack shadowing), BSI achieves >90% QE,
 *  CCD has superior charge transfer efficiency but higher power. */
typedef enum {
    SENSOR_TYPE_CMOS_FSI      = 0,  /* Front-Side Illuminated: light through
                                       metal/dielectric stack; fill factor
                                       limited by in-pixel transistors */
    SENSOR_TYPE_CMOS_BSI      = 1,  /* Back-Side Illuminated: light enters
                                       from substrate back; QE >90% achievable;
                                       dominant in smartphones since ~2011 */
    SENSOR_TYPE_CCD_FT        = 2,  /* Full-Frame CCD: no interline storage;
                                       requires mechanical shutter or strobe */
    SENSOR_TYPE_CCD_IT        = 3,  /* Interline Transfer CCD: vertical CCD
                                       shift registers between photodiode columns */
    SENSOR_TYPE_CCD_FIT       = 4,  /* Frame-Interline Transfer: combines FT+IT
                                       for fast vertical transfer to storage area */
    SENSOR_TYPE_SCMOS         = 5,  /* Scientific CMOS: <1e- read noise,
                                       microlens, back-illuminated, cooled */
    SENSOR_TYPE_SPAD          = 6,  /* Single-Photon Avalanche Diode: Geiger mode,
                                       photon counting, sub-ns timing resolution */
    SENSOR_TYPE_EVENT         = 7   /* Dynamic Vision Sensor (DVS): asynchronous
                                       per-pixel brightness change detection,
                                       microsecond latency, sparse output */
} sensor_technology_t;

/** Shutter mechanism. Determines exposure method and motion artifact behavior.
 *  Global shutter eliminates rolling shutter distortion but adds per-pixel
 *  storage transistor, reducing fill factor by 10-20%. */
typedef enum {
    SHUTTER_GLOBAL            = 0,  /* All pixels start/stop integration
                                       simultaneously; zero spatial distortion
                                       for fast-moving scenes */
    SHUTTER_ROLLING           = 1,  /* Row-sequential reset and read; each row
                                       integrates at slightly different time;
                                       causes skew/wobble for lateral motion */
    SHUTTER_ELECTRONIC_GLOBAL = 2,  /* In-pixel storage node; global transfer
                                       to storage then sequential readout */
    SHUTTER_MECHANICAL        = 3   /* External physical focal-plane shutter;
                                       used with FT-CCD and DSLR still capture */
} shutter_type_t;

/** Active Pixel Sensor (APS) pixel topology. The number of transistors
 *  per pixel trades fill factor against functionality (CDS, GS, HDR). */
typedef enum {
    PIXEL_3T                  = 0,  /* 3T: reset (RST), source follower (SF),
                                       row select (SEL). Simple, high fill
                                       factor, but no CDS → high kTC noise */
    PIXEL_4T                  = 1,  /* 4T Pinned Photodiode: adds transfer gate
                                       (TX). Enables true CDS, complete charge
                                       transfer. Industry standard since ~2005 */
    PIXEL_5T                  = 2,  /* 5T Global Shutter: adds storage gate (SG)
                                       for simultaneous charge transfer to
                                       light-shielded storage node */
    PIXEL_6T                  = 3,  /* 6T Dual Conversion Gain: switchable
                                       sense-node capacitance for per-frame
                                       high/low gain; single-exposure HDR */
    PIXEL_SHARED              = 4   /* Multi-pixel shared FD: 2x2 or 4x1 sharing
                                       reduces transistors/pixel, increases
                                       fill factor in small pixels */
} pixel_architecture_t;

/** Column-parallel ADC architecture. On-chip ADCs are the dominant
 *  CMOS sensor architecture; 1 ADC per column for massive parallelism. */
typedef enum {
    ADC_SINGLE_SLOPE          = 0,  /* Ramp-compare ADC: global ramp vs
                                       per-column comparator + counter.
                                       Simple, low power, but limited speed */
    ADC_SAR                   = 1,  /* Successive Approximation Register:
                                       binary search via capacitive DAC.
                                       Fast, medium power, good DNL */
    ADC_CYCLIC                = 2,  /* Cyclic/algorithmic ADC: iterative
                                       residue amplification. Compact analog,
                                       good for small pixel pitch */
    ADC_DELTA_SIGMA           = 3,  /* Delta-Sigma modulator + decimation:
                                       oversampling for highest SNR. 14-16+ bit
                                       effective, slower per conversion */
    ADC_DUAL_GAIN             = 4   /* Dual-gain ADC: simultaneous high/low
                                       gain readout paths. Extends DR in
                                       single exposure without motion artifacts */
} adc_architecture_t;

/** High Dynamic Range (HDR) capture mode. Extends sensor DR beyond
 *  single-exposure FWC/read_noise limit. Key for automotive, surveillance. */
typedef enum {
    HDR_NONE                  = 0,  /* Standard single exposure; DR = native
                                       sensor DR (~60-75 dB typical) */
    HDR_MULTI_EXPOSURE        = 1,  /* Sequential multi-exposure: short+long
                                       capture fused in ISP. Motion artifacts
                                       between exposures. 100-120+ dB DR */
    HDR_DUAL_CONVERSION       = 2,  /* Simultaneous high/low CG: per-pixel
                                       dual readout. No motion gap. Requires
                                       DCG pixel (6T or dual FD) */
    HDR_SPLIT_PIXEL           = 3,  /* Large+small photodiode per pixel site.
                                       Inherently HDR, no temporal gap.
                                       Reduces effective resolution */
    HDR_STAGGERED             = 4,  /* Alternating row exposures: odd rows
                                       short, even rows long. Single-frame HDR
                                       with inter-line motion artifacts */
    HDR_DOL                   = 5   /* Digital Overlap: overlapping readout
                                       of successive exposures. Minimizes
                                       temporal gap between exposures */
} hdr_mode_t;

/** Color Filter Array (CFA) pattern. Defines spatial color sampling.
 *  Bayer (RGGB) is universal; other patterns optimize for specific use cases
 *  (automotive RCCC for red+clear, RGBW for low-light sensitivity). */
typedef enum {
    CFA_BAYER_RGGB            = 0,  /* Standard Bayer: 50% green (luma),
                                       25% red, 25% blue. Human vision matched */
    CFA_BAYER_BGGR            = 1,  /* Bayer variant: B G / G R */
    CFA_BAYER_GRBG            = 2,  /* Bayer variant: G R / B G */
    CFA_BAYER_GBRG            = 3,  /* Bayer variant: G B / R G */
    CFA_MONOCHROME            = 4,  /* No color filter: luminance only.
                                       Max QE, no demosaicing needed */
    CFA_RCCB                  = 5,  /* Red-Clear-Clear-Blue: higher SNR
                                       via clear pixels, color from R/B */
    CFA_RCCC                  = 6,  /* Red-Clear-Clear-Clear: automotive
                                       75% clear for sensitivity, red for
                                       tail-light/stop-sign detection */
    CFA_QUAD_BAYER            = 7,  /* 2x2 same-color binning groups.
                                       Enables on-sensor binning with
                                       Bayer-equivalent color sampling */
    CFA_RGBW                  = 8   /* Red-Green-Blue-White: W pixel for
                                       luminance, R/G/B for chrominance.
                                       Superior low-light SNR */
} cfa_pattern_t;

/*===========================================================================
 * L1: SENSOR SPECIFICATION DATA STRUCTURE
 *     22 independent physical parameters mapped 1:1 to datasheet values
 *===========================================================================*/

/**
 * @brief Physical sensor specification — all datasheet parameters
 *
 * Each field represents an independent L1 core definition from solid-state
 * image sensor physics. The struct is designed to fully characterize any
 * CMOS/CCD sensor from a datasheet perspective.
 */
typedef struct {
    /* Optical format — sensor die dimensions */
    double   optical_format_inch;     /* Diagonal in inches (e.g. 1/2.55", 1/1.7").
                                         Used for lens matching: image circle
                                         must cover sensor diagonal */
    double   sensor_width_um;         /* Active pixel array width [um] */
    double   sensor_height_um;        /* Active pixel array height [um] */

    /* Pixel array geometry */
    uint32_t active_w;                /* Active columns (horizontal resolution) */
    uint32_t active_h;                /* Active rows (vertical resolution) */
    double   pixel_pitch_um;          /* Center-to-center pixel spacing [um].
                                         The most critical scaling parameter:
                                         determines QE, FWC, DR, resolution */
    double   fill_factor;             /* Ratio: photodiode area / pixel area.
                                         FSI typical: 0.3-0.6. BSI: 0.8-1.0 */

    /* Photoelectric conversion — the core physical process (L1+L4) */
    double   full_well_capacity_e;    /* Max electrons storable in PD [e-/pixel].
                                         FWC ∝ pixel_area ∝ pitch^2.
                                         Typical: 2000 (1.1um) to 80000 (6um) */
    double   conversion_gain_uv_e;    /* Sense node voltage per electron [uV/e-].
                                         CG = q / C_sn where C_sn is sense node
                                         capacitance. 30-200 uV/e- typical */
    double   quantum_efficiency_peak; /* Maximum QE at optimal wavelength [0-1].
                                         BSI: 0.80-0.95. FSI: 0.40-0.70.
                                         QE(λ) = electrons_out / photons_in */
    double   spectral_min_nm;         /* Minimum usable wavelength [nm].
                                         Silicon bandgap limits NIR: ~1100 nm */
    double   spectral_max_nm;         /* Maximum usable wavelength [nm].
                                         UV limited by surface recombination */

    /* Noise floor — fundamental limits (L1 + L4) */
    double   read_noise_e_rms;        /* Input-referred temporal read noise
                                         [e- RMS]. Includes SF thermal noise,
                                         column amp noise, ADC quantization.
                                         sCMOS: <1.0 e-. Consumer: 2-10 e- */
    double   dark_current_60c;        /* Dark current at 60C junction [e-/px/s].
                                         Thermally generated e- in depletion
                                         region. Doubles every ~6C (Arrhenius) */
    double   dark_doubling_c;         /* Temperature increment for 2x increase
                                         [degC]. Silicon: 5.5-7.0 C typical */
    double   prnu_coeff;              /* Photo Response Non-Uniformity [0-1].
                                         Pixel-to-pixel sensitivity variation
                                         from microlens/CFA/PD geometry mismatch */
    double   fpn_coeff;               /* Fixed Pattern Noise [0-1]. Column FPN
                                         from amplifier offset mismatch.
                                         Correctable via digital CDS */

    /* Dynamic range — derived from noise parameters (L4 law) */
    double   dynamic_range_db;        /* DR = 20*log10(FWC / read_noise) [dB].
                                         At minimum analog gain. Consumer: 65-75 dB.
                                         HDR modes: 100-140 dB. sCMOS: ~90 dB */
    double   snr_max_db;              /* SNR at saturation = 20*log10(sqrt(FWC)).
                                         Quantum limit; no sensor can exceed this.
                                         FWC=10000 → SNR_max=40 dB */

    /* On-chip ADC */
    uint8_t  adc_bits;                /* ADC resolution [8, 10, 12, 14, 16].
                                         Determines quantization step size */
    adc_architecture_t adc_type;      /* Column ADC topology */
    double   adc_ref_mv;              /* ADC reference voltage [mV] */

    /* Shutter */
    shutter_type_t shutter_type;      /* Global snapshot vs rolling */
    double   min_exposure_us;         /* Shortest exposure [us].
                                         Limited by reset + TX timing */
    double   max_exposure_us;         /* Longest useful exposure [us].
                                         Limited by dark current + saturation */

    /* Programmable analog gain */
    double   gain_min;                /* Minimum total gain (>= 1.0x).
                                         Unity gain typically at base ISO */
    double   gain_max;                /* Maximum total gain [x].
                                         Limited by amplifier noise floor */
    double   gain_step;               /* Gain step granularity (0.1, 0.3, 1.0 etc) */

    /* High-speed readout interface */
    double   pixel_clock_mhz;         /* Output data clock frequency [MHz] */
    uint8_t  mipi_lanes;              /* MIPI D-PHY data lanes (1, 2, 4) */
    double   mipi_rate_mbps;          /* Per-lane serial data rate [Mbps].
                                         MIPI D-PHY v2.1: up to 4.5 Gbps/lane */

    /* Silicon technology */
    sensor_technology_t technology;   /* FSI/BSI CMOS, CCD family, sCMOS, etc */
    pixel_architecture_t pixel_arch;  /* 3T/4T/5T/6T/shared pixel topology */
    cfa_pattern_t cfa;                /* Bayer pattern or variant */

    /* Parasitic effects */
    double   crosstalk_coeff;         /* Electrical crosstalk between adjacent
                                         pixels [0-1]. Causes MTF degradation
                                         and color mixing at pixel boundaries */
    double   blooming_threshold_e;    /* Excess charge threshold for blooming
                                         [e-]. Above this, charge spills to
                                         adjacent pixels (vertical streaks) */
    double   linearity_error_pct;     /* Max deviation from linear PTC [%].
                                         <1%: excellent. <3%: acceptable.
                                         Measured via PTC analysis */

    /* Power consumption */
    double   active_power_mw;         /* Streaming power at full res/fps [mW] */
    double   standby_power_uw;        /* Idle/standby power [uW] */
} sensor_spec_t;

/*===========================================================================
 * L1: RUNTIME CONFIGURATION & TELEMETRY
 *===========================================================================*/

/**
 * @brief Per-frame programmable sensor register state
 *
 * Models all runtime-changeable parameters exposed by a typical MIPI
 * Camera Serial Interface (CSI) or I2C/SPI sensor register map.
 */
typedef struct {
    /* Region of Interest — windowed/cropped readout */
    uint32_t roi_x;                   /* ROI start column (must be even for Bayer) */
    uint32_t roi_y;                   /* ROI start row (must be even) */
    uint32_t roi_w;                   /* ROI width [pixels] */
    uint32_t roi_h;                   /* ROI height [pixels] */

    /* Charge-domain binning (improves SNR at expense of resolution) */
    uint8_t  binning_h;               /* Horizontal binning factor (1, 2, 4).
                                         Charge summation before readout */
    uint8_t  binning_v;               /* Vertical binning factor (1, 2, 4) */
    uint8_t  skip_h;                  /* Digital horizontal subsampling */
    uint8_t  skip_v;                  /* Digital vertical subsampling */

    /* Exposure triangle parameters */
    double   exposure_us;             /* Integration time [us].
                                         Electronic shutter: reset-to-read interval */
    double   analog_gain;             /* PGA (programmable gain amplifier) setting.
                                         Analog gain reduces ADC quantization
                                         impact but amplifies sensor noise too */
    double   digital_gain;            /* Post-ADC digital multiplication.
                                         Does not improve SNR, just scales DN */

    /* High Dynamic Range configuration */
    hdr_mode_t hdr_mode;              /* HDR capture mode */
    uint8_t  hdr_exposures;           /* Number of exposures (2, 3, 4) */
    double   hdr_ratio;               /* Ratio between successive exposures.
                                         e.g., ratio=4: exposures at T, T/4, T/16 */

    /* Frame rate */
    double   target_fps;              /* Desired output frame rate [Hz].
                                         Sensor may output slower if limited
                                         by exposure time + readout time */

    /* Black level calibration */
    uint16_t black_level_dn;          /* Optical black pixel average [DN].
                                         Subtracted from active pixels in ISP.
                                         Compensates for ADC offset + dark current */

    /* Diagnostic / manufacturing test */
    uint8_t  test_pattern;            /* 0=live image, 1=solid color,
                                         2=color bars, 3=walking ones,
                                         4=PN9 pseudo-random */
} sensor_config_t;

/**
 * @brief Computed real-time sensor telemetry
 *
 * Derived from sensor_spec_t + sensor_config_t. Represents actual
 * operational parameters including derived metrics.
 */
typedef struct {
    double   effective_fps;           /* Achieved frame rate [Hz].
                                         May be lower than target_fps */
    double   effective_exposure_us;   /* Actual integration time used [us].
                                         Clamped between min and max */
    double   effective_gain;          /* Total gain = analog * digital [x] */
    double   row_time_us;             /* Single row readout duration [us].
                                         row_time = (active_cols+h_blank)/pclk */
    double   frame_time_us;           /* Full frame cycle [us].
                                         frame_time = (active_rows+v_blank)*row_time */
    uint32_t output_w;                /* Output image width after ROI+bin+skip */
    uint32_t output_h;                /* Output image height after ROI+bin+skip */
    double   data_rate_mbps;          /* Total output data rate [Mbps] */
    double   dark_signal_e;           /* Accumulated dark electrons per pixel.
                                         dark = dark_current(60C) * exposure_time
                                         * 2^((T-60)/doubling_c) */
    double   noise_floor_e;           /* Total noise floor [e- RMS].
                                         sqrt(read^2 + dark_signal + quant^2) */
    double   current_dr_db;           /* Dynamic range at current gain.
                                         DR reduced at high gain due to lower FWC */
} sensor_status_t;

/*===========================================================================
 * L4: FUNDAMENTAL PHYSICS FUNCTIONS
 *===========================================================================*/

/**
 * Compute sensor dynamic range in dB.
 *
 * Theorem (L4): DR = 20 * log10( FWC / noise_floor_rms )
 * where noise_floor_rms = sqrt( read_noise^2 + dark_signal + LSB^2/12 )
 *
 * Dark signal is Poisson: mean = variance = dark_e. Quantization adds
 * LSB^2/12 variance (uniform distribution over [-LSB/2, LSB/2]).
 *
 * Reference: Holst & Lomheim §3.4, Eq. 3.17
 * Complexity: O(1)
 *
 * @param fwc_e      Full-well capacity [electrons]
 * @param read_noise Read noise RMS [electrons]
 * @param dark_e     Dark signal accumulated during exposure [electrons]
 * @param lsb_e      ADC LSB size [electrons per DN]
 * @return Dynamic range [dB]
 */
double sensor_dynamic_range_db(double fwc_e, double read_noise_e,
                                double dark_e, double lsb_e);

/**
 * Compute photon shot noise limited SNR (linear).
 *
 * Theorem (L4 — Poisson statistics): For a Poisson process with mean N,
 * the standard deviation is sqrt(N). Hence:
 *   SNR_linear = N / sqrt(N) = sqrt(N).
 *
 * This is the fundamental quantum-mechanical noise floor. No sensor
 * technology can exceed this limit for a given signal level.
 *
 * Example: FWC=10000 e- → SNR_max = 100 → 40 dB at saturation.
 *
 * Reference: Nakamura §2.1.1; Janesick "Photon Transfer" Ch.2
 * Complexity: O(1)
 *
 * @param signal_e Signal level in electrons
 * @return SNR as linear ratio (not dB)
 */
double sensor_snr_shot_limited(double signal_e);

/**
 * Compute total sensor SNR including all noise sources.
 *
 * Complete noise model (L4 — sensor noise budget):
 *   SNR = S / sqrt( S + N_read^2 + D + (p*S)^2 + N_quant^2 )
 *
 * where:
 *   S          = signal [e-], Poisson noise variance = S
 *   N_read^2   = read noise variance (Gaussian, signal-independent)
 *   D          = dark current [e-], Poisson noise variance = D
 *   (p*S)^2    = PRNU variance (proportional to signal squared)
 *   N_quant^2  = quantization noise = LSB^2 / 12
 *
 * Reference: Holst & Lomheim §3.5, Eq. 3.23; Nakamura §2.3.2
 * Complexity: O(1)
 *
 * @param signal_e    Signal level [electrons]
 * @param read_noise  Input-referred read noise RMS [electrons]
 * @param dark_e      Dark signal accumulated [electrons]
 * @param prnu_coeff  PRNU coefficient [0-1] (fractional sensitivity variation)
 * @param lsb_e       ADC LSB size [electrons/DN]
 * @return SNR (linear ratio, not dB)
 */
double sensor_total_snr(double signal_e, double read_noise_e,
                         double dark_e, double prnu_coeff, double lsb_e);

/**
 * Compute dark current at temperature T using Arrhenius relationship.
 *
 * L4 Law: I_dark(T) = I_dark(T0) * 2^( (T - T0) / deltaT_doubling )
 *
 * Silicon dark current doubles approximately every 5.5-7.0 °C due to
 * the exponential temperature dependence of thermal generation in the
 * depletion region. This is why scientific sensors are cooled to -20°C
 * to -80°C, reducing dark current by 3-4 orders of magnitude.
 *
 * Reference: Holst & Lomheim §4.2.1; Theuwissen Ch.5 §5.3
 * Complexity: O(1)
 *
 * @param dark_t0     Dark current at reference temperature [e-/s]
 * @param t_celsius   Target junction temperature [°C]
 * @param t0_celsius  Reference temperature [°C]
 * @param doubling_c  Doubling increment [°C], silicon ~6.0
 * @return Dark current at temperature T [e-/s]
 */
double sensor_dark_current_at_temp(double dark_t0, double t_celsius,
                                    double t0_celsius, double doubling_c);

/**
 * Photon shot noise standard deviation (Poisson distribution).
 *
 * For Poisson-distributed photon arrival: sigma = sqrt(N_photons).
 * This irreducible noise sets the fundamental sensitivity limit
 * for any photodetector.
 *
 * Complexity: O(1)
 * @param photons Number of detected photons (electrons)
 * @return Standard deviation [electrons RMS]
 */
double sensor_shot_noise_sigma(double photons);

/*===========================================================================
 * L2: CORE CONCEPT FUNCTIONS — Sensor characterization methodology
 *===========================================================================*/

/**
 * Compute full-well capacity from voltage swing and conversion gain.
 *
 * FWC = V_swing_max / CG
 * where CG must be expressed in V/e- (convert from uV/e-: CG_V = CG_uV * 1e-6).
 *
 * This method is used when FWC is estimated from oscilloscope measurement
 * of the pixel output voltage at saturation, rather than from PTC.
 *
 * Complexity: O(1)
 *
 * @param v_swing_v    Maximum linear voltage swing at sense node [V]
 * @param cg_uv_per_e  Conversion gain [uV/e-]
 * @return Full well capacity [electrons]
 */
double sensor_fwc_from_vswing(double v_swing_v, double cg_uv_per_e);

/**
 * Photon Transfer Curve (PTC) analysis — the gold standard for sensor
 * characterization. PTC relates noise variance to mean signal.
 *
 * Method: At multiple illumination levels, capture N frames. Compute
 * temporal mean and variance per pixel (averaged over uniform region).
 * Plot variance vs mean. The shot-noise-limited region has slope = 1/CG.
 *
 * PTC reveals:
 *   - Conversion gain (slope of shot-noise region)
 *   - Read noise (y-intercept, extrapolated to mean=0)
 *   - Full-well capacity (saturation knee)
 *   - PRNU (deviation from linear at high signal)
 *   - Linearity (curvature before saturation)
 *
 * Reference: Janesick, "Photon Transfer" (2007), SPIE Press
 * Complexity: O(n)
 *
 * @param mean   Array of mean signal values [DN], length n
 * @param var    Array of temporal variance values [DN^2], length n
 * @param n      Number of data points (typically 20-100)
 * @param cg_dn_e Output: conversion gain [DN/e-]
 * @return R-squared (coefficient of determination) of linear fit [0-1].
 *         R^2 > 0.99 indicates excellent PTC linearity.
 */
double sensor_ptc_analysis(const double *mean, const double *var,
                            uint32_t n, double *cg_dn_e);

/**
 * Compute quantum efficiency from photocurrent and incident optical power.
 *
 * QE = (electrons generated per second) / (photons incident per second)
 *    = (I_photo / q) / (P_opt / (h * nu))
 *    = (I_photo * h * c) / (q * lambda * P_opt)
 *
 * Physical constants:
 *   q = 1.602176634e-19 C     (elementary charge)
 *   h = 6.62607015e-34 J*s    (Planck constant)
 *   c = 2.99792458e8 m/s      (speed of light in vacuum)
 *
 * Complexity: O(1)
 *
 * @param photocurrent_a  Measured photocurrent [A]
 * @param opt_power_w     Incident optical power [W]
 * @param wavelength_nm   Monochromatic wavelength [nm]
 * @return Quantum efficiency [0-1]. Values > 1 indicate measurement error
 *         or impact ionization gain (SPAD/APD).
 */
double sensor_quantum_efficiency(double photocurrent_a, double opt_power_w,
                                  double wavelength_nm);

/*===========================================================================
 * L2: CONFIGURATION MANAGEMENT FUNCTIONS
 *===========================================================================*/

/**
 * Initialize sensor specification with realistic defaults.
 *
 * Sets values representative of a modern 12MP 1/2.55" BSI CMOS smartphone
 * sensor (similar to Sony IMX series, Samsung ISOCELL).
 *
 * Complexity: O(1)
 */
void sensor_spec_init_default(sensor_spec_t *s);

/**
 * Initialize configuration with safe, valid defaults bounded by spec.
 *
 * Full resolution, mid-range exposure, unity gain, no HDR, valid ROI.
 *
 * Complexity: O(1)
 */
void sensor_config_init_default(sensor_config_t *cfg, const sensor_spec_t *s);

/**
 * Validate sensor configuration against specification limits.
 *
 * Checks: ROI within active array, binning factors valid (1/2/4),
 * exposure within [min,max], gain within [min,max], HDR mode compatible
 * with sensor capabilities.
 *
 * Complexity: O(1)
 *
 * @return 0 if valid, -1 if any violation (detail printed to stderr)
 */
int sensor_config_validate(const sensor_config_t *cfg, const sensor_spec_t *s);

/**
 * Compute output resolution after applying ROI, binning, and subsampling.
 *
 * output_w = roi_w / binning_h / skip_h
 * output_h = roi_h / binning_v / skip_v
 *
 * Complexity: O(1)
 */
void sensor_output_resolution(const sensor_config_t *cfg,
                               uint32_t *w, uint32_t *h);

/**
 * Compute single row readout time.
 *
 * row_time = (active_columns + h_blank_pixels) / pixel_clock_frequency
 *
 * h_blank: additional dead time per row for CDS, ADC settling
 *
 * Complexity: O(1)
 */
double sensor_row_time_us(const sensor_spec_t *s, const sensor_config_t *cfg,
                           uint32_t h_blank);

/**
 * Compute maximum achievable frame rate.
 *
 * frame_time = (active_rows + v_blank) * row_time
 * fps_max = 1e6 / frame_time_us
 *
 * v_blank: additional dead rows for frame reset, ISP processing
 *
 * Complexity: O(1)
 */
double sensor_max_frame_rate(const sensor_spec_t *s, const sensor_config_t *cfg,
                              uint32_t h_blank, uint32_t v_blank);

/**
 * Compute electrons per digital number.
 *
 * e-/DN = 1 / (CG_in_DN * analog_gain)
 * where CG_in_DN = (conversion_gain_uv_e * 1e-6) * (2^adc_bits) / adc_ref_mv * 1e3
 *
 * This factor is crucial for converting between DN and physical units.
 *
 * Complexity: O(1)
 */
double sensor_dn_per_electron(const sensor_spec_t *s, const sensor_config_t *cfg);

/**
 * Print sensor specification to stdout in human-readable tabular form.
 *
 * Complexity: O(1)
 */
void sensor_spec_print(const sensor_spec_t *s);

/**
 * Compute complete sensor telemetry (status) from spec and config.
 *
 * Derives all fields in sensor_status_t from the physical parameters
 * and current configuration, including timing, noise, and DR.
 *
 * Complexity: O(1)
 */
void sensor_compute_status(const sensor_spec_t *s, const sensor_config_t *cfg,
                            sensor_status_t *st);

/**
 * Compute effective full-well capacity accounting for charge binning.
 *
 * Charge-domain binning sums charge from multiple pixels:
 *   FWC_eff = FWC * binning_h * binning_v
 *
 * This increases SNR proportionally to sqrt(binning factor).
 *
 * Complexity: O(1)
 */
double sensor_effective_fwc(const sensor_spec_t *s, const sensor_config_t *cfg);

/**
 * Convert digital number to electrons.
 *
 * e- = (DN - black_level) * (e- per DN)
 *
 * Subtracts black level offset then applies the current gain factor.
 *
 * Complexity: O(1)
 */
double sensor_dn_to_electrons(const sensor_spec_t *s, const sensor_config_t *cfg,
                               uint16_t dn);

#endif /* CAMERA_SENSOR_H */
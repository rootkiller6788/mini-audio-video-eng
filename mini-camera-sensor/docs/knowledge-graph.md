# Knowledge Graph — mini-camera-sensor

## L1: Definitions — COMPLETE
| ID | Definition | Type | Location |
|----|-----------|------|----------|
| D1 | Quantum Efficiency (QE) | double | camera_sensor.h |
| D2 | Full-Well Capacity (FWC) | double | camera_sensor.h |
| D3 | Conversion Gain (CG) | double | camera_sensor.h |
| D4 | Read Noise | double | camera_sensor.h |
| D5 | Dark Current | double | camera_sensor.h |
| D6 | PRNU (Photo Response Non-Uniformity) | double | camera_sensor.h |
| D7 | FPN (Fixed Pattern Noise) | double | camera_sensor.h |
| D8 | Dynamic Range (DR) | double | camera_sensor.h |
| D9 | Pixel Pitch | double | camera_sensor.h |
| D10 | Fill Factor | double | camera_sensor.h |
| D11 | Sensor Technology Types (FSI/BSI/CCD/sCMOS/SPAD/DVS) | enum | camera_sensor.h |
| D12 | Shutter Types (Global/Rolling/Electronic-Global) | enum | camera_sensor.h |
| D13 | Pixel Architectures (3T/4T/5T/6T/Shared) | enum | camera_sensor.h |
| D14 | ADC Architectures (SS/SAR/Cyclic/DS/DualGain) | enum | camera_sensor.h |
| D15 | HDR Modes (Multi-exposure/DCG/Split/Staggered/DOL) | enum | camera_sensor.h |
| D16 | CFA Patterns (Bayer variants/Mono/RCCB/RCCC/Quad) | enum | camera_sensor.h |
| D17 | Pixel Raw Type | typedef | pixel_array.h |
| D18 | Bayer Color Phases (R/Gr/Gb/B) | enum | pixel_array.h |
| D19 | Pixel Defect Types (Dead/Hot/Stuck/Blinking/Column/Cluster) | enum | pixel_array.h |
| D20 | CIE XYZ, CIE xy, CIELAB | struct | color_science.h |
| D21 | Illuminants (A/B/C/D50/D55/D65/D75/E/F2/F7/F11) | enum | color_science.h |
| D22 | Exposure Value (EV) | double | exposure_control.h |
| D23 | Metering Modes (Average/CW/Spot/Matrix/Highlight/Face) | enum | exposure_control.h |
| D24 | Flicker Modes (Off/50Hz/60Hz/Auto) | enum | exposure_control.h |
| D25 | Noise Components (Shot/Read/Dark/PRNU/FPNcol/FPNrow/kTC/Quant/1f/RTS) | enum | noise_model.h |

## L2: Core Concepts — COMPLETE
| ID | Concept | Implementation |
|----|---------|---------------|
| C1 | Photoelectric Conversion | sensor_quantum_efficiency() |
| C2 | Charge Accumulation & Readout | sensor_compute_status() |
| C3 | Correlated Double Sampling (CDS) | noise_ktc_reset_e() |
| C4 | Bayer Spatial Sampling | bayer_color_at(), bayer_color_counts() |
| C5 | Sensor Characterization (PTC) | sensor_ptc_analysis() |
| C6 | ISP Pipeline Data Flow | isp_pipeline.h |
| C7 | RAW→RGB→YUV Pipeline | isp_pipeline.h stages |
| C8 | Trichromatic Color Theory | color_science.h conversions |
| C9 | Chromatic Adaptation | color_bradford_cat(), color_von_kries_cat() |
| C10 | Exposure Triangle | ae_pi_control() |
| C11 | 18% Gray / Zone System | ae_config_init_default() |
| C12 | Sensor Noise Budget | noise_params_t |
| C13 | Black Level Calibration | calibrate_black_level() |
| C14 | Lens Shading (cos^4 law) | calibrate_lens_shading() |

## L3: Mathematical Structures — COMPLETE
| ID | Structure | Implementation |
|----|-----------|---------------|
| M1 | 2D Array Indexing & Striding | raw_frame_t.stride, pixel_array.c |
| M2 | 3x3 Color Matrices | CCM in isp_pipeline.c, color_science.c |
| M3 | CIE 1931 Standard Observer | SRGB_TO_XYZ, XYZ_TO_SRGB matrices |
| M4 | Poisson Distribution | noise_generate_shot() |
| M5 | Gaussian Distribution (Box-Muller) | noise_generate_read() |
| M6 | Linear Least Squares (Normal Equations) | color_ccm_calibrate(), isp_ccm_calibrate() |
| M7 | 2D Convolution/Interpolation | Demosaic kernels (5x5) |
| M8 | Cramer's Rule 3x3 Solver | color_ccm_calibrate(), isp_ccm_calibrate() |
| M9 | Histogram & Percentile Statistics | ae_compute_statistics() |
| M10 | PI Controller | ae_pi_control() |

## L4: Fundamental Laws — COMPLETE
| ID | Law/Theorem | Formula | Location |
|----|------------|---------|----------|
| T1 | Dynamic Range | DR = 20*log10(FWC/NoiseFloor) | sensor_dynamic_range_db() |
| T2 | Poisson Shot Noise | sigma = sqrt(N) | sensor_shot_noise_sigma() |
| T3 | Total SNR | S/sqrt(S+Nr^2+D+(pS)^2+q^2/12) | sensor_total_snr() |
| T4 | Arrhenius Dark Current | I(T)=I(T0)*2^((T-T0)/ΔT) | sensor_dark_current_at_temp() |
| T5 | kTC Reset Noise | sigma = sqrt(kTC)/q | noise_ktc_reset_e() |
| T6 | Johnson-Nyquist | v_n = sqrt(4kTR·BW) | noise_johnson_nyquist_vrms() |
| T7 | Quantization Noise | sigma = LSB/sqrt(12) | noise_quantization_rms() |
| T8 | sRGB Transfer Function | piecewise linear+power | isp_gamma_srgb_encode/decode() |
| T9 | Planckian Locus | CIE xy(T) | color_planckian_xy() |
| T10 | Gray World Assumption | avg(R)=avg(G)=avg(B) | color_gray_world() |

## L5: Algorithms/Methods — COMPLETE
| ID | Algorithm | Location |
|----|-----------|----------|
| A1 | Bilinear Demosaicing | demosaic_bilinear() |
| A2 | Malvar-He-Cutler (2004) | demosaic_mhc() |
| A3 | Gradient-Corrected Demosaicing | demosaic_gradient_corrected() |
| A4 | AHD Demosaicing (Hirakawa-Parks 2005) | demosaic_ahd() |
| A5 | Gray World WB | color_gray_world() |
| A6 | White Patch WB | color_white_patch() |
| A7 | Shades of Gray WB | color_shades_of_gray() |
| A8 | CCM (LS Calibration) | color_ccm_calibrate() |
| A9 | Bradford CAT | color_bradford_cat() |
| A10 | von Kries CAT | color_von_kries_cat() |
| A11 | McCamy CCT | color_xy_to_cct_mccamy() |
| A12 | PI-based AE Control | ae_pi_control() |
| A13 | Average Metering | ae_meter_average() |
| A14 | Center-Weighted Metering | ae_meter_center_weighted() |
| A15 | Spot Metering | ae_meter_spot() |
| A16 | Matrix Metering | ae_meter_matrix() |
| A17 | Percentile Metering | ae_meter_percentile() |
| A18 | Flicker Detection | flicker_detect() |
| A19 | PTC Noise Estimation | noise_ptc_estimate() |
| A20 | Read Noise Estimation | noise_estimate_read_noise() |
| A21 | Hot/Dead Pixel Detection | detect_hot_pixels(), detect_dead_pixels() |
| A22 | Column FPN Correction | estimate_column_fpn() |
| A23 | 2x2 Charge Binning | raw_frame_bin_2x2() |
| A24 | Lens Shading Calibration | calibrate_lens_shading() |
| A25 | Defect Map Generation | calibrate_defect_map() |
| A26 | PTC Characterization | calibrate_ptc() |
| A27 | sRGB ↔ XYZ Conversion | srgb_linear_to_xyz(), xyz_to_srgb_linear() |
| A28 | XYZ ↔ Lab Conversion | xyz_to_lab(), lab_to_xyz() |
| A29 | CIEDE2000 | color_delta_e_2000() |
| A30 | CIEDE CMC | color_delta_e_cmc() |

## L6: Canonical Problems — COMPLETE
| ID | Problem | Solution |
|----|---------|----------|
| P1 | Bayer→Full-Color Reconstruction | Demosaic algorithms comparison (example_isp_demosaic.c) |
| P2 | Sensor Telemetry Computation | sensor_compute_status() (example_sensor_spec.c) |
| P3 | AE Convergence | PI control + scene adaptation (example_auto_exposure.c) |
| P4 | ColorChecker Calibration | color_ccm_calibrate() + calibrate_color() |
| P5 | Noise Profile Characterization | PTC analysis + noise_ptc_estimate() |
| P6 | HDR Exposure Bracket | ae_hdr_bracket(), ae_determine_hdr_bracket() |
| P7 | RAW→sRGB Rendering | Full ISP pipeline (isp_pipeline_execute()) |
| P8 | SNR vs Exposure Optimization | noise_vs_gain(), noise_optimal_gain() |

## L7: Applications — PARTIAL+ (3 applications)
| ID | Application | Implementation |
|----|------------|---------------|
| AP1 | Digital Camera ISP | Full pipeline: RAW→RGB→YUV |
| AP2 | Smartphone Camera | Default sensor spec (1/2.55", 12MP BSI) |
| AP3 | Factory Calibration | calibrate_sensor_full() pipeline |
| AP4 | Machine Vision Camera | RCCC CFA, ROI, binning, subsampling |
| AP5 | Scientific CMOS (sCMOS) | sCMOS sensor type, ultra-low noise model |

## L8: Advanced Topics — PARTIAL (3/5 topics)
| ID | Topic | Status |
|----|-------|--------|
| AD1 | Adaptive Homogeneity-Directed Demosaicing | IMPLEMENTED (demosaic_ahd) |
| AD2 | CIEDE2000 Perceptual Color Difference | IMPLEMENTED (color_delta_e_2000) |
| AD3 | Bradford Chromatic Adaptation | IMPLEMENTED (color_bradford_cat) |
| AD4 | RTS/1-f Noise Models | IMPLEMENTED (noise_generate_pixel) |
| AD5 | Deep Learning Demosaicing | DOCUMENTED ONLY |

## L9: Research Frontiers — PARTIAL (documented)
| ID | Frontier | Status |
|----|----------|--------|
| RF1 | SPAD/Quanta Image Sensors | DOCUMENTED (SENSOR_TYPE_SPAD) |
| RF2 | Event-Based/DVS Cameras | DOCUMENTED (SENSOR_TYPE_EVENT) |
| RF3 | Computational Photography | DOCUMENTED |
| RF4 | 6G/Terahertz Imaging | DOCUMENTED |

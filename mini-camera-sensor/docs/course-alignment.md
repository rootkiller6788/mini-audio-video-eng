# Course Alignment — mini-camera-sensor

## Nine-University Curriculum Mapping

### MIT — 6.630 Electromagnetic Waves
| Topic | Implementation |
|-------|---------------|
| Photodetection physics | sensor_quantum_efficiency() |
| Semiconductor optical sensors | sensor_technology_t (FSI/BSI) |
| Photon statistics | noise_generate_shot(), sensor_snr_shot_limited() |

### Stanford — EE247 Optical/Imaging Sensors
| Topic | Implementation |
|-------|---------------|
| Imaging sensor design | pixel_architecture_t (3T/4T/5T/6T) |
| Noise in detectors | noise_params_t, kTC, Johnson-Nyquist |
| Array readout | row_time, max_frame_rate computation |

### Berkeley — EE117 Electromagnetics (Optoelectronics)
| Topic | Implementation |
|-------|---------------|
| Photodiodes | sensor_spec_t (QE, FWC, spectral range) |
| p-n junction physics | dark current Arrhenius model |
| Detector figures of merit | DR, SNR, NEP modeling |

### Illinois — ECE 310 Digital Signal Processing
| Topic | Implementation |
|-------|---------------|
| 2D sampling theory | Bayer CFA spatial sampling |
| Interpolation and reconstruction | Demosaic algorithms (bilinear, MHC, AHD) |
| Filter design | 5x5 MHC kernels, bilateral filter |

### Michigan — EECS 411 Microwave/Detector Physics
| Topic | Implementation |
|-------|---------------|
| Semiconductor detectors | Silicon spectral response (380-1050 nm) |
| Thermal noise | kTC noise, dark current doubling |
| Detector characterization | PTC analysis (Janesick method) |

### Georgia Tech — ECE 6350 EM (Optical Sensors)
| Topic | Implementation |
|-------|---------------|
| Optical sensor systems | Complete sensor spec + config model |
| Imaging arrays | Pixel array readout, binning, subsampling |
| System performance | SNR curve, DR analysis |

### TU Munich — High-Frequency Engineering
| Topic | Implementation |
|-------|---------------|
| Photodiodes in sensor systems | CMOS sensor technology comparison |
| Noise in electronic systems | Full noise budget (10 components) |
| Signal processing chains | ISP pipeline (17 stages) |

### ETH Zürich — 227-0455 EM (Semiconductor Sensors)
| Topic | Implementation |
|-------|---------------|
| Semiconductor photodetectors | Pin photodiode physics, QE |
| Precision measurement | PTC characterization, read noise estimation |
| Calibration methodology | Full factory calibration pipeline |

### Tsinghua — EM Fields (Photoelectric Sensors)
| Topic | Implementation |
|-------|---------------|
| Photoelectric detection | Photoelectric conversion model |
| Sensor physics | Complete sensor spec parameterization |
| Engineering applications | Smartphone camera, machine vision profiles |

## Reference Textbook Mapping

| Textbook | Topics Covered |
|----------|---------------|
| Nakamura (2005) — Image Sensors & Signal Processing | Sensor architectures, pixel design, noise, characterization |
| Holst & Lomheim (2011) — CMOS/CCD Sensors | DR/SNR formulas, dark current, readout, testing |
| Janesick (2007) — Photon Transfer | PTC analysis, conversion gain, noise floor |
| Poynton (2012) — Digital Video and HD | Color science, gamma, ISP pipeline, display |
| Wyszecki & Stiles (1982) — Color Science | CIE colorimetry, color difference, illuminants |

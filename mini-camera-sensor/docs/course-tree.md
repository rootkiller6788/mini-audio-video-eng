# Course Tree — mini-camera-sensor

## Prerequisite Dependency Tree

```
mini-camera-sensor
├── mini-signal-system-theory (0.)
│   ├── Fourier analysis → MTF, spatial frequency analysis
│   ├── Sampling theory → Bayer CFA, Nyquist in 2D
│   └── Convolution → Demosaic kernel operations
│
├── mini-circuit-analysis (1.)
│   ├── Transistor circuits → Source follower, reset transistor
│   └── Noise analysis → Johnson-Nyquist, kTC
│
├── mini-analog-electronics (2.)
│   ├── Amplifier design → PGA, column amplifier
│   └── ADC architectures → Single-slope, SAR, delta-sigma
│
├── mini-digital-electronics (3.)
│   ├── Digital logic → Row/column decoder
│   └── Memory architecture → Line buffer, frame buffer
│
├── mini-communication-principle (5.)
│   └── MIPI D-PHY → High-speed serial interface
│
├── mini-digital-signal-process (6.)
│   ├── 2D FIR filters → Demosaic kernels
│   ├── Median filter → Defect correction, chroma denoising
│   └── Interpolation → Bilinear, gradient-corrected
│
├── mini-electromagnetic-wave (7.)
│   ├── Photon energy → QE computation
│   ├── Wave-particle duality → Poisson statistics
│   └── Optical absorption → Spectral response
│
└── mini-sensor-measurement (8.)
    ├── Calibration methodology → Factory calibration pipeline
    ├── Uncertainty analysis → Noise estimation
    └── EMVA 1288 standard → PTC characterization

Downstream Dependencies:
├── mini-wireless-mobile-comm (11.) → Mobile phone camera integration
├── mini-navigation-positioning (14.) → Visual odometry, SLAM
├── mini-iot-edge-computing (15.) → Edge AI camera
└── mini-industrial-fieldbus (16.) → Machine vision cameras

Internal Dependency Graph:
camera_sensor.h ──→ pixel_array.h ──→ demosaic.h
                 ──→ noise_model.h
                 ──→ color_science.h ──→ isp_pipeline.h
                 ──→ exposure_control.h
                 ──→ sensor_calibration.h

Core → Pixel Array → Demosaic → ISP Pipeline (linear chain)
                  → Calibration (sensor characterization branch)
       → Noise Model → Calibration (reused by calibration)
       → Color Science → ISP Pipeline + Calibration
       → Exposure Control (independent branch)
```

## Learning Path

1. **Start with**: camera_sensor.h/c — understand sensor physics (QE, FWC, noise)
2. **Then**: pixel_array.h/c — learn Bayer CFA spatial sampling
3. **Then**: noise_model.h/c — master sensor noise physics
4. **Then**: color_science.h/c — understand colorimetry and color spaces
5. **Then**: demosaic.h/c — learn CFA interpolation algorithms
6. **Then**: isp_pipeline.h/c — integrate into full processing chain
7. **Then**: exposure_control.h/c — understand AE control theory
8. **Finally**: sensor_calibration.h/c — factory calibration methodology

## Key Equations Summary

| Equation | Formula | Location |
|----------|---------|----------|
| Dynamic Range | DR = 20·log10(FWC/σ_noise) | camera_sensor.c |
| Shot Noise SNR | SNR = √N | camera_sensor.c |
| Total SNR | S/√(S+Nr²+D+(pS)²+q²/12) | camera_sensor.c |
| Dark Current | I(T)=I(T₀)·2^(T-T₀)/ΔT | camera_sensor.c |
| kTC Noise | σ = √(kTC)/q | noise_model.c |
| sRGB Encode | V'=12.92V (V≤0.0031308) else 1.055V^(1/2.4)-0.055 | isp_pipeline.c |
| McCamy CCT | CCT=-449n³+3525n²-6823.3n+5520.33 | color_science.c |
| CIEDE2000 | ΔE = √((ΔL'/SL)²+(ΔC'/SC)²+(ΔH'/SH)²+RT·ΔC'/SC·ΔH'/SH) | color_science.c |

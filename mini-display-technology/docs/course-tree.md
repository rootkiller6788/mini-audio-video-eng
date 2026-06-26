# Course Tree — mini-display-technology

## Prerequisite Dependency Tree

```
mini-display-technology
├── 0. mini-signal-system-theory (Fourier/Laplace transforms, sampling theory)
│   └── Nyquist sampling, frequency domain analysis
├── 1. mini-circuit-analysis (RC transient for pixel charging, transmission lines)
│   └── Cable impedance, propagation delay
├── 3. mini-digital-electronics (binary encoding, parallel/serial)
│   └── 8b/10b encoding, digital logic
├── 5. mini-communication-principle (line coding, channel capacity)
│   └── TMDS coding, Shannon-Hartley
├── 6. mini-digital-signal-process (2D filtering, interpolation, decimation)
│   └── Image scaling, convolution, histogram
├── 7. mini-electromagnetic-wave (light propagation, spectral power)
│   └── Color science, CMF, photometry
├── 8. mini-sensor-measurement (photodiode, colorimeter)
│   └── Display measurement, gamma calibration
└── 11. mini-wireless-mobile-comm (MIPI DSI for mobile displays)
    └── MIPI packet protocol, DCS commands
```

## Internal Module Dependencies

```
display_types.h
├── color_science.h → display_types.h
├── display_interface.h → display_types.h
└── image_process.h → display_types.h + color_science.h

src/display_timing.c → display_types.h
src/color_science.c → color_science.h
src/image_process.c → image_process.h
src/display_interface.c → display_interface.h
src/gamma_calibration.c → display_types.h + color_science.h
src/frame_buffer.c → display_types.h
```

## Knowledge Flow
1. **Display Types & Timing** (L1-L2) → Foundation for all display work
2. **Color Science** (L1-L4) → sRGB → CIEXYZ → CIELAB → gamut → calibration
3. **Image Processing** (L5) → Scaling → Dithering → Histogram → Enhancement
4. **Display Interfaces** (L2-L6) → TMDS → EDID → HDMI → DisplayPort
5. **Gamma Calibration** (L5-L8) → LUT → BT.1886 → PQ → HDR tone mapping
6. **Frame Buffer** (L1-L2) → Allocation → Pixel I/O → Blit → Integrity

## L9 Research Frontiers
- MicroLED: GaN μLED arrays, mass transfer, <10μm pixel pitch
- Quantum Dot: CdSe/InP QD, narrow FWHM emission, BT.2020 gamut
- Holographic/Light-field: Wavefront reconstruction, spatial light modulators


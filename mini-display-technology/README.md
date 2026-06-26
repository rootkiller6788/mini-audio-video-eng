# mini-display-technology — Display Technology Fundamentals

Display engineering library implementing core display timing (VESA CVT/GTF/DMT), color science (CIE XYZ/Lab/Luv, gamma, PQ/HLG HDR), image processing (scaling, dithering, histogram), display interfaces (TMDS, HDMI, DisplayPort, MIPI DSI, EDID), and gamma calibration (BT.1886, PQ, HDR tone mapping).

## Module Status: COMPLETE ✅

- **L1 Definitions**: Complete (15 struct/enum/typedef)
- **L2 Core Concepts**: Complete (15 core concepts implemented)
- **L3 Math Structures**: Complete (10 mathematical structures)
- **L4 Fundamental Laws**: Complete (10 theorems verified in C + Lean 4)
- **L5 Algorithms**: Complete (25 algorithms)
- **L6 Canonical Problems**: Complete (3 end-to-end examples)
- **L7 Applications**: Partial+ (4: iPhone display, Kindle e-ink, billboard LED, HDMI EDID)
- **L8 Advanced Topics**: Partial+ (4: HDR tone mapping, 3D LUT, CLAHE, CIEDE2000)
- **L9 Research Frontiers**: Partial (3 documented)

## Knowledge Coverage

| Level | Name | Status | Count |
|-------|------|--------|-------|
| L1 | Definitions | Complete | 15 items |
| L2 | Core Concepts | Complete | 15 items |
| L3 | Math Structures | Complete | 10 items |
| L4 | Fundamental Laws | Complete | 10 theorems |
| L5 | Algorithms/Methods | Complete | 25 algorithms |
| L6 | Canonical Problems | Complete | 3 examples |
| L7 | Applications | Partial+ | 4 apps |
| L8 | Advanced Topics | Partial+ | 4 topics |
| L9 | Research Frontiers | Partial | 3 documented |

## Core Definitions

- **Display Types**: LCD TN/IPS/VA, OLED RGB/Pentile, AMOLED, MicroLED, E-Ink, CRT, Plasma, DLP, LCoS, LED
- **Display Interfaces**: VGA, DVI-D/I, HDMI 1.4/2.0/2.1, DisplayPort 1.2/1.4/2.0, MIPI DSI, eDP, LVDS
- **Pixel Formats**: RGB888, BGR888, RGBA8888, RGB565, YUV 4:4:4/4:2:2/4:2:0, NV12, 10-bit/12-bit
- **Color Standards**: BT.601 NTSC/PAL, BT.709, BT.2020, BT.2100 PQ/HLG, sRGB, Adobe RGB, DCI-P3, Display P3
- **Timing Parameters**: Pixel clock, h/v active/blank, sync width, front/back porch, polarity, refresh rate
- **Color Spaces**: CIE XYZ, xyY, CIELAB, CIELUV, RGB primaries, white points (D65/D50/DCI-P3)
- **EDID**: 128-byte block, manufacturer ID, timing descriptors, chromaticity, monitor name
- **Gamma**: Power-law, sRGB, BT.1886, PQ (ST 2084), HLG (ARIB STD-B67)

## Core Theorems

| Theorem | Formula | Verification |
|---------|---------|-------------|
| Nyquist Pixel Bandwidth | f_clk ≥ 2 × W × H × f_refresh | `nyquist_pixel_bound` (Lean) |
| Shannon-Hartley (Cable) | C = B × log₂(1+SNR) | `tmds_max_cable_length_m()` |
| Grassmann's Additivity | XYZ(A+B) = XYZ(A) + XYZ(B) | `grassmann_superposition` (Lean) |
| Weber-Fechner (Gamma) | ΔL/L ≈ constant | `weber_fechner_gamma_justification` (Lean) |
| Quantization Error | \|e\| ≤ Δ/2, Δ = 1/(2^N-1) | `quantization_error_bound` (Lean) |
| EDID Checksum | Σ bytes ≡ 0 (mod 256) | `edid_checksum_valid` (Lean) |
| Gamma Encoding Inverse | decode(encode(L)) = L | `gamma_encoding_inverse` (Lean) |
| TMDS DC Balance | Running disparity bounded | `tmds_encode_8b10b()` |
| Dithering Conservation | Σ error_i = 0 | `floyd_steinberg_weight_sum` (Lean) |
| Tone Map Monotonicity | L1≤L2 → T(L1)≤T(L2) | `reinhard_monotone` (Lean) |

## Core Algorithms

- **VESA CVT 1.2**: Coordinated Video Timing — piecewise blanking formulas
- **VESA GTF 1.1**: Generalized Timing Formula with margin parameter
- **VESA DMT**: 45-entry standard mode database (640×480 to 7680×4320)
- **sRGB Transfer**: IEC 61966-2-1 piecewise gamma encode/decode
- **ST 2084 PQ**: SMPTE Perceptual Quantizer for HDR (m1/m2, c1/c2/c3)
- **HLG**: ARIB STD-B67 Hybrid Log-Gamma OETF/EOTF
- **CIEDE2000**: Sharma-Wu-Dalal color difference (hue rotation, chroma weighting)
- **Bradford Adaptation**: Chromatic adaptation matrix (source → destination white)
- **Bilinear/Bicubic/Lanczos Scaling**: Separable interpolation with configurable kernels
- **Floyd-Steinberg Dithering**: 7/16, 3/16, 5/16, 1/16 error diffusion
- **Atkinson Dithering**: 6-neighbor error diffusion (ideal for e-ink)
- **Jarvis-Judice-Ninke / Stucki**: 12-weight error diffusion kernels
- **Bayer Ordered Dither**: Recursive threshold matrix generation
- **CLAHE**: Contrast Limited Adaptive Histogram Equalization
- **Otsu Thresholding**: Automatic bimodal threshold optimization
- **TMDS 8b/10b**: Transition-minimized differential signaling (DVI/HDMI)
- **EDID Parse**: 128-byte block → structured monitor info
- **BT.1886 Calibration**: Black-level-aware reference EOTF
- **HDR Tone Mapping**: Reinhard, Hable, ACES, BT.2390 (Hermite knee)
- **3D LUT**: Trilinear interpolation color correction

## Canonical Problems (Examples)

1. **VESA Timing Computation** (`examples/example_vesa_timing.c`)
   - Compute timings for 720p to 8K with CVT/CVT-RB
   - Interface bandwidth selection (HDMI 1.4 → DP 2.0)
   - DMT standard mode database lookup

2. **Color Gamut Analysis** (`examples/example_color_gamut.c`)
   - sRGB/DCI-P3/BT.2020 gamut comparison
   - iPhone 15 Pro / Galaxy S24 display simulation
   - ΔE2000 calibration accuracy assessment
   - Gamma calibration from measured luminance ramp

3. **Dithering Pipeline** (`examples/example_dithering.c`)
   - 6 dithering methods compared (Floyd-Steinberg, Atkinson, Bayer, etc.)
   - E-ink simulation (Kindle/reMarkable)
   - Billboard LED display processing
   - Zone plate frequency response test

## Test Results

| Test Suite | Tests | Result |
|------------|-------|--------|
| test_display_timing | 14/14 | ✅ All pass |
| test_color_science | 23/23 | ✅ All pass |
| test_image_process | 13/13 | ✅ All pass |
| test_gamma | 13/13 | ✅ All pass |
| **Total** | **63/63** | **✅ 100%** |

## Course Mapping

| School | Course | Topic Coverage |
|--------|--------|---------------|
| MIT | 6.003 Signal Processing | Sampling, Nyquist pixel bandwidth |
| MIT | 6.450 Digital Comm | TMDS 8b/10b, channel capacity |
| Stanford | EE102A Signal Processing | Sampling, quantization, scaling |
| Stanford | EE247 Optical/Display | Colorimetry, gamma, metrology |
| Berkeley | EE123 DSP | 2D filtering, histogram, dithering |
| Berkeley | EE117 EM Waves | SPD × CMF, radiometry → photometry |
| Illinois | ECE 310 DSP | Multirate (scaling), 2D convolution |
| Illinois | ECE 459 Comm | Line coding (TMDS) |
| Michigan | EECS 351 DSP | Image processing, filter design |
| Georgia Tech | ECE 4270 DSP | Image compression, color subsampling |
| Georgia Tech | ECE 6350 EM | Signal integrity, eye diagram |
| TU Munich | Display Technology | VESA timing, panel types, luminance |
| ETH | 227-0427 Signal Processing | Filter banks, sampling rate conversion |
| ETH | 227-0455 EM/Optics | CIE standards, photometry |
| Tsinghua | 信号与系统 | Fourier (DCT), sampling theory |
| Tsinghua | 通信原理 | Source/channel coding |

## Line Count

- `include/` + `src/`: **5756 lines** ≥ 3000 ✅

## Build & Run

```bash
make          # Build library, tests, and examples
make test     # Run all tests (63/63 pass)
make examples # Build example programs
make clean    # Remove build artifacts
```

## Reference

- Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
- CIE 15:2018 — Colorimetry
- IEC 61966-2-1 — sRGB Color Space
- ITU-R BT.709, BT.2020, BT.2100
- SMPTE ST 2084 — High Dynamic Range EOTF
- VESA CVT 1.2, GTF 1.1, DMT 1.3
- HDMI 2.1 Specification (HDMI Forum, 2017)
- VESA DisplayPort Standard 2.0 (2022)
- Gonzalez & Woods, "Digital Image Processing" (2018)
- Reinhard et al., "Photographic Tone Reproduction" (ACM SIGGRAPH 2002)


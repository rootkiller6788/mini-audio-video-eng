# Knowledge Graph — mini-display-technology

## L1: Definitions
| # | Item | C Struct | Lean Definition |
|---|------|----------|-----------------|
| 1 | Display Type (LCD/OLED/CRT/...) | `display_type_t` enum | — |
| 2 | Display Interface (VGA/DVI/HDMI/DP/DSI) | `display_interface_t` enum | — |
| 3 | Pixel Format (RGB888/YUV422/...) | `pixel_format_t` enum | — |
| 4 | Pixel (RGB, YCbCr, Float) | `pixel_rgb_t`, `pixel_ycbcr_t` | `RGB8` |
| 5 | Resolution (width × height) | `resolution_t` | `Resolution` |
| 6 | Display Timing (VESA-style) | `display_timing_t` | `DisplayTiming` |
| 7 | Frame Buffer | `framebuffer_t` | — |
| 8 | Color Standard (BT.601/709/2020/sRGB) | `color_standard_t` | — |
| 9 | CIE XYZ / xyY / Lab / Luv | `cie_xyz_t`, `cie_xyy_t` | `CieXYZ`, `CieXY` |
| 10 | Gamma / Transfer Function | `transfer_func_t` | — |
| 11 | White Point (D65/D50/DCI-P3) | `white_point_t` | — |
| 12 | Color Depth / Range | `color_depth_t` | `Quantization` |
| 13 | EDID Block (128 bytes) | `edid_block_t` | `EdidBlock` |
| 14 | VESA Mode (DMT standard) | `vesa_mode_t` | `VesaMode` |
| 15 | HDMI InfoFrame / CEC | `hdmi_avi_infoframe_t` | — |

## L2: Core Concepts
| # | Concept | Implementation |
|---|---------|---------------|
| 1 | Pixel Clock & Scanout | `timing_pixel_clock_hz()` |
| 2 | Blanking Intervals (H/V) | CVT/GTF blanking formulas |
| 3 | Refresh Rate Calculation | CVT/GTF/DMT |
| 4 | Frame Buffer Read/Write | `framebuffer_pixel_write/read()` |
| 5 | Color Space Conversion | RGB↔XYZ↔Lab↔Luv↔xyY↔YCbCr |
| 6 | Gamma Encoding (OETF/EOTF) | `transfer_srgb_*`, `transfer_pq_*` |
| 7 | Image Scaling (Nearest/Bilinear/Bicubic/Lanczos) | `image_scale()` |
| 8 | Dithering & Halftoning | `image_dither()` |
| 9 | Edge Detection (Sobel) | `kernel_sobel()` |
| 10 | TMDS 8b/10b Encoding | `tmds_encode_8b10b()` |
| 11 | EDID Monitor Identification | `edid_parse_block()` |
| 12 | DisplayPort Link Calculation | `dp_link_bandwidth_mbps()` |
| 13 | Histogram & Equalization | `histogram_compute/equalize()` |
| 14 | Alpha Blending / Compositing | `framebuffer_alpha_blend()` |
| 15 | Gamma LUT Operations | `gamma_lut_create_*()` |

## L3: Mathematical Structures
| # | Structure | Implementation |
|---|-----------|---------------|
| 1 | 3×3 Color Transformation Matrix | `color_matrix_t`, matrix multiply |
| 2 | RGB Primaries System | `primaries_to_matrix[_inv]()` |
| 3 | Bradford Chromatic Adaptation | `chromatic_adaptation_bradford()` |
| 4 | Spectral Power Distribution × CMF | `spd_to_xyz()` |
| 5 | CIE 1931 2° CMF Table | `cmf_1931_data` (81 wavelength samples) |
| 6 | 2D Convolution Kernel | `conv_kernel2d_t` |
| 7 | Separable Filter Kernel | `separable_kernel_t` |
| 8 | Cumulative Distribution (Histogram) | `histogram_cdf_t` |
| 9 | Error Diffusion Matrix | Floyd-Steinberg/Atkinson/Jarvis/Stucki |
| 10 | 3D LUT Trilinear Interpolation | `color_lut3d_apply()` |

## L4: Fundamental Laws
| # | Theorem | Verification |
|---|---------|-------------|
| 1 | Nyquist-Shannon (pixel sampling BW) | `nyquist_pixel_bound` (Lean) |
| 2 | Shannon-Hartley (cable channel capacity) | `tmds_max_cable_length_m()` |
| 3 | Grassmann's Laws (color mixing) | `grassmann_scalability/superposition` (Lean) |
| 4 | Weber-Fechner Law (gamma basis) | `weber_fechner_gamma_justification` (Lean) |
| 5 | Quantization Error Bound (Δ/2) | `quantization_error_bound` (Lean) |
| 6 | Gamma Encoding Inverse | `gamma_encoding_inverse` (Lean) |
| 7 | EDID Checksum Property (Σ≡0 mod 256) | `edid_checksum_valid` (Lean) |
| 8 | Tone Mapping Monotonicity | `reinhard_monotone` (Lean) |
| 9 | VESA Mode Consistency | `standard_vesa_mode_consistency` (Lean) |
| 10 | Dithering Error Conservation | `floyd_steinberg_weight_sum` (Lean) |

## L5: Algorithms/Methods
| # | Algorithm | Complexity | File |
|---|-----------|-----------|------|
| 1 | VESA CVT 1.2 Timing Computation | O(1) | `display_timing.c` |
| 2 | VESA GTF 1.1 Timing Computation | O(1) | `display_timing.c` |
| 3 | VESA DMT Mode Lookup | O(n) | `display_timing.c` |
| 4 | sRGB Transfer Encode/Decode | O(1) | `color_science.c` |
| 5 | ST 2084 PQ OETF/EOTF | O(1) | `color_science.c` |
| 6 | HLG OETF/EOTF | O(1) | `color_science.c` |
| 7 | CIEDE2000 Color Difference | O(1) | `color_science.c` |
| 8 | Gamut Coverage (Monte Carlo) | O(n) | `color_science.c` |
| 9 | Bilinear Image Scaling | O(W×H) | `image_process.c` |
| 10 | Bicubic/Lanczos Scaling | O(W×H×K) | `image_process.c` |
| 11 | Floyd-Steinberg Dithering | O(W×H) | `image_process.c` |
| 12 | Atkinson Dithering | O(W×H) | `image_process.c` |
| 13 | Jarvis-Judice-Ninke Dithering | O(W×H) | `image_process.c` |
| 14 | Stucki Dithering | O(W×H) | `image_process.c` |
| 15 | Bayer Ordered Dither | O(W×H) | `image_process.c` |
| 16 | Bayer Matrix Generation (Recursive) | O(2^2n) | `image_process.c` |
| 17 | CLAHE | O(W×H×Tiles) | `image_process.c` |
| 18 | Otsu Thresholding | O(256) | `image_process.c` |
| 19 | TMDS 8b/10b Encode | O(8) | `display_interface.c` |
| 20 | Gamma Power-Law Fitting (Linear Reg) | O(n) | `gamma_calibration.c` |
| 21 | BT.1886 EOTF LUT | O(256) | `gamma_calibration.c` |
| 22 | Reinhard Tone Mapping | O(1) | `gamma_calibration.c` |
| 23 | Hable Film Tone Mapping | O(1) | `gamma_calibration.c` |
| 24 | ACES Tone Mapping | O(1) | `gamma_calibration.c` |
| 25 | BT.2390 Tone Mapping (Hermite) | O(1) | `gamma_calibration.c` |

## L6: Canonical Problems
| # | Problem | Example |
|---|---------|---------|
| 1 | Compute VESA timing for any resolution/refresh | `example_vesa_timing.c` |
| 2 | Compare display color gamuts and calibrate | `example_color_gamut.c` |
| 3 | End-to-end dithering pipeline with quality assessment | `example_dithering.c` |

## L7: Applications
| # | Application | Evidence |
|---|-------------|---------|
| 1 | iPhone 15 Pro / Galaxy S24 Display Analysis | `example_color_gamut.c` print |
| 2 | E-Ink Kindle / reMarkable Dithering | `example_dithering.c` Atkinson |
| 3 | Billboard LED Display Processing | `example_dithering.c` Step 7 |
| 4 | HDMI EDID Monitor Detection | `edid_parse_block()` |

## L8: Advanced Topics
| # | Topic | Implementation |
|---|-------|---------------|
| 1 | HDR Tone Mapping (Reinhard/Hable/ACES/BT.2390) | `gamma_calibration.c` |
| 2 | 3D Color LUT with Trilinear Interpolation | `color_lut3d_apply()` |
| 3 | CLAHE (Contrast Limited Adaptive HE) | `image_process.c` |
| 4 | CIEDE2000 Perceptual Color Metric | `color_science.c` |

## L9: Research Frontiers
| # | Frontier | Documentation |
|---|----------|--------------|
| 1 | MicroLED Pixel Density (>3000 PPI AR/VR) | `display_technology.lean` |
| 2 | Quantum Dot Gamut Expansion | `display_technology.lean` |
| 3 | Holographic / Light-field Display | Documented here |


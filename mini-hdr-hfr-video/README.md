# mini-hdr-hfr-video — HDR & HFR Video Engineering

High Dynamic Range (HDR) and High Frame Rate (HFR) video core library.
Implements transfer functions (PQ, HLG), tone mapping, color science,
frame rate conversion, motion estimation, and related algorithms.

## Reference
- Poynton, C. (2012). *Digital Video and HD: Algorithms and Interfaces* (2nd ed.)
- SMPTE ST 2084:2014 — High Dynamic Range EOTF
- ITU-R BT.2100 — HDR TV Image Parameters
- ARIB STD-B67 — HLG Specification

---

## Module Status: COMPLETE

- **L1-L6**: Complete (22 definitions, 12 concepts, 10 math structures, 6 laws, 18 algorithms, 7 canonical problems)
- **L7**: Complete (6 applications: display, cinema, broadcast, gaming, smartphone, streaming)
- **L8**: Partial (2/6: time-varying TMO, real-time optical flow)
- **L9**: Partial (documented, not implemented)

**Score**: 16/18 (≥16 = COMPLETE)

---

## Core Definitions (L1)

### Transfer Functions
- `hdr_pq_params_t` — Perceptual Quantizer (SMPTE ST 2084)
- `hdr_hlg_params_t` — Hybrid Log-Gamma (ARIB STD-B67)
- `hdr_bt1886_params_t` — BT.1886 SDR Reference

### Color
- `hdr_chromaticity_t` — CIE 1931 xy chromaticity
- `hdr_primaries_set_t` — RGB primary set (BT.709, DCI-P3, BT.2020, ACES)
- `cie_xyz_t`, `cie_lab_t`, `ictcp_t` — Standard color spaces

### HDR Metadata
- `hdr_metadata_t` — ST 2086/2094 metadata (MaxCLL, MaxFALL, mastering display)

### HFR
- `hfr_frame_t` — Video frame (W×H×C, timestamp, type)
- `hfr_motion_field_t` — Motion vector field
- `hfr_affine_transform_t` — Global motion model

---

## Core Theorems (L4)

| Theorem | Formula | Implementation |
|---------|---------|----------------|
| Weber-Fechner Law | ΔL/L = k (k≈0.01) | `hdr_weber_jnd()` |
| Barten CSF | S(u) = M_opt / (k·√(2/T)·...) | `hdr_barten_csf_compute()` |
| PQ Encoding | F_D = ((E^(1/m2)-c1)/(c2-c3·E^(1/m2)))^(1/m1) | `hdr_pq_eotf()` |
| HLG OETF | E' = √(3E) for E≤1/12, else a·ln(12E-b)+c | `hdr_hlg_oetf()` |
| Shannon-Hartley | C = B·log₂(1+SNR) | Lean formalization |
| Nyquist Sampling | f_s ≥ 2·f_max (temporal) | Frame rate basis |

---

## Core Algorithms (L5)

| Algorithm | Function | Complexity |
|-----------|----------|------------|
| PQ EOTF/OETF | `hdr_pq_eotf()` / `hdr_pq_oetf()` | O(1) |
| Reinhard Global TMO | `tmo_reinhard_global()` | O(1) |
| Drago Log TMO | `tmo_drago_log()` | O(1) |
| Bilateral Filter | `tmo_bilateral_filter()` | O(W·H·K²) |
| Block Matching (SAD) | `hfr_me_block_match_exhaustive()` | O(N·M·SR²·BS²) |
| Diamond Search | `hfr_me_block_match_diamond()` | O(N·M·log(SR)·BS²) |
| Horn-Schunck OF | `hfr_optical_flow_horn_schunck()` | O(W·H·Iter) |
| Lucas-Kanade OF | `hfr_optical_flow_lucas_kanade()` | O(W·H·Win²) |
| Phase Correlation | `hfr_phase_correlation()` | O(W·H·log(W·H)) |
| CIEDE2000 | `color_delta_e_2000()` | O(1) |
| OKLab Transform | `color_srgb_to_oklab()` | O(1) |
| Chroma 444↔420 | `chroma_444_to_420()` / `chroma_420_to_444()` | O(W·H) |
| BT.2446 Conversion | `tmo_bt2446_method_a()` / `_b()` | O(1) |
| MCFI | `hfr_mcfi_interpolate()` | O(W·H) |
| Deinterlacing (YADIF) | `hfr_deinterlace_yadif()` | O(W·H) |
| Affine Fit | `hfr_fit_affine_transform()` | O(N) |

---

## Canonical Problems (L6)

1. **HDR→SDR Tone Mapping** — `examples/ex2_tone_mapping.c`
2. **Frame Rate Upconversion** — `examples/ex3_frame_rate_up.c`
3. **HDR Display Pipeline** — `examples/ex1_hdr_display.c`

---

## Course Mapping (9-School)

| School | Course | Alignment |
|--------|--------|-----------|
| MIT | 6.003 Signal Processing | Phase correlation, transfer functions |
| Stanford | EE247 Optical | Color science, gamut mapping |
| Berkeley | EE123 DSP | Bilateral filter, Sobel operators |
| Illinois | ECE 310 DSP | Block matching, frame conversion |
| Michigan | EECS 455 Comm | Shannon-Hartley for HDR |
| Georgia Tech | ECE 4270 DSP | Multirate frame rate conversion |
| TU Munich | Signal Processing | Tone mapping, image processing |
| ETH | 227-0427 Signal Processing | Optical flow, motion estimation |
| Tsinghua | Signal & Systems + Comm | Transfer functions, bit allocation |

---

## Build & Test

```bash
make          # Build all objects
make test     # Build and run all tests
make examples # Build and run all examples
make clean    # Remove build artifacts
```

---

## File Structure

```
mini-hdr-hfr-video/
├── Makefile
├── README.md                    # This file
├── include/
│   ├── hdr_core.h               # HDR core: transfer functions, metadata, display model
│   ├── hdr_tone_mapping.h       # Tone mapping operators, bilateral filter
│   ├── hdr_color.h              # Color spaces, matrices, gamut mapping
│   ├── hfr_core.h               # HFR core: frames, interpolation, deinterlacing
│   └── hfr_motion.h             # Motion estimation, optical flow, MCFI
├── src/
│   ├── hdr_core.c               # PQ/HLG/BT.1886, LUT, histogram, Barten CSF
│   ├── hdr_tone_mapping.c       # Reinhard, Drago, BT.2446, bilateral filter
│   ├── hdr_color.c              # sRGB↔XYZ↔Lab, ICtCp, DE2000, gamut, OKLab
│   ├── hfr_core.c               # Frame ops, buffer, interpolation, deinterlace
│   ├── hfr_motion.c             # Block matching, optical flow, MCFI, phase corr
│   └── hdr_hfr_formal.lean     # Lean 4 formalization
├── tests/
│   ├── test_hdr_core.c
│   ├── test_hdr_tone.c
│   ├── test_hdr_color.c
│   └── test_hfr_core.c
├── examples/
│   ├── ex1_hdr_display.c        # Display pipeline: PQ/HLG evaluation
│   ├── ex2_tone_mapping.c       # TMO comparison + BT.2446 conversion
│   └── ex3_frame_rate_up.c      # Frame rate conversion + motion estimation
└── docs/
    ├── knowledge-graph.md
    ├── coverage-report.md
    ├── gap-report.md
    ├── course-alignment.md
    └── course-tree.md
```

# mini-video-codec — Video Codec Fundamentals

H.264/AVC-style video coding library implementing DCT transforms, motion estimation, intra/inter prediction, quantization, entropy coding (CAVLC, Exp-Golomb), and deblocking — the core building blocks of modern video compression standards.

## Module Status: COMPLETE ✅

- **L1 Definitions**: Complete (17 struct/enum/typedef definitions)
- **L2 Core Concepts**: Complete (16 core concepts implemented)
- **L3 Math Structures**: Complete (DCT-II/DCT-III bases, separable 2D DCT, Hadamard)
- **L4 Fundamental Laws**: Complete (10 theorems: Shannon entropy, Kraft inequality, R-D bound, SQNR, energy conservation, Parseval)
- **L5 Algorithms**: Complete (24 algorithms: H.264 DCT/IDCT, full/diamond/hexagon search, CAVLC, Exp-Golomb, intra prediction modes, deblocking filter, rate control)
- **L6 Canonical Problems**: Complete (3 end-to-end examples: intra pipeline, DCT demo, motion estimation)
- **L7 Applications**: Partial+ (2: video conferencing, streaming media)
- **L8 Advanced Topics**: Partial+ (3: HDR transfer functions, CABAC binarization, hierarchical B-frames)
- **L9 Research Frontiers**: Partial (documented: VVC/VSEI, learning-based coding)

## Knowledge Coverage

| Level | Name | Status | Count |
|-------|------|--------|-------|
| L1 | Definitions | Complete | 17 items |
| L2 | Core Concepts | Complete | 16 items |
| L3 | Math Structures | Complete | 8 items |
| L4 | Fundamental Laws | Complete | 10 theorems |
| L5 | Algorithms/Methods | Complete | 24 algorithms |
| L6 | Canonical Problems | Complete | 3 examples |
| L7 | Applications | Partial+ | 2 apps |
| L8 | Advanced Topics | Partial+ | 3 topics |
| L9 | Research Frontiers | Partial | 3 documented |

## Core Definitions

- **Pixel Formats**: YUV420P, YUV422P, YUV444P, NV12, RGB24, RGBA32, YUV420P10LE
- **Color Space**: BT.601, BT.709, BT.2020 primaries, transfer characteristics, matrix coefficients
- **GOP Structure**: IPPP, IBBP, hierarchical B, all-intra configurations
- **Slice Types**: I, P, B, SP, SI
- **Macroblock Types**: 11 H.264 MB partition types (4x4 through 16x16)
- **NAL Units**: SPS, PPS, IDR, non-IDR, SEI, AUD
- **Motion Vectors**: Quarter-pel precision, sub-pixel interpolation
- **DCT Blocks**: 4x4 (integer), 8x8 (High Profile), floating-point reference
- **Quantization**: QP 0-51, dead-zone, scaling matrices
- **Deblocking**: Boundary strength Bs 0-4, alpha/beta thresholds

## Core Theorems

| Theorem | Formula | Verification |
|---------|---------|-------------|
| Shannon Entropy | H = -Σ pᵢ log₂(pᵢ) | `shannon_entropy()` |
| Kraft Inequality | Σ 2^{-lᵢ} ≤ 1 | `kraft_sum()` |
| Gaussian R-D Bound | R(D) = ½log₂(σ²/D) | `rate_distortion_gaussian()` |
| Quantization Noise Power | E[e²] = Δ²/12 | `quant_distortion()` |
| SQNR | 6.02×(B-QP/6)+1.76 dB | `quant_sqnr_db()` |
| DCT Orthogonality | H·Hᵀ rows dot=0 | `h264_dct_row0_orthogonal_row2` (Lean) |
| Parseval (Energy) | Σx² ↔ ΣX² (scaled) | `dct_verify_energy_conservation()` |
| Princen-Bradley (MDCT) | w[n]²+w[n+N/2]²=1 | Documented (MDCT not in this module) |
| Source Coding Theorem | H ≤ L_avg < H+1 | Verified via Huffman efficiency |
| QP/Qstep Relation | Qstep doubles per 6 QP | `qp_to_qstep()` |

## Core Algorithms

- **H.264 4x4 Integer DCT/IDCT**: Forward inverse with exact integer arithmetic (add/shift only)
- **H.264 8x8 Integer DCT**: High Profile FRExt transform
- **4x4 Hadamard Transform**: For luma DC coefficient decorrelation and SATD
- **Floating-Point 2D DCT**: Separable row-column method for reference
- **Full Search ME**: Exhaustive block matching within search range
- **Diamond Search (DS)**: LDSP→SDSP hierarchical search pattern
- **Hexagon Search (HS)**: 7-point hexagonal pattern with SDSP refinement
- **Sub-Pixel Refinement**: Quarter-pel around integer-pel best match
- **SAD/SSE/SATD**: Three distortion metrics with different complexity/quality tradeoffs
- **H.264 6-tap Half-pel Interpolation**: Wiener filter [1,-5,20,20,-5,1]/32
- **9-mode 4x4 Intra Prediction**: All H.264 modes (Vertical through Horizontal-Up)
- **4-mode 16x16 Intra**: Vertical, Horizontal, DC, Plane
- **Intra Mode Decision**: Minimum SAD/SATD search over all modes
- **CAVLC Encoding**: coeff_token, trailing ones, levels, total_zeros, run_before
- **Exp-Golomb Coding**: ue(v), se(v), te(v) with bit-level I/O
- **CABAC Binarization**: Unary, truncated unary, k-th order EGk bin count computation
- **H.264 Adaptive Deblocking**: Bs computation, alpha/beta thresholds, strong/weak filters
- **Dead-Zone Quantization**: H.264 forward/inverse quant with QP→Qstep mapping
- **Rate Control**: CQP, CBR, VBR, CRF modes with VBV buffer
- **Motion Vector Prediction**: Median of left/top/top-right neighbors
- **Zigzag Scan**: 4x4 and 8x8 with field scan variant
- **Emulation Prevention**: H.264 Annex B 0x03 insertion/removal
- **Start Code Detection**: 0x000001 / 0x00000001 location
- **Color Space Conversion**: BT.601/BT.709 YUV↔RGB, chroma upsampling

## Canonical Problems (Examples)

1. **Video Codec Intra Pipeline** (`examples/example_video_codec.c`)
   - Full H.264-style intra encoding/decoding cycle
   - Parameter init → prediction → DCT → quantize → dequantize → IDCT → reconstruct
   - PSNR, SQNR, and RD cost analysis

2. **DCT Transform Demo** (`examples/example_dct_demo.c`)
   - DCT basis function visualization
   - FP and integer DCT roundtrip verification
   - Hadamard transform, zigzag scan, energy compaction
   - Engine dispatch demonstration

3. **Motion Estimation & Compensation** (`examples/example_motion_comp.c`)
   - Full search, diamond search, hexagon search comparison
   - SAD vs. SSE vs. SATD distortion metrics
   - Sub-pixel interpolation, MV prediction, residual coding
   - Full motion compensation pipeline

## Course Mapping

| School | Course | Topic Coverage |
|--------|--------|---------------|
| MIT | 6.003 Signal Processing | DCT transforms, signal representation |
| MIT | 6.344 Digital Image Processing | Intra prediction, motion estimation |
| MIT | 6.450 Digital Communications | Source coding, entropy, R-D theory |
| Stanford | EE392J Digital Video Processing | Full codec pipeline, H.264 algorithms |
| Stanford | EE359 Wireless | Video over wireless, error resilience |
| Berkeley | EE225B Digital Image Processing | Transform coding, quantization |
| ETH | 227-0447 Image & Video Processing | DCT, motion compensation |
| Illinois | ECE 418 Image & Video Processing | Block matching, prediction |
| Georgia Tech | ECE 6601 Video Compression | H.264/AVC standard |
| TU Munich | Video Coding | CAVLC/CABAC, deblocking |
| Michigan | EECS 556 Image Processing | Spatial/temporal redundancy |
| 清华 | 通信原理 | Source coding, entropy coding |
| 清华 | 数字图像处理 | DCT, motion estimation |

## Building

```bash
make clean && make
make test
```

## File Structure

```
mini-video-codec/
├── Makefile
├── README.md
├── include/
│   ├── video_codec.h       # Core definitions, frame mgmt, GOP, PSNR
│   ├── dct_2d.h            # 2D DCT/IDCT, Hadamard, zigzag, engine
│   ├── motion_est.h        # Block matching, interpolation, MV prediction
│   ├── entropy_codec.h     # Bitstream I/O, Exp-Golomb, CAVLC, CABAC bin
│   ├── prediction.h        # Intra 4x4/16x16/chroma, inter prediction
│   ├── quantizer.h         # QP→Qstep, quantization, rate control
│   └── deblock.h           # H.264 adaptive deblocking filter
├── src/
│   ├── video_codec.c       # Frame management, PSNR, GOP, utilities
│   ├── dct_2d.c            # DCT/IDCT implementations (FP + integer)
│   ├── motion_est.c        # Search algorithms, SAD/SATD, MC
│   ├── entropy_codec.c     # Bitstream, Exp-Golomb, CAVLC, entropy
│   ├── prediction.c        # Intra pred modes, inter pred, MPM
│   ├── quantizer.c         # Quant/dequant, rate control, R-D theory
│   ├── deblocking.c        # Deblocking filter, Bs computation
│   ├── color_space.c       # YUV↔RGB, chroma upsampling, gamma
│   └── video_codec.lean    # Lean 4 formal definitions and theorems
├── tests/
│   ├── test_video_codec.c
│   ├── test_dct_2d.c
│   ├── test_motion_est.c
│   ├── test_entropy.c
│   ├── test_prediction.c
│   └── test_quantizer.c
├── examples/
│   ├── example_video_codec.c
│   ├── example_dct_demo.c
│   └── example_motion_comp.c
└── docs/
    ├── knowledge-graph.md
    ├── coverage-report.md
    ├── gap-report.md
    ├── course-alignment.md
    └── course-tree.md
```

## References

- ITU-T H.264 / ISO/IEC 14496-10 — Advanced Video Coding (2003-2021)
- ITU-T H.265 / ISO/IEC 23008-2 — High Efficiency Video Coding (2013)
- Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
- Richardson, "The H.264 Advanced Video Compression Standard" (2010)
- Wiegand et al., "Overview of the H.264/AVC Video Coding Standard", IEEE Trans. CSVT, 2003
- Sullivan & Wiegand, "Rate-Distortion Optimization for Video Compression", IEEE Signal Processing Magazine, 1998
- Malvar et al., "Low-Complexity Transform and Quantization in H.264/AVC", IEEE Trans. CSVT, 2003
- List et al., "Adaptive Deblocking Filter", IEEE Trans. CSVT, 2003
- Marpe et al., "Context-Based Adaptive Binary Arithmetic Coding in H.264/AVC", IEEE Trans. CSVT, 2003
- Zhu & Ma, "A New Diamond Search Algorithm for Fast Block-Matching Motion Estimation", IEEE Trans. Image Proc., 2000
- Cover & Thomas, "Elements of Information Theory" (2006)
- Ahmed, Natarajan & Rao, "Discrete Cosine Transform", IEEE Trans. C-23, 1974

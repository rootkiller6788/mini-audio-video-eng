# Knowledge Graph — mini-video-codec

## L1: Definitions

| # | Item | Type | Location |
|---|------|------|----------|
| 1 | Chroma Subsampling Format (4:2:0, 4:2:2, 4:4:4) | `enum chroma_subsampling_t` | `include/video_codec.h` |
| 2 | Pixel Format (YUV420P, RGB24, NV12, etc.) | `enum pixel_format_t` | `include/video_codec.h` |
| 3 | Color Primaries (BT.709, BT.2020, etc.) | `enum color_primaries_t` | `include/video_codec.h` |
| 4 | Transfer Characteristics (BT.709, PQ, HLG) | `enum transfer_char_t` | `include/video_codec.h` |
| 5 | Color Matrix (BT.601, BT.709, BT.2020) | `enum color_matrix_t` | `include/video_codec.h` |
| 6 | Slice Type (I, P, B, SP, SI) | `enum slice_type_t` | `include/video_codec.h` |
| 7 | GOP Structure | `struct gop_structure_t` | `include/video_codec.h` |
| 8 | Macroblock Type (11 H.264 types) | `enum mb_type_t` | `include/video_codec.h` |
| 9 | Video Frame Dimensions | `struct video_dimensions_t` | `include/video_codec.h` |
| 10 | Codec Parameters | `struct codec_video_params_t` | `include/video_codec.h` |
| 11 | Video Frame (planar YUV) | `struct video_frame_t` | `include/video_codec.h` |
| 12 | NAL Unit Header | `struct nal_unit_header_t` | `include/video_codec.h` |
| 13 | SPS (Sequence Parameter Set) | `struct sps_t` | `include/video_codec.h` |
| 14 | PPS (Picture Parameter Set) | `struct pps_t` | `include/video_codec.h` |
| 15 | Motion Vector (quarter-pel) | `struct motion_vector_t` | `include/motion_est.h` |
| 16 | Quantization Parameter (QP 0-51) | `QP_MIN`, `QP_MAX` | `include/quantizer.h` |
| 17 | Boundary Strength (Bs 0-4) | `enum boundary_strength_t` | `include/deblock.h` |

## L2: Core Concepts

| # | Concept | Implementation |
|---|---------|---------------|
| 1 | Color Space Conversion (YUV ↔ RGB) | `src/color_space.c` |
| 2 | Chroma Subsampling / Upsampling | `video_chroma_dims()`, `chroma_upsample_*()` |
| 3 | Video Frame Management | `video_frame_alloc/free/copy/clear()` |
| 4 | Frame Quality Metrics (PSNR, SSIM) | `video_frame_psnr()`, `video_frame_ssim_luma()` |
| 5 | Sub-Pixel Interpolation (6-tap Wiener) | `h264_interp_luma()` |
| 6 | Dead-Zone Quantization | `quantize_coeff()`, `quantize_block_4x4()` |
| 7 | Intra Prediction (spatial) | `intra_pred_4x4/16x16/chroma_8x8()` |
| 8 | Inter Prediction (temporal) | `inter_pred_generate/bipred()` |
| 9 | Motion Compensation | `motion_compensate/bipred()` |
| 10 | Entropy Coding (VLC) | `cavlc_encode_4x4/decode_4x4()` |
| 11 | GOP Picture Type Decision | `gop_get_pic_type()` |
| 12 | Deblocking (in-loop filter) | `deblock_filter_edge/mb/frame()` |
| 13 | Emulation Prevention | `emulation_prevent/remove()` |
| 14 | Reference Frame Management | `ref_frame_list_*()` |
| 15 | Weighted Prediction | `wp_weight_t` struct |
| 16 | Mode Decision (RD optimization) | `rd_cost_compute()`, `intra_mode_decision_*()` |

## L3: Mathematical Structures

| # | Structure | Implementation |
|---|-----------|---------------|
| 1 | DCT-II Basis Functions | `dct_basis(k, n, N)` |
| 2 | DCT-III (IDCT) Basis | `idct_basis(k, n, N)` |
| 3 | Separable 2D DCT (row-column) | `dct_2d_fp()`, `idct_2d_fp()` |
| 4 | Integer DCT Transform Matrices | 4x4: [1,1,1,1; 2,1,-1,-2; ...] |
| 5 | Hadamard Matrix 4x4 | `h264_hadamard_4x4()` |
| 6 | Zigzag Scan Ordering | `zigzag_4x4_table()`, `zigzag_scan_4x4()` |
| 7 | Rate-Distortion Lagrangian | J = D + λR |
| 8 | Wiener Interpolation Filter | [1, -5, 20, 20, -5, 1] / 32 |

## L4: Fundamental Laws

| # | Theorem / Law | C Verification | Lean Formalization |
|---|--------------|----------------|-------------------|
| 1 | Shannon Entropy H = -Σpᵢlog₂pᵢ | `shannon_entropy()` | — |
| 2 | Kraft Inequality Σ2^{-lᵢ} ≤ 1 | `kraft_sum()` | — |
| 3 | Gaussian R(D) = ½log₂(σ²/D) | `rate_distortion_gaussian()` | `rdLowerBound` / `rd_lower_bound_nonneg` |
| 4 | Quantization Noise E[e²] = Δ²/12 | `quant_distortion()` | `quantizationNoisePower` / `noise_power_zero` |
| 5 | SQNR ≈ 6.02×B + 1.76 dB | `quant_sqnr_db()` | — |
| 6 | DCT Row Orthogonality | — | `h264_dct_row0_orthogonal_row2` |
| 7 | Energy Conservation (Parseval) | `dct_verify_energy_conservation()` | — |
| 8 | Source Coding Theorem H ≤ L_avg < H+1 | `avg_code_length()` | — |
| 9 | Nyquist Sampling (video context) | Documented in video_params | — |
| 10 | QP/Qstep: step doubles every 6 QP | `qp_to_qstep()` / `qstepOfQp` | — |

## L5: Algorithms

| # | Algorithm | Complexity | Location |
|---|----------|------------|----------|
| 1 | H.264 4x4 Integer DCT | O(N²) per block | `h264_dct_4x4_fwd()` |
| 2 | H.264 4x4 Integer IDCT | O(N²) per block | `h264_idct_4x4_inv()` |
| 3 | H.264 8x8 Integer DCT | O(N³) direct | `h264_dct_8x8_fwd()` |
| 4 | 4x4 Hadamard Transform | O(N²) | `h264_hadamard_4x4_fwd()` |
| 5 | Floating-point 2D DCT/IDCT | O(N³) separable | `dct_2d_fp()/idct_2d_fp()` |
| 6 | Full Search ME | O(R²·B²) | `me_full_search()` |
| 7 | Diamond Search (DS) | O(sqrt(R)·B²) | `me_diamond_search()` |
| 8 | Hexagon Search (HS) | O(sqrt(R)·B²) | `me_hexagon_search()` |
| 9 | Sub-Pixel Refinement | O(B²) around best | `me_subpel_refine()` |
| 10 | SAD Computation | O(B²) | `compute_sad()` |
| 11 | SSE Computation | O(B²) | `compute_sse()` |
| 12 | SATD Computation | O(B²) via Hadamard | `compute_satd()` |
| 13 | 9-mode 4x4 Intra Prediction | O(9·16) | `intra_pred_4x4()` |
| 14 | 4-mode 16x16 Intra Prediction | O(4·256) | `intra_pred_16x16()` |
| 15 | CAVLC Encode 4x4 | O(N_nz) | `cavlc_encode_4x4()` |
| 16 | CAVLC Decode 4x4 | O(N_nz) | `cavlc_decode_4x4()` |
| 17 | Exp-Golomb ue(v)/se(v)/te(v) | O(log v) | `bs_write_ue/se/te()` |
| 18 | Zigzag Scan | O(N²) | `zigzag_scan_4x4/8x8()` |
| 19 | Adaptive Deblocking Filter | O(N) per edge | `deblock_filter_edge()` |
| 20 | Rate Control (CBR) | O(1) per frame | `rc_compute_qp()` |
| 21 | Chroma Upsampling (bilinear) | O(W·H) | `chroma_upsample_bilinear()` |
| 22 | YUV↔RGB Conversion | O(W·H) | `yuv_to_rgb_*()` |
| 23 | MV Prediction (Median) | O(1) | `compute_mvp()` |
| 24 | MV Temporal Scaling | O(1) | `mv_scale_temporal()` |

## L6: Canonical Problems

| # | Problem | Example |
|---|---------|---------|
| 1 | Intra Frame Encoding Pipeline | `examples/example_video_codec.c` |
| 2 | DCT Transform Coding & Analysis | `examples/example_dct_demo.c` |
| 3 | Block-Based Motion Estimation | `examples/example_motion_comp.c` |

## L7: Applications

| # | Application | Status |
|---|-------------|--------|
| 1 | Video Conferencing (low-latency H.264 Baseline) | Documented |
| 2 | Streaming Media (H.264 Main Profile ABR) | Documented |
| 3 | Surveillance Video (CBR, all-intra for seeking) | Documented |

## L8: Advanced Topics

| # | Topic | Implementation |
|---|-------|---------------|
| 1 | HDR Transfer Functions (PQ, HLG) | `bt709_oetf()`, `TRANSFER_PQ`, `TRANSFER_HLG` enums |
| 2 | CABAC Binarization | `cabac_unary/trunc_unary/egk_bins()` |
| 3 | Hierarchical B-Frame GOP | `gop_init_hierarchical()` |

## L9: Research Frontiers

| # | Topic | Status |
|---|-------|--------|
| 1 | VVC/H.266 Transform Design | Documented reference |
| 2 | Learning-Based Video Coding (DVC, end-to-end) | Documented reference |
| 3 | Neural Network Post-Processing Filters | Documented reference |

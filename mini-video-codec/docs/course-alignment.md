# Course Alignment — mini-video-codec

## MIT

### 6.003 Signal Processing
- **DCT transforms**: `dct_basis()`, `dct_2d_fp()` — Ch. 4-5
- **Sampling theory**: Nyquist theorem applied to video spatial/temporal sampling
- **Filter design**: H.264 6-tap Wiener interpolation filter

### 6.344 Digital Image Processing
- **Intra prediction**: Spatial prediction modes (Ch. 8 — Image Compression)
- **Motion estimation**: Block matching, SAD/SSE metrics
- **Transform coding**: DCT-based compression pipeline

### 6.450 Digital Communications
- **Source coding**: Shannon entropy, Huffman/CAVLC coding (Ch. 5)
- **Rate-distortion**: R(D) bound for Gaussian sources (Ch. 10)
- **Quantization**: Scalar quantization, SQNR analysis

## Stanford

### EE392J Digital Video Processing
- **H.264 codec architecture**: Full hybrid video coding pipeline
- **Inter prediction**: Motion estimation, sub-pixel interpolation
- **Transform**: Integer DCT, Hadamard for DC coefficients
- **Entropy coding**: CAVLC, CABAC binarization

### EE359 Wireless Communications
- **Video over wireless**: Error resilience, NAL unit structure
- **Rate control**: CBR/VBR for bandwidth-constrained channels

## UC Berkeley

### EE225B Digital Image Processing
- **Transform coding**: DCT, energy compaction analysis
- **Quantization**: Dead-zone quantizer design
- **Color spaces**: YUV/RGB conversion, chroma subsampling

## ETH Zurich

### 227-0447 Image and Video Processing
- **Motion compensation**: Block-based motion estimation (Ch. 7)
- **Deblocking**: In-loop adaptive deblocking filter
- **Transform**: Integer DCT vs. floating-point DCT comparison

## Illinois

### ECE 418 Image and Video Processing
- **Block matching**: Full search, fast search algorithms
- **Prediction**: Intra/inter mode decision

## Georgia Tech

### ECE 6601 Video Compression
- **H.264/AVC standard**: Profile/level constraints, NAL, VCL
- **Rate-distortion optimization**: Lagrangian mode decision
- **CAVLC/CABAC**: Entropy coding pipeline

## TU Munich

### Video Coding
- **Deblocking filter**: Boundary strength computation, filter decision
- **CAVLC encode/decode**: coeff_token, level, total_zeros, run_before
- **GOP structures**: IPPP, IBBP, hierarchical B

## Michigan

### EECS 556 Image Processing
- **Spatial prediction**: Intra prediction modes
- **Quality metrics**: PSNR, SSIM computation

## 清华 (Tsinghua)

### 通信原理 (Communication Principles)
- **Source coding theorem**: Shannon entropy verification
- **Exp-Golomb codes**: Universal VLC coding

### 数字图像处理 (Digital Image Processing)
- **DCT/IDCT**: Forward and inverse transforms
- **Motion estimation**: Block matching algorithms

## Cross-School Coverage Matrix

| Topic | MIT | Stan | Berk | ETH | Ill | GT | TUM | Mich | 清华 | Total |
|-------|-----|------|------|-----|-----|-----|-----|------|------|-------|
| DCT/Transform | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 9/9 |
| Motion Est. | ✓ | ✓ | — | ✓ | ✓ | ✓ | — | ✓ | ✓ | 7/9 |
| Intra Pred. | ✓ | ✓ | — | — | ✓ | ✓ | — | ✓ | — | 5/9 |
| Quantization | ✓ | — | ✓ | — | — | ✓ | — | — | ✓ | 4/9 |
| Entropy Coding | ✓ | ✓ | — | — | — | ✓ | ✓ | — | ✓ | 5/9 |
| Deblocking | — | ✓ | — | ✓ | — | — | ✓ | — | — | 3/9 |
| Rate Control | — | ✓ | — | — | — | ✓ | — | — | — | 2/9 |
| Color/Format | — | — | ✓ | — | — | — | — | ✓ | — | 2/9 |

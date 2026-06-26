# Course Prerequisite Tree — mini-video-codec

## Dependency Tree

```
mini-video-codec
├── mini-signal-system-theory (0)
│   ├── Fourier Transforms → DCT basis
│   ├── Convolution → Interpolation filters
│   └── Sampling Theorem → Spatial/temporal sampling
├── mini-digital-signal-process (6)
│   ├── FFT → Fast DCT algorithms
│   ├── FIR Filter Design → Wiener interpolation filter
│   └── Quantization → Dead-zone quantization
├── mini-communication-principle (5)
│   ├── Source Coding → CAVLC, Exp-Golomb
│   ├── Rate-Distortion Theory → RD optimization
│   └── Entropy → Shannon entropy verification
├── mini-information-theory (implied)
│   ├── Shannon's Theorems → Source coding theorem
│   └── Channel Capacity → Error-resilient video
└── mini-circuit-analysis (1) [optional]
    └── Digital Logic → Bitstream I/O implementation
```

## Internal Dependency Graph

```
video_codec.h (L1 definitions)
    ├── dct_2d.h (L3 transform structures, depends on L1 block types)
    │   └── dct_2d.c (L5 DCT algorithms)
    ├── motion_est.h (L1 MV, search range; L2 interpolation)
    │   └── motion_est.c (L5 search, SAD/SATD, L6 MC)
    ├── prediction.h (L1 intra modes; L2 prediction)
    │   └── prediction.c (L5 intra pred, L6 inter pred)
    │       └── depends on: motion_est.h (motion compensation)
    ├── entropy_codec.h (L1 bitstream, L2 VLC)
    │   └── entropy_codec.c (L4 entropy, L5 CAVLC/Exp-Golomb)
    ├── quantizer.h (L1 QP, L2 quantization)
    │   └── quantizer.c (L4 R-D, L5 rate control)
    ├── deblock.h (L1 Bs, L2 deblocking)
    │   └── deblocking.c (L5 adaptive filter)
    │       └── depends on: video_codec.h (iclip3)
    └── color_space.c (L2 YUV/RGB, L5 chroma ops)
        depends on: video_codec.h (pixel formats)
```

## Learning Path

### Beginner → Intermediate → Advanced

1. **Start**: `video_codec.h` / `video_codec.c`
   - Understand frame structures, GOP, parameters
   - PSNR/SSIM quality metrics
   
2. **Core Transform**: `dct_2d.h` / `dct_2d.c`
   - DCT basis → 2D separable DCT → H.264 integer DCT
   - Zigzag scan, energy compaction
   
3. **Prediction**: `prediction.h` / `prediction.c`
   - Intra prediction (spatial redundancy)
   - Inter prediction (temporal redundancy)
   
4. **Motion**: `motion_est.h` / `motion_est.c`
   - Block matching algorithms
   - Sub-pixel interpolation
   
5. **Quantization**: `quantizer.h` / `quantizer.c`
   - QP→Qstep, dead-zone quant
   - Rate control algorithms
   
6. **Entropy**: `entropy_codec.h` / `entropy_codec.c`
   - Exp-Golomb codes
   - CAVLC encode/decode
   
7. **Post-Processing**: `deblock.h` / `deblocking.c`
   - Boundary strength, filter thresholds
   - Strong/weak filter application

8. **Color**: `color_space.c`
   - YUV↔RGB conversions
   - Chroma subsampling/upsampling

## Formal Verification Path

1. **Definitions**: `video_codec.lean` — Inductive types for chroma, slice, MB, NAL
2. **Structural Properties**: Block partition validity, GOP picture type
3. **Theorems**: R-D bound non-negativity, DCT row orthogonality, Bs computation
4. **Future Work**: Full H.264 transform orthogonality proof (requires numeric library)

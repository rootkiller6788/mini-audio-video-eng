# mini-audio-codec — Audio Codec Fundamentals

Perceptual audio coding library implementing PCM, ADPCM, MDCT, psychoacoustic modeling, subband filtering, entropy coding, LPC analysis, and bit allocation — the core building blocks of MP3, AAC, and speech codecs.

## Module Status: COMPLETE ✅

- **L1 Definitions**: Complete (15 struct/enum/typedef)
- **L2 Core Concepts**: Complete (15 core concepts implemented)
- **L3 Math Structures**: Complete (11 mathematical structures)
- **L4 Fundamental Laws**: Complete (10 theorems verified in C + Lean)
- **L5 Algorithms**: Complete (24 algorithms)
- **L6 Canonical Problems**: Complete (3 end-to-end examples)
- **L7 Applications**: Partial+ (2: VoIP/G.711, streaming audio)
- **L8 Advanced Topics**: Partial+ (4: perceptual entropy, LSP, NMR, pitch)
- **L9 Research Frontiers**: Partial (documented)

## Knowledge Coverage

| Level | Name | Status | Count |
|-------|------|--------|-------|
| L1 | Definitions | Complete | 15 items |
| L2 | Core Concepts | Complete | 15 items |
| L3 | Math Structures | Complete | 11 items |
| L4 | Fundamental Laws | Complete | 10 theorems |
| L5 | Algorithms/Methods | Complete | 24 algorithms |
| L6 | Canonical Problems | Complete | 3 examples |
| L7 | Applications | Partial+ | 2 apps |
| L8 | Advanced Topics | Partial+ | 4 topics |
| L9 | Research Frontiers | Partial | 3 documented |

## Core Definitions

- **PCM Sample Format**: S16LE, S24LE, S32LE, Float32, Float64, A-law, μ-law
- **Codec Parameters**: Sample rate, bit depth, channels, frame size, bitrate
- **Audio Frame**: Frame index, samples, channels (interleaved)
- **Quantizer**: Step size Δ, full scale, min/max levels
- **MDCT State**: Block length N, twiddle factors, analysis/synthesis window
- **Bark Scale**: 0-25 Bark critical bands, center/edge frequencies
- **Huffman Codebook**: Canonical Huffman codes, symbol-to-code mapping
- **LPC State**: Order p, coefficients a[k], reflection k[i], LSP frequencies

## Core Theorems

| Theorem | Formula | Verification |
|---------|---------|-------------|
| Quantization Noise Power | E[e²] = Δ²/12 | `quant_noise_power()` |
| SQNR | 6.02 × B + 1.76 dB | `compute_sqnr_db()` |
| Princen-Bradley PR | w[n]² + w[n+N/2]² = 1 | `verify_pr_condition()` |
| Shannon Entropy | H = -Σ pᵢ log₂(pᵢ) | `shannon_entropy()` |
| Kraft Inequality | Σ 2^{-lᵢ} ≤ 1 | `kraft_sum()` |
| Source Coding Theorem | H ≤ L_avg < H+1 | Huffman efficiency |
| Rate-Distortion | R(D) = Σ ½log₂(σ²ₖ/Dₖ) | `rate_distortion_bound()` |
| ATH Curve (Terhardt) | ATH(f) dB SPL | `ath_spl_db()` |

## Core Algorithms

- **μ-law / A-law companding**: ITU-T G.711 ± 16-bit ↔ 8-bit
- **IMA ADPCM**: 4:1 compression, adaptive step size
- **MDCT/IMDCT**: Via folding + DCT-IV kernel, FFT-based optimization
- **Window design**: Sine, KBD (Kaiser-Bessel), Vorbis, Low-overlap
- **Huffman coding**: Min-heap tree construction, canonical code generation
- **Rice coding**: Unary quotient + binary remainder, optimal k estimation
- **Levinson-Durbin**: O(p²) solution to Yule-Walker Toeplitz system
- **LPC-to-LSP**: Chebyshev bisection root-finding on unit circle
- **Water-filling**: Information-theoretic optimal bit allocation
- **Two-loop iterative**: MP3-style inner/outer rate-distortion loop
- **Psychoacoustic model**: FFT → Bark grouping → SPL → spreading → SMR
- **Pitch detection**: Autocorrelation peak + parabolic interpolation
- **PQMF filter bank**: MPEG-1 32-band polyphase analysis/synthesis

## Canonical Problems (Examples)

1. **WAV File Codec** (`examples/example_wav_codec.c`)
   - CD-quality stereo WAV create/read
   - μ-law companding demonstration
   - SQNR vs bit depth analysis

2. **MDCT Transform Demo** (`examples/example_mdct_demo.c`)
   - MDCT/IMDCT + TDAC roundtrip
   - Window function design and PR verification
   - Frequency resolution comparison

3. **ADPCM Codec** (`examples/example_adpcm_codec.c`)
   - IMA ADPCM encode/decode
   - Quality analysis (SNR computation)
   - Compression ratio demonstration

## Course Mapping

| School | Course | Topic Coverage |
|--------|--------|---------------|
| MIT | 6.003 Signal Processing | Sampling, quantization, transforms |
| MIT | 6.450 Digital Comm | Source coding, entropy |
| Stanford | EE102A Signal Processing | A/D conversion, compression |
| Stanford | EE359 Wireless | Subband/transform coding |
| Berkeley | EE123 DSP | Speech processing, LPC |
| ETH | 227-0427 Signal Processing | Filter banks, MDCT |
| Illinois | ECE 310 DSP | Multirate, filter banks |
| Michigan | EECS 351 DSP | Speech/audio processing |
| Georgia Tech | ECE 4270 DSP | Data compression |
| TU Munich | Signal Processing | Audio coding |
| 清华 | 信号与系统 | Sampling + quantization |
| 清华 | 通信原理 | Source coding theorem |

## Building

```bash
make clean && make
make test
```

## File Structure

```
mini-audio-codec/
├── Makefile
├── README.md
├── include/
│   ├── audio_codec.h       # PCM, companding, WAV, quantization
│   ├── mdct.h              # MDCT/IMDCT, window functions, DCT-IV
│   ├── psychoacoustic.h    # Bark scale, ATH, masking, SMR
│   ├── bitalloc.h          # Bit allocation algorithms
│   ├── subband.h           # PQMF filter bank
│   ├── entropy.h           # Huffman, Rice, bitstream I/O
│   └── lpc.h               # Linear predictive coding
├── src/
│   ├── pcm_codec.c         # PCM encode/decode, G.711, WAV I/O
│   ├── adpcm_codec.c       # IMA ADPCM
│   ├── mdct.c              # MDCT/IMDCT + window design
│   ├── psychoacoustic.c    # Psychoacoustic analysis pipeline
│   ├── bitalloc.c          # Water-fill, two-loop, greedy allocation
│   ├── subband.c           # PQMF analysis/synthesis + filter design
│   ├── entropy.c           # Huffman tree, Rice, bitstream
│   ├── lpc_analysis.c      # Levinson-Durbin, LSP, pitch detection
│   └── audio_codec.lean    # Lean 4 formal definitions/theorems
├── tests/
│   ├── test_pcm.c
│   ├── test_mdct.c
│   ├── test_psychoacoustic.c
│   ├── test_bitalloc.c
│   ├── test_entropy.c
│   └── test_lpc.c
├── examples/
│   ├── example_wav_codec.c
│   ├── example_mdct_demo.c
│   └── example_adpcm_codec.c
└── docs/
    ├── knowledge-graph.md
    ├── coverage-report.md
    ├── gap-report.md
    ├── course-alignment.md
    └── course-tree.md
```

## References

- Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012)
- Bosi & Goldberg, "Introduction to Digital Audio Coding and Standards" (2003)
- ISO/IEC 11172-3 — MPEG-1 Audio (Psychoacoustic Model, PQMF)
- ITU-T G.711 — Pulse Code Modulation companding
- Zwicker & Fastl, "Psychoacoustics: Facts and Models" (2007)
- Cover & Thomas, "Elements of Information Theory" (2006)
- Makhoul, "Linear Prediction: A Tutorial Review", Proc. IEEE (1975)
- Vaidyanathan, "Multirate Systems and Filter Banks" (1993)

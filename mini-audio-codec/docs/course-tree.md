# Course Tree — mini-audio-codec

## Prerequisites

```
Signals & Systems (MIT 6.003)
├── Fourier Transform → MDCT, DCT-IV
├── Sampling Theorem → PCM, sample rate conversion
└── Quantization → SQNR, noise shaping

Digital Signal Processing (Berkeley EE123)
├── FIR Filter Design → Prototype filters, window design
├── Multirate DSP → Subband filter banks, polyphase
└── Spectral Analysis → FFT-based psychoacoustic model

Information Theory (MIT 6.450)
├── Entropy → Shannon entropy, Kraft inequality
├── Source Coding Theorem → Huffman, Rice/Golomb
└── Rate-Distortion Theory → Bit allocation, perceptual entropy

Speech Processing
├── LPC Analysis → Levinson-Durbin, autocorrelation
├── Vocal Tract Model → LPC synthesis filter
└── LSP/LSF → LPC-to-LSP conversion
```

## Dependency Graph (top to bottom)

```
Level 9: Research Frontiers (6G, AI codecs)
    ↓
Level 8: Advanced Topics
    Perceptual Entropy, LSP, NMR Transparency
    ↓
Level 7: Applications
    VoIP (G.711), Streaming Audio
    ↓
Level 6: Canonical Problems
    WAV Codec, MDCT Demo, ADPCM Codec
    ↓
Level 5: Algorithms
    Huffman, Rice, Levinson-Durbin, Water-fill, Psychoacoustic
    ↓
Level 4: Fundamental Laws
    SQNR, PR Condition, ATH, Rate-Distortion, Kraft
    ↓
Level 3: Math Structures
    DCT-IV, Bark, ERB, Autocorr, Toeplitz, Polyphase
    ↓
Level 2: Core Concepts
    PCM, μ-law, A-law, MDCT, TDAC, Bitstream, Subband
    ↓
Level 1: Definitions
    Codec params, Frame, Quantizer, Huffman entry, LPC state
```

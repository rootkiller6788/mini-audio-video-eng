# Course Alignment — mini-audio-codec

| School | Course | Topic | Our Implementation |
|--------|--------|-------|-------------------|
| **MIT** | 6.003 Signal Processing | Sampling, quantization, transforms | PCM codec, MDCT, SQNR |
| **MIT** | 6.450 Digital Comm | Source coding, entropy, Huffman | Entropy coding, Huffman tree |
| **Stanford** | EE102A Signal Processing | A/D conversion, compression | ADPCM, companding |
| **Stanford** | EE359 Wireless | OFDM (analogy to subband coding) | Subband filter bank |
| **Berkeley** | EE123 DSP | Speech processing, LPC | LPC analysis, Levinson-Durbin |
| **ETH** | 227-0427 Signal Processing | Filter banks, transforms | PQMF, MDCT, window design |
| **Illinois** | ECE 310 DSP | Multirate, filter banks | Subband codec, polyphase |
| **Michigan** | EECS 351 DSP | Speech/audio processing | LSP, pitch detection |
| **Georgia Tech** | ECE 4270 DSP | Data compression | Huffman, Rice coding |
| **TU Munich** | Signal Processing | Audio coding | Psychoacoustic model |
| **清华** | 信号与系统 | Sampling + quantization | SQNR, quantizer design |
| **清华** | 通信原理 | Source coding theorem | Entropy, Kraft inequality |

## Key Reference Texts

1. Poynton, "Digital Video and HD: Algorithms and Interfaces" (2012) — PCM/WAV
2. Bosi & Goldberg, "Introduction to Digital Audio Coding and Standards" (2003) — MDCT, psychoacoustics
3. ISO/IEC 11172-3 (MPEG-1 Audio) — Psychoacoustic Model, PQMF
4. ITU-T G.711 (1988) — μ-law/A-law companding
5. Zwicker & Fastl, "Psychoacoustics: Facts and Models" (2007) — Bark, masking
6. Cover & Thomas, "Elements of Information Theory" (2006) — Entropy, rate-distortion
7. Makhoul, "Linear Prediction: A Tutorial Review" (1975) — LPC
8. Vaidyanathan, "Multirate Systems and Filter Banks" (1993) — Subband/PQMF

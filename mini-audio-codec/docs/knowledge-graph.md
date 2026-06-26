# Knowledge Graph — mini-audio-codec

## L1: Definitions (Complete)

| # | Definition | C Implementation | Lean Definition |
|---|-----------|-----------------|-----------------|
| 1 | PCM sample format (S16LE, S32LE, Float32, etc.) | `pcm_format_t` enum | `PCMSample` structure |
| 2 | Audio codec parameters (sample rate, bit depth, channels) | `codec_params_t` struct | `PCMConfig` structure |
| 3 | Audio frame (samples + metadata) | `audio_frame_t` struct | — |
| 4 | Audio stream descriptor | `audio_stream_t` struct | — |
| 5 | Uniform quantizer (step size, levels) | `quantizer_t` struct | `quantizationStep` |
| 6 | MDCT block length and window type | `mdct_state_t`, `window_type_t` | `mdctBlockLenValid` |
| 7 | Critical bands (Bark scale) | `bark_scale_t` struct | `barkScaleRange` |
| 8 | Psychoacoustic state (SPL, ATH, SMR, mask) | `psychoacoustic_state_t` | — |
| 9 | Bit allocation per band | `band_allocation_t`, `bitalloc_state_t` | — |
| 10 | Huffman codebook entry and tree node | `huffman_entry_t`, `huffman_codebook_t` | — |
| 11 | Bitstream reader/writer | `bitstream_writer_t`, `bitstream_reader_t` | — |
| 12 | LPC coefficients, reflection coeffs, LSP | `lpc_state_t` struct | `LPCFilter` structure |
| 13 | Subband filter bank state | `subband_filterbank_t` struct | `subbandCriticalSampling` |
| 14 | Tonality classification | `tonality_t` enum | — |
| 15 | MPEG layer and frame size | — | `MPEGLayer` inductive type |

## L2: Core Concepts (Complete)

| # | Concept | Implementation |
|---|---------|---------------|
| 1 | PCM encoding/decoding | `pcm_codec.c` — codec_params_init, audio_frame_alloc |
| 2 | μ-law companding (G.711) | `pcm_codec.c` — linear_to_ulaw, ulaw_to_linear |
| 3 | A-law companding (G.711) | `pcm_codec.c` — linear_to_alaw, alaw_to_linear |
| 4 | WAV file container I/O | `pcm_codec.c` — wav_write, wav_read |
| 5 | MDCT forward/inverse transform | `mdct.c` — mdct_forward, mdct_backward |
| 6 | Time-domain aliasing cancellation (TDAC) | `mdct.c` — overlap_add |
| 7 | Psychoacoustic analysis pipeline | `psychoacoustic.c` — psychoacoustic_analyze |
| 8 | Bit allocation core API | `bitalloc.c` — bitalloc_init, bitalloc_set_smr |
| 9 | Subband analysis/synthesis filtering | `subband.c` — subband_analysis_process, synthesis |
| 10 | LPC analysis and synthesis | `lpc_analysis.c` — lpc_autocorr, lpc_synthesis_filter |
| 11 | Huffman encoding/decoding | `entropy.c` — huffman_encode_symbol, decode |
| 12 | Rice/Golomb coding | `entropy.c` — rice_encode, rice_decode |
| 13 | ADPCM encoding/decoding (IMA) | `adpcm_codec.c` — ima_adpcm_encode, decode |
| 14 | Bitstream read/write primitives | `entropy.c` — bitstream_write_bits, read_bits |
| 15 | Sample rate conversion factors | `pcm_codec.c` — compute_src_factors |

## L3: Mathematical Structures (Complete)

| # | Structure | Implementation |
|---|-----------|---------------|
| 1 | DCT-IV kernel (MDCT foundation) | `mdct.c` — dct4_direct, idct4_direct |
| 2 | Bark scale conversion (Zwicker formula) | `psychoacoustic.c` — hz_to_bark, bark_to_hz |
| 3 | ERB scale (Glasberg & Moore) | `psychoacoustic.c` — erb_bandwidth |
| 4 | Shannon entropy H = -Σ pᵢ log₂(pᵢ) | `entropy.c` — shannon_entropy |
| 5 | Kraft inequality Σ 2^{-lᵢ} ≤ 1 | `entropy.c` — kraft_sum |
| 6 | Yule-Walker Toeplitz system | `lpc_analysis.c` — lpc_levinson_durbin |
| 7 | Polyphase decomposition | `subband.c` — polyphase_decompose |
| 8 | Modulation matrix for cosine-modulated FB | `subband.c` — compute_modulation_matrix |
| 9 | Autocorrelation R[k] = E[x[n]x[n+k]] | `lpc_analysis.c` — lpc_autocorr |
| 10 | Spectral Flatness Measure (SFM) | `psychoacoustic.c` — estimate_tonality |
| 11 | Spreading function S(dz) in Bark | `psychoacoustic.c` — spreading_function |

## L4: Fundamental Laws (Complete)

| # | Law/Theorem | C Verification | Lean Formalization |
|---|------------|---------------|-------------------|
| 1 | Quantization noise: E[e²] = Δ²/12 | `quant_noise_power()` | `quantizationNoisePower` |
| 2 | SQNR ≈ 6.02 × B + 1.76 dB | `compute_sqnr_db()` tests | `sqnrApprox`, `sqnr_monotonic_in_bits` |
| 3 | Princen-Bradley PR condition | `verify_pr_condition()` tests | `princenBradleyCondition` |
| 4 | Nyquist-Shannon sampling theorem | `compute_src_factors()` | — |
| 5 | Rate-distortion bound R(D) | `rate_distortion_bound()` | `rate_distortion_zero_when_distortion_equals_variance` |
| 6 | ATH curve (Terhardt) | `ath_spl_db()` tests | — |
| 7 | Shannon source coding theorem | Huffman efficiency computation | `entropy_nonnegative_simple`, `kraft_equality_two_symbol` |
| 8 | LPC stability (Schur-Cohn) | `lpc_is_stable()` | `lpcStablePARCOR` |
| 9 | Perfect reconstruction for PQMF | `verify_pr_subband()` | `subbandCriticalSampling` |
| 10 | MPEG frame structure | `mpegFrameSize` function | `mpeg_frame_size_positive` |

## L5: Algorithms/Methods (Complete)

| # | Algorithm | Implementation |
|---|-----------|---------------|
| 1 | μ-law/A-law companding (G.711) | `pcm_codec.c` |
| 2 | IMA ADPCM encode/decode | `adpcm_codec.c` |
| 3 | MDCT/IMDCT via folding + DCT-IV | `mdct.c` — mdct_forward, mdct_backward |
| 4 | DCT-IV direct computation | `mdct.c` — dct4_direct |
| 5 | Sine window design | `mdct.c` — window_sine |
| 6 | KBD window design (Kaiser-Bessel) | `mdct.c` — window_kbd |
| 7 | Vorbis window design | `mdct.c` — window_vorbis |
| 8 | Huffman tree construction (min-heap) | `entropy.c` — huffman_build_tree |
| 9 | Canonical Huffman code generation | `entropy.c` — huffman_generate_canonical |
| 10 | Rice/Golomb coding | `entropy.c` — rice_encode, rice_decode |
| 11 | Optimal Rice parameter selection | `entropy.c` — rice_optimal_k |
| 12 | Levinson-Durbin recursion | `lpc_analysis.c` — lpc_levinson_durbin |
| 13 | LPC-to-LSP conversion (bisection) | `lpc_analysis.c` — lpc_to_lsp |
| 14 | LSP-to-LPC conversion | `lpc_analysis.c` — lsp_to_lpc |
| 15 | Water-filling bit allocation | `bitalloc.c` — bitalloc_waterfill |
| 16 | Two-loop iterative bit allocation (MP3) | `bitalloc.c` — bitalloc_two_loop |
| 17 | Greedy bit allocation | `bitalloc.c` — bitalloc_greedy |
| 18 | Constant-NMR bit allocation | `bitalloc.c` — bitalloc_constant_nmr |
| 19 | Psychoacoustic model (FFT→SPL→mask→SMR) | `psychoacoustic.c` — psychoacoustic_analyze |
| 20 | Pitch detection via autocorrelation | `lpc_analysis.c` — pitch_detect_autocorr |
| 21 | PQMF analysis/synthesis processing | `subband.c` |
| 22 | MPEG-1 prototype filter design (Kaiser) | `subband.c` — mpeg1_prototype_filter |
| 23 | Kernel prototype filter design | `subband.c` — design_kernel_prototype |
| 24 | Tonality estimation (SFM) | `psychoacoustic.c` — estimate_tonality |

## L6: Canonical Problems (Complete)

| # | Problem | Implementation |
|---|---------|---------------|
| 1 | CD-quality WAV file create/read | `examples/example_wav_codec.c` |
| 2 | MDCT-based transform coding with TDAC | `examples/example_mdct_demo.c` |
| 3 | IMA ADPCM speech waveform coding | `examples/example_adpcm_codec.c` |

## L7: Applications (Partial+)

| # | Application | Relevance |
|---|-------------|-----------|
| 1 | VoIP audio compression (G.711 μ-law/A-law) | ITU-T G.711 companding |
| 2 | Streaming audio (perceptual coding) | Psychoacoustic model + bit allocation |

## L8: Advanced Topics (Partial+)

| # | Topic | Implementation |
|---|-------|---------------|
| 1 | Perceptual entropy (Johnston, 1988) | `psychoacoustic.c` — perceptual_entropy_bits |
| 2 | LSP-based speech coding | `lpc_analysis.c` — lpc_to_lsp, lsp_to_lpc |
| 3 | Noise-to-Mask ratio transparency check | `bitalloc.c` — bitalloc_is_transparent |
| 4 | Pitch detection for speech codecs | `lpc_analysis.c` — pitch_detect_autocorr |

## L9: Research Frontiers (Partial)

| # | Topic | Documentation |
|---|-------|--------------|
| 1 | Perceptual audio coding evolution (MP3→AAC→Opus) | README references |
| 2 | AI-based audio coding (neural vocoders) | Mentioned in course-tree |
| 3 | Semantic audio communication | Future direction |

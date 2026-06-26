# Gap Report — mini-video-codec

## Current Gaps

### L7: Applications
| Gap | Priority | Description |
|-----|----------|-------------|
| Standalone Application Demo | Medium | No standalone application that reads/writes actual video files (e.g., raw YUV, MP4 container) |
| Real-Time Encoding Benchmark | Low | No performance benchmarking of encoding speed vs. quality tradeoffs |

### L8: Advanced Topics
| Gap | Priority | Description |
|-----|----------|-------------|
| CABAC Full Implementation | High | Only binarization is implemented; full CABAC with context modeling and arithmetic coding not included |
| HEVC Transform (16x16, 32x32) | Medium | DCT engine dispatch exists but 16x16/32x32 use FP fallback |
| Scalable Video Coding (SVC) | Low | Temporal/spatial/quality scalability not implemented |
| Multi-View Coding (MVC) | Low | Not implemented |

### L9: Research Frontiers
| Gap | Priority | Description |
|-----|----------|-------------|
| VVC/H.266 Features | Low | Transform block partitioning (QTBT), affine motion, ALF not implemented |
| Neural Video Compression | Low | No ML-based approaches implemented |
| Point Cloud Compression | Low | Not in scope for this module |

## Resolved Gaps (from initial empty state)

All L1-L6 gaps have been resolved:
- L1: All 17 definitions created
- L2: All 16 concepts implemented
- L3: All 8 mathematical structures coded
- L4: All 10 theorems verified (C + Lean)
- L5: All 24 algorithms implemented
- L6: All 3 examples built

## Gap Resolution Plan

1. **L7 Application Module**: Add `src/application_streaming.c` with a simple H.264 baseline encoding loop that writes Annex B byte stream
2. **L8 CABAC**: Implement context initialization, arithmetic encode/decode, and context update for `mb_type`, `mvd`, and `coeff` syntax elements
3. **L8 HEVC Transforms**: Implement 16x16 and 32x32 integer DCT approximations

## Known Limitations (not gaps)

- No actual H.264 bitstream compliance (educational codec, not production)
- No ASM/SIMD optimizations (reference C implementation)
- No multi-threading (single-threaded by design)
- Lean proofs limited to structural properties on Nat/Int (Float proofs not feasible in core Lean 4)
- CAVLC implementation is simplified (fixed-length tokens vs. full adaptive VLC tables)

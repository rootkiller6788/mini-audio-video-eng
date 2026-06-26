# mini-av-sync — Audio-Video Synchronization

> **Module Status: COMPLETE ?**
>
> L1-L7: Complete | L8: Partial+ (4/7 advanced topics) | L9: Partial (documented)
>
> Total Score: 16/18

## Overview

Complete implementation of audio-video synchronization algorithms covering
clock recovery, skew estimation, buffer management, frame scheduling, and
lip sync detection. Implements MPEG-2 Systems (ISO/IEC 13818-1) PTS/DTS/PCR
handling, ATSC A/85 lip sync tolerances, and ITU-R BT.1359-1 relative timing.

## Nine-Layer Knowledge Coverage

| Level | Name | Status |
|-------|------|--------|
| L1 | Definitions | ? Complete — 10 core definitions |
| L2 | Core Concepts | ? Complete — 10 concepts |
| L3 | Mathematical Structures | ? Complete — 10 structures |
| L4 | Fundamental Laws | ? Complete — 8 theorems |
| L5 | Algorithms/Methods | ? Complete — 14 algorithms |
| L6 | Canonical Problems | ? Complete — 5 problems |
| L7 | Applications | ? Complete — 3 applications |
| L8 | Advanced Topics | ? Partial+ — 4 topics |
| L9 | Research Frontiers | ?? Partial — documented |

## Core Definitions (L1)

| Definition | Symbol | Implementation |
|-----------|--------|---------------|
| Presentation Time Stamp | PTS | `av_timestamp_t.pts` |
| Decode Time Stamp | DTS | `av_timestamp_t.dts` |
| System Time Clock | STC | `av_stc_t` |
| Program Clock Reference | PCR | `av_pcr_t` |
| Clock Skew | α?1 (ppm) | `av_clock_model_t.skew_ppm` |
| Clock Drift | d(skew)/dt | `av_clock_model_t.drift_ppm_per_s` |
| Jitter | σ(Δt) | `av_jitter_buffer_t.jitter_estimate_ms` |
| Lip Sync Tolerance | ±45/125ms | `AV_LIPSYNC_*_MAX_MS` |
| Sync Mode | — | `av_sync_mode_t` |
| Frame Duration | Δ_pts | `timestamp.duration` |

## Core Theorems (L4)

| # | Theorem | Formula |
|---|--------|---------|
| 1 | Affine Clock Model | T_slave = α·T_master + β |
| 2 | PLL Transfer Function | H(s) = (2ξωn·s + ωn2)/(s2 + 2ξωn·s + ωn2) |
| 3 | Lip Sync Criterion | `\|diff_ms\| ≤ 45ms (early) ∨ `\|diff_ms\| ≤ 125ms (late)` |
| 4 | DTS ≤ PTS | DTS_i ≤ PTS_i ? frames |
| 5 | PTS Monotonicity | PTS_i+1 = PTS_i + frame_dur_i |
| 6 | PLL DC Gain | H(0) = 1.0 |
| 7 | PI Zero Steady-State Error | e_ss = 0 for constant disturbance |
| 8 | Nyquist Clock Sampling | f_sample ≥ 2·f_max(drift) |

## Core Algorithms (L5)

| # | Algorithm | Complexity |
|---|----------|-----------|
| 1 | Second-order PLL | O(1) |
| 2 | Welford linear regression | O(1) |
| 3 | EWMA filter | O(1) |
| 4 | LMS adaptive tracking | O(1) |
| 5 | Kalman filter (2-state) | O(1) |
| 6 | Theil-Sen robust regression | O(N2·log N) |
| 7 | Allan variance | O(N) |
| 8 | PCR recovery (MPEG-2) | O(1) |
| 9 | SPSC ring buffer | O(1) |
| 10 | Adaptive jitter buffer | O(1) |
| 11 | Watermark flow control | O(1) |
| 12 | EDF frame scheduling | O(N) |
| 13 | B-frame reordering | O(N) |
| 14 | 33-bit PTS unwrap | O(1) |

## Canonical Problems (L6)

1. **Audio Master / Video Slave Sync** — `example_basic_sync.c`
2. **PCR Clock Recovery from MPEG-TS** — `example_clock_recovery.c`
3. **Streaming A/V Sync over IP** — `example_stream_sync.c`
4. **Variable Frame Rate Sync** — `av_ts_stats_t` + discontinuity detection
5. **Multi-Method Skew Estimation** — 5 estimation methods in `av_skew.c`

## Nine-School Course Mapping

| School | Key Course | Topics Covered |
|--------|-----------|---------------|
| MIT | 6.003 · 6.450 | PLL, feedback, digital communication sync |
| Stanford | EE102A · EE359 | Adaptive filtering, wireless sync |
| Berkeley | EE16A/B · EE123 | DSP, adaptive filters, feedback |
| Illinois | ECE 310 · ECE 459 | DSP, communication sync |
| Michigan | EECS 351 · EECS 455 | DSP, comm, automotive radar |
| Georgia Tech | ECE 4270 · ECE 6601 | Real-time DSP, sync theory |
| TU Munich | Signal Processing · Comm | DVB/MPEG systems, broadcasting |
| ETH Zurich | 227-0427 · 227-0436 | Estimation theory, sync |
| 清华 | 信号与系统 · 通信原理 | 锁相环, 时钟恢复, 帧同步 |

## Building

```bash
make          # Build library and all examples
make test     # Build and run test suite
make clean    # Clean build artifacts
```

## Files

```
mini-av-sync/
├── Makefile
├── README.md                    ← This file
├── include/
│   ├── av_sync_core.h          ← PTS/DTS/STC/PCR, sync state, API
│   ├── av_clock.h              ← PLL, EWMA, linreg, LMS, Allan var
│   ├── av_buffer.h             ← Ring buffer, jitter buffer, watermark
│   ├── av_timestamp.h          ← Timestamp conversion, reorder, stats
│   ├── av_skew.h               ← Skew estimation, Kalman, Theil-Sen
│   └── av_scheduler.h          ← EDF scheduler, audio-master pipeline
├── src/
│   ├── av_sync_core.c          ← Core sync, PI controller, PTS unwrap
│   ├── av_clock.c              ← PLL, EWMA, linreg, LMS, Allan, PCR
│   ├── av_buffer.c             ← Ring buffer, jitter buffer, watermark
│   ├── av_timestamp.c          ← Timestamp ops, reorder, stats
│   ├── av_skew.c               ← Skew methods, Kalman, Theil-Sen
│   ├── av_scheduler.c          ← EDF scheduling, audio-master sync
│   └── av_sync.lean            ← Lean 4 formalization (12 theorems)
├── tests/
│   └── test_av_sync.c          ← 50+ assert-based tests
├── examples/
│   ├── example_basic_sync.c    ← Audio-master video-slave simulation
│   ├── example_clock_recovery.c ← PCR recovery with jitter
│   └── example_stream_sync.c   ← Streaming A/V sync over IP
├── demos/
├── benches/
└── docs/
    ├── knowledge-graph.md      ← L1-L9 knowledge map
    ├── coverage-report.md      ← Per-level coverage assessment
    ├── gap-report.md           ← Missing topics and priorities
    ├── course-alignment.md     ← Nine-school course mapping
    └── course-tree.md          ← Prerequisite dependency tree
```

## Standards Compliance

- ISO/IEC 13818-1 (MPEG-2 Systems) — PTS/DTS/PCR definitions
- ATSC A/85:2013 — Lip sync tolerance (±45ms early, ±125ms late)
- ITU-R BT.1359-1 — Relative timing of sound and vision
- EBU R37 — Audio-video sync strategies for broadcasting
- RFC 3550 (RTP) — Jitter computation
- RFC 5905 (NTPv4) — Clock discipline algorithm
- IEEE Std 1139-2008 — Allan variance

## License

MIT — Mini Electronic Info educational module

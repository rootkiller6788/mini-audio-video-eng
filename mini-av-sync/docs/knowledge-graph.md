# Knowledge Graph ¡ª mini-av-sync

## L1: Definitions

| # | Definition | C Implementation | Lean 4 Formalization |
|---|-----------|-----------------|---------------------|
| 1 | PTS (Presentation Time Stamp) | `av_timestamp_t.pts` in `av_sync_core.h` | `Timestamp` in `av_sync.lean` |
| 2 | DTS (Decode Time Stamp) | `av_timestamp_t.dts` in `av_sync_core.h` | Theorem `dts_le_pts` |
| 3 | STC (System Time Clock) | `av_stc_t` in `av_sync_core.h` | `STC` structure |
| 4 | PCR (Program Clock Reference) | `av_pcr_t` in `av_sync_core.h` | `PCR` structure |
| 5 | Clock Drift (frequency deviation) | `drift_ppm` field | `ClockSkew` structure |
| 6 | Clock Skew (rate ratio) | `skew_estimate` in `av_sync_state_t` | `AffineClockModel` |
| 7 | Sync Mode (audio/video master) | `av_sync_mode_t` enum | `SyncMode` inductive |
| 8 | Lip Sync Tolerance (ATSC A/85) | `AV_LIPSYNC_*` macros | `LipSyncBounds` structure |
| 9 | Jitter (inter-arrival variance) | `av_ewma_filter_t.variance` | ¡ª |
| 10 | Frame Duration | `duration` field in `av_timestamp_t` | ¡ª |

## L2: Core Concepts

| # | Concept | Implementation |
|---|---------|---------------|
| 1 | Timestamp-based sync | `av_sync_compute_error()` in `av_sync_core.c` |
| 2 | Master-slave clock architecture | `av_sync_state_t` with master/slave clocks |
| 3 | PTS wrap-around handling | `av_sync_unwrap_pts()`, `av_ts_unwrap_33bit()` |
| 4 | Lip sync detection | `av_sync_check_lipsync()` |
| 5 | B-frame reorder buffer | `av_reorder_buffer_t` in `av_timestamp.h` |
| 6 | Buffer flow control (watermark) | `av_watermark_ctrl_t` in `av_buffer.h` |
| 7 | Frame drop/repeat strategy | `av_scheduler_decide()` in `av_scheduler.c` |
| 8 | Clock recovery from PCR | `av_pcr_recover()` in `av_clock.c` |
| 9 | PTS-DTS relationship | `av_ts_pts_dts_gap()` in `av_timestamp.c` |
| 10 | Timestamp discontinuity detection | `av_ts_detect_discontinuity()` |

## L3: Mathematical Structures

| # | Structure | Implementation |
|---|----------|---------------|
| 1 | Affine clock model (T_s = ¦Á¡¤T_m + ¦Â) | `av_clock_model_t` in `av_clock.h` |
| 2 | PLL transfer function (2nd order) | `av_pll_params_t` with `av_pll_update()` |
| 3 | EWMA filter (y[n] = ¦Á¡¤x + (1-¦Á)¡¤y[n-1]) | `av_ewma_filter_t` with `av_ewma_update()` |
| 4 | Linear regression (least squares) | `av_linreg_t` with Welford's algorithm |
| 5 | LMS adaptive filter | `av_lms_clock_t` with gradient descent |
| 6 | Kalman filter (2-state clock model) | `av_kalman_clock_t` with predict/update |
| 7 | Allan variance (clock stability) | `av_allan_var_t` with `av_allan_var_compute()` |
| 8 | Theil-Sen robust regression | `av_theil_sen_t` with median slope |
| 9 | PI controller (anti-windup) | `av_sync_apply_correction()` integrator |
| 10 | EDF (Earliest Deadline First) | `av_scheduler_t` with deadline-ordered slots |

## L4: Fundamental Laws

| # | Law / Theorem | Verification |
|---|--------------|-------------|
| 1 | Affine clock model: T_slave(t2) - T_slave(t1) = ¦Á¡¤(t2 - t1) | `clockConvert_diff` theorem in Lean; tested in `test_mathematical_laws()` |
| 2 | PLL DC gain = 1.0 (unity at steady state) | `pll_dc_gain_unity` theorem in Lean; tested in `test_pll()` |
| 3 | Lip sync basis: |audio_late| ¡Ü 125ms, |audio_early| ¡Ü 45ms | `isLipSyncWithinBounds` and `detectable_implies_within_bounds` in Lean |
| 4 | DTS ¡Ü PTS (decode before present constraint) | `dts_le_pts` theorem in Lean |
| 5 | PTS monotonicity: PTS[i+1] = PTS[i] + frame_dur[i] | `pts_monotonic` theorem in Lean |
| 6 | Shannon-Nyquist: f_sample ¡Ý 2¡¤f_max for clock recovery | `clock_sampling_rate` in Lean |
| 7 | PI controller: zero steady-state error for constant disturbance | Verified in `test_mathematical_laws()` |
| 8 | Kalman: innovation sequence is zero-mean for correct model | Verified in `test_mathematical_laws()` |

## L5: Algorithms / Methods

| # | Algorithm | File | Complexity |
|---|----------|------|-----------|
| 1 | Second-order PLL clock recovery | `av_clock.c:av_pll_update()` | O(1) |
| 2 | Incremental linear regression (Welford) | `av_clock.c:av_linreg_add_sample()` | O(1) |
| 3 | EWMA jitter filtering | `av_clock.c:av_ewma_update()` | O(1) |
| 4 | LMS adaptive clock tracking | `av_clock.c:av_lms_clock_update()` | O(1) |
| 5 | Allan variance computation | `av_clock.c:av_allan_var_compute()` | O(N) |
| 6 | PCR recovery (MPEG-2 Annex D) | `av_clock.c:av_pcr_recover()` | O(1) |
| 7 | Theil-Sen robust slope estimation | `av_skew.c:av_theil_sen_add()` | O(N2¡¤log N) |
| 8 | Kalman filter predict+update | `av_skew.c:av_kalman_clock_update()` | O(1) |
| 9 | Ring buffer (SPSC lock-free) | `av_buffer.c:av_ring_push/pop()` | O(1) |
| 10 | Jitter buffer with adaptive sizing | `av_buffer.c:av_jitter_push/pop()` | O(1) |
| 11 | Watermark flow control with hysteresis | `av_buffer.c:av_watermark_evaluate()` | O(1) |
| 12 | EDF frame scheduling | `av_scheduler.c:av_scheduler_push/pop()` | O(N) |
| 13 | B-frame display reordering | `av_timestamp.c:av_reorder_insert()` | O(N) |
| 14 | 33-bit PTS unwrap | `av_timestamp.c:av_ts_unwrap_33bit()` | O(1) |

## L6: Canonical Problems

| # | Problem | Solution |
|---|--------|---------|
| 1 | Audio master / video slave sync | `av_am_sync_*()` pipeline in `av_scheduler.c`; `example_basic_sync.c` |
| 2 | PCR recovery from MPEG-TS | `av_pcr_recover()` + `av_stc_interpolate()`; `example_clock_recovery.c` |
| 3 | Streaming A/V sync over IP network | `example_stream_sync.c` with jitter buffer + Kalman + scheduler |
| 4 | Sync with variable frame rate (VFR) | `av_ts_stats_t` + discontinuity detection |
| 5 | Multi-method clock skew estimation | `av_skew_add_measurement()` with 5 methods |

## L7: Applications

| # | Application | Evidence |
|---|-----------|---------|
| 1 | Broadcast TV sync (ATSC A/85 compliant) | Lip sync bounds + audio-master pipeline |
| 2 | OTT/IPTV streaming sync | Jitter buffer + EDF scheduler + Kalman tracking |
| 3 | Live event production sync | Multi-source sync with external clock mode |

## L8: Advanced Topics

| # | Topic | Implementation |
|---|------|---------------|
| 1 | Adaptive clock recovery (time-varying drift) | LMS adaptive filter in `av_lms_clock_update()` |
| 2 | Multi-device NTP-style synchronization | `av_kalman_clock_t` with 2-state tracking |
| 3 | Stochastic jitter modeling | Allan variance analysis of clock noise |
| 4 | Robust statistics (outlier-resistant) | Theil-Sen estimator |

## L9: Research Frontiers

| # | Frontier | Status |
|---|---------|--------|
| 1 | AI-based lip sync detection | Documented, not implemented |
| 2 | Cloud-based distributed synchronization | Documented, not implemented |
| 3 | Semantic-based sync (content-aware) | Documented, not implemented |

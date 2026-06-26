# Course Alignment — mini-av-sync

## Nine-School Curriculum Mapping

### MIT — 6.003 Signal Processing · 6.450 Digital Communications
| Course Topic | Module Coverage |
|-------------|----------------|
| §9: Feedback & PLL | `av_pll_params_t` — 2nd-order PLL design and analysis |
| §10: Modulation/Demodulation | Clock recovery for digital demodulation |
| §6.450 Ch.6: Synchronization | `av_sync_*` — symbol timing, frame sync, carrier recovery |
| Adaptive filters | `av_lms_clock_t` — LMS for channel estimation |

### Stanford — EE102A Signal Processing · EE359 Wireless
| Course Topic | Module Coverage |
|-------------|----------------|
| EE359 §8.4: Synchronization | `av_sync_state_t` core sync engine |
| EE359 §8.5: Clock Recovery | `av_pcr_recover()` PCR recovery + `av_pll_update()` |
| EE102A: Adaptive filtering | `av_lms_clock_t` + `av_kalman_clock_t` |
| EE359: Wireless media sync | `av_jitter_buffer_t` for wireless jitter |

### Berkeley — EE16A/B Circuits · EE123 DSP
| Course Topic | Module Coverage |
|-------------|----------------|
| EE123 §8: Adaptive Filters | LMS, Kalman implementations |
| EE123 §6: Filter Structures | EWMA, IIR (PLL loop filter) |
| EE16B: Feedback Systems | PI controller, anti-windup |
| EE123 §5: Multirate DSP | Clock rate conversion, resampling |

### Illinois — ECE 310 DSP · ECE 459 Communications
| Course Topic | Module Coverage |
|-------------|----------------|
| ECE 459 Ch.7: Synchronization | All sync algorithms |
| ECE 310: Digital Filters | EWMA, PLL discrete-time filters |
| ECE 459: MPEG Systems | PCR recovery, PTS/DTS handling |

### Michigan — EECS 351 DSP · EECS 455 Communications
| Course Topic | Module Coverage |
|-------------|----------------|
| EECS 455: Digital Comm Sync | Pi/2-DPSK, QPSK clock recovery concepts |
| EECS 351: Adaptive Signal Proc. | LMS convergence analysis |
| Automotive radar sync | `av_kalman_clock_t` for target tracking |

### Georgia Tech — ECE 4270 DSP · ECE 6601 Communications
| Course Topic | Module Coverage |
|-------------|----------------|
| ECE 6601: Synchronization theory | PLL loop bandwidth analysis |
| ECE 4270: Real-time DSP | Ring buffer SPSC for real-time constraints |
| Systems engineering | EDF scheduling optimality |

### TU Munich — Signal Processing · Communications
| Course Topic | Module Coverage |
|-------------|----------------|
| DVB/MPEG Systems | PCR recovery, T-STD buffer model |
| Digital Broadcasting | Lip sync per EBU R37 |
| High-Frequency Engineering | Clock jitter characterization |

### ETH Zurich — 227-0427 Signal Processing · 227-0436 Communications
| Course Topic | Module Coverage |
|-------------|----------------|
| 227-0436 §10: Synchronization | PLL design (ωn, ξ parameterization) |
| 227-0427: Estimation Theory | Kalman filter, linear regression, robust estimation |
| Adaptive Systems | LMS gradient descent |

### 清华大学 — 信号与系统 · 通信原理 · 数字信号处理
| Course Topic | Module Coverage |
|-------------|----------------|
| 通信原理 §6: 同步技术 | 锁相环、时钟恢复、帧同步 |
| 数字信号处理 §8: 自适应滤波 | LMS算法、Kalman滤波 |
| 信号与系统 §9: 反馈系统 | PI控制器、环路滤波器 |

## Reference Standards

| Standard | Module Use |
|----------|-----------|
| ISO/IEC 13818-1 (MPEG-2 Systems) | PTS/DTS/PCR definitions and recovery |
| ISO/IEC 14496-10 (H.264/AVC) | Frame timing, DTS/PTS for B-frames |
| ATSC A/85:2013 | Lip sync tolerance thresholds |
| ITU-R BT.1359-1 | Audio-video relative timing |
| EBU R37 | Sync strategies for broadcast |
| RFC 3550 (RTP) | Jitter computation formula |
| RFC 5905 (NTPv4) | Clock discipline principles |
| IEEE Std 1139-2008 | Allan variance definition |

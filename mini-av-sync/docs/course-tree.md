# Course Tree ¡ª mini-av-sync

## Prerequisites Dependency Tree

```
mini-av-sync
©À©¤©¤ mini-signal-system-theory (0)
©¦   ©À©¤©¤ Fourier Transform (spectral analysis of jitter)
©¦   ©À©¤©¤ Laplace Transform (PLL transfer function analysis)
©¦   ©¸©¤©¤ Convolution (FIR/IIR filter response)
©¦
©À©¤©¤ mini-communication-principle (5)
©¦   ©À©¤©¤ Digital modulation (symbol timing recovery)
©¦   ©À©¤©¤ Channel coding (error effects on sync)
©¦   ©¸©¤©¤ Synchronization theory (precursor to A/V sync)
©¦
©À©¤©¤ mini-digital-signal-process (6)
©¦   ©À©¤©¤ FIR/IIR filter design (PLL loop filter, EWMA)
©¦   ©À©¤©¤ Adaptive filters (LMS, Kalman)
©¦   ©À©¤©¤ Multirate DSP (sample rate conversion for speed adjustment)
©¦   ©¸©¤©¤ Statistical signal processing (Allan variance)
©¦
©À©¤©¤ mini-analog-electronics (2)
©¦   ©¸©¤©¤ PLL circuits (hardware PLL for clock generation)
©¦
©À©¤©¤ mini-control-automation (9)
©¦   ©À©¤©¤ PID control (PI controller for sync correction)
©¦   ©À©¤©¤ Feedback systems (PLL feedback loop)
©¦   ©¸©¤©¤ State estimation (Kalman filter)
©¦
©¸©¤©¤ mini-optical-fiber-comm (12) / mini-wireless-mobile-comm (11)
    ©À©¤©¤ Network jitter (jitter buffer design)
    ©¸©¤©¤ QoS (Quality of Service for media streams)
```

## Internal Dependency Graph

```
av_sync_core.h/c ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
  (PTS, DTS, STC, PCR, sync state)  ©¦
                                    ©¦
av_clock.h/c ¡û©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
  (PLL, EWMA, linreg, LMS,          ©¦
   Allan variance, PCR recovery)     ©¦
                                    ©¦
av_buffer.h/c ¡û©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
  (ring buffer, jitter buffer,       ©¦
   watermark control)                ©¦
                                    ©¦
av_timestamp.h/c ¡û©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
  (PTS conversion, reorder buffer,  ©¦
   discontinuity, statistics)        ©¦
                                    ©¦
av_skew.h/c ¡û©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
  (skew estimation, Kalman,          ©¦
   Theil-Sen, direct pair)           ©¦
                                    ©¦
av_scheduler.h/c ¡û©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©à©¤©¤©¤©¤ av_skew.h/c
  (EDF scheduling, frame drop/repeat,©¦
   audio-master sync pipeline)       ©¦
                                    ©¦
examples/ ¡û©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
tests/test_av_sync.c ¡û©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤ all headers
```

## Learning Path

1. **Start**: `av_sync_core.h` ¡ª Understand PTS, DTS, STC, PCR definitions
2. **Basic sync**: `av_sync_core.c` ¡ª Compute error, apply PI correction
3. **Clock recovery**: `av_clock.c` ¡ª PLL, linear regression, PCR recovery
4. **Buffering**: `av_buffer.c` ¡ª Ring buffer, jitter buffer, watermark
5. **Timestamps**: `av_timestamp.c` ¡ª Conversion, reordering, statistics
6. **Skew**: `av_skew.c` ¡ª Kalman, Theil-Sen, LMS tracking
7. **Scheduling**: `av_scheduler.c` ¡ª EDF, audio-master pipeline

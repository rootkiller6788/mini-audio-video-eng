# Course Tree — mini-3d-audio

## Prerequisite Dependency Graph

```
mini-3d-audio (THIS MODULE)
├── mini-signal-system-theory    [L3: Fourier, L3: convolution, L4: Nyquist]
├── mini-digital-signal-process  [L5: FFT, L5: FIR/IIR filters]
├── mini-communication-principle [L2: channel model, L4: Shannon]
├── mini-electromagnetic-wave    [L3: wave equation, L3: spherical harmonics]
├── mini-circuit-analysis        [L2: transfer functions, L3: frequency response]
├── mini-sensor-measurement      [L7: microphone arrays]
└── mini-wireless-mobile-comm    [L2: spatial diversity, L4: Friis]

Upstream Dependencies (what depends on this module):
├── mini-av-sync                 [L6: audio-video synchronization]
├── mini-streaming-protocol      [L7: spatial audio streaming]
└── mini-hdr-hfr-video           [L7: immersive media]
```

## Knowledge Dependency Table

| Knowledge Point | Prerequisite Module | Status |
|-----------------|---------------------|--------|
| DFT/FFT | mini-signal-system-theory | Required |
| FIR filter design | mini-digital-signal-process | Required |
| Frequency response | mini-circuit-analysis | Required |
| Wave equation (spherical) | mini-electromagnetic-wave | Required |
| Shannon sampling theorem | mini-communication-principle | Required |
| Microphone array theory | mini-sensor-measurement | Optional |
| Channel modeling | mini-wireless-mobile-comm | Optional |

## L9 Research Frontiers

| Topic | Description | Related Modules |
|-------|-------------|-----------------|
| MPEG-H 3D Audio | Next-gen spatial audio codec | mini-streaming-protocol |
| 6DoF Audio | Audio for 6-DoF VR experiences | mini-av-sync |
| Neural HRTF | Deep learning for HRTF synthesis | none (AI module) |
| Semantic Communication | Joint source-channel coding for audio | mini-communication-principle |

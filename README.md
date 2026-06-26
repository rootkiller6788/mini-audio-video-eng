# Mini Audio Video Engineering

A collection of **from-scratch, zero-dependency C implementations** of audio-visual signal processing and multimedia engineering fundamentals. Each module maps to MIT (and other top-tier university) courses, bridging theory and practice by translating textbook equations and industry standards into runnable C code.

## Sub-Modules

| Sub-Module | Topics | Key Courses |
|-----------|--------|-------------|
| [mini-3d-audio](mini-3d-audio/) | HRTF, binaural rendering, Ambisonics (FOA/HOA), VBAP panning, room acoustics, spherical harmonics | MIT 6.003, Stanford EE359, Berkeley EE123 |
| [mini-audio-codec](mini-audio-codec/) | PCM encoding, μ-law/A-law companding, MDCT/IMDCT, LPC, psychoacoustic models, bit allocation | MIT 6.003, Stanford EE102A, ETH 227-0427 |
| [mini-av-sync](mini-av-sync/) | AV clock recovery, lip-sync, jitter buffer, MPEG STC, skew detection/compensation, ring buffer | MIT 6.003, Stanford EE398, Berkeley EE290 |
| [mini-camera-sensor](mini-camera-sensor/) | CMOS/CCD sensor models, ISP pipeline, Bayer demosaicing, auto exposure (AE/AGC), color science, noise modeling | Stanford EE392J, Illinois ECE418, Berkeley EE225B |
| [mini-display-technology](mini-display-technology/) | HDMI/DisplayPort/MIPI DSI, EDID, TMDS 8b/10b, VESA CVT/GTF timing, scanout, frame buffer | MIT 6.450, Stanford EE359, Illinois ECE459 |
| [mini-hdr-hfr-video](mini-hdr-hfr-video/) | PQ (ST.2084) / HLG EOTFs, tone mapping, color space transforms, frame interpolation, motion estimation (HFR) | MIT 6.003, Stanford EE392J, ETH 227-0447 |
| [mini-streaming-protocol](mini-streaming-protocol/) | RTP/RTCP (RFC 3550), MPEG-2 Transport Stream, HLS/DASH adaptive streaming, jitter buffer | MIT 6.829, Stanford CS144, Berkeley EE122 |
| [mini-video-codec](mini-video-codec/) | 2D DCT/IDCT, motion estimation & compensation, entropy coding (CABAC/CAVLC), deblocking filter, intra/inter prediction | MIT 6.344, Stanford EE392J, ETH 227-0447 |

## Design Philosophy

- **Zero external dependencies** — pure C (C99/C11), only `libc` and `libm`
- **Self-contained modules** — each directory has its own `Makefile`, `include/`, `src/`, `examples/`, `demos/`, `tests/`
- **Standards-grounded** — implementations reference ITU-T, ISO/IEC, VESA, HDMI Forum, IETF RFC specifications
- **Theory-to-code mapping** — every module includes `docs/` with course-alignment notes and textbook references

## Building

Each module is standalone. Navigate to a module directory and run:

```bash
cd mini-3d-audio
make all    # build everything
make test   # run tests
```

Requires **GCC** and **GNU Make**.

## Project Structure

```
mini-audio-video-eng/
├── mini-3d-audio/              # Spatial audio: HRTF, Ambisonics, binaural rendering
├── mini-audio-codec/           # Audio coding: PCM, companding, MDCT, psychoacoustics
├── mini-av-sync/               # Lip-sync, AV clock recovery, jitter buffer
├── mini-camera-sensor/         # Image sensor modeling, ISP pipeline, demosaicing
├── mini-display-technology/    # Display interfaces, EDID, VESA timing, TMDS
├── mini-hdr-hfr-video/         # HDR EOTFs, tone mapping, high frame rate processing
├── mini-streaming-protocol/    # RTP/RTCP, MPEG-TS, HLS/DASH adaptive streaming
└── mini-video-codec/           # DCT, motion estimation, entropy coding, deblocking
```

## License

MIT

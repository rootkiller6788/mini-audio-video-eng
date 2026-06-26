# Course Tree — mini-hdr-hfr-video

## Prerequisites (Knowledge Dependencies)

```
                          ┌─────────────────┐
                          │ mini-hdr-hfr    │
                          │    video        │
                          └────────┬────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          │                        │                        │
    ┌─────▼─────┐          ┌──────▼──────┐         ┌───────▼───────┐
    │  Color    │          │   Signal    │         │  Computer     │
    │  Science  │          │  Processing │         │   Vision      │
    └─────┬─────┘          └──────┬──────┘         └───────┬───────┘
          │                       │                        │
    ┌─────▼─────┐          ┌──────▼──────┐         ┌───────▼───────┐
    │ Calculus  │          │  Fourier    │         │   Linear      │
    │  (diff)   │          │ Transforms  │         │   Algebra     │
    └───────────┘          └─────────────┘         └───────────────┘
```

## Internal Module Dependencies

```
hdr_core.h ──────────┐
   (transfer funcs,  │
    histograms,       │
    display models,   ├──> hdr_tone_mapping.h ──> tmo_apply
    Barten CSF)       │         (Reinhard, Drago,
                      │          bilateral, BT.2446)
hdr_core.h ──────────┤
        +             │
hdr_color.h ◄────────┘
   (color spaces,
    matrices, Delta E,
    gamut, chroma)

hfr_core.h ──────────┐
   (frames, buffers,  │
    interpolation,    ├──> hfr_motion.h ──> MCFI
    deinterlace,      │     (block match,
    pull-down)        │      optical flow,
                      │      phase correlation)
hdr_core.h
   (PQ for ICtCp)
```

## Learning Path
1. Start with `hdr_core.h` — understand PQ, HLG, BT.1886 transfer functions
2. Move to `hdr_color.h` — color spaces and transformations
3. Learn tone mapping via `hdr_tone_mapping.h`
4. Study HFR concepts via `hfr_core.h`
5. Advanced: motion estimation in `hfr_motion.h`

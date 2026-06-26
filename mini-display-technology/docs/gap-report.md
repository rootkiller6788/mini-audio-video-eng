# Gap Report — mini-display-technology

## Current Gaps

### L7 Applications (Missing)
| Priority | Application | Rationale |
|----------|-------------|-----------|
| Medium | Automotive HUD display | Head-Up Display with windshield projection |
| Medium | Surgical/NIR display | Medical imaging with DICOM GSDF calibration |
| Low | VR latency measurement | Motion-to-photon latency analysis |
| Low | Transparent OLED signage | Digital signage with see-through display |

### L8 Advanced Topics (Missing)
| Priority | Topic | Rationale |
|----------|-------|-----------|
| Medium | Variable Refresh Rate (VRR/FreeSync) | Adaptive sync over HDMI 2.1/DP |
| Medium | DSC (Display Stream Compression) | VESA DSC 1.2a visually lossless |
| Low | Spatial Temporal Dithering (FRC) | 6-bit+FRC → 8-bit effective |
| Low | Subpixel rendering (ClearType) | RGB/BGR striping for text |

### Technical Debt
| Issue | Severity | Location |
|-------|----------|---------|
| color_science.c uses GNU auto keyword | Compilation | Will fix with explicit lambda alternative |
| display_interface.c TMDS decoder uses simplified reverse | Low | Full decode with DC recovery not implemented |
| No GPU/LCD driver hardware integration | Medium | All pure software |


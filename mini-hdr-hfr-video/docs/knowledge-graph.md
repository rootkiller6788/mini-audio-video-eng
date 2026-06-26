# Knowledge Graph — mini-hdr-hfr-video

## L1: Definitions
- HDR: High Dynamic Range (peak luminance, black level)
- HFR: High Frame Rate (24/48/60/120/240 fps standards)
- PQ: Perceptual Quantizer (SMPTE ST 2084)
- HLG: Hybrid Log-Gamma (ITU-R BT.2100)
- EOTF: Electro-Optical Transfer Function
- OETF: Opto-Electrical Transfer Function
- OOTF: Opto-Optical Transfer Function
- BT.1886: SDR reference display EOTF
- BT.2020/BT.709/DCI-P3: Color primaries
- ICtCp: Perceptually uniform HDR color space
- Tone Mapping Operator (TMO)
- Gamut mapping
- Chroma subsampling (4:4:4, 4:2:2, 4:2:0)
- Frame interpolation
- Motion-compensated frame interpolation (MCFI)
- Pull-down / Telecine (2:3, 3:2)
- Deinterlacing (weave, bob, YADIF)
- Shutter angle / Motion blur
- Cadence detection
- CIE XYZ, L*a*b*, OKLab
- Delta E 2000
- Barten CSF
- JND (Just-Noticeable Difference)

## L2: Core Concepts
- Perceptual encoding (PQ curve based on Barten CSF)
- Scene-referred vs display-referred
- Display-adaptive HDR
- Edge-preserving smoothing (bilateral filter)
- Base/detail decomposition
- Local vs global tone mapping
- Motion estimation (block matching, optical flow)
- Occlusion handling in MCFI
- Temporal coherence preservation
- HDR10, HDR10+, Dolby Vision metadata

## L3: Mathematical Structures
- Transfer function curves (PQ, HLG, gamma)
- 3x3 color transformation matrices
- CIE chromaticity diagrams
- Luminance histograms
- Motion vector fields
- Affine and perspective transforms
- Optical flow PDEs
- Cross-power spectrum (phase correlation)

## L4: Fundamental Laws
- Weber-Fechner law (JND proportional to luminance)
- Barten CSF model
- Shannon-Hartley theorem (channel capacity for HDR)
- Nyquist-Shannon sampling theorem (frame rate)
- DeVries-Rose law (sqrt sensitivity at low light)
- Bloch's law (temporal integration)

## L5: Algorithms/Methods
- PQ EOTF/OETF computation
- HLG OETF/OETF computation
- Reinhard global tone mapping
- Drago logarithmic tone mapping
- Bilateral filtering
- Base/detail decomposition
- BT.2446 HDR->SDR conversion (Methods A & B)
- Exhaustive block matching
- Diamond search block matching
- Horn-Schunck optical flow
- Lucas-Kanade optical flow
- Phase correlation (FFT-based)
- CIEDE2000 color difference
- OKLab color space conversion
- Chroma subsampling/upsampling
- Motion vector median filtering
- Affine transform fitting

## L6: Canonical Problems
- HDR to SDR tone mapping (display adaptation)
- Frame rate upconversion (24->60, 24->120)
- Deinterlacing (interlaced to progressive)
- Inverse telecine (remove 2:3 pull-down)
- Gamut mapping between color spaces
- Motion-compensated frame interpolation
- Occlusion detection and hole filling

## L7: Applications
- Modern HDR displays (OLED, LCD with local dimming)
- Digital cinema distribution (DCI-P3)
- Broadcast TV (HLG for backward compatibility)
- Gaming (HDR rendering at 120+ fps)
- Smartphone video processing
- Streaming (Netflix, YouTube HDR)

## L8: Advanced Topics
- AI-based tone mapping (CNN auto-encoders)
- Neural frame interpolation (DAIN, RIFE)
- Perceptually uniform color spaces for HDR
- Time-varying tone mapping for video
- Real-time optical flow
- Adaptive bit-depth allocation

## L9: Research Frontiers
- Semantic-aware tone mapping
- 6G immersive video (volumetric HDR+HFR)
- Light field HDR video
- Quantum dot HDR displays
- Foveated rendering for HDR/HFR

# Gap Report — mini-hdr-hfr-video

## Missing Items by Level

### L8: Advanced Topics (4 missing)
| Priority | Topic | Description | Effort |
|----------|-------|-------------|--------|
| High | AI-based tone mapping | CNN-based TMO not implemented | Large |
| Medium | Neural frame interpolation | DAIN/RIFE not implemented | Large |
| Medium | Perceptually uniform color spaces for HDR | JzAzBz color space | Medium |
| Low | Adaptive bit-depth allocation | Rate control for HDR | Medium |

### L9: Research Frontiers (5 missing)
| Priority | Topic | Description | Effort |
|----------|-------|-------------|--------|
| Low | Semantic-aware tone mapping | Content-based TMO | Research |
| Low | 6G immersive video | Volumetric HDR+HFR | Research |
| Low | Light field HDR video | Multi-view HDR | Research |
| Low | Quantum dot HDR | Display technology | Research |
| Low | Foveated rendering | Gaze-contingent HDR | Research |

## Quality Gaps
- No SIMD optimization for block matching (but educational C code is correct)
- No GPU acceleration for optical flow
- Lean formalization uses `by trivial` placeholders for nontrivial theorems (L4 compliance)

## Remediation Plan
1. Add JzAzBz color space implementation (L8 - Medium)
2. Expand Lean proofs for transfer function properties (L4)
3. None of the L9 gaps block COMPLETE status per SKILL.md ($9.4)

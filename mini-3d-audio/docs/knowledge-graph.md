# Knowledge Graph — mini-3d-audio (Spatial Audio / 3D Audio)

## L1: Definitions (Complete)
- HRTF (Head-Related Transfer Function): `m3a_hrtf`
- HRIR (Head-Related Impulse Response): `m3a_hrir`
- ILD (Interaural Level Difference): `m3a_binaural_cues.ild_db`
- ITD (Interaural Time Difference): `m3a_binaural_cues.itd_sec`
- IPD (Interaural Phase Difference): `m3a_binaural_cues.ipd_rad`
- IACC (Interaural Cross-Correlation): `m3a_binaural_cues.iacc`
- Sound Pressure Level (SPL): `m3a_estimate_spl()`
- Ambisonics Channels (WXYZ, ACN): `m3a_amb_order`, `m3a_acn_index()`
- Spherical Harmonics: `m3a_spherical_harmonic()`
- RT60 (Reverberation Time): `m3a_room_params.rt60_sec`
- Binaural Cues: `m3a_binaural_cues`
- Head Shadow Effect: implicit in ILD computation
- Pinna Filtering: encapsulated in HRTF
- Absorption Coefficient: `m3a_material`, `m3a_material_id`
- Cartesian Coordinates: `m3a_vec3d`
- Spherical Coordinates: `m3a_spherical`
- Audio Buffer: `m3a_audio_buffer`
- Listener State: `m3a_listener`
- Sound Source: `m3a_source`
- Spatial Scene: `m3a_scene`
- Speaker / Loudspeaker: `m3a_speaker`
- VBAP Gains: `m3a_vbap_gains`
- Room Dimensions: `m3a_room`
- Reflection Path: `m3a_image_source`

## L2: Core Concepts (Complete)
- Binaural Hearing: Duplex theory (ITD for LF, ILD for HF)
- Sound Localization (azimuth, elevation, distance): sphere-to-cartesian transforms
- HRTF Measurement and Interpolation: `m3a_hrtf_interpolate_bilinear()`, `m3a_hrtf_interpolate_barycentric()`
- Ambisonics Encoding/Decoding: `m3a_amb_encode_hoa()`, `m3a_amb_decode()`
- Vector Base Amplitude Panning (VBAP): 2D (`m3a_vbap_calc_2d()`) and 3D (`m3a_vbap_calc_3d()`)
- Distance Rendering: `m3a_distance_attenuation()`
- Doppler Effect: `m3a_doppler_ratio()`, `m3a_doppler_shift()`
- Room Acoustics Modeling: image source method, FDN, Schroeder
- Directional Audio Coding: Ambisonics virtual microphone
- Scene Management: `m3a_scene_init()`, render pipeline
- Sound Field Decomposition: Ambisonics encoding

## L3: Mathematical Structures (Complete)
- Spherical Coordinates: `m3a_vec3d_from_spherical()`, `m3a_spherical_from_vec3d()`
- Vector Algebra (dot, norm, normalize, subtract): `m3a_vec3d_*` functions
- Spherical Harmonics (real-valued): `m3a_sh_eval()`, Legendre recursion
- Convolution (OLA): `m3a_ola_process()`, `m3a_ola_init()`
- FIR Filters: implicit in HRIR convolution
- Matrix Operations: `m3a_mat3`, `m3a_rotation_matrix()`, `m3a_mat3_mul_vec()`
- Barycentric Coordinates: `m3a_vbap_find_triangle()`, `m3a_hrtf_interpolate_barycentric()`
- Great-Circle Distance: `m3a_great_circle_distance_deg()`
- dB/Linear Conversion: `m3a_db_to_linear()`, `m3a_linear_to_db()`
- 3D Rotation Matrices (Euler angles): `m3a_rotation_matrix()`
- ACN Indexing: `m3a_acn_index()`
- Normalization (N3D, SN3D, FuMa): `m3a_amb_norm_factor()`
- Hadamard Matrix: `m3a_fdn_hadamard_matrix()`

## L4: Fundamental Laws (Complete)
- Duplex Theory (Rayleigh 1907): ITD for <1.5 kHz, ILD for >1.5 kHz
- Inverse Square Law: `m3a_distance_attenuation()`
- Speed of Sound (temperature-dependent): `m3a_speed_of_sound()`
- Nyquist-Shannon Sampling Theorem: implicit in sample_rate throughout
- Sabine's Reverberation Formula: `m3a_rt60_sabine()`
- Eyring's Revised Formula: `m3a_rt60_eyring()`
- Classical Doppler Effect: `m3a_doppler_ratio()`
- Air Absorption (ISO 9613-1): `m3a_air_absorption()`
- Reflection Coefficient: `m3a_reflection_coeff()`
- Critical Distance: `m3a_critical_distance()`

## L5: Algorithms/Methods (Complete)
- OLA Convolution: `m3a_ola_init()`, `m3a_ola_process()`
- HRTF Bilinear Interpolation: `m3a_hrtf_interpolate_bilinear()`
- HRTF Barycentric Interpolation: `m3a_hrtf_interpolate_barycentric()`
- Minimum-Phase Reconstruction: `m3a_hrtf_to_minimum_phase()`
- HRTF Smoothing: `m3a_hrtf_smooth_magnitude()`
- Diffuse-Field EQ: `m3a_hrtf_diffuse_field_eq()`
- ITD Extraction (cross-correlation): `m3a_hrtf_extract_itd()`
- DFT (HRIR→HRTF): `m3a_hrtf_from_hrir()`
- Spherical Harmonic Evaluation: `m3a_sh_eval()`, legendre_p()
- HOA Encoding: `m3a_amb_encode_hoa()`
- FuMa Encoding: `m3a_amb_encode_fuma()`
- Mode-Matching Decoding: `m3a_amb_decode_mode_matching()`
- Max-rE Weighting: `m3a_amb_maxre_weights()`
- NFC Filter: `m3a_amb_nfc_filter()`
- Normalization Conversion: `m3a_amb_convert_norm()`
- Virtual Microphone: `m3a_amb_virtual_mic()`
- Fractional Delay (Lagrange): `m3a_fractional_delay()`
- 2D VBAP: `m3a_vbap_calc_2d()`
- 3D VBAP: `m3a_vbap_calc_3d()`, `m3a_vbap_build_mesh()`
- DBAP: `m3a_dbap_pan()`
- Crossfade Panning: `m3a_crossfade_pan()`
- Sine Law Panning: `m3a_pan_sine_law()`
- Tangent Law Panning: `m3a_pan_tangent_law()`
- Image Source Method: `m3a_room_image_sources()`, `m3a_ism_generate()`
- FDN Reverb: `m3a_fdn_init()`, `m3a_fdn_hadamard_matrix()`
- Schroeder Reverb: `m3a_schroeder_init()`, `m3a_schroeder_process()`
- Doppler Resampling: `m3a_doppler_resample()`
- Window Functions (Hann, Hamming, Blackman): `m3a_window_*()` 
- Audio Peak/RMS: `m3a_audio_peak()`, `m3a_audio_rms()`

## L6: Canonical Problems (Complete)
- Render sound source at arbitrary azimuth/elevation: `m3a_spatialize_source()`
- HRTF-based binaural rendering: `m3a_binaural_render_mono()`
- First-order Ambisonics encode/decode: `m3a_amb_encode_fuma()`, `m3a_amb_decode_mode_matching()`
- B-format rotation: `m3a_amb_rotate_bformat()`
- VBAP for standard speaker layouts: `m3a_vbap_calc_2d()`, `m3a_vbap_calc_3d()`
- Room impulse response generation: `m3a_room_generate_early_reflections()`
- Complete spatial scene rendering: `m3a_scene_render_block()`
- Moving source with Doppler: `m3a_doppler_shift()`
- Multi-source binaural mixing: `m3a_binaural_mix_sources()`
- Binaural room synthesis: `m3a_rir_to_binaural()`
- Virtual auditory display: `m3a_binaural_renderer` + `m3a_binaural_process_block()`

## L7: Applications (Complete — 4 applications)
- VR/AR spatial audio with head tracking: `m3a_vr_head_tracked_render()`
- 3D audio for gaming: `m3a_scene_render_block()`, multi-source scene
- Immersive audio (Atmos/DTS:X): 7.1.4 speaker layout, `m3a_build_layout_714()`
- Architectural acoustics auralization: `m3a_environmental_render()`, `m3a_ism_generate()`

## L8: Advanced Topics (Partial — 3/6 topics)
- Higher-Order Ambisonics (HOA up to 3rd order): `m3a_amb_encode_hoa()`
- Near-Field Compensated HOA: `m3a_amb_nfc_filter()`
- Anthropometric HRTF Personalization: `m3a_hrtf_personalize()`
- Wave Field Synthesis: [future]
- Real-time binaural on embedded: [future]
- Binaural Ambisonics Decoder: `m3a_amb_binaural_decoder_init()`, `m3a_amb_binaural_render()`

## L9: Research Frontiers (Partial — documented)
- Perceptually motivated spatial codecs (MPEG-H 3D Audio): documented
- 6DoF audio rendering: documented
- Neural HRTF synthesis: documented
- Object-based next-gen broadcasting: documented

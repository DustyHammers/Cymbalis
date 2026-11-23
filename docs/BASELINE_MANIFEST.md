# Baseline manifest — Nova Ride (engine V19)

Imported 2026-10-13 from `Cymbalis_V8_Package_602_final.zip`. SPEAR files excluded.

## Baseline render check

Strikes (time s, velocity, position) = (1.0, 0.30, 0.5), (6.0, 0.65, 0.5), (11.0, 0.90, 0.5); 15.0 s; written with soundfile `PCM_24` at 44100 Hz.

| Articulation overlay | Expected MD5 | Result 2026-10-13 |
|---|---|---|
| `bow_tip.json` | `91ae9a6856c0f58ab4244b90cb078f73` | match |
| `bow_shoulder.json` | `b321ddb1f50137caa7a40b1a853ada3f` | match |

Checked with numpy 2.5.3, scipy 1.18.1, soundfile 0.14.0.

## File MD5s

| File | MD5 |
|---|---|
| `prototype/engine/multi_strike_engine_v19.py` | `bcd9e8d8af079b415a53d9968952a1bc` |
| `prototype/engine/spear_layer.py` | `28059a4f403eec26a590684d439366b3` |
| `prototype/engine/hertz_velocity.py` | `49082b6df9f89bcb62408c35ac454814` |
| `prototype/engine/dsp_filters.py` | `c74aa23b88a50a3011a3c824f4997921` |
| `prototype/load_preset.py` | `743cb9def145fd986c51c70cd9a1b3a7` |
| `presets/nova/Nova_Baseline19.json` | `e2025486455f50371ea0fc097482a61f` |
| `presets/nova/articulations/bell_shoulder.json` | `4c3e0ef0aa3cd4aae1a7fdd75f58ab96` |
| `presets/nova/articulations/bell_tip.json` | `bc394134bfd821417d6caf55a682825c` |
| `presets/nova/articulations/bow_shoulder.json` | `9bc8a83ba9b838c878c57c6f2c4c898b` |
| `presets/nova/articulations/bow_tip.json` | `8065132e1ea433a845552210a2a28eee` |
| `presets/nova/articulations/shoulder.json` | `13972322d914275e03cbf8f477744bec` |
| `presets/nova/articulations/tip.json` | `b75c1383e3375a6c2036bc9ed2e11e30` |
| `presets/nova/data/nova_band_envelopes.json` | `666da54b10ff37e4ac8d42759f9396cd` |
| `presets/nova/data/nova_bell_excitation_ratios_v6.json` | `76266d98eb8a534d1c11e26976e0a1e2` |
| `presets/nova/data/nova_bell_vel_participation.json` | `8cafcaddf4c607de37e60fdc1a3ab694` |
| `presets/nova/data/nova_bell_vp_shoulder_v2_corrected.json` | `ee27a67b29bc692f20373bc51b07fad1` |
| `presets/nova/data/nova_bell_vp_tip_corrected.json` | `ae1f02dde8024a3ee6c7aabdf67ae634` |
| `presets/nova/data/nova_edge_excitation_ratios_neutral.json` | `064ab60a71e0620f492d74dd2615db8d` |
| `presets/nova/data/nova_vel_participation.json` | `9e2591a23b7b48ca12dafa5c371ffcd7` |
| `presets/nova/data/nova_vel_participation_shoulder_correct.json` | `b35e81435c5fa52165c8c7758326a988` |
| `presets/nova/data/nova_vel_participation_tip_s2a_ceil.json` | `1d143acc1baf05397f436b5472f28d8e` |
| `presets/nova/data/nova_vel_tilt.json` | `54533e6c49985a21c3b01a98eccde1dd` |
| `presets/nova/data/nova_edge_npz_raven_onset.json` | `b47dea3f7a0b549ab2f7039187db2c3b` |

## Notes

- `tip.json` and `shoulder.json` reference `nova_vel_participation_tip.json` and `nova_vel_participation_shoulder.json`, which are not in the package. Those two overlays cannot be loaded.
- `nova_edge_npz_raven_onset.json` (added 2026-11-22 in the project session, Kris: "version 8"; not part of the V8 package and not read by the Python engine or `Nova_Baseline19.json`): an NPZ edge for the plugin's EDGE row. Nova has no edge captures. Its mode weights are Nova's edge today; per band and velocity its build-up follows Raven's edge, its peak level is Nova's with Raven's change from full velocity (soft hits darker), and its ring after the peak is Nova's own. Its `bake` block lists what the bake needs (band correction, 30 s window, level exponent 1.5, edge row only, no stick contact on envelope strikes). Details in its `notes`.
- The bow-shoulder baseline render peaks at 1.28 (above full scale) at velocity 0.9. This is the locked behaviour.

# Smoke Flat Ride (602) and Hammered Thin Ride (Bettis)

Imported 2026-10-14 from `Cymbalis_V8_Package_602_final.zip`, unchanged. SPEAR files excluded. No baseline render check: the plugin is the reference (see `CLAUDE.md`).

| File | MD5 |
|---|---|
| `presets/602/602_Baseline3.json` | `cc3a078daf1a6b2dc590216dd8e0a2ca` |
| `presets/602/articulations/bow.json` | `5b29827f702c63a0e43e3f2a516f48b8` |
| `presets/602/articulations/edge.json` | `1b6cb8868bf449a9ad0f070652de9379` |
| `presets/602/data/602_edge_benv_trigger.json` | `4a99feb41f6a98cdad203d9b11b40836` |
| `presets/602/data/602_edge_npz_gamma_tc.json` | `56896ca35bbbbfba029280f782fcf8a8` |
| `presets/602/data/602_fingerprint_gamma.json` | `afc60b4e433c2865b5035abba76308d0` |
| `presets/602/data/602_pkt_data.json` | `21cd5405816d1dac6819a73dd56a1977` |
| `presets/bettis/Bettis_Baseline1.json` | `a22ce1165c84293b3ef5fff17e51e684` |
| `presets/bettis/data/bettis_band_envelopes.json` | `2bde84a5187dafbd227a15291d1f2cc4` |
| `presets/bettis/data/bettis_bell_excitation_ratios.json` | `3dcc4ce04272717c36c70a0e622f035a` |
| `presets/bettis/data/bettis_bow_vel_tilt.json` | `9ac527647b348373ed21069ad139d4a5` |
| `presets/bettis/data/bettis_clusters_full.json` | `7ac72a068981f1a9c5ba07a9a92eb48b` |
| `presets/bettis/data/bettis_edge_band_envs.json` | `37df6178278b7ce00aab9ceb0b238265` |
| `presets/bettis/data/bettis_edge_excitation_ratios.json` | `c07e54305fa73fea4e6cdec88a3930cc` |
| `presets/bettis/data/bettis_edge_excitation_ratios_neutral.json` | `48094acc45dc239bd6f4d1c08807eab3` |
| `presets/bettis/data/bettis_edge_npz_data.json` | `1a423771e81a1b783cea5cc726427514` |
| `presets/bettis/data/bettis_edge_vel_tilt.json` | `331ce293c41c3ea4a4a589d4705d05ac` |
| `presets/bettis/data/bettis_exc_norm_band4.json` | `16ad9f8f26a0aa9203cc33b8f47fb892` |
| `presets/bettis/data/bettis_fingerprint.json` | `c55d620ec6fe37f169321b73f50550a8` |
| `presets/bettis/data/bettis_pkt_data.json` | `7121b09e07e3c7619d6cf576da8a36e8` |

## Notes

- 602 has bow and edge only. Its two overlays (`bow.json`, `edge.json`) are empty.
- Bettis has no articulation overlays. `Bettis_Baseline1.json` sets `nl_amount` 1.0; the plugin bakes every preset with `nl_amount` 0.
- Bettis data files not referenced by `Bettis_Baseline1.json` (`bettis_band_envelopes.json`, `bettis_bow_vel_tilt.json`, `bettis_clusters_full.json`, `bettis_edge_excitation_ratios_neutral.json`, `bettis_exc_norm_band4.json`) are kept as delivered.
- Photos: `resources/ui/SmokeFlatRide.png` (714 x 718, MD5 `ad32593c8f50eea6c56d5955f481bff6`) and `resources/ui/HammeredThinRide.png` (709 x 713, MD5 `b1b457e6b28daf4247bf2ed476ae734d`). Both were recompressed losslessly on 2026-10-24 (oxipng); their decoded pixels are those of the imported files (MD5 `f40fc5681149ac288d628909a86968be` and `11157eed92bee276283a317f973a77d3`).

# Swish (22" China, medium heavy)

Imported 2026-10-17 from `Cymbalis_V8_Package_602_final.zip`, unchanged. SPEAR files excluded. No baseline render check: the plugin is the reference (see `CLAUDE.md`).

| File | MD5 |
|---|---|
| `presets/swish/Swish_Baseline1.json` | `05424a58a7f2af49c80cf2d5906d5ed4` |
| `presets/swish/articulations/bow_shoulder.json` | `9754980a4d39c9876a5ae8e053cbdf1c` |
| `presets/swish/articulations/bow_tip.json` | `4a358134c4d7fa0fa485b95389ddbcdc` |
| `presets/swish/data/swish_bell_excitation_ratios.json` | `b14d367d4579e726295fbb961d2c430a` |
| `presets/swish/data/swish_edge_excitation_ratios_neutral.json` | `c02e2f89161eaf66aa98d2badb6ed3e6` |
| `presets/swish/data/swish_edge_npz_data.json` | `75873ff5d13ce3a35446685d79aeb041` |
| `presets/swish/data/swish_edge_vel_tilt.json` | `affb0b8d8187c0b2e19f999e9d7558a5` |
| `presets/swish/data/swish_fingerprint.json` | `2e8334cdea45b65bd81ed40e106c1066` |
| `presets/swish/data/swish_pkt_data.json` | `32bc33fbfcec5b0c1fc199f4cc758340` |

## Notes

- Python baseline: Render 15 (MD5 `314fc74156f202fbf355b4dd269cfce4`), bell ratios v11.
- Overlays: `bow_tip.json` sets `stick_gain` 6.0 (stick-click transient); `bow_shoulder.json` sets it to 0.0.
- The edge NPZ has 20 bands, 100-10000 Hz, six velocities (0.2-1.0) and one velw for all velocities.
- `Swish_Baseline1.json` sets no `edge_band_env_path`, so in Python V19 the NPZ band envelopes never run for Swish (the session summary's "_eb_band_scale not active").
- Photo: `resources/ui/Swish.png` (737 x 747, MD5 `ca66de93c2aae1d2808046f235a1661f`). Recompressed losslessly on 2026-10-24 (oxipng); its decoded pixels are those of the imported file (MD5 `e3b7230754d0a940be6c8e697cabb650`).


# Raven (20" dry ride)

Built 2026-10-26 in the project session from Kris's SPEAR captures (Bell V1-V4, Bow V1-V7, Edge V1-V7; bow and bell with the stick tip, edge with the shoulder) by `build_preset.py` (kept in the project with its documentation, `SPEAR_Preset_Builder.md`, not in this repo). SPEAR files excluded. No baseline render check: the plugin is the reference (see `CLAUDE.md`).

| File | MD5 |
|---|---|
| `presets/raven/Raven_Baseline1.json` | `f03d08ee5d10cf351a9019c2a876b52e` |
| `presets/raven/data/raven_bell_excitation_ratios.json` | `bdb90d26d2a9bc1a0119c5aea7fcd1ef` |
| `presets/raven/data/raven_build_info.json` | `528384e41cdc74e2e9a6715fdf1db33a` |
| `presets/raven/data/raven_edge_benv_trigger.json` | `0a1f20be7c52c42897acec4b6a01b042` |
| `presets/raven/data/raven_edge_excitation_ratios_neutral.json` | `d6fbff7aa816a34abc62dcd140ee5175` |
| `presets/raven/data/raven_edge_npz_data.json` | `5614a439a80f2a4e47e2ee427743fce5` |
| `presets/raven/data/raven_edge_vel_tilt.json` | `7ca4b1dfe389f178b93ed34c827464b6` |
| `presets/raven/data/raven_fingerprint.json` | `30cb99e57c5f501fe5761227473c5242` |
| `presets/raven/data/raven_pkt_data.json` | `700bda1627e8f58e0e2486f2160efc85` |

## Notes

- Bettis format: fingerprint (900 modes), bell ratios, neutral edge ratios, NPZ edge (7 velocities) with band envelopes, edge pk_t by velocity, edge velocity tilt, and an all-zero edge band envelope file that switches the NPZ envelope on in Python (as the 602's).
- Mode bank: partials of the bow and edge captures clustered within a 0.3 % span; exc from the bow captures only (80th percentile of each capture's normalised peak amplitude), gamma 0.8, modes below 400 Hz x0.65. 682 modes seen in at least 2 bow captures, plus 218 seen only in the edge captures (exc 0: they sound on edge strikes through the NPZ velw).
- T60: log-linear decay fits of the partials, amplitude-weighted median per mode, scaled per one-third-octave band so that the modelled band decay matches the SPEAR band decay of the three loudest bow captures; `t60_scale` 1.0.
- The NPZ edge uses the plugin's 22 one-third-octave bands exactly (89.09 Hz x 2^(j/3)), so the Tuning window's onset-shift bands map one to one. The band envelopes are the SPEAR band levels (loudest band = 1); `edge_rms_correction` divides each band by its velw norm (geometric mean over the velocities, within x0.25-x4), so a band's level follows the SPEAR band level. It acts after the engine normalises the envelope, so a sparse band cannot become the envelope's peak.
- Velocities from the captures' levels (RMS of the first 500 ms relative to the loudest capture of each articulation), in `data/raven_build_info.json` with the SPEAR level targets: BELL +4.72 dB and EDGE +16.23 dB above BOW (Bell V4, Edge V7 against Bow V7).
- Build-up (LF and MF) as Bettis and Swish; wander, stick click and NL off. No velocity tables: bell and bow use the hertz law (v^2).
- Photo: `resources/ui/Raven.png` (712 x 712, transparent, MD5 `16a4fa620ab27117b6aa2c57dad51410`), made 2026-10-26 from Kris's only photo, which is taken at an angle: the rim ellipse (about 34 degrees of tilt) stretched back to a circle, the bell moved back to the centre by a correction that fades out towards the rim. Light and colour (version C2, Kris's choice, replacing version B): the bow's large-scale light and its tint taken from Nova's photo at the same angle, the hammering kept at Nova's local contrast, the edge zone lifted with the bow, the bell and rim moved towards Nova's tint; from about 3 to 4:30 o'clock the bow is version B (the photo's own light, evened out), blended over about 20 degrees. The bell (or centre) zone ends at about 0.29 of the photo's radius.

# Konstan Crash 18"

Built 2026-10-28 in the project session from Kris's SPEAR captures (Bow V1-V6, Edge V1-V7; bow with the stick tip, edge with the shoulder; no bell captures) by `build_preset.py --infer-bell --bell-seed 3` (kept in the project with its documentation, `SPEAR_Preset_Builder.md`, not in this repo). SPEAR files excluded. No baseline render check: the plugin is the reference (see `CLAUDE.md`).

| File | MD5 |
|---|---|
| `presets/konstan/Konstan_Baseline1.json` | `081621b7b0abf8e3326b6e966e938517` |
| `presets/konstan/data/konstan_bell_excitation_ratios.json` | `d625060fbac75a94d7866b02f0f75624` |
| `presets/konstan/data/konstan_build_info.json` | `0218d55d9fc14b0cfed3696ccb0144dc` |
| `presets/konstan/data/konstan_edge_benv_trigger.json` | `0fceb8dc0b3dde81810e0e175a75c43e` |
| `presets/konstan/data/konstan_edge_excitation_ratios_neutral.json` | `e8e98e79b42bdf78304a10c84915249c` |
| `presets/konstan/data/konstan_edge_npz_data.json` | `5e152d329e7a88b8438d25fa28164b36` |
| `presets/konstan/data/konstan_edge_vel_tilt.json` | `3aa0644f8767cfa5b1a8c31a6d6a0568` |
| `presets/konstan/data/konstan_fingerprint.json` | `78ef40019a7c36ee44b422a938e899ca` |
| `presets/konstan/data/konstan_pkt_data.json` | `d28a1fe990cc07ca61e445e5098e907b` |

## Notes

- Bettis format, as RAVEN: fingerprint (900 modes), bell ratios, neutral edge ratios, NPZ edge (7 velocities) in the plugin's 22 bands with `edge_rms_correction`, edge pk_t by velocity, edge velocity tilt, and the all-zero trigger file that switches the NPZ envelope on.
- Mode bank: 700 modes seen in at least 2 bow captures plus 200 seen only in the edge captures. The builder now keeps up to 200 places for edge-only modes: without them the bow filled all 900 and the edge's 271 Hz wash mode was missing. RAVEN builds the same.
- T60: decay fits calibrated per band (82 % of the modes have their own fit).
- **T60 refit from the bow captures (2026-10-31, Kris's choice by ear, version C):** the bow's modes above about 1 kHz died 2-3x faster than in the recordings, partly from the per-band calibration and partly from V19's T60 cap (`_T60_CAP_F` / `_T60_CAP_VAL`: 6 s at 1 kHz, 3 s at 4 kHz, 1.6 s at 10 kHz), so the bow sank into its 376-445 Hz modes and sounded lower and unlike the edge. For each mode with exc > 0: the SPEAR partials within ±0.6 % of its frequency in Bow V4, V5 and V6, summed per frame; a straight-line fit of their level in dB from 0.3 to 3 s (at least 20 frames, peak above -75 dB); where at least two captures fit, T60 = their median, the change limited to x1/3-x3. 577 of the 700 bow modes refitted (median x2.6). The fingerprint now holds the effective T60 of every mode (the refit, or the old value after the cap for the others): 597 entries changed, rounded to 4 decimals. These T60s are meant to be used **without** V19's cap: 289 of them are above it. Between 2 and 4 s the bow's spectral centroid goes from about 480 Hz to 1970 Hz (the edge's is about 2700 Hz).
- **The bell is inferred, not captured.** On NOVA RIDE, RAVEN and HAMMERED THIN RIDE the bell excites the same modes as the bow with a consistent tilt in absolute frequency (less below 1.3 kHz, more at 2.5-9 kHz), concentrated in a few modes whose choice nothing measured predicts. The builder applies that tilt (eased below 1.5 kHz), an extra tilt putting the bell's centroid at 0.7 x the bow's, and a per-mode scatter from Nova's and Raven's bells (seed 3) widened to make the bell purer than theirs; chosen by ear by Kris, 2026-10-28. Shaping values in `data/konstan_build_info.json` (`bell_shaping`). Real bell captures would replace it.
- SPEAR level targets in `data/konstan_build_info.json`: EDGE +13.5 dB above BOW (Edge V7 against Bow V6); BELL +4.72 dB, RAVEN's measured value (the bell is inferred).
- Build-up (LF and MF) as Bettis and Swish; wander, stick click and NL off. No velocity tables: bell and bow use the hertz law (v^2).
- No photo: the plugin draws the wireframe (`docs/ui/wireframe_reference.py`, `konstan`).

# Extra Paper Thin Ride 24"

Built 2026-11-21 in the project session from Kris's SPEAR captures (Bell V1-V3 and Bow V1-V3 with the stick tip, Edge V1, V2, V3, V5 and V6 with the shoulder) by `build_preset.py --low-wash` (kept in the project with its documentation, `SPEAR_Preset_Builder.md`, not in this repo), then reworked by ear with Kris the same day (the edge, below). SPEAR files excluded. No baseline render check: the plugin is the reference (see `CLAUDE.md`).

| File | MD5 |
|---|---|
| `presets/paperthin/PaperThin_Baseline1.json` | `781ed44492d75bb783ec3714aa6af2e4` |
| `presets/paperthin/data/paperthin_bell_excitation_ratios.json` | `950e1bfaf6debc324f4aedf1bf394715` |
| `presets/paperthin/data/paperthin_build_info.json` | `8b8acbbc4e4a173b1c955074e8e39824` |
| `presets/paperthin/data/paperthin_edge_benv_trigger.json` | `df6358410f155bd495fca7e614288233` |
| `presets/paperthin/data/paperthin_edge_excitation_ratios_neutral.json` | `76066fa231d4b3d2a38b375ee7615b57` |
| `presets/paperthin/data/paperthin_edge_npz_data.json` | `ec17e1ceecad5990fccef5e4e3178223` |
| `presets/paperthin/data/paperthin_edge_vel_tilt.json` | `3fc4c6c51a032b63bd47509730c2f910` |
| `presets/paperthin/data/paperthin_fingerprint.json` | `ed4d79383a83784809276015f6ada6d5` |
| `presets/paperthin/data/paperthin_pkt_data.json` | `adc7aa2818b0896c135312d83d1e1a1b` |

## Notes

- Bettis format, as RAVEN and KONSTAN: fingerprint (900 modes), measured bell ratios, neutral edge ratios, NPZ edge (3 rows) in the plugin's 22 bands with `edge_rms_correction`, edge pk_t by velocity, edge velocity tilt, and the all-zero trigger file that switches the NPZ envelope on.
- Mode bank from the bow captures and Edge V3, V5 and V6: 771 modes seen in at least 2 bow captures and 129 edge-only modes, with up to 24 edge modes in each band below 400 Hz (`--low-wash`, the crash wash).
- **The edge (Kris, by ear, 2026-11-21, "C"; this replaces the first version of this branch, which used Edge V1, V2 and V6):**
  - **Edge V1 and V2 left out:** their onset is as bright as the bow's (centroid 3.7-4.5 kHz in the first 150 ms, the bow 4.3-5.4), and they sounded like a tip.
  - **Rows V5, V3, V6 at velocities 0.536, 0.649 and 1.0**, set so each row renders at its recorded level relative to the loudest (V5 0.552, V3 0.606, V6 1.0). The levels' order differs from the V numbers: V5 is quieter than V3.
  - **Every row uses Edge V5's mode weights (velw).** The loud recordings light up a different set of partials, which Kris heard as a different articulation at high velocity.
  - **The V3 and V6 rows keep each band's level and decay but build up to the band's peak as Bettis's edge does at 0.67 and 1.0** (per band: Bettis's normalised rise up to its peak, then the row's own decay from its peak). As recorded, V3's upper bands peaked within 20-60 ms (a ping) and V6's low bands within 20 ms (a thump); on Bettis the upper bands swell over 200-300 ms at high velocity.
- **T60 refit from the bow captures, as KONSTAN** (see its notes): 653 of the 771 bow modes refitted (median x3.0, the x3 limit); the others keep their T60 after V19's cap. Meant to be baked **without** the cap: 327 are above it. The cymbal rings long: the measured band decays are 10-27 s from 180 Hz to 2 kHz.
- **Bell measured,** not inferred: Bell V1-V3 against the upper half of the bow captures (V2, V3).
- SPEAR level targets in `data/paperthin_build_info.json`: BELL +8.69 dB and EDGE +15.77 dB above BOW (the loudest captures, first 500 ms).
- Build-up (LF and MF) as Bettis and Swish; wander, stick click and NL off. No velocity tables: bell and bow use the hertz law (v^2).
- No photo: the plugin draws the wireframe (`docs/ui/wireframe_reference.py`, `paperthin`), from Kris's photos of 2026-11-21.

# Crash 16" (A Zildjian)

Built 2026-11-22 in the project session from Kris's SPEAR captures (Bell V1 and Bow V1-V2 with the stick tip, Edge V1-V3 with the shoulder) by `build_preset.py` with the T60 refit (`refit_t60.py`; both kept in the project with their documentation, `SPEAR_Preset_Builder.md`, not in this repo), then calibrated by rendering the plugin against the recordings the captures came from (below). SPEAR files and recordings excluded. No baseline render check: the plugin is the reference (see `CLAUDE.md`).

| File | MD5 |
|---|---|
| `presets/azcrash/AzCrash_Baseline1.json` | `303e6d9011d13409452962f8bab43685` |
| `presets/azcrash/data/azcrash_bell_excitation_ratios.json` | `e438f54b7e349ca28a291e9767f9a20b` |
| `presets/azcrash/data/azcrash_build_info.json` | `b1f66accd21091d51dada04d1dce7b74` |
| `presets/azcrash/data/azcrash_edge_benv_trigger.json` | `12b196fa0d48141426c79bec6690d1bb` |
| `presets/azcrash/data/azcrash_edge_excitation_ratios_neutral.json` | `f6f971dd825aa7e1bd89698a85ee73a4` |
| `presets/azcrash/data/azcrash_edge_npz_data.json` | `071ebfda929f2cbcf2bdab6999c089ec` |
| `presets/azcrash/data/azcrash_edge_vel_tilt.json` | `29e2b1d6e74224f8b757a8391b39f620` |
| `presets/azcrash/data/azcrash_fingerprint.json` | `5f01196ae1cec10b6dfbbf8d25d0c0dc` |
| `presets/azcrash/data/azcrash_pkt_data.json` | `a80a78f67c30fe86a34334f5bd3e9080` |

## Notes

- Bettis format, as RAVEN, KONSTAN and EXTRA PAPER THIN: fingerprint (900 modes: 706 from the bow captures, 194 seen only in the edge captures), measured bell ratios, neutral edge ratios, NPZ edge (3 rows) in the plugin's 22 bands with `edge_rms_correction`, edge pk_t by velocity, edge velocity tilt, and the all-zero trigger file that switches the NPZ envelope on.
- **T60 refit from the bow captures, as KONSTAN** (see its notes): 617 modes refitted (median x2.1). Meant to be baked **without** V19's cap: 322 of the final T60s are above it.
- **The 152 Hz hum (2026-11-22, Kris: "needs to go"):** the render calibration had stretched four quiet bow modes at 151.2, 151.5, 152.7 and 153.2 Hz (ids 49, 50, 52, 53) to a T60 of 364 s, so after a minute of playing a 152 Hz hum was left ringing for minutes. Their T60 is set back to 12.0 s, their value before the calibration (the neighbouring bow modes: 13.0 s at 127-128 Hz, 5.6 s at 194-196 Hz). Nothing else in the preset changes.
- **Calibrated by rendering (2026-11-22, Kris):** Kris heard the first build's edge as too high-pitched and its bow as too heavy in the low modes, though the captures are of this cymbal. The SPEAR resynthesis matched the recordings (Bow V1 = BeadSoft, Bow V2 = BeadMedium, Bell V1 = BellSoft, Edge V1-V3 = CrashSoft, CrashMedium, CrashLoud) within about 1 dB in every range; the built preset did not: the bow was short of 280-900 Hz with excess lows and highs in its tail, and the loud edge was about 5 dB short below 2.8 kHz with excess 7-18 kHz. So the plugin was rendered and compared with the recordings, and the data adjusted until they matched:
  - **Bow:** mode exc per range from the 0.3-1.0 s spectrum (relative to the highs: 280-900 Hz up about 2.2x), T60 per range from the decay between 0.3-1.0 s and 1-3 s (changes within x0.5-2).
  - **Bell:** the ratios of the modes the bell strikes (exc > 0) scaled per range against BellSoft, not clipped (34 above 1, at most 2.26); modes with exc 0 keep their built value.
  - **Edge:** see the next point.
  - **What remains:** the bow's first 150 ms lacks the strike's low thump and 4-11 kHz sizzle (one fixed decay per mode cannot make them); the bell's tail is thin at 280-900 Hz and bright on top.
- **The edge (Kris, by ear, 2026-11-22):**
  - **One set of mode weights (velw) on every row, Edge V2's.** Below 900 Hz the three captures' matched partials barely overlapped (similarity 0.08 between V2 and V3 below 280 Hz), and Kris heard the loudest edge as a different cymbal.
  - **The modes below 112 Hz weighted from the SPEAR energy within ±4 % of each mode, frame by frame.** On a loud hit the lowest partial glides from about 75 to 111 Hz over the first second, so the builder's ±0.25 % match gave those modes almost no weight, and the loud edge lost its 60-100 Hz body (about 25 dB short of CrashLoud). The split between the sub-89 Hz modes and the 89-112 Hz modes is set per row.
  - **Each row's band envelopes calibrated per band and time window** (0-0.2, 0.3-1.5 and 1.5-4 s) until the plugin matched its recording.
  - **Rows at velocities 0.27, 0.522 and 1.0,** set so each row renders at its recording's level relative to the loudest (0.224, 0.5, 1.0: -13 and -6 dB). The edge's level rises steadily with velocity: -18, -13, -8, -5, -2.4 and 0 dB at 0.15, 0.27, 0.45, 0.6, 0.8 and 1.0.
  - **Every row's envelope peaks at frame 1, band 7 (value 1.0).** The engine normalises a blend of two rows by the blend's peak (and the blended velw by its peak); with the rows peaking in different places, strikes between two rows came out up to about 5 dB louder than the rows themselves. With one set of velw and a shared envelope peak, they don't.
- SPEAR level targets in `data/azcrash_build_info.json`: EDGE +18.1 dB above BOW (Edge V3 against Bow V2, first 500 ms). The only bell capture is soft (Bell V1, -9.66 dB against Bow V2, about level with the soft Bow V1): the bell trim target is -0.7 dB against BOW, as in the renders Kris heard.
- Build-up (LF and MF) as Bettis and Swish; wander, stick click and NL off. No velocity tables: bell and bow use the hertz law (v^2).
- No photo: the plugin draws the wireframe (`docs/ui/wireframe_reference.py`, `azcrash`), from Kris's photos of 2026-11-22.

# HI-HAT (GF A60, 14")

Built 2026-11-20 in the project session from Kris's SPEAR captures of a pair of 14" hats: HH Tip V1-V3 (stick tip on the bow, closed) and HH Shoulder Edge V1-V7 (stick shoulder on the edge; V1-V3 closed at three velocities, V4-V7 opened in steps, Kris 2026-11-20). SPEAR files excluded. Not a V19 preset: its own format (`cymbalis-hihat-1`), read by the hi-hat bake, with the strike model in `docs/hihat_reference.py`. No baseline render check: the plugin is the reference (see `CLAUDE.md`).

| File | MD5 |
|---|---|
| `presets/hihat/HiHat_A60.json` | `c491060b013d35997116a2ab99eeacea` |
| `presets/hihat/data/hihat_envelopes.json` | `d78586a3de2d06f65bae5764e0d52269` |
| `presets/hihat/data/hihat_weights.json` | `6f9f0a6e61d34fb535ce84647dcaa89d` |
| `presets/hihat/data/hihat_weights_free.json` | `687ef98f22320a52a3c5b99b13df7dc2` |

## Notes

- Mode bank: 683 modes, the SPEAR partial frequencies of all ten captures merged within 0.3 %, the strongest per band (12 in each band below 449 Hz, up to 45 above).
- Ten knots (`HiHat_A60.json`): closed TIP and EDGE at velocities 0.3 / 0.6 / 1.0 (V1-V3), and EDGE at openness 0.25 / 0.5 / 0.75 / 1.0 (V4-V7). The open captures' velocities are matched by attack level (first 20 ms) to the closed edge: V4 and V5 about V2 (0.6), V6 and V7 about V3 (1.0). V4 and V5 measure almost the same (level and decay).
- Per knot: the band envelope (22 plugin bands, 10 ms frames, the SPEAR band amplitude from the first frame with a partial) and each mode's weight within its band (unit norm per band; SPEAR energy within 0.4 % of the mode in the first 300 ms, floored at 8 % of the band's strongest).
- What the captures show: closed hits fade 20 dB in about 0.1 s in the upper bands, with a low body around 150-300 Hz; open hits ring 2-2.5 s (V4, V5) to 4-6 s (V6, V7) and lose the low body. SPEAR shows every strike rising over 20-40 ms (its analysis window), which sounds like the pedal closing the cymbals rather than a stick: the stick articulations replace that rise (`docs/hihat_reference.py`); the FOOT articulation keeps it.
- `hihat_weights_free.json` (added 2026-11-21, Kris: "version C"): the fully open knot (V7) as a single free-ringing cymbal, its mode weights from the SPEAR partials lasting at least 0.4 s only, each partial to its nearest mode, no floor (237 modes above 0.05, against 510). The fully open capture still has the bottom cymbal in it: its level pulses at about 1 and 4 Hz and its partials are short-lived (about 7,200 tracks in 1.5 s); the long partials are the top cymbal ringing.
- No photo: the plugin draws the wireframe (`docs/ui/wireframe_reference.py`, HI-HAT section).

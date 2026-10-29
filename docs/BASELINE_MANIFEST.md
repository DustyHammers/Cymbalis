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

## Notes

- `tip.json` and `shoulder.json` reference `nova_vel_participation_tip.json` and `nova_vel_participation_shoulder.json`, which are not in the package. Those two overlays cannot be loaded.
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
| `presets/konstan/Konstan_Baseline1.json` | `d44f57e83c6e4054d635c231cbf8fbba` |
| `presets/konstan/data/konstan_bell_excitation_ratios.json` | `e3f30a458241b02ef857ec216ee72c9c` |
| `presets/konstan/data/konstan_build_info.json` | `8cf7eeb50edb8af9e6a912f89aba75fb` |
| `presets/konstan/data/konstan_edge_benv_trigger.json` | `0fceb8dc0b3dde81810e0e175a75c43e` |
| `presets/konstan/data/konstan_edge_excitation_ratios_neutral.json` | `bbab82122d0a50692fc85fddbe0340ea` |
| `presets/konstan/data/konstan_edge_npz_data.json` | `7453052c09394dfbd2541749538b502a` |
| `presets/konstan/data/konstan_edge_vel_tilt.json` | `3aa0644f8767cfa5b1a8c31a6d6a0568` |
| `presets/konstan/data/konstan_fingerprint.json` | `3b9e45f9b407aa3d56f45034f2c36e3f` |
| `presets/konstan/data/konstan_pkt_data.json` | `7585995edef9d32a778e7e4c1e3e2d2f` |

## Notes

- Bettis format, as RAVEN: fingerprint (900 modes), bell ratios, neutral edge ratios, NPZ edge (7 velocities) in the plugin's 22 bands with `edge_rms_correction`, edge pk_t by velocity, edge velocity tilt, and the all-zero trigger file that switches the NPZ envelope on.
- Mode bank: 700 modes seen in at least 2 bow captures plus 200 seen only in the edge captures. The builder now keeps up to 200 places for edge-only modes: without them the bow filled all 900 and the edge's 271 Hz wash mode was missing. RAVEN builds the same.
- T60: decay fits calibrated per band (82 % of the modes have their own fit).
- **The bell is inferred, not captured.** On NOVA RIDE, RAVEN and HAMMERED THIN RIDE the bell excites the same modes as the bow with a consistent tilt in absolute frequency (less below 1.3 kHz, more at 2.5-9 kHz), concentrated in a few modes whose choice nothing measured predicts. The builder applies that tilt (eased below 1.5 kHz), an extra tilt putting the bell's centroid at 0.7 x the bow's, and a per-mode scatter from Nova's and Raven's bells (seed 3) widened to make the bell purer than theirs; chosen by ear by Kris, 2026-10-28. Shaping values in `data/konstan_build_info.json` (`bell_shaping`). Real bell captures would replace it.
- SPEAR level targets in `data/konstan_build_info.json`: EDGE +13.5 dB above BOW (Edge V7 against Bow V6); BELL +4.72 dB, RAVEN's measured value (the bell is inferred).
- Build-up (LF and MF) as Bettis and Swish; wander, stick click and NL off. No velocity tables: bell and bow use the hertz law (v^2).
- No photo: the plugin draws the wireframe (`docs/ui/wireframe_reference.py`, `konstan`).
- **Low wash (2026-10-29, Kris):** rebuilt with `build_preset.py --infer-bell --bell-seed 3 --low-wash --bell-from <the previous bell ratios>`. The edge's low bands are dense, drifting partials in the captures (30-170 per band below 400 Hz) against 0-3 modes in the first build, and the band level was counted twice (the velw coupling and the band envelope) with the correction capped at ±12 dB, so the edge sounded thin. Each band below 400 Hz now gets up to 24 modes from the edge clusters, as edge-only modes (exc 0), in the places of the weakest edge-only modes above 400 Hz; `edge_rms_correction` is exact (no cap), so the band envelope alone sets each band's level. The 833 modes kept from the first build keep their exc and T60, and the bell keeps its ratios (matched by frequency), so bow and bell strikes are unchanged. Edge strikes change.

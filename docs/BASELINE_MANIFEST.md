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
- Photos: `resources/ui/SmokeFlatRide.png` (714 x 718) and `resources/ui/HammeredThinRide.png` (709 x 713).

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
- Photo: `resources/ui/Swish.png` (737 x 747).

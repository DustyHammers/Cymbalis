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

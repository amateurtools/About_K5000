# K5000 project: bird's-eye view (STUB)

Purpose: one page to get your bearings, for a person or an LLM that lost the thread or is picking the project up cold.
Spine = the table of contents of the Kawai K5000S Owner's Manual (`man/m.txt`), because every feature a user can touch is listed there; each entry points to where our findings live.
Status: **M** measured in the original OS/emulator, **S** from the Kawai spec/manual, **I** inferred (clips, statistics), **?** unknown / not started, **-** not applicable.
All claims in the linked docs should end up carrying one of these tags (not yet true everywhere).

Reading order for a newcomer: this page -> `K5000_PARAMETER_ARCHITECTURE.md` (sections 1-5 model, 15-18 registry/codecs/emulator) -> `K5000_FILE_FORMATS.md` -> `K5000_ADDITIVE_ENGINE.md` -> `K5000_OS_INTERNALS.md` (only if you need to dig into the firmware).

## Part A. Manual spine, with status

| Manual section (page) | What it is | Where documented | Code / tests | Status |
|---|---|---|---|---|
| Welcome, front/rear panel (9-12) | 32 additive sources, up to 6 sources/patch, PCM+ADD mix, 128-band formant filter, 4 macros, arpeggiator, effects | ARCHITECTURE s1; `k5000s_hardware.md` | - | S |
| Guided tour: playing singles/multis, demo, arpeggiator (13-20) | user-level behaviour | ARCHITECTURE s17 (multi/arp) | `test_multi.py` | S/I |
| **Single section** | | | | |
| Single play (22) | bank/slot selection, program change | OS_INTERNALS | `codec_work/px.cpp` | M (dump, ID, PC) |
| Common (25) | name, volume, poly, effect algorithm, macros, switches, sources used | ARCHITECTURE s2-4, s15 | `k5k_registry.py`, `test_registry.py` | M (82 common bytes, all wire addresses) |
| DCO + pitch envelope (30-31) | wave select, coarse/fine, KS pitch, pitch env | ARCHITECTURE s15; FILE_FORMATS | registry | M (wire), ? (pitch law) |
| DCF + filter envelope (32-33) | filter bypass/mode, resonance, cutoff, env, KS, velo | ARCHITECTURE s15 | registry | M (wire), **? (DSP deferred)** |
| DCA envelope (35) | amp env, KS, velocity | ARCHITECTURE s15 | registry | M (wire), ? (rate/level laws) |
| Additive: harmonic levels DHL (38) | soft/loud 64+64 levels, group | ADDITIVE_ENGINE | registry; `test_kram.py` | M (wire, kram), I (level law ~0.75 dB/step) |
| Additive: harmonic envelope DHE (41) | 64 x 4-segment envelopes, loop | ADDITIVE_ENGINE; ARCH s18.4-18.5 | `test_kram.py`, `test_loop.py` | M (storage, loop flags), **? (LP1/LP2 names, rate law)** |
| Additive: formant filter DFL/DFE (44-47) | 128 bands, envelope, LFO | ADDITIVE_ENGINE | registry | M (wire, kram), ? (filter law) |
| MORF (50) | 4-point morph, times, loop | ARCH s17 | registry | M (wire), ? (engine) |
| LFO & copy (51) | waveform, speed, delay, fade, depths | ARCH s15 | registry | M (wire), ? (rates) |
| Control: volume, bend, pan; zone; controllers (53-58) | sources, destinations, depths, lists | ARCH s17 (lists) | `k5k_lists.py` | M |
| Write (59) | store to bank, acks | ARCH s18.1 | `k5k_codecs.py` | M (acks, dumps) |
| **Effect section** | | | | |
| Effect page, algorithms 1-4 (62-66) | routing of 4 effects, source path | ARCH s16 | `k5k_effects.py` | M (layout), I (routing signal flow) |
| 40 effect types, 14 reverbs (67-96) | type table, parameter names and ranges | ARCH s16 | `extract_effects.py`, `k5k_effects.h`, `test_effects.py` | M (names, ranges), **? (display scales, DSP)** |
| **Multi section** | | | | |
| Multi play/edit/write (98-103) | 4 sections, zones, per-section effects | ARCH s17 | `test_multi.py` | M (103-byte layout), ? (voice allocation across sections) |
| **Common section** | | | | |
| System, FSW, backup, reset (106-112) | global settings | ARCH s17 | registry | M (wire), ? (storage) |
| Arpeggiator (108) | patterns, modes, programmable steps | ARCH s17 | registry | M (wire), ? (timing engine) |
| MIDI, dump (113-114) | SysEx dump/receive, device ID | ARCH s18.1; `os_sysex_findings.md` | `k5k_codecs.py` | M |
| Disk: save/load/format, file list (115-119) | file formats on floppy | FILE_FORMATS | `k5kfile/`, `test_codecs.py` | M (byte-exact round trips) |
| Appendix: cyclical wave list (123) | PCM waves incl. looping | PCM_ROM | `pcm_wave_*.csv` | M (directory), ? (voice) |

## Part B. Suggested appendices (service-manual half, not in the Kawai manual)

| Appendix | Content | Source today |
|---|---|---|
| A. Hardware | CPU (FR30/MB91xxx), memory map, K018 additive chip, PCM ROM, UART, panel scan | `k5000s_hardware.md`, OS_INTERNALS |
| B. OS internals | boot, task/interrupt map, edit buffers (0x322F70, 0x364E97, kram), SysEx RX/TX path | OS_INTERNALS, ARCH s18.3 |
| C. Parameter registry | every parameter: ID, wire address, range, default, mapping to image offset | generated: `k5k_param_ids.h` + `k5k_registry.py` (to emit CSV/JSON) |
| D. SysEx reference | all message types with byte-level examples (captured from the OS) | ARCH s18.1, `codec_work/` captures |
| E. File formats | KA1 / KAA / KBA / KCA byte layouts | FILE_FORMATS |
| F. Additive engine laws | what is measured vs fitted vs unknown; calibration plan | ADDITIVE_ENGINE |
| G. Effects reference | all 48 types with signal flow notes and ranges | ARCH s16 + generated table |
| H. Test and verification catalogue | which test proves which claim; how to rerun the emulator sweeps | `schema/test_*.py`, `emu_check/` |
| I. Open questions | the "?" entries above with how to resolve each (real unit, clip fit, OS trace) | to write (seed list below) |
| J. Decisions log | why: registry as single source, codecs at the edges, raw bytes canonical, additive first, DCF deferred, C++/JUCE final | scattered in ARCH s6-14 |
| K. Glossary | DCO/DCF/DCA/DHL/DHE/DFL/DFE/MORF, kram, wire address, image offset | to write |
| L. One-shot build brief (`BUILD_A_SYNTH.md`) | read order, minimal engine (additive oscillator), acceptance tests, do-not-guess list | to write |

## Part C. Seed list for "Open questions"

1. Harmonic-envelope loop names: wire 1/2 vs LP1/LP2 (ARCH s18.5).
2. Additive rate/level laws, harmonic ratios beyond integer, formant filter law (ADDITIVE_ENGINE).
3. DCF: whole filter (deferred by choice).
4. Effect display scales and DSP for all 48 types.
5. Kram bytes 0x000-0x07F per source (common fields), written by code at 0x166148.
6. Multi velocity-switch composite is lossy; section voice allocation.
7. Hardware behaviours with no unit available (calibration recordings).
8. Per-parameter OS apply time (scheduler pacing uses a guessed 2 ms gap).
9. PCM voice (sample playback, looping, interpolation).

## Part D. Conventions to adopt when the real rewrite starts

- Source in Markdown, one file per area, each readable in one go; generate PDF/HTML from it.
- Tables of parameters/effects/layouts are generated from the registry, never typed.
- Every claim carries M/S/I/? and, if M, a test or script name.
- Keep this page current: it is the first thing a newcomer reads.

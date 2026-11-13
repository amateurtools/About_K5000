# Architecture (code)

Layers, from the bottom. A layer may include only layers below it. `core` and `engine` contain no JUCE.

| Layer | Directory | What lives there |
|---|---|---|
| core | `core/registry`, `core/codec`, `core/patch`, `core/link` | parameter registry (ids, ranges, wire addresses; generated from `tools/schema`), SysEx/file/state codecs, `PatchModel` (message thread), snapshots, multi + multi library, output scheduler (declarations), system state (stubs) |
| engine | `engine/*` | audio DSP, header-only units, real-time safe |
| plugin | `plugin/` | JUCE glue: processor, editor (deferred), file dialogs, MIDI, state. Thin: no DSP. |

## Engine map

```
note -> [Arpeggiator] -> Synth (note logic: poly/solo, pedal, voice pool, transpose/tune/pan/path per part)
                           |  per voice, per source:  SourceVoice
                           |       oscillator (AddSource | PcmSource)   <- SourceEnv: DCA env, pitch env, LFO (vibrato, tremolo)
                           |       -> Dcf (filter, env, KS, velocity, growl)  -> level/pan smoothing -> effect path 0..3 (stereo)
                           v
                         FxChain: 4 stereo effect inputs -> FxRack (E1..E4, reverb, GEQ) -> stereo out
                           ^ EffectSetup (from the single or multi common block) + EffectController (MIDI -> parameter offsets)
SynthFx   = Synth + Arpeggiator + FxChain            (single mode)
MultiSynth = 4 x Synth + FxChain                      (multi mode: gate, zones, channels, the multi's effects)
```

| Unit | File | Job |
|---|---|---|
| `Synth` | `voice/synth.*` | which sources start and stop (note logic); owns `VoicePool` and the voices |
| `SourceVoice` | `voice/source_voice.h` | one source of a note: oscillator + DCF + pan + level; HOW it sounds |
| `SourceMod` | `voice/modulation.h` | live controller offsets (pitch cents, cutoff units, level dB, pan); the one place controllers plug in |
| `FxChain` | `voice/fx_chain.h` | rack, setup, effect controller, stereo buffers, chunked render loop |
| `SourceEnv`, `Lfo`, `Dcf` | `common/source_env.h`, `lfo/lfo.h`, `filter/dcf.h` | per-source envelopes, LFO (closed form in time), filter |
| `Arpeggiator` | `arp/arpeggiator.h` | clocked note sequencer, sample-accurate events |
| laws | `AddLaws`, `DcfLaws`, `LfoLaws`, `ReverbLaws`, ... | every guessed number lives in a laws struct, tagged `[?]`; calibrate there only |

## Where does a new feature go?
1. **A per-source behaviour** (key delay, a new modulation destination, a per-source filter term): inside `SourceVoice` (or the unit it owns). Do NOT add a branch to `Synth::noteOn`: it must not know about individual features.
2. **A controller or macro** (bender, wheel, aftertouch, user knobs, assignable controllers): compute a `SourceMod` and call `Synth::setMod`. A new destination = one field in `SourceMod` + one consumer in `SourceVoice`/`Dcf`/the oscillator. Controller-to-destination routing code (the 20-destination table, depths, MIDI mapping) is a separate small class that outputs a `SourceMod`; it has no audio in it.
3. **A part-level behaviour** (section volume, transpose, pan, effect path override): `Synth::PartParams`.
4. **An effect**: a `*Fx` unit in `engine/fx/`, registered in `FxUnit`; the type number is the OS type byte.
5. **A guessed constant**: in the unit's laws struct, with `[?]` and a line in the matching `docs/service` table. Never inline in code.
6. **A parameter that must go over SysEx**: it is a registry row first (`tools/schema/k5k_registry.py`), then `tools/gen_headers.py` regenerates `core/registry/param_ids.h`.

## Real-time rules
`render`, `noteOn`, `noteOff` never allocate or lock. Setup that allocates (sizing buffers) happens in constructors. The plugin hands patches to the audio thread through a try-lock mailbox; the model and UI live on the message thread.

## Tests (ctest)
`core_tests`, `codec_tests`, `model_tests` (core), `engine_tests` (golden voices from the Python prototype), `sf2_tests`, `filter_tests`, `fx_tests`, `wiring_tests` (effects wiring, multi, controllers, arpeggiator, pan), `arp_tests`, `dcf_tests`, `lfo_tests`, `mod_tests`, `controller_tests`, `formant_tests`, `portamento_tests`, `gating_tests`, `am_tests`, `host_tests`, `switch_tests`, `patchfade_tests`, `doc_tests` (undo, document, program guard, nav) (MIDI control plane). A refactor must leave the audio bit-identical: render a few real patches before and after and compare hashes (done for the SourceVoice/FxChain refactor).

## Known loose ends
Output scheduler (declarations only, not in the build); dump/receive (stubs in `core/link/system_state.h`); the plugin has not been compiled in the sandbox; duplicate docs and data trees (`docs/K5000_OS_INTERNALS.md` vs `docs/service/`, `data/fx` vs `tools/schema`) are waiting for approval to clean up.

## Controller path
MIDI bender/pressure/CC -> `ControllerState` (per part, in `Synth`) -> `ControllerMatrix::compute` (patch routing bytes) -> `SourceMod` per source -> `SourceVoice::setMod` -> `AddSource`/`PcmSource`/`SourceEnv`/`Lfo`/`Dcf`. The Effect Controller has its own `ControllerState` inside `FxChain`. Tests: `tests/controller_tests.cpp`.

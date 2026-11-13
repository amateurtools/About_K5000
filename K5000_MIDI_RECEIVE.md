# K5000S MIDI receive map and where each message lands

Status tags: **M** measured on the OS, **S** from a Kawai document (MIDI implementation chart 4.2 "K5000S", manual), **I** inferred, **?** guessed.
Implementation: `engine/host/multi_host.h` (`MultiHost`), driven by `tools/demo_run/demo_run.cpp` and `tests/host_tests.cpp`. Channels are 0-based in code.

## Channel messages (S: chart 4.2, K5000S column "Recognized")
| Message | Lands in | Status |
|---|---|---|
| Note on / off | `MultiSynth::noteOn/noteOff` by section receive channel and gate | done |
| Polyphonic pressure | not recognized by the K5000S | ignored by design (S) |
| Channel pressure | controller source Ch Press of the sections on that channel + Effect Controller | done |
| Pitch bend | Bender source of the channel's sections + Effect Controller | done |
| CC 0 bank select | per-channel pending bank (`MultiHost::bankMsb_`), used by the next program change | done |
| CC 32 bank LSB | always 0 for the K5000S banks | ignored (S) |
| CC 1 wheel, 11 expression, 16-19 G Cont 1-4, 80-83 G Cont 5-8 | controller sources -> source routing, Effect Controller | done |
| CC 7 volume | source "MIDI Volume" **and** channel level of the sections on that channel (`MultiSynth::setChannelMix`, relative to power-on 100) | done; the level law is [?] (OS use of CC 7 as a mix level not traced) |
| CC 10 pan | source "Pan Pot" **and** pan offset (v-64)/63 of the channel's sections | done; [?] as above |
| CC 64 hold 1 | `sustain(channel)` | done |
| CC 120 | all sound off for the channel | done (not in the chart, standard) |
| CC 121 reset all controllers | bend 0, pressure 0, wheel 0, expression 127, hold off (manual text of the chart notes) | done (S) |
| CC 123-127 | all notes off for the channel | done |
| CC 2, 4, 91, 93, 122 | not in the K5000S column | ignored by design (S) |
| CC 5 portamento time, 65 portamento on/off, 84 portamento control | the engine has portamento (common 60/61) but no live override | **gap** |
| CC 69 hold 2 | | **gap** |
| CC 71-79 sound controllers 2-9 (macro: Even/Odd, Release, Attack, Cutoff, FF depth, Velocity, Resonance, Decay) | the panel macros; the engine has user knobs (`setUserKnob`) but no macro offsets | **gap** |
| CC 6/38 data entry, 98/99 NRPN, 100/101 RPN | NRPN 01/20 cutoff, 01/63-65 attack, decay1, release offsets (+-50), 20/40-43 switches SW1, SW2, FSW1, FSW2; RPN 0 bend range, 1 fine tune, 2 coarse tune | **gap** |
| Program change | SECTION mode: sets tone (bank, program) of every section that receives on that channel, resolves it from the banks. NORMAL mode: selects multi `pc` on the unit channel (needs a multi bank) | done |

Bank select MSB values (S, chart "B bank program no. table", K5000S/R): **100 = A (1-128), 101 = Multi (1-64), 102 = D (1-128), 103 = E, 104 = F** (E and F only with the ME-1 expansion). Library bank numbers: A 2, D 3, E 4, F 5. In SECTION mode an MSB of 101 is [?] (no section meaning) and is reported as dropped. The raw "tone MSB" byte of a multi section holds the library bank number (2 = A, 3 = D), not the wire value.

## System exclusive (S: MIDI implementation; the codec is `core/codec/sysex.h`)
| Message | Lands in | Status |
|---|---|---|
| Single dump (0x20, bank 0 = A / 2 = D, slot) | `MultiLibrary::putSingle`; a section pointing at that slot is re-resolved at once | done |
| Parameter change 0x10 with sub1 = 3 (effects: algorithm, reverb, E1-E4, GEQ, effect controller) | multi common bytes (registry segment 4) -> `MultiSynth::setMulti` -> effect rack | done (edit space = multi) |
| Parameter change 0x10 with sub1 = 4, sub2 = 1 (section tone, volume, pan, effect path, transpose, tune, zone, velocity switch, channel) | multi section bytes (registry segment 5, `55 + 12 * section + offset`) | done |
| Multi dump, block dump | | **gap** (decoded, not applied) |
| Dump requests, ID request, mode change | need MIDI output | **gap** (belongs to the transmit harness) |
| Universal non-realtime (GM System On, device inquiry) | | ignored (the K5000S is not GM; the ID request/reply is a Kawai message) |
| Single-mode parameter changes (sub1 = 0..2, edit buffer of a single) | `PatchModel::applySysex` exists in core, but MultiHost only edits the multi | **gap** (single mode) |
| Timing clock 0xF8 | arpeggiator sync | **gap** |

## Result with the three demo files (K5000 demo ROM songs WELCK5KS = demo-1, WARPK5KS = demo-2, 5000S_FX)
Run: `k5k_demo_run song.mid --banks tools/banks --out out.wav`. All three songs start with a GM System On, then 34-36 effect parameter changes, 12-17 single dumps into bank A, a CC 0 = 100 before each program change, program changes on channels 1-4, and then notes with CC 1/7/10, pitch bend and channel pressure on four channels. Nothing in them is in a gap row above: **0 dropped messages** in each. Ignored: the GM reset in every file, and 22 CC 2 messages in 5000S_FX (the K5000S does not receive CC 2).
The unit is assumed to be in multi mode with a multi that has sections on channels 1-4 and program receive = SECTION (the files contain no multi dump); `MultiHost::initMulti()` provides that multi. [?: the real demo-ROM state is not known.]

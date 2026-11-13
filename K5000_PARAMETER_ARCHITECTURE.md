# K5000 plugin: parameter addressing, state and I/O architecture (design)

Purpose: decide, before the C++ exists, how every parameter is named, stored, changed and transported, so the sound engine has the right hooks from its first line. Covers JUCE/DAW save and recall, host automation, SysEx in and out, CC, legacy preset loading, and modulation. Status: design, to be reviewed. Items marked **[check]** must be verified against the Kawai MIDI spec or the real unit.

---------------------------------------------------------------------------------------------------

## 1. Requirements

1. One source of truth for what is currently loaded, and it must be exactly exportable as a real K5000 patch (bytes in, same bytes out).
2. Every parameter has one stable address that works for: host automation, the UI, MIDI CC, Kawai SysEx (receive and send), and preset loading.
3. Modulation (macros, controllers, LFO, switches) never alters stored values; save/export always writes the stored patch.
4. All inputs funnel through one entry point so nothing can bypass validation, undo, notifications or the SysEx echo.
5. The audio thread never allocates, never locks, never waits on UI or MIDI parsing.
6. The plugin can act as a controller for a real unit: send parameter changes, send and receive dumps, and request the unit's current patch.
7. Robust I/O: malformed SysEx, partial messages, wrong device, MIDI overload, host reload mid-note.

## 2. The three address spaces and how they map

### 2.1 Kawai wire address (fixed by the hardware) [spec 3.2]
Parameter change message: `F0 40 <ch> 10 00 0A <sub1> <sub2> <sub3> <sub4> <sub5> <hi> <lo> F7` (function 0x10, group 00, machine 0A). The K5000 receives these but does not transmit them (except arpeggio parameters) **[check on hardware]**.

| sub1 | sub2 | sub3 | sub4 | sub5 | Area |
|---|---|---|---|---|---|
| 01 | 00 | 00 | 00 | param | Single common |
| 01 | 01 | source 0-5 | 00 | param | Single source |
| 02 | 40 | source 0-5 | 00 | param | ADD kit common (HC kit, bytes 1-36) |
| 02 | 41 | source 0-5 | harmonic 0-63 | 00 | ADD soft harmonic level (HC code 1) |
| 02 | 42 | source 0-5 | harmonic 0-63 | 00 | ADD loud harmonic level (HC code 2) |
| 02 | 43 | source 0-5 | formant band 0-127 | 00 | Formant filter band |
| 02 | 44 | source 0-5 | harmonic 0-63 | param 0-7 | Harmonic envelope (8 values per harmonic) |
| 02 | 45 | phase 0-3 | harmonic 0-63 | 00 | HC code for MORF |
| 02 | 46 | source 0-5 | 00 | 00 | ADD MORF execute (a command, not a value) |

Data is two 7-bit bytes (hi, lo); values such as a bipolar parameter are sent as stored (1..127, 64 = 0). Drum (`00`/`10`) and multi areas exist for the K5000W and S and are handled by the same table later.

### 2.2 Internal ParamID (what everything inside the plugin uses)
A 32-bit id, packed so it can also be a map key and sort order: `area:8 | source_or_phase:4 | index:12 | param:8`. `area` follows the sub1/sub2 pair above (single common, single source, ADD kit, soft, loud, formant, harmonic envelope, morf). One-to-one with the wire address; converting is a table lookup, not logic.

### 2.3 Host-visible parameters (what a DAW sees) - see section 6.

## 3. The patch image is the state

`PatchImage` holds the loaded single exactly as stored on the real unit: 82-byte common, 86 bytes per source, 806 bytes per ADD source, source count in the common block. Rules:

- Loading any preset (.ka1, .kaa, .kba, SysEx dump, project state) produces a `PatchImage`. Saving or sending produces bytes from it; the ADD block checksum is recomputed on export.
- A parameter is a view of some bytes in the image: `{ ParamID, byte offset, bit width, range, default, encode/decode, group, flags }`. The registry is **generated from the existing schema CSV** (`schema/k5000_patch_params.csv`, 176 scalar rows) plus generated rows for the bulk arrays, so the spec and the code cannot drift.
- Multi patches (.kca, 64 x 103 bytes) get a `MultiImage` with the same pattern (parameter registry for the 12-byte sections and 14-byte common) so the plugin can run in multi mode later.

### 3.1 Parameter count (sizes the host-exposure decision)
| Class | Count |
|---|---|
| Common scalars | about 81 |
| Source scalars | 86 x 6 = 516 |
| ADD kit scalars (bytes 1-36) | 36 x 6 = 216 |
| Bulk arrays per source | 64 soft + 64 loud + 128 formant + 512 envelope = 768 |
| Bulk arrays total | 4608 |

## 4. Stored, modulation and effective values

Three layers per parameter:

1. **Stored** value: lives in the image; changed by the user, automation, SysEx, preset load.
2. **Modulation offset**: computed each control block from the controller routings (bender, wheel, pressure, expression, assignable controllers 1 and 2, macros 1-4, the user knobs, LFO, the switch functions). It exists only for destinations that can be modulated (the 20 destinations: pitch, cutoff, level, vibrato/growl/tremolo depth, LFO speed, attack/decay 1/release time, velocity, resonance, pan, FF bias, FF env/LFO depth and speed, harmonics lo/hi/even/odd).
3. **Effective** value = clamp(stored + offset). Voices read effective values only.

Consequence: dragging a host knob changes stored; wiggling the mod wheel changes effective; exporting reads stored. The UI shows stored with an optional "modulated" indicator.

Switch functions (Harm Max, Bright, Dark, Saw, Select Loud, DHL 5th/Odd/Even, DHE#1/#2/Loop, DHF Max/Comb/Hicut/Comb2) are **overlays**: temporary replacements or additions to the effective harmonic arrays, never written to the image. Pressing the switch again removes the overlay. **[check]** whether the real unit writes them back when you press save in edit mode.

## 5. One entry point and origin tags

```cpp
enum class Origin { Host, Ui, MidiCc, SysEx, PatchLoad, Undo, Modulation /* not stored */ };

struct ParamEvent { ParamID id; uint16_t value; Origin origin; uint32_t sampleOffset; };

class PatchModel {            // owns the PatchImage; message/audio-safe via the queue below
public:
    bool  set(ParamID, uint16_t value, Origin);          // validates, writes bytes, notifies
    uint16_t get(ParamID) const;                        // stored
    void  loadImage(const PatchImage&, Origin);         // bulk, one notification
    const PatchImage& image() const;                    // for export
    // listeners: host bridge, UI, SysEx-out, undo, engine
};
```

Every listener ignores events whose origin is itself, which is how loops are avoided: a host automation write (Origin::Host) is not echoed back to the host; a SysEx write is not echoed to MIDI out; a UI edit is echoed to MIDI out only when "controller mode" is on.

## 6. JUCE integration

Not every parameter belongs in APVTS: 4608 bulk values and several hundred scalars are too many to be useful host parameters, and DAWs handle a few thousand parameters badly. Tiers:

| Tier | What | Where it lives | Count (target) |
|---|---|---|---|
| A: musical | master volume, per-source volume/pan, coarse/fine, cutoff/resonance, LFO speed/depths, DCA and filter times, formant bias/depth/speed, ADD total gain, harmonic lo/hi/even/odd macros | real `AudioProcessorParameter`s (an APVTS) | about 150-250 |
| B: assignable slots | a pool of generic host parameters ("Slot 1..64") the user maps to any ParamID | APVTS with a user mapping table saved in state | 64 |
| C: bulk | harmonic levels, formant bands, envelopes, names, effect and macro routings | not host parameters; edited in the plugin UI, over SysEx, or by loading a preset | rest |

State save: `getStateInformation` writes one `ValueTree`: the APVTS state (tier A and B), a **binary child with the full PatchImage** (the canonical state), the tier B mapping, the MIDI-learn table, and a version number. State restore: load the image first (Origin::PatchLoad), then set the tier A parameters from the image without re-triggering (guard flag), so nothing is applied twice and the image wins. The APVTS values are therefore views over the image, not a second copy of state.

Automation: host writes arrive on any thread as `AudioProcessorParameter` changes; they are pushed (Origin::Host) into a lock-free queue and applied at the start of the next block with their sample offset where available. Gestures (begin/end) are forwarded to the host for the parameters we send out.

Program/preset list: expose banks as JUCE programs; selecting one loads a `PatchImage` from the preset library (hardware factory banks, user banks), exactly like a SysEx dump or a file.

## 7. SysEx and MIDI I/O

### 7.1 Input
- A parser state machine reassembles messages across MIDI buffers, ignores everything that is not Kawai (0x40) or not our channel/machine byte, enforces a maximum length, and discards a message on any status byte other than F7.
- Handled message types, from the spec: parameter change (0x10), tone/patch dump (send), dump request, block dump with the tone map (sub1..sub19 bitmaps of included tones). **[check]** the full function list on spec pages 4-25.
- Each recognised parameter change becomes `PatchModel::set(id, value, Origin::SysEx)`. A dump becomes one `loadImage` or a bank import.
- The ADD MORF execute command calls the same function the UI uses.

### 7.2 Output / controller mode
- The unit does not transmit parameter changes (spec), so the plugin cannot mirror the hardware by listening. Two modes are needed: **push** (every local change is sent to the unit as a parameter change) and **pull** (a button or timer sends a dump request and loads the reply). Dump size for a single with ADD sources is about 1 kB per 82 + 86n + 806 per ADD source.
- A send queue with coalescing: later values for the same wire address replace earlier ones that have not left yet; large dumps are paced to the MIDI byte rate (31.25 kbit/s on DIN, higher over USB) so the host buffer never overflows; drag gestures are rate-limited (for example 100 messages per second per parameter).
- Device settings: channel, device id, port in/out, direction filters, "echo" off by default.
- Safety: never send until the user enables it, never send while loading a preset (bulk dump replaces it), bank writes require an explicit button.

### 7.3 CC and other MIDI
- CCs: the K5000 patch routes bender, wheel, pressure, expression, two assignable controllers, so these are modulation sources, not parameters. A separate MIDI-learn table maps any CC (and NRPN) to any ParamID; default table empty except the standard ones (1 mod wheel, 7 volume, 10 pan, 11 expression, 64 sustain, 120-123 mode messages).
- Program change and bank select: select the loaded bank slot.

## 8. Threads and real time

- Message thread: UI, file IO, SysEx build/parse of large dumps. MIDI thread (host): short messages only, push into the queue. Audio thread: pops events at the top of each block and applies them to the image copy the voices read.
- Voices read from a **snapshot**: a read-only copy of the image plus the derived tables (harmonic gains, formant gain curve, envelope parameters), rebuilt when a bulk change occurs, swapped atomically at block boundaries. Scalar changes patch the snapshot directly (single writer: the audio thread).
- Derived tables are recomputed lazily with a dirty mask per source (levels, formant, envelopes), so editing one harmonic doesn't rebuild everything. Changes that would click (level, formant) are interpolated over 16 to 32 samples.
- No heap allocation on the audio thread: fixed-size queue (for example 4096 events), pooled voices, preallocated tables.

## 9. Preset formats (the funnel into the image)

| Source | Importer | Notes |
|---|---|---|
| .ka1 (single) | direct | 82 + 86n + 806 per ADD |
| .kaa (bank of 128) | table + pointers | rules in K5000_FILE_FORMATS.md; base = lowest pointer - 0xE04 |
| .kba | consecutive singles | |
| .kca (64 multis) | MultiImage | no header, 64 x 103 B |
| SysEx dump | spec formats | single, block, bank |
| OS flash image | bank A/D extractor | from the original OS |
| Project state | binary image child | version checked |

Every importer validates: checksums (ADD block), source count 2..6, ranges per the registry; invalid bytes are clamped with a warning list, never silently altered on export except for the checksum.

## 10. Undo, dirty tracking, compare

`PatchModel::set` records `(id, old, new, origin)` into a bounded undo ring (coalescing a drag into one entry). `Compare` swaps the working image with the last loaded one. A dirty flag per source feeds the SysEx push mode (only changed areas are re-sent) and the save prompt.

## 11. Engine hooks this requires (checklist)

The additive voice and later the PCM voice must expose:
1. `applySnapshot(const PatchSnapshot&)`: bulk replacement (new preset), with a rule for sounding voices (finish with old, or crossfade) **[check hardware behaviour on program change]**.
2. `onParam(ParamID, value)`: scalar change with a dirty bit.
3. `setModulation(DestinationId, value)`: per-block controller values for the 20 destinations, per source or global.
4. `setOverlay(SwitchFunction, bool)`: the 16 switch overlays.
5. `noteOn/noteOff(note, velocity, sampleOffset)`, `controller(num, value)`, `pitchBend`, `aftertouch`, `sustain`, `programChange`.
6. `getVoiceInfo()`: active voices, for the UI and for voice stealing tests.
7. `render(buffers, numSamples)`.

Each parameter in the additive document, section 17, carries its ParamID so that, as each is implemented, its hook is the registry row and one line in the voice.

## 12. Tests that guard this design

- Round trip: for every file in the 21 + 12 banks and the OS flash banks, import then export gives identical bytes (apart from stale pointer words in .kaa).
- SysEx round trip: image -> parameter change messages -> image equals original for all scalar and bulk areas.
- Address table: each wire address maps to exactly one ParamID and back; all 176 scalar rows plus bulk ranges covered, no gaps.
- Fuzz: random bytes and truncated messages into the SysEx parser must never crash or alter the image.
- Host state: save, reload, save again produces the same blob.
- Origin loops: a SysEx write is not echoed to MIDI out; a host write is not re-applied.

## 13. Open questions

1. Behaviour of the unit when a parameter change arrives for a sounding note (immediate vs next note) per parameter class **[hardware]**.
2. Whether the unit answers dump requests with the current edit buffer or the stored patch **[check spec pages 4-25 and hardware]**.
3. The data hi byte of parameter changes: used for values above 7 bits only? For ADD envelope level bytes with the flag in bit 6, is the flag part of the value? **[check spec p.30]**.
4. Exact list of function codes, handshakes (ACK/NAK) and bank dump formats.
5. Which tier A parameters give the most musical coverage; this is a UI decision.
6. Whether to host the multi (4 sections, effects) in the same plugin from the start.

## 14. Immediate next steps

1. Generate `ParamID` and the registry from the schema CSV (scalar rows plus bulk ranges) as a C++ header and a Python module.
2. Write the Kawai address table as data and a test that checks it against the registry.
3. Port the Python voice's parameter reads to registry lookups (no behaviour change), so the C++ port can follow the same shape.
4. Prototype `PatchModel::set` with origins and a SysEx parameter-change round trip in Python before the C++.

## 15. Step 14 result (registry, wire table, PatchModel prototype)

Files: `schema/k5k_registry.py` (single source of truth; emits the C++ header), `schema/k5k_param_ids.h` (generated), `schema/patch_model.py` (prototype `PatchModel::set` with origins), `schema/test_registry.py` (13 checks, all pass on the 26 real patches in `newp/`).

**Size.** 203 scalar rows (81 common, 86 source, 36 ADD kit) + bulk (soft 64, loud 64, formant 128, harmonic envelope 64 x 9) = 1035 parameters per source; 5805 distinct ParamIDs over six sources (common counted once).

**Wire addresses (from the Kawai MIDI spec, checked against the page images, not just OCR).**
Message: `F0 40 ch 10 00 0A sub1 sub2 sub3 sub4 sub5 hi lo F7` (14 bytes). Value goes in `lo`; `hi` is 0 except for the wave word.

| Area | sub1/sub2/sub3/sub4 | sub5 |
|---|---|---|
| effect algorithm | 03/00/00/00 | 0 |
| reverb, effect 1-4 | 03/00/(1..5)/00 | 0 type, 1 dry/wet, 2-5 para1-4 |
| EQ | 03/01/00/00 | 0-6 freq1-7 |
| single common | 01/00/00/00 | 0-7 name, 8 volume ... 0x29 (names start at body byte 40) |
| source | 01/01/src/00 | = body index 0..85 |
| ADD kit scalars | 02/40/src/00 | = block byte - 1 (0 = morf flag ... 0x23 = LFO depth) |
| soft / loud / formant | 02/41, 42, 43 / src / n | 0 |
| harmonic envelope | 02/44/src/h | 0-7 stored fields, 8 = loop |
| morf HC code | 02/45/phase/h | 0 (not in the patch image) |
| morf execute | 02/46/src/00 | 0, data 1 (an action, not a parameter) |

**Findings that matter for the design**
- Wave is ONE wire word (sub5 0x1C, hi = msb, lo = lsb) but two stored bytes; wire sub5 equals the body index for every other source parameter. The model keeps msb and lsb separate and emits one combined message.
- Harmonic envelope loop is a composite on the wire (0 off, 1 LP1, 2 LP2) stored as two flag bits (bit 6 of decay1 level = S, bit 6 of decay2 level = T). The registry marks the loop parameter as virtual and the two level fields as 6-bit masked.
- `drum_mark` (body byte 39) has no wire address; names start at wire sub5 0.
- Effect parameters share a table per effect slot; the generic names (para1-4) are positional and the type-dependent meaning is still to be tabled.
- Real patches contain out-of-range bytes that the OS accepts (Methania has ADD LFO depth 127, spec range 0-63). Policy implemented: editor origins (UI, host, CC) clamp to the documented range; SysEx-in and preset loading keep any 7-bit value, so import/export stays byte exact.
- Kit parameters on a PCM source, or on a source that does not exist, are rejected by `set`.

**Behaviour verified by the tests**
- every body/block byte is owned by exactly one parameter (except checksums and the dummy byte);
- wire address -> parameter -> wire address is a bijection for all 5804 addressable parameters;
- each real patch can be dumped as parameter-change messages and replayed into a scrambled copy, giving a byte-identical image (checksums recomputed);
- edits from the UI coalesce to one outgoing message (last value wins); SysEx-in never echoes back out.

**Still to do:** morph HC-code area (not stored), effect-type tables, port the voice's reads to registry lookups (step 3), C++ implementation of `PatchModel` (the Python is a prototype only), and verification against a real unit (the spec says the unit does not transmit parameter changes, so receive behaviour is untested).


## 16. Effects and reverb (stubs from the OS)

Source: the OS image (K5000S/R 4.04). UI text is stored with the high bit set. Three tables, all in the OS file (offsets are file offsets, the image is mapped at 0x100000):

- `0x82DFB` 38 label records of 19 bytes (18 chars + 0x1E): 34 parameter names, then DRY/WET 1, DRY/WET 2, DRY/WET, Delay Level.
- `0x82D0B` 48 layout records of 5 bytes, one per type byte 0..47: [wet/level label, slot 1..4 labels], `0xFF` = slot unused. This is the whole per-type parameter layout. Extracted by `schema/extract_effects.py`, shipped as `schema/k5k_effects_os.json`.
- `0x830D4` the 48 type names (OS spelling kept, including its typos "Eerly REF.2" and "Celsete & D").

Stored order in the common block: reverb = type, dry/wet 1, dry/wet 2, 3 parameters; effect n = type, dry/wet (or delay level), 4 parameters. On the wire these are sub5 0 (type), 1 (wet), 2-5 (slots), which is why the wire has "para1..para4" for reverb while the block names (`reverb.drywet2`, `reverb.para2..4`) were positional placeholders. Type 0-10 = reverb, 11-47 = effect (37 effects; the manual says 36). Ranges below are the stored byte range.

| Type byte | OS name | wet/level | slot 1 | slot 2 | slot 3 | slot 4 |
|---|---|---|---|---|---|---|
| 0 | Hall1 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 1 | Hall2 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 2 | Hall3 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 3 | Room1 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 4 | Room2 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 5 | Room3 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 6 | Plate1 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 7 | Plate2 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 8 | Plate3 | DRY/WET 1 0-100 | DRY/WET 2 0-100 | REV. Time (S) 0-50 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 9 | Reverse | DRY/WET 1 0-100 | DRY/WET 2 0-100 | Feedback 0-100 | Predly Time (mS) 0-100 | HI FREQ. Damp 0-100 |
| 10 | Long Delay | DRY/WET 1 0-100 | DRY/WET 2 0-100 | Feedback 0-100 | Delay Time (mS) 0-127 | HI FREQ. Damp 0-100 |
| 11 | Early REF.1 | DRY/WET 0-100 | Slope 0-100 | Predly Time (mS) 0-100 | Feedback 0-100 | – |
| 12 | Eerly REF.2 | DRY/WET 0-100 | Slope 0-100 | Predly Time (mS) 0-100 | Feedback 0-100 | – |
| 13 | TAP DLY1 | Delay Level 0-100 | DLY Time 1 (mS) 0-127 | Tap Level 0-100 | DLY Time 2 (mS) 0-127 | Feedback 0-100 |
| 14 | TAP DLY2 | Delay Level 0-100 | DLY Time 1 (mS) 0-127 | Tap Level 0-100 | DLY Time 2 (mS) 0-127 | Feedback 0-100 |
| 15 | Single DLY | Delay Level 0-100 | D.Time Fine (mS) 0-9 | D.Time CRS (mS) 0-127 | Feedback 0-100 | – |
| 16 | Dual DLY | Delay Level 0-100 | DLY Time L (mS) 0-127 | Feedback L 0-100 | DLY Time R (mS) 0-127 | Feedback R 0-100 |
| 17 | Stereo DLY | Delay Level 0-100 | Delay Time (mS) 0-127 | Feedback 0-100 | – | – |
| 18 | Cross DLY | Delay Level 0-100 | Delay Time (mS) 0-127 | Feedback 0-100 | – | – |
| 19 | Auto Pan | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Wave 0-1 |
| 20 | Auto Pan & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Wave 0-1 |
| 21 | Chorus1 | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Wave 0-1 |
| 22 | Chorus2 | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Wave 0-1 |
| 23 | Chorus1 & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Wave 0-1 |
| 24 | Chorus2 & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Wave 0-1 |
| 25 | Flanger1 | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Feedback 0-100 |
| 26 | Flanger2 | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Feedback 0-100 |
| 27 | Flanger1 & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Feedback 0-100 |
| 28 | Flanger2 & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Feedback 0-100 |
| 29 | Ensemble | DRY/WET 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | – | – |
| 30 | Ensemble & D | DRY/WET 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | – | – |
| 31 | Celeste | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | – |
| 32 | Celsete & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | – |
| 33 | Tremolo | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Wave 0-1 |
| 34 | Tremolo & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Wave 0-1 |
| 35 | Phaser1 | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Feedback 0-100 |
| 36 | Phaser2 | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Predly Time (mS) 0-100 | Feedback 0-100 |
| 37 | Phaser1 & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Feedback 0-100 |
| 38 | Phaser2 & D | DRY/WET 0-100 | Speed 0-100 | Depth 0-100 | Delay Time (mS) 0-100 | Feedback 0-100 |
| 39 | Rotary | DRY/WET 0-100 | Slow Speed 0-100 | Fast Speed 0-100 | Acceleration 0-100 | Slow/Fast 0-1 |
| 40 | Auto Wah | DRY/WET 0-100 | Sense 0-100 | FREQ. Bottom 0-100 | FREQ. Top 0-100 | Resonance 0-100 |
| 41 | Bandpass | DRY/WET 0-100 | Center FREQ. 0-100 | Band Width 0-100 | – | – |
| 42 | Exciter | DRY/WET 0-100 | EQ Low (dB) 0-24 | EQ High (dB) 0-24 | Intensity 0-100 | – |
| 43 | Enhancer | DRY/WET 0-100 | EQ Low (dB) 0-24 | EQ High (dB) 0-24 | Intensity 0-100 | – |
| 44 | Overdrive | DRY/WET 0-100 | EQ Low (dB) 0-24 | EQ High (dB) 0-24 | Output Level 0-100 | Drive 0-100 |
| 45 | Distortion | DRY/WET 0-100 | EQ Low (dB) 0-24 | EQ High (dB) 0-24 | Output Level 0-100 | Drive 0-100 |
| 46 | Over. & D | DRY/WET 0-100 | EQ Low (dB) 0-24 | EQ High (dB) 0-24 | Delay Time (mS) 0-100 | Drive 0-100 |
| 47 | DIST. & D | DRY/WET 0-100 | EQ Low (dB) 0-24 | EQ High (dB) 0-24 | Delay Time (mS) 0-100 | Drive 0-100 |

**Range provenance** (`schema/k5k_effects.py`, per label): `manual` where the manual states it (dry/wet 0-100, single delay fine 0-9 and coarse 0-127 = 0-1270 ms in 10 ms steps, predelay up to 100 ms, Long Delay 200-1470 ms in 10 ms steps); `observed` = the largest value seen in 891 unique patches; `inferred` where combined (the "& D" delay time: manual max 200 ms, stored max 100). **Not yet verified:** the display scale of reverb time (stored 0-50), the stored-to-ms scale of the 720 ms delays and tap delays (stored 0-127), the EQ dB centre (stored 0-24, centre 12 assumed), which wave is 0 (SIN/TRI), and exact effect algorithms. The OS display formulas are in code, not in a table, so they are a separate job.

**Findings**
- The OS does not clear slots a type does not use. 107 patches carry stale non-zero bytes in unused slots (e.g. Ensemble slots 3-4). Keep them on round trip and ignore them in the voice.
- Two stored values outside even the generous range: Enhancer EQ High = 72 and Chorus2 Wave = 20 (one patch each); the OS accepts them.
- Tremolo & D is the only type never used in the corpus; the other 47 are.
- Labels 19 and 20 are both "Intensity" (Exciter and Enhancer have their own label).

Files: `schema/extract_effects.py`, `schema/k5k_effects_os.json`, `schema/k5k_effects.py` (Python API: `layout`, `slot_range`, `type_name`), `schema/k5k_effects.h` (generated), `schema/test_effects.py`.


## 17. Registry gaps closed (Multi, arpeggio, morph, lists)

Files: `schema/k5k_registry.py` (extended: areas MULTI 9, MSECT 10, ARP 11, MORF 8; `mode`, `virtual` and word helpers), `schema/k5k_lists.py` (enums), `schema/test_multi.py`. The registry now holds 1174 parameters per source slot (single 1035, multi common 54, multi section 14 x 4, arpeggio 7, morph harmonic code 64 x 4 phases), 6169 distinct wire addresses.

**Multi (K5000S/R "Combi").** Record = checksum + 54 common + 4 x 12 section bytes = 103. Common bytes 1-38 are the single's effect/EQ layout, 39-46 name, 47 volume, 48 section mask, 49-54 two effect-controller slots. Wire: `04/00/00/00/sub5` for name (0-7), volume (8), then mute/section mask at **0x0A** (0x09 is skipped), controllers 0x0B-0x10. Effect, reverb and EQ parameters of a multi use the same `03/xx` addresses as the single's, so a wire address only resolves given the current edit mode; the registry keys wire lookups by mode (`single`, `multi`, `any`). Section wire `04/01/section/00/sub5`: tone word 0 (hi = bank msb, lo = lsb), volume 1, pan 2, eff path 3, transpose 4, tune 5, zone lo 6, zone hi 7, velo-sw type 8, velo-sw value 9, receive channel 0x0A.
- The wire splits the velocity switch into type and value, but the stored byte is packed (bits 6-5 type, bits 4-0 threshold/4), so the two wire params are **virtual composites** (`velosw_type`, `velosw_value`). `velosw_value` is lossy (4-step resolution; 125-127 all store 31).
- Stored section byte 10 is unused (always 0, no wire address); the spec lists 12 entries with type/value separate where the data has one packed byte plus the unused one.
- Spec/data discrepancies: the multi effect type is documented as 0-36 but real data holds 11-47 like the single; the multi GEQ is documented as 0-127 but data and the single use 58-70 (64 = flat).
- 586 unique real multis: names, tone ids and section fields read through the registry offsets match the loader. A handful of known-corrupt records hold bytes outside the declared ranges (GEQ, controller slots, section mask high bits); the section mask is read with `& 15` by the OS.

**Arpeggio (sub1 0x60, K5000S/R).** on/off `60/00` (0 or 127), speed `60/10` (receive only), tempo `60/11` (10-300 as msb 0-2 + lsb), mode `60/20` (0-10), pattern `60/30` (hi = pattern 0-10, lo = variation 0-7). Transient: not in any patch image. Words are modelled as a hi param plus its lo partner (`partner(p)` = the next index), the same rule as the source wave word and the multi tone word.

**Morph.** HC-code area `02/45/phase/harmonic/00` = 64 params x 4 phases, transient (loaded by the morph patch, not stored in the image). `ACTIONS['morf_execute']` = `02/46/src/00/00` with data lo = 1.

**Lists** (`k5k_lists.py`): control source (14), destination (20; the macro list is the same list), effect destination (10), switch/foot switch (17), arpeggio mode (11) and pattern (11), plus the small enums (LFO wave, poly, pan type, velocity switch type, sweep source, loops, KS pitch, DCF mode). Each parameter is bound to its list with `enum_for(p)`; the test checks every list length against the range of every parameter bound to it.

**System settings:** the MIDI spec has no address space for system (global) settings, so there is nothing to register; the OS keeps them in battery RAM, to be mapped separately from the OS if we want a global-settings codec.

Tests: `test_registry.py` (13), `test_effects.py`, `test_multi.py` (10) all pass.

## 18. Codecs, scheduler and emulator validation

### 18.1 Codec layer (`schema/k5k_codecs.py`, tests `test_codecs.py`, 22 PASS)
Pure functions, no JUCE, no UI. Raw bytes are canonical; import lenient (returns Issues), export strict (CodecError on `structure` problems; `range` problems are reported but allowed).
- Files: `decode_file/encode_file` for KA1, KAA (bank A/D pointer base), KBA, KCA. Round trips are byte-exact for all 26 `newp/` KA1, 100 factory KA1, KCA, KBA; KAA decode->encode->decode gives identical patches.
- SysEx: single dump `F0 40 ch 20 00 0A 00 bank slot <KA1 bytes> F7`; multi dump `...20 00 0A 20 slot <103 B>`; block dump `...21 00 0A 00 bank <19-byte tone map> <patches in slot order>` (tone map byte i bit j = slot 7i+j, confirmed on the OS reply: 12 full bytes + 0x03 = 86 patches, first patch = slot 0); dump requests (`00` single/multi, `01` block), ID request/reply, acks 0x40-0x45, mode 0x31/0x32; `decode_sysex` returns dicts and `unknown` (with reason) instead of raising; `split_stream` pulls complete F0..F7 frames out of a raw MIDI byte stream.
- Finding: real KCA banks contain bytes > 127 (stale high bits, e.g. multi 7 has 9). They are legal in a file, illegal in SysEx: `encode_multi_dump` refuses unless `mask=True`.
- The OS's own single-dump reply decodes with a valid checksum and is byte-identical to the patch that was sent in.

### 18.2 Output scheduler (`schema/k5k_scheduler.py`, `test_scheduler.py`, 10 PASS)
Coalesce by wire address (a 100-step knob sweep sends one message), pace to 3.125 B/ms minus 10 % (+2 ms gap), params superseded by a dump, bulk fallback after 24 distinct dirty params, dumps wait for ack 0x40 with a 1 s timeout, never more than 4 messages per tick. Time is passed in so it is testable; the C++ port is a lock-free SPSC queue + timer thread.
Not measured yet: how long the OS needs per parameter apply (the emulator is not cycle-accurate for wall time). The 2 ms gap is a guess.

### 18.3 Emulator validation of the wire table (`schema/emu_check/`)
Method: boot the real OS in the emulator, load a patch by dump, send one parameter change per registry parameter, run N steps, diff DRAM against a no-op control run.
- Must pump the TX interrupt (`raise(14)` every 2000 steps) or the OS never answers; with it, ID request, one-patch dump request and block dump request all reply.
- Single-mode edit buffer: common + source bytes are at 0x322F70 + image offset. **173/173 common and source parameters tested land exactly at the offset the registry predicts**, nothing at any other offset (two exceptions: name7/name8 set to 0 were ignored; `pan.value` 0 is stored as 1).
- ADD parameters are queued first (events at 0x36AC5E) and applied later: need ~30M steps, not 3M. A second full patch image copy at 0x35DDAB is not touched by edits.
- ADD kit block: 36-byte slots at **0x364E97 + 36·src**, indexed by kit parameter index. 39/45 kit parameters verified exactly; `morfN.patch` shares a byte with its source, `morf.loop` 2 reads back 1, `total_gain` 0 is stored as 1 (floor of 1).
- Soft / loud / formant / harmonic-envelope values do NOT go to DRAM: they are written into kram (0x880 + 2·idx for soft, 0x900 + 2·idx for loud, ... value stored doubled; harmonic envelopes become rate/level pairs near 0xA00). Exact layout: see 18.4.

### 18.4 Kram layout (live sound-parameter RAM), all four ADD sources
Measured with 144 parameter changes (soft/loud/formant/henv at several indices, sources 0-3); `registry.kram_address/kram_apply`, `test_kram.py` 144/144.
One 0x400-byte slot per ADD source, slot s at 0x400*(s+1) (0x400, 0x800, 0xC00, 0x1000):

| offset | content | address |
|---|---|---|
| 0x000-0x07F | not touched by any wire parameter (unexplored) | |
| 0x080 | soft[64] | +0x80 + 2i, value*2 in the even byte |
| 0x100 | loud[64] | +0x100 + 2i, same |
| 0x180 | formant[128] | +0x180 + (i xor 1), one byte, value*2 (neighbours swapped) |
| 0x200 | harmonic envelopes[64] | +0x200 + 8i, four big-endian 16-bit words: attack, decay1, decay2, release |

Envelope word = `flag<<15 | level(6 bit)<<9 | rate(7 bit)<<2`. A rate write changes the low 9 bits and leaves level/flag alone; a level write only bits 9-14. The flag bit of words 1 and 2 is the loop composite (see 18.5); words 0 and 3 kept 1 and 0 in every test. Cross-check: the test patch's soft/loud/formant/envelope values equal the kram contents (soft 127 -> 254, 71 -> 142, formant 111 -> 222, envelope default word 0xFFCC = flag, level 63, rate 115).
Meaning for the new engine: the real OS pre-computes the doubled/packed form, so the voice code can read them directly; our own voice should do the same unpacking at the boundary.
Open: bytes at 0x000-0x07F of each slot; the odd byte of soft/loud words; how a full patch load fills this RAM (we only observed edits).

### 18.5 Harmonic-envelope loop (measured, and a correction)
Flags live in word bit 15 of the decay1 word (S, file: bit 6 of the d1-level byte) and the decay2 word (T, bit 6 of the d2-level byte). Sending the composite loop parameter (wire 02/44/src/h/8) gives, from any starting state (10 sequences tried, history does not matter):

| wire value | S (decay1 flag) | T (decay2 flag) |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 1 | 1 |
| 2 | 0 | 1 |

Earlier the model used 1 = T only, 2 = S+T. The OS says the opposite, so `PatchModel` now follows the OS (`test_loop.py`). **Names resolved:** the open-source `ksynth` library (K5000 editor code written from the Kawai spec) decodes (S=1,T=1) as Loop1 and (S=0,T=1) as Loop2, which is exactly the OS wire order 1 = LP1, 2 = LP2. So the wire numbering `0 off, 1 LP1, 2 LP2` was right and the older reading in `addkit.py` (S=0 -> LP1) was the swapped one; fixed (`AddKit.loop`, ADDITIVE_ENGINE notes). Manual meaning: LP1 loops between the D1 and D2 levels at the D2 rate; LP2 loops between them using both D1 and D2 rates. Side effect: the prototype voice (`addvoice.py`) now applies the D1-rate branch to the other set of patches than before. Kram +0x000-0x07F carries the 36-byte common fields (gain, group, morf, bias, FF env, LFO), written by code at 0x166148, not yet decoded.

### 18.6 Plugin state blob (`schema/k5k_state.py`, `test_state.py`, 12 PASS)
What the host stores with a project. Tagged chunks, so new fields never break old projects:
`K5SB | u16 version | u16 flags | chunks | CRC chunk`, each chunk `tag(4) | u32 LE length | payload`.
- `PTCH` = the current single as real KA1 bytes (the saved state is a valid patch any tool can read), `MULT` = current multi (103 bytes), `SETT` = small JSON (mode, bank/slot, MIDI channel, push-to-hardware flag, port names, UI page).
- Unknown chunks are kept and rewritten; a newer version loads the chunks it knows and reports a warning; a damaged or truncated blob never raises, it returns what could be read plus a list of issues; encode is strict (refuses a structurally invalid patch).
- Not saved: undo history, meters, scheduler queue, anything derivable from the patch (host-automation parameter values are read from the patch through the registry on load).
- Migration rule: bump `VERSION` only when an existing chunk changes meaning; adding chunks or JSON keys needs no bump.

## 19. PCM sources (assumption: sample format = K11/GMega)

**Patch side (all covered).** A PCM source uses the same 86-byte source block as an ADD source; the only PCM-specific field is the wave word (hi at body byte 28, lo at 29, wire `01/01/src/00/0x1C`). Wave value = wave number - 1 (0..463 PCM, names in `k5k_waves.py` from the Sound Diver list); value >= 512 = additive source (806-byte block follows). Emulator check (`emu_check/sweepw.out`): the wave word lands at the predicted bytes for sources 0-2; the emulated board clamps values 0-340 up to 341 because the update OS has no main wave set (an emulator artefact, not a wire rule); values >= 341 are stored verbatim.

**Sample side (assumption, user decision 2026-11-06).** The K5000 PCM ROM is taken to use the same format as the K11/GMega wave ROM: one signed 16-bit little-endian linear sample per ROM word, no compression, one sample per word. K11 evidence: the 2 MB KP069A dump read this way gives clean audio (periodic tails, autocorrelation > 0.9), and the big-endian read is clearly worse (`test_pcm.py`). K5000 evidence is indirect: the OS wave table gives start/loop/end as word addresses and root pitch, with cyc waves fixing the natural speed at 22050 words/s at the root note. **Not confirmed**: no KP170 dump exists. All format specifics live in `k5k_pcm.ASSUMED` so a real dump can overturn them in one place.
- `k5k_pcm.py`: `PcmRom` (loads bytes/file), `decode_entry/encode_entry` (8-byte sample entries, flags byte kept opaque; 123 OS-resident entries decode to the recorded addresses and re-encode byte-exact), `speed_ratio` (2^((note-root)/12)), `render` (linear interpolation, loop..end, seamless loop of whole periods).
- Still open for PCM: the wave -> sample-entry map for waves 1-341 (not in the OS file), loop-flag semantics (flags byte), interpolation quality of the real chip, velocity/key zones within a wave (zone maps), DCF/DCA behaviour on top (shared with ADD, deferred).

### 19.1 Decision (2026-11-06): PCM comes from SoundFonts, not the original ROM
The original KP170 ROM will not be used, so the ROM sample format, the 22050 words/s speed rule and the 256-word loop granularity are no longer on the critical path (`k5k_pcm.py` stays as an optional ROM reader, `ASSUMED` table unchanged). The PCM voice instead reads what a SoundFont already carries per sample: sample rate, loop start/end (sample-accurate), root key + pitch correction (cents), plus per-zone key/velocity ranges, tuning, root override and loop mode (`sampleModes`: none / loop / loop-until-release). Pitch ratio = (sample_rate / output_rate) * 2^((note + coarse/fine - root)/12).
What the original ROM did hold: a table with start/loop/end/root per sample (the 8-byte entries; 1,478 of them, E4 = loop, E5 = end), plus zone maps per wave; the OS image has them for waves 342-464 only. So a K5000 patch's wave number is only a name; the synth needs a **wave map**: wave number (1-464) -> SoundFont instrument/sample, built by name match (`gmega_k5000_name_match.csv`, `sf2k5k/`) and user override, with a fallback sound for unmapped numbers. Patches stay 100 % compatible; the sound depends on the loaded SoundFont.
Open: the wave-map file format and UI; how the noise-attack waves (342-464) layer with their main waves; DCF/DCA on PCM sources (shared with ADD).

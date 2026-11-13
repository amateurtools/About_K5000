# K5000S multi mode (manual pp.97-104) [M] unless marked

- A multi is up to four single patches (sections): layers, key splits, velocity splits, multiple MIDI channels, or combinations. 64 multis (bank menu M00..M63).
- Multi Play shows per section: patch name, MIDI channel, transpose, volume (F1..F8 = transpose/volume of sections 1..4). Transpose in semitones, volume 0..127.
- Common: Name (8 chars), Volume (master volume of the multi). Effect: the multi has its own effect setup (algorithm, E1-E4, reverb, GEQ) [I: the sections' singles contribute no effect settings; consistent with the .kca common block using the single's layout].
- Section (x4): Single patch (bank+number), Sec Volume, Sec Pan (shown as nnL/R, 0x40 centre), Effect Path (routes the whole section to one of the four effect inputs), Receive Ch, Transpose, Fine (tune). Zone Lo / Zone Hi (note range), Velo Sw (OFF / Loud / Soft) with Value = the MIDI velocity threshold.
- A section can be muted with its F key (display "Sec1" -> "S--3"): stored in the section flags byte (bit i = section i+1 enabled).
- Macro / assignable controls work in multi mode but only affect the SELECTED single patch (and others on the same MIDI channel); they add to/subtract from that patch's own values. The arpeggiator settings likewise only affect the selected section (and others on the same channel).
## OS probe, 2026-11-07 (emulated OS, multi 0 of the factory bank, key 20, note on ch 0) [I]
- Reads of the working multi buffer (0x322DEB) at note-on, by code address: 0x118076 multi volume [47]; 0x1180AE section volume (+2); 0x10E630 transpose (+5); 0x10E646 tune (+6); 0x112DEC pan (+3); **0x112DFC effect path (+4)**; 0x11A042/56 bank/patch (+0/+1); 0x120F8E.. enable/zone/velocity switch; 0x12340E receive channel (+11).
- 0x112D76 builds the per-note values; when the UI mode byte (0x31F61C) is 8 or 9 (multi play) it takes pan = section[+3] & 0x7F and **effect path = section[+4] & 3**, stores the path at 0x3188B6 (code 0x112F70); in the other modes it takes the path from the source's own byte (0x319964 image, via 0x112F88). So in multi mode the SECTION's effect path replaces the sources' own paths. **I** (implemented that way in Synth::PartParams.pathOverride).
- Effect bytes 1..38 and the controller slots (49..54) are NOT read at note-on; the effects block is sent when the multi is selected (earlier finding: bytes [2..29]).
- Still open [?]: what the two effect-control slots do (destinations 0..9 look like Effect 1..4 Dry/Wet and Para); pan law; the Fine unit (assumed 100/64 cents per step like the source fine tune); section byte +10.

## Engine status
engine/voice/multi_synth.h: four Synth parts + one FxRack; effects from the multi's common block; gate, transpose, tune, volumes, path override as above. 

Manual p.61: "When in Multi mode, the effects settings of the Single patch are disregarded, and the Multi effects settings are used instead" [M]. Effect Controller details: K5000_EFFECTS.md.

## Plugin side (no UI yet; the UI will be redone later)
- core/patch/multi_library.h: MultiLibrary holds single banks by number (msb 2 = A, 3 = D, 4+ = battery RAM banks; a file named A.kaa / D.kaa registers itself, other files via assignLastBank) and the 64 multis; resolve(multi) -> MultiSnapshot (multi bytes + four resolved singles). Sections whose bank is not loaded, whose slot is empty or whose id is a ROM id (msb 0/1, not resolvable in this OS) stay silent. Test: all enabled sections of all 64 factory multis resolve once A.kaa and D.kaa are loaded.
- PluginProcessor (uncompiled, JUCE): loading a .kca loads the multi bank and enters multi mode; selectMulti(i) / setMultiMode(bool) / multiName(i) / currentMulti() / unresolvedSections() / assignLastBank(msb) are the message-thread API for the future UI. Audio thread: the MultiSnapshot arrives through the same try-lock mailbox, MIDI is channel-aware in multi mode (note, sustain, all notes/sound off per channel), Effect Controller messages (bender, pressure, CC) feed the multi; the other engine is silenced when the mode flips. Choosing a single patch (selectBank) leaves multi mode.
- Not done: state save/restore of the multi and the banks (the state blob still holds only the single patch), program change to select a multi, per-section pan, panel-style per-section mute/transpose/volume edits (the Multi Play F-keys), editing and writing multis, SysEx multi dump in/out, section-wise macro/arp routing ("selected section" concept).

# K5000S arpeggiator

Status tags: S manual, OS seen in the OS string tables, M measured, ? guessed, - not done. Sources: owner's manual pp. 19, 108-112; OS string table (image offset 607300); registry (wire 60/xx).

## What the instrument has
- Panel: ON button, Pattern, Mode, Speed knob [S]. System > Arp pages: common parameters and an edit page with 32-step tables; 8 user arpeggio memories (Write) [S].
- Time base 96 pulses per quarter note. Note values q 96, q! 64, e 48, e! 32, x 24, x! 16, r 12 [S]. Clock INT or EXT [S].
- Common parameters: Note value, Gate %, Level %, Range Lo / Range Hi (C-2 .. C3 in the tutorial), Play Mode (REST / LAST / FIRST / ALTERNATE), Velocity on/off, Clock [S].
- Edit page: Mode (PATTERN / TRIGGER / GATER), Total Step (up to 32), Key Order (KEY ON / LOW->HI), Note value; Pattern Edit: Step, Level (0 = rest, max = repeat), Gate %, Pan ("---" = none), NT1..NT4 (which keys play on the step) [S].
- Mode knob, 11 values [OS]: 1-Oct, 2-Oct, 3-Oct, Hold 1/2/3 Oct, Hold Random, Hold & Trig 1/2/3 Oct, Hold & Trig Random. Pattern knob, 11 values [OS]: Up, U/D 1, U/D 2, Down, Key Order, Random, Chord Trigger, Chord Gate, Seq pattern 1, Seq pattern 2, User.
- In a multi the arpeggiator affects only the selected section and others on the same channel [S].

## Wire (registry, area ARP)
On/off 60/00, Speed 60/10 (receive only), Tempo 60/11 (10-300, msb 0-2 + lsb), Mode 60/20, Pattern 60/30 hi, Variation 60/30 lo (0-7). Seven parameters. **Everything else above (note value, gate, level, range, play mode, velocity, clock, the user step tables, the 8 user memories) has no wire address in our registry.** Either the real unit does not expose it over parameter change (likely, it is saved to disk as an arpeggio file and may travel in a bulk dump), or the MIDI implementation pages we hold do not list it. Open: find the arpeggio dump / file format (probe the OS disk "Save > Arpeggio" and the dump handler).

## Engine (engine/arp/arpeggiator.h, tests/arp_tests.cpp, 30 checks)
Done: sample-accurate clock from tempo and note value; Up, Down, Key Order, Random; U/D 1 (end notes once) and U/D 2 (end notes repeated) [? which is which]; octave range 1-3 and random octave; Hold (latch, a new chord replaces it); Retrigger (restart at step 1 on a new chord) [?]; Range (keys outside play normally); Gate %; Level % and velocity sensitivity; step-table patterns (Chord Trigger, Chord Gate, Seq pattern, User) with rests, repeat, NT1..NT4 and the four Play Modes; switching off sends note-offs. Wired into SynthFx (setArp); events land at their sample offset inside the host block, so the audio does not depend on the block size.

Not done / open:
- The preset step tables of Chord Trigger, Chord Gate, Seq pattern 1 and 2 (and what "Variation" 0-7 selects) are in the OS, not found yet. Placeholder: ascending 8-step table. Next probe: find the table through the pattern-name string table at image 607300 and the code that reads it, or play each preset in the emulated OS and record the note events.
- U/D 1 vs U/D 2 wording, Retrigger exact rule, REPEAT (level max) meaning, Hold Random meaning, Speed knob (0-127) to tempo mapping, EXT clock (host tempo and transport) [?].
- Step pan is stored but not applied to the voice yet.
- Multi: arpeggiator on the selected section / channel only (MultiSynth has none yet).
- Panel controls and System > Arp pages need UI (deferred), and the 8 user memories need a file format and state save.

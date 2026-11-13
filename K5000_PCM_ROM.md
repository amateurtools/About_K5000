# K5000 PCM ROM (KP170): addressing, sample entries, loop flags

# PCM ROM (KP170) addressing - from Stellaris K5Hle::command (0x134E20) and the PCM read path in K5Hle::next (0x130A8E)
TG "voice go" command (write 0x48 to 0x080000, voice no. to 0x080003) reads the voice record E1..E5 and decodes (as Stellaris does):
  hi      = (E1 & 0x1F) << 16            (E1 bit 6 = byte select within the 16-bit word; Stellaris uses it XOR env K5_PCM_SWAP)
  start   = hi | (E3 << 8) | E2          word address (21 bits => 2 M words = 4 MB = 32 Mbit KP170)
  loop    = hi | (E4 << 8)               256-word granularity, same 64 K page as start
  end     = hi | ((E5 << 8) + 0x100)     256-word granularity (Stellaris ORs, so E5=FF wraps - treat end as hi + (E5<<8) + 0x100 when exporting)
Playback in Stellaris: sample(pos) = ROM[ ((byteSel XOR swap) + 2*(start_word_offset + pos)) ] as ONE byte (signed, or unsigned with K5_PCM_U8), linear interpolation, loops between loop and end.
=> Stellaris only uses 8 of every 16 bits; the real chip's word format is NOT confirmed (the real data may be 16-bit or compressed). Needs a real KP170 dump to settle.
kp170.bin: read from the ROM folders, max 4 MB (0x400000); no file => synthesized "stand-in" waves (pcm_spectra.tsv).
For wave values 0..340 the OS gets "not available". CORRECTED (2026-11-05): K019 at 0x050000 is the KEYBOARD SCANNER, not a PCM interface (see section below). The zone-map pointers for waves 1-341 (array at 0x170C70) exist but all point at stub maps (mode 0, sample-entry index 0xFFFF = "not available") inside the 0x172A3C-0x172CC3 region; the real wave-to-sample map for waves 1-341 is not in K5000SR.SYS. Where it comes from (boot ROM, patched at boot, or read through the tone generator host bus HADRS/HDATA) is NOT yet traced.
Decoded OS-resident waves (values 341..463 = waves 342..464, e.g. "Piano Noise Attack", "Omnibus Loop 8"): pcm_wave_addresses.csv (start/loop/end per wave).
These waves are the attack transients and loops, i.e. what the SoundFont import must generate/replace.

# PCM sleuthing: sample rate / samples per cycle (derived from the OS wave table only)
Wave table: 8-byte entries at 0x1FC000 (>=1700 entries, wave records for 1..464 plus layers/variants): [flags][E1][E2][E3][E4][E5][T6][T7].
 T6,T7 = root pitch: semitone = T6 + T7/256 (MIDI-like units, 69 = A440).  Voice record at note N: E7:E6 = (N + 62 - T6) semitones - T7/256 (E7 = 122 - T6 at N = 60; E6 = fraction).
Single-cycle ("Cyc") waves: 256-word cycles carry T6=0x28 (40), 512-word cycles T6=0x1C (28): exactly one octave (12 semitones) apart.
Root frequency of the 256-word cycle = 440 * 2^((40+0xC3/256-69)/12) ... with fine byte read as -(T7/256 mod 1) -> 86.1 Hz.
 86.1 Hz * 512 = 44.1 kHz. So the 256-word cycle holds 512 samples at 44.1 kHz, OR 256 samples at 22.05 kHz - equivalent playback speed; ambiguity = 1 sample per word at 22.05k vs 2 samples per word at 44.1k.
Cross-check: waves whose root is 59.76 (Crystal Cyc, 256 words) and 64.76 (Syn Saw3 Cyc) fit exactly 3 and 4 periods per 512 samples at 44.1 kHz (258.4 and 344.5 Hz).
Therefore: durations are exact: seconds = words / 22050 (two samples per word at 44.1 kHz). The ROM (2M words) = ~95 s.
Still unknown: bits per sample (e.g. 8-bit x2 per word vs 16-bit at half rate vs packed/compressed).

# PCM sample-entry flag byte (byte 0 of the 8-byte entries at 0x1FC000) - and ROM backwards compatibility
Layout recap: [flags][E1][E2][E3][E4][E5][T6][T7]; 1,478 entries. Byte 0 takes only 4 values: 0x00, 0x20, 0x40, 0x60 (bit 5 = 0x20, bit 6 = 0x40).
Confirmed (code + emulator):
- OS routine at 0x10EC0C builds the entry pointer 0x1FC000 + 8*(index-1), stores it at 0x31499C. The four routines that use that pointer
  (0x113182, 0x113482, 0x11664E, and the code around 0x11692A) read only bytes 6,7 (root pitch). A second table at 0x1FEE30 (8-byte entries) is selected by a different mode (0x10EBAC).
- The OS does NOT interpret byte 0. It is copied verbatim into voice-record byte E0 (0x0900E0). Checked on 122 resolved waves of the 137-wave sweep (w_full.json):
  entry 0x40 -> E0 0x40 (105), entry 0x00 -> E0 0x00 (17); 1 ambiguous wave excluded.
- Stellaris (K5Hle::command, 0x134E20) decodes only E1..E5 for the sample, and reads E0 only as (E0 & 3). It never uses bits 5 or 6, so those bits are
  handled by the tone-generator hardware (or are unused by Stellaris), not by OS or emulator code.
Inferred from data only (NOT confirmed):
- Bit 6 (0x40) correlates with loop type: entries with bit 6 clear (0x20, 0x00) have a whole-sample loop in 372/414 and 200/205 cases (90%, 98%);
  entries with bit 6 set (0x60, 0x40) have a whole-sample loop in 83/591 and 19/268 (14%, 7%) and otherwise a tail loop (loop start about 82-86% into the sample).
  ~8-10% exceptions, so it is a correlation, not a rule.
- Bit 5 (0x20) is set for entries 1-1005 and clear from 1006 on. Entries 1006+ are used by waves 342-464; entries with bit 5 set belong to the main waves 1-341
  (not resolvable from the OS update file). Likely main-set vs auxiliary-set (region/bank).
Backwards-compatibility rule for the new synth / ROM tools:
- Treat byte 0 as an opaque "hardware flags" byte: store it as-is in the sample-entry structure, pass it through unchanged on export, never derive behaviour from it.
- The loader must read the same 8-byte entry (flags, E1-E5 = hi/byte-select, start, loop, end, T6/T7 = root pitch) so an original KP170 ROM dump can be loaded later
  with no format change; soundfont-generated waves get a flags value chosen by loop type (bit 6 set for tail loop, clear for whole-sample loop) as a best guess.
- The loop points themselves are E4 (loop, 256-word granularity) and E5 (end), not the flag bits.
To confirm on a real unit: play a bit-6-set wave and a bit-6-clear wave of similar length and compare the loop transition (tail loop vs whole-sample loop).

# K5000S OS internals (OS 4.04, verified in the FR30 emulator)
Emulator sources are in `core/`. Run notes: `RXICR=11` is required for MIDI RX delivery.

## MIDI input
- UART0 RX interrupt is ICR11 (vector 27 -> 0x103E54), NOT ICR04.
- RX ISR (0x122FF8) writes bytes into an 8 KB ring buffer at 0x36AE58..0x36CE57 (head idx @0x36D910, tail idx @0x36D36E/0x36D36C).
- Pop function 0x12E898 (byte -> @R4). Bytes >0x7F inside a message abort the parse (0xF8 clock is skipped).
- SysEx patch parser: 0x12CFA0.

# Note-on -> tone generator (Step 3, patch A001 "Runner", 3 sources; emulator log)
Per source (voice) the OS (code 0x113230..0x113462) fills a command record in TG1 dual-port RAM at 0x0900E0..0x0900FA,
then triggers it with: write 0x080000 = 0x48, write 0x080003 = voice index (0,1,2 = source number).
It also fills a 7-byte voice record at 0x090000 + 7*voice (voice 0 at 0x090000..06, voice 1 at 0x090007..0D, voice 2 at 0x09000E..14),
re-written continuously every refresh tick by code at 0x1151E4..0x115262 (envelope/pitch update loop).

Record 0x0900E0.. for note 3C vel 7F (voice0 / voice1 / voice2):
 e1 00 00 09 | e5 01 01 be | e6 29 51 2b | e7 35 35 30 | e9 40 00 40 | ea 00 00 01 | eb 68 68 5c | ec 91 91 ff
 ed fa fa f2 | ee ff ff ff | ef fa 68 f2 | f1 70 70 ff | f2 7f 7f 53 | f3 70 70 7f | f4 01 01 53 | f5 01 | e0 40 | f6 81 | f7 8e | f8 8a | f9 5b | fa d0 2a 7d

Differential tests (changing only note or velocity):
- e7 = pitch: note 30/3C/48 -> 29/35/41  (+12 per octave, = note - 7). e6, eb, ed, ef track note too (eb: 59/68/77, +0x0F per octave).
- velocity affects: e9 (0x40 -> 0x00 below a threshold), ec/ed/ef (0x91,0xFA -> 0x00 at vel 0x40), f2 and f4 (vel 7F/40/10 -> 53/43/23), plus voice2's ed (F2/DF/B9).
- Fields independent of note/velocity (patch/voice data): e0=40, f5=01, f6=81, f7=8E, f8=8A, f9=5B, ee=FF.
Meaning of each field still to be mapped onto patch parameters (do by changing patch parameters in the dump and diffing).

# Voice-record sweep (all-additive 1-source test patch "ADDTEST1", built from first source of A010 K-Bells)
Each of body bytes 1..167 (except the name) changed one at a time; voice record 0x0900E0..FA read after note 3C/7F. See voice_record_sweep.csv.
Only 40 of 160 bytes change that record; the rest (envelopes, LFO, most of the harmonic data) reach the chip through other writes
(the 0x090000..14 refresh block and the ADD blocks) - NOT yet swept.
Source-0 byte -> record byte (empirical): src3->E0; src4,15,60,67->F2/F4; src8->EB/ED/EF; src10,14,18->E6/E7; src26->FA;
src30,34->E7; src31->E6; src42-49->E9..EF group (DCF/amp level group); src55->EC; src61->F1..F4; src75->F5; src76->F9; src77->F6; src78->F7; src79->F8; src82,83->F0.

# Additive (ADD) data path (Step 3b)
- Changing ADD-block bytes (body 169..973) changes NOTHING in the TG1 dual-port RAM / voice record. K018 (ADD chip) registers at 0x070000 and K019 at 0x050000 are not touched during dump/program change/note-on in this run.
- The additive data lands in the K018 parameter RAM, which the CPU sees as "kram" at 0x800000..0x81FFFF. Perturbing every ADD byte changes 910 bytes of kram,
  all in 0x0000..0x07FF: 0x000-0x001, 0x400-0x5FF (looks like per-harmonic level/fine pairs, 192+192 bytes) and 0x600-0x7FF (repeating 8-byte records, 256+256 bytes).
- Raw ADD bytes are NOT stored verbatim in kram (8-byte substring search found nothing); the OS converts them. The converter (code that writes 0x800400+, see 0x165F1C region for the boot-time init) is the next thing to decode.
- kram is rewritten continuously (>90k writes per 50M steps at 0x80048..), so a write log is too big; diff snapshots of kram instead.
- Files: kram_base.bin / kram_mod.bin in scratchpad = snapshots for the unmodified and all-ADD-bytes-modified patch.

# ADD block -> K018 parameter RAM conversion  (DECODED and verified byte-for-byte against the emulator)
Code: patch-select loader at 0x11FE24..0x11FF1A, run once per source k = 0..5 (called on program select; not on note-on).
B = the source's 806-byte ADD block as stored in the patch (B[0] = its checksum). K = kram (CPU address 0x800000), 16-bit big-endian words.
  base(k) = 0x800400 + k*0x400
  1. B[1..36] (36 bytes: 19 HarmonicCommon + 17 Formant) are copied to work RAM 0x364E98 + 36*k (used by the OS, not the chip).
  2. K[base+0x80 + 2*i]   = B[37+i] << 9                                   for i = 0..127   (128 harmonic levels, 7 bit left-aligned)
  3. K[base+0x180 + 2*j]  = (B[166+2*j] << 9) + (B[165+2*j] << 1)           for j = 0..63    (64 x 14-bit values)
  4. K[base+0x200 + 2*m]  = (B[294+2*m] << 9) + (B[293+2*m] << 2)           for m = 0..255   (256 words = 64 records of 4 words)
       and when (address mod 8) == 0, i.e. m % 4 == 0:  = (B[294+2*m] << 9) | ((B[293+2*m] << 2) + 0x8000)   (bit 15 set marks the first word of each record)
  (806 = 1 + 36 + 128 + 128 + 512 + ... check: 37+128=165, 165+128=293, 293+512=805.)
  Verified: 128/128, 64/64, 256/256 words match the emulator's kram for the test patch.
Not yet explained: kram base+0x000..0x07F (written by code at 0x166148, from the 36-byte common/formant part) and kram 0x000-0x001; the continuous writer at 0x111386/0x1113B4 (a refresh/clear loop).

# PCM source (Step 3c) - test patch PCMTEST1 (one PCM source from A017 Pyper; note 3C vel 7F)
- PCM wave select = source bytes 28,29 (hi,lo 7-bit): wave value = hi*128+lo (1-based wave number = value+1).
- Voice record bytes E1..E5 = first 5 bytes of an 8-byte wave record from an OS-resident table starting at 0x1FDF61 (records end in 0x40 for the first 186; the table continues to at least 0x1FE5E1+).
  E6/E7 depend on the played note (pitch) and the record's remaining bytes.
- Wave values 341..463 (= the 123 waves numbered 342-464, the "extra" ones) resolve from that table. Values 0..340 return the "not available" marker
  (record c0 ff ff ff ff ff, i.e. E0=C0) in this emulator: their data lives in the PCM ROM (KP170), which we do not have.
- Mapping is NOT one record per wave in index order (table has gaps/multi-records); the empirical mapping is in pcm_wave_records.csv.
- The all-PCM patch produces a different K018/kram image than the additive one (685 bytes differ) - the PCM path leaves the ADD harmonic area at defaults.

# K019, boot ROM and chip map (service manual KMI-062 + OS code), 2026-11-05
- K019 (K019-FP, RF5C345) = keyboard scanner. Code 0x11981C-0x11988A polls 0x050003 (bit7 set = key event ready), reads key number from 0x050002 and velocity from 0x050000 (&0x7F), pushes pairs into a ring buffer at 0x3197D4 (index 0x3197D2/D3, overflow flag 0x3197D1). Control writes: 0x050004 <- 5/0x85, 0x050005 <- 5 (code 0x1199F0-0x119A28). Parts list: fitted on W and S, not on R (rack) -> consistent. Field meanings beyond this are inferred.
- Boot / flash-update code outside K5000SR.SYS: the OS calls 0x0C130C (takes a 64 KB-aligned address), 0x0C13B6 (called with chunk sizes 0x8000 / 0x10000, path at 0x121EB8-0x121F40) and 0x0C1734 (init, 0x103B22), and jumps to 0x0C0000 (0x103D3E). Likely flash erase/write and OS-update loader. This region is not in the update file and is not modelled by our emulator (which starts at 0x100410). Probable home: the CPU-bus mask ROM U5 (KP169 on W, KP174 on S/R). Unverified.
- During a 60M-step boot the OS reads nothing from unmapped ranges except a memory test at pc 0x13B5EE/0x13B64E (addresses 0x4000.., 0x400000.., 0x7F3F00..). The CPU-bus mask ROM is not read at boot.
- Main-set sample entries: 1,005 entries (flag bit5 set) are the samples for waves 1-341; 473 entries (bit5 clear) belong to waves 342-464. Of the 1,005, 165 are byte-select pairs (same start/loop/end, E1 bit6 differs). Rising-pitch run segmentation gives ~150 runs (not 341), 233 distinct single-cycle samples vs 75 cyclical waves in the manual: the wave-to-entry map cannot be rebuilt without the KP170 audio. See main_set_entries_grouped.csv.
- Chip map: see k5000_chips.csv / k5000_chips.xlsx (U1 FR30, U2/U3 flash, U4 DRAM, U5 mask ROM, U11 K015 TG1, U16 DP1, U18 K018 ADD, U23-U25 MN19412A effects DSPs, K019 keyboard, FDC HD63266F). Effects DSPs are controlled over three serial (SCL/SDA) pairs from CPU pins 143-148.

# Note-on call path (traced 2026-11-05, tool core/nt.cpp; RXICR=11 is required for MIDI RX delivery in the emulator; note fed at step 200M after bank injection)
Measured sequence for one note-on (90 3C 7F), steps after the byte arrives (call = FR30 CALL, caller->callee):
 - 0x10E452 / 0x10E468 / 0x10E480 are called thousands of times (2215 in 3M steps) around shared-state access: almost certainly interrupt-mask/critical-section wrappers (unverified).
 - Main-loop task slot 0x100530 -> 0x12979E (MIDI input handler), at about +16.5k steps: 0x129A6A -> 0x12E770 -> 0x120F0C -> 0x121030 -> 0x11A8F8 (channel/status decode) -> 0x119FFE -> 0x121980 -> 0x11C01E -> 0x11BA6C -> 0x11B748, which calls 0x11AEC8, 0x11B138, 0x11B17A, 0x119E30, 0x11A494 (note-on handling: voice allocation and per-source setup; not yet disassembled). Repeats 3x in the trace = one pass per source (Runner has 3 sources).
 - Voice start queue consumer 0x11A1FA (called from 0x11A284 context): two ring queues at 0x319729 / 0x31972B (read/write indices), entries at 0x31972D + 32*queue + index. Each entry is a voice slot number 0..31. Voice slot struct = 0x31CC64 + 0x11C*slot (284 bytes). It calls 0x10E874 for each entry.
 - 0x10E874 = per-voice/source start. R4 = slot, struct fields used: +2 (==1 selects the ADD path vs PCM), +4/+7/+0x18/+0x20/+0x28, +0xC (==1 second kind), +0x3C = pointer to the patch in memory, +0x102/+0x103, +0xD2.., +0xF2... Portamento/pitch step: it subtracts the previous note from the new one (<<6 / <<7 / <<8 depending on a mode byte at patch+0x21) and stores the 16-bit result at 0x3179B2 + 2*slot, sign sets 0x3188D3+slot to 1/2 (glide direction).
 - PCM branch (struct +2 != 1): zone-map lookup = 13-byte record at (zone map base from struct+0x20) + 13*R4; the 13 bytes are copied to 0x3188A9; bytes 0,1 form the sample-entry index; pointer to the 8-byte entry = 0x1FC000 + 8*(index-1), stored at 0x31499C. (ADD branch uses table 0x1FEE30 + 8*halfword@+0x28.)
 - Then per-voice stages: 0x112D76, 0x11664E (reads entry bytes 6,7 = root pitch), 0x117F0A, 0x116E88, 0x113182 (writes the TG1 command record 0x0900E0.. and 'go' = 0x48 to 0x080000 plus voice index to 0x080003). Called from 0x10ED82. 0x164E60 handles the ADD/formant copy (0x364E98 + 0xD8*part + 0x24*source = the 36-byte common/formant block).
NOT yet found: where the per-source key range (Zone Lo/Hi) and velocity switch are compared. They are not in 0x10E874 or in the 0x11A1FA queue consumer, so they are applied earlier, in the voice-allocation stage (0x11B748 and its callees 0x11B138/0x11B17A/0x11AEC8). Next step: disassemble those and confirm by editing zone/velocity bytes in a test patch.
- Voice allocator 0x11B748: tries in order 0x11AEC8, 0x11B3F0, 0x11B434 (candidate pickers, presumably free voice / releasing voice / steal oldest; unverified), then 0x11B138/0x11B17A to link the chosen slot, marks the slot struct (+1 |= 0x08, +14 = 4) and queues it for 0x11A1FA. A second variant at 0x11B862 handles slots 0x20+ (slot table at 0x31976E, voice struct stride 0x11C). 32 voice slots (0x1F mask) plus an upper bank. Zone/velocity gating still to be found in its callers (0x11AC84 -> 0x121980, 0x11AE32 -> 0x11C01E -> 0x11C09A).

# Zone selection and map format (decoded 2026-11-05, OS 0x10E742-0x10E7F4; spec in sf2k5k/SF2_TO_K5000_SPEC.md)
- Zone index per mode (note = integer key): mode 1: <36 ->0, >95 ->6, else (k-36)/12+1; mode 2: <30 ->0, >101 ->13, else (k-30)/6+1; mode 4: <25 ->0, >104 ->21, else ((k-25)>>2)+1; mode 8: <24 ->0, >107 ->29, else (k-24)/3+1; other modes: zone 0.
- Zone record (13 bytes) = [idx hi][idx lo][11 adjust bytes, neutral 0x80]; map = [mode byte] + 13*zones.
- All sample entries loop: one-shot "Attack" waves end in a 256-word tail loop. start/loop/end share one 64K-word page.

## Multi mode, panel model and multi note gate - CONFIRMED by emulation (2026-11-05)
- Panel scan: routine 0x13B982 (called each scan tick). Column index at 0x36DC2F (0..6), column strobe written to on-chip port 3, row bits read back from port 3 at pc 0x13BA00 (active low); a changed bit generates an event at 0x146BB4 with R4 = key id = col*8+row, R5 = 1 press / 0 release. The emulator can press a key by clearing bit (id%8) of R1 right after the LDUB at 0x13BA00 while the column == id/8 (core/mk3.cpp).
- Key id 20 (column 2, row 4) = MULTI button: it copies multi 0 from the bank (0x367298) to the working multi buffer at 0x322DEB (103 B) and switches the OS into multi mode. 0x31F61C is only a UI page/state byte (8/9 = multi play pages), not the mode.
- Note-on gate for multi sections = function 0x120F0C (R4 = section, R5 = note, R6 = velocity), called from the MIDI note path (129A6A -> 120F0C). It reads the WORKING buffer 0x322DEB, not the bank:
  * section enabled: buffer[48] AND table[0x170A38 + section] = 1,2,4,8 (bit i = section i+1; bits 4-7 unused).
  * zone: lo = buffer[55+12*sec+7], hi = [..+8]. If lo <= hi: play when lo <= note <= hi. If lo > hi the zone WRAPS (play when note >= lo OR note <= hi).
  * velocity switch byte [..+9]: type = b & 0x60 (0x20 loud, 0x40 soft, 0 or 0x60 = off); threshold = ((b & 0x1F) + 1) * 4, with 0x1F -> 127. Loud plays when vel >= threshold, soft plays when vel < threshold.
  * receive channel = [..+11], 0-based, compared with the MIDI channel nibble (0x12340E). A note on a non-matching channel never reaches the gate (verified: ch 1 note on a multi whose sections are on ch 0 reads none of bytes 48/62/63/64).
  * Section byte +10 is never read in the note path (still unknown).
- Other bytes read at note-on from the working buffer: volume [47], section instrument bank/patch [+0,+1], volume [+2], pan [+3], effect path [+4], transpose [+5], tune [+6]; at multi select time the effects block [2..29].
- Tools: core/mk3.cpp (fast key-press + DRAM dump), core/mk2.cpp (key press + note + read-watch of buffer/bank; slow, 30 s).

## Bank save-to-flash and instrument id resolution (2026-11-06)
- 0x11D0E4-0x11D240 (bank A) and ~0x11D3D2-0x11D52C (bank D): for the RAM bank at R11, rebase 0x381 pointer words (ptr - (R11 - flash)), flash-write 0x20E04 bytes to 0x1A0000 / 0x1D0000 via 0x121D94, rebase back. The OS image's flash area therefore holds one copy of each bank (A at file 0xA0000, D at 0xD0000); see K5000_FILE_FORMATS.md.
- Instrument id lookup for multi sections 0x10F96E-0x10FB1A: id = msb*128+patch; reject id <= 0xFF; upper limit 0x200, or 0x300 when byte 0x318963 != 0; table index = (id - 0x100 - 0x80*k) * 0x1C into: 0x34407C (0x100-0x17F), 0x323278 (0x180-0x1FF), 0x200010 (0x200-0x27F), 0x220E14 (0x280-0x2FF); anything else null. The first pointer word of the entry is the patch pointer; a zero pointer is an empty slot.

## UI strings and effect tables (2026-11-06)
UI text in the OS is stored with the high bit set (ASCII | 0x80), concatenated, no terminators; the pool is around file offsets 0x82C00-0x84400. Effects: 38 labels at 0x82DFB (19 bytes each), 48 per-type layout records at 0x82D0B (5 bytes each), type names at 0x830D4. See K5000_PARAMETER_ARCHITECTURE.md section 16.

## Edit-buffer map (from emulator sweeps, see PARAMETER_ARCHITECTURE section 18.3)
- 0x322F70 + image offset: single-mode working buffer (common + sources); 0x35DDAB: second full image copy (loaded patch, not touched by edits).
- 0x364E97 + 36*src: live ADD kit block per source. kram 0x880/0x900/...: live soft/loud/formant/harmonic-envelope tables (value x2).
- 0x36AC5E / 0x36D7EC: pending-parameter event queue (80 02 <sub2> <src> <idx> ...); 0x31F44C/D: last ADD edit (src, index).
- UART TX needs ICR14 raised periodically in the emulator.

## Patch load / init behaviour (probed in the emulated OS, 2026-11)

- Power-on with empty SRAM and with a used SRAM give the same edit buffer: a ROM default patch named `PowerK5K`, 5 sources.
- Loading a single dump (and selecting it with a program change) writes the 82 common bytes and the first n source blocks. Every source slot >= n is then **overwritten with one fixed default source block** (86 bytes, `007f1000790200004000400359...`, saved as `tools/newp_default_source.bin`). Verified after loading a 6-source patch followed by a 2-source patch: slots 2..5 are the default, not stale data. So the OS does init-then-overwrite for source slots.
- Edits only take effect after a program change has selected the patch; a parameter change sent right after a dump without it is queued but not applied to the edit buffer.
- Changing a wave across the PCM/ADD boundary by SysEx parameter change changes only the two wave bytes. No ADD block is created, nothing in kram changes. Whatever is in the ADD storage stays; there is no default sine written by this path.
- Wave edits via SysEx: values 0..340 are clamped up to 341 (emulator artefact: the update OS has no main wave set, see ARCH 19), 341..511 are stored verbatim, any value >= 512 is stored as exactly 512 (the ADD marker; msb 4, lsb 0). Not a remap.
- Open: where the ADD harmonic data of the current patch lives in the working RAM (it is not contiguous after the source blocks of the edit buffer).

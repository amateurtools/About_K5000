# K5000 file and SysEx formats
Reader/writer: `k5kfile/k5kfile.py` (Single/.KA1, SingleBank/.KAA, load_kba/.KBA, MultiBank/.KCA). Schema: `schema/` (k5000_patch_params.csv, k5k_patch.h).
All formats round-trip byte-exact on the factory data.

| File | Contents |
|---|---|
| .KA1 | one single: 82 B header + 86 B x n sources + 806 B per ADD source (ADD = wave value >= 512, source bytes 28/29) |
| .KAA | OS single-bank image: 128 x 28 B pointer table (patch ptr + 6 ADD ptrs), end ptr at 0xE00, data from 0xE04, 134660 B |
| .KBA | packed singles, no table (walk by source count at body[51]) |
| .KCA | multi bank: 64 x 103 B records, no header |
| .lib | Emagic Sound Diver library (MacBinary II wrapper around an IFF `SSLB` file); recognized, NOT decoded |

## SysEx patch dump  F0 40 ch 20 00 0A 00 <bank> <slot> <body> F7
- Bank byte: 0 = bank A, 2 = bank D (verified 2026-11-06: stored in the A / D slot table; slot byte 0-127 = table slot). Bank bytes 1, 3, 4 are silently ignored on the emulated K5000S OS (nothing stored, no reply). (Internal index = slot, +0x180 for D.)
- Body byte 0 = checksum over body[1 .. 82+86*n) ONLY (seed 0xA5, &0x7F).  header+sources.
- n = body[51] (source count). Header is 82 bytes, sources 86 bytes each.
- Each ADD block (806 bytes) carries its OWN checksum in its byte 0: (0xA5 + sum(block[1:806])) & 0x7F.
  (For a source flagged ADD - flag = byte 28 of source >= 4 ... to be confirmed).
- A bad checksum makes the OS silently drain the rest of the message (no reply).

## Commit (0x134A5C -> 0x11E8FC for slot < 0x80)
- Slot table base 0x34407C (stride 28 bytes); end-of-data pointer at table+0xE00; bank A data limit = 0x344E80+0x20000.
- If new_size - old_size does not fit: error 0x0B ("memory full"), message drained, nothing stored.
- Otherwise: old patch removed by sliding later data down, all later table pointers reduced by old size, slot's 7 words zeroed,
  new header+sources appended at the END of the bank, word0 = its address, ADD blocks appended after and their addresses go into words 1..6.
- A flag byte 0xFF is written at 0x31F473+slot (probably "modified/needs refresh").
- Verified: dump TESTPTCH to A001: slot0 -> 0x364448, other slots shift down, end ptr 0x364BE8.

## .KCA multi bank format (2026-11-05, from user's EmptyMul.KCA + banks/M.kca (64 factory multis, supplied with the A/D bank files))
- File = 64 records x 103 bytes, no header. Byte 0 = checksum, same rule as singles (0xA5 + sum(b[1:]), &0x7F). All 64 factory records pass.
- Common (55 bytes): [0] cksum, [1] effect algorithm 0-3, [2..7] reverb block (6 B, layout unverified), [8..31] effects 1-4 x 6 B (init: 0B 00 00 05 09 00), [32..38] graphic EQ 7 bands (0x40 flat), [39..46] name (8), [47] volume, [48] section flags (low nibble = sections in use; high bits seen set in 1 factory multi, unverified), [49..54] two effect-control slots x 3 B (source, dest, depth; depth centre 0x40, assumed).
- Sections: 4 x 12 bytes from offset 55: bank_msb, patch(lsb), volume, pan(0x40=centre), effect path (0-3), transpose(0x40=0), tune(0x40=0), zone lo, zone hi, b9 = velocity switch, same packing as the single's velo_sw (bits 6-5: 0 off / 1 loud / 2 soft, bits 4-0 = threshold/4); evidence: factory data has types 0 (225), 1 (18), 2 (13) and never 3, and the off-state default 0x13/0x14 = threshold 76/80; b10 always 0 (unknown); b11 = receive channel-1 (inferred: values 0/1/3 only in multis 42,53,54,56,61, in the pattern 0,0,1,3 across sections). Section flags byte 48: bit i = section i+1 enabled (DreamHrn has 0b111 but data in section 4).\n- Multi RAM in the OS: bank at 0x367298 (64 x 103 B). SysEx multi dump accepted: F0 40 ch 20 00 0A 20 <slot 0-63> <103 B body incl. checksum> F7 (verified: stored at 0x367298+103*slot). Dump-request messages tried (00 00 0A 00 .. / 00 00 0A 20 ..) produced no reply. Emulator note-on in the default (single) mode reads none of it; (the multi mode switch was found later: panel key 20, see K5000_OS_INTERNALS.md; zone/velsw/channel are confirmed there).
- Section instrument id = bank_msb*128 + patch (decoded from the OS patch-select code at 0x11A02E-0x11A164, which turns the id into a bank pointer): ids 0-255 (msb 0 and 1) = NOT RESOLVED by this OS (2026-11-06: the check at 0x10F998 rejects every id <= 0xFF, there is no ROM preset space; see 'OS image flash banks' below); msb 2 = user bank A (table 0x34407C); msb 3 = user bank D (table 0x323278); msb 4 = a further bank in battery SRAM (table 0x200010); msb 5 and up = another SRAM bank (table 0x220E14). CORRECTION: an earlier note here said 2 = D; that was wrong. NB the single-patch SysEx bank byte uses a different numbering (0 = A, 2 = D).
- Common block bytes 1..38 of a multi use exactly the single patch's common layout (1 fx algorithm, 2-7 reverb, 8-31 effects 1-4 as type/depth/para1-4, 32-38 graphic EQ); all 64 factory multis fit the single's ranges except the corrupt record noted below. Bytes 49-54 are two controller slots: source (list of 14: Bender, Channel Pressure, Wheel, Expression, MIDI Volume, Panpot, ...), destination (list of 10: Effect 1 Dry/Wet, Effect 1 Para, ... - values seen 0-9), depth (0x40 = centre).
- EmptyMul init section: vol 127, pan 0, tune +1, zone 1..126 (differs from the zone 0..127 / tune 0 seen in factory data).
- Tool: k5kfile/k5kfile.py (Single/.KA1, SingleBank/.KAA, MultiBank/.KCA; byte-exact roundtrip; all 100 factory KA1 and both KAA banks pass).

## .KBA single-patch bank file (2026-11-06, from user's Bbanksfx.kba)
- No header, no pointer table: consecutive single patches, each 82 + 86*n bytes (+806 per ADD source, none here). Walk it using the source count at body[51]. Sample: 47 patches x 254 B (all 2 PCM sources) = 11938 B, all checksums valid.
- Contents: sound-effect patches (Rain, Thunder, Dog, CarCrash, Siren, Lasergun, ...); patches 34-47 are "Untitled" fillers on wave 266 (Sine). Wave values match the Sound Diver wave-name list index (209 Castanet, 229 Rain 2, 236 Dog, 245 CarCrash, 246 Siren, 259 LaserGun).
- Origin/slot mapping unknown (name suggests bank B SFX). Related containers: .KA1 = one single; .KAA = OS bank image (128-slot pointer table, 134660 B); .KCA = 64 x 103 B multis.

## .KAA export rules (verified 2026-11-06 in the emulated OS)
- File = 0xE04-byte table + 0x20000-byte data area = 134660 B (exactly the RAM bank region; bytes after the end pointer are stale padding - zeros are fine).
- Pointers in the file are RAM address MINUS 0x10: bank A data starts at file-ptr 0x344E70 (RAM 0x344E80), bank D at 0x32406C (RAM 0x32407C). Table RAM bases: A 0x34407C, D 0x323278. End pointer at table+0xE00.
- Per slot: word 0 = patch (header + sources, 82+86n B); words 1..6 = ADD blocks INDEXED BY SOURCE NUMBER (word 1+k is the block of source k, 0 for PCM sources) - not by ADD ordinal. Each ADD block is 806 B, normally stored right after its patch, in source order.
- Real files are slot-ordered, and sometimes leave a 2-byte gap after the last ADD block of a slot (seen after slots 9,19,24,29,... of A) - harmless: a packed image without gaps is accepted.
- Test: SingleBank.dump('A'/'D') re-packs the factory banks; injected into the emulator, the OS validation keeps all 100 patches byte-identical and a test note gives identical tone-generator commands to the original files.
- Note: the older tracer core/nt.cpp copies only 0x20084 bytes of a bank file when injecting (truncates the last ~3 KB); core/kinj.cpp copies the whole file.

## What the OS does with out-of-range values (hard check, 2026-11-06, 501 emulator runs)
Method: core/vt.cpp + chk/hard.py - one parameter at a time set to its documented limit and beyond (limit+1 and 127; lo-1 and 0), written to A001 by SysEx, program change, two notes. Per run the tone-generator command records, dual-port RAM and the first 2 KB of ADD parameter RAM are fingerprinted. Full table: docs/K5000_VALUE_LIMITS_REPORT.txt. Scope: the OS only - what the TG/ADD chips do with a value is not observable here.
- No run reset the CPU, left OS code, or hung; notes still played after every case. The OS checks only: SysEx framing/7-bit bytes, checksum(s) and length. It does NOT range-check parameters (even factory patch A016 Rasty holds source4 wave_msb = 63, outside 0..4).
- 116 restricted parameters tested: 26 are clamped to the limit, 16 pass the out-of-range value through (undefined/extrapolated behaviour: source effect_path>3, wheel/express dest1>19, pan.type>3, dco.fine=0, dco.fixed_key>108, dcf.bypass/mode>1, dcf.level>7, dca.velo_curve>11, dca.velo_level>63, lfo.waveform>4, lfo.fade_speed>63, lfo.vibrato/tremolo depth>63, poly>2), 71 show no effect at TG/ADD-RAM level for this patch and note (not proof they are harmless - e.g. effects/reverb parameters go to other hardware).
- Structural rules that DO change behaviour: src_count must match the data (a count that disagrees with body length or checksum gets the patch rejected); wave msb >= 4 turns the source into an ADD source, which needs an ADD block (otherwise rejected); drum_mark != 0 gives no normal voices; src_mute is a source-ON mask (bit set = source sounds; 0 or only bits >= 6 silences everything); poly=2 (SOLO2) does not retrigger on the second note.
- Export rule for our tools: Single.problems() in k5kfile.py reports structural problems (reject) and range problems (undefined); the exporter should clamp to the schema ranges.

## Cross-bank evidence for the multi format (2026-11-06)
- All 187 enabled sections of the 64 factory multis resolve to a patch in our A or D dump (msb 2 -> A, msb 3 -> D): e.g. Halo = A Chimera + D FloorSaw + D Fantapol, Yin&Jan = A DooBass + A Launcher + A Tweeky.
- Junk bytes: pan byte = 0x80 appears only in DISABLED sections (multis 19, 20, 32, 41) - stale memory, ignore. Record 8 (BraStack) has many bytes > 127 in the common block (GEQ 32-38, flags 48-50): a corrupt/unsanitised record; the OS note gate only uses the low 4 bits of byte 48. Such records cannot be sent as SysEx (data bytes must be <= 127) - an exporter must sanitise them.
- Still no variation in the data for: section byte 10 (always 0), msb 4+. (msb 0/1 turned out to be unresolvable on this OS, see below.) These cannot be learned from diffs of these banks.
- The K5000S manual says banks B and C belong to the K5000W; the S has A and D.

## Cross-bank evidence for the .KAA layout (2026-11-06, 21 bank files, 1179 patch instances, 905 unique)

Sources: factory A/D (`3_ABANKINT`, `2_DBANKINT`) plus 19 user banks (Sound Diver / hardware saves: AbankR3, A-Coll2, Collect2, Collctn1, Aerostal, A-Leiter, A-sascha, arpeggi1, Arpeggio, bass1, bass2, BassLead, brass1, British, Classic, D-Kenji, Drums, drums1, EAJ-09).

Layout rules confirmed on all 21 files (every patch and ADD block passes its checksum except one, see below):
- Data always starts at file offset 0xE04 and the end pointer (0xE00) is exact: no gaps (a 10-16 byte tail in 3 files: both factory banks and AbankR3), no pointer shared between slots.
- **The pointer base is not constant.** Seen: 0x34406C (factory A), 0x34407C, 0x323268 (factory D), 0x323278, 0x326E00, 0x3249DE, 0x34A50C, 0x34924E, 0x3527C4/0x356C64, 0x355B4C ... Base = lowest pointer - 0xE04. A reader must take the base from the file, never assume A or D.
- **Slot 0 is not necessarily first in memory.** Data is packed in edit order; the lowest pointer is the first stored patch. Only the pointer order inside a slot (patch, then its ADD blocks) is fixed.
- **ADD pointer words are indexed by source number (word k+1 = source k).** Words for PCM sources, or for sources >= n, are STALE: left over from earlier edits, never cleared by the OS, and they can alias unrelated live data (bass2 slot 2: stale words point exactly at slot 3's body and its first ADD block). Compaction relocates every non-zero word by the same delta, so they keep aliasing. A reader must follow the source flags (wave >= 512), not the non-zero pointer words. A reader that counts non-zero pointers fails on 4 patches in 3 files (bass2, drums1, brass1).
- Source count n is 2-6 in every real patch (n=1 never occurs in a bank).
- Capacity: usable data is 0x20000; BassLead (0x1FD60) and factory A (0x1FD58) are essentially full.
- Our repack (load -> dump -> load) reproduces all 1179 patches byte for byte in every file (21/21), including banks whose layout differs from our canonical one.

Value findings from the same corpus (schema ranges were otherwise right: 0 structural problems, 1 out-of-range value in 1179 patches, the known factory "Rasty" wave_msb 63):
- Effect block can be fully blank: effect1-4 type 0, all seven GEQ bands 0, fx controller and macro depths 0. 24 of 60 patches in A-Leiter, plus some in drums1, EAJ-09, bass1, Drums, D-Kenji. Real hardware stored and accepted these, so 0 is treated as seen, not an error, for effect types, GEQ bands, fxctl depth and macro depths (previous schema said 11-47, 58-70, 33-95).
- Effect type values used in the wild: 11-19, 21-33, 35, 36, 39-45, 47 (never 20, 34, 37, 38, 46). Reverb 0-10 all used, fx algorithm 0-3 all used, poly 0-2 all used, AM 0-2.
- One ADD block has a checksum that does not match its content (bass2 slot 7 "SIDBass1", source 2). It came out of a real bank, so some save path (or Sound Diver) does not fix ADD checksums; our reader keeps the block as is and `valid()` flags it.
- Duplicates: 274 of 1179 patch instances are repeats of patches in other banks (collections overlap heavily).


## Cross-bank evidence for .KCA (2026-11-06, 12 files, 768 multis, 586 unique)

Files: factory `1_MBANKINT`, `EmptyMul`, plus ArpgioCM, ASL-1CMB, BsLdCmb, CBank300, ClassCmb, Colect2c, Drums, K5KV3MLT, PadCpCmb, SESSN-C.
- Every file is exactly 6592 bytes (64 x 103), no header; all 768 multis pass the checksum.
- **Section byte 10 is 0 in all 3072 sections: unused, keep it 0.**
- Bank MSB (section byte 0) seen: 0, 1, 2, 3 only (770 / 231 / 1742 / 329 sections). 2 = user bank A, 3 = user bank D as established; 0 and 1 do not resolve on the K5000S OS. CORRECTION (2026-11-06): these counts include disabled sections; enabled sections only: msb0 248, msb1 194, msb2 1103, msb3 325 (442 of 1870 = 24% use 0/1, mostly third-party sets such as CBank300, SESSN-C, PadCpCmb; EmptyMul defaults to msb 0).
- Rx channel (byte 11) seen 0-3 and 9 (drum kits on channel 10).
- Volume of the multi (byte 47) 38-127; effect algorithm 0-3; section volume byte 2 0-127.
- **Pan (section byte 3):** enabled sections use 1-127 with 64 = centre (1446 of ~1900 are 64). The value 128 (not a legal 7-bit byte) appears in 41 sections, all of them disabled and all in user bank A: leftover from the editor, never read for a disabled section. A writer must clamp to 127 before SysEx.
- **Mask byte 48:** low 4 bits are the enabled sections. The high bits are junk in 3 multis (BraStack 0xDF in the factory bank, E.P.Comp and MIDIpnos 0xD7 in PadCpCmb). BraStack also has bytes 32-38 (GEQ) with 8-bit values, so it came from a damaged/odd save. The OS reads only the low 4 bits; writers should clear the high bits.
- Section byte 6 (tune) 49-79, byte 5 (transpose) 40-88, zone lo 0-127, zone hi 38-127.
- Duplicates: 182 of 768 multis repeat across files (SESSN-C and ASL-1CMB share their first names).
- EmptyMul default section: bank 0, patch 0, volume 127, pan 64, path 0, transpose 64, tune 65, zone 1..126, velsw 0, byte10 0, rx 0.


## OS image flash banks (the "ROM presets", found 2026-11-06 in K5000SR.SYS)

The 1 MB OS image is a flash dump (flash address = file offset + 0x100000). It contains two complete banks in exactly the .KAA layout (0xE04 pointer table + data, pointers are flash addresses):
- file 0x0A0000 (flash 0x1A0000): 100 patches, slots 0-99, first "PowerK5K"; 
- file 0x0D0000 (flash 0x1D0000): 40 patches, slots 0-39, first "DaLead".
All 140 patches pass checksum and every schema/structure check (0 problems; 131+32 PCM sources, 96+95 ADD sources). Round-trip through our packer is exact. Exported as banks/os_flash/A_flash.kaa, D_flash.kaa (+140 .ka1).

What they are: the OS routines at 0x11D0xx (A) and 0x11D3xx (D) are "save bank to flash": rebase every pointer from the RAM bank (R11) to flash (ptr - (R11 - 0x1A0000/0x1D0000)), program 0x20E04 bytes with the flash writer at 0x121D94, rebase back. So 0x1A0000 / 0x1D0000 are the non-volatile copies of user banks A and D, and the image carries the content they had when the image was made. They are the bank content shipped with the OS, not a separate preset space.
- The 40-patch bank has the same names as the D bank in the user's dump (2_DBANKINT) but is an earlier revision: 20 of 40 patches differ in 3 bytes or fewer (checksum plus a value or two), 18 differ in up to 30 bytes, 2 differ in source count; 19 have different ADD blocks. The 100-patch bank shares no name and no patch with the user's 60-patch A dump (names Runner, Gorgizmo... vs PowerK5K, PowerBas...), so the user's A is a different set.
- There is no ROM preset space for ids 0-255: the multi section patch lookup (0x10F96E-0x10F9B4) compares the id with 0xFF and returns "no patch" for anything <= 0xFF; the accepted range is 0x100-0x1FF (A: 0x100-0x17F via RAM table 0x34407C, D: 0x180-0x1FF via 0x323278), and 0x200-0x2FF (further SRAM banks 0x200010 and 0x220E14) only when the byte at 0x318963 is non-zero. Everything above is null. So a multi section with bank msb 0 or 1 takes the same path as an empty slot (0x111476) on this OS. Where those ids come from (other K5000 models, an editor's init state) is not established; the target of the jump 0x111476 is the common "no patch" path used for zero pointers, in single mode it falls back to the working buffer at 0x322F70.

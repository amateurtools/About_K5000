# Kawai K5000 additive (ADD) engine - specification for a from-scratch implementation

Status: edition 4 (see section 12 for the change log, 13 for fitted laws, 17 for the complete parameter inventory). Sources: Kawai MIDI implementation (OCR'd, section 3.1.3 ADD WAVE KIT), the K5000S manual (DHL/DHE/DFL/DFE/MORF pages),
the OS image (ADD block -> K018 chip RAM conversion, verified in the FR30 emulator), 1,305 unique ADD blocks from 1,422 singles (21 user banks, the OS's
own flash banks A and D, loose .KA1 files, the five clip patches), and the Crystalz clip. No new recordings are possible, so everything is tagged:

- **[V]** verified against the OS / emulator / data statistics, safe to implement
- **[S]** stated by the MIDI spec or manual, consistent with the data, not independently measured
- **[M]** measured from a clip
- **[?]** open - a provisional model is given so work can proceed, to be replaced when evidence exists

Companion files: `k5kfile/addkit.py` (decoder), `docs/K5000_FILE_FORMATS.md`, `docs/K5000_OS_INTERNALS.md` (ADD->kram section), `add/blocks.pkl` (the 1,305-block corpus).

---------------------------------------------------------------------------------------------------

## 1. What an ADD source is

A single holds 2-6 sources; each source is PCM (wave 1-464) or ADD (wave value >= 512; 512 is only the marker). Each ADD source owns one 806-byte block
that follows the 82+86n byte body in the file, in source order. [V]

An ADD source is a bank of **64 sine harmonics** (Manual: "each source can use a harmonic bank of 64 harmonics") [S]:

- **Harm Group** LO = harmonics 1..64 of the fundamental, HI = harmonics 65..128. The slot index i = 0..63 is the harmonic number (LO) or number-64 (HI). [S, V by data: 1288 LO / 17 HI]
- Two level sets, **soft** and **loud**, 64 levels each, cross-faded by velocity. [S]
- **64 harmonic envelopes**, one per harmonic slot, 4 segments each (attack, decay1, decay2, release), with optional decay loop. [S, V]
- A **128-band formant filter** (graphic EQ, one band per semitone) with bias, and a per-source envelope or LFO that sweeps it. [S]
- Everything else about the source (pitch, DCF, DCA, LFO, key delay, pan, bender/wheel routing) is the ordinary 86-byte source record; the ADD wave only replaces the oscillator. [V]

## 2. Byte layout of the 806-byte ADD block  (spec No. = byte index + 1)

| Byte | Field | Range / coding | Evidence |
|---|---|---|---|
| 0 | checksum | `(0xA5 + sum(B[1:806])) & 0x7F` | [V] all 1,305 blocks |
| 1 | morf flag | 0/1 (29 blocks = 1) | [S] "source uses Morf mode"; see 6 |
| 2 | Total Gain | 0..63 (50 typical, 63 max, never above) | [S,V] |
| 3 | Harm Group | 0 LO, 1 HI | [S,V] |
| 4 | KS to Gain | 1..127, 64 = 0 (-63..+63) | [S,V] |
| 5 | Velocity curve | 0..11 (4 = 1:1 curve, "#5" on the display; 855/1305 use it) | [S,V] |
| 6 | Velocity depth | 0..127; 0 = soft set only, 127 = full soft/loud cross-fade | [V] see 4.3 |
| 7,9,11,13 | Morf P1..P4 patch no. | 0..127 | [S] |
| 8,10,12,14 | Morf P1..P4 source | 0..11 (0-5 = source 1-6 soft, 6-11 = source 1-6 loud) | [S] |
| 15..18 | Morf Time 1..4 | 0..127 | [S] |
| 19 | Morf P3>P2 loop | 0 off, 1 on (2 also seen) | [S] |
| 20 | Formant bias | 1..127, 64 = 0 (-63..+63 semitones) | [S,V] |
| 21 | Formant sweep source | 0 = envelope, 1 = LFO (214 of 1305 use LFO) | [S,V] |
| 22 | Formant env depth | bipolar, 64 = 0 | [S,V] |
| 23,24 | FF env Attack rate / level | rate 0..127; level bipolar | [S,V] |
| 25,26 | FF env Decay1 rate / level | same | [S,V] |
| 27,28 | FF env Decay2 rate / level | same | [S,V] |
| 29,30 | FF env Release rate / level | same | [S,V] |
| 31 | FF env loop | 0 off, 1 LP1, 2 LP2 | [S,V] |
| 32 | FF velocity depth | bipolar | [S] |
| 33 | FF key-scale depth | bipolar | [S] |
| 34 | FF LFO speed | 0..127 | [S] |
| 35 | FF LFO shape | 0 tri, 1 saw, 2 random | [S,V] (3 values only) |
| 36 | FF LFO depth | 0..63 (a few blocks hold up to 127) | [S] |
| 37..100 | **soft** harmonic levels 1..64 | 0..127 | [V] |
| 101..164 | **loud** harmonic levels 1..64 | 0..127 | [V] |
| 165..292 | formant filter band levels 1..128 | 0..127 (127 = flat; 463/1305 blocks are all 127) | [S,V] |
| 293+8h ... 300+8h | harmonic envelope h = 0..63, 8 bytes | see 5 | [V] |
| 805 | dummy | 0 (one block has 32) | [V] |

The 36 bytes 1..36 are copied by the OS to work RAM (0x364E98 + 36k) and are used by the OS; bytes 37..804 go to the K018 chip (section 8). [V]

## 3. Frequencies

The oscillator frequency is the normal source pitch (coarse/fine/key-follow/fixed-key, pitch envelope, LFO vibrato, bender), call it `f0`.
Harmonic number n has frequency exactly `n * f0` (integer ratios, no stretching) [M: Crystalz peaks fall on k*87.3 Hz, k=1..16]. LO group n = i+1, HI group n = i+65.
Harmonics above Nyquist (or above the chip's limit, unknown) are silent [?].

## 4. Harmonic level

### 4.1 Level law [M]
A level byte L (0..127) is a **logarithmic amplitude**, about **0.75 dB per step**, i.e. 6.02 dB per 8 steps:

    amp(L) = 2^((L - 127) / 8)           (L = 127 -> 1.0, L = 0 -> -96 dB; 0 is treated as silence)

Crystalz harmonics 4..16 (levels 93, 74, 51, 87, 74, 96, 93 ...) measured against the 127 harmonics: slope 0.72..0.78 dB/step, floor at about -80 dB. [M, +-2 dB]

### 4.2 Combination [S + ?]
    level_dB(n) = law(L_set(n)) + law(Total Gain) + KS_gain(key) + formant_gain(n*f0) + harmonic_envelope(n, t) + source_volume + velocity terms
Total Gain (0..63, 63 = the loudest harmonic at its top) is an offset in the same log domain: "the loudest harmonic will be set to this level". The scale of the offset (0.75 dB/step or finer) is [?]; Crystalz (gain 63) vs DtPerc (gain 50) was not compared.
KS to Gain: positive = higher notes louder (manual). Slope per key [?] (suggest +-63 -> +-1 step per key ... fit later).

### 4.3 Soft/loud cross-fade [S + V]
Velocity (through the Velo curve 0..11) selects between the soft and loud set per harmonic; `Velo depth` is the amount of cross-fade.
Statistics back this: with depth 0, 162 of 561 blocks have an empty loud set (unused); with depth 127 only 1 of 435 does, and soft/loud tables are almost always different. [V]
Provisional model: `t = curve(vel) * depth / 127`, `L = soft + (loud - soft) * t` (interpolate in the log domain). [?]
Crystalz: depth 123, loud = soft + 4 (nearly identical sets, so the ear hears little cross-fade).

## 5. Harmonic envelopes (64 per source)

Eight bytes per harmonic h (offset 293 + 8h):

| byte | meaning |
|---|---|
| 0 | Attack rate 0..127 (127 = instant, 64117 of 83520 entries) |
| 1 | Attack level 0..63 (63 = full, 83% of entries) |
| 2 | Decay1 rate |
| 3 | Decay1 level 0..63 in bits 0..5, **bit 6 = flag S** |
| 4 | Decay2 rate |
| 5 | Decay2 level 0..63, **bit 6 = flag T** |
| 6 | Release rate |
| 7 | Release level 0..63 (0 = to silence) |

Key facts [S,V]:
- "The harmonic envelope starts at zero. The attack may go above or below zero and the release need not end at zero."
- Level 63 = full (the harmonic's table level), 0 = silent; the 6-bit level is a linear or log fraction of the harmonic level - [?], provisional: same law as 4.1 with 63 -> 0 dB and one step = 1.5 dB (63 steps over ~96 dB).
- Rates: 127 is fastest. The mapping from rate to time/slope is [?]; the DCA envelope rate law (see the Kawai envelope tables in the manual) is the best starting point, but the harmonic envelope may differ. This is the single biggest open item for matching timbre evolution.
- **Loop** (Manual "Decay Loop"): OFF = after D2 sustain at D2 level; LP1 = loop between Decay1 level and Decay2 level at the Decay2 rate; LP2 = same but uses both Decay1 and Decay2 rates.
  Encoded by the two flags (S = bit 6 of the decay1-level byte, T = bit 6 of the decay2-level byte): S=1,T=1 -> LP1; S=0,T=1 -> LP2; T=0 -> off (S=1,T=0 also reads as off; S=0,T=0, the most common, 63,178 of 83,520 entries, is off). [M: the OS sets exactly these flag pairs for wire loop 1 (LP1) and 2 (LP2), ARCH s18.5; confirmed by the ksynth library]. An earlier version of this note had LP1/LP2 swapped.
  Corroboration from data: entries with T=1 differ in D1 level vs D2 level 70-79% of the time and use non-127 rates in 93-97% of entries; entries with T=0 only 28-40% / 54-66%. [V]
  The loop type is nearly always uniform inside one block (1151 of 1305), as the editor's Loop button applies to a group.
- BioReact has LP2 (S=0,T=1) on all 128 harmonic envelopes in its file (earlier notes said LP1, from the swapped reading). The stepped "sample and hold" feel is therefore not necessarily the LFO alone: loop envelopes with different per-harmonic rates produce slowly shifting timbre. Worth checking in the engine: random LFO still confirmed (see the LFO notes) for the pitch steps.
- In the chip RAM the flags are literally bit 15 of the segment word (see 8), so the engine decodes S/T per segment, not per record.

## 6. Morf

Morf is an editor helper: four "phases" P1..P4, each copied from a source/soft-or-loud set of a stored patch, with times between them and a P3>P2 loop. Pressing Execute **writes the result into the ordinary harmonic envelope bytes** (the manual: "the DHE parameters are replaced by new Morf settings").
Evidence: of 29 blocks with morf flag 1, the patch/source/time fields are all zero or default in 28 (patch 0, source 0, time 64), while the envelope data hold the real content. So a player needs only bytes 1.. as plain envelopes; the morf fields can be stored and exported but do not drive synthesis. [V]
Open: what exactly the flag selects at play time (e.g. how many envelope segments are used, whether the "Time" tracks are expanded into rate/level pairs). Check the 29 flagged blocks (A_flash 'EvhaWatr' etc.) once the voice engine runs. [?]

## 7. Formant filter

128 bands, one per semitone: band n has centre frequency `440 * 2^((n - 70)/12)` Hz (band 1 = 8.18 Hz, band 128 = 12.5 kHz; the manual table matches). [S]
- Band level 0..127 per band: 127 = no cut. The level law is [?], provisional: same 0.75 dB/step as 4.1.
- Bias: -63..+63 semitones, moves the whole filter curve: +12 puts band 70 at 220 Hz (down an octave). [S]
  So the gain applied to a harmonic at frequency f is band_level interpolated at `band = 70 + 12*log2(f/440) + bias_offset`, with sign such that bias +12 reads the curve at one octave lower: effective_band = 70 + 12*log2(f/440) + bias. [S]
- Sweep: either the envelope (FF env, per source) or an LFO adds to the bias. The envelope value works the same as the bias ("If the Formant Filter Env is set to a minus value, the filter moves to the right"). Offset in semitones = `env_level/63 * env_depth/63 * 12 * velo/ks modifiers` [?] (implemented in AddSource::stepFormant: four stages with release, LP1/LP2 loop, velocity and KS depth; LFO tri/saw/random). See decisions.md.
- FF envelope: 4 segments rate/level (bipolar levels) + loop (0 off, 1 LP1, 2 LP2), velocity depth, key-scale depth. LFO: speed, depth 0..63, shape tri/saw/rnd.
- The formant data pack two bands per 16-bit chip word (section 8); band order inside the word is hi byte = odd byte offset. A hardware detail only.

Crystalz: bias -9 and -6 (sources 1 and 2), a decaying FF envelope (depth -45), flat below band ~88: only harmonics above about 1.3 kHz are shaped, which fits the sound moving from bright to soft.

## 8. Chip view (K018 parameter RAM) [V]

Per source k (0..5), base = 0x800400 + k*0x400, 16-bit big-endian words:

    K[base + 0x080 + 2i] = B[37+i] << 9                              i = 0..127 (soft 0..63, loud 64..127)
    K[base + 0x180 + 2j] = (B[166+2j] << 9) + (B[165+2j] << 1)       j = 0..63  (two formant bands per word)
    K[base + 0x200 + 2m] = (B[294+2m] << 9) + (B[293+2m] << 2)       m = 0..255 (4 words per harmonic: atk, d1, d2, rel)
                          word bit 15: attack word always 1, d1 word = S, d2 word = T, release word = bit6 of its level byte

Reading: each chip word is (level7 << 9) | (rate7 << 2) with the loop flag riding in the level byte's bit 6 -> word bit 15. The K018 therefore does the synthesis: levels, envelope stepping, loop, formant filter, partial oscillators. The OS only converts and sends. The chip also needs the 36-byte common block fields (gain, group, morf, bias, ff-env, lfo); these are passed by the OS through a separate path at kram base+0x000..0x07F (written by code at 0x166148; not yet decoded).

Consequence for the replacement OS: the voice engine must implement the chip's maths; the OS code can be replaced by plain C++ that reads the block and drives it.

## 9. Statistics over the corpus (1,305 unique blocks)

| item | result |
|---|---|
| harm group | 1288 LO, 17 HI |
| velocity curve | #4 = 855, #5 = 116, #6 = 122, #11 = 39, #0 = 62, others <30 |
| velocity depth | 0: 599, 127: 457, others (spread) 249 |
| total gain | 0..63, never above 63; 50 = 436, 63 = 160 |
| soft set | 17,949 zero levels of 83,520; 6,207 at 127 |
| loud set | 19,603 zero levels; 12,136 at 127; loud entirely empty in 174 blocks |
| formant | 463 flat (all 127); 775 with non-zero bias; 214 sweep by LFO (shape 1241 tri, 40 rnd, 24 saw) |
| FF env loop | off 697, LP1 449, LP2 159 |
| harmonic envelope loop | off about 80%, LP1/LP2 about 20% (per entry), uniform per block in 1151 |
| attack rate | 127 in 77% of entries |
| morf flag | 29 blocks, nearly all with default morf fields |

## 10. Confirmed vs open

Confirmed: block layout and checksum; 64 harmonics x (soft, loud) x group; per-harmonic 4-segment envelope with loop flags in bit 6; 128 formant bands, 64 chip words; chip word encoding; log level law about 0.75 dB/step; integer harmonic ratios.

Open (in order of impact on sound match):
1. Envelope rate -> time law (harmonic envelope, formant envelope). Needs either more clips or reading the K018 behaviour; the emulator cannot yet run the chip.
2. Level scales of Total Gain and the 6-bit envelope levels; formant band level law.
3. Soft/loud interpolation and the velocity curves 0..11 (the manual shows the shapes; table values unknown).
4. KS-to-gain slope, FF env depth/velo/KS scaling, FF LFO rate in Hz.
5. Morf flag semantics at play time.
6. Behaviour above the chip's harmonic/Nyquist limit; HI group frequency limits.
7. How the 36-byte common block reaches the chip (kram base+0x000..0x07F).

Plan to close them without new recordings: (a) fit rate/level laws to Crystalz (known f0 87.3 Hz, two sources, FF env active), Dt-perc (three flat sources, simple harmonic tables: best for the rate law), dENTiST (LP1 on 64 envelopes, big biases) and BioReact (LP2 loops, random LFO), using the harmonic-amplitude tracks of the clips; (b) implement the engine behind a parameter-table so each law is a single swap; (c) when the user has the real K5000 again, record a handful of calibration patches (one harmonic, one envelope segment at a time).

## 11. Minimal reference voice (pseudo-code)

    for each ADD source s, for each note-on (f0, velocity, key):
        t = curve[velo_curve](velocity) * velo_depth / 127
        for i in 0..63:
            n = (harm_group ? 64 : 0) + i + 1;  f = n * f0;  if f >= nyquist: skip
            L = lerp(soft[i], loud[i], t)                          // log domain
            lev_db = law(L) + gain(total_gain) + ks_gain*(key-60)/... + formant_db(f) + src_volume
            env = HarmEnv(i)                                       // 4 segments + loop
            partial[i] = sine(phase_i) * dB(lev_db + env_db(env, time))
        out = sum(partial) -> DCF (unless bypassed) -> DCA -> pan/effects
        formant_db(f) = interp(band_level, 70 + 12*log2(f/440) + bias + sweep(t))

The decoder in `k5kfile/addkit.py` returns exactly these fields (`AddKit.decode(block)`), and `summary()` prints a patch line for debugging.


---------------------------------------------------------------------------------------------------

## 12. Change log

**Edition 2 (after the first additive voice and the first batch of external clips)**
- Level law confirmed a second time: Simoog's harmonic tables are an exact sawtooth, level = 127 - 8*log2(n), and the clip's harmonic ratios agree within about 1 dB. [M]
- Source mute mask: bit set = source ON. 22 of 26 patches have mask == (1<<n)-1; Thumper, sheff and Airpress (mask 01 or 05 with 2 sources) play only the sources whose bit is set. Polarity still to be confirmed with a clip of a patch whose first source is the muted one (sheff). [S, strong]
- DCA release: steady -47 dB/s at release setting 75 (Simoog clip), linear in dB. An earlier note of "accelerating decay" was wrong: the last 0.2 s of that clip is its end fade. [M]
- Harmonic envelope attack: rate 115 reaches full level in under 30 ms (Simoog), so the rate law is far faster at high rates than the first guess. [M, bound only]
- Pitch envelope: see section 14. [M]
- Sound Diver module files (K5000.hlp, .dll, .GID) were examined: the help file is a Windows help file with one topic per parameter. It gives no numeric scales (confirmed by the user reading the pitch envelope pages). [checked]

**Edition 3 (formant filter)**
- Formant filter gain at a harmonic of frequency f: `gain_dB = (band_level(b) - 127) * 0.75`, with `b = 70 + 12*log2(f/440) + bias + sweep`, band levels interpolated linearly between the 128 bands (1-based), clamped at the ends. 127 = no cut. Fitted on Simoog (DCF bypassed, filter table with a flat 111 shelf, a +12 dB peak at band 63 and a roll-off of about 2.5 steps per band): best step size 0.65 to 0.75 dB, error about 6 dB rms (limited by the beating of four detuned sources and the mp3). A negative bias or sweep moves the curve up in frequency, as the manual says. [M, bias sign and band spacing confirmed; sweep scale open]
- Tabulated which clip patches have a non-flat formant filter and the DCF bypassed: Simoog (LFO sweep), Emubassm source 1 is flat. Most others have the DCF active. See `add/` survey output.

## 13. Fitted laws and where they came from

| Law | Value | Source | Status |
|---|---|---|---|
| Harmonic level | amp = 2^((L-127)/8), 0.75 dB/step, 0 = silent | Crystalz, Simoog | [M] |
| DCA release rate | 47 dB/s at release 75; other settings assume x2 per 12 steps | Simoog | [M] one point |
| DCA attack | 6 = well under 100 ms | Simoog | [M] bound |
| Harmonic envelope rate | rate 115 reaches full in <30 ms (Simoog); rates 42-54 move ~8 dB/s (Crystalz h9-h11 fall 25-45 dB over ~3 s); model 8*2^((r-48)/7.5) dB/s | Simoog, Crystalz | [?] two points, exponential assumed |
| Harmonic envelope level step | 6-bit level, 1.5 dB/step assumed | none | [?] |
| Total Gain | 0.75 dB/step assumed | none | [?] |
| Soft/loud mix | t = curve(vel)*depth/127, linear in amplitude | none | [?] |
| Pitch envelope range | level +63 = +1200 cents (19.05 cents/step), linear in cents | Thumper x2 | [M] |
| Pitch envelope decay | time 127 = 2.45 s for a full +63 -> 0 glide, linear in cents | Thumper x2 | [M] one point |
| Pitch envelope attack | time 39 = about 0.25 s hold at the attack level | Thumper x2 | [M] one point |
| Formant band level step | 0.75 dB/step, linear interpolation across bands, b = 70+12*log2(f/440)+bias+sweep | Simoog | [M] |
| Formant sweep scale | 12 semitones at full level and depth assumed (envelope or LFO) | Simoog (offset drifted +2 to +5.5 st in 0.4 s) | [?] |
| Formant LFO rate | 0.1 to 18 Hz exponential over speed 0..127 assumed (manual range); triangle starts at 0 rising | none | [?] |
| Formant envelope rate | 0.01 s * 2^((127-r)/10) per 63 levels assumed | none | [?] |
| Fine tune | (fine-64) * 100/64 cents assumed | none | [?] |

## 14. Source-level processing around the additive oscillator

An ADD source is more than its 806-byte block. Its 86-byte source record applies, in this order (all of it shared with PCM sources):

1. **Pitch** = note + coarse (semitones, two octaves either way) + fine + KS pitch + **pitch envelope** + LFO vibrato + bender + portamento. One pitch curve drives all harmonics; the phase is accumulated from the instantaneous fundamental, and harmonic n uses n times that phase, so the harmonics stay locked as the pitch moves. [S, V]
2. **Oscillator**: the 64 partials (sections 3 to 8).
3. **DCF** (filter): cutoff, resonance, level, mode, its own envelope with key-scale and velocity sensitivity; bypass bit. Nearly every patch in the corpus has it active, which is the main reason clips of additive patches do not show the raw spectrum (the Airpress clip: harmonics 1 and 2 are 50 dB below their table level because of cutoff 16 with a deep envelope). [V by data, law open]
4. **DCA** (amplifier): the only amplitude envelope in a patch. There is no master envelope in the common section (verified against the common field list). It has attack, decay 1, decay 2, release (all as TIMES, small = fast), a sustain level, key-scale modifiers (level, attack, decay, release) and velocity modifiers (level, attack, decay, release), and its own velocity curve. [S, V]
5. Source volume, pan, effect path, then the common section's volume and effects.

The harmonic envelope (section 5) multiplies the harmonic's level; the DCA multiplies the whole source.

### Pitch envelope (DCO envelope) [M]
Fields: start level, attack time, attack level, decay time, velocity-to-time, velocity-to-level (source bytes 34 to 39). Levels are bipolar, 64 = 0. Behaviour: starts at the start level, moves to the attack level over the attack time, then falls linearly (in cents) to zero over the decay time. It has no sustain.
Fitted on two Thumper clips (start 126, attack time 39, attack level 127, decay time 127; the fundamental starts at about 2x the settled pitch, falls over about 2.45 s, then holds):
- level +63 = +1 octave (1200 cents); -63 = -1 octave assumed by symmetry,
- the glide is linear in cents (about 490 to 510 cents per second at decay time 127),
- the modelled fundamental stays within about 40 cents of the clip over the whole glide.
Open: time laws for other settings (assumed doubling every 12 steps), velocity sensitivity of level and time.

## 15. Reference voice and tools

- `k5kfile/addkit.py`: decoder for the 806-byte block.
- `k5kfile/addvoice.py`: numpy prototype of the additive voice with per-harmonic envelopes (with loops), DCA, pitch envelope and the soft/loud mix. Command line: `python3 addvoice.py patch.ka1 out.wav --note 36 --on 2 --total 4`. PCM sources are skipped; no DCF, no LFO, no formant filter yet. Every timing law lives in one table (`LAWS`).
- Clip analysis helpers (comb-fit pitch tracking, harmonic amplitude extraction) are in the working folder under `add/`.

## 16. Corpus and clip notes

Clips analysed so far (internet recordings, notes and velocities unknown; fitted from the patch where needed):
- Crystalz: level law, f0 87.3 Hz.
- Simoog: sawtooth tables, filter bypassed; DCA release law; harmonic attack bound.
- Thumper (two clips, A0 and A1): pitch envelope.
- Airpress: filter confound (cutoff 16).
- BioReact, dENTiST, Dt-perc, WarmBass: classified earlier (additive/PCM, number of sources); BioReact shows random LFO steps and LP2 loops on all harmonics.

Selection rule for further clips: prefer patches with the DCF bypassed or fully open, a flat formant filter, and neutral velocity settings, so that one law is exercised at a time.


---------------------------------------------------------------------------------------------------

## 17. Complete parameter inventory: everything that reaches the additive oscillator

Legend for "state": **V** implemented in `addvoice.py` and measured; **P** implemented with a provisional law; **-** not implemented yet; **n/a** does not apply to the oscillator. Numbers in brackets are the schema field numbers (`schema/k5000_patch_params.csv`), source fields 1-86, ADD kit bytes 1-806.

### A. In the 806-byte ADD block (per source)
| Group | Parameters | State |
|---|---|---|
| Common (kit bytes 2-6) | morf flag; total gain; harm group LO/HI; KS to gain; velocity curve; velocity depth | harm group V; total gain, velocity curve, velocity depth P; KS to gain -; morf flag n/a at play time |
| Morf set (7-19) | 4 x (patch, source), 4 x time, P3>P2 loop | n/a for playback (the result is baked into the envelopes) |
| Formant filter (20-36) | bias; env/LFO select; env depth; 4 x (rate, level); env loop; velocity depth; key-scale depth; LFO speed, shape, depth | bias and band law V; sweep (env, LFO) P; env loop -, velocity depth -, key-scale depth -, LFO shape saw/random - |
| Harmonic levels (37-164) | 64 soft + 64 loud | V (law); soft/loud mix P |
| Formant bands (165-292) | 128 band levels | V |
| Harmonic envelopes (293-804) | 64 x (attack rate/level, decay 1 rate/level+flag, decay 2 rate/level+flag, release rate/level) | structure V; rate laws P; loops P |

### B. In the 86-byte source record that act on an ADD source
| Group | Fields | Notes | State |
|---|---|---|---|
| Wave select | wave_msb, wave_lsb (29,30) | >= 512 selects ADD | V |
| Pitch | coarse (31), fine (32), fixed key (33), KS pitch (34) | fine scale assumed; fixed key semantics open; KS pitch (0-3 curve) open | coarse P, fine P, fixed key -, KS pitch - |
| Pitch envelope | start, attack time/level, decay time, time velocity, level velocity (35-40) | | range, decay and attack V (one point each); velocity terms - |
| LFO | waveform, speed, delay, fade time, fade speed, vibrato depth + KS, growl depth + KS, tremolo depth + KS (76-86) | vibrato (pitch), tremolo (amplitude) act on the oscillator; growl acts on the DCF | wired (engine/lfo/lfo.h): all 11 bytes read; ALL numeric scalings ? (see below) |
| DCA | velocity curve, envelope (attack, decay 1 time/level, decay 2 time/level, release), key-scale (level, attack, decay 1, release), velocity (level, attack, decay 1, release) (61-75) | amplitude of the source | envelope P; key-scale and velocity terms - |
| Level and routing | source volume (5), pan type and value (27,28), effect path (4) | volume P; pan and path are mixer stage | volume P, others n/a to oscillator |
| Key handling | zone lo/hi (1,2), velocity switch (3), key-on delay (26) | note range, velocity split, start delay | -, -, - |
| Controllers | bender pitch range (6); pressure, wheel, express: 2 destinations + depths each (8-19); assignable controllers 1 and 2: source, destination, depth (20-25) | destinations that reach the additive engine are listed in C | all - |
| DCF | bypass, mode, velocity curve, resonance, level, cutoff, KS, velocity, envelope (41-60) | acts on the oscillator output, not on the additive data | wired (engine/filter/dcf.h): bypass V (1 = bypassed), LP/HP, resonance, level, cutoff, KS, velocity, 4-segment envelope, depth, velocity to depth; ALL scalings ? (see below). KS to attack/decay1 and velocity to attack/decay1 not applied |

### C. Modulation destinations (0-19) usable from the controller sources, and macros
Pitch, Cutoff, Level, Vibrato depth, Growl depth, Tremolo depth, LFO speed, Attack, Decay 1, Release, Velocity, Resonance, Panpot, **FF bias, FF env/LFO depth, FF env/LFO speed, Harmonics LO, Harmonics HI, Harmonics EVEN, Harmonics ODD**.
The last seven are additive-specific: they modify the formant filter and the harmonic levels in real time. "Harmonics LO/HI" raise or lower the lower or upper part of the harmonic range (not the pitch range); EVEN and ODD (odd includes the fundamental) raise or lower those harmonics. State: all -. The depth law, the curve of LO/HI (how many harmonics it affects and how), and whether the change is applied in the log-level domain are open.
Common-section macros 1-4 (two parameters each, depths 33-95) and the assignable controllers reuse this destination list. The four user-assignable knobs (USR1-4) show the same list.

### D. Switch controller functions (common bytes 78-81, 17 values)
OFF; **HARMMAX** (all harmonics to maximum); **HARMBRIT** (higher harmonics louder); **HARMDARK** (first at maximum, successively lower, sharp drop-off); **HARMSAW** (sawtooth: smooth drop-off); **SELECTLOUD** (force loud set); **DHL LOUD** (loud set in the DHL page); **DHL 5TH** (adds harmonics 3, 6, 12, 24, 48); **DHL ODD**; **DHL EVEN**; **DHE#1** (envelope switches rapidly between odd and even harmonics, the first three stay on); **DHE#2** (emphasise attack, turns on feedback); **DHE LOOPON** (turn on harmonic envelope looping); **DHF MAX** (formant frequency to maximum); **DHF COMB** (turns on the formant filter LFO); **DHF HICUT** (sets the filter frequency low to cut the highs); **DHF COMB2** (undocumented in the manual). State: all -. Note "turns on feedback" in DHE#2: the additive engine has a feedback path we have not seen elsewhere; its meaning is open.

### E. Common-section items that touch every source, including additive
Polyphony mode (common 49: poly / solo), portamento on and speed (60, 61), source mute mask (52), source count (51), AM (53: amplitude modulation between sources), common volume (48), effects (fx algorithm, reverb, four effects, graphic EQ: 1-38), the two effect controllers (54-59). State: mute mask V (bit set = on); the rest -.

### F. Items that are not parameters but affect the sound
- Voice allocation and stealing (24 voices? chip budget unknown), note-on retrigger and legato rules in solo mode, key-off handling (release) and sustain pedal, velocity tables (12 curves), bender and wheel scalings.
- The chip's fixed-point behaviour: level quantisation (7-bit levels, 14-bit words), update rates of the envelopes, aliasing near the upper harmonics, the number of harmonics it can run per voice. All unmeasured.

### G. What "additive oscillator ready" requires (checklist)
1. Block decode and checksum: done. 2. 64 harmonics with the level law: done. 3. Per-harmonic envelopes with loops: done, rate laws open. 4. Soft/loud mix by velocity: provisional; needs known-velocity clips. 5. Formant filter: static law done; sweeps provisional; envelope release, loop, velocity and key-scale terms implemented as guesses [?]; live bias/depth/speed from controllers. 6. DCA with key-scale and velocity terms: partial. 7. Pitch (coarse, fine, fixed key, KS, envelope, vibrato, bender, portamento): partly done. 8. LFO (vibrato, tremolo, shapes, delay, fade): not yet. 9. Real-time harmonic controls (macro/controller destinations and the 16 switch functions): not yet. 10. Key delay, zones, velocity switch, solo/poly modes: implemented; AM between sources: structure from the OS (pair k, k+1), law guessed (decisions.md).

## Scaling and velocity inventory (2026-11-07)
Source byte = table index - 1. "Engine" = what `add_source.cpp` does today.

| Parameter | Source byte | Engine | Status |
|---|---|---|---|
| Velocity curves 0..11 (12 x 128 bytes) | OS address 0x16f22d, 128 per curve | loaded from `velocity_curves.h` | [S] read from the OS. Index 4 (shown #5) is exactly 1:1, as the manual says. Index 10 is a constant 127 and index 11 is a steep convex curve with a small dip near input 78; both are read as stored and not yet checked on a clip. |
| Kit velocity curve, velocity depth | kit 5, 6 | soft/loud cross-fade t = depth/127 * curve[vel]/127 | [S] curves, [?] the cross-fade law |
| DCA velocity curve, velo-to-level (0..63) | 60, 71 | attenuation = (level/63) * (1 - curve[vel]/127) * 47.25 dB | [?] law and 47.25 dB maximum are guesses; no clip has a known velocity |
| DCA velo to attack / decay1 / release | 72, 73, 74 | ignored | [?] needs clips at known velocity |
| DCA key scale: level, attack, decay1, release | 67, 68, 69, 70 | ignored | [?] reference key and scale unknown; needs the same patch at several notes |
| Kit KS to gain | kit 4 | ignored | [?] |
| Coarse tune | 30 | ignored (all clip patches have 64 = 0) | [?] to do: +/-24 semitones |
| Fixed key, KS pitch | 32, 33 | ignored | [?] manual: KS pitch 0 / 25 / 33 / 50 cents tracking when fixed key is on |
| Pitch envelope velocity (time, level) | 38, 39 | ignored | [?] |
| LFO (vibrato, growl, tremolo, KS of each, delay, fade) | 75..85 | ignored | not yet |
| Pan type (normal / KS / -KS / random), pan value | 26, 27 | ignored (mono mix) | not yet |
| Key-on delay | 25 | ignored | not yet |

Clip patches with non-default scaling terms: Crystalz (DCA KS level 56, velo level 23, velo curve idx 4, velo release 26), Aquarius (velo curve idx 1, velo level 0), dENTiST (velo curve idx 11 with velo level 63 on two sources: nearly silent unless played hard).


## DCF laws as implemented (all [?], one struct DcfLaws + the DCA time laws)
Cutoff 0..127 = 20 Hz .. 20 kHz exponential (12.7 units per octave); envelope level -1..+1 times depth/63 times 127 units; KS +63 = one octave per octave around key 60; velocity term = sens/63 * (curve/127 - 1) * 64 units; velocity to envelope scales the depth by 1 + sens/63 * (curve/127 - 1); DCF level = +1.5 dB per step (level 0 must be unity: 160 of 175 sources in the factory A bank use 0); resonance via the Airwindows curve (Q 0.557 at 0, 4.31 at 7); 12 dB per octave; envelope times share the DCA time laws. Corpus facts [V]: factory A bank, 175 sources: bypass 0 in 162, LP in 173, resonance 0-2 in 171, cutoff median about 18, envelope depth mostly +30..+63, so the envelope opens the filter. Tendency check: Airpress source 1 sits at 100..300 Hz for note 36 (the clip shows the low harmonics 50 dB down). Calibration plan: measure the cutoff of a DCF-only clip (a patch with a flat harmonic table) at known settings and fit the cutoff unit, the sweep range and the slope. tools/dcf_probe.cpp prints a patch's DCF settings and its cutoff over time.


## LFO laws as implemented (engine/lfo/lfo.h; [S] = manual, rest [?])
Closed form in time, one per source: `wave(t)` and `fade(t)`; the oscillator (vibrato, via SourceEnv::pitchCents), the DCA (tremolo, via SourceEnv::stepDca) and the DCF (growl, in Dcf) read it independently and agree. Waveform byte 0 tri, 1 sqr, 2 saw, 3 sin, 4 rnd (registry order; the manual's list order differs). Speed 0.1..18 Hz [S], taken as 0.1 * 180^(speed/127). Delay 0..2 s [S] = delay/127 * 2. Fade-in time 0..4 s of amplitude ramp 0 -> 1 after the delay [?]. Fade-in speed: starts at speed * (1 - 0.9 * fs/63) and ramps to speed over the fade time [?; the manual only says it "gradually speeds up to the initial speed"]. Depth 63: vibrato +-100 cents, growl +-32 cutoff units (2.5 octaves), tremolo 0..-12 dB (attenuation only) [?]. KS factor 1 + ks/63 * (key - 60)/60, >= 0 [?]. Random = sample and hold, new value per cycle, seeded per note. Corpus [V], factory A bank, 175 sources: 136 triangle, 20 sine; vibrato used by 87 (median depth 10), growl by 33 (median 6), tremolo by 3; median speed 91 (about 4 Hz), delay mostly 0-2, fade mostly 0. These give subtle, musical modulation at the chosen scales.
Open: vibrato range at depth 63, growl range, tremolo depth and whether it is bipolar, KS law, the fade-in speed rule, LFO phase at note start (free-running vs retriggered per note), LFO shared between the unison sources of a note or one per source (manual: per source). Wheel/aftertouch/controller routing to LFO depth and speed is not wired (macro and controller destinations).

- Live speed offset (controller destination 6) [?]: speed byte + offset, clamped 0..127, same rate law; phase continuous across changes (phase anchor).

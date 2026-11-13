# K5K UI — page tree and interaction rules (draft 1)

Status: proposal. Wireframes: `01_play.svg` … `05_picker_arp_mode.svg` (regenerate with `python3 gen_wireframes.py`; colours are the `THEME` dict at the top).

## 1. Canvas and scaling
- Reference canvas **1600 × 1000 (16:10)**. All layout is written in these units; the plugin scales the whole editor by one factor.
- JUCE: `setResizable(true, true)` + `ComponentBoundsConstrainer::setFixedAspectRatio(1.6)`, minimum about 960 × 600. One root component with `AffineTransform::scale(w / 1600)`; children laid out in reference units. Everything is vector (Path / SVG drawable), no fixed-size bitmaps. Last window size is remembered.
- Theme tokens (screen gradient top/bottom, LED colour, dim LED, accent, bezel) live in one struct; the editor header has quick controls (hue / brightness / contrast of the screen) that rewrite it live and persist to the settings file.
- The screen interior is 1254 × 358 units (about 35 % of the height). **Expanded mode**: a button or double-click on the breadcrumb grows the screen over the control area (to about 1254 × 700) for dense editors (64 harmonics, 6 sources); the panel returns on collapse.

- **Font:** Comic Neue (SIL OFL, free; licence text in `fonts/`). The wireframes embed it so they render the same everywhere; in JUCE, embed the TTF as binary data and use it for every label.
- **Grid (reference units):** quick-button columns centred in the 147 px bezel margins, 4 equally spaced buttons per side (pitch 106.7); SW1 / SW2 / PORTA / ARP rows 101.7 apart and shared with the knob rows; assignable and arp knobs on one 135 px column grid centred in their panel; macro knobs 135 px × 126 px grid centred in their panel; all panel gaps 36; keyboard and wheel panel flush to the 24 px side margins.

## 2. The two layers
- **Panel** = playing layer. Every knob and key is a host-automatable parameter (macros 12, user 4, SW1, SW2, Porta, Arp on, Arp pattern / mode / speed, Single/Multi, wheels). Always visible.
- **Screen** = editing layer. Every panel control also has a full home on a screen page; both write the same parameter.
- Rule: a host-driven parameter change moves the knob but never takes over the screen. Only touching a control opens its picker or page.

## 3. Screen furniture (same on every page)
| Slot | Job |
|---|---|
| Breadcrumb bar (top) | where am I; every segment is clickable (jump up) |
| 8 softkeys (bottom) | the tabs of the current level; the active one is filled |
| Top-left pair | UNDO / REDO |
| Top-right pair | COPY / PASTE (copies the thing on screen: harmonic, envelope, source, effect) |
| ▲ ▼ (right) | previous / next sibling (previous / next source, harmonic, preset) |
| Tab row (inside page) | the next level down (e.g. Levels / Envelopes / Formant / Morph / Scaling) |

## 4. Quick-access buttons (8, on the bezel)
| Button | Opens | Notes |
|---|---|---|
| PRESETS | Library: bank grid A/D/E/F, search, favourites, multi list, load file | home of bank / patch selection |
| EDIT | Edit tree (section 5) | softkeys: Common, Source 1-6, Effects |
| WRITE | Store / export: save to bank slot, export .ka1/.kaa/SysEx, compare | rename to SAVE if preferred |
| SYSTEM | MIDI + SysEx send/receive, SoundFont and wave map, audio / CPU, theme, tuning, about | |
| HELP | Context help for the page you are on | |
| MIXER | Multi: 4 sections (patch, volume, pan, channel, zone, velocity switch, transpose, effect path) | disabled / explains itself in single mode |
| EFFECTS | Algorithm, the effect parameters, reverb, graphic EQ, effect controller | |
| ARP | Arpeggiator page: pattern, mode, range, gate, level, tempo, User/Seq step table | replaces DEMO; demo playback moves to HELP |
PLAY (home) is what you see when no page is chosen; pressing the active quick button returns to PLAY.

## 5. Edit tree
```
EDIT
├─ COMMON            volume · poly mode · porta (on/speed) · source count / mute · AM · effect-send
│  ├─ MACRO / USER   what the 4 user knobs control (dest + depth)
│  └─ SWITCHES       SW1 / SW2 / FSW1 / FSW2 function
├─ SOURCE 1..6
│  ├─ BASIC          zone, velocity switch, pan, tune, key-on delay, level
│  ├─ WAVE           ADD (additive) or PCM (wave number, from the wave map)
│  ├─ FILTER (DCF)   mode, cutoff, resonance, envelope, velocity (key scaling is last)
│  ├─ AMP (DCA)      envelope, velocity
│  ├─ LFO            shape, speed, depth (vibrato / growl / tremolo)
│  ├─ CONTROLLERS    bender · pressure · wheel · expression · 2 assignable routes
│  └─ ADDITIVE       (only for ADD sources)
│     ├─ HARMONICS   64 levels, SOFT / LOUD            (02_additive_harmonics)
│     ├─ ENVELOPES   per-harmonic 4-point envelopes     (03_additive_envelope)
│     ├─ FORMANT     bias, envelope / LFO, depth, speed
│     ├─ MORPH       4 snapshots + Execute (deferred, not in the engine yet)
│     └─ SCALING     key scaling, last
└─ EFFECTS           same as the quick button
```

## 6. Picker rule (stepped knobs)
Applies to Arp Pattern, Arp Mode, Porta (Patch / Off / On) now; later effect type, LFO shape, filter mode, switch function.
- **Press-and-drag the knob:** the screen blanks to the option tiles; an outline rectangle follows the knob; **releasing applies** and returns to the previous page. Dragging back to the starting tile and releasing, or Esc, cancels.
- **Plain click on the knob:** the same picker stays open; click a tile or outside to dismiss.
- One begin / end automation gesture per pick. Continuous knobs (Speed, macros, user) show a value readout on the screen while dragged but never blank it.
- Arp Pattern: 11 contour icons (`04_picker_arp_pattern`). Arp Mode: 4 columns (1 / 2 / 3 / Random octaves) × 3 rows (no hold / hold / hold + retrigger); "no hold + random" does not exist and is greyed (`05_picker_arp_mode`).

## 7. Direct manipulation on the screen
- Harmonics: drag to draw, shift = fine, alt-drag = straight line, wheel = nudge selected, double-click = reset; SOFT / LOUD toggle; 64 bars.
- Envelopes: drag points (rate / level), alt-click add or remove, shift = fine, wheel = next / previous harmonic, LP1 / LP2 / loop-off switches.
- Everything has undo / redo and a hover value readout. Touch: no hover-only features.

## 8. Open questions
1. Expanded screen: toggle button or automatic on dense pages?
2. Arp Speed: BPM only or host-tempo sync with a note value?
3. Is WRITE / SAVE one button, or "Save" and "Export" separate?
4. Which page is the default after a patch change: PLAY or stay on the current edit page?

## 9. Document model (draft — agreed direction, details open)
The current patch (and the current multi) is a **document**, like a text file in an editor.
- **Open** = load from a bank slot. **Dirty** = edited since load/save (shown as `*` next to the name). **Close** = load another patch, new/init, or quit.
- Closing a dirty document **always asks**: SAVE / SAVE AS… / DISCARD / CANCEL (`07_save_prompt.svg`, Enter = Save, Esc = Cancel). Clean documents close silently.
- **Undo / redo** per document, unlimited within the session, cleared on close. A knob / envelope drag is one undo step (begin / end gesture). Snapshot based (a patch is a few KB), so it is cheap and exact. **Compare** = toggle between the edited and the last saved version.
- **Banks:** fixed slot counts by kind (singles 128, multis 64); any number of banks, each a file. User banks live in the per-user app-data folder (Linux `~/.config/amateurtoolsdsp/k5k/user/banks/`, Windows `%APPDATA%`, macOS `~/Library/Application Support`; JUCE `userApplicationDataDirectory`). Bank files are K5000-compatible images (.kaa) so import / export / SysEx stay trivial. Imported banks are read-only: SAVE on one becomes SAVE AS into a user bank.
- **SAVE** writes to the document's slot in its user bank. **SAVE AS…** opens the bank page in "choose a slot" mode.
- **DAW project:** the working copy (including unsaved edits) is stored in the host project state, so closing a project never loses edits. Saving to a bank is separate.
- **Host / MIDI program change** never prompts (it cannot ask, and automation must not block): the dirty edits are kept as a recovery copy (Undo history of the previous document) and the new patch loads. Setting to choose: switch silently / ignore while dirty.
- **Crash safety:** the working copy is also written to a recovery file every few seconds while dirty.
- Open: should audition-browsing (quick preset flipping without editing) feel any different? Clean documents never prompt, so no.

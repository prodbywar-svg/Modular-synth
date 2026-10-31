# Modular-synth

A stark drum machine, step sequencer and effects rack for Windows, styled after
Ryoji Ikeda's *datamatics*. Record a live performance, then export it as a master
WAV plus one WAV per instrument.

![Aesthetic reference](docs/aesthetic-reference.png)

## Getting the app

1. On this repo's GitHub page, open **Releases** (right-hand side) and click the newest build.
2. Download **Modular-synth-windows.zip**, unzip it, and double-click `Modular-synth.exe`.
3. Windows may warn that the app is from an unknown publisher. Click **More info → Run anyway**.
4. Click **Options** (top-left of the window) to pick your audio output (and a MIDI controller if you have one).

### On a phone (or any browser)

Open **https://prodbywar-svg.github.io/Modular-synth/** and tap **TAP TO START**.
It is the same engine and sound as the Windows app, with a touch layout: tabs
for **SEQ** (drums and steps), **PADS** (play live), **SOUND** (drum and
effects settings) and **FILE** (export WAV, save and open projects). Turn the
phone sideways for the full 8 x 16 grid.

On iPhone: Safari → Share → **Add to Home Screen** opens it full screen like an
app. Exported WAVs go to the share sheet (Save to Files, AirDrop...). Projects
are the same `.msynth` files as on Windows, so they move between the two.

## The studio

The app opens two windows:

- the **studio window**: PLAY / STOP / REC, EXPORT, SAVE / OPEN, tempo and swing,
  one row per tool, and the studio **MASTER** effects rack that every tool goes through;
- a window for each **tool**: the **drum machine**, up to four **bass synths**
  up to four **loopers** and up to four **vocal tracks** for your audio interface.

Each tool's row in the studio window shows its sound as a scrolling barcode, its
**LEVEL**, **M** / **S** (mute / solo the whole tool), **OPEN** / **HIDE** for its
window, and **REMOVE**, which takes it out of the project (it asks first).
**+ BASS SYNTH**, **+ LOOPER**, **+ VOCAL** (and **+ DRUM MACHINE** once it has been removed) under the rows
adds a fresh tool, already in time with the others. Closing a tool's window only
hides it: it keeps playing.

Every window can be made much smaller: below full height the controls panel
folds away, and narrower still a tool's window shows just **P1-P4** and each
row's signal box (its number boxes still play). The studio window folds its
master rack away the same way.

All tools run on one clock: one tempo, one swing, patterns switch together at the
end of the bar. Space, Esc, R, E, Ctrl+S and Ctrl+O work from any window. Saved
projects remember which tools are in, and where their windows sit. Projects
from earlier versions open as a studio with just the drum machine.

## Playing it

| Do this | To |
| --- | --- |
| `Space` / **PLAY** | start and stop the pattern |
| Click a step | switch it on or off; drag up/down on a lit step to set how hard it hits |
| **P1**-**P4** / `F1`-`F4` | pick a pattern. While playing, the new one waits until the current pattern ends (outlined in blue while it waits) |
| **CLEAR** / `Delete` | empty the pattern on screen and reset every drum effect (tracks and drum bus) to its default (`Ctrl+Z` brings it all back) |
| **RESET** | put every drum machine setting back to how the app starts: each drum's sound, character, controls, level, pan, mute/solo, every effect and the drum bus. Sequences, loaded samples and the studio's tempo are kept (`Ctrl+Z` brings the old settings back) |
| **COPY DRUM**, then **PASTE** (bottom panel), or `Ctrl+C` / `Ctrl+V` | copy the selected drum's sequence, then select another drum (or switch pattern) and paste it there |
| **COPY PATTERN**, then **PASTE**, or `Ctrl+Shift+C` / `Ctrl+V` | duplicate every drum's sequence: copy on P1, switch to P2, paste |
| **RANDOM** (bottom panel) or `Ctrl+R` | write a fresh random sequence for the selected drum on the pattern on screen; press again for another. Kicks lean on the beat, hats run, snares find the backbeat |
| `Ctrl+Z` | undo the last clear, paste, random or reset |
| `Shift`+click **P1**-**P4** / `Shift`+`F1`-`F4` | copy the pattern on screen into that slot |
| Click the number box (01-08), or press `1`-`8` / `A S D F G H J K` | hit a pad live (`Shift` = softer) |
| Click a track name | show its controls in the bottom panel |
| Right-click a track name | change its voice or character, copy / paste / randomize its sequence, or load your own sample |
| **LIVE** / **DIGITAL** / **HR-16** (bottom panel) | switch the selected drum's character |
| Drag a control left/right | change it (`Shift` = fine, double-click = reset). Pitch controls snap to notes; hold `Alt` to go between them |
| **MS** / **NOTE** switches | set a time control in milliseconds or in note lengths that follow the tempo |
| Drag **BPM** / **SWING** up or down | change tempo and swing |
| **M** / **S** | mute / solo a track |
| Click **DRUM BUS** | the drum machine's own effects rack (all 8 drums, before the studio master) |
| `R` / **REC** | record a performance (starts the pattern from step 1) |
| **EXPORT** / `E` | save the recording, or the pattern looped, as WAV files |
| `Ctrl+S` / `Ctrl+O` | save / open a project (`.msynth`) |

A MIDI controller also works: notes 36-43 (standard drum pads) or C4-G4 hit pads 1-8.

## The bass synth

A monophonic bass synth in the same 16-step, 4-pattern grid: 8 rows, one sound
each, every row its own voice. Each lit step holds a **note**, shown in its tag
(`C2`, `D#2`...), a **length** (the line after the tag, up to 16 steps) and a
**velocity** (the number under it).

| Do this | To |
| --- | --- |
| Click an empty step | add a note (the row's last note, one step long) |
| Drag a note up / down | change its pitch, one step of the scale at a time (the mouse wheel does the same) |
| Drag a note sideways | make it longer or shorter |
| `Shift` + drag up / down | set its velocity |
| Click a note (without dragging) / right-click it | remove it |
| **KEY** / **SCALE** | lock every note to a key and scale (13 scales, CHROMATIC = no lock). Changing either moves every note into it (`Ctrl+Z` undoes) |
| Drag from one row's jack (the **CUT** column) to another's | join them with a patch cable: rows joined by cables form a **cut group** (letters A, B... by the jacks), where a new note cuts the one still ringing in the others. Rows with no cable ring on their own |
| Drag a cable's jack onto nothing / right-click a jack | unplug it / unplug every cable at that jack |
| `Z S X D C V G B H N J M` (and on to `/`) | play the selected row live like a piano, snapped to the scale; `[` `]` change the octave |
| `1`-`8`, up / down | select a row |
| Click a row's number box | hear its note |
| Right-click a row name | change its sound, copy / paste / randomize its sequence |

**P1-P4**, **CLEAR**, **RESET**, **COPY ROW / COPY PATTERN / PASTE**, **RANDOM** (in
the key and scale, shaped by the sound) and `Ctrl+Z` work as in the drum machine.
Notes still hold their length when they overlap; a new note on a row while the
last is still held slides into it (**GLIDE**).

| Sound | TONE control |
| --- | --- |
| SUB | sine, HARMONICS adds the overtones |
| PULSE | pulse wave, WIDTH |
| SAW | saw with a SUB OSC an octave down |
| ACID | saw / square (WAVE) through a screaming resonant filter |
| FM | two-operator FM, INDEX = growl |
| REESE | two saws, DETUNE apart |
| 1-BIT | 1-bit square, CRUSH lowers its sample rate |
| BOOM | 808-style sine with a pitch DROP |

Every sound also has CUTOFF, RESO, ENV AMT and F DECAY (a filter envelope),
DRIVE, GLIDE, and a full ATTACK / DECAY / SUSTAIN / RELEASE envelope, drawn
under the controls. Each row and the **BASS BUS** have the full effects rack.

## The looper

**For the lowest latency**, open **INPUT > AUDIO DEVICE AND INPUTS...**, set the
audio device type to **ASIO** and pick your interface (for example "Focusrite USB
ASIO"), then set a small buffer size (64 to 128 samples) in the interface's own
panel. Windows' own audio ("Windows Audio") adds much more delay.

Records your audio interface into a loop that plays on the studio clock. It has
four loop slots, **P1-P4**, that switch at the end of a bar like patterns do.

| Do this | To |
| --- | --- |
| **INPUT** | pick **IN 1** or **IN 2** (on both sides) or **IN 1+2** (a stereo pair). **AUDIO DEVICE AND INPUTS...** in the same menu picks the interface and which of its inputs are 1 and 2 |
| **MON** / `M` | hear the input while you play (use headphones, or turn it down, to avoid feedback) |
| **FX** / **DRY** (next to MON) / `Shift+M` | **FX**: hear the input through the looper's effects rack. **DRY**: hear it as it comes in. Recordings are always dry either way |
| **CLICK** / `K` | the metronome: a click on every beat, higher on the 1. **ON WHILE PLAYING** or **ONLY WHILE RECORDING** (this looper recording or overdubbing); `K` turns it on and off. Like the count-in, it is never recorded or exported |
| **COUNT 1** | the count-in: clicks on the beats for 1, 2 or 4 bars before a recording starts, or **NO COUNT**. The click is never recorded or exported |
| **2 BARS** | how long the slot's next recording is: 1, 2, 4 or 8 bars |
| **REC LOOP** / `L` | record into the slot on screen from the next bar (from the start when the transport is stopped: it starts it). When the bars are done the loop plays straight away. Press again to cancel |
| **OVERDUB** / `O` | record once round the loop on top of what is there, from the next bar |
| `Ctrl+Z` | take off the last recording or overdub, one at a time (or undo a clear / paste / random) |
| Click a slice step | mute it, or bring it back |
| Drag a slice step up / down (or the mouse wheel) | make it play another slice of the bar (each bar of a loop is cut into 16). Changed steps show white, with the slice they play |
| Right-click a slice step / **STRAIGHT** | play its own slice again / every step its own |
| **RANDOM** / `Ctrl+R` | rearrange the slices: stutters, jumps back to a beat, the odd silence |
| **COPY** / **PASTE** (`Ctrl+C` / `Ctrl+V`), `Shift`+click **P1-P4** | copy a slot (loop and slices) into another |
| **CLEAR** / `Delete` | empty the slot on screen and reset the looper's rack |

Loops are recorded before the effects rack, so the effects can change later, and
are moved back by the audio device's latency (shown at the top right) so they
land on the beat. A loop keeps its bars when the tempo changes, playing faster or
slower. The looper's rack and its **IN GAIN**, **LEVEL** and **PAN** are in the panel under
the rows. Saving a project writes each loop as a WAV file in a folder next to it
(`<project> loops`).

## The vocal track

Records your audio interface straight along the song, the way a DAW track does.
PLAY always starts at bar 1, so the recording plays back exactly where it was
sung, on every PLAY and in every export. Each vocal track has its own input, gain,
monitoring and effects rack, so (for example) a looper can take IN 2 while a
vocal track takes IN 1.

| Do this | To |
| --- | --- |
| **INPUT** | pick **IN 1** or **IN 2** (a microphone, in the middle; **PAN** moves it) or **IN 1+2** (a stereo pair). **AUDIO DEVICE AND INPUTS...** picks the interface and which of its inputs are 1 and 2 |
| **MON** / `M`, **FX** / **DRY** / `Shift+M` | hear yourself, through the vocal rack or dry (headphones avoid feedback). Recordings are always dry |
| **REC VOCAL** / `V` | record from the next bar (from bar 1 when the transport is stopped: it starts it), after the count-in. Press again (**STOP REC**) or STOP to finish: what was sung is kept |
| Record again later in the song | only the part you sing over is replaced (a punch-in); the rest stays. The takes it is pieced from show as lines under the recording (T1, T2...) |
| **COUNT 1**, **CLICK** / `K` | count-in and metronome, as on the looper: never recorded or exported |
| `Ctrl+Z` | take off the last recording (or undo a clear / reset) |
| **CLEAR** / `Delete` | empty the recording and reset the track's rack |
| **RESET** | input gain, level, pan, monitoring and the rack back to the start; the recording stays |

The recording is moved back by the audio device's latency so it lands on the beat.
It keeps its timing in seconds, so change the tempo before you record, not after.
Exports include each vocal track as its own stem (`VOX-1`); a vocal recorded during
a REC take is in that take's export. Saving a project writes each vocal track as one
WAV file in the `<project> loops` folder.

### The kit

Every drum comes in three characters:

- **DIGITAL**: the stark synthesized datamatics sounds (the default, listed below).
- **LIVE**: modelled acoustic drums: a kick with beater and shell, a metallic hi-hat,
  a snare with wires, a tom with stick and pitch bend, a hand clap.
- **HR-16**: samples from the Alesis HR-16 drum machine (built into the app). The
  SOUND control picks which one: 8 kicks, 13 hats and cymbals, 17 snares and rims,
  6 toms, 12 claps and percussion; TUNE shifts them up to an octave either way.

Pitch controls show the note as well as the frequency, e.g. `E1 41.2 Hz`.

| Track | Voice | Sound |
| --- | --- | --- |
| 01 | KICK | sine with a fast pitch drop, a click, then hard clipping (PITCH, DECAY, DRIVE, CLICK) |
| 02 | CLICK | single-sample impulse, high-passed (DECAY, TONE, NOISE) |
| 03 | NOISE | pure white-noise snare (DECAY, HIPASS, SNAP, COLOR) |
| 04 | SINE | high sine blip (PITCH, LENGTH, SWEEP) |
| 05 | GLITCH | bit-crushed sample-and-hold noise (RATE, BITS, LENGTH) |
| 06 | CLICK | longer, noisier click |
| 07 | SINE | low sine pulse (no steps yet) |
| 08 | SAMPLE | empty: right-click the name to load a WAV/AIFF/FLAC |

### Effects

Every drum, bass row, tool bus and the studio master have the same effects rack, in this order:

1. **Bitcrusher**: bit depth and sample-rate reduction.
2. **Distortion**: soft clip, hard clip, wavefold or rectify, with a tone control and dry/wet mix.
3. **Filter**: resonant low-pass.
4. **Chorus**: **ENSEMBLE** (three slow voices, wide and lush) or **DOUBLER** (two
   short delays left and right, like a part played twice), with RATE, DEPTH and MIX.
5. **Delay**: time in ms or note lengths (1/64 to 8 bars, including dotted and triplet).
6. **Grain delay**: chops the past into little overlapping grains. Time and grain size
   in ms or notes, pitch shift (±12 semitones), spray (random scatter), feedback.
7. **Reverb**: decay time in ms or notes (e.g. a tail exactly one bar long), damping.
8. **Psychosis**: up to six OTT-style three-band compressors in a row, squashing loud
   parts down and dragging quiet parts up. Release time in ms or notes.

### Effect automation

Press **AUTO** (studio window or any tool window) and play: every effect
control you move is written into the step that is playing, and holding a
control writes each step it passes. Play it back and the control glides from
one recorded step to the next, round the bar, without you touching it.

- It works on every rack: each drum, each bass row, the drum bus, the bass bus
  and the studio master. Each pattern (P1-P4) has its own moves; the studio
  master's loop every bar.
- Recorded steps show as blue marks under a control, and while playing the
  control and its number move in blue. Rows with moves on the pattern on screen
  get a blue **AUTO** tag.
- Right-click a control to clear its moves, or **CLEAR AUTO** for the whole rack.
  **CLEAR** clears the pattern's moves too (`Ctrl+Z` brings them back), and
  `Shift`+click **P1**-**P4** copies them with the pattern.
- Moves are saved with the project and rendered into exports.

### Recording and export

Press **REC** (studio window, or `R` in any window), play: hit pads, mute tracks, move any control in any tool. Press **REC** or
**STOP** to finish. The app stores what you *did*, not the audio, so the export
can rebuild it perfectly, once for the master and once per track. With a looper
in the studio it also keeps the audio input, so what you played into the loopers
(monitored or recorded) is in the export too.

**EXPORT** writes a folder containing:

- `<name>_Master.wav`: the full mix after the studio master effects
- `<name>_DRUMS.wav`, `<name>_BASS-1.wav`, `<name>_LOOP-1.wav`: each tool on its own (its own bus **and** its share of the
  studio master effects)
- `<name>_DRUMS-01-KICK.wav`, `<name>_BASS-1-04-ACID.wav`, ...: each track of a tool
  on its own, after its own effects, its tool's bus and its share of the master

All files are 24-bit stereo at the app's sample rate, start at the same moment
and have the same length (including the reverb/delay tail), so they line up in
any DAW. Tracks that never made a sound are skipped. Stems added together match
the master, except where the master bitcrusher, distortion or Psychosis is on (they
react to the whole mix, so each stem gets the effect as if it played alone).

## For developers

C++17, [JUCE 8](https://juce.com) (free licence), CMake.

```
cmake -S . -B build              # downloads JUCE automatically
cmake --build build --config Release
build/EngineTests_artefacts/Release/EngineTests            # engine tests
build/EngineTests_artefacts/Release/EngineTests --demo out # render demo WAVs (each character + effects)
```

Pass `-DJUCE_DIR=/path/to/JUCE` to use a local JUCE checkout, and
`-DMS_BUILD_VST3=ON` to also build the VST3 plugin (Milestone 3).

On Linux the app needs: `libasound2-dev libfreetype-dev libfontconfig1-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev
libxcomposite-dev libgl1-mesa-dev`.

Every push builds the Windows app on GitHub Actions; pushes to `main` publish it
as a release. The Windows build downloads Steinberg's ASIO SDK for ASIO support;
locally pass `-DMS_ASIO_INCLUDE=<asiosdk>/common` to turn it on. ASIO is a
trademark and software of Steinberg Media Technologies GmbH.

The browser version (`web/`) compiles the same `Source/engine` code to
WebAssembly with plain clang (no JUCE: `MS_NO_JUCE`), and pushes to `main`
publish it on GitHub Pages.

```
./web/build.sh                          # needs clang + wasi-libc + libc++ for wasm32
cd web/site && python3 -m http.server   # then open http://localhost:8000
node web/tests/parity.mjs <native_render>  # wasm output must match the native engine
```

### Layout

- `Source/engine`: the instrument with no UI or audio-device code.
  `StudioEngine` (every tool on one clock, through the studio master) and
  `StudioState` (the tools, studio commands, save/open),
  `EngineCore` (the drum machine: sequencer, patterns, voices, mixer, drum bus),
  `BassEngine` / `BassVoice` / `BassState` (the bass synth: notes, scales, cut
  groups, its 8 sounds), `LooperEngine` / `LooperState` (the looper: loop slots,
  bar-synced recording, slices), `VocalEngine` / `VocalState` (the vocal track:
  recordings pieced into a clip along the song, punch-ins), `Automation` (recorded effect moves), `Voice` (every drum voice
  in its three characters), `Hr16Bank` (the embedded HR-16 samples in
  `assets/hr16`), `FxChain` (effects rack) and `Effects` (grain delay, reverb,
  Psychosis, chorus), `OfflineRenderer` (take replay and WAV export),
  `ProjectState` (project data, commands, save/open).
- `Source/app`: `PluginProcessor` runs the engine on the audio thread, receives
  UI commands through a lock-free queue and records them with sample positions;
  `PluginEditor` is the studio window, which opens each tool's window
  (`DrumMachineView`, `BassSynthView`, `LooperView`, `VocalView`); `SignalBox` draws the signal panels; `Shared` and `Widgets` hold what they share;
  `Standalone` reaches the standalone app's audio device (input, latency).
- `web/wasm/WebEngine.cpp`: the engine as plain C functions for the browser.
  `web/site`: the page (`app.js`), the audio worklet that runs the engine
  (`worklet.js`) and the WAV reader/writer.
- `tests/EngineTests.cpp`: headless tests (timing, determinism, stems summing to
  the master, export files, recorded-event replay, save/open).

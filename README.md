# VocalChop Studio

A MIDI-triggered vocal chop / slice instrument built with [JUCE](https://juce.com)
and exported as **VST3**, **AU**, and **Standalone**. Licensed under
**GPL-3.0** (see `LICENSE.md`).

## Release checklist

- [x] CI builds (Windows / macOS / Linux) with downloadable artifacts
- [x] pluginval strictness-5 run in CI
- [x] GPL-3.0 licensing notice (`LICENSE.md`)
- [x] Green CI run confirmed — all 3 OS build + pluginval SUCCESS
      (run: https://github.com/luiseluise0619-wq/instrument/actions/runs/28557216889)
- [ ] Manual smoke test in a DAW (load sample → slice → play → automate)
- [ ] macOS only: code-sign + notarize before distributing to others
- [ ] Include the full GPL text as `COPYING` in binary distributions

Load a vocal (or any sample), slice it by transient onsets or an even grid,
then trigger the slices from MIDI or the on-screen pad grid. Each chop runs
through a pitch/formant shaper, an optional granular texture, an FX chain
(drive → reverb → delay), stereo widening, and a look-ahead brick-wall limiter.

## Signal flow

```
MIDI note ─► SliceEngine (transient / grid) ─► VoicePool (polyphonic playback)
          ─► PitchFormant (dual-tap pitch shift + formant tilt, dry/wet)
          ─► GranularEngine (bypassed until its mix is raised)
          ─► FXChain: Distortion → Reverb → Delay
          ─► Stereo Width (mid/side)
          ─► Limiter (look-ahead brick-wall)
```

MIDI note **C3 (48)** maps to slice 1; each higher semitone selects the next
slice. You can also drop an audio file directly onto the waveform display.

## Parameters

| ID          | Range        | Default | Purpose                              |
|-------------|--------------|---------|--------------------------------------|
| `pitch`     | -24 … +24 st | 0       | Pitch shift                          |
| `formant`   | -12 … +12 st | 0       | Formant / spectral tilt              |
| `mix`       | 0 … 1        | 1       | Pitch/formant dry/wet                |
| `width`     | 0 … 2        | 1       | Stereo width (mid/side)              |
| `grainSize` | 20 … 500 ms  | 80      | Granular grain length                |
| `drive`     | 0 … 1        | 0       | tanh soft-clip distortion            |
| `reverb`    | 0 … 1        | 0.25    | Room reverb wet level                |
| `delay`     | 0 … 1        | 0.2     | Feedback delay wet level             |
| `delayFeedback` | 0 … 0.95 | 0.4     | Delay feedback amount                |
| `pingpong`  | on / off     | off     | Ping-pong (L↔R) delay                |
| `filterCutoff` | 20 … 20k Hz | 20k   | Filter cutoff (log)                  |
| `filterReso`| 0.1 … 8      | 0.707   | Filter resonance (Q)                 |
| `filterType`| Off/LP/HP/BP | Off     | Filter mode                          |
| `attack`    | 0 … 500 ms   | 5       | Amp envelope attack                  |
| `decay`     | 0 … 2000 ms  | 0       | Amp envelope decay                   |
| `sustain`   | 0 … 1        | 1       | Amp envelope sustain level           |
| `release`   | 0 … 2000 ms  | 20      | Amp envelope release                 |
| `reverse`   | on / off     | off     | Reverse slice playback               |
| `playMode`  | Gate/One-Shot| Gate    | Note-off behaviour                   |
| `outputGain`| -24 … +6 dB  | 0       | Output trim                          |

Six factory presets ship in the preset menu: **Init, Clean Chops, Vocal
Shimmer, Lo-Fi Tape, Reverse Swell, Hard Stutter**. The editor also shows an
output level meter and live playheads over the waveform for active voices.

## Project layout

```
Source/
  PluginProcessor.*        Audio processing + MIDI → slice routing
  PluginEditor.*           Main UI container
  AudioEngine/
    SampleLoader.*         WAV/AIFF/FLAC/Ogg decoding (MP3 where available)
    SliceEngine.*          Transient + grid + manual slicing
    PitchFormant.*         Dual-tap pitch shift + formant tilt
    GranularEngine.*       Overlapping-grain texture
    FXChain.*              Distortion / reverb / delay
    VoicePool.*            Polyphonic slice playback
  DSP/
    Biquad.h               RBJ biquad filter (header-only)
    Limiter.h              Look-ahead brick-wall limiter (header-only)
    TransientDetector.h    HFC onset detection (header-only)
  UI/
    ThemeManager.*         4 neon / glassmorphism themes
    WaveformView.*         Animated waveform + drag-and-drop loading
    KnobComponent.*        Soft-shadow rotary knob
    SliceGrid.*            Clickable slice pad grid
    FXRack.*               FX amount rack
```

## Download (Releases)

Ready-made builds live on the **Releases** page:
grab `VocalChopStudio-vX.Y.Z-Windows.zip` (or macOS/Linux), unzip, and copy
`VocalChop Studio.vst3` into your VST3 folder
(`C:\Program Files\Common Files\VST3\` on Windows). Each zip includes the
Standalone app and the GPL licence text.

First run: the plugin opens in **Synth** mode with the Supersaw Lead patch, so
it makes sound immediately — or hit **Demo** to load the embedded demo vocal
and start chopping. Loading any sample (button or drag-and-drop) re-slices it
right away and switches the engine to Chop automatically; if transient
detection finds too few slices it falls back to an even 16-part grid.

You can play three ways: MIDI, clicking the on-screen keys (click-and-hold
gates the note, sliding plays glissando), or **typing on the computer
keyboard** — `Z S X D C V G B H N J M` is the lower octave and
`Q 2 W 3 E R 5 T 6 Y 7 U` the upper, FL Studio-style.

## Get a build without installing anything (CI)

Every push builds the plugin automatically on GitHub Actions for
**Windows / macOS / Linux** and runs [pluginval](https://github.com/Tracktion/pluginval)
(strictness 5) against the VST3. To download a ready-made build:

1. Open the repo's **Actions** tab on GitHub.
2. Click the latest **Build & Validate** run (green check = build passed).
3. Scroll to **Artifacts** and download e.g. `VocalChopStudio-Windows-VST3`.
4. Unzip and copy the `VocalChop Studio.vst3` folder to
   `C:\Program Files\Common Files\VST3\`, then rescan plugins in your DAW.

## Building

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE is fetched automatically:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

To build against an existing JUCE checkout instead of downloading it:

```bash
cmake -B build -DJUCE_SOURCE_DIR=/path/to/JUCE
cmake --build build
```

Built plugins land under `build/VocalChopStudio_artefacts/`.

## Using it in a DAW

VocalChop Studio is an **instrument / generator** (not an insert effect), so it
receives MIDI notes and outputs the chopped audio:

- **FL Studio** (Windows): loads the **VST3** as a generator in the Channel Rack;
  play slices from the piano roll (MIDI C3 = slice 1) or the on-screen pads.
- **Ableton / Logic / Cubase / Reaper**: load it on an **instrument/MIDI track**
  (Logic uses the AU). FL Studio does not use AU.

Drop an audio file onto the waveform (or click *Load Sample*), pick a slice mode,
then trigger slices with MIDI.

### Synth engine

Switch the **Engine** selector to **Synth** and the instrument makes sound with
no sample at all: a 16-voice, 2-oscillator synth (PolyBLEP saw/square, sine,
triangle) with a detune control, driven by the same ADSR, filter and FX chain.
The on-screen keyboard and MIDI both play it (C3-based, velocity-sensitive).

**100 designed instruments** ship in the Instrument picker, grouped by category
— BASS (Neon Bass, Sub 808, Reese, Wobble Growl, Pluck Bass, Analog Warm,
FM Knock), LEAD (Supersaw, Retro, Acid, Chip, Scream), SYNTH (Analog Poly,
PWM Strings, Hoover, 80s Poly, FM Digital), PIANO (Grand, Bright, Soft,
House), GUITAR (Nylon, Steel String, Clean, Muted, Funk), PAD (Dream,
Warm Strings, Dark, Glass, Choir Air, Analog Sweep), PLUCK (Crystal, Kalimba,
Marimba), KEYS (EP, Soft, House Organ, Retro Organ, Funk Clav), BELL
(Glass Bell, Deep Bell) and MISC (Airy Flute, Synth Brass, Noise Riser).
Picking one dials in the full engine architecture plus knob defaults, and the
**Synth card** exposes the modules as live knobs — Unison, Spread, Sub, Noise,
FM, Vibrato and Chorus — so every patch is hand-tweakable, Serum-style. The **Chords** bar suggests progressions by style
(K-Pop, EDM, Lo-Fi, R&B, Ballad, City Pop) from a curated built-in library:
hit *Generate* for a new progression and click a chord button to hear it
through the active engine.

### Demo samples

Two royalty-free demo samples ship in `examples/` so you have something to load
straight away:

- `examples/vocal_chop_demo.wav` — a vowel-formant "synth vocal" melody (try the
  Pitch / Formant knobs and Grid slicing).
- `examples/drum_loop_120bpm.wav` — a 120 BPM loop with sharp hits (try Transient
  slicing).

They are generated by `tools/generate_demo_samples.py` (pure Python, no deps) —
re-run it to regenerate.

## Implementation notes

- **Pitch / formant shifting** uses [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)
  (MIT), a high-quality spectral engine with proper formant handling: pitch and
  formant are shifted independently and formants are compensated so shifting
  pitch keeps the natural vocal character instead of the "chipmunk" effect. It
  is fetched automatically via CMake `FetchContent` (with its `dsp/` submodule).
- The engine has inherent latency, which is reported to the host via
  `setLatencySamples`; the dry path is delay-matched so the dry/wet mix stays
  phase-aligned. Pin `-DSIGNALSMITH_STRETCH_TAG=<commit>` for reproducible builds.
- **MP3** decoding depends on the platform codec JUCE exposes; WAV/AIFF/FLAC/Ogg
  work everywhere `registerBasicFormats()` covers.
- The **FX rack** UI edits effect amounts; the processing order in the engine is
  fixed (drag-reorder is a future enhancement).

### Real-time safety

- **Sample-rate correct**: slices are resampled from the sample's native rate to
  the host rate (linear interpolation) so a 48 kHz sample plays at the right
  pitch in a 44.1 kHz session.
- **Thread-safe sample/slice handoff**: loading a new sample and re-slicing
  happen on the message thread; playing voices hold a `shared_ptr` to the buffer
  so it can't be freed underneath them, and the audio thread reads slices via a
  try-lock (dropping a trigger on the rare contended block rather than racing).
- **Pad triggers** go through a lock-free FIFO to the audio thread — the UI never
  touches the voice pool directly.
- **Parameter smoothing**: pitch mix, stereo width, and the FX amounts
  (drive / reverb / delay) are smoothed (~20 ms) to avoid zipper noise. No
  allocation, locks, or string work on the audio thread.

# VocalChop Studio

A MIDI-triggered vocal chop / slice instrument built with [JUCE](https://juce.com)
and exported as **VST3**, **AU**, and **Standalone**.

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
| `attack`    | 0 … 200 ms   | 5       | Per-slice attack ramp                |

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

## Implementation notes

- **Pitch shifting** uses a CPU-cheap time-domain dual-tap variable-delay line
  with complementary Hann crossfades (no amplitude modulation, click-free). For
  mastering-grade quality, swap in SoundTouch or zplane élastique inside
  `PitchFormant::processChannel`.
- **Formant** is approximated as a first-order spectral tilt rather than a full
  cepstral envelope shift.
- **MP3** decoding depends on the platform codec JUCE exposes; WAV/AIFF/FLAC/Ogg
  work everywhere `registerBasicFormats()` covers.
- The **FX rack** UI edits effect amounts; the processing order in the engine is
  fixed (drag-reorder is a future enhancement).

#!/usr/bin/env python3
"""
Generates royalty-free demo samples for VocalChop Studio using only the Python
standard library (no numpy). Produces 44.1 kHz / 16-bit stereo WAVs:

  examples/vocal_chop_demo.wav   - a vowel-morphing "synth vocal" melody
                                   ("ah" -> "ee" -> "oo") with soft breath
                                   onsets and gentle vibrato (great for the
                                   pitch / formant knobs and grid slicing)
  examples/drum_loop_120bpm.wav  - a groovy 120 BPM drum loop with sharp
                                   transients: kick + snare + hats plus ghost
                                   snares and extra hats (great for transient
                                   slicing)
  examples/drum_loop_90bpm.wav   - a laid-back 90 BPM half-time drum loop
                                   (a second tempo to chop against)
  examples/chord_stab_pad.wav    - a warm chord/pad stab sequence (great for
                                   pads, texture and harmonic slicing)

Run:  python3 tools/generate_demo_samples.py
"""

import math
import os
import random
import struct
import wave

SR = 44100
random.seed(7)  # deterministic output


def write_wav(path, left, right):
    n = min(len(left), len(right))
    with wave.open(path, "w") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for i in range(n):
            l = int(max(-1.0, min(1.0, left[i])) * 32767)
            r = int(max(-1.0, min(1.0, right[i])) * 32767)
            frames += struct.pack("<hh", l, r)
        w.writeframes(bytes(frames))


def normalize(buf, peak=0.9):
    m = max((abs(x) for x in buf), default=1.0) or 1.0
    g = peak / m
    return [x * g for x in buf]


# ---------------------------------------------------------------------------
# 1) Vowel-morphing "synth vocal" melody
# ---------------------------------------------------------------------------
# Each vowel is a set of formants: (centre Hz, bandwidth, peak gain).
VOWELS = {
    "ah": [(800.0, 90.0, 1.0), (1150.0, 110.0, 0.7), (2900.0, 150.0, 0.35)],
    "ee": [(300.0, 70.0, 1.0), (2300.0, 130.0, 0.85), (3000.0, 160.0, 0.4)],
    "oo": [(320.0, 70.0, 1.0), (820.0, 100.0, 0.55), (2400.0, 150.0, 0.2)],
}


def lerp(a, b, x):
    return a + (b - a) * x


def morph_formants(fa, fb, x):
    """Linearly interpolate between two vowel formant tables."""
    out = []
    for (ca, ba, pa), (cb, bb, pb) in zip(fa, fb):
        out.append((lerp(ca, cb, x), lerp(ba, bb, x), lerp(pa, pb, x)))
    return out


def breath_onset(dur=0.045):
    """A short filtered-noise burst -> a soft breath/consonant transient."""
    n = int(dur * SR)
    out = [0.0] * n
    prev = 0.0
    lp = 0.0
    for i in range(n):
        t = i / SR
        # fast attack, quick decay so the Transient slicer sees a clean onset
        env = (min(1.0, i / (0.004 * SR))) * math.exp(-t * 55.0)
        nz = random.uniform(-1.0, 1.0)
        hp = nz - prev  # crude high-pass to get an airy "hh" hiss
        prev = nz
        lp += 0.35 * (hp - lp)  # tame the very top end a touch
        out[i] = lp * env * 0.6
    return out


def vowel_note(freq, dur, formants_a, formants_b, breath=False):
    n = int(dur * SR)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        # morph the vowel across the note (start -> end)
        x = i / n
        formants = morph_formants(formants_a, formants_b, x)
        vib = 1.0 + 0.012 * math.sin(2 * math.pi * 5.2 * t)  # gentle vibrato
        s = 0.0
        for h in range(1, 34):
            f = freq * h * vib
            if f > SR * 0.5:
                break
            g = 0.0
            for (fc, bw, pk) in formants:
                g += pk * math.exp(-((f - fc) ** 2) / (2 * bw * bw))
            s += (1.0 / h) * g * math.sin(2 * math.pi * f * t)
        out[i] = s
    # attack / release envelope so each note has a clean transient
    atk = int(0.02 * SR)
    rel = int(0.10 * SR)
    for i in range(n):
        e = 1.0
        if i < atk:
            e = i / atk
        if i > n - rel:
            e = max(0.0, (n - i) / rel)
        out[i] *= e
    # prepend a soft breath/consonant onset on selected notes
    if breath:
        b = breath_onset()
        for i in range(min(len(b), n)):
            out[i] += b[i]
    return out


def make_vocal():
    # A minor pentatonic phrase, morphing "ah" -> "ee" -> "oo" across it.
    melody = [220.00, 261.63, 293.66, 329.63, 392.00, 329.63, 293.66, 261.63]
    # vowel at each note; the note morphs from its vowel to the next note's.
    vowels = ["ah", "ah", "ee", "ee", "ee", "oo", "oo", "oo"]
    # give clear onsets on the phrase-start and accented notes
    breaths = [True, False, True, False, True, False, True, False]
    dur = 0.36
    mono = []
    for idx, f in enumerate(melody):
        va = VOWELS[vowels[idx]]
        vb = VOWELS[vowels[min(idx + 1, len(vowels) - 1)]]
        mono.extend(vowel_note(f, dur, va, vb, breath=breaths[idx]))
    mono = normalize(mono, 0.9)
    # Tiny haas-style widen for a stereo feel.
    delay = int(0.008 * SR)
    left = mono
    right = [0.0] * delay + mono[: len(mono) - delay]
    return add_reverb(left, right, wet=0.22)  # lush tail so it isn't dry/"synthy"


# ---------------------------------------------------------------------------
# 2) Drum voices with sharp transients
# ---------------------------------------------------------------------------
def add_kick(buf, start, gain=0.95):
    dur = 0.30
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        f = 110.0 * math.exp(-t * 18.0) + 45.0
        env = math.exp(-t * 8.0)
        buf[start + i] += math.sin(2 * math.pi * f * t) * env * gain


def add_snare(buf, start, gain=0.75):
    dur = 0.20
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        env = math.exp(-t * 22.0)
        tone = math.sin(2 * math.pi * 185.0 * t) * 0.4
        noise = random.uniform(-1.0, 1.0) * 0.7
        buf[start + i] += (tone + noise) * env * gain


def add_hat(buf, start, dur=0.05, gain=0.4):
    prev = 0.0
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        env = math.exp(-t * 60.0)
        n = random.uniform(-1.0, 1.0)
        hp = n - prev  # crude high-pass
        prev = n
        buf[start + i] += hp * env * gain


def make_drums():
    """Groovy one-bar 120 BPM loop: kick + snare + hats, plus ghost snares
    and extra off-beat hats for a bit of swing/movement."""
    bpm = 120.0
    sixteenth = (60.0 / bpm) / 4.0  # 16th note grid
    total = int(sixteenth * 16 * SR)  # one bar of 16 sixteenths
    buf = [0.0] * total

    def pos(step):
        return int(step * sixteenth * SR)

    # hats: on every 8th, plus a couple of extra off-beat 16ths for groove
    for s in (0, 2, 4, 6, 8, 10, 12, 14):
        add_hat(buf, pos(s))
    for s in (7, 15):  # extra ghost hats pushing the groove
        add_hat(buf, pos(s), gain=0.28)

    # kick on beats 1 and 3, with a syncopated pickup
    add_kick(buf, pos(0))
    add_kick(buf, pos(8))
    add_kick(buf, pos(11), gain=0.7)  # syncopated kick

    # snare (backbeat) on beats 2 and 4
    add_snare(buf, pos(4))
    add_snare(buf, pos(12))
    # ghost snares between the backbeats
    add_snare(buf, pos(7), gain=0.28)
    add_snare(buf, pos(14), gain=0.32)

    buf = normalize(buf, 0.9)
    return buf, list(buf)


def make_drums_90():
    """Laid-back half-time 90 BPM loop as a second tempo option."""
    bpm = 90.0
    sixteenth = (60.0 / bpm) / 4.0
    total = int(sixteenth * 16 * SR)  # one bar of 16 sixteenths
    buf = [0.0] * total

    def pos(step):
        return int(step * sixteenth * SR)

    # steady hats on the 8ths with a swung extra
    for s in (0, 2, 4, 6, 8, 10, 12, 14):
        add_hat(buf, pos(s))
    add_hat(buf, pos(11), gain=0.26)

    # half-time feel: kick on 1, big snare on beat 3
    add_kick(buf, pos(0))
    add_kick(buf, pos(10), gain=0.7)
    add_snare(buf, pos(8))
    add_snare(buf, pos(3), gain=0.25)  # ghost

    buf = normalize(buf, 0.9)
    return buf, list(buf)


# ---------------------------------------------------------------------------
# 3) Warm chord / pad stab sequence
# ---------------------------------------------------------------------------
def chord_stab(freqs, dur):
    """A warm detuned-saw-ish stab with a soft lowpass and pad-like envelope."""
    n = int(dur * SR)
    out = [0.0] * n
    lp = 0.0
    for i in range(n):
        t = i / SR
        s = 0.0
        for f in freqs:
            # two slightly detuned partials per note for warmth
            for det in (0.997, 1.003):
                ff = f * det
                # summed low harmonics -> mellow, organ/pad-ish tone
                for h in (1, 2, 3):
                    s += (1.0 / (h * h)) * math.sin(2 * math.pi * ff * h * t)
        s /= len(freqs)
        lp += 0.18 * (s - lp)  # gentle lowpass -> "warm"
        out[i] = lp
    # soft attack, long-ish release for a pad-stab feel
    atk = int(0.03 * SR)
    rel = int(0.25 * SR)
    for i in range(n):
        e = 1.0
        if i < atk:
            e = i / atk
        if i > n - rel:
            e = max(0.0, (n - i) / rel)
        out[i] *= e
    return out


def make_chords():
    # A minor -> F major -> C major -> G major (i-VI-III-VII vibe)
    chords = [
        [220.00, 261.63, 329.63],  # Am
        [174.61, 220.00, 261.63],  # F
        [261.63, 329.63, 392.00],  # C
        [196.00, 246.94, 293.66],  # G
    ]
    dur = 0.7
    mono = []
    for ch in chords:
        mono.extend(chord_stab(ch, dur))
    mono = normalize(mono, 0.9)
    # subtle stereo widen
    delay = int(0.011 * SR)
    left = mono
    right = [0.0] * delay + mono[: len(mono) - delay]
    return add_reverb(left, right, wet=0.30)  # lush pad


# ---------------------------------------------------------------------------
# Schroeder reverb (Freeverb-style combs + allpass) for lush, less-"synthy" tone
# ---------------------------------------------------------------------------
def _comb(x, delay, fb, damp):
    n = len(x)
    out = [0.0] * n
    buf = [0.0] * delay
    idx = 0
    store = 0.0
    for i in range(n):
        d = buf[idx]
        store = d * (1.0 - damp) + store * damp
        buf[idx] = x[i] + store * fb
        out[i] = d
        idx = idx + 1 if idx + 1 < delay else 0
    return out


def _allpass(x, delay, fb):
    n = len(x)
    out = [0.0] * n
    buf = [0.0] * delay
    idx = 0
    for i in range(n):
        d = buf[idx]
        out[i] = -x[i] + d
        buf[idx] = x[i] + d * fb
        idx = idx + 1 if idx + 1 < delay else 0
    return out


def _reverb_mono(x, combs, allpasses, damp=0.25, fb=0.84):
    acc = [0.0] * len(x)
    for cd in combs:
        c = _comb(x, cd, fb, damp)
        for i in range(len(x)):
            acc[i] += c[i]
    for ap in allpasses:
        acc = _allpass(acc, ap, 0.5)
    return acc


def add_reverb(left, right, wet=0.22, tail_s=0.6):
    extra = int(tail_s * SR)
    L = left + [0.0] * extra
    R = right + [0.0] * extra
    wetL = _reverb_mono(L, [1116, 1188, 1277, 1356], [556, 441])
    wetR = _reverb_mono(R, [1139, 1211, 1300, 1379], [579, 464])
    outL = [L[i] * (1.0 - wet) + wetL[i] * wet for i in range(len(L))]
    outR = [R[i] * (1.0 - wet) + wetR[i] * wet for i in range(len(R))]
    m = max(max((abs(v) for v in outL), default=1.0),
            max((abs(v) for v in outR), default=1.0), 1e-9)
    g = 0.92 / m
    return [v * g for v in outL], [v * g for v in outR]


# ---------------------------------------------------------------------------
# A finished "vocal chop" groove — shows what chopping actually sounds like:
# a vocal is sliced and re-sequenced rhythmically over a light beat.
# ---------------------------------------------------------------------------
def make_vocal_chop_groove():
    notes = [261.63, 329.63, 392.00, 329.63]
    vwl = ["ah", "ee", "oo", "ee"]
    src = []
    for i, f in enumerate(notes):
        va = VOWELS[vwl[i]]
        vb = VOWELS[vwl[(i + 1) % len(vwl)]]
        src.extend(vowel_note(f, 0.28, va, vb, breath=(i % 2 == 0)))
    src = normalize(src, 0.9)

    n_sl = 8
    sl_len = max(1, len(src) // n_sl)
    slices = [src[i * sl_len:(i + 1) * sl_len] for i in range(n_sl)]

    bpm = 120.0
    step = (60.0 / bpm) / 4.0  # 16th grid
    pattern = [0, -1, 2, 2, 4, -1, 5, 3, 0, 7, 2, -1, 4, 6, 5, -1]
    total = int(step * len(pattern) * SR) + sl_len
    buf = [0.0] * total

    for s, idx in enumerate(pattern):
        if idx < 0:
            continue
        p = int(s * step * SR)
        sl = slices[idx % n_sl]
        for i in range(len(sl)):
            if p + i < total:
                buf[p + i] += sl[i] * 0.9

    # light beat under the chops
    for s in range(len(pattern)):
        p = int(s * step * SR)
        add_hat(buf, p, gain=0.22)
        if s in (0, 8):
            add_kick(buf, p, gain=0.8)
        if s in (4, 12):
            add_snare(buf, p, gain=0.5)

    buf = normalize(buf, 0.9)
    return add_reverb(buf, list(buf), wet=0.20)


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(here, "examples")
    os.makedirs(out_dir, exist_ok=True)

    l, r = make_vocal()
    write_wav(os.path.join(out_dir, "vocal_chop_demo.wav"), l, r)
    print("wrote examples/vocal_chop_demo.wav   (%.1f s)" % (len(l) / SR))

    l, r = make_drums()
    write_wav(os.path.join(out_dir, "drum_loop_120bpm.wav"), l, r)
    print("wrote examples/drum_loop_120bpm.wav  (%.1f s)" % (len(l) / SR))

    l, r = make_drums_90()
    write_wav(os.path.join(out_dir, "drum_loop_90bpm.wav"), l, r)
    print("wrote examples/drum_loop_90bpm.wav   (%.1f s)" % (len(l) / SR))

    l, r = make_chords()
    write_wav(os.path.join(out_dir, "chord_stab_pad.wav"), l, r)
    print("wrote examples/chord_stab_pad.wav    (%.1f s)" % (len(l) / SR))

    l, r = make_vocal_chop_groove()
    write_wav(os.path.join(out_dir, "vocal_chop_groove.wav"), l, r)
    print("wrote examples/vocal_chop_groove.wav (%.1f s)" % (len(l) / SR))


if __name__ == "__main__":
    main()

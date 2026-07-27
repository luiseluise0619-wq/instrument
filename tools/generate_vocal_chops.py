#!/usr/bin/env python3
"""
Generates royalty-free vocal-chop source material for Slyce, standard library
only. Every sample is formant synthesis: a glottal pulse train with jitter and
shimmer, driven through three band-pass resonators tuned to a real vowel's
F1/F2/F3, plus breath noise. That is the same mechanism the plugin's own vocal
patches use, which is why these chop and pitch-shift like a voice instead of
smearing like a synth pad.

Each file is written so the transient detector finds the phrase boundaries on
its own - short gaps between syllables, hard onsets.

Run:  python3 tools/generate_vocal_chops.py
"""

import math
import random
import struct
import wave
import os

SR = 44100
OUT = os.path.join(os.path.dirname(__file__), "..", "examples")

# F1, F2, F3 in Hz, with relative amplitudes. Measured vowel formants.
VOWELS = {
    "a": ((730, 1090, 2440), (1.00, 0.50, 0.22)),
    "e": ((530, 1840, 2480), (1.00, 0.62, 0.28)),
    "i": ((270, 2290, 3010), (1.00, 0.70, 0.34)),
    "o": ((570,  840, 2410), (1.00, 0.46, 0.16)),
    "u": ((300,  870, 2240), (1.00, 0.38, 0.12)),
}


def biquad_bp(f, q):
    """Constant-peak band-pass coefficients."""
    w = 2.0 * math.pi * f / SR
    al = math.sin(w) / (2.0 * q)
    a0 = 1.0 + al
    return (al / a0, 0.0, -al / a0, (-2.0 * math.cos(w)) / a0, (1.0 - al) / a0)


def run_bp(x, coef):
    b0, b1, b2, a1, a2 = coef
    y = [0.0] * len(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, s in enumerate(x):
        o = b0 * s + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, s
        y2, y1 = y1, o
        y[i] = o
    return y


def glottal(n, f0, jitter=0.008, shimmer=0.06, breath=0.05, vib=(5.2, 0.012)):
    """A voiced source: band-limited pulse train, humanised."""
    out = [0.0] * n
    phase = 0.0
    amp = 1.0
    vr, vd = vib
    for i in range(n):
        t = i / SR
        f = f0 * (1.0 + vd * math.sin(2 * math.pi * vr * t)
                      + random.uniform(-jitter, jitter))
        phase += f / SR
        if phase >= 1.0:
            phase -= 1.0
            amp = 1.0 + random.uniform(-shimmer, shimmer)
        # A rounded glottal pulse: steep close, gentle open. Squaring the
        # rising half is what gives a voice its buzz without aliasing hard.
        p = phase
        g = (p * p * (3.0 - 2.0 * p)) * 2.0 - 1.0 if p < 0.62 else -0.62
        out[i] = g * amp + random.uniform(-breath, breath)
    return out


def say(n, f0, vowel, q=9.0, breath=0.05, vib=(5.2, 0.012)):
    src = glottal(n, f0, breath=breath, vib=vib)
    (f1, f2, f3), (g1, g2, g3) = VOWELS[vowel]
    a = run_bp(src, biquad_bp(f1, q))
    b = run_bp(src, biquad_bp(f2, q * 1.15))
    c = run_bp(src, biquad_bp(f3, q * 1.3))
    return [(a[i] * g1 + b[i] * g2 + c[i] * g3) for i in range(n)]


def env(n, atk, rel, hold=None):
    """Percussive-to-sung envelope in samples."""
    a = max(1, int(atk * SR))
    r = max(1, int(rel * SR))
    h = n - a - r if hold is None else int(hold * SR)
    out = []
    for i in range(n):
        if i < a:
            out.append((i / a) ** 0.6)
        elif i < a + h:
            out.append(1.0)
        else:
            k = (i - a - h) / max(1, r)
            out.append(max(0.0, (1.0 - k)) ** 1.6)
    return out


def place(buf, at, seg, gain=1.0, pan=0.0):
    l = math.cos((pan + 1) * math.pi / 4)
    r = math.sin((pan + 1) * math.pi / 4)
    for i, s in enumerate(seg):
        j = at + i
        if 0 <= j < len(buf[0]):
            buf[0][j] += s * gain * l
            buf[1][j] += s * gain * r


def normalise(buf, peak=0.89):
    m = max(max(abs(x) for x in ch) for ch in buf) or 1.0
    g = peak / m
    return [[x * g for x in ch] for ch in buf]


def write(path, buf):
    """Written MONO on purpose. These are chop SOURCES: the plugin has a Width
    control and a stereo chorus of its own, and every one of these ships inside
    the binary. Stereo would double the download for a placement the user is
    going to override anyway."""
    buf = normalise(buf)
    n = len(buf[0])
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for i in range(n):
            m = (buf[0][i] + buf[1][i]) * 0.5
            frames += struct.pack("<h", int(max(-1, min(1, m)) * 32767))
        w.writeframes(bytes(frames))
    print("  %-26s %5.1f s  %6.0f KB" % (os.path.basename(path), n / SR,
                                          os.path.getsize(path) / 1024))


def blank(seconds):
    n = int(seconds * SR)
    return [[0.0] * n, [0.0] * n]


def hz(midi):
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


# --------------------------------------------------------------------------
def chant():
    """Rhythmic staccato syllables — the classic chop source. 128 BPM."""
    random.seed(11)
    beat = 60.0 / 128
    buf = blank(beat * 32 + 1)
    notes = [69, 69, 72, 76, 74, 72, 69, 67,
             69, 72, 74, 76, 77, 76, 74, 72]
    vows = "aoieaoieaeioaeio"
    for k, m in enumerate(notes * 2):
        dur = beat * 0.42
        n = int(dur * SR)
        seg = say(n, hz(m - 12), vows[k % len(vows)], q=11.0, breath=0.04)
        e = env(n, 0.006, 0.10)
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(k * beat * SR), seg, 0.9, (k % 4 - 1.5) * 0.18)
    return buf


def hook():
    """A sung melodic phrase with real note lengths — chop it or play it."""
    random.seed(23)
    buf = blank(9.0)
    phrase = [(0.00, 76, 0.55, "a"), (0.55, 74, 0.35, "e"), (0.95, 72, 0.75, "a"),
              (1.85, 69, 0.55, "o"), (2.45, 72, 0.40, "e"), (2.90, 74, 1.10, "a"),
              (4.20, 77, 0.55, "i"), (4.80, 76, 0.35, "e"), (5.20, 74, 0.80, "a"),
              (6.10, 72, 0.50, "o"), (6.70, 69, 1.60, "u")]
    for t, m, d, v in phrase:
        n = int(d * SR)
        seg = say(n, hz(m - 12), v, q=8.0, breath=0.055, vib=(5.6, 0.018))
        e = env(n, 0.03, min(0.35, d * 0.5))
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(t * SR), seg, 0.95)
    return buf


def choir():
    """Stacked vowels a fifth and an octave apart — pads and long chops."""
    random.seed(37)
    buf = blank(10.0)
    chords = [(0.0, [57, 64, 69], "a"), (2.5, [53, 60, 65], "o"),
              (5.0, [55, 62, 67], "e"), (7.5, [57, 64, 72], "a")]
    for t, notes, v in chords:
        for i, m in enumerate(notes):
            n = int(2.4 * SR)
            seg = say(n, hz(m - 12), v, q=7.0, breath=0.07, vib=(4.4, 0.010))
            e = env(n, 0.35, 0.9)
            seg = [seg[j] * e[j] for j in range(n)]
            place(buf, int(t * SR), seg, 0.55, (i - 1) * 0.55)
    return buf


def whisper():
    """Breathy, unpitched-leaning texture — air, risers, ad-libs."""
    random.seed(53)
    buf = blank(8.0)
    for k in range(14):
        t = k * 0.55 + random.uniform(-0.04, 0.04)
        d = random.uniform(0.28, 0.62)
        n = int(d * SR)
        v = "aeiou"[k % 5]
        seg = say(n, hz(random.choice([69, 72, 74, 76]) - 12), v,
                  q=5.0, breath=0.55, vib=(6.0, 0.02))
        e = env(n, 0.09, d * 0.55)
        seg = [seg[i] * e[i] * 0.8 for i in range(n)]
        place(buf, int(t * SR), seg, 0.8, random.uniform(-0.7, 0.7))
    return buf


def diva():
    """High belted runs with a wide vibrato — the top-line chop."""
    random.seed(71)
    buf = blank(8.5)
    runs = [(0.0, [81, 79, 77, 76, 74], 0.16), (1.2, [76, 77, 79, 81], 0.20),
            (2.4, [81, 84, 81, 79], 0.22), (3.8, [77, 76, 74, 72], 0.18),
            (5.0, [74, 76, 77, 79, 81], 0.20), (6.4, [81], 1.6)]
    for t, notes, step in runs:
        for i, m in enumerate(notes):
            d = step if len(notes) > 1 else step
            n = int(d * SR)
            seg = say(n, hz(m - 12), "a" if i % 2 == 0 else "e",
                      q=10.0, breath=0.04, vib=(6.4, 0.028))
            e = env(n, 0.012, d * 0.6)
            seg = [seg[j] * e[j] for j in range(n)]
            place(buf, int((t + i * step) * SR), seg, 0.95)
    return buf


def stabs():
    """Short hard vowel stabs on a 16th grid — the densest chop source."""
    random.seed(97)
    sixteenth = 60.0 / 128 / 4
    buf = blank(sixteenth * 64 + 0.6)
    pattern = [1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1]
    notes = [69, 72, 76, 74]
    for k in range(64):
        if not pattern[k % 16]:
            continue
        d = sixteenth * 0.8
        n = int(d * SR)
        seg = say(n, hz(notes[(k // 4) % 4] - 12), "aeio"[k % 4],
                  q=13.0, breath=0.03)
        e = env(n, 0.004, d * 0.7)
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(k * sixteenth * SR), seg, 0.92, (k % 3 - 1) * 0.3)
    return buf


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("generating vocal chop sources…")
    for name, fn in [("vox_chant", chant), ("vox_hook", hook),
                     ("vox_choir", choir), ("vox_whisper", whisper),
                     ("vox_diva", diva), ("vox_stabs", stabs)]:
        write(os.path.join(OUT, name + ".wav"), fn())
    print("done.")

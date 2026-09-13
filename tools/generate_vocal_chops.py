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




# ==========================================================================
# The second wave. Ten sources was not enough to build a track from - a
# chopper lives or dies on how much raw material ships with it - so this
# block adds twenty-six more, grouped by what you would reach for them FOR
# rather than by how they were synthesised. Same formant machine, mono,
# every seed fixed so a rebuild is byte-identical.
# ==========================================================================

def _rev(buf):
    return [list(reversed(ch)) for ch in buf]


def _noise(n, lo, hi, q=1.2):
    """Band-passed noise - breath, clicks, sibilance."""
    src = [random.uniform(-1.0, 1.0) for _ in range(n)]
    a = run_bp(src, biquad_bp(lo, q))
    b = run_bp(a, biquad_bp(hi, q))
    return b


def _syl(n, midi, vowel, q=9.0, breath=0.05, vib=(5.2, 0.012),
         atk=0.008, rel=None):
    """One shaped syllable, ready to place."""
    seg = say(n, hz(midi - 12), vowel, q=q, breath=breath, vib=vib)
    e = env(n, atk, (n / SR) * 0.55 if rel is None else rel)
    return [seg[i] * e[i] for i in range(n)]


# --- chops ----------------------------------------------------------------
def triplet_chop():
    """Syllables on a triplet grid - the swung chop. 140 BPM."""
    random.seed(201)
    trip = 60.0 / 140 / 3
    buf = blank(trip * 48 + 0.5)
    notes = [69, 72, 74, 76, 72, 69]
    for k in range(48):
        if k % 6 == 4:
            continue
        d = trip * 0.82
        n = int(d * SR)
        place(buf, int(k * trip * SR),
              _syl(n, notes[k % 6], "aeiou"[k % 5], q=12.0, breath=0.03,
                   atk=0.005), 0.9, (k % 3 - 1) * 0.25)
    return buf


def halftime_chop():
    """Long lazy syllables on a half-time grid - trap and drill sit here."""
    random.seed(202)
    beat = 60.0 / 70
    buf = blank(beat * 8 + 1.0)
    notes = [69, 67, 72, 69, 65, 67, 72, 74]
    for k in range(8):
        d = beat * random.uniform(0.55, 0.9)
        n = int(d * SR)
        place(buf, int(k * beat * SR),
              _syl(n, notes[k], "aoe"[k % 3], q=8.0, breath=0.06,
                   vib=(4.6, 0.02), atk=0.02), 0.92, (k % 2 - 0.5) * 0.5)
    return buf


def garage_chop():
    """Clipped 2-step syllables with the gaps UK garage leaves. 132 BPM."""
    random.seed(203)
    sixteenth = 60.0 / 132 / 4
    buf = blank(sixteenth * 48 + 0.5)
    pattern = [1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0]
    notes = [76, 74, 72, 69]
    for k in range(48):
        if not pattern[k % 16]:
            continue
        d = sixteenth * random.uniform (0.45, 0.7)
        n = int(d * SR)
        place(buf, int(k * sixteenth * SR),
              _syl(n, notes[(k // 3) % 4], "ieo"[k % 3], q=14.0, breath=0.02,
                   atk=0.003), 0.95, (k % 4 - 1.5) * 0.35)
    return buf


def amen_vox():
    """Mouth-drum syllables arranged like a break - kick, snare, ghost."""
    random.seed(204)
    sixteenth = 60.0 / 165 / 4
    buf = blank(sixteenth * 64 + 0.5)
    # 0 rest, 1 low "b", 2 bright "k", 3 ghost
    grid = [1, 0, 3, 2, 0, 1, 0, 2, 3, 0, 1, 2, 0, 3, 2, 0]
    for k in range(64):
        kind = grid[k % 16]
        if kind == 0:
            continue
        if kind == 1:
            d, n_ = 0.13, 48
            n = int(d * SR)
            seg = _syl(n, n_, "u", q=6.0, breath=0.10, atk=0.002, rel=0.09)
        elif kind == 2:
            d = 0.10
            n = int(d * SR)
            seg = _noise(n, 1600, 5200, 0.9)
            e = env(n, 0.001, 0.07)
            seg = [seg[i] * e[i] * 0.75 for i in range(n)]
        else:
            d = 0.05
            n = int(d * SR)
            seg = _noise(n, 900, 3000, 1.1)
            e = env(n, 0.001, 0.035)
            seg = [seg[i] * e[i] * 0.30 for i in range(n)]
        place(buf, int(k * sixteenth * SR), seg, 0.9, (k % 5 - 2) * 0.2)
    return buf


def stutter_vox():
    """One syllable retriggered at accelerating rates - the build-up chop."""
    random.seed(205)
    buf = blank(6.0)
    t = 0.0
    gap = 0.30
    while t < 5.4:
        d = min(gap * 0.9, 0.26)
        n = int(d * SR)
        place(buf, int(t * SR), _syl(n, 76, "e", q=13.0, breath=0.03,
                                     atk=0.003), 0.92)
        t += gap
        gap *= 0.90
        if gap < 0.045:
            gap = 0.045
    return buf


def reverse_chant():
    """The chant, backwards - sucked-in swells that land ON the beat."""
    random.seed(206)
    beat = 60.0 / 120
    buf = blank(beat * 8 + 0.6)
    for k in range(8):
        d = beat * 0.9
        n = int(d * SR)
        place(buf, int(k * beat * SR),
              _syl(n, [69, 72, 76, 74][k % 4], "aeio"[k % 4], q=10.0,
                   breath=0.05, atk=0.01), 0.9, (k % 2 - 0.5) * 0.4)
    return _rev(buf)


# --- hooks ----------------------------------------------------------------
def pop_topline():
    """A singable four-bar phrase - the one you chop into a hook."""
    random.seed(211)
    buf = blank(8.0)
    phrase = [(0.00, 76, 0.45), (0.50, 74, 0.30), (0.85, 72, 0.55),
              (1.50, 74, 0.35), (1.90, 76, 0.70), (2.75, 79, 0.90),
              (3.80, 76, 0.40), (4.25, 74, 0.35), (4.65, 72, 0.75),
              (5.50, 69, 0.45), (6.05, 72, 0.50), (6.65, 74, 1.20)]
    for i, (t, m, d) in enumerate(phrase):
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "aeiou"[i % 5], q=10.0, breath=0.04,
                   vib=(5.6, 0.018), atk=0.014), 0.95)
    return buf


def trap_melody():
    """Sparse minor-key phrases with long tails, pitched for drill and trap."""
    random.seed(212)
    buf = blank(8.0)
    phrase = [(0.0, 69, 0.9), (1.1, 72, 0.55), (1.8, 71, 0.5),
              (2.5, 69, 1.3), (4.1, 67, 0.8), (5.1, 69, 0.55),
              (5.8, 72, 0.5), (6.5, 69, 1.4)]
    for i, (t, m, d) in enumerate(phrase):
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "ou"[i % 2], q=8.0, breath=0.05,
                   vib=(4.8, 0.024), atk=0.02), 0.93)
    return buf


def afro_hook():
    """Bouncing off-beat syllables in a major pentatonic - afrobeats."""
    random.seed(213)
    eighth = 60.0 / 108 / 2
    buf = blank(eighth * 32 + 0.8)
    penta = [72, 74, 76, 79, 81]
    hits = [0, 1, 3, 4, 6, 7, 9, 11, 12, 14, 15, 17, 18, 20, 22, 23,
            25, 26, 28, 30]
    for i, k in enumerate(hits):
        d = eighth * random.uniform(0.7, 1.15)
        n = int(d * SR)
        place(buf, int(k * eighth * SR),
              _syl(n, penta[i % 5], "aeo"[i % 3], q=11.0, breath=0.04,
                   atk=0.008), 0.92, (i % 3 - 1) * 0.35)
    return buf


def drill_hook():
    """Slid, sighing phrases - the pitched-down UK drill topline."""
    random.seed(214)
    buf = blank(8.0)
    for t, m, d in [(0.0, 64, 1.1), (1.3, 67, 0.7), (2.2, 65, 1.4),
                    (3.9, 62, 1.0), (5.1, 64, 0.8), (6.1, 60, 1.6)]:
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "ou"[int(t) % 2], q=7.0, breath=0.07,
                   vib=(4.2, 0.03), atk=0.03), 0.9)
    return buf


def ballad_line():
    """Slow sustained notes with real vibrato - the ballad chop."""
    random.seed(215)
    buf = blank(9.0)
    for t, m, d in [(0.0, 72, 1.6), (1.8, 74, 1.3), (3.3, 76, 2.0),
                    (5.5, 74, 1.2), (6.9, 72, 1.9)]:
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "ae"[int(t) % 2], q=11.0, breath=0.035,
                   vib=(5.8, 0.030), atk=0.06), 0.94)
    return buf


# --- textures -------------------------------------------------------------
def vowel_pad():
    """Held stacked vowels that morph - a sustained bed to granulate."""
    random.seed(221)
    buf = blank(9.0)
    for i, (m, v) in enumerate([(60, "u"), (67, "o"), (72, "a"), (76, "e")]):
        n = int(8.6 * SR)
        seg = say(n, hz(m - 12), v, q=6.0, breath=0.05, vib=(3.4, 0.010))
        e = env(n, 1.2, 2.2)
        seg = [seg[j] * e[j] for j in range(n)]
        place(buf, int(0.1 * SR), seg, 0.42, (i - 1.5) * 0.5)
    return buf


def nasal_pad():
    """A tight, resonant hum - the nasal texture, good for formant sweeps."""
    random.seed(222)
    buf = blank(8.0)
    for i, m in enumerate([62, 69, 74]):
        n = int(7.6 * SR)
        seg = say(n, hz(m - 12), "i", q=17.0, breath=0.02, vib=(4.0, 0.008))
        e = env(n, 0.9, 1.8)
        seg = [seg[j] * e[j] for j in range(n)]
        place(buf, int(0.15 * SR), seg, 0.5, (i - 1) * 0.55)
    return buf


def hum_bed():
    """Closed-mouth hum, no consonants at all - the quietest bed here."""
    random.seed(223)
    buf = blank(8.0)
    for i, m in enumerate([48, 55, 60]):
        n = int(7.7 * SR)
        seg = say(n, hz(m - 12), "u", q=4.5, breath=0.03, vib=(3.0, 0.007))
        e = env(n, 1.4, 2.4)
        seg = [seg[j] * e[j] for j in range(n)]
        place(buf, int(0.1 * SR), seg, 0.55, (i - 1) * 0.4)
    return buf


def gospel_stack():
    """A wide six-voice chord with per-voice detune - the choir stab source."""
    random.seed(224)
    buf = blank(7.0)
    for t, chord in [(0.0, [60, 64, 67, 72]), (2.4, [57, 60, 65, 69]),
                     (4.6, [59, 62, 67, 71])]:
        for i, m in enumerate(chord):
            for det in (-0.09, 0.09):
                n = int(2.1 * SR)
                seg = say(n, hz(m - 12) * (1.0 + det * 0.01), "a" if i % 2 else "o",
                          q=8.0, breath=0.045, vib=(5.0, 0.014))
                e = env(n, 0.12, 1.1)
                seg = [seg[j] * e[j] for j in range(n)]
                place(buf, int(t * SR), seg, 0.34,
                      ((i - 1.5) * 0.45) + (0.2 if det > 0 else -0.2))
    return buf


def vocoder_bed():
    """Stepped vowel changes on one held pitch - a robotic talking bed."""
    random.seed(225)
    step = 0.16
    buf = blank(step * 44 + 0.6)
    for k in range(44):
        n = int(step * 1.02 * SR)
        seg = say(n, hz(69 - 12), "aeiou"[(k * 3) % 5], q=16.0, breath=0.015,
                  vib=(0.0, 0.0))
        e = env(n, 0.004, step * 0.35)
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(k * step * SR), seg, 0.9)
    return buf


# --- percussive / FX ------------------------------------------------------
def mouth_perc():
    """A full mouth-drum kit laid out one hit per slice."""
    random.seed(231)
    buf = blank(7.0)
    t = 0.12
    for kind in ["b", "k", "ts", "p", "sh", "d", "t", "f",
                 "b", "k", "ts", "sh", "p", "d", "t", "k"]:
        if kind in ("b", "p", "d"):
            n = int(0.15 * SR)
            seg = _syl(n, {"b": 45, "p": 50, "d": 55}[kind], "u",
                       q=5.0, breath=0.12, atk=0.002, rel=0.10)
        else:
            lo, hi, dur = {"k": (1400, 4800, 0.09), "ts": (3200, 9000, 0.06),
                           "sh": (2200, 7000, 0.20), "t": (2600, 8000, 0.05),
                           "f": (1800, 6000, 0.14)}[kind]
            n = int(dur * SR)
            seg = _noise(n, lo, hi, 0.9)
            e = env(n, 0.001, dur * 0.7)
            seg = [seg[i] * e[i] * 0.8 for i in range(n)]
        place(buf, int(t * SR), seg, 0.92)
        t += 0.42
    return buf


def tongue_clicks():
    """Dry clicks and pops - percussion with no pitch to fight your track."""
    random.seed(232)
    buf = blank(6.0)
    t = 0.15
    while t < 5.7:
        dur = random.uniform(0.018, 0.05)
        n = int(dur * SR)
        seg = _noise(n, random.uniform(900, 2600), random.uniform(4000, 11000), 1.4)
        e = env(n, 0.0008, dur * 0.8)
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(t * SR), seg, 0.95, random.uniform(-0.8, 0.8))
        t += random.uniform(0.22, 0.44)
    return buf


def breath_hits():
    """Inhales and exhales - the texture layer under a vocal chop."""
    random.seed(233)
    buf = blank(7.5)
    t = 0.2
    while t < 7.0:
        dur = random.uniform(0.35, 0.85)
        n = int(dur * SR)
        seg = _noise(n, random.uniform(500, 1100), random.uniform(3000, 6500), 0.8)
        # Inhales swell, exhales decay: alternate so both are in the file.
        rising = int(t * 2) % 2 == 0
        e = env(n, dur * (0.75 if rising else 0.05), dur * (0.2 if rising else 0.8))
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(t * SR), seg, 0.9, random.uniform(-0.5, 0.5))
        t += dur + random.uniform(0.12, 0.35)
    return buf


def gasp_fx():
    """Short voiced gasps and yelps - one-shot vocal punctuation."""
    random.seed(234)
    buf = blank(6.5)
    t = 0.15
    i = 0
    while t < 6.1:
        dur = random.uniform(0.12, 0.30)
        n = int(dur * SR)
        m = random.choice([72, 76, 79, 81, 84])
        seg = _syl(n, m, "aeiu"[i % 4], q=12.0, breath=0.16,
                   vib=(7.5, 0.03), atk=0.006)
        place(buf, int(t * SR), seg, 0.95, random.uniform(-0.7, 0.7))
        t += dur + random.uniform(0.18, 0.45)
        i += 1
    return buf


def riser_vox():
    """One voice climbing an octave and a half - chop it for a build."""
    random.seed(235)
    dur = 6.0
    n = int(dur * SR)
    buf = blank(dur + 0.2)
    # Rendered in short pitched segments so the glide is audible without a
    # resampler: 60 steps across 18 semitones.
    steps = 60
    seg_n = n // steps
    for k in range(steps):
        m = 62 + 18.0 * (k / steps) ** 1.35
        s = say(seg_n + 400, hz(m - 12), "iea"[k % 3], q=13.0, breath=0.04,
                vib=(6.5, 0.02))
        e = env(seg_n + 400, 0.004, 0.02)
        s = [s[i] * e[i] for i in range(seg_n + 400)]
        place(buf, k * seg_n, s, 0.35 + 0.6 * (k / steps))
    return buf


def downlifter_vox():
    """The riser inverted - a falling voice for drops and transitions."""
    return _rev(riser_vox())


# --- character ------------------------------------------------------------
def kids_vox():
    """A small, bright voice - short formants, high pitch, playful rhythm."""
    random.seed(241)
    eighth = 60.0 / 120 / 2
    buf = blank(eighth * 28 + 0.7)
    notes = [84, 86, 88, 84, 81, 84, 86, 88]
    for k in range(28):
        if k % 7 == 6:
            continue
        d = eighth * random.uniform(0.6, 0.95)
        n = int(d * SR)
        place(buf, int(k * eighth * SR),
              _syl(n, notes[k % 8], "iea"[k % 3], q=15.0, breath=0.05,
                   vib=(7.0, 0.02), atk=0.006), 0.92, (k % 3 - 1) * 0.3)
    return buf


def deep_male():
    """A chest-voice baritone - the low chop that sits under everything."""
    random.seed(242)
    buf = blank(8.0)
    for t, m, d in [(0.0, 45, 1.3), (1.5, 48, 0.9), (2.6, 43, 1.5),
                    (4.3, 45, 1.0), (5.5, 40, 2.0)]:
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "ou"[int(t) % 2], q=5.5, breath=0.06,
                   vib=(4.0, 0.012), atk=0.025), 0.95)
    return buf


def robot_vox():
    """No vibrato, no jitter, hard gates - a machine reading vowels."""
    random.seed(243)
    step = 0.11
    buf = blank(step * 52 + 0.5)
    for k in range(52):
        if k % 9 == 8:
            continue
        n = int(step * 0.95 * SR)
        seg = say(n, hz([60, 60, 67, 60, 63, 60][k % 6] - 12), "aeiou"[(k * 2) % 5],
                  q=20.0, breath=0.0, vib=(0.0, 0.0))
        e = env(n, 0.002, 0.008)
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(k * step * SR), seg, 0.9)
    return buf


def opera_vox():
    """Wide operatic vibrato on sustained high notes - the dramatic chop."""
    random.seed(244)
    buf = blank(9.0)
    for t, m, d in [(0.0, 79, 2.0), (2.2, 81, 1.5), (3.9, 84, 2.4),
                    (6.5, 79, 2.2)]:
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "ao"[int(t) % 2], q=9.0, breath=0.03,
                   vib=(6.2, 0.055), atk=0.10), 0.95)
    return buf


SECOND_WAVE = [
    ("vox_triplet",    triplet_chop),   ("vox_halftime",   halftime_chop),
    ("vox_garage",     garage_chop),    ("vox_amen",       amen_vox),
    ("vox_stutter",    stutter_vox),    ("vox_revchant",   reverse_chant),
    ("vox_topline",    pop_topline),    ("vox_trapmel",    trap_melody),
    ("vox_afro",       afro_hook),      ("vox_drill",      drill_hook),
    ("vox_ballad",     ballad_line),    ("vox_vowelpad",   vowel_pad),
    ("vox_nasal",      nasal_pad),      ("vox_hum",        hum_bed),
    ("vox_gospel",     gospel_stack),   ("vox_vocoder",    vocoder_bed),
    ("vox_mouthperc",  mouth_perc),     ("vox_clicks",     tongue_clicks),
    ("vox_breath",     breath_hits),    ("vox_gasp",       gasp_fx),
    ("vox_riser",      riser_vox),      ("vox_downlifter", downlifter_vox),
    ("vox_kids",       kids_vox),       ("vox_deep",       deep_male),
    ("vox_robot",      robot_vox),      ("vox_opera",      opera_vox),
]


# ==========================================================================
# THIRD WAVE. Sixteen more, aimed at the gaps the first thirty-six left:
# nothing in a minor/sad register, nothing you could lay under a whole
# section, no consonant-led syllables (which is what actually makes a chop
# sound like WORDS rather than like a vowel), and nothing wet enough to sit
# at the back of a mix without further processing.
# ==========================================================================

def _cons(n, kind, level=0.8):
    """A consonant burst to put in FRONT of a vowel. A chop reads as speech
    because of its onsets - pure vowels chop into something closer to a pad."""
    band = {"t": (2600, 9000, 0.9), "k": (1500, 5200, 0.8),
            "s": (3800, 11000, 1.3), "sh": (2000, 6800, 1.0),
            "f": (1700, 6000, 0.9), "ch": (2400, 8200, 1.1)}[kind]
    seg = _noise(n, band[0], band[1], band[2])
    e = env(n, 0.0015, (n / SR) * 0.75)
    return [seg[i] * e[i] * level for i in range(n)]


def _word(midi, vowel, dur, cons=None, q=11.0, breath=0.04, vib=(5.4, 0.016)):
    """consonant + vowel, concatenated - one syllable that starts with a hit."""
    out = []
    if cons is not None:
        cn = int(min(0.055, dur * 0.28) * SR)
        out += _cons(cn, cons)
    vn = int(dur * SR)
    out += _syl(vn, midi, vowel, q=q, breath=breath, vib=vib, atk=0.006)
    return out


# --- consonant-led chops --------------------------------------------------
def word_chop():
    """Consonant-led syllables - the closest this gets to actual words."""
    random.seed(301)
    sixteenth = 60.0 / 124 / 4
    buf = blank(sixteenth * 56 + 0.8)
    cons = ["t", "k", "sh", "ch", "s", "f"]
    notes = [72, 74, 76, 72, 69, 74]
    k = 0
    for step in range(56):
        if step % 4 == 3:
            continue
        seg = _word(notes[k % 6], "aeiou"[k % 5], sixteenth * 1.05,
                    cons[k % 6], q=13.0)
        place(buf, int(step * sixteenth * SR), seg, 0.92, (k % 3 - 1) * 0.28)
        k += 1
    return buf


def hard_consonant():
    """Ts and Ks with almost no vowel - percussive, cuts through a busy mix."""
    random.seed(302)
    sixteenth = 60.0 / 140 / 4
    buf = blank(sixteenth * 64 + 0.5)
    for k in range(64):
        if k % 8 in (2, 5):
            continue
        seg = _word(79, "i", sixteenth * 0.35, ["t", "k", "ch", "s"][k % 4], q=18.0)
        place(buf, int(k * sixteenth * SR), seg, 0.9, (k % 5 - 2) * 0.22)
    return buf


def whisper_chop():
    """Breathy consonant-led syllables, almost unvoiced - a texture chop."""
    random.seed(303)
    eighth = 60.0 / 96 / 2
    buf = blank(eighth * 32 + 0.9)
    for k in range(32):
        seg = _word([67, 69, 71, 72][k % 4], "uoae"[k % 4], eighth * 0.9,
                    ["f", "sh", "s"][k % 3], q=6.0, breath=0.55, vib=(4.0, 0.01))
        place(buf, int(k * eighth * SR), seg, 0.85, random.uniform(-0.6, 0.6))
    return buf


# --- minor / sad register -------------------------------------------------
def minor_hook():
    """A natural-minor phrase - the first thirty-six leant major."""
    random.seed(311)
    buf = blank(8.5)
    for t, m, d in [(0.0, 69, 0.7), (0.8, 72, 0.5), (1.4, 71, 0.5),
                    (2.0, 69, 1.1), (3.3, 67, 0.6), (4.0, 69, 0.5),
                    (4.6, 71, 0.9), (5.7, 69, 0.6), (6.4, 65, 1.7)]:
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "aoe"[int(t * 2) % 3], q=10.0, breath=0.045,
                   vib=(5.4, 0.022), atk=0.016), 0.94)
    return buf


def sad_pad():
    """A held minor stack - the bed under a sad section."""
    random.seed(312)
    buf = blank(9.5)
    for i, m in enumerate([57, 60, 64, 67, 72]):
        n = int(9.0 * SR)
        seg = say(n, hz(m - 12), "ou"[i % 2], q=6.5, breath=0.05, vib=(3.2, 0.011))
        e = env(n, 1.6, 2.6)
        seg = [seg[j] * e[j] for j in range(n)]
        place(buf, int(0.2 * SR), seg, 0.36, (i - 2) * 0.42)
    return buf


def lament():
    """Descending sighs with a heavy vibrato tail."""
    random.seed(313)
    buf = blank(9.0)
    t = 0.0
    for m in (76, 74, 72, 71, 69, 67):
        d = random.uniform(1.0, 1.6)
        n = int(d * SR)
        place(buf, int(t * SR),
              _syl(n, m, "ao"[int(t) % 2], q=8.5, breath=0.06,
                   vib=(5.0, 0.038), atk=0.05), 0.93)
        t += d * 0.78
    return buf


# --- long beds ------------------------------------------------------------
def sustain_bed():
    """Twelve seconds of one held chord - long enough to sit under a section
    without looping, which nothing in the first two waves was."""
    random.seed(321)
    buf = blank(12.5)
    for i, m in enumerate([48, 55, 60, 64]):
        n = int(12.0 * SR)
        seg = say(n, hz(m - 12), "uoa"[i % 3], q=5.0, breath=0.04, vib=(2.6, 0.008))
        e = env(n, 2.2, 3.4)
        seg = [seg[j] * e[j] for j in range(n)]
        place(buf, int(0.2 * SR), seg, 0.42, (i - 1.5) * 0.5)
    return buf


def swell_bed():
    """Slow swells in and out - built to be chopped ACROSS rather than at."""
    random.seed(322)
    buf = blank(11.0)
    t = 0.0
    i = 0
    while t < 10.0:
        d = random.uniform(1.8, 2.8)
        n = int(d * SR)
        seg = say(n, hz([60, 64, 67, 71][i % 4] - 12), "aeou"[i % 4],
                  q=7.0, breath=0.05, vib=(3.6, 0.012))
        e = env(n, d * 0.45, d * 0.5)
        seg = [seg[j] * e[j] for j in range(n)]
        place(buf, int(t * SR), seg, 0.7, (i % 3 - 1) * 0.45)
        t += d * 0.62
        i += 1
    return buf


# --- wet / ambient --------------------------------------------------------
def _tail(buf, decay=2.4, taps=26):
    """A cheap dense reverb tail: many decaying, slightly detuned delays."""
    out = [list(ch) for ch in buf]
    n = len(out[0])
    random.seed(999)
    for t in range(taps):
        d = int(random.uniform(0.013, decay * 0.45) * SR)
        g = (1.0 - t / taps) ** 2.1 * 0.34
        pan = random.uniform(-0.9, 0.9)
        l = math.cos((pan + 1) * math.pi / 4)
        r = math.sin((pan + 1) * math.pi / 4)
        for i in range(n - d):
            v = (buf[0][i] + buf[1][i]) * 0.5 * g
            out[0][i + d] += v * l
            out[1][i + d] += v * r
    return out


def ambient_vox():
    """Wet from the source - sits at the back of a mix with nothing added."""
    return _tail(minor_hook(), decay=3.0, taps=32)


def cathedral():
    """The choir, drowned. Long enough that a slice IS the reverb."""
    random.seed(331)
    buf = blank(10.0)
    for t, chord in [(0.0, [60, 64, 67]), (3.2, [57, 60, 65]), (6.4, [55, 59, 62])]:
        for i, m in enumerate(chord):
            n = int(2.6 * SR)
            seg = say(n, hz(m - 12), "ao"[i % 2], q=7.0, breath=0.04, vib=(4.4, 0.014))
            e = env(n, 0.35, 1.6)
            seg = [seg[j] * e[j] for j in range(n)]
            place(buf, int(t * SR), seg, 0.5, (i - 1) * 0.55)
    return _tail(buf, decay=3.6, taps=38)


def ghost_vox():
    """Reversed swells with a wet tail - the transition texture."""
    return _tail(_rev(swell_bed()), decay=2.2, taps=24)


# --- rhythm / utility -----------------------------------------------------
def dembow():
    """Syllables on the dembow pattern - reggaeton and afro sit on this."""
    random.seed(341)
    sixteenth = 60.0 / 96 / 4
    buf = blank(sixteenth * 64 + 0.7)
    hits = [0, 3, 6, 8, 11, 14]
    for bar in range(4):
        for j, h in enumerate(hits):
            k = bar * 16 + h
            seg = _word([72, 69, 74, 72][j % 4], "aoe"[j % 3], sixteenth * 1.3,
                        "k" if j % 2 == 0 else None, q=12.0)
            place(buf, int(k * sixteenth * SR), seg, 0.92, (j % 3 - 1) * 0.3)
    return buf


def swung_chop():
    """A hard triplet swing - the pocket the straight-grid chops cannot make."""
    random.seed(342)
    beat = 60.0 / 92
    buf = blank(beat * 10 + 0.8)
    for b in range(10):
        for off, m in ((0.0, 72), (0.66, 76), (0.5, 69)):
            d = beat * 0.3
            n = int(d * SR)
            place(buf, int((b + off) * beat * SR),
                  _syl(n, m, "aei"[b % 3], q=13.0, breath=0.03, atk=0.004),
                  0.9, (off - 0.33) * 1.4)
    return buf


def onebar_loop():
    """Exactly one bar at 120, so the grid slicer lands on clean sixteenths."""
    random.seed(343)
    sixteenth = 60.0 / 120 / 4
    buf = blank(sixteenth * 16)
    pattern = [1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1]
    for k in range(16):
        if not pattern[k]:
            continue
        seg = _word([72, 74, 76, 79][k % 4], "aeio"[k % 4], sixteenth * 0.78,
                    ["t", "k", None, "sh"][k % 4], q=14.0)
        place(buf, int(k * sixteenth * SR), seg, 0.93, (k % 4 - 1.5) * 0.3)
    return buf


def talkbox():
    """Vowel sweeps on a fixed pitch - the talkbox / vocoder lead source."""
    random.seed(344)
    buf = blank(8.0)
    t = 0.0
    i = 0
    while t < 7.4:
        d = random.uniform(0.30, 0.75)
        n = int(d * SR)
        # Slide the formant by crossfading two vowels across the segment.
        a = say(n, hz(69 - 12), "aeiou"[i % 5], q=15.0, breath=0.02, vib=(0.0, 0.0))
        b = say(n, hz(69 - 12), "aeiou"[(i + 2) % 5], q=15.0, breath=0.02, vib=(0.0, 0.0))
        e = env(n, 0.01, d * 0.4)
        seg = [(a[j] * (1 - j / n) + b[j] * (j / n)) * e[j] for j in range(n)]
        place(buf, int(t * SR), seg, 0.92)
        t += d * 0.95
        i += 1
    return buf


def shout_stack():
    """A crowd shout - many detuned voices on one syllable. Drop material."""
    random.seed(345)
    buf = blank(6.0)
    for t in (0.15, 1.7, 3.2, 4.7):
        for v in range(7):
            d = random.uniform(0.45, 0.7)
            n = int(d * SR)
            seg = _syl(n, 69 + random.choice([-12, 0, 0, 12]),
                       "ae"[v % 2], q=7.0, breath=0.14,
                       vib=(6.0 + v * 0.3, 0.02), atk=0.008)
            place(buf, int((t + random.uniform(-0.02, 0.02)) * SR), seg,
                  0.34, random.uniform(-0.85, 0.85))
    return buf


THIRD_WAVE = [
    ("vox_word",      word_chop),      ("vox_hardcons",  hard_consonant),
    ("vox_whispchop", whisper_chop),   ("vox_minor",     minor_hook),
    ("vox_sadpad",    sad_pad),        ("vox_lament",    lament),
    ("vox_sustain",   sustain_bed),    ("vox_swell",     swell_bed),
    ("vox_ambient",   ambient_vox),    ("vox_cathedral", cathedral),
    ("vox_ghost",     ghost_vox),      ("vox_dembow",    dembow),
    ("vox_swung",     swung_chop),     ("vox_onebar",    onebar_loop),
    ("vox_talkbox",   talkbox),        ("vox_shout",     shout_stack),
]


# ==========================================================================
# FOURTH WAVE. Twenty-four more: more genre pockets, more actual playable
# ad-libs, and more utility FX. These are still generated formant voices, not
# sample-pack lifts, so they remain royalty-free and reproducible.
# ==========================================================================

def neon_chop():
    """Fast bright hyperpop chops with tiny gaps and high formants."""
    random.seed(401)
    sixteenth = 60.0 / 158 / 4
    buf = blank(sixteenth * 80 + 0.45)
    notes = [81, 84, 86, 88, 84, 81, 79, 76]
    for k in range(80):
        if k % 10 in (4, 9):
            continue
        seg = _word(notes[k % 8], "iea"[k % 3], sixteenth * 0.62,
                    ["t", "ch", None, "s"][k % 4], q=16.0, breath=0.035,
                    vib=(7.0, 0.018))
        place(buf, int(k * sixteenth * SR), seg, 0.9, (k % 5 - 2) * 0.18)
    return buf


def jersey_chop():
    """Triplet bounce with call-response pockets for Jersey club."""
    random.seed(402)
    trip = 60.0 / 144 / 3
    buf = blank(trip * 54 + 0.5)
    hits = [0, 1, 3, 5, 6, 8, 10, 12, 13, 16]
    for bar in range(3):
        for j, h in enumerate(hits):
            k = bar * 18 + h
            seg = _word([72, 76, 79, 74][j % 4], "aeio"[j % 4], trip * 0.92,
                        ["k", None, "t", "sh"][j % 4], q=13.0, breath=0.035)
            place(buf, int(k * trip * SR), seg, 0.9, (j % 3 - 1) * 0.28)
    return buf


def amapiano_vox():
    """Loose, late syllables for amapiano / afro-house grooves."""
    random.seed(403)
    eighth = 60.0 / 112 / 2
    buf = blank(eighth * 40 + 0.7)
    hits = [0, 2, 3, 6, 8, 11, 12, 14, 17, 19, 22, 24, 27, 30, 31, 35]
    for i, k in enumerate(hits):
        late = random.uniform(0.0, eighth * 0.22)
        seg = _word([67, 69, 72, 74, 76][i % 5], "aoe"[i % 3],
                    eighth * random.uniform(0.7, 1.4),
                    None if i % 3 else "k", q=10.0, breath=0.055)
        place(buf, int((k * eighth + late) * SR), seg, 0.92, (i % 4 - 1.5) * 0.25)
    return buf


def footwork_chop():
    """Ultra-short repeating syllables for footwork / jungle edits."""
    random.seed(404)
    step = 60.0 / 160 / 4
    buf = blank(step * 96 + 0.35)
    for k in range(96):
        if k % 12 in (5, 11):
            continue
        seg = _word([72, 72, 79, 76][k % 4], "ie"[k % 2], step * 0.48,
                    ["t", "k", "ch", None][k % 4], q=18.0, breath=0.02)
        place(buf, int(k * step * SR), seg, 0.86, (k % 7 - 3) * 0.12)
    return buf


def latin_chop():
    """Open-vowel syncopation for pop reggaeton hooks."""
    random.seed(405)
    sixteenth = 60.0 / 100 / 4
    buf = blank(sixteenth * 64 + 0.6)
    hits = [0, 3, 4, 7, 10, 12, 15]
    for bar in range(4):
        for j, h in enumerate(hits):
            k = bar * 16 + h
            seg = _word([69, 72, 74, 76][(bar + j) % 4], "aoae"[j % 4],
                        sixteenth * 1.05, ["t", None, "k"][j % 3],
                        q=12.0, breath=0.04)
            place(buf, int(k * sixteenth * SR), seg, 0.92, (j % 3 - 1) * 0.24)
    return buf


def rnb_run():
    """Smooth melismatic run for R&B chops."""
    random.seed(411)
    buf = blank(8.0)
    t = 0.05
    runs = [[76, 77, 79, 81, 79, 77], [74, 76, 77, 79], [72, 74, 76, 77, 76, 74]]
    for r, notes in enumerate(runs):
        step = 0.135 + r * 0.015
        for i, m in enumerate(notes):
            n = int(step * SR)
            seg = _syl(n, m, "aei"[i % 3], q=10.0, breath=0.04,
                       vib=(6.0, 0.026), atk=0.010)
            place(buf, int((t + i * step) * SR), seg, 0.94)
        t += len(notes) * step + 0.75
    return buf


def kpop_hook():
    """Bright stacked-pop phrase with short callouts."""
    random.seed(412)
    buf = blank(7.5)
    phrase = [(0.0, 79, .34), (.42, 81, .28), (.75, 84, .46), (1.45, 81, .34),
              (1.86, 79, .42), (2.65, 76, .55), (3.55, 81, .30), (3.9, 84, .32),
              (4.35, 86, .52), (5.1, 84, .36), (5.55, 81, .95)]
    for i, (t, m, d) in enumerate(phrase):
        seg = _word(m, "iea"[i % 3], d, None if i % 4 else "ch",
                    q=13.0, breath=0.035, vib=(6.3, 0.020))
        place(buf, int(t * SR), seg, 0.93, (i % 3 - 1) * 0.18)
    return buf


def soul_phrase():
    """Warm mid-register phrase that feels sampled rather than synthetic."""
    random.seed(413)
    buf = blank(8.5)
    for i, (t, m, d) in enumerate([(0, 65, .9), (1.05, 69, .65), (1.85, 72, 1.0),
                                  (3.1, 71, .8), (4.0, 69, .75), (5.0, 65, 1.4),
                                  (6.6, 67, 1.1)]):
        n = int(d * SR)
        seg = _syl(n, m, "aoe"[i % 3], q=8.0, breath=0.075,
                   vib=(4.8, 0.020), atk=0.035)
        place(buf, int(t * SR), seg, 0.94)
    return buf


def falsetto_hook():
    """Airy high hook for pitched vocal leads."""
    random.seed(414)
    buf = blank(8.0)
    for i, (t, m, d) in enumerate([(0, 84, .7), (.9, 86, .5), (1.55, 88, .95),
                                  (2.9, 86, .45), (3.45, 84, .7), (4.45, 81, 1.5),
                                  (6.2, 84, 1.0)]):
        n = int(d * SR)
        seg = _syl(n, m, "ie"[i % 2], q=12.5, breath=0.12,
                   vib=(5.8, 0.024), atk=0.04)
        place(buf, int(t * SR), seg, 0.88)
    return buf


def alto_harmony():
    """Lower harmony phrase to stack under other chops."""
    random.seed(415)
    buf = blank(8.0)
    for i, (t, m, d) in enumerate([(0, 57, .85), (1.0, 60, .7), (1.85, 64, .9),
                                  (3.0, 62, 1.0), (4.25, 60, .7), (5.1, 57, 1.6)]):
        n = int(d * SR)
        seg = _syl(n, m, "oua"[i % 3], q=7.0, breath=0.055,
                   vib=(4.3, 0.016), atk=0.035)
        place(buf, int(t * SR), seg, 0.93)
    return buf


def monk_drone():
    """Dark chant drone with slow formant movement."""
    random.seed(421)
    buf = blank(11.0)
    for i, m in enumerate([43, 50, 55]):
        n = int(10.4 * SR)
        seg = say(n, hz(m - 12), "u" if i != 1 else "o", q=5.2,
                  breath=0.045, vib=(2.2, 0.006))
        e = env(n, 1.8, 2.8)
        seg = [seg[j] * e[j] for j in range(n)]
        place(buf, int(0.25 * SR), seg, 0.46, (i - 1) * 0.5)
    return _tail(buf, decay=2.8, taps=28)


def glass_choir():
    """Thin icy vowels for ambient pads."""
    random.seed(422)
    buf = blank(10.0)
    for i, m in enumerate([72, 76, 79, 83]):
        n = int(9.4 * SR)
        seg = say(n, hz(m - 12), "i" if i % 2 else "e", q=18.0,
                  breath=0.08, vib=(3.1, 0.009))
        e = env(n, 1.6, 2.5)
        seg = [seg[j] * e[j] * 0.55 for j in range(n)]
        place(buf, int(0.2 * SR), seg, 0.55, (i - 1.5) * 0.48)
    return _tail(buf, decay=2.0, taps=18)


def tape_warp_vox():
    """Wobbly vintage vowel phrase for lo-fi chops."""
    random.seed(423)
    buf = blank(8.5)
    t = 0.0
    for i, m in enumerate([69, 72, 71, 67, 69, 65, 64]):
        d = random.uniform(0.55, 1.05)
        n = int(d * SR)
        seg = _syl(n, m, "aoeu"[i % 4], q=7.0, breath=0.09,
                   vib=(2.2 + i * 0.15, 0.040), atk=0.025)
        place(buf, int(t * SR), seg, 0.9, random.uniform(-0.25, 0.25))
        t += d * 0.78
    return buf


def telephone_vox():
    """Narrow-band radio/phone voice, great before distortion."""
    random.seed(424)
    buf = blank(6.2)
    sixteenth = 60.0 / 118 / 4
    for k in range(42):
        if k % 6 == 4:
            continue
        n = int(sixteenth * 0.72 * SR)
        seg = _word([72, 74, 76, 72][k % 4], "aeio"[k % 4], sixteenth * 0.55,
                    ["t", None, "k"][k % 3], q=24.0, breath=0.02,
                    vib=(0.0, 0.0))
        place(buf, int(k * sixteenth * SR), seg, 0.85)
    return buf


def alien_formant():
    """Extreme formant-like chirps for character chops."""
    random.seed(425)
    buf = blank(6.4)
    t = 0.1
    for k in range(22):
        d = random.uniform(0.08, 0.23)
        n = int(d * SR)
        seg = say(n, hz(random.choice([57, 60, 72, 84]) - 12),
                  "ieou"[k % 4], q=random.uniform(18.0, 28.0),
                  breath=0.04, vib=(8.0, 0.06))
        e = env(n, 0.002, d * 0.55)
        seg = [seg[i] * e[i] for i in range(n)]
        place(buf, int(t * SR), seg, 0.86, random.uniform(-0.85, 0.85))
        t += random.uniform(0.16, 0.35)
    return buf


def choir_hit():
    """Short stacked choir hits for drops."""
    random.seed(431)
    buf = blank(6.0)
    for t, chord in [(0.1, [60, 64, 67, 72]), (1.55, [57, 60, 65, 69]),
                     (3.0, [55, 62, 67, 74]), (4.4, [60, 67, 72, 76])]:
        for i, m in enumerate(chord):
            n = int(0.72 * SR)
            seg = _syl(n, m, "ao"[i % 2], q=8.0, breath=0.07,
                       vib=(5.0, 0.018), atk=0.012, rel=0.38)
            place(buf, int(t * SR), seg, 0.38, (i - 1.5) * 0.45)
    return buf


def vocal_impact():
    """Big one-shot vocal impact with a noisy front edge."""
    random.seed(432)
    buf = blank(5.2)
    for t, m in [(0.1, 48), (1.9, 43), (3.55, 55)]:
        burst = _cons(int(0.08 * SR), "sh", 0.9)
        body = _syl(int(0.65 * SR), m, "o", q=5.8, breath=0.11,
                    vib=(3.8, 0.012), atk=0.004, rel=0.52)
        place(buf, int(t * SR), burst + body, 0.9)
    return _tail(buf, decay=1.3, taps=14)


def laugh_chop():
    """Short voiced laughs and ha's."""
    random.seed(433)
    buf = blank(6.0)
    t = 0.12
    for k in range(18):
        d = random.uniform(0.12, 0.28)
        seg = _word(random.choice([69, 72, 76, 79]), "a", d,
                    "ch" if k % 5 == 0 else None, q=9.0, breath=0.18,
                    vib=(7.0, 0.035))
        place(buf, int(t * SR), seg, 0.88, random.uniform(-0.6, 0.6))
        t += random.uniform(0.18, 0.42)
    return buf


def cry_chop():
    """Tiny emotional yelps for pop transitions."""
    random.seed(434)
    buf = blank(6.4)
    t = 0.18
    for k in range(14):
        d = random.uniform(0.18, 0.45)
        n = int(d * SR)
        seg = _syl(n, random.choice([76, 79, 81, 84]), "ie"[k % 2],
                   q=12.0, breath=0.12, vib=(6.8, 0.050), atk=0.012)
        place(buf, int(t * SR), seg, 0.86, random.uniform(-0.5, 0.5))
        t += d + random.uniform(0.12, 0.34)
    return buf


def crowd_response():
    """Group call-and-response, several small voices around one phrase."""
    random.seed(435)
    buf = blank(7.4)
    for t in (0.2, 1.8, 3.4, 5.0):
        for v in range(5):
            d = random.uniform(0.32, 0.58)
            seg = _word(69 + random.choice([-5, 0, 3, 7]), "aeo"[v % 3], d,
                        None if v % 2 else "t", q=8.5, breath=0.12,
                        vib=(5.3 + v * 0.2, 0.018))
            place(buf, int((t + random.uniform(-0.04, 0.04)) * SR),
                  seg, 0.34, random.uniform(-0.9, 0.9))
    return buf


def breath_riser_short():
    """Short airy vocal risers for fills."""
    random.seed(436)
    buf = blank(5.5)
    for base in (0.1, 2.7):
        steps = 24
        for k in range(steps):
            d = 0.075
            n = int(d * SR)
            m = 67 + 12 * (k / steps)
            seg = _syl(n, m, "ie"[k % 2], q=15.0, breath=0.34,
                       vib=(7.0, 0.022), atk=0.003)
            place(buf, int((base + k * d * 0.82) * SR), seg, 0.5 + 0.45 * k / steps)
    return buf


def vowel_perc_loop():
    """Percussive vowels only, designed to slice into a playable kit."""
    random.seed(437)
    sixteenth = 60.0 / 126 / 4
    buf = blank(sixteenth * 64 + 0.45)
    pattern = [1, 0, 1, 0, 2, 0, 1, 2, 0, 1, 0, 2, 1, 0, 2, 0]
    for k in range(64):
        kind = pattern[k % 16]
        if kind == 0:
            continue
        midi = 50 if kind == 1 else 74
        seg = _syl(int(sixteenth * 0.55 * SR), midi, "u" if kind == 1 else "i",
                   q=7.0 if kind == 1 else 18.0, breath=0.08,
                   atk=0.002, rel=0.055)
        place(buf, int(k * sixteenth * SR), seg, 0.9, (k % 4 - 1.5) * 0.18)
    return buf


def ooh_stack():
    """Soft ooh/ah stack for instant pop backing vocals."""
    random.seed(438)
    buf = blank(8.2)
    for t, chord in [(0.0, [60, 64, 67]), (2.5, [62, 65, 69]), (5.0, [59, 64, 67, 72])]:
        for i, m in enumerate(chord):
            n = int(2.35 * SR)
            seg = _syl(n, m, "oua"[i % 3], q=7.5, breath=0.06,
                       vib=(4.8, 0.015), atk=0.12, rel=1.2)
            place(buf, int(t * SR), seg, 0.45, (i - 1) * 0.52)
    return buf


def micro_chop():
    """Tiny one-syllable grains for fast MPC-style keyboard chopping."""
    random.seed(439)
    step = 60.0 / 150 / 8
    buf = blank(step * 96 + 0.35)
    for k in range(96):
        if k % 16 in (7, 14, 15):
            continue
        seg = _word(random.choice([69, 72, 76, 79]), "aei"[k % 3],
                    step * random.uniform(0.85, 1.25),
                    ["t", "k", None, "s"][k % 4], q=15.5, breath=0.03)
        place(buf, int(k * step * SR), seg, 0.84, (k % 5 - 2) * 0.15)
    return buf


def dance_pop_hook():
    """Chart-pop / Billboard-style vowel hook: bright, simple, singable."""
    random.seed(451)
    buf = blank(8.0)
    phrase = [(0.0, 76, .38), (.46, 79, .34), (.88, 81, .52), (1.62, 79, .42),
              (2.15, 76, .72), (3.15, 74, .34), (3.58, 76, .36), (4.05, 79, .62),
              (5.05, 81, .42), (5.58, 79, .35), (6.05, 76, 1.15)]
    for i, (t, m, d) in enumerate(phrase):
        seg = _word(m, "aei"[i % 3], d, None if i % 5 else "t",
                    q=12.0, breath=0.035, vib=(5.7, 0.018))
        place(buf, int(t * SR), seg, 0.94, (i % 3 - 1) * 0.12)
    return buf


def pop_adlibs():
    """Short modern pop ad-libs: hey/ah style one-shots without copying words."""
    random.seed(452)
    buf = blank(6.8)
    t = 0.1
    for k in range(16):
        d = random.uniform(0.16, 0.38)
        seg = _word(random.choice([74, 76, 79, 81, 84]), "aei"[k % 3], d,
                    ["t", None, "sh", None][k % 4], q=12.5, breath=0.07,
                    vib=(6.2, 0.026))
        place(buf, int(t * SR), seg, 0.88, random.uniform(-0.65, 0.65))
        t += random.uniform(0.26, 0.52)
    return buf


def festival_chant():
    """Big crowd-style chant for EDM drops and festival breaks."""
    random.seed(453)
    beat = 60.0 / 128
    buf = blank(beat * 16 + 0.8)
    hits = [0, 1, 2, 3.5, 4, 5, 6.5, 8, 9, 10, 11.5, 12, 13, 14.5]
    for j, h in enumerate(hits):
        for v in range(5):
            d = beat * random.uniform(0.28, 0.48)
            seg = _word(69 + random.choice([-12, 0, 3, 7]), "ao"[v % 2], d,
                        None if v % 2 else "t", q=8.0, breath=0.13,
                        vib=(5.0 + v * 0.2, 0.016))
            place(buf, int((h * beat + random.uniform(-0.012, 0.012)) * SR),
                  seg, 0.26, random.uniform(-0.85, 0.85))
    return _tail(buf, decay=1.1, taps=10)


def future_house_chop():
    """Bouncy future-house vocal slices on a tight offbeat grid."""
    random.seed(454)
    sixteenth = 60.0 / 124 / 4
    buf = blank(sixteenth * 64 + 0.5)
    pattern = [1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1]
    for k in range(64):
        if not pattern[k % 16]:
            continue
        seg = _word([72, 76, 79, 81][(k // 3) % 4], "iea"[k % 3],
                    sixteenth * 0.82, ["k", None, "t"][k % 3],
                    q=15.0, breath=0.03, vib=(6.2, 0.012))
        place(buf, int(k * sixteenth * SR), seg, 0.9, (k % 4 - 1.5) * 0.22)
    return buf


def slap_house_vox():
    """Dark pitched-down house vocal source."""
    random.seed(455)
    buf = blank(8.0)
    for i, (t, m, d) in enumerate([(0, 57, .55), (.75, 60, .40), (1.4, 62, .62),
                                  (2.45, 57, .80), (3.65, 55, .52), (4.35, 57, .42),
                                  (5.0, 60, .58), (6.0, 55, 1.1)]):
        seg = _word(m, "oua"[i % 3], d, None if i % 3 else "k",
                    q=7.0, breath=0.075, vib=(4.1, 0.018))
        place(buf, int(t * SR), seg, 0.94)
    return buf


def edm_drop_vox():
    """Short vocal shots designed to sit right before an EDM drop."""
    random.seed(456)
    buf = blank(6.0)
    for t, m, cons in [(0.15, 72, "t"), (1.35, 76, "ch"), (2.65, 79, "k"), (4.05, 84, "sh")]:
        seg = _word(m, "ae"[int(t) % 2], 0.42, cons, q=14.0, breath=0.05,
                    vib=(6.8, 0.030))
        place(buf, int(t * SR), seg, 0.92)
    return _tail(buf, decay=1.0, taps=12)


def trance_vocal():
    """Long euphoric vowel line for trance/progressive builds."""
    random.seed(457)
    buf = blank(10.0)
    for i, (t, m, d) in enumerate([(0, 72, 1.4), (1.7, 76, 1.2), (3.2, 79, 1.5),
                                  (5.0, 81, 1.0), (6.3, 79, 2.2)]):
        n = int(d * SR)
        seg = _syl(n, m, "aei"[i % 3], q=10.0, breath=0.06,
                   vib=(5.2, 0.028), atk=0.10)
        place(buf, int(t * SR), seg, 0.88)
    return _tail(buf, decay=2.1, taps=20)


def bigroom_call():
    """Simple stadium call, easy to chop across keys."""
    random.seed(458)
    beat = 60.0 / 128
    buf = blank(beat * 12 + 0.6)
    for i, h in enumerate([0, 1.5, 3, 4, 6, 7.5, 9, 10]):
        seg = _word([69, 72, 76, 79][i % 4], "ao"[i % 2], beat * 0.42,
                    "t" if i % 2 == 0 else None, q=9.0, breath=0.08)
        place(buf, int(h * beat * SR), seg, 0.92, (i % 2 - 0.5) * 0.35)
    return buf


def chopped_hook_grid():
    """Already-grid-ready dance-pop hook fragments."""
    random.seed(459)
    step = 60.0 / 126 / 4
    buf = blank(step * 64 + 0.5)
    notes = [76, 79, 81, 79, 76, 74, 76, 72]
    for k in range(64):
        if k % 8 in (3, 7):
            continue
        seg = _word(notes[k % 8], "aei"[k % 3], step * 0.75,
                    None if k % 4 else "ch", q=13.0, breath=0.035)
        place(buf, int(k * step * SR), seg, 0.88, (k % 3 - 1) * 0.2)
    return buf


def edm_breath_fill():
    """Breathy chopped fills for pre-drop tension."""
    random.seed(460)
    step = 60.0 / 128 / 8
    buf = blank(step * 96 + 0.4)
    for k in range(96):
        if k % 12 in (2, 6, 9):
            continue
        n = int(step * random.uniform(0.7, 1.35) * SR)
        seg = _noise(n, 900 + (k % 5) * 400, 5200 + (k % 4) * 900, 0.9)
        e = env(n, 0.001, (n / SR) * 0.65)
        seg = [seg[i] * e[i] * (0.45 + 0.45 * k / 96) for i in range(n)]
        place(buf, int(k * step * SR), seg, 0.88, (k % 5 - 2) * 0.18)
    return buf


FOURTH_WAVE = [
    ("vox_neon",       neon_chop),       ("vox_jersey",     jersey_chop),
    ("vox_amapiano",   amapiano_vox),
    ("vox_latin",      latin_chop),      ("vox_rnbrun",     rnb_run),
    ("vox_kpop",       kpop_hook),       ("vox_soul",       soul_phrase),
    ("vox_falsetto",   falsetto_hook),   ("vox_alto",       alto_harmony),
    ("vox_monk",       monk_drone),      ("vox_glasschoir", glass_choir),
    ("vox_tapewarp",   tape_warp_vox),   ("vox_telephone",  telephone_vox),
    ("vox_choirhit",   choir_hit),       ("vox_impact",     vocal_impact),
    ("vox_shortriser", breath_riser_short),
    ("vox_vowelperc",  vowel_perc_loop),
    ("vox_oohstack",   ooh_stack),
    ("vox_dancepop",   dance_pop_hook),   ("vox_popadlib",  pop_adlibs),
    ("vox_festival",   festival_chant),   ("vox_futurehouse", future_house_chop),
    ("vox_slaphouse",  slap_house_vox),   ("vox_edmdrop",   edm_drop_vox),
    ("vox_trance",     trance_vocal),     ("vox_bigroom",   bigroom_call),
    ("vox_hookgrid",   chopped_hook_grid),("vox_edmbreath", edm_breath_fill),
]


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("generating vocal chop sources…")
    for name, fn in [("vox_chant", chant), ("vox_hook", hook),
                     ("vox_choir", choir), ("vox_whisper", whisper),
                     ("vox_diva", diva), ("vox_stabs", stabs)] + SECOND_WAVE + THIRD_WAVE + FOURTH_WAVE:
        write(os.path.join(OUT, name + ".wav"), fn())
    print("done.")

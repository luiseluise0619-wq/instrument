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


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("generating vocal chop sources…")
    for name, fn in [("vox_chant", chant), ("vox_hook", hook),
                     ("vox_choir", choir), ("vox_whisper", whisper),
                     ("vox_diva", diva), ("vox_stabs", stabs)] + SECOND_WAVE:
        write(os.path.join(OUT, name + ".wav"), fn())
    print("done.")

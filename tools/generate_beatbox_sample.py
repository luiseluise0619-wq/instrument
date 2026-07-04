#!/usr/bin/env python3
"""
Generates examples/vox_beatbox.wav — a human-beatbox-style one-bar loop
(voiced "buh" kicks, "pff" snares, "ts" hats, "ka" rims) built from the same
stdlib-only synthesis approach as generate_demo_samples.py.

The point of this sample: press Demo until it loads, and the transient
slicer maps each mouth-drum hit to its own key — drums played with a voice.

Run:  python3 tools/generate_beatbox_sample.py
"""

import math
import os
import random
import struct
import wave

SR = 44100
random.seed(11)  # deterministic output


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
# Mouth-drum voices. Each one leads with a hard transient so the Transient
# slicer gives every hit its own pad.
# ---------------------------------------------------------------------------
def bb_kick(buf, start, gain=1.0):
    """'Buh' — lip pop into a pitched chest tone with a puff of breath."""
    dur = 0.26
    lp = 0.0
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        f = 150.0 * math.exp(-t * 30.0) + 55.0
        voiced = math.sin(2 * math.pi * f * t) * math.exp(-t * 11.0)
        # lip pop: a few ms of low-passed noise right at the front
        pop = 0.0
        if t < 0.012:
            lp += 0.25 * (random.uniform(-1.0, 1.0) - lp)
            pop = lp * math.exp(-t * 260.0) * 1.4
        # soft 'uh' breath under the tone
        breath = random.uniform(-1.0, 1.0) * 0.05 * math.exp(-t * 25.0)
        buf[start + i] += (voiced * 0.95 + pop + breath) * gain


def bb_snare(buf, start, gain=0.85):
    """'Pff' — an airy consonant burst with a faint voiced centre."""
    dur = 0.20
    band = 0.0
    prev = 0.0
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        n = random.uniform(-1.0, 1.0)
        hp = n - prev          # crude high-pass ('f' hiss)
        prev = n
        band += 0.30 * (hp - band)   # pull it back into the mid band ('pf')
        env = math.exp(-t * 20.0)
        tone = math.sin(2 * math.pi * 190.0 * t) * 0.18 * math.exp(-t * 45.0)
        buf[start + i] += (band * 0.9 + tone) * env * gain


def bb_hat(buf, start, gain=0.5):
    """'Ts' — a short tongue-tip hiss."""
    dur = 0.07
    prev = 0.0
    prev2 = 0.0
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        n = random.uniform(-1.0, 1.0)
        hp = n - prev
        prev = n
        hp2 = hp - prev2       # double high-pass -> bright 's'
        prev2 = hp
        buf[start + i] += hp2 * math.exp(-t * 70.0) * gain * 0.6


def bb_ka(buf, start, gain=0.5):
    """'Ka' — a clicky mid-band rim consonant."""
    dur = 0.09
    band = 0.0
    prev = 0.0
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        n = random.uniform(-1.0, 1.0)
        hp = n - prev
        prev = n
        band += 0.45 * (hp - band)   # 'k' click sits higher than 'pf'
        buf[start + i] += band * math.exp(-t * 55.0) * gain


def make_beatbox():
    """One bar at 100 BPM: boots - ts - pff - ts ... with human jitter."""
    bpm = 100.0
    sixteenth = (60.0 / bpm) / 4.0
    total = int(sixteenth * 16 * SR) + int(0.25 * SR)  # room for the last tail
    buf = [0.0] * total

    def pos(step):
        # a few ms of human timing slop, but never before the grid slot
        return int(step * sixteenth * SR) + int(random.uniform(0.0, 0.006) * SR)

    hits = [
        (0,  bb_kick,  1.00), (2,  bb_hat, 0.50), (4,  bb_snare, 0.85),
        (6,  bb_hat,   0.45), (7,  bb_ka,  0.35), (8,  bb_kick,  0.95),
        (10, bb_kick,  0.60), (11, bb_hat, 0.45), (12, bb_snare, 0.90),
        (14, bb_hat,   0.50), (15, bb_ka,  0.40),
    ]
    for step, voice, gain in hits:
        voice(buf, pos(step), gain=gain)

    buf = normalize(buf, 0.9)
    # near-dry (chops want tight transients); tiny haas widen only
    delay = int(0.006 * SR)
    left = buf
    right = [0.0] * delay + buf[: len(buf) - delay]
    return left, right


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(here, "examples")
    os.makedirs(out_dir, exist_ok=True)

    l, r = make_beatbox()
    write_wav(os.path.join(out_dir, "vox_beatbox.wav"), l, r)
    print("wrote examples/vox_beatbox.wav (%.1f s)" % (len(l) / SR))


if __name__ == "__main__":
    main()

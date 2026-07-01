#!/usr/bin/env python3
"""
Generates royalty-free demo samples for VocalChop Studio using only the Python
standard library (no numpy). Produces two 44.1 kHz / 16-bit stereo WAVs:

  examples/vocal_chop_demo.wav  - a vowel-formant "synth vocal" melody
                                  (great for the pitch / formant knobs and
                                   grid slicing)
  examples/drum_loop_120bpm.wav - a 120 BPM drum loop with sharp transients
                                  (great for transient slicing)

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
# 1) Vowel-formant "synth vocal" melody
# ---------------------------------------------------------------------------
def vowel_note(freq, dur):
    # "ah"-ish formants: (centre Hz, bandwidth, peak gain)
    formants = [(800.0, 90.0, 1.0), (1150.0, 110.0, 0.7), (2900.0, 150.0, 0.35)]
    n = int(dur * SR)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
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
    return out


def make_vocal():
    # A minor pentatonic phrase
    melody = [220.00, 261.63, 293.66, 329.63, 392.00, 329.63, 293.66, 261.63]
    dur = 0.34
    mono = []
    for f in melody:
        mono.extend(vowel_note(f, dur))
    mono = normalize(mono, 0.9)
    # Tiny haas-style widen for a stereo feel.
    delay = int(0.008 * SR)
    left = mono
    right = [0.0] * delay + mono[: len(mono) - delay]
    return left, right


# ---------------------------------------------------------------------------
# 2) 120 BPM drum loop with sharp transients
# ---------------------------------------------------------------------------
def add_kick(buf, start):
    dur = 0.30
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        f = 110.0 * math.exp(-t * 18.0) + 45.0
        env = math.exp(-t * 8.0)
        buf[start + i] += math.sin(2 * math.pi * f * t) * env * 0.95


def add_snare(buf, start):
    dur = 0.20
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        env = math.exp(-t * 22.0)
        tone = math.sin(2 * math.pi * 185.0 * t) * 0.4
        noise = random.uniform(-1.0, 1.0) * 0.7
        buf[start + i] += (tone + noise) * env * 0.75


def add_hat(buf, start, dur=0.05):
    prev = 0.0
    for i in range(int(dur * SR)):
        if start + i >= len(buf):
            break
        t = i / SR
        env = math.exp(-t * 60.0)
        n = random.uniform(-1.0, 1.0)
        hp = n - prev  # crude high-pass
        prev = n
        buf[start + i] += hp * env * 0.4


def make_drums():
    bpm = 120.0
    step = (60.0 / bpm) / 2.0  # 8th note
    total = int(step * 8 * SR)  # one bar of 8 eighth-notes
    buf = [0.0] * total
    for s in range(8):
        pos = int(s * step * SR)
        add_hat(buf, pos)                 # hats on every 8th
        if s in (0, 4):
            add_kick(buf, pos)            # kick on beats 1 and 3
        if s in (2, 6):
            add_snare(buf, pos)           # snare on beats 2 and 4
        if s == 7:
            add_kick(buf, pos)            # pickup kick before the loop
    buf = normalize(buf, 0.9)
    return buf, list(buf)


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(here, "examples")
    os.makedirs(out_dir, exist_ok=True)

    l, r = make_vocal()
    write_wav(os.path.join(out_dir, "vocal_chop_demo.wav"), l, r)
    print("wrote examples/vocal_chop_demo.wav  (%.1f s)" % (len(l) / SR))

    l, r = make_drums()
    write_wav(os.path.join(out_dir, "drum_loop_120bpm.wav"), l, r)
    print("wrote examples/drum_loop_120bpm.wav (%.1f s)" % (len(l) / SR))


if __name__ == "__main__":
    main()

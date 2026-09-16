#!/usr/bin/env python3
"""
Polish Slyce's built-in vocal sources in-place.

This does not add third-party audio. It only improves the generated,
royalty-free WAVs already in examples/ with a restrained sample-library style
mastering pass:

  - remove DC offset
  - high-pass sub rumble
  - smooth harsh peaks with fast compression
  - add light vocal-friendly saturation
  - tiny edge fades to prevent clicks
  - consistent peak headroom

Run from the repo root:
    python tools/polish_vocal_samples.py
"""

from __future__ import annotations

import math
import os
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"


def _read_wav(path: Path):
    with wave.open(str(path), "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        sr = w.getframerate()
        frames = w.readframes(w.getnframes())

    if width != 2:
        raise RuntimeError(f"{path.name}: only 16-bit PCM is supported")

    count = len(frames) // 2
    ints = struct.unpack("<" + "h" * count, frames)
    data = [[0.0] * (count // channels) for _ in range(channels)]
    for i in range(count // channels):
        for ch in range(channels):
            data[ch][i] = ints[i * channels + ch] / 32768.0
    return sr, data


def _write_wav(path: Path, sr: int, data):
    channels = len(data)
    n = len(data[0])
    frames = bytearray()
    for i in range(n):
        for ch in range(channels):
            v = max(-1.0, min(1.0, data[ch][i]))
            frames += struct.pack("<h", int(round(v * 32767.0)))

    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(bytes(frames))


def _highpass(x, sr: int, hz: float = 32.0):
    # Simple one-pole high-pass. Good enough for removing generated rumble/DC
    # without changing the vocal character.
    rc = 1.0 / (2.0 * math.pi * hz)
    dt = 1.0 / sr
    a = rc / (rc + dt)
    y = [0.0] * len(x)
    prev_y = 0.0
    prev_x = x[0] if x else 0.0
    for i, s in enumerate(x):
        o = a * (prev_y + s - prev_x)
        y[i] = o
        prev_y = o
        prev_x = s
    return y


def _compress(x, sr: int, threshold_db: float = -17.0, ratio: float = 2.4):
    thr = 10.0 ** (threshold_db / 20.0)
    attack = math.exp(-1.0 / (0.004 * sr))
    release = math.exp(-1.0 / (0.085 * sr))
    env = 0.0
    out = [0.0] * len(x)
    makeup = 1.12
    for i, s in enumerate(x):
        a = abs(s)
        coef = attack if a > env else release
        env = coef * env + (1.0 - coef) * a
        if env > thr:
            over = env / thr
            gain = over ** ((1.0 / ratio) - 1.0)
        else:
            gain = 1.0
        out[i] = s * gain * makeup
    return out


def _low_shelf_warmth(x, sr: int):
    # Parallel low-mid body, kept subtle so chops still cut through a mix.
    cutoff = 260.0
    a = math.exp(-2.0 * math.pi * cutoff / sr)
    lp = 0.0
    out = [0.0] * len(x)
    for i, s in enumerate(x):
        lp = (1.0 - a) * s + a * lp
        out[i] = s + 0.08 * lp
    return out


def _soft_saturate(x, drive: float = 1.08):
    norm = math.tanh(drive)
    return [math.tanh(s * drive) / norm for s in x]


def _fade_edges(x, sr: int):
    n = len(x)
    fade = min(n // 8, max(16, int(0.006 * sr)))
    if fade <= 1:
        return x
    y = x[:]
    for i in range(fade):
        g = 0.5 - 0.5 * math.cos(math.pi * i / fade)
        y[i] *= g
        y[n - 1 - i] *= g
    return y


def _normalise(data, peak: float = 0.91):
    m = max(max(abs(v) for v in ch) for ch in data) or 1.0
    g = peak / m
    return [[v * g for v in ch] for ch in data], m, peak


def polish(path: Path):
    sr, data = _read_wav(path)
    polished = []
    for ch in data:
        mean = sum(ch) / max(1, len(ch))
        y = [s - mean for s in ch]
        y = _highpass(y, sr)
        y = _low_shelf_warmth(y, sr)
        y = _compress(y, sr)
        y = _soft_saturate(y)
        y = _fade_edges(y, sr)
        polished.append(y)

    polished, before_peak, _ = _normalise(polished)
    _write_wav(path, sr, polished)
    return len(polished[0]) / sr, before_peak


def main():
    names = sorted(
        p for p in EXAMPLES.glob("*.wav")
        if p.name.startswith("vox_") or p.name.startswith("vocal_chop")
    )
    print(f"polishing {len(names)} vocal source wavs...")
    for p in names:
        sec, before = polish(p)
        print(f"  {p.name:<26} {sec:5.1f}s  pre-peak {before:0.3f}")
    print("done.")


if __name__ == "__main__":
    main()

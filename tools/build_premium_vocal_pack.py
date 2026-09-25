#!/usr/bin/env python3
"""Turn separated vocal stems into compact, release-ready SLYCE sources.

The script deliberately never reads accompaniment stems.  It finds sustained,
pitched activity in ``vocals.wav``, rejects short/high-frequency transient
regions (the usual residual cymbal/hat failure mode), trims silence, applies
edge fades and writes two eight-phrase PCM-24 WAV banks per song.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf


@dataclass
class Region:
    start: int
    end: int
    score: float
    hf_ratio: float
    transient_density: float
    accompaniment_overlap: float


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def frame_features(mono: np.ndarray, sr: int):
    size = 2048
    hop = 512
    if len(mono) < size:
        mono = np.pad(mono, (0, size - len(mono)))
    count = 1 + (len(mono) - size) // hop
    shape = (count, size)
    strides = (mono.strides[0] * hop, mono.strides[0])
    frames = np.lib.stride_tricks.as_strided(mono, shape=shape, strides=strides)
    windowed = frames * np.hanning(size)[None, :]
    rms = np.sqrt(np.mean(windowed * windowed, axis=1) + 1.0e-12)
    db = 20.0 * np.log10(rms + 1.0e-12)
    mag = np.abs(np.fft.rfft(windowed, axis=1)) + 1.0e-10
    freqs = np.fft.rfftfreq(size, 1.0 / sr)
    total = np.sum(mag, axis=1) + 1.0e-10
    hf = np.sum(mag[:, freqs >= 6000.0], axis=1) / total
    flatness = np.exp(np.mean(np.log(mag), axis=1)) / np.mean(mag, axis=1)
    norm = mag / total[:, None]
    flux = np.zeros(count, dtype=np.float64)
    if count > 1:
        flux[1:] = np.sum(np.maximum(0.0, norm[1:] - norm[:-1]), axis=1)
    return db, hf, flatness, flux, hop


def moving_average(values: np.ndarray, width: int) -> np.ndarray:
    width = max(1, width)
    return np.convolve(values, np.ones(width) / width, mode="same")


def find_regions(audio: np.ndarray, sr: int, accompaniment: np.ndarray | None = None, max_regions: int = 32):
    mono = np.mean(audio, axis=1)
    db, hf, flatness, flux, hop = frame_features(mono, sr)
    accompaniment_flux = None
    if accompaniment is not None:
        _, _, _, accompaniment_flux, _ = frame_features(np.mean(accompaniment, axis=1), sr)
    smooth_db = moving_average(db, max(3, int(0.12 * sr / hop)))
    peak_db = float(np.percentile(smooth_db, 98))
    threshold = max(-46.0, peak_db - 31.0)
    # Hat/cymbal residuals are energetic but short, noisy and top-heavy.
    voiced = (smooth_db >= threshold) & (hf < 0.38) & (flatness < 0.52)
    bridge = max(1, int(0.28 * sr / hop))
    voiced = moving_average(voiced.astype(np.float64), bridge) > 0.10

    raw = []
    start = None
    for i, active in enumerate(np.r_[voiced, False]):
        if active and start is None:
            start = i
        elif not active and start is not None:
            if i - start >= max(2, int(0.34 * sr / hop)):
                raw.append((start, i))
            start = None

    # Split long continuous verses at quiet local minima, keeping playable
    # phrases instead of embedding multi-minute stems.
    split = []
    max_frames = int(3.8 * sr / hop)
    min_frames = int(0.30 * sr / hop)
    for a, b in raw:
        while b - a > max_frames:
            lo = a + int(1.25 * sr / hop)
            hi = min(b - min_frames, a + max_frames)
            cut = lo + int(np.argmin(smooth_db[lo:hi])) if hi > lo else a + max_frames
            split.append((a, cut))
            a = cut
        if b - a >= min_frames:
            split.append((a, b))

    flux_gate = float(np.percentile(flux, 88))
    accompaniment_gate = (float(np.percentile(accompaniment_flux, 82))
                            if accompaniment_flux is not None else 1.0)
    regions = []
    for a, b in split:
        seg_hf = float(np.median(hf[a:b]))
        transient_density = float(np.mean((flux[a:b] > flux_gate) & (hf[a:b] > 0.16)))
        accompaniment_overlap = 0.0
        if accompaniment_flux is not None:
            stop = min(b, len(accompaniment_flux))
            if stop > a:
                accompaniment_overlap = float(np.mean(
                    (flux[a:stop] > flux_gate) &
                    (accompaniment_flux[a:stop] > accompaniment_gate)))
        duration = (b - a) * hop / sr
        # Reject the characteristic isolated or repeated cymbal pattern.
        if ((duration < 0.65 and seg_hf > 0.20) or transient_density > 0.30 or
                (seg_hf > 0.22 and accompaniment_overlap > 0.20)):
            continue
        tonal = 1.0 - float(np.median(np.clip(flatness[a:b], 0.0, 1.0)))
        level = float(np.mean(np.clip((smooth_db[a:b] - threshold) / 24.0, 0.0, 1.0)))
        duration_score = min(1.0, duration / 2.0)
        score = (0.48 * tonal + 0.34 * level + 0.18 * duration_score -
                 0.45 * seg_hf - 0.35 * transient_density - 0.30 * accompaniment_overlap)
        pad = int(0.055 * sr)
        regions.append(Region(max(0, a * hop - pad), min(len(audio), b * hop + 2048 + pad),
                              score, seg_hf, transient_density, accompaniment_overlap))

    # Prefer strong phrases spread through the whole song rather than sixteen
    # nearly identical adjacent cuts.
    chosen = []
    for region in sorted(regions, key=lambda r: r.score, reverse=True):
        centre = (region.start + region.end) // 2
        if any(abs(centre - (x.start + x.end) // 2) < int(0.55 * sr) for x in chosen):
            continue
        chosen.append(region)
        if len(chosen) >= max_regions:
            break
    return sorted(chosen, key=lambda r: r.start)


def clean_clip(clip: np.ndarray, sr: int) -> np.ndarray:
    clip = np.asarray(clip, dtype=np.float64)
    # Remove DC and sub/kick leakage with a one-pole 82 Hz high-pass.
    clip -= np.mean(clip, axis=0, keepdims=True)
    rc = 1.0 / (2.0 * math.pi * 82.0)
    alpha = rc / (rc + 1.0 / sr)
    out = np.empty_like(clip)
    prev_x = np.zeros(clip.shape[1])
    prev_y = np.zeros(clip.shape[1])
    for i, x in enumerate(clip):
        y = alpha * (prev_y + x - prev_x)
        out[i] = y
        prev_x, prev_y = x, y

    # Gentle saturation controls separator spikes without flattening phrasing.
    out = np.tanh(out * 1.06) / np.tanh(1.06)
    peak = float(np.max(np.abs(out))) + 1.0e-12
    active = out[np.abs(out) > 10.0 ** (-48.0 / 20.0)]
    rms = float(np.sqrt(np.mean(active * active))) if active.size else peak
    gain = min(10.0 ** (-1.5 / 20.0) / peak, 10.0 ** (-16.0 / 20.0) / max(rms, 1.0e-9))
    out *= gain
    fade = min(len(out) // 4, int(0.012 * sr))
    if fade:
        ramp = 0.5 - 0.5 * np.cos(np.linspace(0.0, math.pi, fade))
        out[:fade] *= ramp[:, None]
        out[-fade:] *= ramp[::-1, None]
    if len(out):
        out[0] = out[-1] = 0.0
    return out.astype(np.float32)


def build_bank(clips: list[np.ndarray], sr: int) -> np.ndarray:
    gap = np.zeros((int(0.19 * sr), 2), dtype=np.float32)
    parts = []
    for clip in clips:
        if clip.shape[1] == 1:
            clip = np.repeat(clip, 2, axis=1)
        parts.extend((clip[:, :2], gap))
    return np.concatenate(parts[:-1], axis=0) if parts else np.zeros((1, 2), dtype=np.float32)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("stems", type=Path, help="Demucs model output directory")
    parser.add_argument("--output", type=Path, default=Path("examples"))
    parser.add_argument("--manifest", type=Path, default=Path("assets/premium_vocal_manifest.csv"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    rows = []
    for stem in sorted(args.stems.glob("*/vocals.wav")):
        audio, sr = sf.read(stem, always_2d=True, dtype="float32")
        accompaniment_path = stem.with_name("no_vocals.wav")
        accompaniment = None
        if accompaniment_path.exists():
            accompaniment, accompaniment_sr = sf.read(accompaniment_path, always_2d=True, dtype="float32")
            if accompaniment_sr != sr:
                raise ValueError(f"Sample-rate mismatch: {stem} / {accompaniment_path}")
        regions = find_regions(audio, sr, accompaniment)
        clips = [clean_clip(audio[r.start:r.end], sr) for r in regions]
        source = slug(stem.parent.name)
        for bank_index in range((len(clips) + 7) // 8):
            group = clips[bank_index * 8:(bank_index + 1) * 8]
            if not group:
                continue
            filename = f"premium_{source}_{bank_index + 1}.wav"
            destination = args.output / filename
            bank = build_bank(group, sr)
            sf.write(destination, bank, sr, subtype="PCM_24")
            selected = regions[bank_index * 8:(bank_index + 1) * 8]
            rows.append({
                "file": filename,
                "source_file": stem.parent.name,
                "source": "user_supplied_vocal_separation",
                "phrases": len(group),
                "sample_rate": sr,
                "seconds": round(len(bank) / sr, 3),
                "median_hf_ratio": round(float(np.median([r.hf_ratio for r in selected])), 5),
                "median_transient_density": round(float(np.median([r.transient_density for r in selected])), 5),
                "median_accompaniment_overlap": round(float(np.median([r.accompaniment_overlap for r in selected])), 5),
                "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            })
            print(f"{filename}: {len(group)} phrases, {len(bank) / sr:.1f}s")

    if not rows:
        raise SystemExit("No vocal banks were produced")
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    with args.manifest.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {args.manifest} ({len(rows)} banks)")


if __name__ == "__main__":
    main()

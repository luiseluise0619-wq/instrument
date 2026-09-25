#!/usr/bin/env python3
"""Non-destructive WAV preparation for SLYCE. Dry-run unless --apply is given.

Dependencies: numpy, scipy, soundfile. Input files are NEVER overwritten.
Instrument mode preserves per-file gain and attack/tail shape; do not normalise
velocity layers as if they were independent vocal one-shots. No pitch correction,
noise removal, time stretch, trimming or replacement voices are performed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
from typing import Any

import numpy as np
import soundfile as sf
from scipy import signal

SUPPORTED_SUBTYPES = {"PCM_16", "PCM_24", "PCM_32", "FLOAT", "DOUBLE"}


def db(value: float) -> float:
    return 20.0 * math.log10(max(float(value), 1e-12))


def measurements(audio: np.ndarray) -> dict[str, float]:
    peak = float(np.max(np.abs(audio), initial=0.0))
    energy = np.mean(audio * audio, axis=1)
    active = energy > max(peak * 0.04, 1e-6) ** 2
    active_rms = math.sqrt(float(np.mean(energy[active]))) if np.any(active) else 0.0
    return {"peak_dbfs": round(db(peak), 4), "active_rms_dbfs": round(db(active_rms), 4),
            "mean_dc": float(np.max(np.abs(np.mean(audio, axis=0)), initial=0.0))}


def riff_metadata(path: Path) -> list[tuple[bytes, bytes]]:
    """Retain cue/smpl/bext/LIST/etc without moving sample positions.

    RIFF/WAVE only. RF64 is explicitly rejected instead of silently discarding
    its extended sizes. Large data chunks are skipped, never copied to memory.
    """
    chunks: list[tuple[bytes, bytes]] = []
    with path.open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
            raise ValueError("Only RIFF/WAVE is supported; RF64 and non-WAV are not rewritten")
        size = path.stat().st_size
        while stream.tell() + 8 <= size:
            tag, length = struct.unpack("<4sI", stream.read(8))
            end = stream.tell() + length
            if end > size:
                raise ValueError("Invalid WAV chunk size")
            if tag not in {b"fmt ", b"data", b"fact", b"JUNK", b"PAD "}:
                if length > 8 * 1024 * 1024:
                    raise ValueError("An ancillary metadata chunk exceeds the safety limit")
                chunks.append((tag, stream.read(length)))
            stream.seek(end + (length & 1))
    return chunks


def append_metadata(path: Path, metadata: list[tuple[bytes, bytes]]) -> None:
    if not metadata:
        return
    with path.open("r+b") as stream:
        stream.seek(0, os.SEEK_END)
        for tag, content in metadata:
            stream.write(struct.pack("<4sI", tag, len(content)))
            stream.write(content)
            if len(content) & 1:
                stream.write(b"\0")
        total = stream.tell()
        if total - 8 > 0xFFFFFFFF:
            raise ValueError("Result is too large for RIFF/WAVE")
        stream.seek(4)
        stream.write(struct.pack("<I", total - 8))


def process_audio(audio: np.ndarray, sample_rate: int, profile: str,
                  match_levels: bool = False, de_ess: bool = False,
                  has_loop_metadata: bool = False) -> tuple[np.ndarray, dict[str, Any]]:
    if audio.ndim != 2 or audio.shape[0] == 0 or audio.shape[1] not in (1, 2):
        raise ValueError("Expected a nonempty mono or stereo sample")
    if not np.all(np.isfinite(audio)):
        raise ValueError("Sample contains NaN or infinite values")
    if profile not in {"vocal", "instrument"}:
        raise ValueError("Unknown profile")
    if sample_rate < 8000 or sample_rate > 384000:
        raise ValueError("Unsupported sample rate")
    if profile == "instrument" and (match_levels or de_ess):
        raise ValueError("Level matching/de-essing is vocal-only; preserve instrument velocity layers")
    result = audio.astype(np.float64, copy=True)
    info: dict[str, Any] = {"before": measurements(result), "operations": []}
    if len(result) < 32 or np.max(np.abs(result)) < 1e-8:
        info["operations"].append("unchanged: very short or silent sample")
        info["after"] = measurements(result)
        return result, info

    # Static DC correction is intentionally not an automatic noise gate.
    offsets = np.mean(result, axis=0)
    result -= offsets
    info["operations"].append("per-channel DC removal")
    if profile == "vocal":
        # Keep the original duration; bass fundamentals are not pitch-shifted.
        sos = signal.butter(2, 40.0, btype="highpass", fs=sample_rate, output="sos")
        result = signal.sosfilt(sos, result, axis=0)
        info["operations"].append("40 Hz high-pass")
        if de_ess:
            cutoff = min(6500.0, sample_rate * 0.35)
            low = signal.sosfilt(signal.butter(2, cutoff, fs=sample_rate, output="sos"), result, axis=0)
            high = result - low
            a = math.exp(-1.0 / (0.008 * sample_rate))
            high_energy = signal.lfilter([1.0 - a], [1.0, -a], np.mean(high * high, axis=1))
            full_energy = signal.lfilter([1.0 - a], [1.0, -a], np.mean(result * result, axis=1))
            ratio = np.sqrt(np.maximum(high_energy, 0.0) / np.maximum(full_energy, 1e-12))
            strength = np.clip((ratio - 0.65) / 0.45, 0.0, 1.0)
            strength *= (full_energy > 1e-7)
            # Stereo-linked, maximum 3 dB attenuation of the high component.
            gain = 10.0 ** ((-3.0 * strength) / 20.0)
            result = low + high * gain[:, None]
            info["operations"].append("optional stereo-linked high-band attenuation, at most 3 dB")
        if not has_loop_metadata:
            for count, beginning in ((round(sample_rate * 0.0008), True),
                                     (round(sample_rate * 0.002), False)):
                count = max(2, min(count, len(result) // 8))
                t = np.linspace(0.0, 1.0, count)
                fade = t * t * (3.0 - 2.0 * t)
                if beginning:
                    result[:count] *= fade[:, None]
                else:
                    result[-count:] *= fade[::-1, None]
            info["operations"].append("0.8 ms start / 2 ms end fades (no trim)")
        else:
            info["operations"].append("edge fades skipped: loop metadata present")
        if match_levels:
            before_gain = measurements(result)["active_rms_dbfs"]
            gain_db = float(np.clip(-20.0 - before_gain, -6.0, 6.0))
            result *= 10.0 ** (gain_db / 20.0)
            info["operations"].append(f"optional active-level matching: {gain_db:+.2f} dB before peak ceiling")
        ceiling = 10.0 ** (-1.0 / 20.0)
        peak = float(np.max(np.abs(result), initial=0.0))
        if peak > ceiling:
            result *= ceiling / peak
            info["operations"].append("static attenuation to -1 dBFS sample-peak ceiling (not true-peak limiting)")
    else:
        # Do not alter key/velocity-layer balance or re-envelope an instrument
        # sample. Refuse unsafe PCM writes rather than silently clipping it.
        info["operations"].append("instrument gain, timing and envelopes preserved")
    info["after"] = measurements(result)
    return result, info


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Source WAV directory")
    parser.add_argument("--output", type=Path, required=True, help="Separate destination directory")
    parser.add_argument("--profile", choices=("vocal", "instrument"), default="vocal")
    parser.add_argument("--match-levels", action="store_true", help="Vocal-only, bounded +/-6 dB level matching")
    parser.add_argument("--de-ess", action="store_true", help="Vocal-only, optional gentle high-band processing")
    parser.add_argument("--apply", action="store_true", help="Write processed copies; default is report-only")
    args = parser.parse_args(argv)
    source = args.input.expanduser().resolve()
    destination = args.output.expanduser().resolve()
    if not source.is_dir():
        parser.error("Input directory does not exist")
    if source == destination or source in destination.parents or destination in source.parents:
        parser.error("Input and output must be separate, non-nested directories")
    if args.profile == "instrument" and (args.match_levels or args.de_ess):
        parser.error("Instrument mode does not allow vocal level matching or de-essing")
    paths = sorted(p for p in source.rglob("*") if p.suffix.lower() == ".wav" and p.is_file())
    if not paths:
        parser.error("No WAV files found")
    destination.mkdir(parents=True, exist_ok=True)
    report: dict[str, Any] = {"mode": "apply" if args.apply else "dry-run", "profile": args.profile,
                              "input": str(source), "output": str(destination), "files": []}
    failures = 0
    for path in paths:
        entry: dict[str, Any] = {"file": str(path.relative_to(source))}
        try:
            if path.is_symlink() or source not in path.resolve().parents:
                raise ValueError("Symlinked inputs are not processed")
            properties = sf.info(path)
            if properties.subtype not in SUPPORTED_SUBTYPES:
                raise ValueError(f"Unsupported WAV subtype: {properties.subtype}")
            if properties.frames > properties.samplerate * 600 or properties.channels not in (1, 2):
                raise ValueError("Maximum is 10 minutes and 2 channels per sample")
            metadata = riff_metadata(path)
            audio, rate = sf.read(path, dtype="float64", always_2d=True)
            result, details = process_audio(audio, rate, args.profile, args.match_levels,
                                            args.de_ess, any(tag == b"smpl" for tag, _ in metadata))
            entry.update(details)
            entry.update({"frames": len(audio), "sample_rate": rate, "channels": audio.shape[1],
                          "subtype": properties.subtype, "metadata_chunks": [t.decode("ascii", "replace") for t, _ in metadata],
                          "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
            if args.apply:
                output = destination / path.relative_to(source)
                if output.exists():
                    raise FileExistsError(f"Refusing to overwrite an existing output: {output}")
                if properties.subtype.startswith("PCM") and np.max(np.abs(result)) >= 1.0:
                    raise ValueError("DC-corrected instrument would clip PCM; use manual gain review")
                output.parent.mkdir(parents=True, exist_ok=True)
                temp: Path | None = None
                try:
                    with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".wav", delete=False) as f:
                        temp = Path(f.name)
                    sf.write(temp, result, rate, format="WAV", subtype=properties.subtype)
                    append_metadata(temp, metadata)
                    # Exclusive creation protects another process's output too.
                    with output.open("xb") as f:
                        with temp.open("rb") as src:
                            import shutil
                            shutil.copyfileobj(src, f)
                    entry["output"] = str(output)
                    entry["status"] = "written"
                finally:
                    if temp is not None:
                        temp.unlink(missing_ok=True)
            else:
                entry["status"] = "analysed; no WAV written"
        except Exception as exc:
            failures += 1
            entry["status"] = "error"
            entry["error"] = str(exc)
        report["files"].append(entry)
    report["failures"] = failures
    report_path = destination / "polish_report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"{len(paths)} WAV(s), {failures} error(s). {report_path}")
    print("Source WAV files were not modified.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

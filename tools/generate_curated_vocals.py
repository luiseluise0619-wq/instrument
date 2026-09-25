#!/usr/bin/env python3
"""Generate the small, release-facing Slyce vocal collection.

These are deterministic, original formant-synthesis performances.  No model,
downloaded recording, commercial sample pack, or Output/Arcade content is
used.  The larger legacy generator stays in the repository so retired sounds
remain reproducible, while this script owns the hand-curated premium additions.
"""

from __future__ import annotations

import csv
import hashlib
import math
import random
import wave
from pathlib import Path

import generate_vocal_chops as voice

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "examples"
SR = voice.SR


def _finish(buf, target_active_db=-16.0, peak_db=-1.5):
    """DC/sub cleanup, gentle tape curve, edge fades and active-level match."""
    mono = [(buf[0][i] + buf[1][i]) * 0.5 for i in range(len(buf[0]))]
    mean = sum(mono) / max(1, len(mono))
    mono = [x - mean for x in mono]

    # One-pole high-pass at 48 Hz: no rumble, no removal of vocal body.
    rc = 1.0 / (2.0 * math.pi * 48.0)
    dt = 1.0 / SR
    a = rc / (rc + dt)
    hp, last_x, last_y = [], mono[0] if mono else 0.0, 0.0
    for x in mono:
        y = a * (last_y + x - last_x)
        hp.append(math.tanh(y * 1.08) / math.tanh(1.08))
        last_x, last_y = x, y

    # Active RMS ignores the designed gaps between syllables.
    active = [x for x in hp if abs(x) > 10 ** (-48.0 / 20.0)]
    rms = math.sqrt(sum(x * x for x in active) / max(1, len(active)))
    target = 10 ** (target_active_db / 20.0)
    peak_limit = 10 ** (peak_db / 20.0)
    gain = min(target / max(rms, 1.0e-9), peak_limit / max(max(map(abs, hp)), 1.0e-9))
    hp = [x * gain for x in hp]

    # Six milliseconds is inaudible as an envelope but removes file-edge pops.
    fade = min(len(hp) // 4, int(0.006 * SR))
    for i in range(fade):
        g = 0.5 - 0.5 * math.cos(math.pi * i / max(1, fade - 1))
        hp[i] *= g
        hp[-1 - i] *= g
    if hp:
        hp[0] = hp[-1] = 0.0
    return hp


def _write(name, buf, group, character, bpm):
    data = _finish(buf)
    path = OUT / f"{name}.wav"
    pcm = bytearray()
    random.seed(0x51CE + len(name))
    for x in data:
        # Deterministic TPDF dither prevents low-level tails quantising to a buzz.
        dither = (random.random() - random.random()) * 0.5
        q = max(-32768, min(32767, round(x * 32767.0 + dither)))
        pcm.extend(int(q).to_bytes(2, "little", signed=True))
    pcm[0:2] = b"\0\0"
    pcm[-2:] = b"\0\0"
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SR)
        wav.writeframes(pcm)
    peak = max(abs(x) for x in data)
    active = [x for x in data if abs(x) > 10 ** (-48.0 / 20.0)]
    rms = math.sqrt(sum(x*x for x in active) / max(1, len(active)))
    return {
        "file": path.name, "source": "original_procedural_formant_synthesis",
        "group": group, "character": character, "bpm": bpm,
        "sample_rate": SR, "seconds": round(len(data) / SR, 3),
        "peak_dbfs": round(20 * math.log10(max(peak, 1e-9)), 3),
        "active_rms_dbfs": round(20 * math.log10(max(rms, 1e-9)), 3),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def silk_breath():
    random.seed(5101); buf = voice.blank(6.2)
    for i, (at, note, dur) in enumerate([(0.08,76,.30),(.72,79,.42),(1.55,81,.32),(2.25,84,.58),(3.35,81,.40),(4.2,79,.72)]):
        seg = voice._syl(int(dur*SR), note, "iea"[i%3], q=13.5, breath=.14,
                         vib=(5.7,.018), atk=.028, rel=dur*.48)
        voice.place(buf, int(at*SR), seg, .92, (i%3-1)*.18)
    return buf


def velvet_bloom():
    random.seed(5102); buf = voice.blank(7.5)
    phrase=[(0,69,.70),(0.88,72,.52),(1.55,76,.86),(2.75,74,.55),(3.48,72,.74),(4.52,76,.48),(5.2,79,1.42)]
    for i,(at,note,dur) in enumerate(phrase):
        seg=voice._word(note,"aeo"[i%3],dur,None if i%3 else "sh",q=9.5,breath=.055,vib=(5.0,.021))
        voice.place(buf,int(at*SR),seg,.94,(i%3-1)*.12)
    return buf


def prism_vowel():
    random.seed(5103); buf=voice.blank(6.8)
    for i,(at,note,dur) in enumerate([(0,72,.38),(.5,76,.34),(.94,79,.52),(1.72,81,.40),(2.32,79,.72),(3.4,76,.34),(3.86,79,.46),(4.55,84,1.30)]):
        seg=voice._syl(int(dur*SR),note,"eia"[i%3],q=14.0,breath=.025,vib=(6.0,.015),atk=.008,rel=dur*.52)
        voice.place(buf,int(at*SR),seg,.93,(i%4-1.5)*.13)
    return buf


def cloud_phrase():
    random.seed(5104); buf=voice.blank(9.4)
    for i,(at,note,dur) in enumerate([(0.1,67,1.05),(1.45,72,1.0),(2.8,76,1.3),(4.45,72,.85),(5.65,79,1.75),(7.55,76,1.2)]):
        seg=voice._syl(int(dur*SR),note,"oua"[i%3],q=8.7,breath=.10,vib=(4.3,.024),atk=.09,rel=dur*.62)
        voice.place(buf,int(at*SR),seg,.86,(i%2-.5)*.28)
    return voice._tail(buf,decay=1.7,taps=14)


def noir_formant():
    random.seed(5105); buf=voice.blank(7.8)
    for i,(at,note,dur) in enumerate([(0.08,48,.72),(1.02,51,.52),(1.76,55,.88),(2.92,53,.65),(3.82,48,1.02),(5.15,46,.58),(5.95,51,1.25)]):
        seg=voice._syl(int(dur*SR),note,"uoa"[i%3],q=6.2,breath=.045,vib=(3.8,.016),atk=.026,rel=dur*.58)
        voice.place(buf,int(at*SR),seg,.95)
    return buf


def ember_call():
    random.seed(5106); beat=60/126; buf=voice.blank(beat*16+.8)
    for i,pos in enumerate([0,.75,2,3.5,5,6,8,9.5,11,12,14]):
        dur=beat*(.42 if i%3 else .68)
        seg=voice._word([60,63,67,70][i%4],"ao"[i%2],dur,None if i%4 else "t",q=7.2,breath=.075,vib=(4.5,.017))
        voice.place(buf,int(pos*beat*SR),seg,.93,(i%3-1)*.2)
    return buf


def aurora_tail():
    random.seed(5107); buf=voice.blank(10.5)
    for i,note in enumerate([60,67,72,76]):
        seg=voice._syl(int(9.7*SR),note,"aoei"[i],q=9.0,breath=.07,vib=(4.1+i*.2,.014),atk=1.0,rel=2.4)
        voice.place(buf,int((.18+i*.018)*SR),seg,.38,(i-1.5)*.5)
    return voice._tail(buf,decay=2.5,taps=24)


def lunar_tail():
    random.seed(5108); buf=voice.blank(10.2)
    for i,(at,note,dur) in enumerate([(0,69,2.0),(2.25,72,1.8),(4.35,76,2.2),(6.9,72,2.55)]):
        seg=voice._syl(int(dur*SR),note,"uoae"[i],q=7.8,breath=.09,vib=(3.5,.027),atk=.22,rel=dur*.55)
        voice.place(buf,int(at*SR),seg,.83,(i%2-.5)*.35)
    return voice._tail(buf,decay=2.1,taps=20)


def glass_motion():
    random.seed(5109); buf=voice.blank(9.0)
    for i,note in enumerate([72,76,79,83]):
        seg=voice._syl(int(8.2*SR),note,"iei"[i%3],q=17.0,breath=.105,vib=(3.2+i*.25,.010),atk=1.25,rel=2.0)
        voice.place(buf,int((.2+i*.035)*SR),seg,.31,(i-1.5)*.55)
    return voice._tail(buf,decay=1.8,taps=16)


def pulse_hook():
    random.seed(5110); step=60/128/4; buf=voice.blank(step*64+.45)
    notes=[72,76,79,81,79,76,74,76]
    for k in range(64):
        if k%16 in (3,6,7,11,14,15):
            continue
        dur=step*(.62 if k%4 else .88)
        seg=voice._syl(int(dur*SR),notes[k%8],"aei"[k%3],q=13.0,breath=.035,vib=(5.8,.012),atk=.006,rel=dur*.55)
        voice.place(buf,int(k*step*SR),seg,.90,(k%3-1)*.18)
    return buf


PACK = [
    ("vox_silk_breath", silk_breath, "Breath Chops", "airy voiced one-shots", 124),
    ("vox_velvet_bloom", velvet_bloom, "Clear Hooks", "warm open-vowel hook", 120),
    ("vox_prism_vowel", prism_vowel, "Clear Hooks", "bright modern vowel hook", 126),
    ("vox_cloud_phrase", cloud_phrase, "Air & Dream", "soft breath-led phrase", 110),
    ("vox_noir_formant", noir_formant, "Dark Formant", "low intimate vowel phrase", 118),
    ("vox_ember_call", ember_call, "Dark Formant", "dark dance call", 126),
    ("vox_aurora_tail", aurora_tail, "Long Tails", "wide evolving choir vowel", 100),
    ("vox_lunar_tail", lunar_tail, "Long Tails", "slow emotional vowels", 100),
    ("vox_glass_motion", glass_motion, "Air & Dream", "icy moving vowel cloud", 96),
    ("vox_pulse_hook", pulse_hook, "Rhythmic Chops", "tight melodic chop grid", 128),
]


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    rows=[]
    for name, fn, group, character, bpm in PACK:
        row=_write(name,fn(),group,character,bpm)
        rows.append(row)
        print(f"{row['file']:<24} {row['seconds']:>5.1f}s  {row['active_rms_dbfs']:>6.1f} dBFS active")
    manifest=ROOT/"assets"/"curated_vocal_manifest.csv"
    with manifest.open("w",newline="",encoding="utf-8") as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    print(f"wrote {manifest}")

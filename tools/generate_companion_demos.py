"""Generate the three eight-bar SLYCE companion MIDI demos.

No third-party package is used. Each SMF track includes a text instruction
naming the SLYCE preset to select; MIDI itself intentionally contains only
musical performance data, not vendor-specific state blobs.
"""
from pathlib import Path
import struct

PPQ = 480
BAR = PPQ * 4


def vlq(value: int) -> bytes:
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, 0x80 | (value & 0x7F))
        value >>= 7
    return bytes(out)


def track(name: str, preset: str, notes: list[tuple[int, int, int, int, int]]) -> bytes:
    # notes: start tick, duration, note, velocity, channel
    events: list[tuple[int, int, bytes]] = []
    title = name.encode("utf-8")
    instruction = ("SLYCE preset: " + preset).encode("utf-8")
    events.append((0, 0, b"\xFF\x03" + vlq(len(title)) + title))
    events.append((0, 1, b"\xFF\x01" + vlq(len(instruction)) + instruction))
    for start, duration, note, velocity, channel in notes:
        events.append((start, 3, bytes([0x90 | channel, note, velocity])))
        events.append((start + duration, 2, bytes([0x80 | channel, note, 0])))
    events.sort(key=lambda item: (item[0], item[1]))  # note-off before note-on
    body = bytearray()
    last = 0
    for tick, _, message in events:
        body += vlq(tick - last) + message
        last = tick
    body += b"\x00\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + body


def tempo_track(bpm: int, title: str) -> bytes:
    us = round(60_000_000 / bpm)
    name = title.encode("utf-8")
    body = (b"\x00\xFF\x03" + vlq(len(name)) + name
            + b"\x00\xFF\x51\x03" + us.to_bytes(3, "big")
            + b"\x00\xFF\x58\x04\x04\x02\x18\x08"
            + b"\x00\xFF\x2F\x00")
    return b"MTrk" + struct.pack(">I", len(body)) + body


def chord_notes(chords: list[tuple[int, ...]], channel: int, velocity: int = 68):
    result = []
    for bar in range(8):
        for note in chords[bar % len(chords)]:
            result.append((bar * BAR, BAR - 60, note, velocity, channel))
    return result


def bass_notes(roots: list[int], channel: int):
    result = []
    for bar in range(8):
        root = roots[bar % len(roots)]
        for beat in (0, 2):
            result.append((bar * BAR + beat * PPQ, PPQ + PPQ // 2,
                           root, 82 if beat == 0 else 70, channel))
    return result


def vocal_notes(pattern: tuple[int, ...], channel: int):
    result = []
    for bar in range(8):
        for step, note in enumerate(pattern):
            if (bar + step) % 3 != 2:
                result.append((bar * BAR + step * (PPQ // 2), PPQ // 3,
                               note, 78 + (step % 3) * 7, channel))
    return result


def melody_notes(scale: tuple[int, ...], channel: int):
    result = []
    for bar in range(8):
        for step in range(4):
            note = scale[(bar * 2 + step) % len(scale)]
            result.append((bar * BAR + step * PPQ, PPQ * 3 // 4,
                           note, 64 + ((bar + step) % 4) * 6, channel))
    return result


def write_demo(path: Path, title: str, bpm: int, specs):
    chunks = [tempo_track(bpm, title)]
    for name, preset, notes in specs:
        chunks.append(track(name, preset, notes))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    path.write_bytes(header + b"".join(chunks))


def main():
    out = Path(__file__).resolve().parents[1] / "examples" / "companion"
    out.mkdir(parents=True, exist_ok=True)

    write_demo(out / "SLYCE_Velvet_8bar.mid", "SLYCE Velvet set", 112, [
        ("Velvet Vocal", "Velvet Vocal Kit", vocal_notes((36, 38, 40, 43), 0)),
        ("Velvet EP", "Velvet EP", chord_notes([(60, 63, 67), (58, 62, 65),
                                                  (55, 58, 62), (56, 60, 63)], 1)),
        ("Velvet Bass", "Velvet Bass", bass_notes([36, 34, 31, 32], 2)),
        ("Human Air", "Human Air", chord_notes([(72, 75, 79), (70, 74, 77),
                                                  (67, 70, 74), (68, 72, 75)], 3, 48)),
    ])
    write_demo(out / "SLYCE_Glass_8bar.mid", "SLYCE Glass set", 126, [
        ("Glass Vocal", "Glass Vocal Kit", vocal_notes((36, 40, 38, 43, 41), 0)),
        ("Crystal Drop", "Crystal Drop", melody_notes((72, 75, 79, 82, 84, 82, 79), 1)),
        ("Pearl Bell", "Pearl Bell", melody_notes((84, 87, 91, 89, 87), 2)),
        ("Pure Sub", "Pure Sub", bass_notes([36, 39, 31, 34], 3)),
    ])
    write_demo(out / "SLYCE_Dream_8bar.mid", "SLYCE Dream set", 98, [
        ("Dream Vocal", "Dream Vocal Kit", vocal_notes((36, 39, 43, 41), 0)),
        ("Midnight Keys", "Midnight Keys", chord_notes([(57, 60, 64), (53, 57, 60),
                                                          (55, 59, 62), (52, 55, 59)], 1, 62)),
        ("Cloud Pad", "Cloud Pad", chord_notes([(69, 72, 76), (65, 69, 72),
                                                  (67, 71, 74), (64, 67, 71)], 2, 44)),
        ("Soft Chime", "Soft Chime", melody_notes((81, 84, 88, 86, 84, 79), 3)),
    ])


if __name__ == "__main__":
    main()

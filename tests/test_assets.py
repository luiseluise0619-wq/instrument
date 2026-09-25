import csv
import hashlib
import json
import re
import unittest
import wave
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def pcm24(sample: bytes) -> int:
    value = int.from_bytes(sample, "little", signed=False)
    return value - (1 << 24) if value & (1 << 23) else value


class EmbeddedAssets(unittest.TestCase):
    def test_release_contains_only_the_24_curated_vocal_banks(self):
        names = json.loads((ROOT / "assets/vocal_asset_names.json").read_text())
        with (ROOT / "assets/premium_vocal_manifest.csv").open(
            newline="", encoding="utf-8"
        ) as manifest_file:
            rows = list(csv.DictReader(manifest_file))

        self.assertEqual(len(names), 24)
        self.assertEqual(len(rows), 24)
        manifest = {row["file"]: row for row in rows}
        self.assertEqual(set(names), set(manifest))
        self.assertEqual(Counter(row["source_file"] for row in rows), {
            "glass_tide_a": 4,
            "glass_tide_b": 4,
            "quiet_blue_a": 4,
            "quiet_blue_b": 4,
            "come_undo_me": 4,
            "dark_space": 4,
        })
        self.assertEqual(sum(int(row["phrases"]) for row in rows), 192)

        digests = set()
        for name in names:
            with self.subTest(name=name):
                path = ROOT / "examples" / name
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertEqual(digest, manifest[name]["sha256"])
                self.assertNotIn(digest, digests)
                digests.add(digest)

                with wave.open(str(path), "rb") as wav:
                    self.assertEqual(wav.getnchannels(), 2)
                    self.assertEqual(wav.getsampwidth(), 3)
                    self.assertEqual(wav.getframerate(), 44100)
                    self.assertGreater(wav.getnframes(), 44100)
                    first = wav.readframes(1)
                    wav.setpos(wav.getnframes() - 1)
                    last = wav.readframes(1)
                self.assertEqual([pcm24(first[:3]), pcm24(first[3:])], [0, 0])
                self.assertEqual([pcm24(last[:3]), pcm24(last[3:])], [0, 0])

    def test_source_symbols_match_declared_assets_and_no_retired_catalogue_remains(self):
        names = json.loads((ROOT / "assets/vocal_asset_names.json").read_text())
        source = (ROOT / "Source/PluginProcessor.cpp").read_text(encoding="utf-8")
        symbols = set(re.findall(r"BinaryData::([a-z0-9_]+)_wav\b", source))
        self.assertEqual(symbols, {Path(name).stem for name in names})
        self.assertNotIn("#if 0 // Retired catalogue", source)
        self.assertNotIn("slyce::assets::isSynthetic", source)
        self.assertNotRegex(source, r"BinaryData::(?:vox_|vocal_chop_demo)")


if __name__ == "__main__":
    unittest.main(verbosity=2)

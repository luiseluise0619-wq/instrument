import importlib.util
from pathlib import Path
import tempfile
import unittest
import hashlib
import json
import struct
import numpy as np
import soundfile as sf

script = Path(__file__).resolve().parent.parent / "tools" / "polish_samples.py"
spec = importlib.util.spec_from_file_location("polish_samples", script)
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

class PolishTests(unittest.TestCase):
    def test_vocal_shape_finite_and_ceiling(self):
        sr=48000; t=np.arange(sr)/sr
        x=np.column_stack((.9*np.sin(2*np.pi*220*t)+.03,.7*np.sin(2*np.pi*330*t)-.02))
        y,info=m.process_audio(x,sr,"vocal",True,True)
        self.assertEqual(y.shape,x.shape);self.assertTrue(np.isfinite(y).all())
        self.assertLessEqual(np.max(np.abs(y)),10**(-1/20)+1e-12)
        self.assertEqual(float(np.max(np.abs(y[[0,-1]]))),0.)
    def test_instrument_preserves_gain_and_envelope(self):
        sr=48000;t=np.arange(sr)/sr;x=(.4*np.sin(2*np.pi*440*t)+.04)[:,None]
        y,_=m.process_audio(x,sr,"instrument")
        self.assertTrue(np.allclose(y,x-.04,atol=1e-10))
    def test_instrument_rejects_level_matching(self):
        with self.assertRaises(ValueError):m.process_audio(np.ones((100,1)),48000,"instrument",True)
    def test_invalid_values_rejected(self):
        with self.assertRaises(ValueError):m.process_audio(np.full((100,1),np.nan),48000,"vocal")
    def test_silent_and_tiny_unchanged(self):
        for x in (np.zeros((100,1)),np.ones((1,1))*.7):
            y,_=m.process_audio(x,48000,"vocal");self.assertTrue(np.array_equal(x,y))
    def test_dry_run_writes_no_audio(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);src=root/"source";src.mkdir();out=root/"result"
            sf.write(src/"voice.wav",.3*np.sin(np.arange(4000)*.1),48000,subtype="PCM_24")
            before=(src/"voice.wav").read_bytes()
            self.assertEqual(m.main(["--input",str(src),"--output",str(out)]),0)
            self.assertFalse((out/"voice.wav").exists());self.assertEqual(before,(src/"voice.wav").read_bytes())
    def test_apply_preserves_audio_format_duration_and_loop_metadata(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);src=root/"source";src.mkdir();out=root/"result"
            wav=src/"voice.wav";sf.write(wav,.3*np.sin(np.arange(4000)*.1),48000,subtype="PCM_24")
            smpl=struct.pack("<9I",0,0,20833,60,0,0,0,0,0)
            m.append_metadata(wav,[(b"smpl",smpl),(b"cue ",struct.pack("<I",0))])
            before=hashlib.sha256(wav.read_bytes()).hexdigest()
            self.assertEqual(m.main(["--input",str(src),"--output",str(out),"--apply"]),0)
            p=sf.info(out/"voice.wav");self.assertEqual((p.frames,p.channels,p.samplerate,p.subtype),(4000,1,48000,"PCM_24"))
            self.assertIn((b"smpl",smpl),m.riff_metadata(out/"voice.wav"))
            self.assertEqual(before,hashlib.sha256(wav.read_bytes()).hexdigest())
            self.assertEqual(m.main(["--input",str(src),"--output",str(out),"--apply"]),1)
    def test_nested_output_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)
            with self.assertRaises(SystemExit):m.main(["--input",str(src),"--output",str(src/"out")])
if __name__=="__main__":unittest.main(verbosity=2)

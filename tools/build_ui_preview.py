#!/usr/bin/env python3
"""Visual layout preview. This is explicitly NOT a JUCE runtime screenshot."""
from pathlib import Path
import json,re
import numpy as np
import soundfile as sf
ROOT=Path(__file__).resolve().parents[1]
x,sr=sf.read(ROOT/'assets/fallback_vocals/vocal_chop_demo.wav',always_2d=True)
def peaks(x,n):
    out=[]
    for i in range(n):
        a=x[i*len(x)//n:max(i*len(x)//n+1,(i+1)*len(x)//n)]
        out.append([round(min(0,float(a.min())),5),round(max(0,float(a.max())),5)])
    return out
# These are display examples only. The native editor gets all counts from the processor.
src=(ROOT/'Source/PluginProcessor.cpp').read_text()
data={'name':'Velvet Vowels [SYN]','sampleRate':sr,'peaks':peaks(x,1600),
      'slices':[peaks(x[i*len(x)//8:(i+1)*len(x)//8],256) for i in range(8)],
      'presetCount':0, 'chopCount':0}
# Derive display counts from the production table where available, otherwise label the data as an example.
match=re.search(r'getNumChopPresets\(\).*?return\s+(\d+)',src,re.S)
if not match:raise RuntimeError('Could not find the chop preset count in source.')
data['chopCount']=int(match[1])
# Count the actual SoundPreset table, not unrelated braced strings elsewhere.
sound_table=re.search(r'const SoundPreset kSoundPresets\[\]\s*=\s*\{(.*?)\n\};',src,re.S)
if not sound_table:raise RuntimeError('Could not find factory sound presets.')
factory_presets=data['chopCount']+len(re.findall(r'\{\s*"',sound_table[1]))
# The native All Presets row includes synthesizer instruments and sound presets.
# 429 is the validated factory count in reports/factory_matrix_summary.txt.
data['presetCount']=429+factory_presets
contract=json.loads((ROOT/'assets/reference/layout.json').read_text())
html=(ROOT/'preview/index.template.html').read_text().replace('__CONTRACT__',json.dumps(contract)).replace('__ENVELOPE__',json.dumps(data,separators=(',',':')))
(ROOT/'preview/index.html').write_text(html)
print('Wrote preview/index.html (shared geometry and skin; not a native screenshot).')

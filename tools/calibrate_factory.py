#!/usr/bin/env python3
"""Conservative fixed factory bus trims from native reference renders.
Not LUFS normalization and not listening validation. Does not compress velocity.
Run: python tools/calibrate_factory.py reports/factory_before_calibration.csv
"""
import csv, math, statistics, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=Path(sys.argv[1]) if len(sys.argv)>1 else root/'reports/factory_before_calibration.csv'
groups={}
with source.open(newline='',encoding='utf-8') as f:
    for r in csv.DictReader(f): groups.setdefault(int(r['index']),[]).append(r)
if sorted(groups)!=list(range(429)): raise SystemExit('Expected contiguous 429 instrument indexes')
records=[]
for index,rows in sorted(groups.items()):
    name=rows[0]['name'];cat=rows[0]['category']
    rms=statistics.mean(float(r['rms']) for r in rows)
    peak=max(float(r['peak']) for r in rows)
    family=[]
    for rr in groups.values():
        if rr[0]['category']==cat:
            family.append(20*math.log10(max(1e-12,statistics.mean(float(r['rms']) for r in rr))))
    # Only pull half way toward the family median. Drums/FX are intentional
    # level contrasts; their correction is even more conservative.
    strength=.25 if cat in ('DRUMS','HITS','MISC') else .50
    gain=max(-5.0,min(2.5,(statistics.median(family)-20*math.log10(max(rms,1e-12)))*strength))
    # Prevent boosting transient-rich patches above a conservative reference
    # sample peak. The final common limiter still protects live polyphony.
    gain=min(gain,20*math.log10(.63/max(1e-12,peak)))
    gain=round(gain,2)
    records.append((index,cat,name,gain,rms,peak))
header='''#pragma once
#include <array>
// Fixed dB trims derived by tools/calibrate_factory.py. Partial correction
// within each family; velocities, transient shape and relative voices remain.
// Reference renders are finite-signal tests, not subjective listening approval.
namespace slyce::factory {
inline constexpr std::array<float,429> levelTrimsDb {{
'''
for i,cat,name,gain,rms,peak in records:
    header+=f'    {gain:.2f}f, // {i}: {name}\n'
header+='}};\n}\n'
(root/'Source/AudioEngine/FactoryLevelTrims.h').write_text(header,encoding='utf-8')
with (root/'reports/factory_level_trims.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.writer(f);w.writerow(('index','category','name','trim_db','reference_rms','reference_peak'));w.writerows(records)
print(f'Wrote {len(records)} fixed trims; range {min(r[3] for r in records)}..{max(r[3] for r in records)} dB')

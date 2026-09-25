#!/usr/bin/env python3
"""Deterministic SYNTHETIC vowel/chop fallback bank; not human recordings.
Replaces no user files. NumPy + SciPy required only for regeneration.
All waveforms are made here from harmonics/noise; no downloaded samples/models.
"""
from pathlib import Path
import csv, hashlib, json, math, wave
import numpy as np
from scipy.signal import butter, sosfilt

ROOT=Path(__file__).resolve().parents[1]
SR=44100
DURATION=4.0
# Artistic resonant peaks (Hz), not a model of an identified person's voice.
VOWELS=np.array([[730,1150,2600],[430,1700,2650],[320,2150,3000],
                 [470,860,2450],[340,650,2300]],float)

def env(n,attack,release):
    out=np.ones(n);a=min(n//2,max(8,int(attack*SR)));r=min(n//2,max(8,int(release*SR)))
    out[:a]=np.sin(np.linspace(0,np.pi/2,a))**2
    out[-r:]=np.cos(np.linspace(0,np.pi/2,r))**2
    return out

def syllable(seconds,note,vowel_a,vowel_b,rng,airy=False,choir=False,robot=False,soft=False):
    n=max(32,int(seconds*SR));t=np.arange(n)/SR
    f0=440*2**((note-69)/12)
    bend=2**((rng.uniform(-.11,.11)*np.exp(-t/.025)+.014*np.sin(2*np.pi*5.2*t))/12)
    phase=2*np.pi*f0*np.cumsum(bend)/SR
    a=VOWELS[vowel_a]*rng.uniform(.96,1.055)
    b=VOWELS[vowel_b]*rng.uniform(.96,1.055)
    vowel_pos=np.clip((t-.015)/max(.035,seconds*.7),0,1)
    vowel_pos=vowel_pos*vowel_pos*(3-2*vowel_pos)
    x=np.zeros(n)
    bands=np.array([100,170,240.])
    max_h=min(70,int(7600/f0))
    for h in range(1,max_h+1):
        frequency=f0*h
        def amp(peaks):
            res=np.sum(np.array([1.0,.64,.28])*np.exp(-.5*((frequency-peaks)/bands)**2))
            return (.08+res)/(h**(1.2 if soft else .98))
        weight=amp(a)*(1-vowel_pos)+amp(b)*vowel_pos
        if choir:
            tone=(np.sin(phase*h)+.42*np.sin(phase*h*2**(.055/12)+.8)
                  +.42*np.sin(phase*h*2**(-.065/12)+1.7))/1.84
        elif robot:
            tone=np.sin(phase*h + .7*np.sin(phase*.5))
        else: tone=np.sin(phase*h + .08/h)
        x+=weight*tone
    noise=sosfilt(butter(2,[1600,7200],btype='bandpass',fs=SR,output='sos'),rng.standard_normal(n))
    x+=noise*(.025 if airy else .006)
    x-=np.mean(x)
    peak=max(1e-9,np.max(np.abs(x)))
    x=x/peak
    return x*env(n,.026 if soft else .0045,.11 if soft else .035)

def render(name,index):
    rng=np.random.default_rng(2026091900+index)
    kind=name.removesuffix('.wav').removeprefix('vox_')
    pad=any(k in kind for k in ('pad','sustain','ambient','choir','cathedral','swell','monk','oohstack','gospel'))
    perc=any(k in kind for k in ('beatbox','mouthperc','clicks','gasp','impact','breath','vowelperc','stabs','bigroom'))
    airy=any(k in kind for k in ('air','whisp','breath','falsetto','ghost','glass'))
    robotic=any(k in kind for k in ('robot','vocoder','talkbox'))
    dark=any(k in kind for k in ('minor','sad','deep','drill','lament','monk'))
    out=np.zeros(int(SR*DURATION))
    intervals=[0,3,7,10] if dark else [0,4,7,12]
    base=48+(-12 if 'deep' in kind else 12 if 'kids' in kind or 'falsetto' in kind else 0)
    if pad:
        for j,iv in enumerate([0,7,12] if 'choir' in kind else [0,12]):
            voice=syllable(3.75,base+iv,index%5,(index+2)%5,rng,airy,True,robotic,True)
            start=int((.016+j*.009)*SR);out[start:start+len(voice)]+=voice/(1+j*.55)
    else:
        step=1/3 if 'triplet' in kind or 'afro' in kind else .5
        hits=int(3.8/step)
        for j in range(hits):
            # Different layouts retain genuine silence for transient detection.
            swing=(.045 if any(k in kind for k in ('swung','garage','rnbrun','soul')) and j%2 else 0)
            at=.02+j*step+swing
            note=base+intervals[(j+index)%4]
            dur=step*(.36 if perc else .67 if 'stutter' in kind else .83)
            v=syllable(dur,note,(index+j)%5,(index+j+1)%5,rng,airy,False,robotic,False)
            if perc:
                tt=np.arange(len(v))/SR
                v*=np.exp(-tt/(.045+.018*(index%3)))
                n=sosfilt(butter(2,[900,8000],btype='bandpass',fs=SR,output='sos'),rng.standard_normal(len(v)))
                v+=.07*n*np.exp(-tt/.03)*env(len(v),.0015,.015)
            start=int(at*SR);end=min(len(out),start+len(v))
            if end>start:out[start:end]+=v[:end-start]*(.75+.2*(j%3)/2)
    if 'reverse' in kind or 'revchant' in kind: out=out[::-1].copy()
    if any(k in kind for k in ('riser','downlifter','swell')):
        ramp=np.linspace(0,1,len(out));out*=np.sqrt(ramp if 'downlifter' not in kind else 1-ramp)
    if 'telephone' in kind:
        out=sosfilt(butter(3,[400,2700],btype='bandpass',fs=SR,output='sos'),out)
    if any(k in kind for k in ('ambient','cathedral','ghost','glass','sustain')):
        dry=out.copy()
        for sec,gain in [(0.073,.12),(.131,.08),(.223,.045)]:
            n=int(sec*SR);out[n:]+=dry[:-n]*gain
    # Gentle sub-bass removal, no heavy brightening/brick-wall mastering.
    out=sosfilt(butter(2,55,btype='highpass',fs=SR,output='sos'),out)
    out*=env(len(out),.004,.045)
    out-=np.mean(out)
    # Final fades after DC removal keep both file edges precisely at zero.
    out*=env(len(out),.003,.02)
    out*=.794328/max(1e-9,np.max(np.abs(out)))
    assert np.isfinite(out).all()
    return out,('sustained texture' if pad else 'vowel percussion' if perc else 'melodic vowel chops')

def main():
    names=json.loads((ROOT/'assets/vocal_asset_names.json').read_text())
    destination=ROOT/'assets/fallback_vocals';destination.mkdir(parents=True,exist_ok=True)
    rows=[]
    for i,name in enumerate(names):
        x,kind=render(name,i)
        # TPDF dither before signed 16-bit quantization; deterministic per file.
        rng=np.random.default_rng(19260000+i)
        q=np.clip(np.round(x*32767+(rng.random(len(x))-rng.random(len(x)))*.5),-32768,32767).astype('<i2')
        q[0]=q[-1]=0
        path=destination/name
        with wave.open(str(path),'wb') as f:
            f.setnchannels(1);f.setsampwidth(2);f.setframerate(SR);f.writeframes(q.tobytes())
        rows.append(dict(file=name,source='procedural_synthesis_not_human',kind=kind,sample_rate=SR,
                         frames=len(q),bpm=120,peak_dbfs=round(20*math.log10(np.max(abs(q.astype(float)))/32768),3),
                         rms_dbfs=round(20*math.log10(np.sqrt(np.mean((q.astype(float)/32768)**2))),3),
                         sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    with (destination/'manifest.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    print(f'Created {len(rows)} synthetic sources, {sum(r["frames"] for r in rows)/SR:.0f} seconds total.')
if __name__=='__main__':main()

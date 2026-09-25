#!/usr/bin/env python3
"""Build a clean, reference-derived chassis and a single geometry contract.
The source artwork is ONLY static material/lighting. Every changing label,
waveform, key and control is removed; the JUCE editor renders those live.
No third-party font files are copied or embedded.
"""
from pathlib import Path
import json, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT=Path(__file__).resolve().parents[1]
LAYOUT={
 'browserTabs':[[63,137,92,35],[156,137,91,35],[248,137,81,35]],
 'search':[65,180,263,34], 'browser':[62,225,267,463],
 'previous':[365,144,30,38], 'next':[396,144,30,38], 'favorite':[439,144,38,38],
 'title':[498,140,478,44], 'menu':[986,144,39,38], 'bpm':[1053,142,61,39], 'key':[1118,142,63,39],
 'workspaceTabs':[[360,205,104,31],[465,205,73,31],[539,205,74,31],[614,205,74,31]],
 'sliceCount':[1011,207,85,28], 'zoomOut':[1099,207,37,28], 'zoomIn':[1138,207,37,28],
 'wave':[370,249,799,165], 'play':[369,425,59,35], 'snap':[503,425,78,35],
 'sensitivity':[678,428,114,29], 'detect':[811,425,86,35], 'autoSlice':[908,425,76,35],
 'manual':[991,425,82,35], 'clear':[1087,425,81,35],
 'pads':[[358+(208*i),480+(124*j),200,116] for j in range(2) for i in range(4)],
 'rightTabs':[[1198,132,84,39],[1283,132,100,39],[1384,132,98,39]],
 'fxPower':[1441,186,28,28],
 'knobs':[[1230,225,92,84],[1363,225,92,84],[1230,349,92,84],[1363,349,92,84],
          [1217,665,64,64],[1310,665,64,64],[1398,665,64,64]],
 'knobLabels':[[1229,310,94,24],[1362,310,94,24],[1229,434,94,24],[1362,434,94,24],
               [1209,728,80,23],[1302,728,80,23],[1390,728,80,23]],
 'loopPower':[1441,485,28,28], 'loopWave':[1234,521,212,49],
 'loopPrev':[1209,527,23,35], 'loopNext':[1447,527,25,35],
 'loopLength':[1275,578,97,30], 'sync':[1382,578,81,30],
 'keys':[201,782,1091,96], 'pitch':[80,788,35,73], 'mod':[136,788,35,73],
 'overlay':[358,240,821,510],
}
# Attribute table generates attached parameters, accessible names and preview controls.
KNOBS=[('reverb','REVERB'),('delay','DELAY'),('filterCutoff','FILTER'),('drive','DRIVE'),
       ('attack','ATTACK'),('release','RELEASE'),('synthGlide','GLIDE')]

def blank(im,box,top,bottom,radius=0):
    x,y,w,h=box
    a=np.zeros((h,w,3),np.float32)
    t=np.linspace(0,1,h)[:,None,None]
    a[:]=np.array(top)[None,None,:]*(1-t)+np.array(bottom)[None,None,:]*t
    # Original reference's translucent material has a very slight side gradient.
    a+=np.sin(np.linspace(0,math.pi,w))[None,:,None]*0.8
    layer=Image.fromarray(np.uint8(np.clip(a,0,255)))
    if radius:
        mask=Image.new('L',(w,h),0);ImageDraw.Draw(mask).rounded_rectangle((0,0,w-1,h-1),radius,fill=255)
        im.paste(layer,(x,y),mask)
    else: im.paste(layer,(x,y))

def build_assets():
    dest=ROOT/'assets/reference';dest.mkdir(parents=True,exist_ok=True)
    for dark in (False,True):
        theme='dark' if dark else 'light'
        im=Image.open(ROOT/f'design/reference_{theme}.png').convert('RGB')
        if im.size!=(1536,1024):raise ValueError('Reference must be 1536 × 1024.')
        # Keep the exact chassis, wordmark, outer reflections and material boundaries.
        # Clear ALL live fields rather than leaving a screenshot under fake controls.
        b1=(19,22,27) if dark else (240,246,252)
        b2=(14,17,21) if dark else (230,239,247)
        blank(im,[60,137,273,551],b1,b2,6)
        blank(im,[359,139,822,49],b1,b2,5)
        blank(im,[359,204,820,262],(16,19,24) if dark else (243,248,253),b2,7)
        blank(im,[357,477,824,251],(17,20,24) if dark else (232,240,247),b2,4)
        blank(im,[1207,138,268,620],b1,b2,5)
        blank(im,[62,779,1240,105],(15,18,22) if dark else (230,237,244),b2,5)
        # Product counts come from the catalog, not the concept image.
        blank(im,[134,704,185,49],(20,24,31) if dark else (227,239,251),(17,21,27) if dark else (226,237,249),4)
        try:
            import cv2
            a=np.array(im);mask=np.zeros(a.shape[:2],np.uint8)
            mask[931:955,265:1240]=255
            a=cv2.inpaint(a,mask,8,cv2.INPAINT_TELEA);im=Image.fromarray(a)
        except ImportError:
            blank(im,[262,927,979,34],(18,20,29) if dark else (221,233,247),(17,19,27) if dark else (222,234,248),8)
        im.save(dest/f'reference_chrome_{theme}.png',optimize=True)
        # Real metallic centre of the reference dial, with the fixed pointer removed.
        orig=Image.open(ROOT/f'design/reference_{theme}.png').convert('RGB')
        cx,cy=(1276,276) if dark else (1276,267)
        face=orig.crop((cx-33,cy-33,cx+33,cy+33)).convert('RGBA')
        try:
            import cv2
            arr=np.array(face)[:,:,:3]; mask=np.zeros((66,66),np.uint8)
            cv2.line(mask,(33,6),(33,27),255,6)
            arr=cv2.inpaint(arr,mask,4,cv2.INPAINT_TELEA)
            face=Image.fromarray(arr).convert('RGBA')
        except ImportError:
            # Replace the small needle strip with neighbouring brushed-metal texture.
            face.paste(face.crop((22,5,28,29)),(30,5))
        alpha=Image.new('L',(66,66),0);ImageDraw.Draw(alpha).ellipse((4,4,61,61),fill=255)
        face.putalpha(alpha);face.save(dest/f'reference_knob_{theme}.png',optimize=True)
    # Standard JSON is also consumed by the browser preview and the geometry tests.
    doc={'canvas':[1536,1024],'build':'REF-20260920-04','layout':LAYOUT,
         'knobs':[{'id':i,'caption':c,'bounds':r,'labelBounds':l} for (i,c),r,l in zip(KNOBS,LAYOUT['knobs'],LAYOUT['knobLabels'])]}
    (dest/'layout.json').write_text(json.dumps(doc,indent=2)+'\n')
    h=['#pragma once','#include <juce_gui_basics/juce_gui_basics.h>','#include <array>',
       'namespace slyce::reference {','inline constexpr int width=1536, height=1024;',
       'inline constexpr const char* build="REF-20260920-04";',
       'struct Rect { int x,y,w,h; juce::Rectangle<int> get() const {return {x,y,w,h};} };']
    for name,val in LAYOUT.items():
        if isinstance(val[0],list):
            h.append('inline constexpr std::array<Rect, %d> %s {{ %s }};'%(len(val),name,','.join('{'+','.join(map(str,v))+'}' for v in val)))
        else:h.append('inline constexpr Rect %s {%s};'%(name,','.join(map(str,val))))
    h.append('inline constexpr std::array<const char*,7> parameterIDs {{'+','.join('"'+i+'"' for i,c in KNOBS)+'}};')
    h.append('inline constexpr std::array<const char*,7> captions {{'+','.join('"'+c+'"' for i,c in KNOBS)+'}};')
    h.append('}\n')
    (ROOT/'Source/UI/Reference/ReferenceLayout.h').write_text('\n'.join(h))
    print('Built 4 skin assets and one shared layout contract.')

if __name__=='__main__':build_assets()

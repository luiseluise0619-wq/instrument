#!/usr/bin/env python3
"""Structural preflight, NOT a compiler or proof of JUCE API compatibility."""
from pathlib import Path
import json,re,sys
ROOT=Path(__file__).resolve().parents[1]
external={'JuceHeader.h','BinaryData.h','ReferenceSkinData.h','signalsmith-stretch.h'}
issues=[]
files=list((ROOT/'Source').rglob('*.h'))+list((ROOT/'Source').rglob('*.cpp'))
# Strip comments/string/char literals before simple bracket matching.
token=re.compile(r'//[^\n]*|/\*.*?\*/|R"([^ ()\\\t\r\n]*)\(.*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',re.S)
for p in files:
    s=p.read_text(encoding='utf-8-sig')
    for inc in re.findall(r'^\s*#include\s*"([^"]+)"',s,re.M):
        if inc not in external and not (p.parent/inc).exists() and not (ROOT/'Source'/inc).exists():
            issues.append(f'{p.relative_to(ROOT)}: missing local include {inc}')
    stack=[];matching={')':'(',']':'[','}':'{'}
    for x in token.sub(' ',s):
        if x in '([{':stack.append(x)
        elif x in ')]}':
            if not stack or stack.pop()!=matching[x]:issues.append(f'{p.relative_to(ROOT)}: unmatched {x}');break
    else:
        if stack:issues.append(f'{p.relative_to(ROOT)}: unclosed delimiters')
cm=(ROOT/'CMakeLists.txt').read_text()
for name in re.findall(r'\bSource/[\w/]+\.cpp\b',cm):
    if not (ROOT/name).is_file():issues.append('CMake missing '+name)
report={'kind':'structural_preflight_not_JUCE_compilation','source_files':len(files),'issues':issues}
print(json.dumps(report,ensure_ascii=False,indent=2));sys.exit(bool(issues))

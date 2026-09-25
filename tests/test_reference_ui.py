"""Static UI contract checks. These are NOT a JUCE compiler or visual-runtime test."""
from pathlib import Path
import json,re,unittest,hashlib,zipfile
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
D=json.loads((ROOT/'assets/reference/layout.json').read_text(encoding='utf-8'));L=D['layout']
class ReferenceUIContract(unittest.TestCase):
 def test_dedicated_entrypoint(self):
  src=(ROOT/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
  body=src.split('VocalChopAudioProcessor::createEditor()',1)[1].split('}',1)[0]
  self.assertIn('return new SlyceReferenceEditor (*this);',body)
  self.assertNotIn('new VocalChopAudioProcessorEditor',body)
 def test_build_sources(self):
  src=(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
  self.assertIn('Source/UI/Reference/ReferenceEditor.cpp',src)
  self.assertNotIn('Source/PluginEditor.cpp',src)
  self.assertIn('HEADER_NAME ReferenceSkinData.h NAMESPACE ReferenceSkinData',src)
  self.assertIn('PRIVATE VocalChopData SlyceReferenceSkin',src)
 def test_control_bounds(self):
  for name,value in L.items():
   values=value if isinstance(value[0],list) else [value]
   for x,y,w,h in values:
    with self.subTest(name=name,bounds=(x,y,w,h)):
     self.assertGreater(w,0);self.assertGreater(h,0);self.assertGreaterEqual(x,0);self.assertGreaterEqual(y,0)
     self.assertLessEqual(x+w,1536);self.assertLessEqual(y+h,1024)
 def test_grid_is_two_by_four(self):
  self.assertEqual(len(L['pads']),8);self.assertEqual(len({r[0] for r in L['pads']}),4);self.assertEqual(len({r[1] for r in L['pads']}),2)
  self.assertEqual(L['pads'][0],[358,480,200,116])
 def test_parameter_bindings(self):
  src=(ROOT/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
  for k in D['knobs']:self.assertRegex(src,r'"'+re.escape(k['id'])+r'"\s*,\s*"')
  editor=(ROOT/'Source/UI/Reference/ReferenceEditor.cpp').read_text(encoding='utf-8')
  self.assertIn('SliderAttachment>(proc.getAPVTS(),ref::parameterIDs',editor)
  self.assertIn('SliderAttachment>(proc.getAPVTS(),"pitch",pitch)',editor)
  self.assertIn('SliderAttachment>(proc.getAPVTS(),"synthVibrato",mod)',editor)
 def test_no_fake_waveform_in_chrome(self):
  for theme in ['light','dark']:
   img=Image.open(ROOT/f'assets/reference/reference_chrome_{theme}.png')
   self.assertEqual(img.size,(1536,1024))
   # Dynamic waveform field was removed from the art: smooth y gradient,
   # not the original alternating voice waveform. Sampling one row suffices
   # for this asset contract; this is not an audio test.
   channel=[img.getpixel((x,341))[2] for x in range(386,1145)]
   self.assertLess(max(channel)-min(channel),5)
 def test_live_audio_and_ui(self):
  src=(ROOT/'Source/UI/Reference/ReferenceEditor.cpp').read_text(encoding='utf-8')
  for symbol in ['proc.getLoadedSample()','proc.getSliceEngine()','proc.getScopeRing()',
                 'proc.triggerSliceDirectPad(i)','proc.pressSlicePad(offset,velocity)',
                 'proc.releaseSlicePad(offset)','proc.getLooper().readOverview',
                 'std::make_unique<LooperPanel>(proc)','proc.loadSampleFromFile',
                 'proc.loadSfzBank','proc.applyInstrument','proc.applyPreset',
                 'proc.loadUserPreset','proc.saveUserPreset','proc.finalizeActivation']:
   with self.subTest(symbol=symbol):self.assertIn(symbol,src)
 def test_unlicensed_first_launch_opens_activation(self):
  editor=(ROOT/'Source/UI/Reference/ReferenceEditor.cpp').read_text(encoding='utf-8')
  panel=(ROOT/'Source/UI/UnlockPanel.h').read_text(encoding='utf-8')
  self.assertIsNotNone(re.search(r'if\s*\(!proc\.isLicensed\(\)\).*?showActivationPanel\(\)',editor,re.S))
  self.assertIn('panel->onActivated=restore',editor)
  self.assertIn('std::function<void()> onActivated',panel)
  self.assertIn('self->onActivated();',panel)
 def test_browser_selection_preserves_scroll_offset(self):
  editor=(ROOT/'Source/UI/Reference/ReferenceEditor.cpp').read_text(encoding='utf-8')
  refresh=editor.split('void refreshBrowser(bool resetScroll=false)',1)[1].split('int countKind',1)[0]
  self.assertIn('if(resetScroll)rowOffset=0;',refresh)
  self.assertNotIn('visible.clear();rowOffset=0;',refresh)
  self.assertIn('refreshBrowser(false);syncRecordBars()',editor)
  self.assertIn('search.getText();refreshBrowser(true)',editor)
  self.assertIn('selectedCategory={};query={};search.clear();refreshBrowser(true)',editor)
 def test_reference_namespace_identifiers_exist(self):
  src=(ROOT/'Source/UI/Reference/ReferenceEditor.cpp').read_text(encoding='utf-8')
  h=(ROOT/'Source/UI/Reference/ReferenceLayout.h').read_text(encoding='utf-8')
  for word in set(re.findall(r'\bref::([A-Za-z_]\w*)',src)):
   self.assertRegex(h,r'\b'+word+r'\b')
 def test_no_control_characters(self):
  src=(ROOT/'Source/UI/Reference/ReferenceEditor.cpp').read_text(encoding='utf-8')
  self.assertFalse(any(ord(c)<32 and c not in '\n\t\r' for c in src))
 def test_non_destructive_build(self):
  script=(ROOT/'BUILD_REFERENCE_WINDOWS.ps1').read_text(encoding='utf-8')
  self.assertIn('build-reference-ui',script);self.assertNotIn('Remove-Item',script)
  self.assertIn('REF-20260920-04',script)
 def test_preview_is_not_mislabeled_native(self):
  html=(ROOT/'preview/index.html').read_text(encoding='utf-8')
  self.assertIn('JUCE 실행 화면이나 오디오 엔진이 아닙니다',html)
  self.assertIn('REF-20260919-02',html)
 def test_audio_code_preserved(self):
  original=ROOT.parent/'SLYCE_Integrated_Source_20260919.zip'
  if not original.exists():self.skipTest('Original delivery ZIP not present for checksum comparison')
  with zipfile.ZipFile(original) as archive:
   for name in archive.namelist():
    rel=Path(*Path(name).parts[1:])
    if str(rel).startswith(('Source/AudioEngine/','Source/DSP/')) and not name.endswith('/'):
     self.assertEqual(hashlib.sha256((ROOT/rel).read_bytes()).digest(),hashlib.sha256(archive.read(name)).digest(),str(rel))
if __name__=='__main__':unittest.main(verbosity=2)

// Real-JUCE screenshot tool. Build with -DSLYCE_BUILD_TOOLS=ON, then run
// SlyceUISnapshot /absolute/path/to/output under a desktop or xvfb-run.
// Unlike preview/index.html this instantiates the SAME editor as the VST3.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Reference/ReferenceEditor.h"
#include <iostream>

static juce::Button* findButton(juce::Component& parent, const juce::String& name)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent(i);
        if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr
            && button->getName() == name)
            return button;
        if (auto* nested = findButton(*child, name))
            return nested;
    }
    return nullptr;
}

static void collectButtons(juce::Component& parent, juce::Array<juce::Button*>& out)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent(i);
        if (auto* button = dynamic_cast<juce::Button*>(child)) out.add(button);
        collectButtons(*child, out);
    }
}

static void collectSliders(juce::Component& parent, juce::Array<juce::Slider*>& out)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent(i);
        if (auto* slider = dynamic_cast<juce::Slider*>(child)) out.add(slider);
        collectSliders(*child, out);
    }
}

static juce::TextButton* findTextButton(juce::Component& parent,
                                        const juce::String& text, int occurrence=0)
{
    juce::Array<juce::Button*> buttons;collectButtons(parent,buttons);
    for(auto* button:buttons)
        if(auto* textButton=dynamic_cast<juce::TextButton*>(button))
            if(textButton->getButtonText()==text&&occurrence--==0)return textButton;
    return nullptr;
}

static juce::Button* findButtonByTooltip(juce::Component& parent,
                                         const juce::String& tooltipStart)
{
    juce::Array<juce::Button*> buttons;collectButtons(parent,buttons);
    for(auto* button:buttons)if(button->getTooltip().startsWith(tooltipStart))return button;
    return nullptr;
}

static bool capture(juce::Component& editor, const juce::File& output,
                    const juce::String& name)
{
    auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
    auto file=output.getChildFile(name+".png");
    file.deleteFile();auto stream=file.createOutputStream();
    return stream&&juce::PNGImageFormat().writeImageToStream(image,*stream);
}

int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    const auto output=argc>1?juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                             :juce::File::getCurrentWorkingDirectory().getChildFile("ui-screenshots");
    if(!output.createDirectory()){std::cerr<<"Cannot create output directory\n";return 1;}
    VocalChopAudioProcessor processor;
    processor.prepareToPlay(48000.0,512);
    processor.getSliceEngine().setMode(SliceEngine::Grid);
    processor.getSliceEngine().setGridDivision(8);
    processor.getSliceEngine().rebuildSlices();
    processor.setSelectedSlice(0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* reference=dynamic_cast<SlyceReferenceEditor*>(editor.get());
    if(!reference){std::cerr<<"Wrong editor: reference routing is not active\n";return 2;}
    editor->setSize(1536,1024);editor->addToDesktop(0);editor->setVisible(true);
    const char* names[]={"paper-light","noir-violet","mint-glass","rose-quartz","amber-studio","azure-ice-glass","lavender-haze","champagne-silver"};
    for(int theme=0;theme<8;++theme){
        reference->setReferenceTheme(theme);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
        auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
        auto file=output.getChildFile("JUCE_reference_"+juce::String(names[theme])+".png");
        file.deleteFile();auto stream=file.createOutputStream();
        if(!stream||!juce::PNGImageFormat().writeImageToStream(image,*stream))return 3;
        std::cout<<file.getFullPathName()<<"\n";
    }
    // The USER page is the most important first-run import route. Keep a
    // real-JUCE snapshot of it so its drop target cannot regress unnoticed.
    reference->setReferenceTheme(0);
    if(auto* user=findButton(*editor,"USER"))user->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    auto userImage=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
    auto userFile=output.getChildFile("JUCE_reference_user-import.png");
    userFile.deleteFile();
    {
        auto userStream=userFile.createOutputStream();
        if(!userStream||!juce::PNGImageFormat().writeImageToStream(userImage,*userStream))return 4;
    }
    std::cout<<userFile.getFullPathName()<<"\n";

    // Exercise every non-modal top-level button through the same JUCE Button
    // callback used by a real mouse click. Modal file/save/menu paths are
    // inventoried below but intentionally not submitted by an unattended test.
    juce::StringArray audit;
    auto click=[&](const juce::String& name,const juce::String& shot={}){
        auto* button=findButton(*editor,name);
        if(button==nullptr){audit.add("FAIL missing: "+name);return false;}
        if(!button->isVisible()||!button->isEnabled()||button->getWidth()<=0||button->getHeight()<=0){audit.add("FAIL unavailable: "+name);return false;}
        button->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        if(shot.isNotEmpty()&&!capture(*editor,output,shot)){audit.add("FAIL capture: "+name);return false;}
        audit.add("PASS click: "+name);return true;
    };
    auto dismissPopupOrAlert=[&]{
        juce::PopupMenu::dismissAllActiveMenus();
        if(auto* modal=juce::Component::getCurrentlyModalComponent())modal->exitModalState(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    };
    auto openAndDismiss=[&](juce::Button* button,const juce::String& label){
        if(button==nullptr){audit.add("FAIL modal button missing: "+label);return;}
        button->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        dismissPopupOrAlert();audit.add("PASS modal path opens: "+label);
    };

    std::cerr << "AUDIT browser/workspace\n";
    click("PRESETS","audit-01-presets");
    click("INSTRUMENTS","audit-02-instruments");
    click("USER","audit-03-user");
    click("WAVEFORM","audit-04-waveform");
    click("SLICES","audit-05-slices");
    click("LOOPER","audit-06-looper");
    {
        std::cerr << "AUDIT looper controls\n";
        juce::Array<juce::Button*> looperButtons;collectButtons(*editor,looperButtons);
        int recIndex=0;
        for(auto* button:looperButtons)
            if(auto* textButton=dynamic_cast<juce::TextButton*>(button);textButton&&textButton->isVisible()){
                const auto label=textButton->getButtonText();
                std::cerr << "AUDIT looper button: " << label << "\n";
                const auto font=textButton->getLookAndFeel().getTextButtonFont(*textButton,textButton->getHeight());
                const int required=juce::GlyphArrangement::getStringWidthInt(font,label)+4;
                audit.add(required<=textButton->getWidth()?"PASS looper label fits: "+label:"FAIL looper label clipped: "+label);
                if(label=="Rec"){
                    std::cerr << "AUDIT looper record start: " << (recIndex+1) << "\n";
                    textButton->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);juce::AudioBuffer<float> block(2,512);juce::MidiBuffer noMidi;block.clear();processor.processBlock(block,noMidi);
                    std::cerr << "AUDIT looper record stop: " << (recIndex+1) << "\n";
                    audit.add(processor.getLooper().getTrackState(recIndex)!=LoopStation::Empty?"PASS looper track "+juce::String(recIndex+1)+" record":"FAIL looper track "+juce::String(recIndex+1)+" record");
                    textButton->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);block.clear();processor.processBlock(block,noMidi);
                    std::cerr << "AUDIT looper record clear: " << (recIndex+1) << "\n";
                    processor.getLooper().tapClear(recIndex);block.clear();processor.processBlock(block,noMidi);++recIndex;
                }
            }
        if(auto* sync=findTextButton(*editor,"Sync")){const bool before=processor.getLooper().isTempoSync();sync->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);audit.add(processor.getLooper().isTempoSync()!=before?"PASS looper panel Sync":"FAIL looper panel Sync");sync->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);}
        if(auto* tap=findTextButton(*editor,"Tap")){tap->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);audit.add("PASS looper Tap");}
        if(auto* play=findTextButton(*editor,"Play all")){play->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(80);audit.add("PASS looper Play all empty-safe");}
        for(int more=0;;++more){auto* actions=findTextButton(*editor,"...",more);if(actions==nullptr)break;openAndDismiss(actions,"Looper actions "+juce::String(more+1));}
        openAndDismiss(findTextButton(*editor,"DRAG"),"Looper hold and drag help");
    }
    std::cerr << "AUDIT looper controls complete\n";
    click("WAVEFORM");
    click("CONTROLS","audit-07-controls");
    for(const auto& label:{"VOICE / TONE","FX / SPACE","MASTER / PLAY","SYNTH / MOTION"})
        if(auto* tab=findTextButton(*editor,label)){tab->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(50);audit.add("PASS controls page: "+juce::String(label));}
        else audit.add("FAIL controls page missing: "+juce::String(label));
    click("WAVEFORM");
    click("MAIN","audit-08-main");
    auto auditVisibleKnobs=[&](const juce::String& page,int expected){
        juce::Array<juce::Slider*> sliders;collectSliders(*editor,sliders);
        int checked=0;
        for(auto* slider:sliders){
            if(!slider->isVisible()||slider->getWidth()<=0||slider->getHeight()<=0
               ||slider->getComponentID().isEmpty())continue;
            const auto id=slider->getComponentID();
            auto* parameter=processor.getAPVTS().getParameter(id);
            if(parameter==nullptr)continue;
            const auto before=parameter->getValue();
            const double lo=slider->getMinimum(),hi=slider->getMaximum();
            double next=lo+(hi-lo)*(before<0.55f?0.73:0.27);
            slider->setValue(next,juce::sendNotificationSync);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            const auto after=parameter->getValue();
            audit.add(std::abs(after-before)>1.0e-5f
                          ?"PASS knob changes parameter: "+page+" / "+id
                          :"FAIL knob inert: "+page+" / "+id);
            parameter->setValueNotifyingHost(before);
            ++checked;
        }
        audit.add(checked==expected?"PASS knob count: "+page+" = "+juce::String(checked)
                                   :"FAIL knob count: "+page+" expected "+juce::String(expected)+" got "+juce::String(checked));
    };
    auditVisibleKnobs("MAIN",7);
    click("FX","audit-09-fx");
    auditVisibleKnobs("FX",4);
    click("MASTER","audit-10-master");
    auditVisibleKnobs("MASTER",7);
    click("MAIN");

    const int beforeDemo=processor.getCurrentDemoIndex();
    click("Previous sound");click("Next sound");
    audit.add(processor.getCurrentDemoIndex()==beforeDemo?"PASS previous/next sound round-trip":"FAIL previous/next sound round-trip");
    click("Favourite");click("Favourite");
    click("Zoom out");click("Zoom in");
    click("DETECT");
    audit.add(processor.getSliceEngine().getNumSlices()>0?"PASS transient detection":"FAIL transient detection");
    click("MANUAL");
    audit.add(processor.getSliceEngine().getMode()==SliceEngine::Manual?"PASS manual slicing state":"FAIL manual slicing state");
    click("AUTO");
    audit.add(processor.getSliceEngine().getMode()==SliceEngine::Transient?"PASS auto slicing state":"FAIL auto slicing state");
    click("CLEAR");
    audit.add(processor.getSliceEngine().getNumSlices()==1?"PASS clear slice markers":"FAIL clear slice markers");
    click("AUTO");

    if(auto* bypass=processor.getAPVTS().getRawParameterValue("fxBypass")){
        const bool before=bypass->load()>0.5f;click("Bypass creative effects");
        audit.add((bypass->load()>0.5f)!=before?"PASS FX power toggles state":"FAIL FX power state");
        click("Bypass creative effects");
    }
    const bool syncBefore=processor.getLooper().isTempoSync();click("SYNC");
    audit.add(processor.getLooper().isTempoSync()!=syncBefore?"PASS looper sync toggles state":"FAIL looper sync state");
    click("SYNC");click("Previous loop track");click("Next loop track");
    click("Play / stop recorded loops","audit-11-loop-power-empty");click("WAVEFORM");

    std::cerr << "AUDIT audio preview\n";
    if(click("Preview full sample")){
        juce::AudioBuffer<float> preview(2,512);juce::MidiBuffer noMidi;preview.clear();
        processor.processBlock(preview,noMidi);
        audit.add(preview.getMagnitude(0,preview.getNumSamples())>1.0e-5f?"PASS sample preview produces audio":"FAIL sample preview silent");
    }

    std::cerr << "AUDIT modal paths\n";
    openAndDismiss(findButton(*editor,"CHOP"),"Engine menu");
    openAndDismiss(findButton(*editor,"THEME"),"Theme menu");
    openAndDismiss(findButton(*editor,"Menu"),"Main menu");
    openAndDismiss(findButton(*editor,juce::String(processor.getSliceEngine().getNumSlices())+" SLICES"),"Slice page menu");
    openAndDismiss(findButtonByTooltip(*editor,"Set metronome"),"BPM editor");
    openAndDismiss(findButton(*editor,"SAVE*"),"Save preset dialog");

    std::cerr << "AUDIT inventory\n";
    juce::Array<juce::Button*> inventory;collectButtons(*editor,inventory);
    for(auto* button:inventory){
        const auto label=button->getName().isNotEmpty()?button->getName():"<icon/blank>";
        const auto b=button->getBounds();
        audit.add("BUTTON "+label+" visible="+juce::String((int)button->isVisible())
                  +" enabled="+juce::String((int)button->isEnabled())
                  +" bounds="+b.toString()+" tip="+button->getTooltip());
    }
    audit.add("MODAL PATH PRESENT: LOAD / DROP YOUR SAMPLE");
    audit.add("MODAL PATH PRESENT: ENGINE");
    audit.add("MODAL PATH PRESENT: THEME");
    audit.add("MODAL PATH PRESENT: SAVE");
    audit.add("MODAL PATH PRESENT: Menu");
    const auto auditFile=output.getChildFile("button-audit.txt");
    if(!auditFile.replaceWithText(audit.joinIntoString("\n")))return 5;
    for(const auto& line:audit)if(line.startsWith("FAIL")){std::cerr<<line<<"\n";return 6;}
    std::cout<<auditFile.getFullPathName()<<"\n";
    editor->removeFromDesktop();editor.reset();processor.releaseResources();return 0;
}

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KnobComponent.h"
#include "ThemeManager.h"
#include "AppleLookAndFeel.h"
#include <memory>
#include <vector>

/** Real, parameter-attached detail controls. The reference front panel stays
    uncluttered without throwing away synthesis, ADSR, granular or arp features. */
class DetailPanel final : public juce::Component {
    struct Item {
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::Label> caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choice;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggle;
        bool knob=false;
    };
    juce::AudioProcessorValueTreeState& state;
    AppleLookAndFeel look;
    juce::TextButton tabs[4];
    juce::Viewport viewport;
    juce::Component grid;
    std::vector<Item> items;
    int page=0;
public:
    explicit DetailPanel(juce::AudioProcessorValueTreeState& p,int initialPage=0):state(p) {
        setLookAndFeel(&look);setOpaque(true);
        const char* names[]={"VOICE / TONE","FX / SPACE","MASTER / PLAY","SYNTH / MOTION"};
        for(int i=0;i<4;++i){tabs[i].setButtonText(names[i]);tabs[i].onClick=[this,i]{showPage(i);};addAndMakeVisible(tabs[i]);}
        viewport.setViewedComponent(&grid,false);viewport.setScrollBarsShown(true,false);addAndMakeVisible(viewport);
        setSize(750,420);showPage(initialPage);
    }
    ~DetailPanel() override { viewport.setViewedComponent(nullptr,false);setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override {
        const auto& t=ThemeManager::active();g.fillAll(t.wellT);
        g.setColour(t.text);g.setFont(juce::Font(juce::FontOptions(18.0f)));
        g.drawText("SLYCE   /   SOUND DETAILS",16,10,getWidth()-32,28,juce::Justification::centredLeft);
        g.setColour(t.textSecondary);g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("Double-click a dial to reset. These controls are saved with the sound.",16,getHeight()-28,getWidth()-32,20,juce::Justification::centredLeft);
    }
    void resized() override {
        auto row=getLocalBounds().reduced(16).withTop(48).withHeight(28);
        const int w=row.getWidth()/4;
        for(auto& b:tabs)b.setBounds(row.removeFromLeft(w).reduced(2,0));
        viewport.setBounds(16,90,getWidth()-32,getHeight()-126);
        const int cols=6,cellW=juce::jmax(80,(viewport.getWidth()-16)/cols),cellH=110;
        grid.setSize(viewport.getWidth()-16,juce::jmax(viewport.getHeight(),((int)items.size()+cols-1)/cols*cellH));
        for(int i=0;i<(int)items.size();++i){auto& item=items[(size_t)i];juce::Rectangle<int> cell((i%cols)*cellW,(i/cols)*cellH,cellW,cellH);
            if(item.knob)item.control->setBounds(cell.reduced(16,8));
            else {item.caption->setBounds(cell.reduced(6,0).removeFromTop(24));item.control->setBounds(cell.reduced(8,0).withTop(cell.getY()+32).withHeight(30));}
        }
    }
private:
    void add(const char* id) {
        auto* p=state.getParameter(id);if(!p)return;
        Item item;
        if(auto* choice=dynamic_cast<juce::AudioParameterChoice*>(p)) {
            auto c=std::make_unique<juce::ComboBox>();c->addItemList(choice->choices,1);
            item.choice=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state,id,*c);item.control=std::move(c);
        } else if(dynamic_cast<juce::AudioParameterBool*>(p)) {
            auto b=std::make_unique<juce::ToggleButton>(p->getName(32));
            item.toggle=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,id,*b);item.control=std::move(b);
        } else {
            auto k=std::make_unique<KnobComponent>(p->getName(32));
            item.slider=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state,id,k->getSlider());item.knob=true;item.control=std::move(k);
        }
        if(!item.knob){item.caption=std::make_unique<juce::Label>();item.caption->setText(p->getName(32),juce::dontSendNotification);item.caption->setColour(juce::Label::textColourId,ThemeManager::active().textSecondary);grid.addAndMakeVisible(*item.caption);}
        grid.addAndMakeVisible(*item.control);items.push_back(std::move(item));
    }
    void showPage(int next) {
        page=juce::jlimit(0,3,next);items.clear();
        for(int i=0;i<4;++i)tabs[i].setToggleState(i==page,juce::dontSendNotification);
        if(page==0)for(auto* id:{"engine","synthWave","synthOctave","attack","decay","sustain","release","pitch","formant","mix","reverse","playMode"})add(id);
        if(page==1)for(auto* id:{"reverb","delay","delayFeedback","delaySync","pingpong","drive","filterType","filterCutoff","filterReso","grainSize","grainMix","width"})add(id);
        if(page==2)for(auto* id:{"outputGain","macroHype","macroSpace","macroDirt","arpMode","arpRate","arpGate","arpOct","pumpAmt","pumpRate"})add(id);
        if(page==3)for(auto* id:{"synthUnison","synthSpread","synthDetune","synthSub","synthNoise","synthFM","synthVibrato","synthChorus","synthLfoRate","synthLfoAmt","synthGlide"})add(id);
        resized();repaint();
    }
};

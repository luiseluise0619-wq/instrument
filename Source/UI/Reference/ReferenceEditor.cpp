#include "ReferenceEditor.h"
#include "ReferenceLayout.h"
#include "ReferenceSkinData.h"
#include "../../PluginProcessor.h"
#include "../ThemeManager.h"
#include "../DetailPanel.h"
#include "../LooperPanel.h"
#include "../ChordBar.h"
#include "../UnlockPanel.h"
#include "../TypingKeymap.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>
#include <functional>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace ref = slyce::reference;
namespace {
struct Palette {
    bool dark=false;
    int scheme=0;
    juce::Colour text() const {
        if(scheme==1)return juce::Colour(0xfff0edff);
        if(scheme==2)return juce::Colour(0xffeffff8);
        if(scheme==3)return juce::Colour(0xff4b2444);
        if(scheme==4)return juce::Colour(0xfffff3df);
        if(scheme==5)return juce::Colour(0xff12375f);
        if(scheme==6)return juce::Colour(0xfff3ecff);
        if(scheme==7)return juce::Colour(0xfffff2d0);
        if(scheme==8)return juce::Colour(0xfff5f2ff);
        return juce::Colour(0xff1c355d);
    }
    juce::Colour secondary() const {
        if(scheme==1)return juce::Colour(0xffaaa2c6);
        if(scheme==2)return juce::Colour(0xff88cfc1);
        if(scheme==3)return juce::Colour(0xff8d587c);
        if(scheme==4)return juce::Colour(0xffd19b68);
        if(scheme==5)return juce::Colour(0xff4f7598);
        if(scheme==6)return juce::Colour(0xffbda8e8);
        if(scheme==7)return juce::Colour(0xffd0b57d);
        if(scheme==8)return juce::Colour(0xff8d91c7);
        return juce::Colour(0xff506b96);
    }
    juce::Colour accent() const {
        if(scheme==1)return juce::Colour(0xffaa70ff);
        if(scheme==2)return juce::Colour(0xff00a88a);
        if(scheme==3)return juce::Colour(0xffe64f9b);
        if(scheme==4)return juce::Colour(0xffe77900);
        if(scheme==5)return juce::Colour(0xff178cff);
        if(scheme==6)return juce::Colour(0xff8c65e8);
        if(scheme==7)return juce::Colour(0xffc7964b);
        if(scheme==8)return juce::Colour(0xffff3fd5);
        return juce::Colour(0xff167cff);
    }
    juce::Colour ice() const {
        if(scheme==1)return juce::Colour(0xff61cfff);
        if(scheme==2)return juce::Colour(0xff57d7c4);
        if(scheme==3)return juce::Colour(0xffff83c6);
        if(scheme==4)return juce::Colour(0xffffbd49);
        if(scheme==5)return juce::Colour(0xff5ed6ff);
        if(scheme==6)return juce::Colour(0xffc6a5ff);
        if(scheme==7)return juce::Colour(0xfff4cc82);
        if(scheme==8)return juce::Colour(0xff45f2ff);
        return juce::Colour(0xff4c9cff);
    }
    juce::Colour top() const {
        if(scheme==1)return juce::Colour(0xff252438);
        if(scheme==2)return juce::Colour(0xff18282a);
        if(scheme==3)return juce::Colour(0xfffffafd);
        if(scheme==4)return juce::Colour(0xff2a1a16);
        if(scheme==5)return juce::Colour(0xfff8fcff);
        if(scheme==6)return juce::Colour(0xff241a3b);
        if(scheme==7)return juce::Colour(0xff2b2418);
        if(scheme==8)return juce::Colour(0xff101126);
        return juce::Colour(0xfff6faff);
    }
    juce::Colour bottom() const {
        if(scheme==1)return juce::Colour(0xff0c0d17);
        if(scheme==2)return juce::Colour(0xff071415);
        if(scheme==3)return juce::Colour(0xfff6dfeb);
        if(scheme==4)return juce::Colour(0xff100907);
        if(scheme==5)return juce::Colour(0xffdceefc);
        if(scheme==6)return juce::Colour(0xff0c0718);
        if(scheme==7)return juce::Colour(0xff100d08);
        if(scheme==8)return juce::Colour(0xff060712);
        return juce::Colour(0xffe7eff7);
    }
    juce::Colour line() const {
        if(scheme==1)return juce::Colour(0xff51496d);
        if(scheme==2)return juce::Colour(0xff2d6964);
        if(scheme==3)return juce::Colour(0xffe1b9cf);
        if(scheme==4)return juce::Colour(0xff70452f);
        if(scheme==5)return juce::Colour(0xffa6cfe9);
        if(scheme==6)return juce::Colour(0xff604d88);
        if(scheme==7)return juce::Colour(0xff725d38);
        if(scheme==8)return juce::Colour(0xff464a78);
        return juce::Colour(0xffcbd9e9);
    }
    juce::Colour skinTint() const {
        if(scheme==1)return juce::Colour(0xff2b194c);
        if(scheme==2)return juce::Colour(0xff0b2d2b);
        if(scheme==3)return juce::Colour(0xffffe0f1);
        if(scheme==4)return juce::Colour(0xff321b15);
        if(scheme==5)return juce::Colour(0xffd6f1ff);
        if(scheme==6)return juce::Colour(0xff271542);
        if(scheme==7)return juce::Colour(0xff352611);
        if(scheme==8)return juce::Colour(0xff171a45);
        return juce::Colour(0xffe7f3ff);
    }
};
juce::Font font(float size,float spacing=0.0f) {
    // Use the installed system sans; no font binaries are redistributed.
    return juce::Font(juce::FontOptions(size)).withExtraKerningFactor(spacing);
}
float nativeDpiScale(const juce::Component& c) {
#if JUCE_WINDOWS
    if (auto handle = c.getWindowHandle()) {
        const auto dpi = ::GetDpiForWindow(static_cast<HWND>(handle));
        if (dpi > 0) return (float) dpi / 96.0f;
    }
    // The editor calls setSize/resized before its peer exists, so there is no
    // HWND yet on the first layout pass.  Use the system DPI as the startup
    // fallback and let GetDpiForWindow take over after the peer is created.
    const auto systemDpi = ::GetDpiForSystem();
    if (systemDpi > 0) return (float) systemDpi / 96.0f;
#endif
    return 1.0f;
}
void text(juce::Graphics& g,juce::String s,juce::Rectangle<float> r,
          juce::Colour c,float size=12.0f,juce::Justification j=juce::Justification::centredLeft,float spacing=0.0f) {
    g.setColour(c);g.setFont(font(size,spacing));g.drawText(s,r,j,true);
}
void card(juce::Graphics& g,juce::Rectangle<float> r,const Palette& p,bool selected=false,float radius=6.0f) {
    const float shadowAlpha=p.dark?.42f:(p.scheme==4?.13f:.08f);
    g.setColour(juce::Colours::black.withAlpha(shadowAlpha));g.fillRoundedRectangle(r.translated(0,p.dark?3.0f:2.0f),radius);
    juce::ColourGradient material(p.top(),r.getTopLeft(),p.bottom(),r.getBottomLeft(),false);
    if(p.scheme==1)material.addColour(.42,p.top().interpolatedWith(p.accent(),.10f));
    else if(p.scheme==2)material.addColour(.38,juce::Colours::white.withAlpha(.98f));
    else if(p.scheme==3)material.addColour(.48,p.top().interpolatedWith(p.ice(),.07f));
    else if(p.scheme==4)material.addColour(.62,p.bottom().interpolatedWith(p.accent(),.07f));
    else if(p.scheme==5)material.addColour(.32,juce::Colours::white.interpolatedWith(p.ice(),.035f));
    else if(p.scheme==6)material.addColour(.50,p.top().interpolatedWith(p.ice(),.08f));
    else if(p.scheme==7)material.addColour(.56,p.bottom().interpolatedWith(p.ice(),.08f));
    g.setGradientFill(material);
    g.fillRoundedRectangle(r,radius);
    if(p.scheme==3){juce::ColourGradient pearl(juce::Colours::white.withAlpha(.16f),r.getTopLeft(),p.ice().withAlpha(.035f),r.getBottomRight(),false);g.setGradientFill(pearl);g.fillRoundedRectangle(r.reduced(1),radius-1);}
    if(p.scheme==4){g.setColour(p.accent().withAlpha(.045f));for(float y=r.getY()+5;y<r.getBottom()-3;y+=4)g.drawHorizontalLine((int)y,r.getX()+4,r.getRight()-4);}
    if(p.scheme==5){g.setColour(juce::Colours::white.withAlpha(.48f));g.drawHorizontalLine((int)r.getY()+2,r.getX()+radius,r.getRight()-radius);g.setColour(p.ice().withAlpha(.08f));g.fillRoundedRectangle(r.reduced(2),radius-2);}
    if(p.scheme==6){juce::ColourGradient haze(p.ice().withAlpha(.045f),r.getTopRight(),juce::Colours::white.withAlpha(.18f),r.getBottomLeft(),false);g.setGradientFill(haze);g.fillRoundedRectangle(r.reduced(1),radius-1);}
    if(p.scheme==7){g.setColour(p.accent().withAlpha(.035f));for(float y=r.getY()+4;y<r.getBottom()-3;y+=3)g.drawHorizontalLine((int)y,r.getX()+4,r.getRight()-4);g.setColour(juce::Colours::white.withAlpha(.38f));g.drawHorizontalLine((int)r.getY()+2,r.getX()+radius,r.getRight()-radius);}
    if(selected){g.setColour(p.accent().withAlpha(p.dark?.17f:.085f));g.fillRoundedRectangle(r,radius);}
    g.setColour(selected?p.accent():p.line());g.drawRoundedRectangle(r.reduced(0.6f),radius,selected?1.5f:0.9f);
    if(p.dark){g.setColour(p.ice().withAlpha(selected?.30f:.08f));g.drawRoundedRectangle(r.reduced(1.7f),radius-1,0.7f);}
    g.setColour(juce::Colours::white.withAlpha(p.dark?0.13f:((p.scheme==4||p.scheme==7)?.62f:.9f)));
    g.drawHorizontalLine((int)r.getY()+1,r.getX()+radius,r.getRight()-radius);
}
void chevron(juce::Graphics& g,juce::Rectangle<float> r,bool right,juce::Colour c) {
    auto center=r.getCentre();juce::Path path;
    const float dx=right?2.8f:-2.8f;
    path.startNewSubPath(center.x-dx,center.y-5);path.lineTo(center.x+dx,center.y);path.lineTo(center.x-dx,center.y+5);
    g.setColour(c);g.strokePath(path,juce::PathStrokeType(1.35f));
}
void powerIcon(juce::Graphics& g,juce::Rectangle<float> r,juce::Colour c) {
    const auto a=r.withSizeKeepingCentre(15.0f,15.0f);const auto center=a.getCentre();juce::Path arc;
    arc.addCentredArc(center.x,center.y,6.4f,6.4f,0,0.55f,juce::MathConstants<float>::twoPi-0.55f,true);
    g.setColour(c);g.strokePath(arc,juce::PathStrokeType(1.5f));g.drawLine(center.x,center.y-8,center.x,center.y-1,1.5f);
}
void heartIcon(juce::Graphics& g,juce::Rectangle<float> r,juce::Colour c,bool filled) {
    const auto a=r.withSizeKeepingCentre(16.0f,16.0f);const float x=a.getX(),y=a.getY();juce::Path h;
    h.startNewSubPath(x+8,y+14);h.cubicTo(x-7,y+4,x+3,y-2,x+8,y+3);
    h.cubicTo(x+13,y-2,x+23,y+4,x+8,y+14);h.closeSubPath();g.setColour(c);
    if(filled)g.fillPath(h);else g.strokePath(h,juce::PathStrokeType(1.25f));
}
void smallIcon(juce::Graphics& g,int kind,juce::Rectangle<float> r,juce::Colour c) {
    auto q=r.withSizeKeepingCentre(12.0f,12.0f);const auto center=q.getCentre();g.setColour(c);
    if(kind==1){heartIcon(g,r,c,false);return;}
    if(kind==2){g.drawEllipse(q,1);g.drawLine(center.x,center.y,center.x,center.y-4,1);g.drawLine(center.x,center.y,center.x+3,center.y+1,1);return;}
    if(kind==3){for(int i=0;i<5;++i){float h=(i%2?9.0f:5.0f);if(i==2)h=13;g.drawLine(q.getX()+i*3,center.y-h/2,q.getX()+i*3,center.y+h/2,1.2f);}return;}
    if(kind==4){g.drawRoundedRectangle(q,2,1);g.drawLine(q.getX()+2,q.getY()+3,q.getRight()-2,q.getY()+3,1);return;}
    if(kind==5){for(int i=0;i<3;++i){g.fillEllipse(q.getX(),q.getY()+i*4,1.6f,1.6f);g.drawLine(q.getX()+4,q.getY()+i*4+1,q.getRight(),q.getY()+i*4+1,1);}return;}
    juce::Path n;n.startNewSubPath(q.getX()+4,q.getY()+9);n.lineTo(q.getX()+4,q.getY());n.lineTo(q.getRight()-1,q.getY()-1);n.lineTo(q.getRight()-1,q.getY()+7);
    g.strokePath(n,juce::PathStrokeType(1));g.fillEllipse(q.getX(),q.getY()+7,5,4);g.fillEllipse(q.getRight()-5,q.getY()+5,5,4);
}
class RefLook final : public juce::LookAndFeel_V4 {
    Palette& p;
public:
    explicit RefLook(Palette& a):p(a){}
    juce::Font getComboBoxFont(juce::ComboBox&) override {return font(12);}
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox&) override {
        card(g,{1,1,(float)w-2,(float)h-3},p);const float x=(float)w-14,y=(float)h/2;juce::Path path;
        path.startNewSubPath(x-3,y-2);path.lineTo(x,y+1);path.lineTo(x+3,y-2);g.setColour(p.text());g.strokePath(path,juce::PathStrokeType(1));
    }
    void positionComboBoxText(juce::ComboBox& b,juce::Label& l) override {
        l.setBounds(10,1,b.getWidth()-28,b.getHeight()-2);l.setFont(font(12));l.setColour(juce::Label::textColourId,p.text());
    }
    void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&) override {}
};
class RefButton final : public juce::Button {
public:
    enum Style { Raised,Tab,Bare,Previous,Next,Heart,Power,Play,More,ZoomIn,ZoomOut,BarePrevious,BareNext };
    Palette& p;Style style;bool selected=false;
    RefButton(Palette& palette,juce::String name,Style s=Raised):Button(name),p(palette),style(s){setWantsKeyboardFocus(false);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
    void paintButton(juce::Graphics& g,bool hover,bool down) override {
        auto r=getLocalBounds().toFloat().reduced(0.8f);const auto c=selected?p.accent():p.text();
        if(style==Raised||style==Previous||style==Next||style==Heart||style==Play||style==More)card(g,r,p,selected);
        else if(style==Tab&&selected){g.setGradientFill(juce::ColourGradient(p.accent().withAlpha(p.dark?0.24f:0.13f),r.getTopLeft(),p.accent().withAlpha(p.dark?0.035f:0.015f),r.getBottomLeft(),false));g.fillRoundedRectangle(r,6);g.setColour(p.accent());g.fillRect(r.withHeight(p.dark?2.2f:1.5f).withY(r.getBottom()-2).reduced(12,0));}
        if(hover||down){g.setColour(p.accent().withAlpha(down?(p.dark?.24f:.14f):(p.dark?.11f:.05f)));g.fillRoundedRectangle(r,5);}
        if(selected&&p.dark&&(style==Raised||style==Play)){g.setColour(p.ice().withAlpha(.20f));g.drawRoundedRectangle(r.reduced(2),4,.7f);}
        if(style==Previous||style==Next||style==BarePrevious||style==BareNext){chevron(g,r,style==Next||style==BareNext,c);return;}
        if(style==Heart){heartIcon(g,r,c,selected);return;}
        if(style==Power){powerIcon(g,r,selected?p.accent():p.secondary());return;}
        if(style==More){g.setColour(c);for(int i=-1;i<=1;++i)g.fillEllipse(r.getCentreX()+i*5-1.3f,r.getCentreY()-1.3f,2.6f,2.6f);return;}
        if(style==Play){const auto q=r.withSizeKeepingCentre(18.0f,19.0f);juce::Path a;a.startNewSubPath(q.getX()+2,q.getY());a.lineTo(q.getRight(),q.getCentreY());a.lineTo(q.getX()+2,q.getBottom());a.closeSubPath();g.setColour(p.accent());g.fillPath(a);return;}
        if(style==ZoomIn||style==ZoomOut){auto q=r.withSizeKeepingCentre(12.0f,12.0f).translated(-2,-2);g.setColour(c);g.drawEllipse(q,1.2f);g.drawLine(q.getRight()-1,q.getBottom()-1,q.getRight()+4,q.getBottom()+4,1.2f);g.drawLine(q.getX()+3,q.getCentreY(),q.getRight()-3,q.getCentreY(),1);if(style==ZoomIn)g.drawLine(q.getCentreX(),q.getY()+3,q.getCentreX(),q.getBottom()-3,1);return;}
        ::text(g,getName(),r,c,style==Tab?12.0f:11.5f,juce::Justification::centred);
    }
};
class RefKnob final : public juce::Slider {
    Palette& p;juce::Image& light;juce::Image& dark;int index;
    bool accentEnabled=true;
public:
    RefKnob(Palette& a,juce::Image& l,juce::Image& d,int i):p(a),light(l),dark(d),index(i) {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
        setMouseDragSensitivity(180);setWantsKeyboardFocus(false);
    }
    void paint(juce::Graphics& g) override {
        auto r=getLocalBounds().toFloat();const auto center=r.getCentre();const float radius=juce::jmin(r.getWidth(),r.getHeight())*.46f;
        const float start=juce::MathConstants<float>::pi*1.25f,end=juce::MathConstants<float>::pi*2.75f;
        auto arc=[&](juce::Colour c,float thick,float stop){juce::Path path;path.addCentredArc(center.x,center.y,radius,radius,0,start,stop,true);g.setColour(c);g.strokePath(path,juce::PathStrokeType(thick,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));};
        const auto unlit=p.dark?juce::Colour(0xff39364b):(p.scheme==2?juce::Colour(0xffb4d8d1):(p.scheme==3?juce::Colour(0xffe4c4d6):(p.scheme==4?juce::Colour(0xffddc69f):(p.scheme==5?juce::Colour(0xffb6d6e7):(p.scheme==6?juce::Colour(0xffd2c7e7):(p.scheme==7?juce::Colour(0xffd3c6b0):juce::Colour(0xffcbd5e1)))))));
        arc(unlit,5,end);
        const float value=(float)valueToProportionOfLength(getValue()),angle=start+(end-start)*value;
        auto accent=accentEnabled?((p.dark && (index==1||index==3||index>=4))?p.ice():p.accent()):(p.dark?juce::Colour(0xff5b626f):juce::Colour(0xffaeb8c4));
        if(value>0.001f){arc(accent.withAlpha(accentEnabled?(p.dark?.25f:.15f):.10f),p.dark?11.0f:9.0f,angle);arc(accent,accentEnabled?(p.dark?4.4f:3.6f):2.4f,angle);}
        const float face=radius*1.60f;auto faceR=r.withSizeKeepingCentre(face,face);
        g.setColour(juce::Colours::black.withAlpha(p.dark?.35f:.14f));g.fillEllipse(faceR.translated(0,3).expanded(1));
        const auto& image=p.dark?dark:light;
        if(image.isValid())g.drawImage(image,faceR);else {g.setColour(p.top());g.fillEllipse(faceR);}
        if(p.scheme>=2){g.setColour(p.skinTint().withAlpha((p.scheme==4||p.scheme==7)?.20f:.13f));g.fillEllipse(faceR.reduced(1));}
        if(p.scheme==3){juce::ColourGradient pearl(juce::Colours::white.withAlpha(.30f),faceR.getTopLeft(),p.ice().withAlpha(.09f),faceR.getBottomRight(),false);g.setGradientFill(pearl);g.fillEllipse(faceR.reduced(2));}
        if(p.scheme==5){g.setColour(p.ice().withAlpha(.12f));g.fillEllipse(faceR.reduced(3));}
        if(p.scheme==6){juce::ColourGradient haze(juce::Colours::white.withAlpha(.22f),faceR.getTopLeft(),p.ice().withAlpha(.10f),faceR.getBottomRight(),false);g.setGradientFill(haze);g.fillEllipse(faceR.reduced(2));}
        if(p.scheme==7){g.setColour(p.accent().withAlpha(.08f));for(float y=faceR.getY()+4;y<faceR.getBottom()-3;y+=3)g.drawHorizontalLine((int)y,faceR.getX()+4,faceR.getRight()-4);}
        g.setColour(p.dark?p.ice().withAlpha(.50f):((p.scheme==4||p.scheme==7)?p.accent().withAlpha(.34f):juce::Colours::white.withAlpha(.85f)));g.drawEllipse(faceR,1.1f);
        const float inner=face*.27f,outer=face*.43f;
        g.setColour(p.dark?juce::Colour(0xfff5f4ff):juce::Colour(0xff34405a));
        g.drawLine(center.x+std::sin(angle)*inner,center.y-std::cos(angle)*inner,center.x+std::sin(angle)*outer,center.y-std::cos(angle)*outer,1.7f);
    }
    void setAccentEnabled(bool shouldBeEnabled){accentEnabled=shouldBeEnabled;repaint();}
};
class RefWheel final : public juce::Slider {
    Palette& p;
public:
    explicit RefWheel(Palette& a):p(a){setSliderStyle(juce::Slider::LinearVertical);setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);setWantsKeyboardFocus(false);}
    void paint(juce::Graphics& g) override {
        auto r=getLocalBounds().toFloat().reduced(3,1);g.setColour(p.dark?juce::Colour(0xff06090d):juce::Colour(0xffa1adba));g.fillRoundedRectangle(r,5);
        auto body=r.reduced(4,4);g.setGradientFill(juce::ColourGradient(p.dark?juce::Colour(0xff303740):juce::Colour(0xfff4f8fb),body.getTopLeft(),p.dark?juce::Colour(0xff0b0e13):juce::Colour(0xff8c99a7),body.getBottomLeft(),false));g.fillRoundedRectangle(body,3);
        for(float y=body.getY()+4;y<body.getBottom()-3;y+=2.4f){g.setColour(p.dark?juce::Colours::white.withAlpha(.09f):juce::Colours::white.withAlpha(.65f));g.drawLine(body.getX()+2,y,body.getRight()-2,y,.6f);}
        float y=body.getBottom()-4-(float)valueToProportionOfLength(getValue())*(body.getHeight()-8);g.setColour(p.ice());g.drawLine(body.getX()+1,y,body.getRight()-1,y,1.4f);
        g.setColour(p.line());g.drawRoundedRectangle(r,5,1);
    }
};
struct Peak {float lo=0,hi=0;};
std::vector<Peak> envelope(const juce::AudioBuffer<float>& sample,int first,int length,int bins) {
    std::vector<Peak> result((size_t)juce::jmax(1,bins));
    if(sample.getNumChannels()<1||sample.getNumSamples()<1||length<=0)return result;
    first=juce::jlimit(0,sample.getNumSamples()-1,first);length=juce::jmin(length,sample.getNumSamples()-first);
    for(int i=0;i<bins;++i){auto& v=result[(size_t)i];int from=first+(int)((int64_t)i*length/bins),to=juce::jmin(first+length,first+(int)((int64_t)(i+1)*length/bins)+1);
        for(int n=from;n<to;++n)for(int ch=0;ch<juce::jmin(2,sample.getNumChannels());++ch){float x=sample.getSample(ch,n);if(std::isfinite(x)){v.lo=juce::jmin(v.lo,x);v.hi=juce::jmax(v.hi,x);}}}
    return result;
}
void drawEnvelope(juce::Graphics& g,const std::vector<Peak>& v,juce::Rectangle<float> r,const Palette& p,double start=0,double span=1) {
    if(v.empty())return;const int pixels=juce::jmax(1,(int)r.getWidth());juce::Path shape;
    const float mid=r.getCentreY(),gain=r.getHeight()*.46f;shape.startNewSubPath(r.getX(),mid);
    for(int i=0;i<pixels;++i){int j=juce::jlimit(0,(int)v.size()-1,(int)((start+span*i/pixels)*v.size()));shape.lineTo(r.getX()+i,mid-juce::jlimit(0.0f,1.0f,v[(size_t)j].hi)*gain);}
    for(int i=pixels-1;i>=0;--i){int j=juce::jlimit(0,(int)v.size()-1,(int)((start+span*i/pixels)*v.size()));shape.lineTo(r.getX()+i,mid-juce::jlimit(-1.0f,0.0f,v[(size_t)j].lo)*gain);}
    shape.closeSubPath();
    juce::ColourGradient wave(p.ice().withAlpha(p.dark?.98f:.93f),r.getTopLeft(),p.accent().withAlpha(p.dark?.70f:.53f),r.getBottomLeft(),false);
    if(p.scheme==1)wave.addColour(.48,p.accent().withAlpha(.88f));
    else if(p.scheme==2)wave.addColour(.52,p.accent().withAlpha(.78f));
    else if(p.scheme==3)wave.addColour(.45,juce::Colour(0xffffb7dc).withAlpha(.88f));
    else if(p.scheme==4)wave.addColour(.58,juce::Colour(0xffff953d).withAlpha(.82f));
    else if(p.scheme==5)wave.addColour(.48,juce::Colour(0xff2fb8ff).withAlpha(.88f));
    else if(p.scheme==6)wave.addColour(.46,juce::Colour(0xffa884ff).withAlpha(.84f));
    else if(p.scheme==7)wave.addColour(.55,juce::Colour(0xffd9aa62).withAlpha(.84f));
    g.setGradientFill(wave);g.fillPath(shape);
    if(p.dark){g.setColour(p.ice().withAlpha(.22f));g.strokePath(shape,juce::PathStrokeType(.8f));}
}
}

class SlyceReferenceEditor::Surface final : public juce::Component,
    private juce::Timer,private juce::ChangeListener,public juce::FileDragAndDropTarget
{
public:
    VocalChopAudioProcessor& proc;
    Palette palette;
    RefLook look{palette};
    juce::Image chromeLight,chromeDark,knobLight,knobDark,themedChrome;
    juce::TooltipWindow tooltip{this,550};
    struct Sound {int kind=0,index=0;juce::String name,group;juce::String key()const{return juce::String(kind)+":"+juce::String(index);}};
    std::vector<Sound> catalog,visible;
    juce::StringArray favourites,recent;
    juce::String selectedCategory,query,selectedPresetName,selectedCatalogKey;
    int selectionEngine=-1,selectionInstrument=-1,selectionDemo=-1;
    int browserTab=0,rowOffset=0,cardPage=0,selectedLoop=0,workspaceTab=0,rightTab=0;
    juce::TextEditor search;
    juce::ComboBox snap,loopLength;
    juce::Slider sensitivity;
    RefWheel pitch{palette},mod{palette};
    std::vector<std::unique_ptr<RefButton>> buttons;
    std::array<std::unique_ptr<RefKnob>,7> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,7> knobAttachments;
    std::array<juce::String,7> knobCaptions;
    std::array<RefButton*,3> browserButtons{},rightButtons{};
    std::array<RefButton*,4> workspaceButtons{};
    RefButton *menuButton=nullptr,*heart=nullptr,*autoButton=nullptr,*manualButton=nullptr,*syncButton=nullptr,*fxButton=nullptr,*loopButton=nullptr,*countButton=nullptr,*importButton=nullptr;
    RefButton *engineButton=nullptr,*themeButton=nullptr,*saveButton=nullptr;
    std::vector<juce::Component*> workspaceControls;
    std::unique_ptr<juce::Component> overlay;
    std::unique_ptr<RefButton> closeOverlay;
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<juce::AudioBuffer<float>> sample;
    std::vector<Peak> fullEnvelope;
    std::array<std::vector<Peak>,8> padEnvelopes;
    std::vector<SlicePoint> slices;
    std::set<int> typedNotes;
    int ticks=0,lastSelected=-2,lastEngine=-1;
    std::array<float,512> loopPeaks{};
    bool fxEnabled=true,dirty=false;

    class Browser final : public juce::Component {
        Surface& s;
    public:
        explicit Browser(Surface& x):s(x){setMouseCursor(juce::MouseCursor::PointingHandCursor);}
        void paint(juce::Graphics& g) override {s.paintBrowser(g);}
        void mouseDown(const juce::MouseEvent& e) override {
            if(e.position.x>getWidth()-10){draggingScroll=true;setScroll(e.position.y);}
            else s.clickBrowser(e.position.y);
        }
        void mouseDrag(const juce::MouseEvent& e) override {if(draggingScroll)setScroll(e.position.y);}
        void mouseUp(const juce::MouseEvent&) override {draggingScroll=false;}
        void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w) override {
            int total=(int)s.visible.size();
            const bool typeGroups=s.home()&&s.browserTab==1;
            if(typeGroups){juce::StringArray groups;for(auto& a:s.catalog)if(a.kind==1||a.kind==4)groups.addIfNotAlreadyThere(a.group);total=groups.size();}
            const int pageSize=s.browserTab==2?11:(typeGroups?16:15);
            s.rowOffset=juce::jlimit(0,juce::jmax(0,total-pageSize),s.rowOffset+(w.deltaY<0?3:-3));repaint();
        }
    private:
        bool draggingScroll=false;
        void setScroll(float y){
            int total=(int)s.visible.size();const bool groups=s.home()&&s.browserTab==1;
            if(groups){juce::StringArray a;for(auto& x:s.catalog)if(x.kind==1||x.kind==4)a.addIfNotAlreadyThere(x.group);total=a.size();}
            const int page=s.browserTab==2?11:(groups?16:15),maxOffset=juce::jmax(0,total-page);
            const float trackTop=s.browserTab==2?132.0f:0.0f;
            const float fraction=juce::jlimit(0.0f,1.0f,(y-trackTop)/juce::jmax(1.0f,(float)getHeight()-trackTop));
            s.rowOffset=juce::jlimit(0,maxOffset,juce::roundToInt(fraction*maxOffset));repaint();
        }
    } browser{*this};
    class Wave final : public juce::Component {
        Surface& s;int dragging=-1;double zoomStart=0,zoomSpan=1;
        double fraction(float x)const {return juce::jlimit(0.0,1.0,zoomStart+zoomSpan*(x-7)/juce::jmax(1,getWidth()-14));}
        float xAt(double frac)const {return 7+(float)((frac-zoomStart)/zoomSpan)*(getWidth()-14);}
    public:
        explicit Wave(Surface& a):s(a){setMouseCursor(juce::MouseCursor::PointingHandCursor);}
        void resetZoom(){zoomStart=0;zoomSpan=1;repaint();}
        void zoom(bool in){const double centre=zoomStart+zoomSpan*.5;zoomSpan=juce::jlimit(1.0/16,1.0,zoomSpan*(in?.5:2));zoomStart=juce::jlimit(0.0,1.0-zoomSpan,centre-zoomSpan*.5);repaint();}
        int markerAt(float x)const {if(!s.sample)return -1;for(int i=1;i<(int)s.slices.size();++i)if(std::abs(xAt((double)s.slices[(size_t)i].startSample/s.sample->getNumSamples())-x)<7)return i;return -1;}
        void paint(juce::Graphics& g) override {
            auto r=getLocalBounds().toFloat();auto waveArea=r.reduced(7,0).withTrimmedTop(31).withTrimmedBottom(3);
            g.setColour(s.palette.line().withAlpha(.7f));g.drawHorizontalLine((int)waveArea.getY(),waveArea.getX(),waveArea.getRight());g.drawHorizontalLine((int)waveArea.getBottom(),waveArea.getX(),waveArea.getRight());
            if(s.proc.isSynthMode()&&!s.proc.isMelodyMode()){
                const auto& ring=s.proc.getScopeRing();int end=s.proc.getScopeWritePos();juce::Path p;
                for(int i=0;i<800;++i){float v=ring[(size_t)((end-800+i)&2047)].load(std::memory_order_relaxed);float x=waveArea.getX()+waveArea.getWidth()*i/799.0f,y=waveArea.getCentreY()-v*waveArea.getHeight()*.45f;if(i==0)p.startNewSubPath(x,y);else p.lineTo(x,y);}
                g.setColour(s.palette.ice());g.strokePath(p,juce::PathStrokeType(1));text(g,"LIVE INSTRUMENT",r.withHeight(23),s.palette.secondary(),10,juce::Justification::centred,.12f);return;
            }
            if(!s.sample){text(g,"DROP AUDIO HERE  |  OR CLICK LOAD / DROP YOUR SAMPLE",waveArea,s.palette.secondary(),13,juce::Justification::centred);return;}
            const int n=s.sample->getNumSamples();const int sel=s.proc.getSelectedSlice();
            if(juce::isPositiveAndBelow(sel,(int)s.slices.size())){
                const auto& a=s.slices[(size_t)sel];float x=xAt((double)a.startSample/n),to=xAt((double)(a.startSample+a.lengthSamples)/n);
                auto q=juce::Rectangle<float>(x,waveArea.getY(),to-x,waveArea.getHeight()).getIntersection(waveArea);
                g.setColour(s.palette.accent().withAlpha(.10f));g.fillRoundedRectangle(q,3);g.setColour(s.palette.accent());g.drawRoundedRectangle(q.reduced(.4f),3,.9f);
            }
            drawEnvelope(g,s.fullEnvelope,waveArea,s.palette,zoomStart,zoomSpan);
            const int step=juce::jmax(1,(int)s.slices.size()/24);
            for(int i=0;i<(int)s.slices.size();++i){float x=xAt((double)s.slices[(size_t)i].startSample/n);if(x<waveArea.getX()||x>waveArea.getRight())continue;
                g.setColour(s.palette.accent().withAlpha(.8f));g.drawVerticalLine((int)x,waveArea.getY(),waveArea.getBottom());g.fillEllipse(x-3.3f,waveArea.getY()-9,6.6f,6.6f);
                if(i%step==0)text(g,juce::String(i+1),{x-13,0,26,20},s.palette.text(),11,juce::Justification::centred);
            }
        }
        void mouseDown(const juce::MouseEvent& e) override {
            dragging=markerAt(e.position.x);if(dragging>=0)return;
            if(!s.sample)return;int at=(int)(fraction(e.position.x)*s.sample->getNumSamples());
            for(int i=0;i<(int)s.slices.size();++i){const auto& a=s.slices[(size_t)i];if(at>=a.startSample&&at<a.startSample+a.lengthSamples){s.auditionSlice(i);break;}}
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            if(dragging<1||!s.sample)return;const int n=s.sample->getNumSamples();double f=fraction(e.position.x);
            int div=s.snap.getSelectedId();if(div>1){const int d=1<<(div+1);f=std::round(f*d)/d;}
            std::vector<int> points;for(const auto& a:s.slices)points.push_back(a.startSample);
            int low=points[(size_t)dragging-1]+8,high=dragging+1<(int)points.size()?points[(size_t)dragging+1]-8:n-8;if(high<low)return;
            points[(size_t)dragging]=juce::jlimit(low,high,(int)(f*n));s.proc.getSliceEngine().sliceByManual(points);s.refreshSample();
        }
        void mouseUp(const juce::MouseEvent&) override{dragging=-1;}
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            if(!s.sample)return;int hit=markerAt(e.position.x);std::vector<int> points;for(auto& a:s.slices)points.push_back(a.startSample);
            if(hit>0)points.erase(points.begin()+hit);else points.push_back((int)(fraction(e.position.x)*s.sample->getNumSamples()));
            std::sort(points.begin(),points.end());s.proc.getSliceEngine().sliceByManual(points);s.refreshSample();
        }
        void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w) override {zoomStart=juce::jlimit(0.0,1.0-zoomSpan,zoomStart-w.deltaY*zoomSpan*.4);repaint();}
    } wave{*this};
    class Pad final : public juce::Component {
        Surface& s;int index;
    public:
        Pad(Surface& a,int i):s(a),index(i){setMouseCursor(juce::MouseCursor::PointingHandCursor);}
        void paint(juce::Graphics& g) override {
            const int actual=s.cardPage*8+index;auto r=getLocalBounds().toFloat().reduced(1);bool active=actual==s.proc.getSelectedSlice();card(g,r,s.palette,active,7);
            text(g,juce::String(actual+1),r.reduced(13,7).withHeight(23),s.palette.text(),12);
            text(g,"...",r.withX(r.getRight()-31).withWidth(23).withHeight(25),s.palette.secondary(),14,juce::Justification::centred);
            if(s.proc.isVocalKitMode()&&actual<VocalKitEngine::kNumSlots){
                drawEnvelope(g,s.padEnvelopes[(size_t)index],r.reduced(24,25).withTrimmedBottom(5),s.palette);
                auto st=s.proc.getVocalKit().getSlotSettings(actual);auto name=st.name.isNotEmpty()?st.name:"EMPTY - DROP SAMPLE";if(std::abs(st.pitchSemitones)>.01f)name+=(st.pitchSemitones>0?"  +":"  ")+juce::String(st.pitchSemitones,0)+" st";
                text(g,name,r.reduced(13,8).withTop(r.getBottom()-27),st.missing?juce::Colours::red:(active?s.palette.accent():s.palette.secondary()),10.5f,juce::Justification::centredRight);
            } else if(actual<(int)s.slices.size()){
                drawEnvelope(g,s.padEnvelopes[(size_t)index],r.reduced(24,25).withTrimmedBottom(5),s.palette);
                static constexpr int whites[]={0,2,4,5,7,9,11};int midi=48+12*(actual/7)+whites[actual%7];
                auto name=juce::MidiMessage::getMidiNoteName(midi,true,true,4);float transpose=s.proc.getSliceTranspose(actual);
                if(std::abs(transpose)>.01f)name+=(transpose>0?" +":" ")+juce::String(transpose,0);
                text(g,name,r.reduced(13,8).withTop(r.getBottom()-27),active?s.palette.accent():s.palette.secondary(),12,juce::Justification::centredRight);
            } else text(g,"NO SLICE",r,s.palette.secondary().withAlpha(.5f),10,juce::Justification::centred,.08f);
        }
        void mouseDown(const juce::MouseEvent& e) override {int actual=s.cardPage*8+index;if(s.proc.isVocalKitMode()){if(actual>=VocalKitEngine::kNumSlots)return;if(e.mods.isPopupMenu()||(e.position.x>getWidth()-33&&e.position.y<32))s.kitSlotMenu(actual,this);else{s.proc.auditionVocalKitSlot(actual);s.refreshSample();}return;}if(actual>=(int)s.slices.size())return;if(e.mods.isPopupMenu()||(e.position.x>getWidth()-33&&e.position.y<32))s.sliceMenu(actual,this);else s.auditionSlice(actual);}
        void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w) override {s.changeCardPage(w.deltaY<0?1:-1);}
    };
    std::array<std::unique_ptr<Pad>,8> pads;
    class Keybed final : public juce::Component {
        Surface& s;int held=-1;std::set<int> active;
        static bool black(int k){int n=k%12;return n==1||n==3||n==6||n==8||n==10;}
        juce::Rectangle<float> rect(int k)const {int whiteBefore=0;for(int i=0;i<k;++i)if(!black(i))++whiteBefore;float width=getWidth()/35.0f;
            if(black(k))return {whiteBefore*width-width*.29f,1,width*.58f,getHeight()*.60f};return {whiteBefore*width,1,width,(float)getHeight()-2};}
        int at(juce::Point<float> point)const {for(int k=0;k<60;++k)if(black(k)&&rect(k).contains(point))return k;for(int k=0;k<60;++k)if(!black(k)&&rect(k).contains(point))return k;return -1;}
        void down(int k,float y){if(k==held)return;release();if(k<0)return;held=k;s.noteOn(k-24,juce::jlimit(.3f,1.0f,.3f+.7f*y/getHeight()));}
    public:
        explicit Keybed(Surface& a):s(a){setMouseCursor(juce::MouseCursor::PointingHandCursor);}
        ~Keybed()override{release();}
        void release(){if(held>=0){s.noteOff(held-24);held=-1;}}
        void highlight(int offset,bool on){if(on)active.insert(offset+24);else active.erase(offset+24);repaint();}
        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colour(0xff12151a));g.fillRoundedRectangle(getLocalBounds().toFloat(),5);
            for(int layer=0;layer<2;++layer)for(int k=0;k<60;++k){if(black(k)!=(layer==1))continue;auto r=rect(k).reduced(black(k)?0.2f:.65f,0);bool on=active.count(k)>0;
                juce::Colour top,bottom;if(layer){top=juce::Colour(0xff41454b);bottom=juce::Colour(0xff111418);}else {top=juce::Colour(s.palette.dark?0xffb3b2b9:0xffd5d8df);bottom=juce::Colour(s.palette.dark?0xffe5e3e8:0xfffcfdff);}
                if(on){top=top.interpolatedWith(s.palette.accent(),.72f);bottom=bottom.interpolatedWith(s.palette.accent(),.5f);}
                g.setGradientFill(juce::ColourGradient(top,r.getTopLeft(),bottom,r.getBottomLeft(),false));g.fillRoundedRectangle(r,2.4f);
                g.setColour(juce::Colour(0xff5e626b));g.drawRoundedRectangle(r,2.4f,.75f);
                if(layer){g.setColour(juce::Colours::white.withAlpha(.15f));g.drawLine(r.getX()+2,r.getY()+3,r.getX()+2,r.getBottom()-5,.8f);g.setColour(juce::Colours::white.withAlpha(.12f));g.drawLine(r.getX()+2,r.getBottom()-4,r.getRight()-2,r.getBottom()-4,1);}
                else if(k%12==0){text(g,"C"+juce::String(k/12+1),r.withY(r.getBottom()-22).withHeight(20),juce::Colour(0xff37415b),10,juce::Justification::centred);}
                for(int i=0;i<slyce::keymap::numKeys;++i){
                    const int mapped=24+slyce::keymap::semitoneFor(i,s.proc.isChopMode());
                    if(mapped!=k)continue;
                    const auto label=juce::String::charToString(slyce::keymap::keys[i]).toUpperCase();
                    text(g,label,r.withY(r.getBottom()-(black(k)?17.0f:39.0f)).withHeight(15),
                         black(k)?juce::Colours::white.withAlpha(.72f):juce::Colour(0xff65708a),
                         8.5f,juce::Justification::centred);
                    break;
                }
            }
        }
        void mouseDown(const juce::MouseEvent& e)override{down(at(e.position),e.position.y);}
        void mouseDrag(const juce::MouseEvent& e)override{down(at(e.position),e.position.y);}
        void mouseUp(const juce::MouseEvent&)override{release();}
    } keybed{*this};

    class KitEditor final : public juce::Component, private juce::Timer {
        Surface& s; int slot=0,dragHandle=0; bool updating=false; float viewStart=0.0f,viewSpan=1.0f;
        juce::TextEditor name;
        juce::TextButton replace{"REPLACE SAMPLE"},preview{"PREVIEW"},reset{"RESET SLOT"};
        juce::TextButton pitchDown{"-1"},pitchUp{"+1"},pitchReset{"RESET"},zoomOut{"WAVE -"},zoomIn{"WAVE +"};
        juce::ToggleButton midiFollow{"MIDI FOLLOW"},reverse{"REVERSE"},oneShot{"ONE-SHOT"},loop{"LOOP"};
        juce::ComboBox choke;
        std::array<juce::Slider,15> sliders;
        std::array<const char*,15> labels{{"GAIN","PAN","SLOT PITCH","FINE","SPEED","FORMANT COLOUR","ATTACK","DECAY","SUSTAIN","RELEASE","FILTER","DELAY SEND","REVERB SEND","VELOCITY","LOOP XFADE"}};
        std::unique_ptr<juce::FileChooser> chooser;
        std::shared_ptr<const juce::AudioBuffer<float>> sample;
        std::vector<Peak> peaks;
        VocalKitEngine::SlotSettings settings;
        juce::Rectangle<float> waveRect()const{return {18.0f,57.0f,(float)getWidth()-36.0f,150.0f};}
        float screenX(float f,const juce::Rectangle<float>& r)const{return r.getX()+r.getWidth()*(f-viewStart)/viewSpan;}
        float sampleFraction(float x,const juce::Rectangle<float>& r)const{return juce::jlimit(0.0f,1.0f,viewStart+viewSpan*(x-r.getX())/r.getWidth());}
        void refreshPeaks(){if(!sample){peaks.clear();return;}int a=(int)(viewStart*sample->getNumSamples()),b=(int)((viewStart+viewSpan)*sample->getNumSamples());peaks=envelope(*sample,a,juce::jmax(1,b-a),1024);}
        void zoom(bool in){float centre=juce::jlimit(0.0f,1.0f,(settings.start+settings.end)*.5f);viewSpan=in?juce::jmax(.08f,viewSpan*.5f):juce::jmin(1.0f,viewSpan*2.0f);viewStart=juce::jlimit(0.0f,1.0f-viewSpan,centre-viewSpan*.5f);refreshPeaks();repaint();}
        void apply(){if(updating)return;settings.name=name.getText().trim();settings.gainDb=(float)sliders[0].getValue();settings.pan=(float)sliders[1].getValue();settings.pitchSemitones=(float)sliders[2].getValue();settings.fineCents=(float)sliders[3].getValue();settings.timeStretch=(float)sliders[4].getValue();settings.formantSemitones=(float)sliders[5].getValue();settings.attackMs=(float)sliders[6].getValue();settings.decayMs=(float)sliders[7].getValue();settings.sustain=(float)sliders[8].getValue();settings.releaseMs=(float)sliders[9].getValue();settings.filterHz=(float)sliders[10].getValue();settings.delaySend=(float)sliders[11].getValue();settings.reverbSend=(float)sliders[12].getValue();settings.velocityAmount=(float)sliders[13].getValue();settings.loopCrossfadeMs=(float)sliders[14].getValue();settings.reverse=reverse.getToggleState();settings.oneShot=oneShot.getToggleState();settings.loop=loop.getToggleState();settings.chokeGroup=choke.getSelectedId()-1;s.proc.getVocalKit().setSlotSettings(slot,settings);s.refreshSample();repaint();}
        void load(int target){slot=juce::jlimit(0,VocalKitEngine::kNumSlots-1,target);updating=true;settings=s.proc.getVocalKit().getSlotSettings(slot);sample=s.proc.getVocalKit().getSlotSample(slot);viewStart=0;viewSpan=1;refreshPeaks();name.setText(settings.name,false);const double values[]={settings.gainDb,settings.pan,settings.pitchSemitones,settings.fineCents,settings.timeStretch,settings.formantSemitones,settings.attackMs,settings.decayMs,settings.sustain,settings.releaseMs,settings.filterHz,settings.delaySend,settings.reverbSend,settings.velocityAmount,settings.loopCrossfadeMs};for(int i=0;i<15;++i)sliders[(size_t)i].setValue(values[i],juce::dontSendNotification);reverse.setToggleState(settings.reverse,juce::dontSendNotification);oneShot.setToggleState(settings.oneShot,juce::dontSendNotification);loop.setToggleState(settings.loop,juce::dontSendNotification);choke.setSelectedId(settings.chokeGroup+1,juce::dontSendNotification);updating=false;repaint();}
        void chooseFile(){chooser=std::make_unique<juce::FileChooser>("Load and map one sample",juce::File(),"*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");auto safe=juce::Component::SafePointer<KitEditor>(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& c){if(!safe)return;auto file=c.getResult();if(!file.existsAsFile())return;juce::String error;if(!safe->s.proc.loadVocalKitSlot(safe->slot,file,error)){safe->s.notify("Unable to load sample",error);return;}safe->s.openWorkspace(0);safe->s.refreshSample();safe->s.updateContextControls();});}
        void timerCallback()override{if(!midiFollow.getToggleState())return;int next=s.proc.getSelectedSlice();if(next!=slot&&juce::isPositiveAndBelow(next,VocalKitEngine::kNumSlots))load(next);}
    public:
        KitEditor(Surface& owner,int initial):s(owner){setWantsKeyboardFocus(true);addAndMakeVisible(name);name.setMultiLine(false);name.onFocusLost=[this]{apply();};for(auto* b:{&replace,&preview,&reset})addAndMakeVisible(*b);replace.onClick=[this]{chooseFile();};preview.onClick=[this]{s.proc.auditionVocalKitSlot(slot);};reset.onClick=[this]{auto keep=settings;VocalKitEngine::SlotSettings fresh;fresh.name=keep.name;fresh.sourcePath=keep.sourcePath;fresh.midiNote=keep.midiNote;s.proc.getVocalKit().setSlotSettings(slot,fresh);load(slot);s.refreshSample();};for(auto* b:{&pitchDown,&pitchUp,&pitchReset,&zoomOut,&zoomIn})addAndMakeVisible(*b);zoomOut.onClick=[this]{zoom(false);};zoomIn.onClick=[this]{zoom(true);};auto nudge=[this](float delta){sliders[2].setValue(juce::jlimit(-12.0,12.0,sliders[2].getValue()+delta),juce::sendNotificationSync);};pitchDown.onClick=[nudge]{nudge(-1);};pitchUp.onClick=[nudge]{nudge(1);};pitchReset.onClick=[this]{sliders[2].setValue(0,juce::sendNotificationSync);};for(auto* b:{&midiFollow,&reverse,&oneShot,&loop}){addAndMakeVisible(*b);b->onClick=[this]{apply();};}midiFollow.setToggleState(true,juce::dontSendNotification);addAndMakeVisible(choke);choke.addItem("No Choke",1);for(int i=1;i<=8;++i)choke.addItem("Choke "+juce::String(i),i+1);choke.onChange=[this]{apply();};for(auto& x:sliders){addAndMakeVisible(x);x.setSliderStyle(juce::Slider::LinearHorizontal);x.setTextBoxStyle(juce::Slider::TextBoxRight,false,62,20);x.onValueChange=[this]{apply();};}sliders[0].setRange(-24,12,.1);sliders[1].setRange(-1,1,.01);sliders[2].setRange(-12,12,1);sliders[2].setTextValueSuffix(" st");sliders[3].setRange(-100,100,1);sliders[3].setTextValueSuffix(" cents");sliders[4].setRange(.25,4,.01);sliders[5].setRange(-12,12,.1);sliders[6].setRange(0,1000,1);sliders[7].setRange(0,4000,1);sliders[8].setRange(0,1,.01);sliders[9].setRange(1,4000,1);sliders[10].setRange(40,20000,1);sliders[10].setSkewFactorFromMidPoint(1000);for(int i=11;i<=13;++i)sliders[(size_t)i].setRange(0,1,.01);sliders[14].setRange(0,100,.5);sliders[14].setTextValueSuffix(" ms");load(initial);startTimerHz(20);}
        void paint(juce::Graphics& g)override
        {
            g.fillAll(s.palette.bottom());card(g,getLocalBounds().toFloat().reduced(1),s.palette,false,8);
            text(g,"SAMPLE EDIT  /  "+juce::MidiMessage::getMidiNoteName(VocalKitEngine::kRootNote+slot,true,true,4),{18,8,470,34},s.palette.text(),14);
            auto r=waveRect();g.setColour(s.palette.line());g.drawRoundedRectangle(r,5,1);drawEnvelope(g,peaks,r.reduced(4),s.palette);
            float a=screenX(settings.start,r),b=screenX(settings.end,r),la=screenX(settings.loopStart,r),lb=screenX(settings.loopEnd,r);
            g.setColour(s.palette.accent().withAlpha(.12f));g.fillRect(juce::Rectangle<float>(juce::jmax(r.getX(),a),r.getY(),juce::jmax(0.0f,juce::jmin(r.getRight(),b)-juce::jmax(r.getX(),a)),r.getHeight()));
            g.setColour(s.palette.accent());if(r.getX()<=a&&a<=r.getRight())g.drawVerticalLine((int)a,r.getY(),r.getBottom());if(r.getX()<=b&&b<=r.getRight())g.drawVerticalLine((int)b,r.getY(),r.getBottom());
            if(settings.loop){const float dash[]={4,3};g.setColour(s.palette.ice());if(r.getX()<=la&&la<=r.getRight())g.drawDashedLine({la,r.getY(),la,r.getBottom()},dash,2,2,0);if(r.getX()<=lb&&lb<=r.getRight())g.drawDashedLine({lb,r.getY(),lb,r.getBottom()},dash,2,2,0);}
            float play=s.proc.getVocalKit().getSlotPlayhead(slot);if(play>=viewStart&&play<=viewStart+viewSpan){g.setColour(juce::Colours::white.withAlpha(.9f));g.drawVerticalLine((int)screenX(play,r),r.getY(),r.getBottom());}
            if(!sample)text(g,settings.missing?"MISSING: "+settings.sourcePath:"EMPTY - LOAD A SAMPLE",r,s.palette.secondary(),13,juce::Justification::centred);
            for(int i=0;i<15;++i){const float y=244.0f+(i/2)*29.0f;text(g,labels[(size_t)i],{20.0f+(i%2)*385.0f,y,96.0f,22.0f},s.palette.secondary(),10.0f);}
            text(g,"New imports use one source mapped chromatically from C3.",{18,(float)getHeight()-23,(float)getWidth()-36,18},s.palette.secondary(),9.5f);
        }
        void resized()override
        {
            name.setBounds(510,10,260,28);zoomOut.setBounds(655,62,55,22);zoomIn.setBounds(714,62,55,22);
            replace.setBounds(18,211,125,25);preview.setBounds(150,211,80,25);reset.setBounds(237,211,90,25);midiFollow.setBounds(335,211,115,25);reverse.setBounds(458,211,85,25);oneShot.setBounds(548,211,92,25);loop.setBounds(645,211,62,25);choke.setBounds(710,211,70,25);
            for(int i=0;i<15;++i)sliders[(size_t)i].setBounds(118+(i%2)*385,244+(i/2)*29,275,22);
            const int pitchRow=244+29;sliders[2].setBounds(118,pitchRow,170,22);pitchDown.setBounds(291,pitchRow,31,22);pitchUp.setBounds(325,pitchRow,31,22);pitchReset.setBounds(359,pitchRow,48,22);
        }
        void mouseDown(const juce::MouseEvent& e)override{auto r=waveRect();if(!r.contains(e.position))return;float f=sampleFraction(e.position.x,r);const float v[]={settings.start,settings.end,settings.loopStart,settings.loopEnd};dragHandle=1;for(int i=1;i<4;++i)if(std::abs(f-v[i])<std::abs(f-v[dragHandle-1]))dragHandle=i+1;mouseDrag(e);}
        void mouseDrag(const juce::MouseEvent& e)override{if(dragHandle==0)return;auto r=waveRect();float f=sampleFraction(e.position.x,r);if(dragHandle==1){settings.start=juce::jmin(f,settings.end-.001f);settings.loopStart=juce::jmax(settings.loopStart,settings.start);}else if(dragHandle==2){settings.end=juce::jmax(f,settings.start+.001f);settings.loopEnd=juce::jmin(settings.loopEnd,settings.end);}else if(dragHandle==3)settings.loopStart=juce::jlimit(settings.start,settings.loopEnd-.001f,f);else settings.loopEnd=juce::jlimit(settings.loopStart+.001f,settings.end,f);s.proc.getVocalKit().setSlotSettings(slot,settings);repaint();}
        void mouseUp(const juce::MouseEvent&)override{dragHandle=0;s.refreshSample();}
        void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w)override{zoom(w.deltaY>0);}
    };
    std::array<int,128> heldCounts{};
    class LoopOverview final : public juce::Component {
        Surface& s;
    public:
        explicit LoopOverview(Surface& a):s(a){setMouseCursor(juce::MouseCursor::PointingHandCursor);}
        void paint(juce::Graphics& g)override {auto r=getLocalBounds().toFloat().reduced(1);card(g,r,s.palette,false,4);
            if(!s.proc.getLooper().anyContent()){text(g,"OPEN LOOP STATION",r,s.palette.secondary(),10,juce::Justification::centred,.06f);return;}
            g.setColour(s.palette.ice());for(int i=0;i<210;++i){int idx=i*512/210;float h=s.loopPeaks[(size_t)idx]*(r.getHeight()-6)*.48f;float x=r.getX()+i*r.getWidth()/210;g.drawLine(x,r.getCentreY()-h,x,r.getCentreY()+h,1);}
            float pos=s.proc.getLooper().getTrackPosition(s.selectedLoop);g.setColour(s.palette.accent());g.drawVerticalLine((int)(r.getX()+r.getWidth()*pos),r.getY(),r.getBottom());
        }
        void mouseDown(const juce::MouseEvent&)override{s.openWorkspace(2);}
    } overview{*this};

    explicit Surface(VocalChopAudioProcessor& p):proc(p) {
        setSize(ref::width,ref::height);setWantsKeyboardFocus(true);setComponentID(ref::build);
        palette.scheme=proc.getReferenceThemeScheme();
        palette.dark=palette.scheme==1;
        if(auto* bypass=proc.getAPVTS().getRawParameterValue("fxBypass"))fxEnabled=bypass->load()<0.5f;
        chromeLight=juce::ImageCache::getFromMemory(ReferenceSkinData::reference_chrome_light_png,ReferenceSkinData::reference_chrome_light_pngSize);
        chromeDark=juce::ImageCache::getFromMemory(ReferenceSkinData::reference_chrome_dark_png,ReferenceSkinData::reference_chrome_dark_pngSize);
        knobLight=juce::ImageCache::getFromMemory(ReferenceSkinData::reference_knob_light_png,ReferenceSkinData::reference_knob_light_pngSize);
        knobDark=juce::ImageCache::getFromMemory(ReferenceSkinData::reference_knob_dark_png,ReferenceSkinData::reference_knob_dark_pngSize);
        setLookAndFeel(&look);readBrowserState();buildCatalog();
        const char* browserNames[]={"PRESETS","INSTRUMENTS","USER"};
        for(int i=0;i<3;++i)browserButtons[(size_t)i]=&button(browserNames[i],ref::browserTabs[(size_t)i],RefButton::Tab,[this,i]{browserTab=i;selectedCategory={};query={};search.clear();refreshBrowser(true);});
        addAndMakeVisible(search);search.setBounds(ref::search.get());search.setFont(font(12.5f));search.setBorder(juce::BorderSize<int>(7,35,4,7));search.setMultiLine(false);search.setScrollbarsShown(false);search.onTextChange=[this]{query=search.getText();refreshBrowser(true);};
        addAndMakeVisible(browser);browser.setBounds(ref::browser.get());
        // This remains visible instead of being hidden in the overflow menu:
        // a first-time user must immediately know where their audio goes.
        importButton=&button("LOAD / DROP YOUR SAMPLE",ref::importAudio,RefButton::Raised,[this]{loadSample();});
        importButton->setTooltip("Click to choose one WAV, AIFF, FLAC, OGG or MP3 file, or drag the file anywhere onto SLYCE. It is mapped across the keyboard from C3.");
        button("Previous sound",ref::previous,RefButton::Previous,[this]{stepSound(-1);});button("Next sound",ref::next,RefButton::Next,[this]{stepSound(1);});
        heart=&button("Favourite",ref::favorite,RefButton::Heart,[this]{auto k=currentKey();if(favourites.contains(k))favourites.removeString(k);else favourites.addIfNotAlreadyThere(k);writeBrowserState();refreshBrowser(false);repaint();});
        auto& titleHit=button("",ref::title,RefButton::Bare,[this]{selectedCategory="All Presets";refreshBrowser(true);});titleHit.setTooltip("Browse sounds. Current sound and sample names are live, not part of the skin.");
        engineButton=&button("ENGINE",{690,159,109,24},RefButton::Raised,[this]{showEngineMenu();});
        engineButton->setTooltip("Choose Chop, Instrument, Sampled, Mapped Sample or Air Vocal");
        themeButton=&button("THEME",{803,159,120,24},RefButton::Raised,[this]{showThemeMenu();});
        themeButton->setTooltip("Choose the complete chassis and glass theme");
        saveButton=&button("SAVE",{927,159,49,24},RefButton::Raised,[this]{savePreset();});
        saveButton->setTooltip("Save the current sound to USER");
        auto& menu=button("Menu",ref::menu,RefButton::More,[this]{showMenu();});menuButton=&menu;menu.setTooltip("Theme, sound details, sample import, presets, activation. Build "+juce::String(ref::build));
        button("",ref::bpm,RefButton::Bare,[this]{editBpm();}).setTooltip("Set metronome / recording tempo");
        const char* tabNames[]={"WAVEFORM","SLICES","LOOPER","CONTROLS"};for(int i=0;i<4;++i)workspaceButtons[(size_t)i]=&button(tabNames[i],ref::workspaceTabs[(size_t)i],RefButton::Tab,[this,i]{openWorkspace(i);});
        countButton=&button("",ref::sliceCount,RefButton::Bare,[this]{showSlicePages();});
        button("Zoom out",ref::zoomOut,RefButton::ZoomOut,[this]{wave.zoom(false);});button("Zoom in",ref::zoomIn,RefButton::ZoomIn,[this]{wave.zoom(true);});
        addAndMakeVisible(wave);wave.setBounds(ref::wave.get());workspaceControls.push_back(&wave);
        auto& play=button("Preview full sample",ref::play,RefButton::Play,[this]{proc.triggerWholeSamplePreview();});play.setTooltip("Play the complete loaded sample once");workspaceControls.push_back(&play);
        addAndMakeVisible(snap);snap.setBounds(ref::snap.get());snap.addItemList({"Off","1/8","1/16","1/32"},1);snap.setSelectedId(3);snap.setTooltip("Snap dragged slice boundaries to divisions of the sample");workspaceControls.push_back(&snap);
        addAndMakeVisible(sensitivity);sensitivity.setBounds(ref::sensitivity.get());sensitivity.setSliderStyle(juce::Slider::LinearHorizontal);sensitivity.setRange(.01,.99,.01);sensitivity.setValue(proc.getSliceEngine().getSensitivity(),juce::dontSendNotification);sensitivity.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);sensitivity.setWantsKeyboardFocus(false);sensitivity.onValueChange=[this]{proc.getSliceEngine().setSensitivity((float)sensitivity.getValue());};workspaceControls.push_back(&sensitivity);
        auto& detect=button("DETECT",ref::detect,RefButton::Raised,[this]{proc.getSliceEngine().detectTransientsOnce();dirty=true;refreshSample();});detect.setTooltip("Analyze once with the current sensitivity, then leave the cuts editable");workspaceControls.push_back(&detect);
        autoButton=&button("AUTO",ref::autoSlice,RefButton::Raised,[this]{proc.getSliceEngine().setMode(SliceEngine::Transient);proc.getSliceEngine().rebuildSlices();dirty=true;refreshSample();});autoButton->setTooltip("Persistent transient slicing mode");workspaceControls.push_back(autoButton);
        manualButton=&button("MANUAL",ref::manual,RefButton::Raised,[this]{proc.getSliceEngine().setMode(SliceEngine::Manual);refreshSample();});workspaceControls.push_back(manualButton);
        auto& clear=button("CLEAR",ref::clear,RefButton::Raised,[this]{proc.getSliceEngine().sliceByManual({0});refreshSample();});clear.setTooltip("Remove slice markers; keep the audio sample intact");workspaceControls.push_back(&clear);
        for(int i=0;i<8;++i){pads[(size_t)i]=std::make_unique<Pad>(*this,i);addAndMakeVisible(*pads[(size_t)i]);pads[(size_t)i]->setBounds(ref::pads[(size_t)i].get());workspaceControls.push_back(pads[(size_t)i].get());}
        const char* rightNames[]={"MAIN","FX","MASTER"};for(int i=0;i<3;++i)rightButtons[(size_t)i]=&button(rightNames[i],ref::rightTabs[(size_t)i],RefButton::Tab,[this,i]{setRightTab(i);});
        fxButton=&button("Bypass creative effects",ref::fxPower,RefButton::Power,[this]{toggleFX();});
        for(int i=0;i<7;++i){auto& k=knobs[(size_t)i];k=std::make_unique<RefKnob>(palette,knobLight,knobDark,i);addAndMakeVisible(*k);k->setBounds(ref::knobs[(size_t)i].get());k->setName(ref::captions[(size_t)i]);k->setComponentID(ref::parameterIDs[(size_t)i]);
        }
        loopButton=&button("Play / stop recorded loops",ref::loopPower,RefButton::Power,[this]{auto& l=proc.getLooper();if(!l.anyContent()){openWorkspace(2);return;}if(l.anyRunning())l.tapStopAll();else l.tapPlayAll();});
        addAndMakeVisible(overview);overview.setBounds(ref::loopWave.get());
        button("Previous loop track",ref::loopPrev,RefButton::BarePrevious,[this]{selectedLoop=(selectedLoop+LoopStation::kNumTracks-1)%LoopStation::kNumTracks;overview.repaint();});
        button("Next loop track",ref::loopNext,RefButton::BareNext,[this]{selectedLoop=(selectedLoop+1)%LoopStation::kNumTracks;overview.repaint();});
        addAndMakeVisible(loopLength);loopLength.setBounds(ref::loopLength.get());loopLength.addItem("Free",1);loopLength.addItem("1 Bar",2);loopLength.addItem("2 Bars",3);loopLength.addItem("4 Bars",4);loopLength.addItem("8 Bars",5);
        loopLength.onChange=[this]{static const int lengths[]={0,1,2,4,8};proc.getLooper().setRecordBars(lengths[juce::jlimit(0,4,loopLength.getSelectedItemIndex())]);};syncRecordBars();
        syncButton=&button("SYNC",ref::sync,RefButton::Raised,[this]{proc.getLooper().setTempoSync(!proc.getLooper().isTempoSync());updateTabs();});syncButton->setTooltip("Sync metronome and recording grid to host tempo. Existing loops are not time-stretched.");
        addAndMakeVisible(keybed);keybed.setBounds(ref::keys.get());addAndMakeVisible(pitch);pitch.setBounds(ref::pitch.get());addAndMakeVisible(mod);mod.setBounds(ref::mod.get());
        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.getAPVTS(),"pitch",pitch));pitch.setDoubleClickReturnValue(true,0);
        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.getAPVTS(),"synthVibrato",mod));mod.setDoubleClickReturnValue(true,0);
        setTheme(palette.scheme);refreshSample();if(proc.getSelectedSlice()<0 && !slices.empty())proc.setSelectedSlice(0);refreshBrowser(true);proc.addChangeListener(this);startTimerHz(30);

        // The reference editor replaced the legacy editor, but its constructor
        // never opened UnlockPanel.  Queue this until construction has
        // completed so it works both in the Standalone wrapper and when a DAW
        // creates the VST3 editor.  A valid machine-bound ticket makes
        // isLicensed() true, so activated users never see this again.
        if(!proc.isLicensed()){
            auto safe=juce::Component::SafePointer<Surface>(this);
            juce::MessageManager::callAsync([safe]{if(safe&&!safe->proc.isLicensed())safe->showActivationPanel();});
        }
    }
    ~Surface() override {stopTimer();proc.removeChangeListener(this);keybed.release();releaseTyped();attachments.clear();setLookAndFeel(nullptr);}
    RefButton& button(juce::String name,const ref::Rect& bounds,RefButton::Style style,std::function<void()> action) {
        auto b=std::make_unique<RefButton>(palette,name,style);b->onClick=std::move(action);b->setBounds(bounds.get());addAndMakeVisible(*b);auto& result=*b;buttons.push_back(std::move(b));return result;
    }
    void setDark(bool dark) { setTheme(dark?1:0); }
    juce::String engineLabel()const {
        const int e=(int)proc.getAPVTS().getRawParameterValue("engine")->load();
        static const char* names[]={"CHOP","INSTRUMENT","SFZ","SAMPLE MAP","LEGACY","AIR VOCAL"};
        return names[juce::jlimit(0,5,e)];
    }
    void updateContextControls(){
        if(engineButton){engineButton->setName(engineLabel());engineButton->repaint();}
        if(themeButton){themeButton->setName("THEME");themeButton->repaint();}
        if(saveButton){saveButton->setName(dirty?"SAVE*":"SAVE");saveButton->repaint();}
        if(importButton){importButton->setName("LOAD / DROP YOUR SAMPLE");importButton->repaint();}
    }
    void showEngineMenu(){
        juce::PopupMenu m;const int selected=(int)proc.getAPVTS().getRawParameterValue("engine")->load();
        struct EngineItem { int engine; const char* name; };
        static constexpr EngineItem items[]={{0,"Chop"},{1,"Instrument"},{2,"Sampled"},{3,"Mapped Sample"},{5,"Air Vocal"}};
        for(const auto& item:items)m.addItem(item.engine+1,item.name,true,selected==item.engine);
        auto safe=juce::Component::SafePointer<Surface>(this);m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(engineButton),[safe](int id){if(!safe||id<1)return;safe->releaseTyped();safe->keybed.release();const int engine=id-1;if(engine==4&&!safe->proc.getVocalKit().hasSlot(0))safe->proc.loadFactoryVocalKit(0);else if(engine==5)safe->proc.applyAirVocalPreset(safe->proc.getCurrentAirVocalPreset());else safe->setParameter("engine",(float)engine);safe->dirty=true;safe->syncRightPanel();safe->refreshSample();safe->updateContextControls();safe->repaint();});
    }
    void showThemeMenu(){
        juce::PopupMenu m;const char* names[]={"Arctic Blue","Noir Violet Glass","Emerald Night","Rose Pink","Sunset Carbon","Azure Ice Glass","Ultraviolet Glass","Carbon Gold","Arcade Pulse"};
        for(int i=0;i<9;++i)m.addItem(i+1,names[i],true,palette.scheme==i);
        auto safe=juce::Component::SafePointer<Surface>(this);m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(themeButton),[safe](int id){if(safe&&id>0)safe->setTheme(id-1);});
    }
    void rebuildThemeChrome() {
        themedChrome=juce::Image(juce::Image::RGB,ref::width,ref::height,true);
        juce::Graphics bg(themedChrome);
        bg.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        const auto& source=palette.dark?chromeDark:chromeLight;
        if(source.isValid())bg.drawImageAt(source,0,0);else bg.fillAll(palette.bottom());
        if(palette.scheme==0)return;

        // All theme work is baked once on selection, rather than composited on
        // every 30 Hz repaint. This keeps host/editor redraw cost predictable.
        const auto whole=juce::Rectangle<float>(0,0,(float)ref::width,(float)ref::height);
        if(palette.scheme==1){
            juce::ColourGradient noir(juce::Colour(0xff090b14).withAlpha(.38f),whole.getTopLeft(),juce::Colour(0xff24153b).withAlpha(.52f),whole.getBottomRight(),false);
            noir.addColour(.50,juce::Colour(0xff101221).withAlpha(.33f));bg.setGradientFill(noir);bg.fillRect(whole);
        }else if(palette.scheme==8){
            // Arcade Pulse is a real glass/arcade skin, not only a pink
            // accent on the dark default: give it a violet-to-navy body,
            // cyan horizon and restrained scanline texture.
            juce::ColourGradient arcade(juce::Colour(0xff020208),whole.getTopLeft(),
                                         juce::Colour(0xff090616),whole.getBottomRight(),false);
            arcade.addColour(.34,juce::Colour(0xff120b30));
            arcade.addColour(.66,juce::Colour(0xff041326));
            bg.setGradientFill(arcade); bg.fillRect(whole);
            bg.setColour(juce::Colour(0xffff3fd5).withAlpha(.075f));
            bg.fillEllipse(82.0f,64.0f,610.0f,330.0f);
            bg.setColour(juce::Colour(0xff45f2ff).withAlpha(.055f));
            bg.fillEllipse(790.0f,385.0f,650.0f,390.0f);
            bg.setColour(juce::Colour(0xff45f2ff).withAlpha(.035f));
            for(int y=136; y<770; y+=6) bg.drawHorizontalLine(y,52.0f,1486.0f);
        }else if(palette.scheme==2){
            juce::ColourGradient mint(juce::Colours::white.withAlpha(.16f),whole.getTopLeft(),palette.skinTint().withAlpha(.31f),whole.getBottomRight(),false);
            mint.addColour(.52,juce::Colour(0xffeffffb).withAlpha(.20f));bg.setGradientFill(mint);bg.fillRect(whole);
        }else if(palette.scheme==3){
            juce::ColourGradient rose(juce::Colour(0xfffff7fb).withAlpha(.18f),whole.getTopRight(),palette.skinTint().withAlpha(.34f),whole.getBottomLeft(),false);
            rose.addColour(.44,juce::Colour(0xffffedf7).withAlpha(.18f));bg.setGradientFill(rose);bg.fillRect(whole);
        }else if(palette.scheme==4){
            juce::ColourGradient amber(juce::Colour(0xfffffbef).withAlpha(.15f),whole.getTopLeft(),palette.skinTint().withAlpha(.35f),whole.getBottomRight(),false);
            amber.addColour(.58,juce::Colour(0xfffff3da).withAlpha(.22f));bg.setGradientFill(amber);bg.fillRect(whole);
        }else if(palette.scheme==5){
            juce::ColourGradient ice(juce::Colours::white.withAlpha(.24f),whole.getTopLeft(),palette.skinTint().withAlpha(.38f),whole.getBottomRight(),false);
            ice.addColour(.38,juce::Colour(0xffeef9ff).withAlpha(.30f));ice.addColour(.72,juce::Colour(0xffcfeeff).withAlpha(.18f));bg.setGradientFill(ice);bg.fillRect(whole);
        }else if(palette.scheme==6){
            juce::ColourGradient lavender(juce::Colour(0xfffffbff).withAlpha(.20f),whole.getTopRight(),palette.skinTint().withAlpha(.39f),whole.getBottomLeft(),false);
            lavender.addColour(.47,juce::Colour(0xfff4efff).withAlpha(.24f));bg.setGradientFill(lavender);bg.fillRect(whole);
        }else{
            juce::ColourGradient champagne(juce::Colour(0xfffffdfa).withAlpha(.18f),whole.getTopLeft(),palette.skinTint().withAlpha(.34f),whole.getBottomRight(),false);
            champagne.addColour(.35,juce::Colour(0xfff8f4ec).withAlpha(.24f));champagne.addColour(.72,juce::Colour(0xffe9dfcf).withAlpha(.16f));bg.setGradientFill(champagne);bg.fillRect(whole);
        }

        const juce::Rectangle<float> panels[]={
            {54,135,288,632},{350,135,837,632},{1196,135,291,632},{50,773,1438,123}
        };
        for(const auto panel:panels){
            if(palette.dark){
                juce::Colour panelTop = juce::Colour(0xff40345c);
                juce::Colour panelBottom = juce::Colour(0xff080a12);
                if (palette.scheme == 2) { panelTop = juce::Colour(0xff123f42); panelBottom = juce::Colour(0xff041416); }
                else if (palette.scheme == 4) { panelTop = juce::Colour(0xff4b2418); panelBottom = juce::Colour(0xff120907); }
                else if (palette.scheme == 6) { panelTop = juce::Colour(0xff3d2764); panelBottom = juce::Colour(0xff0d071b); }
                else if (palette.scheme == 7) { panelTop = juce::Colour(0xff49391f); panelBottom = juce::Colour(0xff110d08); }
                juce::ColourGradient glass(panelTop.withAlpha(.42f),panel.getTopLeft(),panelBottom.withAlpha(.58f),panel.getBottomLeft(),false);
                if (palette.scheme == 2) glass.addColour(.45, juce::Colour(0xff0b6b65).withAlpha(.18f));
                else if (palette.scheme == 4) glass.addColour(.52, juce::Colour(0xffd5652b).withAlpha(.15f));
                else if (palette.scheme == 6) glass.addColour(.42, juce::Colour(0xff8a54e8).withAlpha(.19f));
                else if (palette.scheme == 7) glass.addColour(.58, juce::Colour(0xffd0a04c).withAlpha(.16f));
                bg.setGradientFill(glass);bg.fillRoundedRectangle(panel,10);
                bg.setColour(palette.ice().withAlpha(.08f));bg.drawRoundedRectangle(panel.reduced(1),9,1);
            }else{
                const float panelAlpha=palette.scheme==2?.19f:(palette.scheme==3?.16f:(palette.scheme==5?.25f:(palette.scheme==6?.18f:.10f)));
                bg.setColour(juce::Colours::white.withAlpha(panelAlpha));bg.fillRoundedRectangle(panel,10);
                if(palette.scheme==2){bg.setColour(palette.accent().withAlpha(.07f));bg.drawRoundedRectangle(panel.reduced(1),9,1.2f);}
                else if(palette.scheme==3){juce::ColourGradient sheen(juce::Colours::white.withAlpha(.28f),panel.getTopLeft(),palette.ice().withAlpha(.055f),panel.getBottomRight(),false);bg.setGradientFill(sheen);bg.fillRoundedRectangle(panel.reduced(2),8);}
                else if(palette.scheme==5){juce::ColourGradient frost(juce::Colours::white.withAlpha(.34f),panel.getTopLeft(),palette.ice().withAlpha(.055f),panel.getBottomLeft(),false);bg.setGradientFill(frost);bg.fillRoundedRectangle(panel.reduced(2),8);bg.setColour(palette.ice().withAlpha(.12f));bg.drawRoundedRectangle(panel.reduced(1),9,1.1f);}
                else if(palette.scheme==6){juce::ColourGradient haze(palette.ice().withAlpha(.065f),panel.getTopRight(),juce::Colours::white.withAlpha(.24f),panel.getBottomLeft(),false);bg.setGradientFill(haze);bg.fillRoundedRectangle(panel.reduced(2),8);bg.setColour(palette.accent().withAlpha(.085f));bg.drawRoundedRectangle(panel.reduced(1),9,1.1f);}
                else if(palette.scheme==7){bg.setColour(palette.accent().withAlpha(.07f));bg.drawRoundedRectangle(panel.reduced(1),9,1.25f);bg.setColour(juce::Colours::white.withAlpha(.26f));bg.drawHorizontalLine((int)panel.getY()+2,panel.getX()+10,panel.getRight()-10);}
                else {bg.setColour(palette.accent().withAlpha(.065f));bg.drawRoundedRectangle(panel.reduced(1),9,1.3f);}
            }
        }
        bg.setColour(palette.accent().withAlpha(palette.dark?.42f:.19f));bg.drawRoundedRectangle(49.5f,34.5f,1437.0f,861.0f,16.0f,palette.dark?1.6f:1.1f);
        if(palette.dark){
            // Two crisp neon rails imply glow without an expensive blur pass.
            bg.setColour(palette.accent().withAlpha(.16f));bg.fillRect(351.0f,134.0f,835.0f,2.0f);
            bg.setColour(palette.ice().withAlpha(.10f));bg.fillRect(1197.0f,134.0f,289.0f,2.0f);
        }else if(palette.scheme==4){
            bg.setColour(palette.accent().withAlpha(.055f));for(int y=137;y<766;y+=5)bg.drawHorizontalLine(y,1200.0f,1482.0f);
        }else if(palette.scheme==5){
            bg.setColour(juce::Colours::white.withAlpha(.28f));bg.fillRect(352.0f,136.0f,833.0f,2.0f);bg.setColour(palette.ice().withAlpha(.08f));bg.fillRect(1198.0f,136.0f,287.0f,2.0f);
        }else if(palette.scheme==6){
            bg.setColour(palette.ice().withAlpha(.09f));bg.drawLine(54.0f,765.0f,342.0f,136.0f,1.0f);bg.drawLine(1197.0f,765.0f,1485.0f,136.0f,1.0f);
        }else if(palette.scheme==7){
            bg.setColour(palette.accent().withAlpha(.038f));for(int y=137;y<766;y+=4){bg.drawHorizontalLine(y,55.0f,341.0f);bg.drawHorizontalLine(y,1197.0f,1485.0f);}
        }
    }
    void setTheme(int scheme) {
        palette.scheme=juce::jlimit(0,8,scheme);
        palette.dark=palette.scheme==1||palette.scheme==2||palette.scheme==4||palette.scheme==6||palette.scheme==7||palette.scheme==8;
        proc.setReferenceThemeScheme(palette.scheme);
        ThemeManager::setIndex(palette.dark?20:21);
        rebuildThemeChrome();
        search.setColour(juce::TextEditor::backgroundColourId,juce::Colours::transparentBlack);search.setColour(juce::TextEditor::textColourId,palette.text());search.setColour(juce::TextEditor::highlightColourId,palette.accent().withAlpha(.22f));search.setColour(juce::CaretComponent::caretColourId,palette.accent());search.setTextToShowWhenEmpty("Search presets...",palette.secondary());
        look.setColour(juce::ComboBox::textColourId,palette.text());look.setColour(juce::PopupMenu::backgroundColourId,palette.bottom());look.setColour(juce::PopupMenu::textColourId,palette.text());look.setColour(juce::PopupMenu::highlightedBackgroundColourId,palette.accent().withAlpha(.2f));look.setColour(juce::PopupMenu::highlightedTextColourId,palette.text());
        sensitivity.setColour(juce::Slider::thumbColourId,palette.ice());sensitivity.setColour(juce::Slider::trackColourId,palette.ice());sensitivity.setColour(juce::Slider::backgroundColourId,palette.line());
        updateContextControls();syncRightPanel();sendLookAndFeelChange();repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        if(themedChrome.isValid())g.drawImageAt(themedChrome,0,0);else g.fillAll(palette.bottom());
        // The supplied mockup artwork had macOS traffic-light dots baked in.
        // Remove only the three dots; a large cover rectangle used to erase the
        // FIND / SLICE / CREATE copy immediately below them.
        // The RGB traffic lights are baked into the bitmap.  Copy a clean
        // strip from the same header gradient over their row so the dots are
        // actually removed without leaving three obvious white circles.
        // Match the local light-blue header field rather than painting white
        // circles or a rectangular patch over the gradient.
        const auto patch=themedChrome.isValid()?themedChrome.getPixelAt(54,46):palette.bottom();g.setColour(patch);
        for (const auto centre : { juce::Point<float>{68.0f, 53.0f},
                                   juce::Point<float>{86.0f, 53.0f},
                                   juce::Point<float>{108.0f, 53.0f} })
            g.fillEllipse(centre.x - 8.0f, centre.y - 8.0f, 16.0f, 16.0f);
        // Search well and icon; the text belongs to the live TextEditor.
        auto r=ref::search.get().toFloat();g.setColour(palette.line());g.drawRoundedRectangle(r.reduced(.5f),8,.8f);g.drawEllipse(76,190,11,11,1.1f);g.drawLine(85,199,90,204,1.1f);
        auto title=currentTitle();text(g,title,{498,143,470,23},palette.text(),15.5f);
        text(g,proc.isAirVocalMode()?"AIR VOCAL":(proc.isChopMode()?"VOCAL CHOP KIT":(proc.isMelodyMode()?"VOCAL INSTRUMENT":"INSTRUMENT")),{499,165,181,19},palette.secondary(),9.5f,juce::Justification::centredLeft,.17f);
        text(g,"BPM",{1053,143,61,17},palette.secondary(),10,juce::Justification::centred);
        double bpm=proc.getHostBpm();if(bpm<=0)bpm=proc.getLooper().getMetroBpm();text(g,juce::String(bpm,0),{1053,160,61,23},palette.text(),13,juce::Justification::centred);
        text(g,"KEY",{1118,143,63,17},palette.secondary(),10,juce::Justification::centred);int root=proc.getDetectedKeyRoot();const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        text(g,root>=0&&root<12?juce::String(names[root])+(proc.isDetectedKeyMinor()?" Min":" Maj"):"--",{1118,160,63,23},palette.text(),12,juce::Justification::centred);
        if(!overlay){g.setColour(palette.line().withAlpha(.8f));g.drawRoundedRectangle(358.5f,238.5f,820.0f,228.5f,8,.8f);text(g,"SNAP",{460,428,39,29},palette.secondary(),10.5f);text(g,"SENSITIVITY",{600,428,77,29},palette.secondary(),10);}
        g.setColour(palette.line().withAlpha(.7f));g.drawLine(1211,475,1469,475,1);g.drawLine(1211,620,1469,620,1);
        if(rightTab==0||rightTab==1){smallIcon(g,4,{1214,192,15,17},fxEnabled?palette.text():palette.secondary());text(g,fxEnabled?"CREATIVE FX":"CREATIVE FX  OFF",{1239,189,188,24},fxEnabled?palette.text():palette.secondary(),12.5f,juce::Justification::centredLeft,.1f);}
        if(rightTab==2){smallIcon(g,4,{1214,192,15,17},palette.text());text(g,"MASTER BUS",{1239,189,188,24},palette.text(),12.5f,juce::Justification::centredLeft,.1f);}
        if(rightTab==0){smallIcon(g,4,{1214,491,15,17},palette.text());text(g,"BUILT-IN LOOPER",{1239,488,192,24},palette.text(),12,juce::Justification::centredLeft,.075f);}
        if(rightTab==0||rightTab==2){smallIcon(g,4,{1214,638,15,17},palette.text());text(g,rightTab==2?"MACROS":"TONE",{1239,635,150,24},palette.text(),12,juce::Justification::centredLeft,.1f);}
        for(int i=0;i<7;++i)if(knobs[(size_t)i]&&knobs[(size_t)i]->isVisible())text(g,knobCaptions[(size_t)i].isNotEmpty()?knobCaptions[(size_t)i]:juce::String(ref::captions[(size_t)i]),ref::knobLabels[(size_t)i].get().toFloat(),palette.text(),12,juce::Justification::centred,.015f);
        if(rightTab==0)text(g,"LENGTH",{1217,578,59,30},palette.secondary(),11.5f);
        text(g,"PITCH",{73,861,49,22},palette.text(),10,juce::Justification::centred,.045f);text(g,"MOD",{129,861,49,22},palette.text(),10,juce::Justification::centred,.045f);
        text(g,"MANY PRESETS",{146,731,182,20},palette.text(),12,juce::Justification::centredLeft,.15f);
        text(g,juce::String(proc.getNumDemoSamples())+" VOCALS",{147,751,177,15},palette.accent(),9,juce::Justification::centredLeft,.2f);
        text(g,juce::String(proc.getInstrumentNames().size())+" SOUNDS",{147,766,177,15},palette.accent(),9,juce::Justification::centredLeft,.2f);
        const juce::String facts[]={juce::String(proc.getNumDemoSamples())+" VOCALS",juce::String(proc.getInstrumentNames().size())+" SOUNDS","BUILT-IN LOOPER","CREATIVE FX","MANY PRESETS"};
        const int xs[]={266,443,638,876,1081},widths[]={140,145,192,155,160};
        for(int i=0;i<5;++i){text(g,facts[i],{(float)xs[i],932,(float)widths[i],24},palette.text(),13,juce::Justification::centred,.18f);if(i<4){g.setColour(palette.secondary());g.drawLine((float)(i==0?420:i==1?615:i==2?852:1051),933,(float)(i==0?420:i==1?615:i==2?852:1051),951,.8f);}}
    }
    void paintOverChildren(juce::Graphics& g) override {
        // The reference chrome contains three macOS traffic-light pixels.
        // Paint over them after child components so they cannot leak through
        // at Windows DPI scales.  Keep the FIND/SLICE/CREATE copy below it.
        const auto bg=themedChrome.isValid()?themedChrome.getPixelAt(54,46):palette.skinTint();
        g.setColour(bg);
        g.fillRect(54.0f,43.0f,76.0f,23.0f);
    }
    juce::String currentTitle()const {
        if(selectedPresetName.isNotEmpty())return selectedPresetName;
        if(proc.isAirVocalMode())return proc.getAirVocalPresetNames()[proc.getCurrentAirVocalPreset()];
        if(proc.isVocalKitMode())return "Vocal Kit";
        if(proc.isSynthMode()&&!proc.isMelodyMode()){auto names=proc.getInstrumentNames();int i=proc.getCurrentInstrument();if(juce::isPositiveAndBelow(i,names.size()))return names[i];}
        return proc.getLoadedSampleName().isNotEmpty()?proc.getLoadedSampleName():"Choose a vocal or instrument";
    }
    juce::String currentKey()const {if(selectedCatalogKey.isNotEmpty())return selectedCatalogKey;if(proc.isAirVocalMode())return "4:"+juce::String(proc.getCurrentAirVocalPreset());if(proc.isSynthMode()&&!proc.isMelodyMode())return "1:"+juce::String(proc.getCurrentInstrument());return "0:"+juce::String(proc.getCurrentDemoIndex());}
    void setParameter(const char* id,float value){if(auto* p=proc.getAPVTS().getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();dirty=true;updateContextControls();}}
    void noteOn(int offset,float velocity){const int note=offset+48;if(!juce::isPositiveAndBelow(note,128))return;if(heldCounts[(size_t)note]++==0)proc.pressSlicePad(offset,velocity);keybed.highlight(offset,true);
        if(proc.isChopMode()&&!slices.empty()){int i=proc.diatonicSliceIndex(offset);i=((i%(int)slices.size())+(int)slices.size())%(int)slices.size();proc.setSelectedSlice(i);}}
    void noteOff(int offset){int note=offset+48;if(!juce::isPositiveAndBelow(note,128)||heldCounts[(size_t)note]<1)return;if(--heldCounts[(size_t)note]==0){proc.releaseSlicePad(offset);keybed.highlight(offset,false);}}
    void releaseTyped(){for(int n:typedNotes)noteOff(n);typedNotes.clear();}
    bool scanKeys(){
        auto* focused=juce::Component::getCurrentlyFocusedComponent();bool editing=dynamic_cast<juce::TextEditor*>(focused)!=nullptr;
        // LooperPanel is an in-editor workspace, not a modal blocker. The old
        // generic overlay test disabled Z/X/C... as soon as LOOPER opened,
        // making the selected looper instrument impossible to audition or
        // record from the computer keyboard.
        const bool looperWorkspace=dynamic_cast<LooperPanel*>(overlay.get())!=nullptr;
        bool allowed=!editing&&(!overlay||looperWorkspace)&&getTopLevelComponent()->hasKeyboardFocus(true);
        std::set<int> now;if(allowed)for(int i=0;i<slyce::keymap::numKeys;++i){int c=(int)slyce::keymap::keys[i];int up=(int)juce::CharacterFunctions::toUpperCase((juce::juce_wchar)c);if(juce::KeyPress::isKeyCurrentlyDown(c)||juce::KeyPress::isKeyCurrentlyDown(up))now.insert(slyce::keymap::semitoneFor(i,proc.isChopMode()));}
        for(int n:typedNotes)if(!now.count(n))noteOff(n);for(int n:now)if(!typedNotes.count(n))noteOn(n,.86f);bool used=!now.empty()||!typedNotes.empty();typedNotes=std::move(now);return used;
    }
    void auditionSlice(int i){if(proc.isVocalKitMode()){if(juce::isPositiveAndBelow(i,VocalKitEngine::kNumSlots))proc.auditionVocalKitSlot(i);return;}if(!juce::isPositiveAndBelow(i,(int)slices.size()))return;setParameter("engine",0);proc.setSelectedSlice(i);proc.triggerSliceDirectPad(i);wave.repaint();for(auto& p:pads)p->repaint();}
    void refreshSample(){
        if(proc.isVocalKitMode()){
            int selected=juce::jlimit(0,VocalKitEngine::kNumSlots-1,proc.getSelectedSlice());proc.setSelectedSlice(selected);auto next=proc.getVocalKit().getSlotSample(selected);const bool different=next!=sample;sample=std::const_pointer_cast<juce::AudioBuffer<float>>(next);slices.clear();if(different){wave.resetZoom();fullEnvelope=sample?envelope(*sample,0,sample->getNumSamples(),4096):std::vector<Peak>{};}cardPage=juce::jlimit(0,1,cardPage);for(int i=0;i<8;++i){int actual=cardPage*8+i;auto slotSample=actual<VocalKitEngine::kNumSlots?proc.getVocalKit().getSlotSample(actual):nullptr;padEnvelopes[(size_t)i]=slotSample?envelope(*slotSample,0,slotSample->getNumSamples(),256):std::vector<Peak>{};pads[(size_t)i]->repaint();}if(countButton){countButton->setName("15 SLOTS");countButton->repaint();}updateContextControls();updateTabs();wave.repaint();repaint(ref::title.get());return;}
        auto next=proc.getLoadedSample();const bool different=next!=sample;sample=next;slices=proc.getSliceEngine().getSlices();
        if(different){wave.resetZoom();fullEnvelope=sample?envelope(*sample,0,sample->getNumSamples(),4096):std::vector<Peak>{};}
        cardPage=juce::jlimit(0,juce::jmax(0,((int)slices.size()-1)/8),cardPage);
        for(int i=0;i<8;++i){int actual=cardPage*8+i;if(sample&&actual<(int)slices.size()){auto a=slices[(size_t)actual];padEnvelopes[(size_t)i]=envelope(*sample,a.startSample,a.lengthSamples,256);}else padEnvelopes[(size_t)i].clear();pads[(size_t)i]->repaint();}
        if(countButton){countButton->setName(juce::String((int)slices.size())+" SLICES");countButton->repaint();}
        updateContextControls();updateTabs();wave.repaint();repaint(ref::title.get());
    }
    void changeCardPage(int direction){cardPage+=direction;refreshSample();}
    void openKitEditor(int slot){proc.selectVocalKitSlot(slot);showPanel(std::make_unique<KitEditor>(*this,slot));}
    void updateTabs(){for(int i=0;i<3;++i){browserButtons[(size_t)i]->selected=i==browserTab;browserButtons[(size_t)i]->repaint();rightButtons[(size_t)i]->selected=i==rightTab;rightButtons[(size_t)i]->repaint();}for(int i=0;i<4;++i){workspaceButtons[(size_t)i]->selected=i==workspaceTab;workspaceButtons[(size_t)i]->repaint();}
        if(autoButton){autoButton->selected=proc.getSliceEngine().getMode()!=SliceEngine::Manual;autoButton->repaint();manualButton->selected=!autoButton->selected;manualButton->repaint();}
        if(syncButton){syncButton->selected=proc.getLooper().isTempoSync();syncButton->repaint();}if(fxButton){fxButton->selected=fxEnabled;fxButton->repaint();}if(heart){heart->selected=favourites.contains(currentKey());heart->repaint();}}
    void setRightTab(int tab){rightTab=juce::jlimit(0,2,tab);syncRightPanel();updateTabs();repaint();}
    void bindRightKnob(int i,const char* id,const char* caption,bool useReferenceDefault=false){
        auto& k=knobs[(size_t)i];if(!k)return;knobAttachments[(size_t)i].reset();knobCaptions[(size_t)i]=caption;k->setName(caption);k->setComponentID(id);
        auto* parameter=proc.getAPVTS().getParameter(id);if(!parameter){k->setVisible(false);return;}
        k->setRange(parameter->getNormalisableRange().start,parameter->getNormalisableRange().end);k->setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));
        if(useReferenceDefault)knobAttachments[(size_t)i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.getAPVTS(),ref::parameterIDs[(size_t)i],*k);
        else knobAttachments[(size_t)i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.getAPVTS(),id,*k);
        k->onValueChange=[this,i,parameter]{knobs[(size_t)i]->setTooltip(knobCaptions[(size_t)i]+": "+parameter->getText(parameter->getValue(),24));};
        k->setTooltip(knobCaptions[(size_t)i]+": "+parameter->getText(parameter->getValue(),24)+" - double-click to reset");
    }
    void syncRightPanel(){
        static const char* airIDs[]={"airVocalAir","airVocalBody","airVocalVowel","airVocalBloom","airVocalMotion","airVocalSpace"};
        static const char* airNames[]={"AIR","BODY","VOWEL","BLOOM","MOTION","SPACE"};
        static const char* fxIDs[]={"reverb","delay","filterCutoff","drive"};
        static const char* fxNames[]={"REVERB","DELAY","FILTER","DRIVE"};
        static const char* masterIDs[]={"outputGain","width","macroHype","macroSpace","macroDirt","pumpAmt","pumpRate"};
        static const char* masterNames[]={"OUTPUT","WIDTH","HYPE","SPACE","DIRT","PUMP","RATE"};
        for(int i=0;i<7;++i){
            if(rightTab==0&&proc.isAirVocalMode()&&i<6)bindRightKnob(i,airIDs[i],airNames[i]);
            else if(rightTab==0)bindRightKnob(i,ref::parameterIDs[(size_t)i],ref::captions[(size_t)i],true);
            else if(rightTab==1&&i<4)bindRightKnob(i,fxIDs[i],fxNames[i]);
            else if(rightTab==2)bindRightKnob(i,masterIDs[i],masterNames[i]);
            if(knobs[(size_t)i]){
                const bool isFxKnob = i < 4 && (rightTab == 0 || rightTab == 1);
                const bool knobVisible = (rightTab == 0 && (!proc.isAirVocalMode() || i < 6)) || rightTab == 2 || (rightTab == 1 && i < 4);
                knobs[(size_t)i]->setVisible(knobVisible);
                // The Creative FX power switch is shared by MAIN and FX.  Its
                // bypass state must therefore dim the rings in both tabs;
                // previously MAIN left the rings blue while the FX tab dimmed
                // them, which made the power button look ineffective.
                knobs[(size_t)i]->setAccentEnabled(!isFxKnob || fxEnabled);
            }
        }
        if(fxButton)fxButton->setVisible(rightTab==0||rightTab==1);
        if(loopButton)loopButton->setVisible(rightTab==0);
        overview.setVisible(rightTab==0);
        loopLength.setVisible(rightTab==0);
        if(syncButton)syncButton->setVisible(rightTab==0);
    }
    void showPanel(std::unique_ptr<juce::Component> panel){releaseTyped();keybed.release();overlay=std::move(panel);addAndMakeVisible(*overlay);overlay->setBounds(ref::overlay.get());for(auto* c:workspaceControls)c->setVisible(false);
        closeOverlay=std::make_unique<RefButton>(palette,"X",RefButton::Raised);addAndMakeVisible(*closeOverlay);closeOverlay->setBounds(1138,249,30,27);closeOverlay->onClick=[this]{openWorkspace(0);};repaint();}
    void showActivationPanel(){
        auto safe=juce::Component::SafePointer<Surface>(this);
        auto panel=std::make_unique<UnlockPanel>([safe](juce::String email,juce::String key){return safe&&safe->proc.finalizeActivation(email,key);});
        auto restore=[safe]{juce::MessageManager::callAsync([safe]{if(safe)safe->openWorkspace(0);});};
        panel->onDismiss=restore;
        panel->onActivated=restore;
        showPanel(std::move(panel));
    }
    void openWorkspace(int tab){workspaceTab=juce::jlimit(0,3,tab);rightTab=0;closeOverlay.reset();overlay.reset();for(auto* c:workspaceControls)c->setVisible(true);
        if(tab==2)showPanel(std::make_unique<LooperPanel>(proc));else if(tab==3)showPanel(std::make_unique<DetailPanel>(proc.getAPVTS(),3));updateTabs();repaint();}
    void showDetails(int page){showPanel(std::make_unique<DetailPanel>(proc.getAPVTS(),page));updateTabs();}
    void syncRecordBars(){int bars=proc.getLooper().getRecordBars();loopLength.setSelectedId(bars==1?2:bars==2?3:bars==4?4:bars==8?5:1,juce::dontSendNotification);}
    void toggleFX(){
        // Processor-owned true bypass: the effect values and host automation
        // remain untouched, and the state survives editor recreation.
        setParameter("fxBypass",fxEnabled?1.0f:0.0f);fxEnabled=!fxEnabled;
        dirty=true;syncRightPanel();updateTabs();updateContextControls();repaint();
    }
    void buildCatalog(){catalog.clear();auto names=proc.getDemoSampleNames(),groups=proc.getDemoSampleGroups();for(int i=0;i<names.size();++i)catalog.push_back({0,i,names[i],groups[i]});names=proc.getInstrumentNames();groups=proc.getInstrumentCategories();for(int i=0;i<names.size();++i)catalog.push_back({1,i,names[i],groups[i]});names=proc.getPresetNames();for(int i=0;i<names.size();++i)catalog.push_back({2,i,names[i],"Preset"});names=proc.getUserPresetNames();for(int i=0;i<names.size();++i)catalog.push_back({3,i,names[i],"User"});names=proc.getAirVocalPresetNames();for(int i=0;i<names.size();++i)catalog.push_back({4,i,names[i],"AIR VOCAL"});catalog.push_back({5,0,"Velvet Bloom","Mapped Vocal"});catalog.push_back({5,1,"Prism Vowel","Mapped Vocal"});catalog.push_back({5,2,"Air Dream","Mapped Vocal"});}
    bool home()const{return query.isEmpty()&&selectedCategory.isEmpty();}
    static bool isHatLike(const Sound& a){
        // Vocal Chops is a phrase/preset lane. Keep percussion hat patches
        // out of that lane without removing them from the factory catalogue
        // or the dedicated Drums category.
        return a.name.containsIgnoreCase("hat") || a.group.containsIgnoreCase("hat");
    }
    void refreshBrowser(bool resetScroll=false){
        // Rebuilding the filtered rows and moving the viewport are separate
        // operations.  Instrument/preset selection broadcasts a processor
        // change, so resetting here unconditionally made every click on a
        // lower row jump the browser back to the first item.
        if(resetScroll)rowOffset=0;
        visible.clear();buildCatalog();for(const auto& a:catalog){bool ok=true;
        if(query.isNotEmpty()){
            ok=a.name.containsIgnoreCase(query)||a.group.containsIgnoreCase(query);
            if(browserTab==2)ok=ok&&a.kind==3;
        }
        else if(browserTab==2)ok=a.kind==3;
        else if(selectedCategory=="All Presets")ok=a.kind==1||a.kind==2;
        else if(selectedCategory=="Favorites")ok=favourites.contains(a.key());
        else if(selectedCategory=="Recent")ok=recent.contains(a.key());
        else if(selectedCategory=="Vocal Instruments")ok=a.kind==5;
        else if(selectedCategory=="Vocal Chops")ok=(a.kind==0||(a.kind==2&&a.index<proc.getNumChopPresets()))&&!isHatLike(a);
        else if(selectedCategory=="Vocal Phrases")ok=a.kind==0&&(a.group.containsIgnoreCase("phrase")||a.name.containsIgnoreCase("phrase"));
        else if(selectedCategory=="Adlibs & FX")ok=a.kind==0&&(a.group.containsIgnoreCase("fx")||a.group.containsIgnoreCase("adlib"));
        else if(selectedCategory=="Textures")ok=a.kind==1&&(a.group.containsIgnoreCase("texture")||a.name.containsIgnoreCase("texture"));
        else if(selectedCategory=="Atmospheres")ok=a.kind==1&&(a.group.containsIgnoreCase("pad")||a.name.containsIgnoreCase("ambient"));
        else if(selectedCategory=="AIR VOCAL")ok=a.kind==4;
        else if(selectedCategory.startsWith("group:"))ok=a.kind==0&&a.group==selectedCategory.substring(6);
        else if(selectedCategory.startsWith("instr:"))ok=a.kind==1&&a.group.equalsIgnoreCase(selectedCategory.substring(6));
        else if(selectedCategory.startsWith("genre:")){int bank=selectedCategory.substring(6).getIntValue();ok=false;auto roles=proc.getGenreRoleNames(bank);for(int j=0;j<roles.size();++j)if(proc.getGenreRoleInstruments(bank,j).contains(a.name)&&a.kind==1){ok=true;break;}}
        if(ok)visible.push_back(a);
    }
        int total=(int)visible.size();
        const bool typeGroups=home()&&browserTab==1;
        if(typeGroups){juce::StringArray groups;for(const auto& a:catalog)if(a.kind==1||a.kind==4)groups.addIfNotAlreadyThere(a.group);total=groups.size();}
        const int pageSize=browserTab==2?11:(typeGroups?16:15);
        rowOffset=juce::jlimit(0,juce::jmax(0,total-pageSize),rowOffset);
        updateTabs();browser.repaint();}
    int countKind(int kind)const{int n=0;for(auto& a:catalog)if(a.kind==kind)++n;return n;}
    juce::StringArray homeNames()const{juce::StringArray n{"All Presets","Favorites","Recent","Vocal Instruments","Vocal Chops","Vocal Phrases","Adlibs & FX","Textures","Atmospheres","Genres"};n.addArray(proc.getGenreBankNames());n.add("AIR VOCAL");return n;}
    static float homeY(int i){if(i<3)return i*26.0f; if(i<9)return 99+(i-3)*26.0f;return 280+(i-9)*26.0f;}
    void paintBrowserScrollbar(juce::Graphics& g,int total,int page,float top=38.0f,float height=419.0f){if(total<=page)return;auto track=juce::Rectangle<float>(260,top,5,height);g.setColour(palette.line().withAlpha(.55f));g.fillRoundedRectangle(track,2.5f);const float h=juce::jmax(28.0f,track.getHeight()*page/(float)total);const float y=track.getY()+(track.getHeight()-h)*rowOffset/(float)juce::jmax(1,total-page);g.setColour(palette.accent());g.fillRoundedRectangle({track.getX(),y,track.getWidth(),h},2.5f);text(g,juce::String(rowOffset+1)+"-"+juce::String(juce::jmin(total,rowOffset+page))+" / "+juce::String(total),{147,top-30,106,26},palette.secondary(),9.5f,juce::Justification::centredRight);}
    void paintBrowser(juce::Graphics& g){
        if(browserTab==2){
            auto drop=juce::Rectangle<float>(4,2,258,88);
            card(g,drop,palette,true,7);
            text(g,"YOUR SAMPLE",{16,8,232,18},palette.accent(),12,juce::Justification::centred,.10f);
            text(g,"CLICK TO CHOOSE A SAMPLE",{12,29,242,16},palette.text(),10.5f,juce::Justification::centred);
            text(g,"OR DROP AUDIO ANYWHERE IN SLYCE",{12,46,242,16},palette.text(),10.5f,juce::Justification::centred);
            text(g,"One file maps across the keyboard from C3",{12,65,242,14},palette.secondary(),10.0f,juce::Justification::centred);
            text(g,"SAVED USER PRESETS",{7,102,253,20},palette.secondary(),10.5f,juce::Justification::centredLeft,.08f);
            g.setColour(palette.line());g.drawLine(7,125,260,125,1);
            for(int i=0;i<11&&i+rowOffset<(int)visible.size();++i){const auto& a=visible[(size_t)(i+rowOffset)];auto row=juce::Rectangle<float>(1,132+i*28.0f,265,28);if(a.key()==currentKey()){g.setColour(palette.accent().withAlpha(.12f));g.fillRoundedRectangle(row,4);}smallIcon(g,0,row.withWidth(28),palette.secondary());text(g,a.name,row.withTrimmedLeft(34).withTrimmedRight(9),palette.text(),12);}
            paintBrowserScrollbar(g,(int)visible.size(),11,132.0f,305.0f);
            if(visible.empty()){text(g,"No saved presets yet",{8,158,250,20},palette.secondary(),11.5f,juce::Justification::centred);text(g,"Load a sample above, then press SAVE",{8,180,250,20},palette.secondary(),10.5f,juce::Justification::centred);}
            return;
        }
        if(home()&&browserTab==0){auto names=homeNames();for(int i=0;i<names.size();++i){auto row=juce::Rectangle<float>(0,homeY(i),267,26);const bool selected=(i==3&&proc.isMelodyMode())||(i==4&&proc.isChopMode())||(names[i]=="AIR VOCAL"&&proc.isAirVocalMode()); if(selected){g.setColour(palette.accent().withAlpha(.12f));g.fillRoundedRectangle(row,5);}smallIcon(g,i==1?1:i==2?2:i==4?3:i>=7?5:0,{7,row.getY()+4,20,18},selected?palette.accent():palette.text());
            text(g,names[i],row.withTrimmedLeft(38).withTrimmedRight(38),selected?palette.accent():palette.text(),12.5f);
            juce::String count;if(i==0)count=juce::String(countKind(1)+countKind(2));if(i==1)count=juce::String(favourites.size());if(i==2)count=juce::String(recent.size());if(i==3)count="3";if(i==4){int vocalCount=0;for(const auto& sound:catalog)if((sound.kind==0||(sound.kind==2&&sound.index<proc.getNumChopPresets()))&&!isHatLike(sound))++vocalCount;count=juce::String(vocalCount);}
            if(count.isNotEmpty())text(g,count,row.withTrimmedLeft(222).withTrimmedRight(9),selected?palette.accent():palette.secondary(),12,juce::Justification::centredRight);
            else chevron(g,row.withTrimmedLeft(240),true,palette.secondary());}
            g.setColour(palette.line().withAlpha(.65f));g.drawLine(7,89,263,89,1);g.drawLine(7,269,263,269,1);return;}
        if(home()&&browserTab==1){juce::StringArray groups;for(auto& a:catalog)if(a.kind==1||a.kind==4)groups.addIfNotAlreadyThere(a.group);groups.sort(true);for(int i=0;i<juce::jmin(16,groups.size()-rowOffset);++i){auto group=groups[i+rowOffset];auto row=juce::Rectangle<float>(0,i*28.0f,267,28);smallIcon(g,5,row.withWidth(28),palette.text());text(g,group,row.withTrimmedLeft(38).withTrimmedRight(48),palette.text(),12.5f);int count=0;for(auto& a:catalog)if((a.kind==1||a.kind==4)&&a.group.equalsIgnoreCase(group))++count;text(g,juce::String(count),row.withTrimmedLeft(222).withTrimmedRight(14),palette.secondary(),12,juce::Justification::centredRight);}paintBrowserScrollbar(g,groups.size(),16);return;}
        auto heading=selectedCategory.isEmpty()?(browserTab==2?"User presets":"Search results"):selectedCategory;if(heading.startsWith("group:"))heading=heading.substring(6);if(heading.startsWith("instr:"))heading=heading.substring(6)+" Instruments";if(heading.startsWith("genre:")){auto banks=proc.getGenreBankNames();int n=heading.substring(6).getIntValue();heading=banks[n];}
        text(g,"<  "+heading,{7,0,253,28},palette.secondary(),12);g.setColour(palette.line());g.drawLine(7,31,260,31,1);
        for(int i=0;i<15&&i+rowOffset<(int)visible.size();++i){const auto& a=visible[(size_t)(i+rowOffset)];auto row=juce::Rectangle<float>(1,39+i*28.0f,265,28);if(a.key()==currentKey()){g.setColour(palette.accent().withAlpha(.12f));g.fillRoundedRectangle(row,4);}smallIcon(g,a.kind==0?3:0,row.withWidth(28),palette.secondary());text(g,a.name,row.withTrimmedLeft(34).withTrimmedRight(9),palette.text(),12);}
        paintBrowserScrollbar(g,(int)visible.size(),15);
        if(visible.empty())text(g,browserTab==2?"Save a preset from the ... menu.":"No matching sounds",{8,60,250,60},palette.secondary(),12,juce::Justification::centred);
    }
    void clickBrowser(float y){
        if(browserTab==2){
            if(y<92){loadSample();return;}
            if(y<132)return;
            const int row=((int)y-132)/28+rowOffset;
            if(juce::isPositiveAndBelow(row,(int)visible.size()))chooseSound(visible[(size_t)row]);
            return;
        }
        if(home()&&browserTab==0){auto names=homeNames();for(int i=0;i<names.size();++i)if(y>=homeY(i)&&y<homeY(i)+26){if(i==3){selectedCategory="Vocal Instruments";refreshBrowser(true);return;}if(i==9){showGenreMenu();return;}if(names[i]=="AIR VOCAL"){selectedCategory="AIR VOCAL";refreshBrowser(true);return;}if(i>=10){selectedCategory="genre:"+juce::String(i-10);refreshBrowser(true);return;}selectedCategory=names[i];refreshBrowser(true);return;}}
        if(home()&&browserTab==1){juce::StringArray groups;for(auto& a:catalog)if(a.kind==1||a.kind==4)groups.addIfNotAlreadyThere(a.group);groups.sort(true);int row=(int)y/28+rowOffset;if(juce::isPositiveAndBelow(row,groups.size())){selectedCategory=groups[row]=="AIR VOCAL"?"AIR VOCAL":"instr:"+groups[row];refreshBrowser(true);}return;}
        if(y<33){selectedCategory={};query={};search.clear();refreshBrowser(true);return;}int row=((int)y-39)/28+rowOffset;if(y>=39&&juce::isPositiveAndBelow(row,(int)visible.size()))chooseSound(visible[(size_t)row]);
    }
    void chooseSound(const Sound& a){releaseTyped();keybed.release();selectedPresetName={};selectedCatalogKey={};
        if(a.kind==0){proc.loadDemoSample(a.index);setParameter("engine",0);}else if(a.kind==1)proc.applyInstrument(a.index);else if(a.kind==2){proc.applyPreset(a.index);selectedPresetName=a.name;}else if(a.kind==4){proc.applyAirVocalPreset(a.index);selectedPresetName=a.name;}else if(a.kind==5){proc.loadFactoryVocalKit(a.index);selectedPresetName=a.name;}else {proc.loadUserPreset(a.name);selectedPresetName=a.name;}
        selectedCatalogKey=a.key();selectionEngine=(int)proc.getAPVTS().getRawParameterValue("engine")->load();selectionInstrument=proc.getCurrentInstrument();selectionDemo=proc.getCurrentDemoIndex();dirty=false;updateContextControls();
        recent.removeString(a.key());recent.insert(0,a.key());while(recent.size()>30)recent.remove(recent.size()-1);writeBrowserState();syncRightPanel();refreshSample();browser.repaint();repaint();grabKeyboardFocus();}
    void stepSound(int direction){if(!home()&&!visible.empty()){int index=0;for(int i=0;i<(int)visible.size();++i)if(visible[(size_t)i].key()==currentKey()){index=i;break;}int n=(int)visible.size();chooseSound(visible[(size_t)((index+direction+n)%n)]);return;}
        int kind=proc.isChopMode()||proc.isMelodyMode()?0:1;std::vector<Sound> list;for(auto& a:catalog)if(a.kind==kind)list.push_back(a);if(list.empty())return;int index=kind==0?proc.getCurrentDemoIndex():proc.getCurrentInstrument();index=(juce::jmax(0,index)+direction+(int)list.size())%(int)list.size();chooseSound(list[(size_t)index]);}
    void showGenreMenu(){juce::PopupMenu menu;auto names=proc.getGenreBankNames();for(int i=0;i<names.size();++i)menu.addItem(i+1,names[i]);auto safe=juce::Component::SafePointer<Surface>(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&browser),[safe](int id){if(safe&&id>0){safe->selectedCategory="genre:"+juce::String(id-1);safe->refreshBrowser(true);}});}
    void showSlicePages(){juce::PopupMenu m;int total=proc.isVocalKitMode()?VocalKitEngine::kNumSlots:(int)slices.size();int n=juce::jmax(1,(total+7)/8);for(int i=0;i<n;++i)m.addItem(i+1,(proc.isVocalKitMode()?"Slots ":"Slices ")+juce::String(i*8+1)+" - "+juce::String(juce::jmin(total,(i+1)*8)),true,i==cardPage);auto safe=juce::Component::SafePointer<Surface>(this);m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(countButton),[safe](int id){if(safe&&id>0){safe->cardPage=id-1;safe->refreshSample();}});}
    void sliceMenu(int slice,juce::Component* target){juce::PopupMenu m;m.addItem(1,"Pitch -1 semitone");m.addItem(2,"Pitch +1 semitone");m.addItem(3,"Reset pitch");m.addSeparator();m.addItem(4,"Export slice as WAV...");auto safe=juce::Component::SafePointer<Surface>(this);m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),[safe,slice](int id){if(!safe)return;if(id>=1&&id<=3){safe->proc.setSliceTranspose(slice,id==3?0:safe->proc.getSliceTranspose(slice)+(id==1?-1:1));safe->refreshSample();}else if(id==4)safe->exportSlice(slice);});}
    void kitSlotMenu(int slot,juce::Component* target){auto current=proc.getVocalKit().getSlotSettings(slot);juce::PopupMenu m;m.addItem(1,"Edit sample...");m.addSeparator();m.addItem(2,"Slot Pitch -1 semitone");m.addItem(3,"Slot Pitch +1 semitone");m.addItem(4,"Reset slot pitch");auto safe=juce::Component::SafePointer<Surface>(this);m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),[safe,slot,current](int id)mutable{if(!safe)return;if(id==1){safe->openKitEditor(slot);return;}if(id>=2&&id<=4){current.pitchSemitones=(float)(id==4?0:juce::jlimit(-12.0,12.0,(double)current.pitchSemitones+(id==2?-1.0:1.0)));safe->proc.getVocalKit().setSlotSettings(slot,current);safe->refreshSample();}});}
    void exportSlice(int slice){chooser=std::make_unique<juce::FileChooser>("Export slice",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SLYCE_slice_"+juce::String(slice+1)+".wav"),"*.wav");auto safe=juce::Component::SafePointer<Surface>(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,slice](const juce::FileChooser& c){if(!safe)return;auto f=c.getResult();if(f==juce::File())return;juce::String error;if(!safe->proc.exportSliceToFile(slice,f,error))safe->notify("Export failed",error);});}
    static bool isAudioOrInstrumentFile(const juce::File& file){
        return file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3;sfz");
    }
    void loadSample(bool sfz=false)
    {
        chooser=std::make_unique<juce::FileChooser>(sfz?"Load SFZ instrument":"Load and map one sample across the keyboard",juce::File(),sfz?"*.sfz":"*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        auto safe=juce::Component::SafePointer<Surface>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,sfz](const juce::FileChooser& c){
            if(!safe)return;
            auto f=c.getResult();if(!f.existsAsFile())return;
            juce::String error;
            const bool ok=sfz?safe->proc.loadSfzBank(f,error):safe->proc.loadSampleFromFile(f,false);
            if(ok&&!sfz){safe->setParameter("engine",3.0f);safe->setParameter("arpMode",0.0f);safe->setParameter("pumpAmt",0.0f);} // clean root-C3 mapping; no inherited beat gate
            if(!ok)safe->notify("Unable to load sample",error.isEmpty()?"Unsupported or unreadable audio file.":error);
            safe->selectedPresetName={};safe->selectedCatalogKey={};safe->refreshSample();safe->updateContextControls();safe->repaint();
        });
    }
    void notify(juce::String title,juce::String message){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,title,message,"OK",this);}
    void savePreset(){auto* alert=new juce::AlertWindow("Save user preset","Name this sound",juce::MessageBoxIconType::NoIcon,this);alert->addTextEditor("name",currentTitle(),"Name");alert->addButton("Save",1);alert->addButton("Cancel",0);auto safe=juce::Component::SafePointer<Surface>(this);alert->enterModalState(true,juce::ModalCallbackFunction::create([safe,alert](int id){if(!safe||id!=1)return;auto name=alert->getTextEditorContents("name").trim();if(name.isEmpty())return;if(!safe->proc.saveUserPreset(name))safe->notify("Save failed","The user preset could not be written.");else{safe->dirty=false;safe->selectedPresetName=name;safe->updateContextControls();safe->notify("Saved to USER",name+" is ready in the USER tab.");}safe->refreshBrowser(false);}),true);}
    void editBpm(){auto* alert=new juce::AlertWindow("Recording tempo","Sets the metronome and recording grid. Existing loops keep their recorded length.",juce::MessageBoxIconType::NoIcon,this);alert->addTextEditor("bpm",juce::String(proc.getLooper().getMetroBpm(),0),"BPM (40-240)");alert->addButton("Apply",1);alert->addButton("Cancel",0);auto safe=juce::Component::SafePointer<Surface>(this);alert->enterModalState(true,juce::ModalCallbackFunction::create([safe,alert](int id){if(safe&&id==1){safe->proc.getLooper().setMetroBpm(alert->getTextEditorContents("bpm").getFloatValue());safe->repaint();}}),true);}
    void showMenu(){juce::PopupMenu m;juce::PopupMenu appearance;appearance.addItem(101,"Arctic Blue",true,palette.scheme==0);appearance.addItem(102,"Noir Violet Glass",true,palette.scheme==1);appearance.addItem(103,"Emerald Night",true,palette.scheme==2);appearance.addItem(104,"Rose Pink",true,palette.scheme==3);appearance.addItem(105,"Sunset Carbon",true,palette.scheme==4);appearance.addSeparator();appearance.addItem(106,"Azure Ice Glass",true,palette.scheme==5);appearance.addItem(107,"Ultraviolet Glass",true,palette.scheme==6);appearance.addItem(108,"Carbon Gold",true,palette.scheme==7);appearance.addItem(109,"Arcade Pulse",true,palette.scheme==8);m.addSubMenu("Theme",appearance);m.addSeparator();m.addItem(1,"Load and map one sample...");m.addItem(2,"Load SFZ instrument...");m.addItem(3,"Save user preset...");m.addItem(4,"All sounds / presets");m.addSeparator();
        juce::PopupMenu engines;auto* parameter=proc.getAPVTS().getRawParameterValue("engine");int selected=parameter?(int)parameter->load():0;struct EngineItem{int engine;const char* name;};static constexpr EngineItem engineItems[]={{0,"Chop"},{1,"Instrument"},{2,"Sampled"},{3,"Mapped Sample"},{5,"Air Vocal"}};for(const auto& item:engineItems)engines.addItem(201+item.engine,item.name,true,selected==item.engine);m.addSubMenu("Engine",engines);
        m.addItem(5,"Voice / envelope details");m.addItem(6,"FX / space details");m.addItem(7,"Master / arpeggiator");m.addItem(8,"Synth / modulation");m.addItem(9,"Loop station");m.addItem(10,"Chords");
        juce::PopupMenu slice;const int divisions[]={4,8,16,32,64};for(int i=0;i<5;++i)slice.addItem(301+i,juce::String(divisions[i])+" equal slices");m.addSubMenu("Grid slicing",slice);m.addSeparator();m.addItem(11,proc.isLicensed()?"Activation / licence":"Activate SLYCE...");m.addItem(12,"Build "+juce::String(ref::build));
        auto safe=juce::Component::SafePointer<Surface>(this);m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton),[safe](int id){if(!safe)return;if(id>=101&&id<=109){safe->setTheme(id-101);return;}if(id>=201&&id<=206){safe->releaseTyped();safe->keybed.release();if(id==205&&!safe->proc.getVocalKit().hasSlot(0))safe->proc.loadFactoryVocalKit(0);else if(id==206)safe->proc.applyAirVocalPreset(safe->proc.getCurrentAirVocalPreset());else safe->setParameter("engine",(float)(id-201));safe->syncRightPanel();safe->refreshSample();safe->repaint();return;}
            if(id>=301&&id<=305){static const int div[]={4,8,16,32,64};safe->proc.getSliceEngine().setMode(SliceEngine::Grid);safe->proc.getSliceEngine().setGridDivision(div[id-301]);safe->proc.getSliceEngine().rebuildSlices();safe->refreshSample();return;}
            if(id==1)safe->loadSample();else if(id==2)safe->loadSample(true);else if(id==3)safe->savePreset();else if(id==4){safe->selectedCategory="All Presets";safe->refreshBrowser(true);}else if(id==5||id==8)safe->showDetails(id-5);else if(id==6)safe->setRightTab(1);else if(id==7)safe->setRightTab(2);else if(id==9)safe->openWorkspace(2);else if(id==10){auto panel=std::make_unique<ChordBar>(safe->proc);panel->setSize(790,180);juce::CallOutBox::launchAsynchronously(std::move(panel),ref::keys.get(),safe.getComponent());}else if(id==11)safe->showActivationPanel();else if(id==12)safe->notify("SLYCE Reference UI",juce::String(ref::build)+"\nDedicated reference editor active.\n\nTheme changes the chassis material, glass tint, controls and waveform accent.");});}
    bool isInterestedInFileDrag(const juce::StringArray& paths)override {for(auto& a:paths)if(isAudioOrInstrumentFile(juce::File(a)))return true;return false;}
    void filesDropped(const juce::StringArray& paths,int,int)override
    {
        if(paths.isEmpty())return;
        juce::File f(paths[0]);juce::String error;
        const bool sfz=f.hasFileExtension("sfz");
        const bool ok=sfz?proc.loadSfzBank(f,error):proc.loadSampleFromFile(f,false);
        if(ok&&!sfz){setParameter("engine",3.0f);setParameter("arpMode",0.0f);setParameter("pumpAmt",0.0f);}
        if(!ok)notify("Unable to load file",error.isEmpty()?"The file could not be decoded.":error);
        selectedPresetName={};selectedCatalogKey={};refreshSample();updateContextControls();repaint();
    }
    void mouseDown(const juce::MouseEvent&)override{grabKeyboardFocus();}
    void readBrowserState(){auto f=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Slyce/reference-browser.xml");if(auto xml=juce::parseXML(f)){favourites=juce::StringArray::fromTokens(xml->getStringAttribute("favourites"),",","");recent=juce::StringArray::fromTokens(xml->getStringAttribute("recent"),",","");favourites.removeEmptyStrings();recent.removeEmptyStrings();}}
    void writeBrowserState(){auto f=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Slyce/reference-browser.xml");if(!f.getParentDirectory().createDirectory())return;juce::XmlElement x("SlyceReferenceBrowser");x.setAttribute("favourites",favourites.joinIntoString(","));x.setAttribute("recent",recent.joinIntoString(","));x.writeTo(f);}
private:
    void changeListenerCallback(juce::ChangeBroadcaster*)override {
        if(selectionEngine!=(int)proc.getAPVTS().getRawParameterValue("engine")->load()||selectionInstrument!=proc.getCurrentInstrument()||selectionDemo!=proc.getCurrentDemoIndex()){selectedPresetName={};selectedCatalogKey={};}refreshSample();refreshBrowser(false);syncRecordBars();repaint();}
    void timerCallback()override {
        scanKeys();if(ticks++%3==0){proc.getLooper().readOverview(loopPeaks.data(),(int)loopPeaks.size());overview.repaint();loopButton->selected=proc.getLooper().anyRunning();loopButton->repaint();repaint(ref::bpm.get());}
        if(auto* bypass=proc.getAPVTS().getRawParameterValue("fxBypass")){const bool enabled=bypass->load()<0.5f;if(enabled!=fxEnabled){fxEnabled=enabled;syncRightPanel();updateTabs();repaint();}}
        if(proc.isVocalKitMode()){int selected=juce::jlimit(0,VocalKitEngine::kNumSlots-1,proc.getSelectedSlice());auto latest=proc.getVocalKit().getSlotSample(selected);if(latest.get()!=sample.get())refreshSample();}
        else {auto latest=proc.getLoadedSample();if(latest!=sample||slices.size()!=(size_t)proc.getSliceEngine().getNumSlices())refreshSample();}
        int engine=(int)proc.getAPVTS().getRawParameterValue("engine")->load();if(engine!=lastEngine){lastEngine=engine;releaseTyped();keybed.release();updateContextControls();repaint();}
        int selected=proc.getSelectedSlice();if(selected!=lastSelected){lastSelected=selected;if(proc.isVocalKitMode())refreshSample();else {wave.repaint();for(auto& p:pads)p->repaint();}}
        if(proc.isSynthMode()&&!proc.isMelodyMode()&&!overlay)wave.repaint();
    }
};

SlyceReferenceEditor::SlyceReferenceEditor(VocalChopAudioProcessor& p):AudioProcessorEditor(&p),surface(std::make_unique<Surface>(p)) {
    addAndMakeVisible(*surface);
    setWantsKeyboardFocus(true);

    // Let the host own the editor rectangle.  In particular, FL Studio sends
    // resize requests through the VST3 IPlugView rather than dragging JUCE's
    // resizer.  A fixed-aspect constrainer makes those requests get rejected
    // (or leaves the old rectangle painted), which is why the reference editor
    // used to stop growing in the host.  The surface is scaled-to-fit in
    // resized() below, so dropping the constrainer does not distort the skin.
    setResizable(true, true);
    setResizeLimits(640, 427, 2304, 1536);
    // The supplied reference skin is a 1536x1024 canvas.  Opening at the
    // native canvas size keeps the right FX/master panel and the full keybed
    // visible; resizing down still uses the fixed 1.5 aspect ratio.
    // Coordinates describe a 1536x1024 design canvas.  JUCE component sizes
    // are logical pixels, while Windows may render at 125/150% DPI.  Start at
    // a DPI-compensated logical size so the complete reference canvas, including
    // the right FX/master panel, fits on screen instead of being clipped.
    auto dpiScale = nativeDpiScale(*this);
    if (!(dpiScale > 0.1f)) dpiScale = 1.0f;
    #if JUCE_WINDOWS
    dpiScale=juce::jmax(1.5f,dpiScale);
    #endif
    setSize(juce::jmax(640, juce::roundToInt(ref::width / dpiScale)),
            juce::jmax(427, juce::roundToInt(ref::height / dpiScale)));
    setName("SLYCE Reference UI "+juce::String(ref::build));
}
SlyceReferenceEditor::~SlyceReferenceEditor()=default;
void SlyceReferenceEditor::paint(juce::Graphics& g){g.fillAll(ThemeManager::active().bgBottom);}
void SlyceReferenceEditor::resized(){
    // The reference artwork is authored in a 1536x1024 design coordinate
    // space.  JUCE already gives this component logical (DPI-aware) bounds,
    // so the resize transform must be derived directly from those bounds.
    // Applying the monitor DPI a second time here made the child occupy only
    // part of FL Studio's editor rectangle, leaving an apparently empty frame
    // after a host resize.  Keeping DPI handling in the initial setSize()
    // above and using the actual editor bounds here makes every host resize
    // repaint the entire rectangle.
    const auto available = getLocalBounds().toFloat();
    const float availableW = juce::jmax(1.0f, available.getWidth());
    const float availableH = juce::jmax(1.0f, available.getHeight());
    const float scaleX = juce::jmax(0.1f, availableW
                                            / static_cast<float>(ref::width));
    const float scaleY = juce::jmax(0.1f, availableH
                                            / static_cast<float>(ref::height));

    // The host is intentionally allowed to choose a free aspect ratio.  Fit
    // both axes instead of using the smaller uniform scale: that keeps the
    // reference chassis and its pale/dark background covering the whole host
    // rectangle, with no unpainted strip when FL Studio is resized wider or
    // taller than 3:2.  At the reference aspect ratio these values are equal,
    // so the pixel-accurate layout is unchanged; only a deliberately
    // non-reference host rectangle gets proportionally stretched.
    surface->setTransform(juce::AffineTransform());
    surface->setBounds(0, 0, ref::width, ref::height);
    surface->setTransform(juce::AffineTransform::scale(scaleX, scaleY));
}
bool SlyceReferenceEditor::keyPressed(const juce::KeyPress&){return surface->scanKeys();}
bool SlyceReferenceEditor::keyStateChanged(bool){return surface->scanKeys();}
void SlyceReferenceEditor::setReferenceDark(bool dark){surface->setDark(dark);}
void SlyceReferenceEditor::setReferenceTheme(int scheme){surface->setTheme(scheme);}

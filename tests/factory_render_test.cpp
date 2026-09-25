// Renders the exact production factory architecture (no fake oscillator).
// Use with real JUCE, or the explicitly-labelled math/buffer test shim.
#include "../Source/AudioEngine/FactoryInstruments.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <set>
#include <string>
int main(int argc,char** argv) {
    using namespace slyce::factory;
    const bool extensive = argc>1 && std::string(argv[1])=="--matrix";
    std::cout << "index,category,name,sample_rate,note,velocity,peak,rms,finite,trim_db\n";
    int failures=0,renders=0; std::set<std::string> unique;
    for(int i=0;i<kNumInstruments;++i) {
        if(!unique.insert(kInstruments[i].name).second) { std::cerr<<"duplicate name: "<<kInstruments[i].name<<"\n"; ++failures; }
        for(int rate: {44100,48000}) {
            for(int ni=0;ni<(extensive?3:1);++ni) {
                auto d=voicedDefinition(i); const std::string cat=d.category;
                int note=60; if(cat=="BASS")note=48; if(cat=="DRUMS")note=60;
                if(extensive)note+=(ni-1)*12;
                for(int vi=0;vi<(extensive?2:1);++vi) {
                    const float vel=extensive?(vi==0?0.35f:0.85f):0.78f;
                    SynthEngine engine; engine.prepare({double(rate),256,2});
                    configureInstrument(engine,i); engine.reset(); engine.noteOn(note,vel);
                    juce::AudioBuffer<float> buffer(2,256);
                    const int hold=cat=="PAD"||cat=="MISC"?int(rate*1.4):int(rate*0.65);
                    const int total=hold+int(rate*0.30);
                    double squares=0;float peak=0;bool finite=true;int pos=0;
                    while(pos<total) {
                        int n=std::min(256,total-pos); if(pos<hold)n=std::min(n,hold-pos);
                        if(pos==hold)engine.noteOff(note);
                        buffer.clear();engine.render(buffer,n);
                        for(int ch=0;ch<2;++ch)for(int j=0;j<n;++j) {
                            float x=buffer.getSample(ch,j);finite=finite&&std::isfinite(x);
                            peak=std::max(peak,std::abs(x));squares+=double(x)*x;
                        }
                        pos+=n;
                    }
                    double rms=std::sqrt(squares/(total*2));
                    std::cout<<i<<','<<cat<<','<<d.name<<','<<rate<<','<<note<<','<<vel<<','<<std::setprecision(9)<<peak<<','<<rms<<','<<finite<<','<<levelTrimsDb[i]<<'\n';
                    if(!finite||peak>8||rms<1e-8)++failures;
                    ++renders;
                }
            }
        }
    }

    // The companion bank is a curated contract, not merely twenty appended
    // rows. Four names pre-date the bank and are deliberately upgraded in
    // place so old sessions keep their indices.
    const char* companion[] = {
        "Velvet EP", "Midnight Keys", "Glass EP", "Dusty Keys",
        "Crystal Drop", "Silk Pluck", "Neon Pluck", "Muted Pluck",
        "Pearl Bell", "Dream Music Box", "Wooden Mallet", "Soft Chime",
        "Cloud Pad", "Human Air", "Velvet Choir", "Frozen Voice",
        "Pure Sub", "Velvet Bass", "Slide 808", "Dark Reese",
        "Silk Lead", "Breath Lead", "Liquid Lead", "Warm Analog"
    };
    for (const char* wanted : companion)
    {
        int found = -1;
        for (int i = 0; i < kNumInstruments; ++i)
            if (std::string (kInstruments[i].name) == wanted)
                found = found < 0 ? i : -2;
        if (found < 0)
        {
            std::cerr << "companion preset missing/duplicate: " << wanted << "\n";
            ++failures;
            continue;
        }

        SynthEngine engine;
        engine.prepare ({48000.0, 256, 2});
        configureInstrument (engine, found);
        engine.reset();
        auto& patch = engine.patch();
        const std::string nm (wanted);
        const bool bass = nm == "Pure Sub" || nm == "Velvet Bass"
                       || nm == "Slide 808" || nm == "Dark Reese";
        if (bass && ! patch.monoMode.load())
        {
            std::cerr << "companion bass is not mono: " << wanted << "\n";
            ++failures;
        }

        // Chord/poly path, or the mono last-note-priority equivalent. A note
        // released out of order must never leave an infinite/stuck signal.
        juce::AudioBuffer<float> b (2, 256);
        const int root = bass ? 40 : 60;
        engine.noteOn (root, 0.35f);
        engine.noteOn (root + 4, 0.85f);
        engine.noteOn (root + 7, 0.62f);
        for (int k = 0; k < 24; ++k) { b.clear(); engine.render (b, 256); }
        engine.noteOff (root + 7);
        engine.noteOff (root);
        engine.noteOff (root + 4);
        bool finite = true;
        float tailPeak = 0.0f;
        for (int k = 0; k < 900; ++k)
        {
            b.clear(); engine.render (b, 256);
            if (k > 850)
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < 256; ++n)
                    {
                        const float x = b.getSample (ch, n);
                        finite = finite && std::isfinite (x);
                        tailPeak = std::max (tailPeak, std::abs (x));
                    }
        }
        if (! finite || tailPeak > 0.02f)
        {
            std::cerr << "companion release/finite failure: " << wanted
                      << " tail=" << tailPeak << "\n";
            ++failures;
        }
    }
    std::cerr<<"Factory renders="<<renders<<", instruments="<<kNumInstruments<<", failures="<<failures<<"\n";
    return failures?1:0;
}

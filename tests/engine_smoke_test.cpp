// Can be built against real JUCE with tests/CMakeLists.txt.
// In the delivered report, distinguish SDK tests from local-shim smoke tests.
#include <juce_dsp/juce_dsp.h>
#include "AudioEngine/VoicePool.h"
#include "AudioEngine/SynthEngine.h"
#include "AudioEngine/SamplerEngine.h"
#include "AudioEngine/GranularEngine.h"
#include "AudioEngine/FXChain.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::atomic<bool> countAllocations { false };
static std::atomic<size_t> allocationCount { 0 };
void* operator new (std::size_t n)
{
    if (countAllocations.load(std::memory_order_relaxed)) ++allocationCount;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void*p) noexcept { ::operator delete(p); }
void operator delete[](void*p,std::size_t) noexcept { ::operator delete(p); }
static int passed=0;
static void check(bool ok,const char* name)
{ std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)std::exit(1);++passed; }
static bool finite(const juce::AudioBuffer<float>& b)
{for(int c=0;c<b.getNumChannels();++c)for(int n=0;n<b.getNumSamples();++n)if(!std::isfinite(b.getSample(c,n)))return false;return true;}
int main()
{
    constexpr double sr=48000.;
    const juce::dsp::ProcessSpec spec {sr,256,2};
    auto source=std::make_shared<juce::AudioBuffer<float>>(2,48000);
    for(int n=0;n<48000;++n){source->setSample(0,n,.75f);source->setSample(1,n,-.75f);}
    juce::AudioBuffer<float> block(2,256);
    Voice voice;
    slyce::quality::SampleReader::warmUp();
    voice.start(sr,source,sr,0,48000,0,1,0,0,1,0,false,false);
    block.clear();voice.render(block,256);
    check(block.getSample(0,0)==0&&std::abs(block.getSample(0,255)-.75f)<1e-6,"chop zero-attack has a short click-safe onset");
    const float previous=block.getSample(0,255);
    voice.start(sr,source,sr,0,48000,12,1,0,0,1,0,true,false);
    block.clear();voice.render(block,256);
    check(std::abs(block.getSample(0,0)-previous)<1e-6,"stolen chop voice starts continuously");
    voice.release();block.clear();voice.render(block,256);
    check(!voice.isActive()&&std::abs(block.getSample(0,255))<1e-7,"chop note-off reaches silence with a minimum release");
    juce::AudioBuffer<float> empty;
    voice.render(empty,1);
    check(true,"zero-channel chop buffer is accepted");

    VoicePool pool;pool.prepare(spec);pool.setSource(source,sr);
    for(int n=0;n<24;++n)pool.triggerVoice(0,48000,.5f,0);
    pool.prepare(spec);block.clear();pool.renderNextBlock(block,256);
    check(block.getMagnitude(0,256)==0,"re-prepare clears all chop voices, including one-shots");

    SamplerEngine sampler;sampler.prepare(sr,256);sampler.loadFromBuffer(*source,sr,"test",60);
    sampler.noteOn(72,.7f);block.clear();sampler.render(block,256);
    check(finite(block)&&block.getMagnitude(0,256)>0.1f,"melodic/multisample in-memory playback renders pitched audio");
    juce::AudioBuffer<float> shortSample(1,1);shortSample.setSample(0,0,.5f);
    sampler.loadFromBuffer(shortSample,sr,"one sample",60);sampler.noteOn(127,1);
    block.clear();sampler.render(block,256);
    check(finite(block),"one-sample banks and extreme pitch ratios remain finite");

    SynthEngine synth;synth.prepare(spec);synth.patch().resetToInit();
    synth.patch().stringMix=.9f;synth.patch().stringDamp=.55f;synth.patch().bodyType=1;synth.patch().bodyAmount=.3f;
    synth.patch().driftCents=0;synth.setEnvelope(0,50,.7f,2);
    allocationCount=0;countAllocations=true;
    for(int n=0;n<64;++n){synth.noteOn(36+n%48,.8f);block.clear();synth.render(block,256);synth.noteOff(36+n%48);}
    countAllocations=false;
    std::printf("MEASURE string note-on/render allocations: %zu\n",allocationCount.load());
    check(allocationCount==0,"physical-string note-on and render do not allocate in this harness");
    check(finite(block)&&block.getMagnitude(0,256)>1e-5,"polyphonic physical strings render finite, non-silent audio");
    juce::AudioBuffer<float> large(2,4096);large.clear();synth.render(large,4096);
    check(finite(large)&&large.getMagnitude(0,4096)>1e-5,"synth oversized host buffers are processed rather than dropped");
    synth.reset();block.clear();synth.render(block,256);
    check(block.getMagnitude(0,256)==0,"synth reset clears physical body and chorus histories");

    bool sampleRates=true;
    for(double rate:{32000.,44100.,48000.,96000.,192000.})
    {
        SynthEngine x;x.prepare({rate,256,2});x.setEnvelope(1,50,.5f,10);
        for(int family=0;family<5;++family)
        {
            x.patch().resetToInit();x.patch().unison=3;
            if(family==0){x.patch().stringMix=.85f;x.patch().bodyType=0;x.patch().bodyAmount=.3f;}
            if(family==1){x.patch().formantVowel=2;x.patch().formantAmount=.7f;x.patch().chorusMix=.3f;}
            if(family==2){x.patch().fmAmount=.8f;x.patch().fmRatio=3.5f;x.patch().inharmonic=.4f;}
            if(family==3){x.patch().subLevel=.7f;x.patch().pitchEnvOct=4;}
            if(family==4){x.patch().bodyType=3;x.patch().bodyAmount=.9f;x.patch().filterQ=7;}
            for(int note:{24,60,96,127}){x.noteOn(note,.8f);block.clear();x.render(block,256);sampleRates=sampleRates&&finite(block);x.noteOff(note);}
        }
    }
    check(sampleRates,"five synth families / five sample rates / four registers stay finite");
    synth.prepare(spec);synth.patch().resetToInit();synth.patch().bodyType=0;synth.patch().bodyAmount=.8f;synth.patch().satAmount=0;
    synth.patch().chorusMix=0;synth.setEnvelope(.5,10,.6,2);synth.noteOn(48,1);
    block.clear();synth.render(block,256);synth.noteOff(48);block.clear();synth.render(block,256);
    block.clear();synth.render(block,256);
    check(block.getMagnitude(0,256)>1e-8,"body resonance continues after the last voice finishes");

    GranularEngine grain;grain.prepare(spec);grain.setMix(.6f);
    for(int k=0;k<30;++k){for(int c=0;c<2;++c)for(int n=0;n<256;++n)block.setSample(c,n,.3f);grain.process(block);}
    grain.setMix(0);for(int k=0;k<600;++k){block.clear();grain.process(block);}
    grain.setMix(.6f);float stalePeak=0;
    for(int k=0;k<100;++k){block.clear();grain.process(block);stalePeak=juce::jmax(stalePeak,block.getMagnitude(0,256));}
    check(stalePeak<1e-8,"Texture re-enable does not replay grains from before a long bypass");
    DelayFX delay;delay.prepare(spec);delay.setTimeSeconds(.020f);delay.setFeedback(.3f);
    double phase=0;float last=0;double largest=0;
    for(int k=0;k<160;++k)
    {
        if(k==100)delay.setTimeSeconds(.047f);
        for(int n=0;n<256;++n){float x=.2f*std::sin((float)phase);phase+=2*3.14159265358979323846*150/sr;block.setSample(0,n,x);block.setSample(1,n,x);}
        delay.process(block,.6f);
        for(int n=0;n<256;++n){float x=block.getSample(0,n);if(k>=99&&k<=110)largest=std::max(largest,(double)std::abs(x-last));last=x;}
    }
    std::printf("MEASURE delay-change maximum sample step: %.8f\n",largest);
    check(largest<.02,"delay time change uses a bounded crossfade rather than a tap jump");
    delay.process(empty,.5f);check(true,"zero-channel delay buffer is accepted");
    ReverbFX reverb;reverb.prepare(spec);
    for(int c=0;c<2;++c)for(int n=0;n<256;++n)block.setSample(c,n,.1f);
    reverb.process(block,0);bool unchanged=true;
    for(int c=0;c<2;++c)for(int n=0;n<256;++n)unchanged=unchanged&&block.getSample(c,n)==.1f;
    reverb.process(empty,.5f);
    check(unchanged,"reverb zero amount is an exact bypass; empty input is safe");
    std::printf("TOTAL: %d passed, 0 failed\n",passed);
}

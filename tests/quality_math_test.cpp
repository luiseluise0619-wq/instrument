#include "SoundQuality.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <limits>
#include <random>
using namespace slyce::quality;
int passed = 0;
void check (bool ok, const char* label)
{
    std::printf ("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (! ok) std::exit (1);
    ++passed;
}
double rms (const std::vector<float>& v)
{
    double s = 0.0;
    for (float x : v) s += x * (double) x;
    return std::sqrt (s / std::max ((size_t) 1, v.size()));
}
int main()
{
    check (exclusiveLoopEnd (0) == 1 && exclusiveLoopEnd (1023) == 1024
           && exclusiveLoopEnd (-1) == 0
           && exclusiveLoopEnd (std::numeric_limits<int>::max()) == std::numeric_limits<int>::max(),
           "SFZ inclusive loop endpoints convert safely to half-open ranges");
    SampleReader::warmUp();
    SampleReader reader;
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> left (48000), right (48000);
    std::mt19937 rng (27);
    for (size_t i = 0; i < left.size(); ++i)
    { left[i] = (int) (rng() % 2001) / 1000.0f - 1.0f; right[i] = -left[i]; }
    reader.setRatio (1.0);
    bool exact = true;
    for (int i = 0; i < 48000; i += 13)
    {
        float l,r; reader.readStereo (left.data(), right.data(), i, 0, 48000, false, l, r);
        exact = exact && l == left[(size_t) i] && r == right[(size_t) i];
    }
    check (exact, "native-rate integer reads are sample-exact");
    std::fill (left.begin(), left.end(), 0.5f);
    std::fill (right.begin(), right.end(), -0.5f);
    bool unity = true;
    for (double rate : {0.25, 0.5, 1.0, 1.1, 2.0, 4.0, 8.0, 64.0})
    {
        reader.setRatio (rate);
        for (double pos : {0.0, 0.5, 1.7, 117.25, 47999.0})
        {
            float l,r; reader.readStereo (left.data(), right.data(), pos, 0, 48000, false, l,r);
            unity = unity && std::abs (l - .5f) < 2e-6 && std::abs (r + .5f) < 2e-6;
        }
    }
    check (unity, "DC gain and stereo kernel agreement across eight playback rates");
    reader.setRatio (1.3);
    std::fill (left.begin(), left.end(), 100.0f);
    std::fill (left.begin() + 100, left.begin() + 110, 0.1f);
    bool bounded = true;
    for (double p = 100.0; p < 110; p += .13)
    {
        float l,r; reader.readStereo (left.data(), nullptr, p, 100, 110, false,l,r);
        bounded = bounded && std::abs (l - .1f) < 2e-6 && l == r;
    }
    check (bounded, "short slices never read adjacent syllables; mono duplicates coherently");
    bool shortLoops = true;
    for (int length : {1,2,3,5,7})
        for (double p : {-10000.3, -0.5, 0.0, 99.8, 100000.2})
        {
            float l,r; reader.readStereo (left.data(), nullptr,p,100,100+length,true,l,r);
            shortLoops = shortLoops && std::abs (l - .1f) < 2e-6;
        }
    check (shortLoops, "short loop wrapping is safe for negative and multi-wrap positions");
    float a,b;
    reader.readStereo (nullptr,nullptr,0,0,0,false,a,b);
    check (a == 0 && b == 0, "empty sample read is silent");
    reader.readStereo (left.data(),nullptr,std::numeric_limits<double>::quiet_NaN(),0,10,false,a,b);
    check (a == 0 && b == 0, "invalid read position is silent");

    auto rendered = [&] (double hz, double rate, bool bandlimited)
    {
        for (size_t i=0;i<left.size();++i) left[i] = (float) std::sin (2*pi*hz*i/48000.0);
        reader.setRatio (rate);
        std::vector<float> out;
        for (double pos=128.0;pos<47000.0;pos+=rate)
        {
            float l,r;
            if (bandlimited) reader.readStereo(left.data(),nullptr,pos,0,48000,false,l,r);
            else { int i=(int)pos; float f=(float)(pos-i); l=left[i]+f*(left[i+1]-left[i]); }
            out.push_back(l);
        }
        return rms(out);
    };
    const double clean = rendered (5000.0,2.0,true);
    check (std::abs (clean - std::sqrt (.5)) < .005, "5 kHz source at +12 semitones retains passband level");
    const double oldAlias = rendered(16000.0,2.0,false), newAlias = rendered(16000.0,2.0,true);
    const double reduction = 20*std::log10 (oldAlias / std::max (newAlias,1e-12));
    std::printf ("MEASURE alias rejection improvement: %.2f dB (16 kHz source, 48 kHz host, +12 semitones)\n",reduction);
    check (reduction > 45.0,"upward resampling suppresses this above-Nyquist tone by >45 dB vs linear");
    const double newHigh = rendered(18000,1.5,true), oldHigh=rendered(18000,1.5,false);
    std::printf("MEASURE noninteger-rate alias improvement: %.2f dB\n",20*std::log10(oldHigh/std::max(newHigh,1e-12)));
    check(newHigh < oldHigh*.03,"noninteger upward resampling suppresses the above-Nyquist test tone");

    Declicker d; a=.8f; b=-.4f; d.process(a,b); d.begin(144);
    a=b=0; d.process(a,b);
    check(std::abs(a-.8f)<1e-7&&std::abs(b+.4f)<1e-7,"voice-steal correction starts at the previous output");
    float last=a; double maxStep=0;
    for(int n=0;n<150;++n){a=b=0;d.process(a,b);maxStep=std::max(maxStep,(double)std::abs(a-last));last=a;}
    check(a==0&&b==0&&maxStep<.01,"declick tail reaches zero smoothly within its bounded duration");
    a=.25f;b=.1f;d.process(a,b);d.begin(128,-.3f,-.2f);a=-.3f;b=-.2f;d.process(a,b);
    check(std::abs(a-.25f)<1e-6&&std::abs(b-.1f)<1e-6,"loop-wrap correction starts continuously without changing loop length");
    bool shape=true;float previous=0;
    for(int n=0;n<=1000;++n){float v=smoothstep(n/1000.f);shape=shape&&v>=previous&&v>=0&&v<=1;previous=v;}
    check(shape&&smoothstep(-1)==0&&smoothstep(2)==1,"edge-fade curve is monotonic and clamped");

    const double omega=2*pi*620.0/48000.0, alpha=std::sin(omega)/(2*20.0), a0=1+alpha;
    const float b0=(float)(alpha/a0), a1=(float)(-2*std::cos(omega)/a0), a2=(float)((1-alpha)/a0);
    float z1=0,z2=0, x1=0,x2=0,y1=0,y2=0;double maxError=0,sum=0;
    for(int n=0;n<96000;++n)
    {
        const float x=n==0?1.f:0.f;
        const float y=bodyBandPass(x,b0,a1,a2,z1,z2);
        const float ref=b0*x-b0*x2-a1*y1-a2*y2;
        x2=x1;x1=x;y2=y1;y1=ref;
        maxError=std::max(maxError,(double)std::abs(y-ref));sum+=y;
    }
    check(maxError<2e-5,"body resonator agrees with an independent direct-form-I reference");
    check(std::abs(sum)<1e-4,"body bandpass has near-zero DC response");
    check(std::abs(z1)<1e-10&&std::abs(z2)<1e-10,"body impulse tail decays to silence");
    bool phase=true;
    for(double sr:{32000.,44100.,48000.,96000.,192000.})
        for(double hz:{55.,110.,220.,440.,880.,1760.})
            for(float damping:{.15f,.35f,.55f,.85f})
            {
                double w=2*pi*hz/sr,pole=1-damping;
                double totalPhase=w*stringDelay(sr,hz,damping)+std::atan2(pole*std::sin(w),1-pole*std::cos(w));
                phase=phase&&std::abs(totalPhase-2*pi)<1e-5;
            }
    check(phase,"physical-string loop compensates damping phase at six notes / five sample rates");
    check(nyquistGain(.1)==1&&nyquistGain(.5)==0&&nyquistGain(10)==0,
          "oscillator guard removes out-of-band fundamentals");
    std::printf("TOTAL: %d passed, 0 failed\n",passed);
}

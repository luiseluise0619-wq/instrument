#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "LoopPages.h"
#include "../DSP/BoundedQueue.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <vector>

/** Six-track performance looper.
    - One ordered, bounded multi-producer command queue (UI and MIDI).
    - Copy-on-write audio pages: Undo is an immutable take, not a guessed range.
    - Import prepares private pages off the callback; adoption is a swap.
    - Export/state retains page references briefly, then copies PCM off-thread.
    The small state mutex is held while rendering a block and while capturing
    page references, NOT while resampling, encoding or copying the PCM export.
    This is not a claim of hard real-time lock freedom: snapshot acquisition
    can briefly contend with a callback. No heap allocation/PCM-wide copying
    is performed by ordinary record, playback, overdub, or Undo processing.
*/
class LoopStation {
public:
    enum State { Empty=0, Recording, Playing, Overdub, Stopped, Armed };
    static constexpr int kNumTracks=6;
    static constexpr double kMaxSeconds=30.0;
    static constexpr double kMaxExportSeconds=120.0;
    // A freshly recorded/imported take must come back at the level at which
    // it entered the looper. Peak protection belongs to the processor's
    // post-looper limiter; pre-attenuating every track here made a single
    // loop audibly quieter even when there was no risk of clipping.
    static constexpr float kDefaultTrackGain=1.0f;

private:
    struct Track {
        slyce::LoopPages loop, undo;
        std::atomic<int> state{Empty}, lenSamples{0}, layers{0};
        std::atomic<float> volume{kDefaultTrackGain}, pan{0.0f}, uiPos{0.0f};
        std::atomic<bool> muted{false}, reversed{false}, undoAvail{false}, redoState{false};
        int recPos=0, targetLen=0, armCountdown=0, dubSamples=0, undoLayers=0;
        std::int64_t startStamp=0;
        float gainL=kDefaultTrackGain, gainR=kDefaultTrackGain;
    };
    struct ImportPayload {
        slyce::LoopPages samples;
        std::shared_ptr<slyce::LoopPagePool> pool;
        int length=0;
        ImportPayload* retiredNext=nullptr;
    };
    // kind 1..6 = per-track commands, 10..12 = master Stop/Play/Clear.
    struct Command { int kind=0, track=0; ImportPayload* payload=nullptr; };
    struct TrackSnapshot {
        slyce::LoopPages audio;
        int length=0, state=Empty, layers=0;
        float volume=kDefaultTrackGain, pan=0.0f;
        bool muted=false, reversed=false;
        std::int64_t startStamp=0;
    };
public:
    struct Snapshot {
        double getSampleRate() const noexcept { return rate; }
    private:
        friend class LoopStation;
        std::shared_ptr<slyce::LoopPagePool> pool;
        std::array<TrackSnapshot,kNumTracks> tracks;
        double rate=44100.0;
        float bpm=120.0f;
        bool metro=false, tempoSync=false;
        int masterLength=0, capacity=0, recordBars=0;
        std::int64_t transport=0, masterStamp=0;
    };

    LoopStation()=default;
    ~LoopStation() {
        Command c;
        while(commands.pop(c)) delete c.payload;
        collectRetired();
    }
    LoopStation(const LoopStation&)=delete;
    LoopStation& operator=(const LoopStation&)=delete;

    // Called while the owner is stopped/reconfiguring its callback, like a
    // normal JUCE prepareToPlay(). Same-rate prepares do not erase anything.
    void prepare(double sampleRate,int /*maxBlock*/) {
        const double nextRate=std::isfinite(sampleRate) && sampleRate>=8000.0 && sampleRate<=384000.0
                                ? sampleRate : 44100.0;
        std::unique_ptr<Snapshot> old;
        { std::lock_guard<std::recursive_mutex> guard(stateMutex);
          if(pool && std::abs(nextRate-srHz)<0.001) return; }
        if(pool) old=captureSnapshot();
        const int capacity=static_cast<int>(std::ceil(nextRate*kMaxSeconds));
        const int pages=(capacity+slyce::LoopPagePool::framesPerPage-1)/slyce::LoopPagePool::framesPerPage;
        // Live + undo + one exported snapshot + prepared imports/restore.
        auto replacement=std::make_shared<slyce::LoopPagePool>(static_cast<std::uint32_t>(pages*kNumTracks*4+32));
        std::array<slyce::LoopPages,kNumTracks> newAudio,newUndo;
        std::array<int,kNumTracks> lengths{};
        for(int ti=0;ti<kNumTracks;++ti){
            newAudio[ti]=slyce::LoopPages(replacement,capacity);
            newUndo[ti]=slyce::LoopPages(replacement,capacity);
            if(old && old->tracks[ti].length>0){
                const auto& from=old->tracks[ti];
                const int len=std::min(capacity,std::max(1,static_cast<int>(std::llround(from.length*nextRate/old->rate))));
                lengths[ti]=len;
                for(int n=0;n<len;++n){
                    const double x=std::min(static_cast<double>(from.length-1),n*old->rate/nextRate);
                    const int i=static_cast<int>(x),j=std::min(from.length-1,i+1);
                    const float f=static_cast<float>(x-i);
                    if(!newAudio[ti].set(n,from.audio.get(0,i)+(from.audio.get(0,j)-from.audio.get(0,i))*f,
                                          from.audio.get(1,i)+(from.audio.get(1,j)-from.audio.get(1,i))*f))
                        throw std::bad_alloc();
                }
            }
        }
        std::lock_guard<std::recursive_mutex> guard(stateMutex);
        // Pending imports are tied to the old pool/rate; never publish them in
        // the new one. A host device reconfiguration is an explicit boundary.
        Command c;while(commands.pop(c)) delete c.payload;
        pool=std::move(replacement);maxLenSamples=capacity;srHz=nextRate;
        publishedRate.store(nextRate);publishedCapacity.store(capacity);
        transport=old ? static_cast<std::int64_t>(std::llround(old->transport*nextRate/old->rate)) : 0;
        masterStamp=old ? static_cast<std::int64_t>(std::llround(old->masterStamp*nextRate/old->rate)) : 0;
        masterLen=old ? static_cast<int>(std::llround(old->masterLength*nextRate/old->rate)) : 0;
        for(int ti=0;ti<kNumTracks;++ti){
            auto& t=tracks[ti];t.loop.swap(newAudio[ti]);t.undo.swap(newUndo[ti]);
            t.lenSamples.store(lengths[ti]);t.undoAvail.store(false);t.redoState.store(false);
            t.recPos=t.targetLen=t.armCountdown=t.dubSamples=0;
            if(old){
                const auto& from=old->tracks[ti];
                t.layers.store(from.layers);t.volume.store(from.volume);t.pan.store(from.pan);
                t.muted.store(from.muted);t.reversed.store(from.reversed);
                t.startStamp=static_cast<std::int64_t>(std::llround(from.startStamp*nextRate/old->rate));
                t.state.store(lengths[ti] ? (from.state==Playing || from.state==Overdub ? Playing : Stopped) : Empty);
            }else {t.state.store(Empty);t.layers.store(0);t.startStamp=0;}
            setImmediateGain(t);t.uiPos.store(0.0f);
        }
        clickDecay=static_cast<float>(std::exp(-1.0/(0.012*srHz)));
        gainSlew=static_cast<float>(1.0-std::exp(-1.0/(0.004*srHz)));
        metroCountdown=0;metroBeat=0;clickEnv=clickPhase=0;
        collectRetired();
    }

    bool tapMain(int track){return push(1,track);}
    bool tapClear(int track){return push(2,track);}
    bool tapUndo(int track){return push(3,track);}
    bool tapReRecord(int track){return push(4,track);}
    bool tapStopTrack(int track){return push(6,track);}
    bool tapStopAll(){return push(10,0);}
    bool tapPlayAll(){return push(11,0);}
    bool tapClearAll(){return push(12,0);}
    std::uint64_t getDroppedCommandCount() const noexcept {return rejectedCommands.load();}
    std::uint64_t getMemoryFailureCount() const noexcept {return memoryFailures.load();}

    bool importAudio(int track,const juce::AudioBuffer<float>& src,double srcRate){
        if(!validTrack(track) || src.getNumChannels()<1 || src.getNumSamples()<1 || !std::isfinite(srcRate) || srcRate<=0) return false;
        collectRetired();
        std::shared_ptr<slyce::LoopPagePool> currentPool;double rate;int cap;
        {std::lock_guard<std::recursive_mutex> guard(stateMutex);currentPool=pool;rate=srHz;cap=maxLenSamples;}
        if(!currentPool) return false;
        const double wanted=static_cast<double>(src.getNumSamples())*rate/srcRate;
        // Do not silently truncate an imported song to thirty seconds.
        if(!std::isfinite(wanted) || wanted<32 || wanted>cap+0.5) return false;
        auto pending=std::make_unique<ImportPayload>();
        pending->pool=currentPool;pending->length=static_cast<int>(std::llround(wanted));
        pending->samples=slyce::LoopPages(currentPool,cap);
        for(int n=0;n<pending->length;++n){
            const double x=std::min(static_cast<double>(src.getNumSamples()-1),n*srcRate/rate);
            const int i=static_cast<int>(x),j=std::min(i+1,src.getNumSamples()-1);
            const float f=static_cast<float>(x-i);
            auto sample=[&](int ch){const int c=std::min(ch,src.getNumChannels()-1);return src.getSample(c,i)+(src.getSample(c,j)-src.getSample(c,i))*f;};
            const float l=sample(0),r=sample(1);
            if(!std::isfinite(l) || !std::isfinite(r) || !pending->samples.set(n,l,r)) return false;
        }
        if(!commands.push(Command{5,track,pending.get()})){rejectedCommands.fetch_add(1);return false;}
        pending.release();return true;
    }

    void setMetronomeOn(bool on){if(on && !metroOn.load())metroReset.store(true);metroOn.store(on);}
    bool isMetronomeOn()const{return metroOn.load();}
    void setMetroBpm(float bpm){if(std::isfinite(bpm))metroBpm.store(std::clamp(bpm,40.0f,240.0f));}
    float getMetroBpm()const{return metroBpm.load();}
    void setTempoSync(bool on){tempoSync.store(on);}
    bool isTempoSync()const{return tempoSync.load();}
    // Called by the PROCESSOR, never dependent on an editor timer. This syncs
    // the click/record grid; it deliberately does NOT time-stretch stored audio.
    void updateHostTempo(double bpm,double ppq,bool playing){
        if(tempoSync.load() && std::isfinite(bpm) && bpm>=40 && bpm<=240){
            setMetroBpm(static_cast<float>(bpm));
            if(playing && std::isfinite(ppq) && ppq>=0){
                std::lock_guard<std::recursive_mutex> guard(stateMutex);
                const double beat=std::floor(ppq),fraction=ppq-beat;
                metroCountdown=fraction<1e-9 ? 0.0 : (1.0-fraction)*srHz*60.0/bpm;
                metroBeat=static_cast<int>(static_cast<std::int64_t>(beat)+(fraction<1e-9?0:1))%4;
            }
        }
    }
    void setRecordBars(int bars){recordBars.store(bars==1 || bars==2 || bars==4 || bars==8 ? bars : 0);}
    int getRecordBars()const{return recordBars.load();}
    void setMuted(int t,bool m){if(validTrack(t))tracks[t].muted.store(m);}
    bool isMuted(int t)const{return validTrack(t) && tracks[t].muted.load();}
    void setReversed(int t,bool r){if(validTrack(t))tracks[t].reversed.store(r);}
    bool isReversed(int t)const{return validTrack(t) && tracks[t].reversed.load();}
    void setPan(int t,float p){if(validTrack(t)&&std::isfinite(p))tracks[t].pan.store(std::clamp(p,-1.0f,1.0f));}
    float getPan(int t)const{return validTrack(t)?tracks[t].pan.load():0.0f;}
    void setTrackVolume(int t,float v){if(validTrack(t)&&std::isfinite(v))tracks[t].volume.store(std::clamp(v,0.0f,1.5f));}
    float getTrackVolume(int t)const{return validTrack(t)?tracks[t].volume.load():0.0f;}
    int getTrackState(int t)const{return validTrack(t)?tracks[t].state.load():Empty;}
    int getTrackLayers(int t)const{return validTrack(t)?tracks[t].layers.load():0;}
    float getTrackPosition(int t)const{return validTrack(t)?tracks[t].uiPos.load():0.0f;}
    bool canUndo(int t)const{return validTrack(t)&&tracks[t].undoAvail.load();}
    bool isRedo(int t)const{return validTrack(t)&&tracks[t].redoState.load();}
    double getSampleRate()const{return publishedRate.load();}
    double getMaxRecordSeconds()const{return kMaxSeconds;}
    bool anyContent()const{for(const auto& t:tracks)if(t.lenSamples.load()>0 || t.state.load()==Recording)return true;return false;}
    bool anyRunning()const{for(const auto& t:tracks){const int s=t.state.load();if(s==Recording||s==Playing||s==Overdub)return true;}return false;}

    bool readOverview(float* peaks,int bins)const {
        if(!peaks||bins<1||bins>512)return false;
        std::unique_lock<std::recursive_mutex> lock(stateMutex,std::try_to_lock);
        if(!lock.owns_lock())return false;
        std::fill(peaks,peaks+bins,0.0f);
        for(const auto& t:tracks){const int length=t.lenSamples.load();if(length<=0)continue;
            for(int b=0;b<bins;++b)for(int k=0;k<12;++k){
                const int n=std::min(length-1,int((static_cast<std::int64_t>(b)*12+k)*length/(bins*12)));
                const float x=std::max(std::abs(t.loop.get(0,n)),std::abs(t.loop.get(1,n)));
                peaks[b]=std::max(peaks[b],std::min(1.0f,x));
            }
        }
        return true;
    }

    std::unique_ptr<Snapshot> captureSnapshot()const{
        for(int attempt=0;attempt<3;++attempt){
            std::shared_ptr<slyce::LoopPagePool> currentPool;int cap;
            {std::lock_guard<std::recursive_mutex> guard(stateMutex);currentPool=pool;cap=maxLenSamples;}
            if(!currentPool)return {};
            auto result=std::make_unique<Snapshot>();result->pool=currentPool;result->capacity=cap;
            for(auto& t:result->tracks)t.audio=slyce::LoopPages(currentPool,cap);
            std::lock_guard<std::recursive_mutex> guard(stateMutex);
            if(pool!=currentPool)continue;
            result->rate=srHz;result->bpm=metroBpm.load();result->metro=metroOn.load();result->tempoSync=tempoSync.load();
            result->masterLength=masterLen;result->masterStamp=masterStamp;result->transport=transport;result->recordBars=recordBars.load();
            for(int ti=0;ti<kNumTracks;++ti){
                const auto& from=tracks[ti];auto& to=result->tracks[ti];
                to.state=from.state.load();to.length=to.state==Recording?from.recPos:from.lenSamples.load();
                to.layers=std::max(to.length>0?1:0,from.layers.load());
                to.volume=from.volume.load();to.pan=from.pan.load();to.muted=from.muted.load();to.reversed=from.reversed.load();to.startStamp=from.startStamp;
                to.audio.captureFrom(from.loop,to.length);
            }
            return result;
        }
        return {};
    }

    // wholeMix: one bounded common cycle, or explicit seconds requested by UI.
    // If the exact common cycle exceeds 120 s, fail instead of silently cutting
    // it. The caller can then offer an explicit duration. No always-on boost:
    // attenuation is applied ONLY if the mix would exceed -1 dBFS sample peak.
    bool renderMixdown(juce::AudioBuffer<float>& out,int onlyTrack=-1,double durationSeconds=0.0)const{
        auto s=captureSnapshot();if(!s || onlyTrack<-1 || onlyTrack>=kNumTracks)return false;
        return renderSnapshot(*s,out,onlyTrack,durationSeconds);
    }
    static bool renderSnapshot(const Snapshot& s,juce::AudioBuffer<float>& out,int onlyTrack=-1,double durationSeconds=0.0,bool protectPeak=true){
        if(onlyTrack<-1 || onlyTrack>=kNumTracks || !std::isfinite(durationSeconds) || durationSeconds<0 || durationSeconds>kMaxExportSeconds)return false;
        std::int64_t cycle=0;const auto cap=static_cast<std::int64_t>(s.rate*kMaxExportSeconds);
        for(int ti=0;ti<kNumTracks;++ti){const auto& t=s.tracks[ti];
            if(t.length<=0 || (onlyTrack>=0?ti!=onlyTrack:t.muted))continue;
            if(!cycle)cycle=t.length;
            else if(durationSeconds==0){const auto g=std::gcd(cycle,static_cast<std::int64_t>(t.length));
                if(cycle/g>cap/t.length)return false;
                cycle=cycle/g*t.length;}
        }
        if(!cycle)return false;
        if(durationSeconds>0)cycle=std::max<std::int64_t>(1,std::llround(durationSeconds*s.rate));
        if(cycle>cap || cycle>std::numeric_limits<int>::max())return false;
        out.setSize(2,static_cast<int>(cycle));out.clear();
        for(int ti=0;ti<kNumTracks;++ti){const auto& t=s.tracks[ti];
            if(t.length<=0 || (onlyTrack>=0?ti!=onlyTrack:t.muted))continue;
            const auto phase=s.masterStamp-t.startStamp;
            const float gl=t.volume*(t.pan>0?1-t.pan:1),gr=t.volume*(t.pan<0?1+t.pan:1);
            for(int n=0;n<static_cast<int>(cycle);++n){int idx=positiveModulo(phase+n,t.length);if(t.reversed)idx=t.length-1-idx;
                out.addSample(0,n,t.audio.get(0,idx)*gl);out.addSample(1,n,t.audio.get(1,idx)*gr);}
        }
        const float peak=out.getMagnitude(0,out.getNumSamples());
        if(!std::isfinite(peak))return false;
        constexpr float ceiling=0.891250938f;
        if(protectPeak && peak>ceiling){const float g=ceiling/peak;for(int ch=0;ch<2;++ch)for(int n=0;n<out.getNumSamples();++n)out.setSample(ch,n,out.getSample(ch,n)*g);}
        return true;
    }

    void process(juce::AudioBuffer<float>& io,int requestedSamples){
        std::lock_guard<std::recursive_mutex> guard(stateMutex);
        Command c;int drained=0;
        // Bounded drain: a producer flooding controls cannot starve the callback.
        while(drained++<256 && commands.pop(c))apply(c);
        const unsigned emergency=emergencyFlags.exchange(0);
        if(emergency&1u)apply(Command{10,0,nullptr});
        if(emergency&2u)apply(Command{12,0,nullptr});
        if(!pool || io.getNumChannels()<1)return;
        const int count=std::clamp(requestedSamples,0,io.getNumSamples());
        const int channels=std::min(2,io.getNumChannels());
        // Keep the transport silent while the looper is idle.  The metronome
        // is a count-in/record aid, so it should only sound once a track has
        // been armed or is actively recording; an untouched looper must pass
        // audio through transparently.
        bool countInActive = false;
        for (const auto& track : tracks)
        {
            const auto state = track.state.load(std::memory_order_relaxed);
            if (state == Armed || state == Recording)
            {
                countInActive = true;
                break;
            }
        }
        const bool click=metroOn.load() && countInActive;
        const double samplesPerBeat=srHz*60.0/std::max(40.0f,metroBpm.load());
        if(metroReset.exchange(false)){metroCountdown=0;metroBeat=0;}
        for(int n=0;n<count;++n){
            const float inL=finiteSample(io.getSample(0,n));
            const float inR=channels>1?finiteSample(io.getSample(1,n)):inL;
            float outL=0,outR=0;bool advance=false;
            if(click){if(metroCountdown<=0){clickEnv=1;clickPhase=0;clickFreq=metroBeat%4==0?1568.0f:1046.5f;++metroBeat;metroCountdown+=samplesPerBeat;}metroCountdown-=1;}
            if(clickEnv>1e-5f){const float tick=std::sin(clickPhase)*clickEnv*0.22f;clickPhase+=static_cast<float>(6.283185307179586*clickFreq/srHz);clickEnv*=clickDecay;outL+=tick;outR+=tick;}
            for(auto& t:tracks){
                int st=t.state.load(std::memory_order_relaxed);
                if(st==Armed){
                    bool ready=false;
                    if(t.armCountdown>0){--t.armCountdown;continue;}
                    else ready=masterLen<=0 || positiveModulo(transport-masterStamp,masterLen)==0 || !anyRunning();
                    if(ready){t.state.store(Recording);t.startStamp=transport;st=Recording;}
                    else continue;
                }
                if(st==Empty||st==Stopped)continue;
                advance=true;
                if(st==Recording){
                    if(!t.loop.set(t.recPos,inL,inR)){memoryFailures.fetch_add(1);finalize(t, true);continue;}
                    ++t.recPos;
                    if(t.recPos>=maxLenSamples || (t.targetLen>0 && t.recPos>=t.targetLen))finalize(t, true);
                    continue;
                }
                const int len=t.lenSamples.load(std::memory_order_relaxed);if(len<=0)continue;
                int idx=positiveModulo(transport-t.startStamp,len);
                if(t.reversed.load(std::memory_order_relaxed))idx=len-1-idx;
                // Monitor the OLD loop; the live input is already present in io.
                const float oldL=t.loop.get(0,idx),oldR=t.loop.get(1,idx);
                const float vol=t.muted.load(std::memory_order_relaxed)?0.0f:t.volume.load(std::memory_order_relaxed);
                const float pan=t.pan.load(std::memory_order_relaxed);
                t.gainL+=(vol*(pan>0?1-pan:1)-t.gainL)*gainSlew;
                t.gainR+=(vol*(pan<0?1+pan:1)-t.gainR)*gainSlew;
                outL+=oldL*t.gainL;outR+=oldR*t.gainR;
                if(st==Overdub){
                    if(!t.loop.set(idx,finiteSample(oldL+inL),finiteSample(oldR+inR))){memoryFailures.fetch_add(1);t.state.store(Playing);}
                    else {++t.dubSamples;if(t.dubSamples%len==0)t.layers.fetch_add(1);}
                }
            }
            io.setSample(0,n,finiteSample(inL+outL));if(channels>1)io.setSample(1,n,finiteSample(inR+outR));
            if(advance)++transport;
        }
        for(auto& t:tracks){const int st=t.state.load();
            if(st==Recording)t.uiPos.store(static_cast<float>(t.recPos)/std::max(1,t.targetLen>0?t.targetLen:maxLenSamples));
            else {const int len=t.lenSamples.load();if(len>0)t.uiPos.store(static_cast<float>(positiveModulo(transport-t.startStamp,len))/len);}
        }
    }

    // Portable versioned little-endian float PCM state. No platform struct
    // layouts, padding, or raw pointers are serialised. CRC guards corruption.
    std::vector<std::uint8_t> serialize()const{
        auto s=captureSnapshot();if(!s)return {};
        std::vector<std::uint8_t> bytes;std::size_t frames=0;for(auto& t:s->tracks)frames+=static_cast<std::size_t>(t.length);
        bytes.reserve(160+frames*8);
        const char magic[]="SLLOOP02";bytes.insert(bytes.end(),magic,magic+8);
        appendDouble(bytes,s->rate);appendFloat(bytes,s->bpm);
        appendU32(bytes,(s->metro?1u:0u)|(s->tempoSync?2u:0u));appendU32(bytes,static_cast<std::uint32_t>(s->recordBars));
        appendU32(bytes,static_cast<std::uint32_t>(s->masterLength));appendU64(bytes,static_cast<std::uint64_t>(s->masterStamp));appendU64(bytes,static_cast<std::uint64_t>(s->transport));
        for(auto& t:s->tracks){
            appendU32(bytes,static_cast<std::uint32_t>(t.length));appendU32(bytes,static_cast<std::uint32_t>(t.layers));
            appendU32(bytes,static_cast<std::uint32_t>(t.state==Recording?Stopped:t.state==Overdub?Playing:t.state));
            appendFloat(bytes,t.volume);appendFloat(bytes,t.pan);appendU32(bytes,(t.muted?1u:0u)|(t.reversed?2u:0u));appendU64(bytes,static_cast<std::uint64_t>(t.startStamp));
            for(int n=0;n<t.length;++n){appendFloat(bytes,t.audio.get(0,n));appendFloat(bytes,t.audio.get(1,n));}
        }
        appendU32(bytes,crc32(bytes.data(),bytes.size()));return bytes;
    }
    bool restore(const void* source,std::size_t size){
        // Decode and allocate first. A malformed state never destroys the take.
        if(!source || size<48+32*kNumTracks+4 || size>600u*1024u*1024u)return false;
        const auto* b=static_cast<const std::uint8_t*>(source);
        if(std::memcmp(b,"SLLOOP02",8)!=0 || readU32(b+size-4)!=crc32(b,size-4))return false;
        Reader r{b+8,b+size-4};const double storedRate=r.f64();const float bpm=r.f32();
        const auto flags=r.u32();const int bars=static_cast<int>(r.u32());const int storedMaster=static_cast<int>(r.u32());
        const auto storedMasterStamp=static_cast<std::int64_t>(r.u64());const auto storedTransport=static_cast<std::int64_t>(r.u64());
        if(!r.ok || !std::isfinite(storedRate)||storedRate<8000||storedRate>384000||!std::isfinite(bpm)||bpm<40||bpm>240||storedMaster<0||storedMaster>storedRate*kMaxSeconds)return false;
        std::shared_ptr<slyce::LoopPagePool> currentPool;double rate;int cap;
        {std::lock_guard<std::recursive_mutex> guard(stateMutex);currentPool=pool;rate=srHz;cap=maxLenSamples;}
        if(!currentPool){prepare(storedRate,512);std::lock_guard<std::recursive_mutex> guard(stateMutex);currentPool=pool;rate=srHz;cap=maxLenSamples;}
        auto staging=std::make_unique<Snapshot>();staging->pool=currentPool;staging->rate=rate;
        for(auto& t:staging->tracks){
            const int len=static_cast<int>(r.u32());t.layers=static_cast<int>(r.u32());t.state=static_cast<int>(r.u32());t.volume=r.f32();t.pan=r.f32();const auto tf=r.u32();
            const auto stamp=static_cast<std::int64_t>(r.u64());
            if(!r.ok||len<0||len>storedRate*kMaxSeconds||t.layers<0||t.layers>1000000||t.state<Empty||t.state>Armed||!std::isfinite(t.volume)||t.volume<0||t.volume>1.5||!std::isfinite(t.pan)||std::abs(t.pan)>1||stamp<0||stamp>(1LL<<58))return false;
            if(static_cast<std::size_t>(r.end-r.p)<static_cast<std::size_t>(len)*8)return false;
            const auto* pcm=r.p;
            for(int n=0;n<len*2;++n){const float x=r.f32();if(!std::isfinite(x)||std::abs(x)>64.0f)return false;}
            t.audio=slyce::LoopPages(currentPool,cap);t.length=len?std::min(cap,std::max(1,static_cast<int>(std::llround(len*rate/storedRate)))):0;
            t.muted=(tf&1u)!=0;t.reversed=(tf&2u)!=0;t.startStamp=static_cast<std::int64_t>(std::llround(stamp*rate/storedRate));
            auto at=[&](int n,int ch){const auto raw=readU32(pcm+8*n+4*ch);float x;std::memcpy(&x,&raw,4);return x;};
            for(int n=0;n<t.length;++n){const double x=std::min(static_cast<double>(len-1),n*storedRate/rate);const int i=static_cast<int>(x),j=std::min(i+1,len-1);const float f=static_cast<float>(x-i);
                if(!t.audio.set(n,at(i,0)+(at(j,0)-at(i,0))*f,at(i,1)+(at(j,1)-at(i,1))*f))return false;}
        }
        if(!r.ok||r.p!=r.end || storedMasterStamp<0||storedTransport<0||storedMasterStamp>(1LL<<58)||storedTransport>(1LL<<58))return false;
        {std::lock_guard<std::recursive_mutex> guard(stateMutex);
            if(pool!=currentPool)return false;
            Command pending;while(commands.pop(pending))retire(pending.payload);
            for(int ti=0;ti<kNumTracks;++ti){auto& t=tracks[ti];auto& from=staging->tracks[ti];
                t.loop.swap(from.audio);t.undo.clear();t.lenSamples.store(from.length);t.layers.store(from.layers);
                t.state.store(from.length ? (from.state==Playing?Playing:Stopped):Empty);t.volume.store(from.volume);t.pan.store(from.pan);t.muted.store(from.muted);t.reversed.store(from.reversed);t.startStamp=from.startStamp;
                t.undoAvail.store(false);t.redoState.store(false);t.recPos=t.targetLen=t.armCountdown=t.dubSamples=0;setImmediateGain(t);}
            masterLen=static_cast<int>(std::llround(storedMaster*rate/storedRate));masterStamp=static_cast<std::int64_t>(std::llround(storedMasterStamp*rate/storedRate));transport=static_cast<std::int64_t>(std::llround(storedTransport*rate/storedRate));
            setMetroBpm(bpm);metroOn.store((flags&1u)!=0);tempoSync.store((flags&2u)!=0);setRecordBars(bars);metroReset.store(true);
        }
        collectRetired();return true;
    }
    // Message thread / destructor only. Old buffers returned by an import are
    // released here, not by delete/free in the callback.
    void collectRetired(){auto* p=retired.exchange(nullptr,std::memory_order_acq_rel);while(p){auto* next=p->retiredNext;delete p;p=next;}}

private:
    static bool validTrack(int t){return t>=0 && t<kNumTracks;}
    static int positiveModulo(std::int64_t x,int m){if(m<=0)return 0;const auto r=x%m;return static_cast<int>(r<0?r+m:r);}
    static float finiteSample(float v){return std::isfinite(v)?std::clamp(v,-64.0f,64.0f):0.0f;}
    static void setImmediateGain(Track& t){const float v=t.muted.load()?0:t.volume.load(),p=t.pan.load();t.gainL=v*(p>0?1-p:1);t.gainR=v*(p<0?1+p:1);}
    bool push(int kind,int track){if(kind<10 && !validTrack(track))return false;
        if(commands.push(Command{kind,track,nullptr}))return true;
        rejectedCommands.fetch_add(1);if(kind==10)emergencyFlags.fetch_or(1);if(kind==12)emergencyFlags.fetch_or(2);return false;}
    void retire(ImportPayload* p)noexcept{if(!p)return;auto* old=retired.load(std::memory_order_relaxed);do{p->retiredNext=old;}while(!retired.compare_exchange_weak(old,p,std::memory_order_release,std::memory_order_relaxed));}
    bool anyLength()const{for(auto& t:tracks)if(t.lenSamples.load()>0)return true;return false;}
    void clear(Track& t){t.loop.clear();t.undo.clear();t.lenSamples.store(0);t.layers.store(0);t.state.store(Empty);t.undoAvail.store(false);t.redoState.store(false);t.uiPos.store(0);t.recPos=t.targetLen=t.dubSamples=t.armCountdown=0;t.startStamp=transport;}
    void finalize(Track& t, bool resumePlaying){
        const int len=std::clamp(t.targetLen>0?std::min(t.targetLen,t.recPos):t.recPos,0,maxLenSamples);
        if(len<=0){clear(t);return;}
        t.lenSamples.store(len);t.layers.store(1);t.targetLen=0;
        // A completed take becomes part of the mix immediately. The next
        // track can then be recorded over the still-playing first take.
        t.state.store(resumePlaying ? Playing : Stopped);
        t.undoAvail.store(false);
        if(masterLen<=0){masterLen=len;masterStamp=t.startStamp;}
    }
    void beginRecord(Track& t){
        t.recPos=0;t.targetLen=0;t.dubSamples=0;t.undoAvail.store(false);t.redoState.store(false);t.undo.clear();
        const int bars=recordBars.load();if(bars>0)t.targetLen=std::min(maxLenSamples,std::max(1,static_cast<int>(std::llround(srHz*60.0/metroBpm.load()*4.0*bars))));
        t.startStamp=transport;
        if(masterLen>0 && anyRunning()){t.armCountdown=0;t.state.store(Armed);}
        else if(metroOn.load()){t.armCountdown=std::max(1,static_cast<int>(std::llround(srHz*60.0/metroBpm.load()*4.0)));metroReset.store(true);t.state.store(Armed);}
        else {t.armCountdown=0;t.state.store(Recording);}
    }
    int quantisedLength(const Track& t)const{
        if(masterLen<=0){if(!metroOn.load())return t.recPos;const double bar=srHz*60.0/metroBpm.load()*4;
            return std::clamp(static_cast<int>(std::llround(std::max(1.0,std::round(t.recPos/bar))*bar)),1,maxLenSamples);}
        int best=std::clamp(static_cast<int>(std::llround(static_cast<double>(t.recPos)/masterLen))*masterLen,masterLen,maxLenSamples);
        long long error=std::llabs(static_cast<long long>(best)-t.recPos);
        // Subdivisions are offered only when sample-exact. 1003 samples must
        // not create a 1000-sample master or a drifting fractional sub-loop.
        for(int divisor:{1,2,4,8})if(masterLen%divisor==0){const int q=masterLen/divisor;if(q>0 && std::llabs(static_cast<long long>(q)-t.recPos)<error){best=q;error=std::llabs(static_cast<long long>(q)-t.recPos);}}
        return best;
    }
    void apply(Command c){
        if(c.kind>=10){
            if(c.kind==12){for(auto& t:tracks)clear(t);masterLen=0;masterStamp=transport=0;metroReset.store(true);}
            else if(c.kind==10){for(auto& t:tracks){const int st=t.state.load();if(st==Recording)finalize(t, false);else if(st==Armed)clear(t);else if(st==Playing||st==Overdub)t.state.store(Stopped);}}
            else if(c.kind==11){
                const auto oldMaster=masterStamp;transport=0;masterStamp=0;metroReset.store(true);
                for(auto& t:tracks)if(t.lenSamples.load()>0){const int len=t.lenSamples.load();const int phase=positiveModulo(oldMaster-t.startStamp,len);t.startStamp=phase==0?0:len-phase;t.state.store(Playing);}
            }
            return;
        }
        if(!validTrack(c.track)){retire(c.payload);return;}auto& t=tracks[c.track];const int st=t.state.load();
        if(c.kind==5){
            auto* pending=c.payload;if(!pending || pending->pool!=pool){retire(pending);return;}
            t.loop.swap(pending->samples);t.undo.clear();t.lenSamples.store(pending->length);t.layers.store(1);t.undoAvail.store(false);t.redoState.store(false);t.recPos=t.targetLen=t.dubSamples=0;
            t.startStamp=transport;if(masterLen<=0){masterLen=pending->length;masterStamp=transport;}t.state.store(Playing);setImmediateGain(t);retire(pending);return;
        }
        if(c.kind==2){clear(t);if(!anyLength()){masterLen=0;masterStamp=transport;}return;}
        if(c.kind==4){clear(t);if(!anyLength()){masterLen=0;masterStamp=transport;}beginRecord(t);return;}
        if(c.kind==6){if(st==Recording)finalize(t, false);else if(st==Armed)clear(t);else if(st==Playing||st==Overdub)t.state.store(Stopped);return;}
        if(c.kind==3){
            if(t.undoAvail.load() && (st==Playing||st==Overdub||st==Stopped)){
                t.loop.swap(t.undo);const int before=t.layers.load();t.layers.store(t.undoLayers);t.undoLayers=before;t.redoState.store(!t.redoState.load());if(st==Overdub)t.state.store(Playing);
            }return;
        }
        if(c.kind==1){
            if(st==Empty)beginRecord(t);
            else if(st==Armed)clear(t);
            else if(st==Recording){t.targetLen=quantisedLength(t);if(t.recPos>=t.targetLen)finalize(t, true);}
            else if(st==Stopped){t.state.store(Playing);setImmediateGain(t);}
            else if(st==Playing){
                t.undo.captureFrom(t.loop,t.lenSamples.load());t.undoLayers=t.layers.load();t.dubSamples=0;t.redoState.store(false);t.undoAvail.store(true);t.state.store(Overdub);
            }else if(st==Overdub){if(t.dubSamples>0 && t.dubSamples<t.lenSamples.load())t.layers.fetch_add(1);t.state.store(Playing);}
        }
    }
    static void appendU32(std::vector<std::uint8_t>& b,std::uint32_t x){for(int k=0;k<4;++k)b.push_back(static_cast<std::uint8_t>(x>>(8*k)));}
    static void appendU64(std::vector<std::uint8_t>& b,std::uint64_t x){for(int k=0;k<8;++k)b.push_back(static_cast<std::uint8_t>(x>>(8*k)));}
    static void appendFloat(std::vector<std::uint8_t>& b,float x){std::uint32_t u;std::memcpy(&u,&x,4);appendU32(b,u);}
    static void appendDouble(std::vector<std::uint8_t>& b,double x){std::uint64_t u;std::memcpy(&u,&x,8);appendU64(b,u);}
    static std::uint32_t readU32(const std::uint8_t* b){return static_cast<std::uint32_t>(b[0])|(static_cast<std::uint32_t>(b[1])<<8)|(static_cast<std::uint32_t>(b[2])<<16)|(static_cast<std::uint32_t>(b[3])<<24);}
    static std::uint32_t crc32(const std::uint8_t* p,std::size_t n){
        static const auto table=[](){std::array<std::uint32_t,256> t{};for(std::uint32_t i=0;i<256;++i){auto c=i;for(int k=0;k<8;++k)c=(c>>1)^((c&1)?0xedb88320u:0u);t[i]=c;}return t;}();
        std::uint32_t crc=0xffffffffu;for(std::size_t i=0;i<n;++i)crc=(crc>>8)^table[(crc^p[i])&255];return ~crc;
    }
    struct Reader {const std::uint8_t* p;const std::uint8_t* end;bool ok=true;
        std::uint32_t u32(){if(end-p<4){ok=false;return 0;}auto v=readU32(p);p+=4;return v;}
        std::uint64_t u64(){const auto lo=u32(),hi=u32();return static_cast<std::uint64_t>(lo)|(static_cast<std::uint64_t>(hi)<<32);}
        float f32(){auto v=u32();float x;std::memcpy(&x,&v,4);return x;}
        double f64(){auto v=u64();double x;std::memcpy(&x,&v,8);return x;}
    };
    mutable std::recursive_mutex stateMutex;
    std::shared_ptr<slyce::LoopPagePool> pool;
    std::array<Track,kNumTracks> tracks;
    slyce::BoundedQueue<Command,256> commands;
    std::atomic<ImportPayload*> retired{nullptr};
    std::atomic<std::uint64_t> rejectedCommands{0},memoryFailures{0};
    std::atomic<unsigned> emergencyFlags{0};
    std::atomic<double> publishedRate{44100.0};std::atomic<int> publishedCapacity{0};
    int maxLenSamples=0,masterLen=0;std::int64_t transport=0,masterStamp=0;
    // A first take should always have an audible four-beat count-in.  The
    // metronome can still be turned off from the Looper actions menu, but
    // starting a fresh session must not silently begin recording.
    // Stay silent until the user enables the metronome.  Once enabled, a new
    // recording gets the four-beat count-in; idle playback never emits ticks.
    std::atomic<bool> metroOn{false},metroReset{false},tempoSync{false};
    std::atomic<float> metroBpm{120.0f};std::atomic<int> recordBars{0};
    double srHz=44100.0,metroCountdown=0;int metroBeat=0;
    float clickEnv=0,clickPhase=0,clickFreq=1046.5f,clickDecay=0.99f,gainSlew=0.005f;
};

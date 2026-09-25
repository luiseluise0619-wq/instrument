#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <memory>
namespace slyce {
// Portable, lossless float-PCM archive for edited/embedded chop samples.
// No struct padding or machine endianness; bounded decode validates before allocation.
struct SampleStateCodec {
    static constexpr std::size_t maxBytes=128u*1024u*1024u;
    static std::uint32_t hash(const std::uint8_t* b,std::size_t n){std::uint32_t h=2166136261u;for(std::size_t i=0;i<n;++i)h=(h^b[i])*16777619u;return h;}
    static void u32(std::vector<std::uint8_t>& b,std::uint32_t x){for(int k=0;k<4;++k)b.push_back(static_cast<std::uint8_t>(x>>(8*k)));}
    static std::uint32_t u32(const std::uint8_t* b){return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
    static std::vector<std::uint8_t> encode(const juce::AudioBuffer<float>& pcm,double rate) {
        const int frames=pcm.getNumSamples(),ch=pcm.getNumChannels();
        if(ch<1||ch>2||frames<=0||!std::isfinite(rate)||rate<8000||rate>384000)return {};
        const std::size_t bytes=std::size_t(frames)*ch*4;
        if(bytes>maxBytes)return {};
        std::vector<std::uint8_t> b;b.reserve(bytes+28);b.insert(b.end(),{'S','L','S','A','M','P','0','1'});
        std::uint64_t rateBits;std::memcpy(&rateBits,&rate,8);u32(b,std::uint32_t(rateBits));u32(b,std::uint32_t(rateBits>>32));
        u32(b,std::uint32_t(ch));u32(b,std::uint32_t(frames));
        for(int n=0;n<frames;++n)for(int c=0;c<ch;++c){float x=pcm.getSample(c,n);if(!std::isfinite(x))return {};std::uint32_t v;std::memcpy(&v,&x,4);u32(b,v);}
        u32(b,hash(b.data(),b.size()));return b;
    }
    static std::shared_ptr<juce::AudioBuffer<float>> decode(const void* source,std::size_t size,double& outRate) {
        if(!source||size<28||size>maxBytes+28)return {};
        const auto* b=static_cast<const std::uint8_t*>(source);
        if(std::memcmp(b,"SLSAMP01",8)!=0||u32(b+size-4)!=hash(b,size-4))return {};
        const std::uint64_t bits=std::uint64_t(u32(b+8))|(std::uint64_t(u32(b+12))<<32);
        double rate;std::memcpy(&rate,&bits,8);
        const auto channels=u32(b+16),frames=u32(b+20);
        if(!std::isfinite(rate)||rate<8000||rate>384000||channels<1||channels>2||!frames||std::uint64_t(frames)*channels*4+28!=size)return {};
        for(std::size_t pos=24;pos<size-4;pos+=4){auto v=u32(b+pos);float x;std::memcpy(&x,&v,4);if(!std::isfinite(x))return {};}
        auto result=std::make_shared<juce::AudioBuffer<float>>(int(channels),int(frames));
        std::size_t pos=24;for(std::uint32_t n=0;n<frames;++n)for(std::uint32_t c=0;c<channels;++c){auto v=u32(b+pos);float x;std::memcpy(&x,&v,4);result->setSample(int(c),int(n),x);pos+=4;}
        outRate=rate;return result;
    }
};
}

#include "../Source/AudioEngine/SampleStateCodec.h"
#include "../Source/DSP/Limiter.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <random>
#include <functional>
using Buffer=juce::AudioBuffer<float>;
static int checks=0;
void check(bool ok,const char* m){++checks;if(!ok)throw std::runtime_error(m);}
int main(){
 using Codec=slyce::SampleStateCodec;
 int cases=0;auto run=[&](const char* name,const std::function<void()>& f){f();++cases;std::cout<<"PASS "<<name<<"\n";};
 run("mono/stereo exact PCM and sample-rate roundtrip",[]{
   for(int ch:{1,2})for(double rate:{8000.,44100.,48000.,96000.,384000.}){
     Buffer b(ch,773);for(int c=0;c<ch;++c)for(int n=0;n<773;++n)b.setSample(c,n,float(std::sin(n*.019+c)*.75));
     auto data=Codec::encode(b,rate);double restored=0;auto out=Codec::decode(data.data(),data.size(),restored);
     check(out&&restored==rate,"decode");check(out->getNumChannels()==ch&&out->getNumSamples()==773,"shape");
     for(int c=0;c<ch;++c)for(int n=0;n<773;++n)check(out->getSample(c,n)==b.getSample(c,n),"lossless PCM");
   }
 });
 run("malformed/empty payload leaves output rate untouched",[]{double rate=123;check(!Codec::decode(nullptr,0,rate),"null");check(rate==123,"rate unchanged");});
 run("CRC/content corruption rejected",[]{Buffer b(2,32);b.clear();auto data=Codec::encode(b,48000);data[47]^=3;double rate=123;check(!Codec::decode(data.data(),data.size(),rate),"corrupt");check(rate==123,"rate unchanged");});
 run("truncated/trailing payload rejected",[]{Buffer b(1,32);b.clear();auto data=Codec::encode(b,48000);double rate=123;check(!Codec::decode(data.data(),data.size()-1,rate),"truncated");data.push_back(0);check(!Codec::decode(data.data(),data.size(),rate),"extra");});
 run("invalid sample rates rejected",[]{Buffer b(2,4);b.clear();for(double rate:{0.,-1.,7999.,384001.,std::numeric_limits<double>::infinity()})check(Codec::encode(b,rate).empty(),"bad rate");});
 run("NaN/Inf audio cannot enter saved archive",[]{Buffer b(1,32);b.clear();for(float x:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){b.setSample(0,0,x);check(Codec::encode(b,48000).empty(),"nonfinite");}});
 run("empty/unsupported channel counts rejected",[]{Buffer a(2,0),b(3,32);b.clear();check(Codec::encode(a,48000).empty(),"empty");check(Codec::encode(b,48000).empty(),"channels");});
 run("malicious dimension with valid checksum rejected",[]{Buffer b(1,32);b.clear();auto data=Codec::encode(b,48000);for(int i=20;i<24;++i)data[i]=255;auto h=Codec::hash(data.data(),data.size()-4);for(int k=0;k<4;++k)data[data.size()-4+k]=std::uint8_t(h>>(8*k));double rate=0;check(!Codec::decode(data.data(),data.size(),rate),"huge dimensions");});
 run("limiter ceiling across sample rates/variable block sizes",[]{
   for(double rate:{44100.,48000.,96000.}){Limiter limit;limit.prepare(rate,1024);limit.setCeiling(.89f);std::mt19937 rng(11);std::uniform_real_distribution<float> random(-3,3);
   for(int pass=0;pass<160;++pass){int count=(pass%7==0?1:pass%5==0?1024:127);Buffer b(2,count);for(int c=0;c<2;++c)for(int n=0;n<count;++n)b.setSample(c,n,random(rng));limit.process(b);for(int c=0;c<2;++c)for(int n=0;n<count;++n)check(std::isfinite(b.getSample(c,n))&&std::abs(b.getSample(c,n))<.891f,"ceiling");}}
 });
 run("limiter neutral signal delay matches reported latency",[]{Limiter l;l.prepare(48000,1024);Buffer b(2,1024);b.clear();b.setSample(0,0,.1f);l.process(b);check(std::abs(b.getSample(0,l.getLatencySamples())-.1f)<1e-7,"delay");});
 std::cout<<"PASS "<<cases<<" sample-state/limiter cases, "<<checks<<" assertions\n";
}

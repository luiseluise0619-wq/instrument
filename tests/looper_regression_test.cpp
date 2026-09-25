#include "../Source/AudioEngine/LoopStation.h"
#include <iostream>
#include <thread>
#include <functional>
#include <stdexcept>
using Buffer=juce::AudioBuffer<float>;
constexpr double sr=48000;
static int assertions=0;
void require(bool x,const char* m){++assertions;if(!x)throw std::runtime_error(m);}
Buffer constant(int n,float v){Buffer b(2,n);for(int c=0;c<2;++c)for(int i=0;i<n;++i)b.setSample(c,i,v);return b;}
Buffer ramp(int n){auto b=constant(n,0);for(int c=0;c<2;++c)for(int i=0;i<n;++i)b.setSample(c,i,0.05f+0.15f*i/n);return b;}
Buffer tick(LoopStation& l,int n=0,float v=0){auto b=constant(std::max(1,n),v);l.process(b,n);return b;}
void ready(LoopStation& l,int tr,const Buffer& b){l.setTrackVolume(tr,1);require(l.importAudio(tr,b,sr),"import");tick(l);}
Buffer mix(LoopStation& l,int tr=-1){Buffer b;require(l.renderMixdown(b,tr),"export");return b;}
float difference(const Buffer& a,const Buffer& b){if(a.getNumSamples()!=b.getNumSamples())return 999;float e=0;for(int c=0;c<2;++c)for(int n=0;n<a.getNumSamples();++n)e=std::max(e,std::abs(a.getSample(c,n)-b.getSample(c,n)));return e;}
int main(){
 int tests=0;
 auto run=[&](const char* name,std::function<void()> fn){try{fn();++tests;std::cout<<"PASS "<<name<<"\n";}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<"\n";throw;}};
 run("empty looper is unity transparent",[]{LoopStation l;l.prepare(sr,128);auto b=tick(l,64,.237f);require(std::abs(b.getSample(0,0)-.237f)<1e-6,"dry level changed");require(std::abs(b.getSample(1,63)-.237f)<1e-6,"dry level changed");});
 run("record monitoring stays at unity",[]{LoopStation l;l.prepare(sr,128);l.tapMain(0);auto b=tick(l,64,.237f);require(std::abs(b.getSample(0,0)-.237f)<1e-6,"record monitor changed");require(std::abs(b.getSample(1,63)-.237f)<1e-6,"record monitor changed");});
 run("new track playback matches recorded level",[]{LoopStation l;l.prepare(sr,128);l.tapMain(0);tick(l,64,.237f);l.tapMain(0);tick(l);l.tapMain(0);auto b=tick(l,64,0);require(std::abs(b.getSample(0,0)-.237f)<1e-6,"playback level changed");require(std::abs(b.getSample(1,63)-.237f)<1e-6,"playback level changed");require(std::abs(l.getTrackVolume(0)-1.0f)<1e-6,"default volume not unity");});
 run("native import / forward undo / redo",[]{LoopStation l;l.prepare(sr,128);auto b=ramp(64);ready(l,0,b);require(difference(b,mix(l,0))<1e-6,"native");l.tapMain(0);tick(l,8,.1f);l.tapMain(0);tick(l);auto d=mix(l,0);l.tapUndo(0);tick(l);require(difference(b,mix(l,0))<1e-6,"undo");l.tapUndo(0);tick(l);require(difference(d,mix(l,0))<1e-6,"redo");});
 run("reverse AFTER dub then undo",[]{LoopStation l;l.prepare(sr,128);auto b=ramp(64);ready(l,0,b);l.tapMain(0);tick(l,8,.1f);l.tapMain(0);tick(l);l.setReversed(0,true);l.tapUndo(0);tick(l);auto d=mix(l,0);for(int c=0;c<2;++c)for(int n=0;n<64;++n)require(std::abs(d.getSample(c,n)-b.getSample(c,63-n))<1e-6,"reverse undo");});
 run("reverse DURING dub then undo",[]{LoopStation l;l.prepare(sr,128);auto b=ramp(64);ready(l,0,b);l.tapMain(0);tick(l,8,.1f);l.setReversed(0,true);tick(l,13,.2f);l.tapUndo(0);tick(l);l.setReversed(0,false);require(difference(b,mix(l,0))<1e-6,"mid reverse undo");});
 run("undo while stopped",[]{LoopStation l;l.prepare(sr,128);auto b=ramp(64);ready(l,0,b);l.tapMain(0);tick(l,32,.1f);l.tapStopTrack(0);tick(l);l.tapUndo(0);tick(l);require(l.getTrackState(0)==LoopStation::Stopped,"stopped");require(difference(b,mix(l,0))<1e-6,"stopped undo");});
 run("no doubled live overdub monitoring",[]{LoopStation l;l.prepare(sr,128);ready(l,0,constant(64,0));l.tapMain(0);auto b=tick(l,1,.1f);require(std::abs(b.getSample(0,0)-.1f)<1e-6,"double monitoring");});
 run("same-rate prepare preserves audio",[]{LoopStation l;l.prepare(sr,128);auto b=ramp(64);ready(l,0,b);l.prepare(sr,1024);require(difference(b,mix(l,0))<1e-6,"prepare wipe");});
 run("sample-rate change preserves duration",[]{LoopStation l;l.prepare(sr,128);ready(l,0,ramp(48000));l.prepare(96000,512);auto b=mix(l,0);require(b.getNumSamples()==96000,"duration");require(b.getMagnitude(0,96000)>.1f,"audio remains");});
 run("2s + 3s export common 6s cycle",[]{LoopStation l;l.prepare(sr,128);ready(l,0,constant(96000,.1f));ready(l,1,constant(144000,.2f));require(mix(l).getNumSamples()==288000,"LCM");});
 run("unbounded LCM rejected; explicit duration works",[]{LoopStation l;l.prepare(sr,128);ready(l,0,constant(10007,.1f));ready(l,1,constant(10009,.2f));Buffer b;require(!l.renderMixdown(b),"cap");require(l.renderMixdown(b,-1,4.0),"explicit");require(b.getNumSamples()==192000,"duration");});
 run("integer PCM export has headroom",[]{LoopStation l;l.prepare(sr,128);ready(l,0,constant(64,.8f));ready(l,1,constant(64,.8f));auto b=mix(l);require(b.getMagnitude(0,64)<.892f,"peak");});
 run("clear then play not coalesced",[]{LoopStation l;l.prepare(sr,128);ready(l,0,ramp(64));l.tapClearAll();l.tapPlayAll();tick(l);require(!l.anyContent(),"clear dropped");});
 run("nine commands not overwritten",[]{LoopStation l;l.prepare(sr,128);ready(l,0,ramp(64));l.tapClear(0);for(int n=0;n<7;++n)l.tapMain(0);l.tapStopTrack(0);tick(l);require(!l.anyContent(),"ring overwrite");});
 run("queue overflow signalled and emergency clear survives",[]{LoopStation l;l.prepare(sr,128);ready(l,0,ramp(64));for(int n=0;n<300;++n)l.tapMain(0);l.tapClearAll();tick(l);require(l.getDroppedCommandCount()>0,"overflow visible");require(!l.anyContent(),"clear priority");});
 run("1003-frame imported master stays exact",[]{LoopStation l;l.prepare(sr,128);ready(l,0,ramp(1003));l.tapMain(1);tick(l,1003,.1f);l.tapMain(1);tick(l);require(mix(l,1).getNumSamples()==1003,"master lattice");});
 run("stop preserves unfinished take",[]{LoopStation l;l.prepare(sr,128);l.tapMain(0);tick(l,97,.1f);l.tapStopAll();tick(l);auto b=mix(l,0);require(b.getNumSamples()==97,"stop discarded recording");});
 run("metronome does not enter recording",[]{LoopStation l;l.prepare(sr,128);l.setMetronomeOn(true);l.setRecordBars(1);l.tapMain(0);auto b=tick(l,192001);require(b.getMagnitude(0,b.getNumSamples())>0.01f,"click audible");auto d=mix(l,0);require(d.getMagnitude(0,d.getNumSamples())==0,"click leaked");require(d.getNumSamples()==96000,"fixed bar length");});
 run("save restore audio/metadata/checksum",[]{LoopStation l;l.prepare(sr,128);ready(l,0,ramp(1234));l.setPan(0,.2f);l.setReversed(0,true);l.setTempoSync(true);l.setRecordBars(2);auto b=mix(l,0);auto bytes=l.serialize();l.tapClearAll();tick(l);require(l.restore(bytes.data(),bytes.size()),"restore");require(difference(b,mix(l,0))<1e-6,"PCM mismatch");require(l.isReversed(0)&&l.isTempoSync()&&l.getRecordBars()==2,"metadata");bytes[bytes.size()/2]^=1;require(!l.restore(bytes.data(),bytes.size()),"CRC");require(difference(b,mix(l,0))<1e-6,"bad restore mutated take");});
 run("empty session roundtrip",[]{LoopStation l;l.prepare(sr,128);auto bytes=l.serialize();require(l.restore(bytes.data(),bytes.size()),"empty state");});
 run("recording prefix survives save",[]{LoopStation l;l.prepare(sr,128);l.tapMain(0);tick(l,100,.1f);auto bytes=l.serialize();l.tapClearAll();tick(l);require(l.restore(bytes.data(),bytes.size()),"recording restore");require(l.getTrackState(0)==LoopStation::Stopped,"resumes safely stopped");require(mix(l,0).getNumSamples()==100,"prefix lost");});
 run("snapshot isolated from later dub/clear",[]{LoopStation l;l.prepare(sr,128);auto b=ramp(1024);ready(l,0,b);auto snapshot=l.captureSnapshot();l.tapMain(0);tick(l,1024,.4f);l.tapClearAll();tick(l);Buffer out;require(LoopStation::renderSnapshot(*snapshot,out,0),"snapshot");require(difference(b,out)<1e-6,"COW isolation");});
 run("rapid imports remain ordered and private",[]{LoopStation l;l.prepare(sr,128);l.setTrackVolume(0,1);require(l.importAudio(0,constant(64,.1f),sr),"first");require(l.importAudio(0,constant(64,.2f),sr),"second");tick(l);require(std::abs(mix(l,0).getSample(0,0)-.2f)<1e-6,"last import");});
 run("invalid inputs rejected without replacing audio",[]{LoopStation l;l.prepare(sr,128);auto b=ramp(64);ready(l,0,b);require(!l.importAudio(-1,b,sr),"track");require(!l.importAudio(0,b,0),"rate");require(!l.importAudio(0,constant(1,.1f),sr),"short");require(difference(b,mix(l,0))<1e-6,"untouched");});
 run("multi-producer control queue accounting",[]{slyce::BoundedQueue<int,256> q;std::atomic<int> produced{0},consumed{0};std::atomic<bool> doneA{false},doneB{false};auto producer=[&](std::atomic<bool>& done){for(int n=0;n<10000;++n){while(!q.push(1))std::this_thread::yield();++produced;}done=true;};std::thread a(producer,std::ref(doneA)),b(producer,std::ref(doneB));int x;while(!doneA||!doneB){while(q.pop(x))consumed+=x;}a.join();b.join();while(q.pop(x))consumed+=x;require(produced==20000 && consumed==20000,"MPSC loss");});
 run("concurrent playback/snapshot/import",[]{LoopStation l;l.prepare(sr,128);ready(l,0,ramp(2048));std::atomic<bool> done{false};std::thread audio([&]{auto b=constant(64,0);for(int n=0;n<1200;++n){b.clear();l.process(b,64);}done=true;});for(int n=0;n<40;++n){auto s=l.captureSnapshot();Buffer b;require(s&&LoopStation::renderSnapshot(*s,b,0),"parallel snapshot");if(n%10==0)require(l.importAudio(0,ramp(2048),sr),"parallel import");}audio.join();require(done,"audio finished");});
 std::cout<<"PASS "<<tests<<" looper/queue cases, "<<assertions<<" assertions\n";
}

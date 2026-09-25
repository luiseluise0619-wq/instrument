#pragma once
// LOCAL TEST SHIM ONLY. Not JUCE, not linked into / shipped with the plugin.
// Used to syntax-check the patched engines and execute their own DSP loops
// when the JUCE SDK is unavailable. Reverb and file decoding are placeholders.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
namespace juce {
using uint32=std::uint32_t;
template<class T> T jmin(T a,T b){return std::min(a,b);}
template<class T> T jmax(T a,T b){return std::max(a,b);}
template<class T> T jlimit(T a,T b,T x){assert(a<=b);return std::clamp(x,a,b);}
template<class T> struct MathConstants {static constexpr T pi=(T)3.14159265358979323846, twoPi=pi*2,halfPi=pi/2;};
struct ScopedNoDenormals {};
struct ValueSmoothingTypes {struct Linear {};};
template<class T,class Kind=ValueSmoothingTypes::Linear> class SmoothedValue {
 T current{},target{},step{};int count=0,length=0;
 public: SmoothedValue(T x=0):current(x),target(x){}
 void reset(double sr,double sec){length=(int)std::floor(sr*sec);count=0;current=target;}
 void setCurrentAndTargetValue(T v){current=target=v;count=0;step=0;}
 void setTargetValue(T v){if(v==target)return;target=v;if(length<=0){current=v;count=0;}else{count=length;step=(target-current)/(T)count;}}
 T getNextValue(){if(count>0){if(--count==0)current=target;else current+=step;}return current;}
 T getCurrentValue()const{return current;}bool isSmoothing()const{return count>0;}
 T skip(int n){while(n-->0)getNextValue();return current;}
};
template<class T> class AudioBuffer {
 std::vector<T> storage;std::array<T*,64> ptr{};int channels=0,samples=0;
 public:
 AudioBuffer()=default;AudioBuffer(int ch,int n){setSize(ch,n);}
 AudioBuffer(T*const* p,int ch,int n):channels(ch),samples(n){for(int i=0;i<ch;++i)ptr[i]=p[i];}
 AudioBuffer(const AudioBuffer& b){makeCopyOf(b);}AudioBuffer&operator=(const AudioBuffer& b){if(this!=&b)makeCopyOf(b);return*this;}
 AudioBuffer(AudioBuffer&&)=default;AudioBuffer&operator=(AudioBuffer&&)=default;
 void setSize(int ch,int n,bool=false,bool=false,bool=false){channels=ch;samples=n;storage.assign((size_t)ch*n,T{});for(int i=0;i<ch;++i)ptr[i]=storage.data()+i*n;}
 void makeCopyOf(const AudioBuffer& b){setSize(b.channels,b.samples);for(int c=0;c<channels;++c)std::copy_n(b.ptr[c],samples,ptr[c]);}
 int getNumChannels()const{return channels;}int getNumSamples()const{return samples;}
 T*getWritePointer(int ch,int offset=0){assert(ch>=0&&ch<channels&&offset>=0&&offset<=samples);return ptr[ch]+offset;}
 const T*getReadPointer(int ch,int offset=0)const{assert(ch>=0&&ch<channels&&offset>=0&&offset<=samples);return ptr[ch]+offset;}
 T*const*getArrayOfWritePointers(){return ptr.data();}
 T getSample(int ch,int n)const{assert(n>=0&&n<samples);return getReadPointer(ch)[n];}
 void setSample(int ch,int n,T v){assert(n>=0&&n<samples);getWritePointer(ch)[n]=v;}
 void addSample(int ch,int n,T v){assert(n>=0&&n<samples);getWritePointer(ch)[n]+=v;}
 void clear(){for(int c=0;c<channels;++c)std::fill_n(ptr[c],samples,T{});}
 void clear(int c,int start,int n){assert(start+n<=samples);std::fill_n(ptr[c]+start,n,T{});}
 void copyFrom(int c,int start,const AudioBuffer& b,int bc,int bs,int n){assert(start+n<=samples&&bs+n<=b.samples);std::copy_n(b.ptr[bc]+bs,n,ptr[c]+start);}
 float getMagnitude(int start,int n)const{float p=0;for(int c=0;c<channels;++c)for(int i=start;i<start+n;++i)p=std::max(p,(float)std::abs(ptr[c][i]));return p;}
};
class Random {uint64_t seed=0x197429;public:float nextFloat(){seed=(seed*0x5deece66dULL+11)&0xffffffffffffULL;return(float)((seed>>16)/4294967296.0);}};
class SpinLock {
 std::mutex m;
 public: struct ScopedLockType{std::lock_guard<std::mutex>g;ScopedLockType(SpinLock&l):g(l.m){}};
 struct ScopedTryLockType{std::unique_lock<std::mutex>g;ScopedTryLockType(SpinLock&l):g(l.m,std::try_to_lock){}bool isLocked()const{return g.owns_lock();}};
};
class CriticalSection{public:std::recursive_mutex m;};
struct ScopedLock{std::lock_guard<std::recursive_mutex>g;ScopedLock(CriticalSection&c):g(c.m){}};
struct Decibels{static float decibelsToGain(float db){return std::pow(10.f,db/20.f);}};
class String {
 std::string s;
 public: String()=default;String(const char*p):s(p?p:""){}String(std::string t):s(std::move(t)){}
 bool isEmpty()const{return s.empty();}bool isNotEmpty()const{return!s.empty();}int length()const{return(int)s.size();}char operator[](int i)const{return s[(size_t)i];}
 const std::string&str()const{return s;}
 bool operator==(const char*x)const{return s==x;}bool operator==(const String&x)const{return s==x.s;}
 friend String operator+(const char*a,const String&b){return String(std::string(a)+b.s);}
 String trim()const{auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==s.npos?String():String(s.substr(a,b-a+1));}
 String toLowerCase()const{auto t=s;for(auto&c:t)c=(char)std::tolower((unsigned char)c);return t;}
 String substring(int a,int b)const{a=std::max(a,0);b=std::max(a,b);return a>=(int)s.size()?String():String(s.substr(a,b-a));}
 String substring(int a)const{return substring(a,(int)s.size());}
 int indexOfChar(int a,char c)const{auto p=s.find(c,std::max(0,a));return p==s.npos?-1:(int)p;}int indexOfChar(char c)const{return indexOfChar(0,c);}
 bool contains(const String& x) const { return s.find(x.s) != std::string::npos; }
    bool containsIgnoreCase(const String& x) const { return toLowerCase().contains(x.toLowerCase()); }
    bool containsChar(char c)const{return s.find(c)!=s.npos;}bool containsOnly(const char*c)const{return s.find_first_not_of(c)==s.npos;}
 int getIntValue()const{try{return std::stoi(s);}catch(...){return 0;}}float getFloatValue()const{try{return std::stof(s);}catch(...){return 0;}}
 bool startsWith(const char*c)const{return s.rfind(c,0)==0;}bool startsWithIgnoreCase(const char*c)const{return toLowerCase().startsWith(String(c).toLowerCase().s.c_str());}
 bool equalsIgnoreCase(const char*c)const{return toLowerCase()==String(c).toLowerCase();}
 String upToFirstOccurrenceOf(const char*c,bool incl,bool)const{auto p=s.find(c);return p==s.npos?*this:String(s.substr(0,p+(incl?std::strlen(c):0)));}
 String fromFirstOccurrenceOf(const char*c,bool incl,bool)const{auto p=s.find(c);return p==s.npos?String():String(s.substr(p+(incl?0:std::strlen(c))));}
 String replaceCharacter(char a,char b)const{auto t=s;std::replace(t.begin(),t.end(),a,b);return t;}
 String replace(const String&a,const String&b)const{auto t=s;size_t p=0;while(!a.s.empty()&&(p=t.find(a.s,p))!=t.npos){t.replace(p,a.s.size(),b.s);p+=b.s.size();}return t;}
 void swapWith(String&b){s.swap(b.s);}
};
class StringArray {std::vector<String>v;public:auto begin(){return v.begin();}auto end(){return v.end();}auto begin()const{return v.begin();}auto end()const{return v.end();}void add(const String&s){v.push_back(s);}void addLines(const String&s){std::istringstream i(s.str());std::string l;while(std::getline(i,l))v.emplace_back(l);}int size()const{return(int)v.size();}bool isEmpty()const{return v.empty();}String&getReference(int i){return v[i];}String&operator[](int i){return v[i];}const String&operator[](int i)const{return v[i];}};
struct CharacterFunctions{static bool isWhitespace(char c){return std::isspace((unsigned char)c)!=0;}};
class File {std::filesystem::path p;public:File()=default;File(String x):p(x.str()){}bool existsAsFile()const{return std::filesystem::is_regular_file(p);}String getFullPathName()const{return p.string();}String getFileName()const{return p.filename().string();}String getFileNameWithoutExtension()const{return p.stem().string();}File getParentDirectory()const{return File(p.parent_path().string());}File getChildFile(String s)const{return File((p/s.str()).string());}File getSiblingFile(String s)const{return getParentDirectory().getChildFile(s);}String loadFileAsString()const{std::ifstream f(p);std::ostringstream s;s<<f.rdbuf();return s.str();}};
class AudioFormatReader {public:long long lengthInSamples=0;unsigned numChannels=0;double sampleRate=44100;bool read(AudioBuffer<float>*,int,int,int,bool,bool){return false;}};
class AudioFormatManager {public:void registerBasicFormats(){}AudioFormatReader*createReaderFor(const File&){return nullptr;}};
namespace dsp {
struct ProcessSpec{double sampleRate=44100;uint32 maximumBlockSize=512,numChannels=2;};
template<class T>struct AudioBlock {T*const*p;size_t channels,samples;AudioBlock(T*const*pp,size_t c,size_t n):p(pp),channels(c),samples(n){}};
template<class T>struct ProcessContextReplacing{AudioBlock<T>&b;ProcessContextReplacing(AudioBlock<T>&bb):b(bb){}};
class Reverb{public:struct Parameters{float roomSize{},damping{},wetLevel{},dryLevel{},width{};};void prepare(ProcessSpec){}void reset(){}void setParameters(Parameters){}void process(ProcessContextReplacing<float>ctx){for(size_t c=0;c<ctx.b.channels;++c)std::fill_n(ctx.b.p[c],ctx.b.samples,0.f);}};
}
}

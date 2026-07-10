// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '파일 → 메모리 오디오' 디코더예요. WAV/AIFF/FLAC/Ogg(플랫폼이 지원하면 MP3도)
// 를 읽어 float 버퍼(AudioBuffer)로 풀어줍니다. 이 버퍼가 이후 SliceEngine/VoicePool로
// 넘어가 잘리고 재생됩니다. 실패하면 nullptr을 돌려주고 outSampleRate는 건드리지 않아요.
// [스레드] 파일 접근/디코딩은 무겁고 블로킹이라 반드시 메시지 스레드에서만.

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

/**
    Decodes an audio file (WAV / AIFF / FLAC / Ogg, plus MP3 where the platform
    codec is available) into an in-memory float buffer. Returns nullptr on
    failure and leaves outSampleRate untouched.
*/
// [클래스] SampleLoader — 정적(static) 함수 모음. 객체를 만들 필요 없이 SampleLoader::decode(...)로 씀.
class SampleLoader
{
public:
    // [역할] decode(파일) — 파일 경로에서 디코딩. outSampleRate에 원본 샘플레이트를 채워줌(참조 인자).
    static std::shared_ptr<juce::AudioBuffer<float>>
        decode (const juce::File& file, double& outSampleRate);

    // [역할] decode(메모리) — 메모리 안의 오디오 바이트에서 디코딩(내장 자원 등).
    static std::shared_ptr<juce::AudioBuffer<float>>
        decode (const void* data, int sizeBytes, double& outSampleRate);

private:
    // [내부 도우미] readAll — 리더에서 전체 샘플을 읽어 버퍼로. 두 decode가 공유.
    static std::shared_ptr<juce::AudioBuffer<float>>
        readAll (juce::AudioFormatReader* reader, double& outSampleRate);
};

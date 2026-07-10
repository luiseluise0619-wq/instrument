// [파일 역할] SampleLoader.h의 구현. 오디오 파일/메모리를 float 버퍼로 디코딩.
#include "SampleLoader.h"

// [함수] readAll — 주어진 리더에서 전체 오디오를 읽어 공유 버퍼로 반환.
std::shared_ptr<juce::AudioBuffer<float>>
SampleLoader::readAll (juce::AudioFormatReader* reader, double& outSampleRate)
{
    // [메모리] unique_ptr로 리더를 감싸 함수 끝에서 자동 삭제(누수 방지). 소유권을 여기서 넘겨받음.
    std::unique_ptr<juce::AudioFormatReader> r (reader);
    // 리더가 없거나 길이가 0이면 실패(nullptr).
    if (r == nullptr || r->lengthInSamples <= 0)
        return nullptr;

    // 원본 샘플레이트를 호출자에게 전달.
    outSampleRate = r->sampleRate;

    // 채널 수(최소 1)와 샘플 수 파악.
    const int numChannels = juce::jmax (1, (int) r->numChannels);
    const int numSamples  = (int) r->lengthInSamples;

    // 버퍼를 할당하고 전체를 읽어 채움.
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    r->read (buffer.get(), 0, numSamples, 0, true, true);
    return buffer;
}

// [함수] decode(파일) — 파일에서 리더를 만들어 readAll로 디코딩.
std::shared_ptr<juce::AudioBuffer<float>>
SampleLoader::decode (const juce::File& file, double& outSampleRate)
{
    // 포맷 매니저에 기본 포맷 등록 후 리더 생성.
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();     // WAV, AIFF (+ FLAC/Ogg where enabled)
    return readAll (manager.createReaderFor (file), outSampleRate);
}

// [함수] decode(메모리) — 메모리 바이트에서 디코딩.
std::shared_ptr<juce::AudioBuffer<float>>
SampleLoader::decode (const void* data, int sizeBytes, double& outSampleRate)
{
    // 방어: 데이터가 없거나 크기가 이상하면 실패.
    if (data == nullptr || sizeBytes <= 0)
        return nullptr;

    juce::AudioFormatManager manager;
    manager.registerBasicFormats();

    // [메모리] 메모리를 감싸는 입력 스트림 생성(false=데이터를 복사하지 않고 참조). 소유권을 리더로 이동.
    auto stream = std::make_unique<juce::MemoryInputStream> (data, (size_t) sizeBytes, false);
    return readAll (manager.createReaderFor (std::move (stream)), outSampleRate);
}

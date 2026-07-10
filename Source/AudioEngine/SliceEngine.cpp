// [파일 역할] SliceEngine.h의 구현. 샘플을 조각(슬라이스)으로 자르는 계산이 여기 있음.
// [스레드] 자르기 계산(rebuildSlices 등)은 모두 느긋한 메시지 스레드에서. 결과만 오디오가 읽음.
#include "SliceEngine.h"
// TransientDetector: 소리가 '탁' 시작되는 지점을 찾아주는 도우미(DSP 폴더).
#include "../DSP/TransientDetector.h"

// [함수] setSample — 자를 원본을 넣고 곧바로 슬라이스를 다시 계산.
void SliceEngine::setSample (std::shared_ptr<juce::AudioBuffer<float>> b, double sr)
{
    // 소유권 이동으로 원본 보관.
    sample = std::move (b);
    // 샘플레이트 저장(0 이하면 44100).
    sampleRate = sr > 0.0 ? sr : 44100.0;
    // 새 원본에 맞춰 조각 목록 재계산.
    rebuildSlices();
}

// [함수] publish — 새 조각 목록을 SpinLock으로 잠깐 잠그고 교체(오디오 쪽 tryGetSlice와 충돌 방지).
void SliceEngine::publish (std::vector<SlicePoint> newSlices)
{
    // 짧은 임계구역: 이 안에서만 slices를 바꿈.
    const juce::SpinLock::ScopedLockType sl (slicesLock);
    // 복사 대신 이동으로 교체(빠름).
    slices = std::move (newSlices);
}

// [함수] rebuildSlices — 현재 모드에 따라 알맞은 방식으로 조각을 다시 만듦.
void SliceEngine::rebuildSlices()
{
    // 원본이 없거나 비면 빈 목록 발행하고 끝.
    if (sample == nullptr || sample->getNumSamples() == 0)
    {
        publish ({});
        return;
    }

    // 모드별로 다른 빌더 호출.
    switch (mode)
    {
        case Transient: publish (buildTransient()); break;
        case Grid:      publish (buildGrid());      break;
        // Manual은 사용자가 sliceByManual로 이미 만든 목록을 그대로 유지.
        case Manual:    /* keep whatever sliceByManual() last published */ break;
    }
}

// [함수] buildTransient — 트랜지언트(소리 시작점)를 감지해 그 사이사이를 조각으로 만듦.
std::vector<SlicePoint> SliceEngine::buildTransient()
{
    // 이전 트랜지언트 표시 초기화.
    transients.clear();
    if (sample == nullptr)
        return {};

    // 감지 파라미터에 현재 감도 반영.
    TransientDetector::Params p;
    p.sensitivity = sensitivity;
    // 실제 감지 실행 → 시작점 위치 목록을 받음.
    transients = TransientDetector::detect (*sample, sampleRate, p);

    // Fall back to a grid if onset detection found nothing usable.
    // 하나도 못 찾았으면 균등 그리드로 대체(빈 결과 방지).
    if (transients.empty())
        return buildGrid();

    // 감지된 시작점들 사이 구간을 각각 한 조각으로 변환.
    std::vector<SlicePoint> built;
    const int numSamples = sample->getNumSamples();
    for (size_t i = 0; i < transients.size(); ++i)
    {
        const int start = transients[i];
        // 다음 시작점이 이 조각의 끝. 마지막이면 파일 끝까지.
        const int end   = (i + 1 < transients.size()) ? transients[i + 1] : numSamples;
        // 길이가 양수인 조각만 추가. { start, end-start } = SlicePoint 초기화.
        if (end > start)
            built.push_back ({ start, end - start });
    }
    return built;
}

// [함수] buildGrid — 원본을 gridDiv 등분으로 균등하게 자름. const=멤버 상태 안 바꿈.
std::vector<SlicePoint> SliceEngine::buildGrid() const
{
    std::vector<SlicePoint> built;
    if (sample == nullptr)
        return built;

    const int numSamples = sample->getNumSamples();
    // 한 조각 길이 = 전체 / 등분수. 최소 1 보장.
    const int sliceLen   = juce::jmax (1, numSamples / gridDiv);

    for (int i = 0; i < gridDiv; ++i)
    {
        const int start = i * sliceLen;
        // 시작점이 끝을 넘으면 중단.
        if (start >= numSamples)
            break;
        // 마지막 조각은 남은 전부를 포함(나눗셈 나머지 흡수), 아니면 start+sliceLen까지.
        const int end = (i == gridDiv - 1) ? numSamples : juce::jmin (numSamples, start + sliceLen);
        built.push_back ({ start, end - start });
    }
    return built;
}

// [함수] sliceByManual — 사용자가 직접 찍은 위치들로 조각을 만듦.
void SliceEngine::sliceByManual (const std::vector<int>& points)
{
    // 수동 모드로 전환.
    mode = Manual;

    std::vector<SlicePoint> built;
    const int numSamples = sample != nullptr ? sample->getNumSamples() : 0;
    if (numSamples > 0)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            // 시작점을 유효 범위로 클램프.
            const int start = juce::jlimit (0, numSamples - 1, points[i]);
            // 끝 = 다음 점(클램프) 또는 파일 끝.
            const int end   = (i + 1 < points.size())
                                  ? juce::jlimit (0, numSamples, points[i + 1])
                                  : numSamples;
            if (end > start)
                built.push_back ({ start, end - start });
        }
    }
    // 완성 목록 발행(이동).
    publish (std::move (built));
}

// [함수] getSlice — 인덱스로 한 조각을 얻음(메시지 스레드). 범위 밖이면 빈 optional.
std::optional<SlicePoint> SliceEngine::getSlice (int i) const
{
    // 범위 검사 실패 시 std::nullopt(값 없음) 반환.
    if (i < 0 || i >= (int) slices.size())
        return std::nullopt;
    return slices[(size_t) i];
}

// [함수] tryGetSlice — ★오디오 스레드★가 조각 정보를 안전히 읽음. try-lock 실패나 범위밖이면 false.
bool SliceEngine::tryGetSlice (int index, SlicePoint& out) const
{
    // [try-lock] 마침 publish 중이면 못 잡고 false → 이번 트리거는 조용히 포기(기다리지 않음).
    const juce::SpinLock::ScopedTryLockType sl (slicesLock);
    if (! sl.isLocked())
        return false;
    // 범위 검사.
    if (index < 0 || index >= (int) slices.size())
        return false;
    // out에 조각을 복사해 넘기고 성공 반환.
    out = slices[(size_t) index];
    return true;
}

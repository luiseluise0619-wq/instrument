// [파일 역할] VoicePool.h의 선언들을 '실제로 구현'한 파일(.cpp).
// [신호 체인 위치] 소리 생성의 심장부. 아래 Voice::render 안이 바로 '오디오 콜백'에서
//   돌아가는 코드라, 여기서 배우는 실시간 오디오 안전 규칙이 이 프로젝트에서 가장 중요합니다.
// [오디오 스레드 vs 메시지 스레드]
//   · 오디오 스레드: 사운드카드가 "다음 몇 밀리초 소리 내놔!"라고 주기적으로 부르는 콜백.
//     여기서 늦으면 '뚝/틱' 글리치가 남. → new/delete(메모리 할당), lock(대기), 파일접근 절대 금지.
//   · 메시지 스레드: 화면 그리기·파일 로드 등. 여기선 할당/락 OK, 대신 오디오 데이터는 조심히 넘김.
#include "VoicePool.h"

// std::move, std::remove_if, std::find 같은 표준 알고리즘/유틸을 쓰기 위한 헤더.
#include <algorithm>

//==============================================================================
// [함수] Voice::start — 이 보이스에게 "이 슬라이스를 이 설정으로 재생 시작"이라고 지시.
// 대부분 오디오 스레드에서 호출되지만, 무거운 할당은 하지 않고 '숫자 준비'만 함(안전).
void Voice::start (double hostSampleRate,
                   std::shared_ptr<const juce::AudioBuffer<float>> src, double srcSampleRate,
                   int startSample, int lengthSamples,
                   float vel,
                   float attackMs, float decayMs, float sustain0to1, float releaseMs,
                   bool reverse, bool oneShot)
{
    // [방어] 원본이 없거나 길이가 0이면 재생할 게 없으니 비활성 처리하고 즉시 빠져나감(예외 안전).
    if (src == nullptr || lengthSamples <= 0 || src->getNumSamples() == 0)
    {
        active = false;
        return;
    }

    // [메모리] std::move = 참조수를 새로 늘리는 복사 대신 '소유권 이동'(더 빠름). 이제 source가 버퍼를 붙듦.
    source        = std::move (src);
    // 원본 전체 길이(샘플 수)를 캐싱 — 매 샘플 경계검사에 쓰려고.
    srcNumSamples = source->getNumSamples();
    // 왼쪽 채널 데이터의 시작 주소(읽기 전용 포인터)를 캐싱 — 루프에서 함수 호출 없이 바로 접근하려고.
    srcL          = source->getReadPointer (0);
    // 스테레오면 오른쪽 채널, 모노면 왼쪽을 그대로 재사용(오른쪽=왼쪽). 항상 srcR이 유효하게 만듦.
    srcR          = source->getNumChannels() > 1 ? source->getReadPointer (1) : srcL;

    // [방어] 호스트 샘플레이트가 0 이하로 넘어오면 44100으로 대체(0으로 나눔 방지).
    const double hostSR = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    // [리샘플 핵심] ratio = 원본레이트 / 호스트레이트. pos를 이만큼씩 전진시키면 음정·속도가 맞음.
    ratio = (srcSampleRate > 0.0 ? srcSampleRate : hostSR) / hostSR;

    // jlimit = 값을 [최소,최대] 범위로 자름. 시작 위치가 원본 밖으로 나가지 않게 안전 클램프.
    sliceStart = (double) juce::jlimit (0, srcNumSamples - 1, startSample);
    // 슬라이스 길이도 원본 끝을 넘지 않도록 jmin으로 제한.
    length     = (double) juce::jmin (lengthSamples, srcNumSamples - (int) sliceStart);

    // 재생 방향/모드를 이번 보이스에 기록.
    reversePlay = reverse;
    oneShotMode = oneShot;

    // Reverse reads from the slice end backwards; forward reads from the start.
    // Keep pos strictly inside the slice for bounds safety.
    // 역방향이면 슬라이스 '끝-1'에서 시작해 거꾸로 읽고, 정방향이면 시작점부터 읽음.
    if (reversePlay)
        pos = (sliceStart + length) - 1.0;
    else
        pos = sliceStart;

    // 세기(0~1) 클램프.
    velocity = juce::jlimit (0.0f, 1.0f, vel);

    // ms → 출력 샘플 수 변환(초=ms*0.001, 거기에 샘플레이트 곱). jmax(1,..)로 최소 1샘플 보장(0 나눔 방지).
    attackSamples  = juce::jmax (1, (int) (attackMs  * 0.001 * hostSR));
    decaySamples   = juce::jmax (1, (int) (decayMs   * 0.001 * hostSR));
    releaseSamples = juce::jmax (1, (int) (releaseMs * 0.001 * hostSR));
    // 슬라이스 끝 5ms 페이드(끊길 때 '틱' 방지)를 샘플 수로 계산.
    endFadeSamples = juce::jmax (1, (int) (0.005 * hostSR)); // 5 ms click-free tail
    // 유지 레벨 클램프.
    sustainLevel   = juce::jlimit (0.0f, 1.0f, sustain0to1);

    // ADSR 상태 기계를 처음(attack)으로 초기화.
    stage       = Stage::attack;
    stagePos    = 0;
    releaseFrom = 1.0f;

    // 길이가 실제로 있으면 활성화(소리 냄).
    active = length > 0.0;
}

// [함수] envelope — 지금 이 순간의 음량 배수(0~1)를 ADSR 단계에 따라 계산. const=상태 안 바꿈.
float Voice::envelope() const
{
    // 기본 음량 배수 1.0에서 시작.
    float env = 1.0f;

    // [문법] switch = stage 값에 따라 다른 가지로 분기. enum이라 의미가 또렷.
    switch (stage)
    {
        // attack: 0에서 1까지 선형 상승(진행도 = 지난샘플/총샘플).
        case Stage::attack:
            env = (float) stagePos / (float) attackSamples;
            break;

        case Stage::decay:
            // Ramp from 1.0 down to the sustain level over the decay stage.
            // decay: 1.0에서 sustain 레벨까지 선형 하강.
            env = 1.0f + ((float) stagePos / (float) decaySamples) * (sustainLevel - 1.0f);
            break;

        // sustain: 손을 떼기 전까지 일정한 유지 레벨.
        case Stage::sustain:
            env = sustainLevel;
            break;

        case Stage::release:
            // Ramp from the level captured at note-off down to zero.
            // release: 손 뗀 순간 레벨(releaseFrom)에서 0까지 선형 하강.
            env = releaseFrom * (1.0f - (float) stagePos / (float) releaseSamples);
            break;

        // finished / 그 외: 소리 없음(0).
        case Stage::finished:
        default:
            env = 0.0f;
            break;
    }

    // End-of-slice fade: remaining output samples until the slice ends. Applies
    // in both directions so playback never clicks at the boundary.
    // 슬라이스 끝까지 남은 원본 샘플 수(방향에 따라 다르게 계산).
    const double srcRemaining = reversePlay ? (pos - sliceStart)
                                            : ((sliceStart + length) - pos);
    // 그걸 '출력 샘플' 시간으로 환산(ratio로 나눔). jmax로 0 나눗셈 방지.
    const double outRemaining = srcRemaining / juce::jmax (1.0e-9, ratio);
    // 끝까지 남은 시간이 페이드 길이보다 짧으면, 그 비율만큼 음량을 줄여 부드럽게 끝냄(틱 방지).
    if (outRemaining < (double) endFadeSamples)
        env *= (float) juce::jmax (0.0, outRemaining / (double) endFadeSamples);

    // 최종 배수를 0~1로 클램프해 반환.
    return juce::jlimit (0.0f, 1.0f, env);
}

// [함수] render — ★오디오 콜백★에서 불림. out 버퍼에 이 보이스 소리를 '더해서'(+=) 채움.
// [실시간 안전] 이 안에는 할당/락/파일접근이 하나도 없음 — 오직 준비된 숫자로 산수만. 이게 정답 패턴.
void Voice::render (juce::AudioBuffer<float>& out, int numSamples)
{
    // 비활성이거나 원본이 없으면 아무것도 안 함(안전 탈출).
    if (! active || source == nullptr)
        return;

    // 출력 채널 수와 각 채널의 쓰기 포인터를 미리 확보(루프 안 함수 호출 줄이기).
    const int outChannels = out.getNumChannels();
    float* outL = out.getWritePointer (0);
    float* outR = outChannels > 1 ? out.getWritePointer (1) : nullptr;

    // 슬라이스 끝 위치(원본 샘플 기준).
    const double sliceEnd = sliceStart + length;

    // [루프] 이번 블록의 샘플 하나하나를 처리. n = 0..numSamples-1.
    for (int n = 0; n < numSamples; ++n)
    {
        // Bounds / slice-end check (direction aware).
        // 재생 헤드가 슬라이스 밖으로 나갔으면 재생 종료(방향에 따라 검사 조건이 다름).
        if (reversePlay ? (pos < sliceStart) : (pos >= sliceEnd))
        {
            active = false;
            return;
        }

        // Linear interpolation for the resampled read.
        // [리샘플링] pos는 소수점 위치라 두 이웃 샘플(i0,i1) 사이를 '선형 보간'해서 읽음.
        const int   i0   = juce::jlimit (0, srcNumSamples - 1, (int) pos);
        // 다음 샘플 인덱스(끝을 넘지 않게 jmin).
        const int   i1   = juce::jmin (i0 + 1, srcNumSamples - 1);
        // 두 샘플 사이의 소수 비율(0~1).
        const float frac = (float) (pos - (double) i0);

        // 좌/우 채널 각각 선형 보간값 계산: a + frac*(b-a).
        const float sL = srcL[i0] + frac * (srcL[i1] - srcL[i0]);
        const float sR = srcR[i0] + frac * (srcR[i1] - srcR[i0]);

        // 음량 배수 = 엔벨로프 × 세기.
        const float env = envelope() * velocity;
        // [중요] '='이 아니라 '+='. 여러 보이스가 같은 out에 겹쳐 쓰므로 더해야 화음이 섞임.
        outL[n] += sL * env;
        if (outR != nullptr)
            outR[n] += sR * env;

        // Advance the read head (reverse steps by -ratio).
        // 읽기 헤드 전진(역방향이면 -ratio 만큼 뒤로).
        pos += reversePlay ? -ratio : ratio;

        // Advance the envelope state machine (output-sample time).
        // ADSR 진행도 +1, 그리고 단계 전환을 검사.
        ++stagePos;
        switch (stage)
        {
            // attack이 끝나면 decay로 넘어가고 진행도 리셋.
            case Stage::attack:
                if (stagePos >= attackSamples)
                {
                    stage    = Stage::decay;
                    stagePos = 0;
                }
                break;

            // decay가 끝나면 sustain으로.
            case Stage::decay:
                if (stagePos >= decaySamples)
                {
                    stage    = Stage::sustain;
                    stagePos = 0;
                }
                break;

            case Stage::sustain:
                // Held until the slice ends or release() moves us on.
                // sustain은 손 떼거나(release) 슬라이스가 끝날 때까지 그대로 유지.
                break;

            // release가 끝나면 완전히 정지.
            case Stage::release:
                if (stagePos >= releaseSamples)
                {
                    active = false;
                    return;
                }
                break;

            // finished / 그 외: 즉시 정지.
            case Stage::finished:
            default:
                active = false;
                return;
        }
    }
}

// [함수] release — 건반에서 손을 뗐을 때. ADSR의 release(꺼짐) 단계를 시작.
void Voice::release()
{
    // One-shot voices ignore note-off entirely.
    // one-shot은 손 떼도 무시(끝까지 재생). 비활성이면 할 것 없음.
    if (! active || oneShotMode)
        return;

    // 이미 release/finished가 아니면 release로 전환.
    if (stage != Stage::release && stage != Stage::finished)
    {
        // Capture the current level so the release ramps from where we are.
        // 현재 음량 레벨을 붙잡아 둠 — 그 지점에서 0으로 내려가야 뚝 튀지 않음.
        switch (stage)
        {
            case Stage::attack:  releaseFrom = (float) stagePos / (float) attackSamples; break;
            case Stage::decay:   releaseFrom = 1.0f + ((float) stagePos / (float) decaySamples) * (sustainLevel - 1.0f); break;
            case Stage::sustain: releaseFrom = sustainLevel; break;
            default:             releaseFrom = 1.0f; break;
        }
        // 잡은 레벨을 0~1로 클램프.
        releaseFrom = juce::jlimit (0.0f, 1.0f, releaseFrom);

        // release 단계로 진입, 진행도 리셋.
        stage    = Stage::release;
        stagePos = 0;
    }
}

// [함수] hardStop — MIDI 'All Sound Off'. one-shot 포함 모든 보이스를 아주 짧은 페이드로 강제 종료.
void Voice::hardStop()
{
    // MIDI All Sound Off: even one-shot voices must die, but through a very
    // short release ramp so the emergency stop itself doesn't click.
    // 비활성/이미 끝난 보이스는 건너뜀.
    if (! active || stage == Stage::finished)
        return;

    // 현재 레벨에서 시작해서,
    releaseFrom    = envelope();
    // one-shot 무시 규칙을 잠깐 꺼서 release가 실제로 돌게 하고,
    oneShotMode    = false;        // let the release actually run
    // 아주 짧은 64샘플 페이드로,
    releaseSamples = 64;
    // release 단계로 강제 진입.
    stage          = Stage::release;
    stagePos       = 0;
}

// [함수] normalisedPosition — 재생 헤드가 전체 샘플의 어디쯤인지 0~1로 반환(파형 재생선 표시용).
float Voice::normalisedPosition() const
{
    // 비활성/빈 원본이면 -1(표시 안 함).
    if (! active || srcNumSamples <= 0)
        return -1.0f;

    // pos를 전체 길이로 나눠 0~1 비율로.
    return juce::jlimit (0.0f, 1.0f, (float) (pos / (double) srcNumSamples));
}

//==============================================================================
// [함수] VoicePool::prepare — 재생 시작 전(메시지 스레드) 준비. 샘플레이트 저장 + 상태 초기화.
void VoicePool::prepare (juce::dsp::ProcessSpec spec)
{
    // 유효한 샘플레이트 저장(0 이하면 44100).
    hostSampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    // std::atomic<float> is not copyable, so initialise each slot explicitly.
    // atomic은 복사가 안 되므로 슬롯마다 -1(비활성)로 하나씩 초기화. relaxed=순서 보장 불필요한 단순 저장.
    for (auto& p : playheads)
        p.store (-1.0f, std::memory_order_relaxed);

    // 혹시 남아있던 소리들 모두 release.
    releaseAll();
}

// [함수] VoicePool::setSource — 새 샘플로 교체(메시지 스레드). ★무덤(graveyard) 패턴의 핵심★.
void VoicePool::setSource (std::shared_ptr<const juce::AudioBuffer<float>> src, double sampleRate)
{
    // Message thread. Prune retired buffers nobody references any more (a
    // use_count of 1 means only this graveyard holds it — voices can never
    // re-acquire an old source, so the count can only fall).
    // [정리] 무덤에서 '이제 아무도 안 쓰는' 버퍼(참조수==1, 즉 여기만 소유)를 골라 지움.
    //  remove_if는 지울 것들을 뒤로 몰고, erase로 실제로 잘라냄(erase-remove 관용구).
    //  [...] 는 람다(즉석 함수): 각 버퍼 s의 use_count()가 1인지 판정.
    retiredSources.erase (std::remove_if (retiredSources.begin(), retiredSources.end(),
                                          [] (const std::shared_ptr<const juce::AudioBuffer<float>>& s)
                                          { return s.use_count() == 1; }),
                          retiredSources.end());

    // 빠져나갈 옛 버퍼를 잠시 담을 변수.
    std::shared_ptr<const juce::AudioBuffer<float>> old;
    {
        // [스레드 안전] 아주 짧은 임계구역: 여기서만 source 포인터를 바꿈. 오디오 쪽 try-lock과 충돌 방지.
        const juce::SpinLock::ScopedLockType sl (sourceLock);
        // 기존 source를 old로 빼내고,
        old = std::move (source);
        // 새 src를 source로 넣고,
        source = std::move (src);
        // 새 원본의 샘플레이트도 갱신.
        sourceSampleRate = sampleRate > 0.0 ? sampleRate : hostSampleRate;
    }

    // Park the outgoing buffer so its final release happens here, not inside
    // processBlock when a voice slot gets reused. Never park duplicates (the
    // same buffer can come back through prepareToPlay -> setSource): two
    // graveyard entries would keep each other's use_count above 1 forever
    // and the buffer would leak.
    // [무덤에 눕히기] 옛 버퍼를 여기(메시지 스레드)에 보관 → 마지막 해제가 오디오 스레드가 아니라 여기서 일어남.
    //  단, 이미 무덤에 있는 것과 같은 버퍼면 넣지 않음(중복이면 서로 참조수를 붙들어 영영 못 지움=누수).
    if (old != nullptr && old != source
        && std::find (retiredSources.begin(), retiredSources.end(), old)
               == retiredSources.end())
        retiredSources.push_back (std::move (old));
}

// [함수] setEnvelope — 새로 눌릴 보이스가 물려받을 ADSR 값을 저장(오디오 스레드, 블록당 1회).
void VoicePool::setEnvelope (float attackMsIn, float decayMsIn, float sustain0to1, float releaseMsIn)
{
    // 음수 방지 클램프.
    attackMs   = juce::jmax (0.0f, attackMsIn);
    decayMs    = juce::jmax (0.0f, decayMsIn);
    sustainLvl = juce::jlimit (0.0f, 1.0f, sustain0to1);
    releaseMs  = juce::jmax (0.0f, releaseMsIn);
}

// [함수] setReverse — 재생 방향 플래그 저장.
void VoicePool::setReverse (bool shouldReverse)
{
    reversePlay = shouldReverse;
}

// [함수] setPlayMode — one-shot/gate 모드 플래그 저장.
void VoicePool::setPlayMode (bool oneShot)
{
    oneShotMode = oneShot;
}

// [함수] triggerVoice — ★오디오 스레드★. 빈 보이스를 찾아 재생 시작. 성공 시 번호, 실패 시 -1.
int VoicePool::triggerVoice (int startSample, int lengthSamples, float velocity)
{
    // Audio thread. Grab a snapshot of the source without blocking the loader.
    // 원본의 '스냅샷'(포인터 복사)을 뜸. 로더(메시지 스레드)를 막지 않으려고 try-lock 사용.
    std::shared_ptr<const juce::AudioBuffer<float>> src;
    double srcSR;
    {
        // [try-lock vs blocking lock] 오디오는 기다리면 글리치 → try-lock으로 '잠깐 시도'만.
        const juce::SpinLock::ScopedTryLockType sl (sourceLock);
        // 락 못 잡았거나(=setSource와 겹침) 원본이 없으면 이번 트리거는 조용히 포기(-1).
        if (! sl.isLocked() || source == nullptr)
            return -1;
        // 포인터를 복사하면 참조수가 +1 → 그 사이 교체돼도 이 버퍼는 살아있음(재생 중 사라짐 방지).
        src   = source;            // ref-count bump keeps the buffer alive
        srcSR = sourceSampleRate;
    }

    // Prefer a free voice; otherwise steal the first one.
    // 비어 있는 보이스를 우선 찾아 시작.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.isActive())
        {
            v.start (hostSampleRate, src, srcSR, startSample, lengthSamples, velocity,
                     attackMs, decayMs, sustainLvl, releaseMs, reversePlay, oneShotMode);
            return i;
        }
    }
    // 전부 사용 중이면 0번 보이스를 '빼앗아'(voice stealing) 새 음을 얹음.
    voices[0].start (hostSampleRate, src, srcSR, startSample, lengthSamples, velocity,
                     attackMs, decayMs, sustainLvl, releaseMs, reversePlay, oneShotMode);
    return 0;
}

// [함수] releaseVoice — 특정 번호 보이스에게 release 지시(범위 검사 포함).
void VoicePool::releaseVoice (int voiceIndex)
{
    if (voiceIndex >= 0 && voiceIndex < kMaxVoices)
        voices[(size_t) voiceIndex].release();
}

// [함수] releaseAll — 모든 보이스 release.
void VoicePool::releaseAll()
{
    for (auto& v : voices)
        v.release();
}

// [함수] stopAll — 모든 보이스 즉시 강제 종료(one-shot 포함).
void VoicePool::stopAll()
{
    for (auto& v : voices)
        v.hardStop();
}

// [함수] renderNextBlock — ★오디오 콜백★. 모든 보이스를 out에 합치고 재생 위치를 게시(publish).
void VoicePool::renderNextBlock (juce::AudioBuffer<float>& out, int numSamples)
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        // 이 보이스 소리를 out에 더함.
        v.render (out, numSamples);

        // Publish the playhead once per block: normalised position while
        // active, -1 once the voice has become inactive.
        // [스레드 통신] 재생 위치를 atomic에 저장 → 화면(메시지 스레드)이 락 없이 읽어감. 비활성이면 -1.
        playheads[(size_t) i].store (v.isActive() ? v.normalisedPosition() : -1.0f,
                                     std::memory_order_relaxed);
    }
}

// [함수] copyPlayheads — 메시지 스레드에서 활성 재생 위치들을 dst로 복사(락 없이 atomic 읽기).
int VoicePool::copyPlayheads (float* dst, int maxCount) const
{
    // 방어: 잘못된 인자면 0개.
    if (dst == nullptr || maxCount <= 0)
        return 0;

    // 활성(>=0)인 위치만 골라 앞에서부터 채움.
    int count = 0;
    for (int i = 0; i < kMaxVoices && count < maxCount; ++i)
    {
        const float p = playheads[(size_t) i].load (std::memory_order_relaxed);
        if (p >= 0.0f)
            dst[count++] = p;
    }
    // 실제로 채운 개수 반환.
    return count;
}

/*  [초보자 안내]
    맨 마지막 안전장치, '브릭월(벽돌벽) 리미터'예요. 어떤 일이 있어도 소리가
    천장(0dB 근처)을 못 넘게 막아 스피커와 귀를 보호합니다.
    비결은 look-ahead(미리보기): 소리를 몇 밀리초 늦게 내보내는 대신,
    그 사이에 앞으로 올 피크를 먼저 보고 볼륨을 미리 줄여 둡니다.
    덕분에 피크가 도착할 때는 이미 게인이 내려가 있어서, '퍽' 하고 잘리는
    대신 자연스럽게 눌러집니다. 헤더 한 장짜리 구현이니 통째로 읽기 좋아요.
*/

// [신호 체인에서 담당] 신호 체인의 '맨 끝'. FXChain/LoopStation을 다 지난 최종 합을 받아,
//   0dB를 넘지 않게 눌러 출력합니다. 여기서 미리보기(look-ahead) 지연이 생기므로 호스트에 알립니다.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/**
    Look-ahead brick-wall limiter.

    A short look-ahead delay lets the gain-reduction envelope ramp down before
    a peak arrives. The gain tracks the MINIMUM target over the whole
    look-ahead window — releasing only from targets that have already left the
    delay line — so no delayed peak can exit above the ceiling. Header-only.
*/
// [클래스] Limiter — 미리보기 지연 + 슬라이딩 최솟값 게인으로 천장을 절대 못 넘게 함.
class Limiter
{
public:
    // [함수] prepare — 준비. 미리보기 지연 길이와 릴리즈 계수를 계산하고 버퍼를 확보.
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        // 미리보기 길이(ms→샘플). 최소 1.
        lookaheadSamples = juce::jmax (1, (int) (lookaheadMs * 0.001 * sr));
        // 릴리즈 감쇠 계수 = exp(-1/(시간*샘플레이트)). 게인이 천천히 회복하는 속도.
        releaseCoeff = std::exp (-1.0f / (float) (releaseMs * 0.001 * sr));

        // 채널별 미리보기 지연 라인을 0으로 확보.
        for (auto& d : delayLines)
            d.assign ((size_t) lookaheadSamples, 0.0f);

        // One extra slot so a sample's target is still inside the window on
        // the step that sample leaves the delay line.
        // 목표 게인의 슬라이딩 창. 한 칸 더 크게 잡아 경계 샘플까지 창 안에 남게 함.
        targetWindow.assign ((size_t) lookaheadSamples + 1, 1.0f);

        // 위치/최솟값/게인 초기화.
        writePos  = 0;
        winPos    = 0;
        windowMin = 1.0f;
        gain      = 1.0f;
    }

    // [역할] setCeiling — 천장(최대 허용 진폭, 선형값) 설정. 0.001~1.0 클램프.
    void setCeiling (float linear) noexcept { ceiling = juce::jlimit (0.001f, 1.0f, linear); }

    /** Latency this limiter adds (report it to the host). */
    // [역할] 이 리미터가 만드는 지연(샘플). 호스트에 알려 위상 정렬에 씀.
    int getLatencySamples() const noexcept { return lookaheadSamples; }

    // [함수] process — ★오디오 콜백★. 미리보기로 피크를 먼저 보고 게인을 미리 낮춰 천장을 지킴.
    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), 2);
        const int numSamples = buffer.getNumSamples();
        if (numCh == 0 || lookaheadSamples <= 0)
            return;

        const int windowLen = (int) targetWindow.size();

        for (int n = 0; n < numSamples; ++n)
        {
            // Peak across channels at the current input sample.
            // 이번 입력 샘플의 채널 간 최대 절댓값(피크).
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, n)));

            // Target gain needed to keep this peak under the ceiling.
            // 이 피크를 천장 아래로 누르는 데 필요한 목표 게인(피크가 천장보다 크면 줄임).
            const float target = peak > ceiling ? ceiling / peak : 1.0f;

            // Sliding-window minimum of targets across the look-ahead span.
            // 창에서 빠져나갈 값(expiring)을 기억하고, 그 자리에 새 목표를 넣음. 창 위치 순환.
            const float expiring = targetWindow[(size_t) winPos];
            targetWindow[(size_t) winPos] = target;
            winPos = (winPos + 1) % windowLen;

            // 슬라이딩 최솟값 갱신.
            if (target <= windowMin)
            {
                // 새 목표가 더 작으면 그게 최솟값.
                windowMin = target;
            }
            else if (expiring <= windowMin)
            {
                // The window's minimum just fell out — rescan (rare, and the
                // window is only a couple of hundred entries).
                // 방금 창을 빠져나간 값이 최솟값이었으면 다시 훑어 최솟값 재계산(드물고 창이 작아 저렴).
                windowMin = 1.0f;
                for (const float t : targetWindow)
                    windowMin = juce::jmin (windowMin, t);
            }

            // Attack instantly to the windowed minimum; release toward it
            // only once every lower target has left the look-ahead window.
            // 어택은 즉시(최솟값으로 바로 내림), 릴리즈는 천천히(더 낮은 목표가 창을 다 빠져나간 뒤 회복).
            if (windowMin < gain) gain = windowMin;
            else                  gain = windowMin + (gain - windowMin) * releaseCoeff;

            for (int ch = 0; ch < numCh; ++ch)
            {
                // 미리보기 지연 라인에서 '과거' 샘플을 꺼내고, 그 자리에 현재를 넣음.
                auto& line = delayLines[(size_t) ch];
                const float delayed = line[(size_t) writePos];
                line[(size_t) writePos] = buffer.getSample (ch, n);
                // 지연된 샘플에 미리 낮춰둔 게인을 곱해 출력 → 피크가 도착할 땐 이미 눌려 있음.
                buffer.setSample (ch, n, delayed * gain);
            }

            // 지연 라인 쓰기 위치 순환.
            writePos = (writePos + 1) % lookaheadSamples;
        }
    }

private:
    // 샘플레이트와 설정값(미리보기/릴리즈 시간, 천장).
    double sr = 44100.0;
    float  lookaheadMs = 2.0f;
    float  releaseMs   = 50.0f;
    float  ceiling     = 0.98f;

    // 미리보기 길이/쓰기 위치/현재 게인/릴리즈 계수/채널별 지연 라인.
    int    lookaheadSamples = 1;
    int    writePos = 0;
    float  gain = 1.0f;
    float  releaseCoeff = 0.0f;
    std::vector<float> delayLines[2];

    // Sliding-minimum state for the look-ahead gain hold.
    // 슬라이딩 최솟값 상태(창 배열·위치·현재 최솟값).
    std::vector<float> targetWindow;
    int    winPos    = 0;
    float  windowMin = 1.0f;
};

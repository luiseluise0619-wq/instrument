// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '트랜지언트(소리 시작점) 검출기'예요. 드럼의 '탁', 말의 첫음처럼 소리가
// 갑자기 시작되는 순간을 찾아 그 위치(샘플 번호)를 목록으로 돌려줍니다.
// SliceEngine이 이걸 불러 샘플을 자동으로 조각냅니다(자르는 칼의 '눈' 역할).
// [방법] HFC(High-Frequency Content, 고주파 함량): 소리가 시작될 때 고주파가 확 늘어나는
//   성질을 이용. 짧은 구간마다 FFT로 주파수를 보고, 고주파 에너지가 튀는 지점을 시작점으로 봅니다.
// [스레드] FFT는 무거워서 메시지 스레드에서만 돌립니다(오디오 콜백 아님).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <algorithm>
#include <cmath>

/**
    High-Frequency-Content (HFC) transient detector.

    Header-only utility used by SliceEngine. Computes a short-time HFC onset
    function, normalises it, and returns peak positions (in samples) that lie
    above a sensitivity-scaled threshold and are separated by at least a
    minimum gap.
*/
// [클래스] TransientDetector — 정적 함수 하나(detect)로 시작점 목록을 계산.
class TransientDetector
{
public:
    // [구조체] Params — 검출 설정. FFT 창 크기·홉·감도·최소 간격.
    struct Params
    {
        int   fftOrder    = 10;    // window = 1 << fftOrder (1024)
        int   hop         = 256;
        float sensitivity = 0.3f;  // 0..1, higher = fewer onsets
        float minGapMs    = 50.0f;
    };

    // Note: no default argument for `p` — GCC/Clang reject `= {}` for a nested
    // struct with member initializers used inside the enclosing class.
    // [함수] detect — 버퍼 전체를 훑어 시작점(onset) 위치들을 반환. (p에 기본값을 못 주는 이유는 위 주석 설명.)
    static std::vector<int> detect (const juce::AudioBuffer<float>& buffer,
                                     double sampleRate,
                                     const Params& p)
    {
        std::vector<int> onsets;
        const int numSamples = buffer.getNumSamples();
        // FFT 창 크기 = 2^fftOrder(예: 1024).
        const int windowSize = 1 << p.fftOrder;
        // 소리가 창보다 짧거나 샘플레이트가 이상하면 빈 결과.
        if (numSamples < windowSize || sampleRate <= 0.0)
            return onsets;

        // 첫 채널(모노 기준)로 분석.
        const float* data = buffer.getReadPointer (0);

        // FFT 객체와 Hann 창(양끝을 부드럽게 해 스펙트럼 누설 방지) 준비.
        juce::dsp::FFT fft (p.fftOrder);
        std::vector<float> window ((size_t) windowSize);
        for (int i = 0; i < windowSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                         * (float) i / (float) windowSize);

        // FFT 작업 버퍼(크기 2배 필요)와 결과 저장용 배열들.
        std::vector<float> fftBuf ((size_t) windowSize * 2);
        std::vector<float> onsetFn;
        std::vector<int>   onsetPos;

        // hop 간격으로 창을 밀며 각 구간의 HFC 값을 계산.
        for (int start = 0; start + windowSize <= numSamples; start += p.hop)
        {
            // 작업 버퍼 초기화 후 창(window)을 곱한 신호를 채움.
            std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
            for (int i = 0; i < windowSize; ++i)
                fftBuf[(size_t) i] = data[start + i] * window[(size_t) i];

            // 크기 스펙트럼(각 주파수의 세기)으로 변환.
            fft.performFrequencyOnlyForwardTransform (fftBuf.data());

            // HFC: sum of |X[k]| weighted by bin index k.
            // HFC = 각 주파수 세기 × 주파수 번호(k)의 합. 고주파일수록 가중치 커짐.
            float hfc = 0.0f;
            for (int k = 0; k < windowSize / 2; ++k)
                hfc += fftBuf[(size_t) k] * (float) k;

            // 이 구간의 HFC 값과 시작 위치 기록.
            onsetFn.push_back (hfc);
            onsetPos.push_back (start);
        }

        // 계산된 값이 없으면 빈 결과.
        if (onsetFn.empty())
            return onsets;

        // 전체 최댓값으로 정규화 기준을 잡음.
        const float peak = *std::max_element (onsetFn.begin(), onsetFn.end());
        if (peak <= 0.0f)
            return onsets;

        // 임계값 = 최댓값 × 감도. 감도가 높을수록 문턱이 높아 시작점이 적게 잡힘.
        const float threshold = peak * juce::jlimit (0.01f, 0.99f, p.sensitivity);
        // 최소 간격(ms→샘플): 너무 가까운 시작점은 하나로 취급.
        const int   minGap    = (int) (p.minGapMs * 0.001f * (float) sampleRate);
        int lastOnset = -minGap;

        // 이웃보다 큰 '봉우리'이면서, 임계 이상이고, 이전 시작점과 충분히 떨어졌으면 시작점으로 채택.
        for (size_t i = 1; i + 1 < onsetFn.size(); ++i)
        {
            const bool isPeak = onsetFn[i] > onsetFn[i - 1] && onsetFn[i] >= onsetFn[i + 1];
            if (isPeak && onsetFn[i] >= threshold && onsetPos[i] - lastOnset >= minGap)
            {
                onsets.push_back (onsetPos[i]);
                lastOnset = onsetPos[i];
            }
        }

        return onsets;
    }
};

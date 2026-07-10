// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 '바이쿼드(biquad)' 필터 한 개예요. biquad = 2차(2nd-order) 디지털 필터로,
// 저역통과(LowPass)·고역통과(HighPass) 같은 기본 EQ의 벽돌 한 장입니다.
// 소리에서 특정 주파수 위/아래를 깎는 도구예요. FXChain의 FilterFX가 이걸 채널마다 사용합니다.
// [헤더 온리] 구현이 헤더 안에 다 들어 있어(cpp 없음) 어디서든 #include만 하면 바로 씁니다.
// [용어] '계수(coefficient)'=필터의 성질을 정하는 숫자들. '상태(z1/z2)'=지난 샘플을 기억하는 메모리.

#pragma once

#include <cmath>

/**
    Minimal Direct-Form-II transposed biquad. Header-only so it can be dropped
    into any translation unit without adding a compile target.

    Coefficients follow the RBJ audio EQ cookbook. Call one of the design
    helpers (lowPass / highPass / peak) then process() per sample.
*/
// [클래스] Biquad — 2차 IIR 필터. lowPass/highPass로 성질을 정하고 process()로 샘플마다 통과.
class Biquad
{
public:
    // [역할] reset — 내부 메모리(z1,z2)를 0으로. noexcept=예외 안 던짐(오디오에서 안전).
    void reset() noexcept { z1 = z2 = 0.0f; }

    // [역할] setSampleRate — 필터 설계 기준이 되는 샘플레이트 지정.
    void setSampleRate (double sr) noexcept { sampleRate = sr > 0.0 ? sr : 44100.0; }

    // [역할] lowPass — 저역통과 계수 계산(RBJ 쿡북 공식). freqHz 위쪽을 깎음, q=공진(뾰족함).
    void lowPass (float freqHz, float q) noexcept
    {
        // w0=정규화 각주파수, cosw/alpha=중간 계산값.
        const float w0    = twoPi * clampFreq (freqHz) / (float) sampleRate;
        const float cosw  = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * juceMax (q, 0.0001f));

        // 분자(b)·분모(a) 계수(교과서 공식 그대로).
        const float b0 = (1.0f - cosw) * 0.5f;
        const float b1 =  1.0f - cosw;
        const float b2 = (1.0f - cosw) * 0.5f;
        const float a0 =  1.0f + alpha;
        const float a1 = -2.0f * cosw;
        const float a2 =  1.0f - alpha;
        // a0로 정규화해 실제 사용 계수로 변환.
        normalise (b0, b1, b2, a0, a1, a2);
    }

    // [역할] highPass — 고역통과 계수 계산. freqHz 아래쪽을 깎음.
    void highPass (float freqHz, float q) noexcept
    {
        const float w0    = twoPi * clampFreq (freqHz) / (float) sampleRate;
        const float cosw  = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * juceMax (q, 0.0001f));

        const float b0 =  (1.0f + cosw) * 0.5f;
        const float b1 = -(1.0f + cosw);
        const float b2 =  (1.0f + cosw) * 0.5f;
        const float a0 =   1.0f + alpha;
        const float a1 =  -2.0f * cosw;
        const float a2 =   1.0f - alpha;
        normalise (b0, b1, b2, a0, a1, a2);
    }

    // [역할] process — ★오디오 콜백★. 입력 샘플 x를 필터에 통과시켜 출력 y 반환.
    // Direct-Form-II transposed 구조: z1/z2에 과거를 기억하며 한 샘플씩 계산.
    inline float process (float x) noexcept
    {
        const float y = b0c * x + z1;
        z1 = b1c * x - a1c * y + z2;
        z2 = b2c * x - a2c * y;
        return y;
    }

private:
    // 원주율 2배(각주파수 계산용) 상수.
    static constexpr float twoPi = 6.28318530717958647692f;

    // 작은 도우미: 둘 중 큰 값(0 나눗셈 방지용).
    static float juceMax (float a, float b) noexcept { return a > b ? a : b; }
    // 컷오프 주파수를 10Hz~나이퀴스트(0.49*fs)로 클램프(불안정 방지).
    float clampFreq (float f) const noexcept
    {
        const float nyq = (float) sampleRate * 0.49f;
        return f < 10.0f ? 10.0f : (f > nyq ? nyq : f);
    }

    // [도우미] normalise — 모든 계수를 a0로 나눠 실제 사용 계수(b0c..a2c)로 저장.
    void normalise (float b0, float b1, float b2,
                    float a0, float a1, float a2) noexcept
    {
        const float inv = 1.0f / a0;
        b0c = b0 * inv; b1c = b1 * inv; b2c = b2 * inv;
        a1c = a1 * inv; a2c = a2 * inv;
    }

    // 샘플레이트, 정규화된 계수들, 상태 메모리(z1/z2).
    double sampleRate = 44100.0;
    float b0c = 1.0f, b1c = 0.0f, b2c = 0.0f, a1c = 0.0f, a2c = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

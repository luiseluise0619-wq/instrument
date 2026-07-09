# CODE_TOUR — Slyce 코드 여행 가이드 (초보자용)

프로그래밍을 처음 배우는 사람이 이 저장소를 읽을 수 있게 쓴 안내서입니다.
코드 파일 상단에도 같은 톤의 `[초보자 안내]` 주석이 달려 있으니, 이 문서로
큰 그림을 잡고 파일로 들어가면 됩니다.

---

## 1. 이게 뭐 하는 물건인가 — VST 플러그인이란?

**Slyce**는 목소리(또는 아무 샘플)를 조각내서 건반으로 연주하는 악기예요.

- **DAW** = 음악 만드는 프로그램 (Logic, Ableton, FL Studio, Cubase...).
- **플러그인** = DAW에 꽂아 쓰는 확장 프로그램. 악기(소리를 냄)일 수도,
  이펙트(소리를 가공함)일 수도 있어요. Slyce는 **악기**입니다.
- **VST3 / AU** = 플러그인의 "규격"(콘센트 모양 같은 것). 같은 코드를
  VST3(윈도우·리눅스·맥), AU(맥 전용), Standalone(DAW 없이 단독 실행)
  세 가지로 구워냅니다.
- **JUCE** = 이 세 가지 규격을 한 번에 처리해 주는 C++ 프레임워크.
  우리는 음악 로직만 쓰고, "DAW와 대화하는 법"은 JUCE가 다 해줍니다.

사용 흐름: 샘플을 드래그해서 넣는다 → 자동으로 조각(슬라이스)난다 →
MIDI 건반(C3부터 반음씩)이나 화면 패드로 조각을 연주한다.

---

## 2. 이 코드의 핵심 교육 가치 — 오디오 스레드 vs 메시지 스레드

이 저장소에서 단 하나만 배워 간다면 이것입니다.

프로그램 안에는 여러 "일꾼(스레드)"이 동시에 일합니다. 오디오 플러그인엔
성격이 정반대인 일꾼 둘이 있어요:

| | 오디오 스레드 | 메시지 스레드 |
|---|---|---|
| 하는 일 | `processBlock()` — 소리 계산 | UI 그리기, 클릭 처리, 파일 로드 |
| 호출 빈도 | 초당 수백 번 (예: 48000Hz ÷ 256샘플 ≈ 187회/초) | 필요할 때 |
| 마감 | **밀리초 단위, 절대 엄수** | 좀 늦어도 됨 (최악: 화면이 살짝 버벅) |
| 늦으면 | 스피커에서 "뚝/지직" (글리치) | 아무도 안 죽음 |

### 왜 실시간 코드는 특별한가

오디오 스레드가 마감을 한 번이라도 놓치면 청취자 귀에 바로 들립니다.
그래서 이 경로에서는 **"얼마나 걸릴지 보장 못 하는 일"을 전부 금지**해요:

- **메모리 할당 금지** (`new`, `malloc`, `std::vector::push_back`...) —
  OS가 메모리를 찾느라 얼마나 걸릴지 모름.
- **잠금(lock) 금지** — 다른 스레드가 잠가놨으면 무한정 기다리게 됨
  (이걸 "priority inversion"이라고 해요).
- **파일/네트워크 금지** — 디스크는 밀리초가 아니라 수십 밀리초 단위.

대신 이 코드가 쓰는 안전한 도구들:

- **미리 할당** — 버퍼는 전부 `prepareToPlay()`(재생 시작 전)에 확보.
- **`std::atomic`** — 잠금 없이 값 하나를 안전하게 주고받는 원자 변수.
  UI가 재생 위치를 읽을 때 이걸 씁니다.
- **try-lock** — 잠겨 있으면 기다리지 않고 그냥 포기.
  `SliceEngine::tryGetSlice()`가 대표 사례: 재슬라이스와 겹친 그 한 번의
  트리거는 버리는 게, 오디오 전체가 멈추는 것보다 낫다.
- **무덤(graveyard) 패턴** — `VoicePool::setSource()` 참고. 새 샘플을
  로드해도 옛 버퍼를 바로 지우지 않고 "무덤 목록"에 보관합니다. 아직 그
  버퍼로 소리 내는 보이스가 있을 수 있으니까요. 아무도 안 쓰는 게
  확인되면(참조 카운트 1) 메시지 스레드에서 치웁니다 — 메모리 해제가
  오디오 스레드에서 터지는 걸 원천 차단.

---

## 3. 신호 흐름 — 소리가 지나가는 길

```
샘플 파일 드롭
   │  SampleLoader        (WAV/AIFF/FLAC/Ogg → 메모리 버퍼로 해독)
   ▼
SliceEngine               (조각내기: transient 자동감지 / 균등분할 / 수동)
   │
MIDI 노트 (C3=조각1, 반음마다 다음 조각)
   ▼
VoicePool                 (여러 조각을 동시에 재생하는 보이스 창고 + ADSR)
   ▼
PitchFormant              (음정·포먼트 분리 조절 — 다람쥐 소리 방지)
   ▼
GranularEngine            (알갱이 질감 — mix 올리기 전엔 통과만)
   ▼
FXChain                   (드라이브 → 리버브 → 딜레이)
   ▼
Stereo Width              (좌우 넓힘, mid/side)
   ▼
Limiter                   (look-ahead 브릭월 — 최종 안전장치)
   ▼
DAW / 스피커
```

이 순서는 `PluginProcessor.cpp`의 `processBlock()` 안에 그대로 코드로
적혀 있습니다. 문서와 코드를 나란히 놓고 읽어 보세요.

---

## 4. 빌드하는 법 (CMake)

C++은 파이썬과 달리 "컴파일"(기계어로 번역) 과정이 필요합니다.
CMake는 그 번역 과정을 지휘하는 도구예요.

준비물: CMake 3.22+, Git, C++ 컴파일러(맥: Xcode, 윈도우: Visual Studio,
리눅스: gcc/clang).

```bash
# 가장 쉬운 길 — 헬퍼 스크립트
./build.sh        # macOS / Linux
build.bat         # Windows

# 직접 할 때
cmake -B build    # 1) 빌드 준비 (JUCE를 자동으로 내려받음 — 최초 1회 오래 걸림)
cmake --build build --config Release   # 2) 실제 컴파일
```

결과물은 `build/` 아래에 VST3 / AU / Standalone 으로 생깁니다.
이미 JUCE를 받아 둔 게 있으면 `-DJUCE_SOURCE_DIR=/path/to/JUCE`로 재사용.

---

## 5. 파일 지도 — 어디부터 읽을까

읽는 순서 추천: **①→②→③** 순서로 가면 큰 그림 → 실시간 규칙 → 부품 순.

| 순서 | 파일 | 뭐 하는 파일 |
|---|---|---|
| ① | `Source/PluginProcessor.h/.cpp` | 심장. 모든 엔진이 꽂혀 있고 `processBlock()`이 소리를 만듦 |
| ① | `Source/PluginEditor.h/.cpp` | 얼굴(UI). 메시지 스레드에서 돌고, atomic으로 오디오 상태를 훔쳐봄 |
| ② | `Source/AudioEngine/VoicePool.h/.cpp` | 보이스 창고 + 무덤 패턴 (실시간 메모리 관리의 교과서) |
| ② | `Source/AudioEngine/SliceEngine.h/.cpp` | 조각내기 + try-lock 발행 패턴 |
| ③ | `Source/AudioEngine/SampleLoader.h/.cpp` | 오디오 파일 해독 |
| ③ | `Source/AudioEngine/PitchFormant.h/.cpp` | 음정/포먼트 (Signalsmith Stretch 사용, 지연 보정) |
| ③ | `Source/AudioEngine/SynthEngine.h/.cpp` | 별도의 신시사이저 엔진 (오실레이터·필터·ADSR) |
| ③ | `Source/AudioEngine/GranularEngine.h/.cpp` | 알갱이(그레인) 질감 |
| ③ | `Source/AudioEngine/FXChain.h/.cpp` | 드라이브/리버브/딜레이 |
| ③ | `Source/DSP/Limiter.h` | 헤더 1장짜리 브릭월 리미터 — 짧아서 통독 추천 |
| ③ | `Source/DSP/Biquad.h`, `TransientDetector.h` | 필터 수학, 온셋 감지 |
| — | `Source/UI/*` | 화면 부품들 (파형 뷰, 패드 그리드, 노브, 테마...) |
| — | `CMakeLists.txt` | 빌드 설계도 (JUCE·Signalsmith를 자동으로 가져옴) |

파라미터 전체 목록(음정·필터·엔벨로프 등 21개)과 프리셋은 `README.md`에
표로 정리되어 있습니다.

---

## 6. 용어 미니 사전

- **샘플(sample)** — ① 녹음된 소리 파일 ② 디지털 소리의 최소 단위 숫자 하나.
  문맥으로 구분해요 (48kHz = 1초에 숫자 48000개).
- **버퍼(buffer)** — 샘플 숫자들을 담는 배열. `processBlock`은 한 번에
  버퍼 하나(보통 128~512샘플)씩 처리합니다.
- **MIDI** — "어떤 건반을 얼마나 세게 눌렀다/뗐다"라는 악보 신호. 소리 자체가
  아니라 지시서예요.
- **ADSR** — Attack(시작) Decay(감쇠) Sustain(유지) Release(여운).
  소리 크기가 시간에 따라 그리는 곡선.
- **APVTS** — `AudioProcessorValueTreeState`. JUCE의 파라미터 금고.
  UI 노브와 오디오 코드와 DAW 자동화(automation)를 한 곳에서 묶어줍니다.
- **latency(지연)** — 처리 때문에 소리가 늦게 나오는 양. 숨기는 게 아니라
  DAW에 정직하게 신고하면 DAW가 다른 트랙을 밀어서 맞춰줍니다.

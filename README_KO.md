# SLYCE — Reference UI 교체본

**화면 식별자: `REF-20260920-04` · 소스 프로젝트 버전: 3.3.5**

이 ZIP은 이전 통합 소스를 포함하는 전체 프로젝트다. 음향 엔진을 다시 바꾸는 패치가 아니라, 사용자 레퍼런스를 기준으로 **기본 에디터 자체를 교체하는 UI 수정본**이다.

## 이번에 달라진 구조

이전 전달본의 `createEditor()`는 기존 `VocalChopAudioProcessorEditor`를 생성했다. 이번에는 `SlyceReferenceEditor`를 직접 생성한다. CMake에서도 기존 `PluginEditor.cpp` 대신 `Source/UI/Reference/ReferenceEditor.cpp`를 컴파일한다. 이전 에디터 파일은 비교용으로 남아 있지만 빌드되지 않는다.

단순한 색상 테이블 변경이 아니다. 1536×1024 레퍼런스 좌표로 브라우저, 프리셋 바, 파형, 2×4 패드, 2×2 FX 노브, 미니 루퍼, 3개 Tone 노브, 건반을 배치했다. 전체 화면은 동일 종횡비로 확대/축소된다.

프레임·로고·유리 질감·외곽 반사는 사용자가 제공한 이미지에서 가져온 **정적 스킨**이다. 하지만 스크린샷 전체를 덮어 씌우는 방식은 아니다. 원본의 파형·숫자·이름·검색·노브·키·패드 부분은 제거했고, 그 위에 실제 JUCE 컴포넌트를 구현했다. 슬라이더는 기존 APVTS 파라미터에, 파형/패드는 로딩된 오디오와 SliceEngine에, 건반은 기존 노트 이벤트 경로에 연결된다.

원본 이미지의 가상 값 대신 실제 데이터가 표시된다. 악기 개수, 프리셋 수, 현재 음색 이름, 파형, 슬라이스 수, 음정 및 노브 각도는 로딩된 프로젝트 상태에 따라 바뀐다. 보컬 목록은 사용자 제공 6곡에서 보컬 스템만 분리해 만든 24개 뱅크, 총 192개 가창 슬라이스로 교체했다. 이전 합성 보컬과 드럼·하이햇·입 퍼커션 소스는 출시 바이너리에 포함하지 않는다.

## 실행 전에 확인

이 파일은 **소스 코드**이며 미리 컴파일된 Windows VST3/EXE가 아니다. 소스 파일을 복사하는 것만으로 기존에 설치된 플러그인의 모습은 바뀌지 않는다.

Windows에서는 새 폴더에 압축을 풀고 PowerShell에서 다음을 실행한다.

```powershell
.\BUILD_REFERENCE_WINDOWS.ps1
```

이 스크립트는 `build-reference-ui`라는 별도 빌드 폴더를 사용한다. 기존 설치본을 자동으로 삭제하거나 덮어쓰지 않는다. 빌드 성공 후 다음 **새 실행파일**을 먼저 연다.

```text
build-reference-ui/VocalChopStudio_artefacts/Release/Standalone/Slyce.exe
```

새 VST3 위치:

```text
build-reference-ui/VocalChopStudio_artefacts/Release/VST3/Slyce.vst3
```

상단 `...` 메뉴에 **Build REF-20260920-04**가 있어야 이번 에디터다. 이 식별자가 없으면 다른 빌드/설치본을 실행 중이다. DAW에서 사용할 때는 DAW를 종료하고 기존 VST3를 백업한 다음, 새 `.vst3` **전체 번들**을 교체하고 재검색한다. 같은 이름의 설치본이 여러 경로에 중복되지 않았는지 확인한다.

빌드에는 Visual Studio 2022 C++ 도구, CMake 3.24 이상, JUCE 및 기존 DSP 의존성이 필요하다. 인터넷 다운로드를 사용할 수 없는 환경은 로컬 소스를 지정한다.

```powershell
.\BUILD_REFERENCE_WINDOWS.ps1 `
  -JuceSourceDir "C:\SDK\JUCE" `
  -SignalsmithStretchSourceDir "C:\SDK\signalsmith-stretch" `
  -SignalsmithLinearSourceDir "C:\SDK\signalsmith-linear"
```

macOS/Linux는 `BUILD_REFERENCE_MAC_LINUX.sh`를 사용한다. 기존 음원·세션·라이선스 처리와 플러그인 식별 코드는 유지한다.

## 테마 / 기능 위치

기본 테마는 **Paper Light**다. 상단 `... → Theme`에서 Paper Light / Noir Violet을 선택한다. 저장된 이전 세션의 다크 레퍼런스 테마는 유지될 수 있다.

상단 `...` 메뉴에 음원 가져오기, SFZ 악기 로딩, 사용자 프리셋 저장, 4개 엔진 선택, 상세 ADSR/FX/Master/Synth, 코드 바, 라이선스 등록을 남겨두었다. `LOOP` 또는 미니 루퍼를 누르면 실제 6트랙 루퍼가 열린다. `MOD`, `FX`, `MASTER`는 기존 파라미터 상세 패널을 연다. `WAVEFORM`을 누르면 레퍼런스 메인 화면으로 돌아온다.

파형 마커는 드래그하고, 파형을 더블클릭하면 마커를 추가/제거한다. 8개보다 많은 슬라이스는 `N SLICES` 메뉴 또는 패드 위 마우스 휠로 페이지를 바꾼다. 패드 우측 `...`에서는 음정과 WAV 내보내기를 선택한다. 피치/모듈레이션 휠과 노브는 실제 파라미터에 연결된다.

`SYNC`의 범위는 기존과 같다. 메트로놈/녹음 그리드의 호스트 BPM 연동이며, 저장된 루프의 타임스트레치를 새로 추가한 것은 아니다.

## 미리보기와 실제 검증 범위

`preview/index.html`은 동일 스킨 이미지와 같은 좌표 계약(`assets/reference/layout.json`)을 사용하는 **브라우저 미리보기**다. 실제 JUCE 실행화면 또는 오디오 플러그인이 아니며, 표시된 8등분 파형은 포함된 합성 WAV의 예시다. `preview/skin_preview_light.png`, `skin_preview_dark.png`도 이 브라우저에서 렌더링한 레이아웃 미리보기다.

실제 JUCE 화면을 저장하는 별도 도구도 있다.

```powershell
.\BUILD_REFERENCE_WINDOWS.ps1 -Snapshots
```

이 옵션은 실제 `createEditor()`로 만든 에디터를 캡처해 `native-ui-screenshots`에 저장한다. 브라우저 미리보기를 네이티브 캡처로 이름만 바꾸지 않는다.

**이번 환경에서 실제 JUCE SDK 전체 빌드와 DAW 실행은 검증하지 못했다.** 의존성 다운로드가 DNS 제한으로 실패했다. 따라서 운영체제별 폰트 렌더링, 네이티브 컴파일/API 호환, 팝업 위치, HiDPI 및 전체 DAW 동작은 아래 절차로 확인해야 한다. 정적 검사 또는 대체 JUCE shim 기반 DSP 테스트 통과를 실제 플러그인 빌드 성공으로 해석하면 안 된다.

검사 기록: `reports/reference_ui_contract.txt`, `reports/reference_preview_checks.json`, `reports/reference_ctest.txt`. 이전 `reports/`의 다른 파일은 이전 통합본의 검사 기록으로 이번 네이티브 UI 빌드 결과가 아니다.

## 핵심 수정 파일

```text
Source/PluginProcessor.cpp                    # 새 에디터 생성 경로
Source/UI/Reference/ReferenceEditor.h/.cpp     # 실제 기본 화면과 이벤트 연결
Source/UI/Reference/ReferenceLayout.h          # 레퍼런스 좌표
Source/UI/ThemeManager.cpp                    # 기본 Paper Light
assets/reference/*.png                       # 정적 프레임/금속 노브 스킨
assets/reference/layout.json                  # 소스/미리보기 공유 좌표
CMakeLists.txt                               # 신규 UI와 스킨을 실제 빌드에 연결
BUILD_REFERENCE_WINDOWS.ps1                  # 별도 빌드 경로 / 잘못된 이전 실행파일 방지
```

원본 WAV와 DSP 소스는 이번 UI 수정에서 변경하지 않았다. 이전 통합본을 덮어쓰기 전에 백업하고, 먼저 새 폴더에서 빌드/테스트한다.

SLYCE Windows 3.3.5 — Arcade Pulse / MIDI

1. Standalone 실행

압축을 푼 뒤 `Standalone\Slyce.exe`를 실행합니다.
상단 메뉴에서 `Theme → Arcade Pulse`를 선택하면 네온 마젠타/시안 글래스 스킨을 사용할 수 있습니다.

2. VST3 설치

1) FL Studio, Logic 호스트, Ableton 등 DAW를 종료합니다.
2) 이 폴더의 `install-windows.bat`를 관리자 권한으로 실행합니다.
3) 설치가 끝나면 DAW의 플러그인 재검색을 실행합니다.

설치 위치:
`C:\Program Files\Common Files\VST3\Slyce.vst3`

기존 SLYCE 버전은 삭제하지 않고 사용자 폴더의 `Slyce-previous-version`으로 이동합니다.

3. 보컬찹 사용

메인 화면의 `LOAD / DROP YOUR SAMPLE`에 WAV, AIFF, FLAC, OGG 또는 MP3를 넣습니다.
`AUTO` 또는 `DETECT`로 슬라이스한 뒤 키보드로 연주합니다.
루퍼에서 트랙의 악기 메뉴를 열고 `Current Vocal Chop`을 선택하면 현재 보컬찹을 해당 트랙에 녹음할 수 있습니다.

4. 루퍼 카운트인

루퍼의 `Click`이 켜져 있으면 첫 녹음 전에 1마디 카운트인이 들립니다.
카운트인 클릭은 녹음된 루프에 포함되지 않습니다.

5. MIDI 설정

루퍼 상단의 `MIDI` 버튼을 누르면 기능별 CC 번호를 직접 지정할 수 있습니다.
`Apply`를 누르면 적용되고, 플러그인 상태 저장 시 설정도 함께 저장됩니다.

기본 매핑은 트랙 1–6 녹음 CC20–25, 전체 정지 CC26, 전체 재생 CC27입니다.

6. 설치가 안 될 때

- DAW가 실행 중이면 종료한 뒤 다시 설치합니다.
- 설치 파일과 `Slyce.vst3`가 같은 ZIP에서 풀렸는지 확인합니다.
- FL Studio에서는 Options → Manage plugins → Find more plugins로 재검색합니다.
- `Slyce.vst3` 전체 폴더를 복사해야 하며 내부 DLL만 따로 복사하면 안 됩니다.

빌드 식별자: 3.3.5 / Windows x64

7. Pro Tools용 unsigned AAX 개발 빌드

AAX는 Avid SDK가 필요한 별도 포맷이며 SDK 자체는 배포 파일에 포함하지 않습니다.
Avid 계정으로 SDK를 받은 뒤 경로를 지정하고 CMake를 다시 구성하면 개발/테스트용
서명되지 않은 `.aaxplugin`을 생성할 수 있습니다.

```powershell
$env:AAX_SDK_PATH = "C:\SDK\AAX_SDK"
cmake -S . -B build-aax -G "Visual Studio 17 2022" -A x64 `
  -DSLYCE_AAX_SDK_PATH="$env:AAX_SDK_PATH"
cmake --build build-aax --config Release --target VocalChopStudio_AAX
```

unsigned AAX는 개발용 Pro Tools 테스트 목적입니다. 일반 사용자에게 배포하려면
Avid 규격의 디지털 서명이 필요합니다.

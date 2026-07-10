// [파일 역할] SamplerEngine.h의 구현. .sfz 텍스트 파싱 + 오디오 디코딩 + 재생.
#include "SamplerEngine.h"

// std::function: 콜백(함수 객체)을 담는 표준 타입. 파서에 넘길 콜백에 사용.
#include <functional>

// [내부 전용 도우미] 이 파일에서만 쓰는 파싱 함수들.
namespace
{
    // One opcode token: name=value, where value runs until the next
    // " name=" boundary (sample paths may contain spaces).
    // [함수] parseOpcodes — 한 줄에서 "이름=값" 쌍들을 뽑아 콜백 cb로 넘김.
    //   값에 공백이 있을 수 있어(샘플 경로), 다음 "이름=" 직전까지를 값으로 봄.
    void parseOpcodes (const juce::String& line,
                       std::function<void (const juce::String&, const juce::String&)> cb)
    {
        int i = 0;
        const int len = line.length();
        while (i < len)
        {
            // '=' 위치를 찾음. 없으면 끝.
            const int eq = line.indexOfChar (i, '=');
            if (eq < 0)
                break;

            // Opcode name = last word before '='.
            // 이름 = '=' 바로 앞의 한 단어(공백 전까지 뒤로 훑음).
            int nameStart = eq;
            while (nameStart > i && ! juce::CharacterFunctions::isWhitespace (line[nameStart - 1]))
                --nameStart;
            const auto name = line.substring (nameStart, eq).trim().toLowerCase();

            // Value runs to the start of the NEXT opcode (word directly
            // followed by '='), or to end of line.
            // 값 = 다음 '=' 앞 단어 시작 전까지(다음 옵코드가 시작되는 지점), 없으면 줄 끝까지.
            int valEnd = len;
            const int nextEq = line.indexOfChar (eq + 1, '=');
            if (nextEq >= 0)
            {
                int ws = nextEq;
                while (ws > eq + 1 && ! juce::CharacterFunctions::isWhitespace (line[ws - 1]))
                    --ws;
                valEnd = ws;
            }
            const auto value = line.substring (eq + 1, valEnd).trim();
            // 이름이 비어있지 않으면 콜백 호출.
            if (name.isNotEmpty())
                cb (name, value);
            // 다음 옵코드 위치로 이동.
            i = valEnd;
        }
    }

    // [함수] noteNameToMidi — "60" 같은 숫자나 "c4","c#4","db3" 같은 음이름을 MIDI 번호로. SFZ: C4=60.
    int noteNameToMidi (const juce::String& v)
    {
        // Numeric ("60") or names like c4, c#4, db3 (SFZ: C4 = 60).
        // 전부 숫자면 그대로 정수로.
        if (v.containsOnly ("0123456789-"))
            return v.getIntValue();

        const auto s = v.toLowerCase();
        // a~g 각 음의 반음 오프셋 표.
        static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
        // 첫 글자가 a~g가 아니면 실패(-1).
        if (s.isEmpty() || s[0] < 'a' || s[0] > 'g')
            return -1;
        int semi = base[s[0] - 'a'];
        int idx = 1;
        // 샤프(#)/플랫(b) 반영.
        if (idx < s.length() && (s[idx] == '#' || s[idx] == 'b'))
        {
            semi += (s[idx] == '#') ? 1 : -1;
            ++idx;
        }
        // 옥타브 숫자를 읽어 MIDI 번호 계산(0~127 클램프).
        const int octave = s.substring (idx).getIntValue();
        return juce::jlimit (0, 127, (octave + 1) * 12 + semi);
    }
} // namespace

// [함수] loadSfz — ★메시지 스레드★. .sfz를 읽어 리전들을 디코딩하고 새 Bank로 교체.
bool SamplerEngine::loadSfz (const juce::File& sfzFile, juce::String& error)
{
    // 파일 존재 확인.
    if (! sfzFile.existsAsFile())
    {
        error = "File not found: " + sfzFile.getFullPathName();
        return false;
    }

    // 오디오 포맷 매니저에 기본 포맷 등록(wav/aiff/flac/ogg 읽기).
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();   // wav, aiff, flac, ogg

    // 새 뱅크 생성(파일명을 이름으로).
    auto newBank = std::make_shared<Bank>();
    newBank->name = sfzFile.getFileNameWithoutExtension();

    // 파싱 상태들: 기본 경로, 그룹/현재 리전, 진행 상태, 누적 바이트.
    juce::File defaultPath = sfzFile.getParentDirectory();
    Region groupDefaults, current;
    bool inRegion = false;
    int64_t totalBytes = 0;
    // 디코딩 총량 상한 1.6GB(과도한 RAM 사용 방지).
    constexpr int64_t kMaxBytes = (int64_t) 1600 * 1024 * 1024;   // 1.6 GB decoded cap

    // 파일 전체를 줄 단위로 읽음.
    juce::StringArray lines;
    lines.addLines (sfzFile.loadFileAsString());

    // 현재 만드는 리전/그룹의 샘플 경로.
    juce::String pendingSample;   // path of the region being built
    juce::String groupSample;

    // [람다] applyOpcode — 옵코드 하나(name=value)를 리전 r에 반영.
    auto applyOpcode = [&] (Region& r, juce::String& samplePath,
                            const juce::String& name, const juce::String& value)
    {
        // 각 옵코드를 해당 필드에 매핑.
        if      (name == "sample")          samplePath = value;
        else if (name == "default_path")    defaultPath = sfzFile.getParentDirectory()
                                                              .getChildFile (value.replaceCharacter ('\\', '/'));
        else if (name == "lokey")           r.lokey = noteNameToMidi (value);
        else if (name == "hikey")           r.hikey = noteNameToMidi (value);
        else if (name == "key")             { r.lokey = r.hikey = r.root = noteNameToMidi (value); }
        else if (name == "pitch_keycenter") r.root  = noteNameToMidi (value);
        else if (name == "lovel")           r.lovel = value.getIntValue();
        else if (name == "hivel")           r.hivel = value.getIntValue();
        else if (name == "tune")            r.tuneCents = value.getFloatValue();
        else if (name == "volume")          r.volumeDb  = value.getFloatValue();
        else if (name == "loop_mode")       r.loop = value.trim().startsWithIgnoreCase ("loop");
        else if (name == "loop_start")      r.loopStart = value.getIntValue();
        else if (name == "loop_end")        r.loopEnd   = value.getIntValue();
        else if (name == "ampeg_release")   r.releaseSeconds = juce::jlimit (0.01f, 8.0f,
                                                                             value.getFloatValue());
    };

    // [람다] finishRegion — 지금까지 모은 리전을 실제 오디오와 함께 뱅크에 추가.
    auto finishRegion = [&]() -> bool
    {
        // 리전 중이 아니면 할 것 없음.
        if (! inRegion)
            return true;
        inRegion = false;

        // 샘플 경로 결정(리전 우선, 없으면 그룹).
        auto path = pendingSample.isNotEmpty() ? pendingSample : groupSample;
        if (path.isEmpty())
            return true;   // header-only region: ignore

        // 실제 오디오 파일을 읽는 리더 생성.
        const auto audioFile = defaultPath.getChildFile (path.replaceCharacter ('\\', '/'));
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (audioFile));
        if (reader == nullptr)
        {
            error = "Can't read sample: " + audioFile.getFullPathName();
            return false;
        }

        // 리전 복사 후 채널/용량 확인.
        Region r = current;
        const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);
        totalBytes += (int64_t) reader->lengthInSamples * numCh * (int64_t) sizeof (float);
        // 상한 초과면 실패(메모리 보호).
        if (totalBytes > kMaxBytes)
        {
            error = "This bank decodes to more than 1.6 GB of RAM - use a "
                    "smaller instrument set.";
            return false;
        }

        // 오디오를 RAM 버퍼로 디코딩.
        r.data.setSize (numCh, (int) reader->lengthInSamples);
        reader->read (&r.data, 0, (int) reader->lengthInSamples, 0, true, numCh > 1);
        r.srcRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
        // 루프 구간이 이상하면 루프 끔.
        if (r.loopEnd <= r.loopStart || r.loopEnd > r.data.getNumSamples())
            r.loop = false;

        // 완성 리전을 뱅크에 추가.
        newBank->regions.push_back (std::move (r));
        return true;
    };

    // 각 줄을 순회하며 헤더/옵코드를 처리.
    for (auto rawLine : lines)
    {
        // '//' 주석 제거하고 공백 정리.
        auto line = rawLine.upToFirstOccurrenceOf ("//", false, false).trim();
        if (line.isEmpty())
            continue;

        // Headers and opcodes can share a line; walk header by header.
        // 한 줄에 헤더와 옵코드가 섞일 수 있어 헤더 단위로 훑음.
        while (line.isNotEmpty())
        {
            // <region> 시작: 이전 리전 마감 후 새 리전 시작(그룹 기본값 상속).
            if (line.startsWithIgnoreCase ("<region>"))
            {
                if (! finishRegion())
                    return false;
                inRegion = true;
                current = groupDefaults;
                pendingSample = {};
                line = line.substring (8).trim();
                continue;
            }
            // <group> 시작: 이전 리전 마감 후 그룹 기본값 초기화 + 뒤따르는 옵코드 반영.
            if (line.startsWithIgnoreCase ("<group>"))
            {
                if (! finishRegion())
                    return false;
                groupDefaults = {};
                groupSample = {};
                line = line.substring (7).trim();
                // Opcodes after <group> configure the group defaults.
                parseOpcodes (line.upToFirstOccurrenceOf ("<", false, false),
                              [&] (const juce::String& n, const juce::String& v)
                              { applyOpcode (groupDefaults, groupSample, n, v); });
                const int next = line.indexOfChar ('<');
                line = next >= 0 ? line.substring (next) : juce::String();
                continue;
            }
            // 그 밖의 헤더(<control>/<global> 등): default_path만 반영.
            if (line.startsWith ("<"))   // <control>, <global>, <curve>...
            {
                const auto body = line.fromFirstOccurrenceOf (">", false, false);
                parseOpcodes (body.upToFirstOccurrenceOf ("<", false, false),
                              [&] (const juce::String& n, const juce::String& v)
                              {
                                  if (n == "default_path")
                                      applyOpcode (groupDefaults, groupSample, n, v);
                              });
                const int next = body.indexOfChar ('<');
                line = next >= 0 ? body.substring (next) : juce::String();
                continue;
            }

            // Plain opcode line for the current region/group.
            // 헤더 없이 옵코드만 있는 줄: 리전 중이면 현재 리전에, 아니면 그룹 기본값에 반영.
            parseOpcodes (line, [&] (const juce::String& n, const juce::String& v)
            {
                if (inRegion) applyOpcode (current, pendingSample, n, v);
                else          applyOpcode (groupDefaults, groupSample, n, v);
            });
            break;
        }
    }
    // 마지막 리전 마감.
    if (! finishRegion())
        return false;

    // 하나도 못 만들었으면 실패.
    if (newBank->regions.empty())
    {
        error = "No playable <region> entries found in this .sfz.";
        return false;
    }

    // [스레드 교체] 완성된 뱅크를 원자적으로 교체 → 재생 중인 보이스는 옛 뱅크를 계속 붙들어 안전.
    std::atomic_store (&bank, std::shared_ptr<const Bank> (newBank));
    return true;
}

// [함수] findRegion — 노트·세기에 정확히 맞는 리전을 찾고, 없으면 가장 가까운 건반 범위로 대체.
const SamplerEngine::Region* SamplerEngine::findRegion (const Bank& b, int note, int vel127) const
{
    const Region* best = nullptr;
    // 건반+세기 범위에 딱 맞는 첫 리전.
    for (const auto& r : b.regions)
        if (note >= r.lokey && note <= r.hikey && vel127 >= r.lovel && vel127 <= r.hivel)
        {
            best = &r;
            break;
        }
    if (best == nullptr)   // fall back to nearest key range, any velocity
    {
        // 못 찾으면 건반 거리(d)가 가장 가까운 리전으로 대체.
        int bestDist = 1 << 20;
        for (const auto& r : b.regions)
        {
            const int d = note < r.lokey ? r.lokey - note
                        : note > r.hikey ? note - r.hikey : 0;
            if (d < bestDist)
            {
                bestDist = d;
                best = &r;
            }
        }
    }
    return best;
}

// [함수] findFreeVoice — 빈 보이스, 없으면 가장 조용한 보이스를 빼앗음(voice stealing).
SamplerEngine::Voice* SamplerEngine::findFreeVoice()
{
    for (auto& v : voices)
        if (! v.active())
            return &v;

    // Steal the quietest.
    // env×gain(현재 들리는 음량)이 가장 작은 보이스를 훔침.
    Voice* steal = &voices[0];
    for (auto& v : voices)
        if (v.env * v.gain < steal->env * steal->gain)
            steal = &v;
    return steal;
}

// [함수] noteOn — ★오디오 스레드★. 알맞은 리전을 찾아 보이스를 시작.
void SamplerEngine::noteOn (int midiNote, float velocity)
{
    // 현재 뱅크를 원자적으로 읽음(재생 중 유지되게 shared_ptr 복사).
    auto b = std::atomic_load (&bank);
    if (b == nullptr)
        return;

    // 세기를 1~127로. 그에 맞는 리전을 찾음.
    const int vel127 = juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f));
    const auto* r = findRegion (*b, midiNote, vel127);
    if (r == nullptr)
        return;

    // 보이스를 시작 상태로 세팅.
    auto& v = *findFreeVoice();
    v.hold      = b;
    v.region    = r;
    v.pos       = 0.0;
    v.note      = midiNote;
    v.releasing = false;
    v.env       = 1.0f;
    v.fadeIn    = 64;
    // 릴리즈 감쇠계수 = exp(-1/(초*샘플레이트)).
    v.relCoeff  = std::exp (-1.0f / (float) (juce::jmax (0.01f, r->releaseSeconds) * sr));
    // 음량 = 세기 × 리전 볼륨(dB→선형).
    v.gain      = velocity * juce::Decibels::decibelsToGain (r->volumeDb);
    // 재생 비율 = 음정차(반음+튜닝)에 따른 배속 × (원본레이트/호스트레이트).
    v.ratio     = std::pow (2.0, (midiNote - r->root + r->tuneCents / 100.0) / 12.0)
                  * (r->srcRate / sr);
}

// [함수] noteOff — 해당 노트 보이스를 릴리즈 시작으로 표시.
void SamplerEngine::noteOff (int midiNote)
{
    for (auto& v : voices)
        if (v.active() && v.note == midiNote)
            v.releasing = true;
}

// [함수] releaseAll — 모든 보이스를 릴리즈로.
void SamplerEngine::releaseAll()
{
    for (auto& v : voices)
        v.releasing = true;
}

// [함수] render — ★오디오 콜백★. 활성 보이스를 리샘플링해 out에 더함(선형 보간).
void SamplerEngine::render (juce::AudioBuffer<float>& out, int numSamples)
{
    const int numCh = juce::jmin (2, out.getNumChannels());
    if (numCh == 0)
        return;

    for (auto& v : voices)
    {
        if (! v.active())
            continue;

        // 이 보이스가 재생 중인 리전 데이터.
        const auto& d = v.region->data;
        const int srcCh  = d.getNumChannels();
        const int srcLen = d.getNumSamples();

        for (int n = 0; n < numSamples; ++n)
        {
            int i0 = (int) v.pos;
            // 루프 지점을 지나면 루프 시작으로 되감음.
            if (v.region->loop && i0 >= v.region->loopEnd - 1)
            {
                v.pos -= (double) (v.region->loopEnd - v.region->loopStart);
                i0 = (int) v.pos;
            }
            // 끝에 닿거나 음량이 거의 0이면 보이스 종료(붙든 뱅크 참조도 놓음).
            if (i0 >= srcLen - 1 || v.env < 1.0e-4f)
            {
                v.region = nullptr;
                v.hold.reset();
                break;
            }

            // 선형 보간용 소수 비율과 이번 샘플 게인.
            const float frac = (float) (v.pos - (double) i0);
            float g = v.gain * v.env;
            // 시작 클릭 방지 페이드인.
            if (v.fadeIn > 0)
            {
                g *= 1.0f - (float) v.fadeIn / 64.0f;
                --v.fadeIn;
            }
            // 릴리즈 중이면 엔벨로프 지수 감쇠.
            if (v.releasing)
                v.env *= v.relCoeff;

            // 각 채널을 선형 보간해 출력에 더함.
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* src = d.getReadPointer (juce::jmin (ch, srcCh - 1));
                const float s = src[i0] + frac * (src[i0 + 1] - src[i0]);
                out.addSample (ch, n, s * g);
            }
            // 읽기 위치를 재생 비율만큼 전진.
            v.pos += v.ratio;
        }
    }
}

// [초보자 안내 — 신호 체인에서 담당]
// 이 파일은 소리가 아니라 '라이선스(정품 확인)'를 담당해요. 두 종류의 키로 잠금을 풉니다.
//   ① Gumroad 키: 온라인으로 한 번 확인(기기 수를 셈). 통과하면 결과를 이 기기에 저장해 다음부터는 오프라인.
//   ② "VCS-" 오프라인 키: 구매자 이메일에 대한 RSA 서명. 인터넷 없이 아래 공개키로 검증(기기 수 안 셈).
// 키를 넣기 전엔 DEMO로 동작(기능은 전부 되지만 1분마다 2초씩 소리가 뮤트됨).
// [정직한 한계] 이건 가벼운 파일 공유를 막을 뿐, 바이너리를 직접 뜯는 크래커는 못 막습니다(모든 플러그인이 그래요).
// [문법] 전부 static 함수라 객체 없이 vcs::Licensing::함수() 로 씁니다.

#pragma once

#include <JuceHeader.h>

/**
    Licensing for Slyce (formerly VocalChop Studio).

    Two kinds of key unlock the plugin:

    1. Gumroad keys (what buyers get automatically). Activation calls the
       Gumroad license-verify API once, which counts activations — a key is
       good for kMaxDevices machines. After that one online check the
       activation is stored locally (bound to this machine's ID) and the
       plugin never needs the network again.

    2. Offline "VCS-" keys (giveaways, press, no-internet fallback). The key
       is an RSA signature of the buyer's e-mail; verification is entirely
       offline against the public key below. No device counting.

    Until a key is entered the plugin runs as a DEMO: fully functional, but
    the output mutes for two seconds every minute.

    Honest threat model: this stops casual file-sharing, not crackers. The
    local licence file is machine-bound and tamper-signed, but anyone
    determined enough to patch the binary wins — as with every plugin.
*/
// [네임스페이스] vcs — 이 프로젝트의 라이선스 관련 코드를 담는 이름공간(이름 충돌 방지).
namespace vcs
{
// [구조체] Licensing — 라이선스 관련 정적 함수/상수 모음(도구 상자).
struct Licensing
{
    // Gumroad product permalink (the bit after gumroad.com/l/...).
    // The product page URL must be gumroad.com/l/slyce — or update this
    // constant to match and rebuild.
    // Gumroad 상품 주소의 끝부분과, 키 하나로 활성화 가능한 최대 기기 수.
    static constexpr const char* kGumroadPermalink = "slyce";
    static constexpr int kMaxDevices = 3;

    // RSA public key for offline "VCS-" keys (e, n hex — juce::RSAKey format).
    // 오프라인 키 검증용 RSA '공개키'. 서명을 검증만 할 수 있고, 새 키를 '만들'수는 없음(개인키가 있어야 함).
    static constexpr const char* kPublicKey =
        "10001,"
        "a93e24be4e0bc84531360cbdb5d2ba0db8ccab3102926bff321aeca7ade263c1"
        "6dafcc0e3efe03d66c2fc39df238c4971dcfe83f5579981a3b0b99276ce0712b"
        "d69314a8c652285d886ad111a1513e69d8c6e911fa3630bc0d6b45be2a91ae9d"
        "85dab54d11678a4be9fd1d13cbed461b9a88e89c80981644e182d31bb13d06a7";

    //==========================================================================
    // [함수] normEmail/normKey — 입력을 표준형으로 정리(이메일=소문자·공백제거, 키=공백/대시 제거).
    static juce::String normEmail (const juce::String& e)
    {
        return e.trim().toLowerCase();
    }
    static juce::String normKey (const juce::String& k)
    {
        return k.removeCharacters (" \t\r\n-").trim();
    }

    /** True for our offline VIP keys ("VCS-...."). Call BEFORE normKey
        (normKey strips the dashes). */
    // [함수] looksLikeOfflineKey — "VCS"로 시작하면 오프라인 키. 대시를 지우는 normKey '전에' 불러야 함.
    static bool looksLikeOfflineKey (const juce::String& raw)
    {
        return raw.trim().startsWithIgnoreCase ("VCS");
    }

    //==========================================================================
    // Offline "VCS-" keys: RSA signature of SHA256(e-mail).
    // [함수] verifyOfflineKey — 오프라인 키 검증. 키(서명)를 공개키로 풀어 이메일의 해시와 같은지 비교.
    static bool verifyOfflineKey (const juce::String& emailIn, const juce::String& keyIn)
    {
        const auto email = normEmail (emailIn);
        auto key = normKey (keyIn);
        if (key.startsWithIgnoreCase ("VCS"))
            key = key.substring (3);
        if (email.isEmpty() || key.length() < 64)
            return false;

        // 키(16진수)를 큰 정수(서명)로 파싱. 0이면 잘못된 키.
        juce::BigInteger sig;
        sig.parseString (key, 16);
        if (sig.isZero())
            return false;

        // 공개키를 적용해 서명을 '복호'(sig^e mod n) → 원래 해시가 나와야 함.
        juce::RSAKey pub (kPublicKey);
        pub.applyToValue (sig);   // sig^e mod n

        // 이메일의 SHA256 해시를 계산해, 복호 결과와 같은지 비교(같으면 정품 키).
        const juce::SHA256 sha (email.toRawUTF8(), (size_t) email.getNumBytesAsUTF8());
        juce::BigInteger want;
        want.parseString (sha.toHexString().removeCharacters (" "), 16);

        return sig == want;
    }

    //==========================================================================
    // Gumroad keys: one online check, counted per device by Gumroad.
    // [구조체] OnlineResult — 온라인 확인 결과(성공 여부/표시 메시지/구매자 이메일).
    struct OnlineResult
    {
        bool ok = false;
        juce::String message;   // human-readable, shown in the unlock panel
        juce::String email;     // buyer e-mail from the receipt (if any)
    };

    /** BLOCKING network call — run it on a background thread. */
    // [함수] activateOnline — Gumroad API로 키를 확인. ★블로킹(대기)★이라 반드시 백그라운드 스레드에서 호출.
    static OnlineResult activateOnline (const juce::String& keyIn)
    {
        OnlineResult r;
        const auto key = keyIn.trim();

        // Gumroad 검증 API 주소에 상품/키/사용횟수증가 파라미터를 POST 데이터로 붙임.
        juce::URL url ("https://api.gumroad.com/v2/licenses/verify");
        url = url.withPOSTData ("product_permalink=" + juce::URL::addEscapeChars (kGumroadPermalink, true)
                                + "&license_key=" + juce::URL::addEscapeChars (key, true)
                                + "&increment_uses_count=true");

        // 서버에 연결해 응답 스트림을 엶(8초 타임아웃).
        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withConnectionTimeoutMs (8000));

        // 연결 자체가 안 되면(인터넷 없음) 안내.
        if (stream == nullptr)
        {
            r.message = "No internet connection. Check the network and retry "
                        "(activation needs to go online just this once).";
            return r;
        }

        // 응답 전체를 읽어 JSON으로 파싱.
        const auto response = stream->readEntireStreamAsString();
        const auto json = juce::JSON::parse (response);

        // success가 아니면 인식 실패.
        if (! json.getProperty ("success", false))
        {
            r.message = "Key not recognised. Paste the license key exactly as "
                        "it appears on your Gumroad receipt.";
            return r;
        }

        // 환불/차지백된 구매면 무효.
        const auto purchase = json.getProperty ("purchase", juce::var());
        if (purchase.getProperty ("refunded", false)
            || purchase.getProperty ("chargebacked", false))
        {
            r.message = "This purchase was refunded, so the key is no longer active.";
            return r;
        }

        // 사용 횟수(활성화된 기기 수)가 한도를 넘으면 거부.
        const int uses = (int) json.getProperty ("uses", 0);
        if (uses > kMaxDevices)
        {
            r.message = "This key has already been activated on "
                        + juce::String (kMaxDevices) + " devices.";
            return r;
        }

        // 여기까지 오면 성공: 결과에 이메일/메시지를 채워 반환.
        r.ok = true;
        r.email = purchase.getProperty ("email", juce::String()).toString();
        r.message = "Activated - thank you for supporting Slyce!";
        return r;
    }

    //==========================================================================
    // Local activation file: machine-bound + tamper-signed.
    // [함수] machineId — 이 기기의 고유 ID(활성화를 이 기기에 묶으려고).
    static juce::String machineId()
    {
        return juce::SystemStats::getUniqueDeviceID();
    }

    // [함수] localSig — 키+기기ID+비밀문구를 SHA256으로 섞은 '위변조 방지 서명'. 파일을 손대면 서명이 안 맞음.
    static juce::String localSig (const juce::String& key, const juce::String& machine)
    {
        const auto material = normKey (key) + "|" + machine + "|vcs.rider.7f2";
        const juce::SHA256 sha (material.toRawUTF8(), (size_t) material.getNumBytesAsUTF8());
        return sha.toHexString().removeCharacters (" ");
    }

    // [함수] licenseFile — 활성화 정보를 저장할 파일 경로(사용자 앱 데이터 폴더의 Slyce/license.xml).
    static juce::File licenseFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Slyce")
                   .getChildFile ("license.xml");
    }

    // [함수] saveActivation — 이메일/키/기기ID/서명을 XML로 저장. 저장 성공 여부 반환.
    static bool saveActivation (const juce::String& email, const juce::String& key)
    {
        auto f = licenseFile();
        f.getParentDirectory().createDirectory();

        juce::XmlElement xml ("VCSLicense");
        xml.setAttribute ("email",   normEmail (email));
        xml.setAttribute ("key",     key.trim());
        xml.setAttribute ("machine", machineId());
        xml.setAttribute ("sig",     localSig (key, machineId()));
        return xml.writeTo (f);
    }

    /** True when a valid activation for THIS machine is on disk. */
    // [함수] loadActivation — 저장된 활성화가 이 기기에 유효한지 확인(플러그인 시작 시 정품 여부 판단).
    static bool loadActivation()
    {
        // 파일이 없으면 미활성.
        const auto f = licenseFile();
        if (! f.existsAsFile())
            return false;

        // XML 파싱과 태그 확인.
        const auto xml = juce::parseXML (f);
        if (xml == nullptr || ! xml->hasTagName ("VCSLicense"))
            return false;

        // 이 기기의 것이 맞고 서명이 일치하는지(위변조 검사).
        const auto key     = xml->getStringAttribute ("key");
        const auto machine = xml->getStringAttribute ("machine");
        if (machine != machineId()
            || xml->getStringAttribute ("sig") != localSig (key, machine))
            return false;

        // Offline keys re-verify cryptographically; Gumroad keys were
        // verified online at activation and are trusted from the local sig.
        // 오프라인 키는 암호로 한 번 더 검증. Gumroad 키는 활성화 때 온라인 검증했으므로 로컬 서명으로 신뢰.
        if (looksLikeOfflineKey (key))
            return verifyOfflineKey (xml->getStringAttribute ("email"), key);

        return key.isNotEmpty();
    }
};
} // namespace vcs

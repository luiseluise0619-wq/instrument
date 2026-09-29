(() => {
  "use strict";

  const words = {
    en: {
      skip:"Skip to content", navWorkflow:"How it works", navSound:"Sounds", navLooper:"Looper", navBuy:"Get Slyce", topCta:"Get Slyce",
      eyebrow:"VOCAL CHOP INSTRUMENT", headline:"Turn a voice\ninto an instrument.",
      heroLede:"Slyce gives vocals and samples a place on your keyboard. Find a phrase, slice it into playable notes, then build the idea into a loop.",
      standalone:"Standalone", buyCta:"Get Slyce", listenCta:"Hear a vocal chop", caption:"The Slyce instrument — slice, shape and play in one place.",
      stripOne:"VOCAL CHOPS",stripTwo:"PLAYABLE SOUNDS",stripThree:"LIVE LOOPING",stripFour:"DAW READY",
      listenEyebrow:"A QUICK LISTEN",listenTitle:"Hear the idea, not a feature list.",listenText:"Press play for a short vocal-chop example. The sound loads only when you ask for it.",audioPlay:"Play sample",audioPause:"Pause sample",
      workflowEyebrow:"FROM SAMPLE TO PERFORMANCE",workflowTitle:"Three moves. A sound you can play.",workflowText:"No separate editor, sampler and loop app to keep in sync. Start with a sound and stay in the same instrument.",
      stepOneTitle:"Bring in a sound",stepOneText:"Load a built-in vocal or drop in your own audio sample. Slyce maps a single sample across the keyboard so it is ready to play.",
      stepTwoTitle:"Find the cuts",stepTwoText:"Use transient detection for natural phrases or choose a beat grid for rhythmic chops. Adjust the slices, then audition them from the pads or keys.",
      stepThreeTitle:"Play and shape",stepThreeText:"Play from a MIDI controller or computer keyboard. Adjust pitch and timing, add effects, and keep the performance moving.",
      soundEyebrow:"MORE THAN A CHOPPER",soundTitle:"One instrument.\nRoom for your sound.",soundText:"Start with Slyce’s built-in sounds, then bring your own sample when you have something specific in mind. Shape the source with pitch, envelope and creative effects without leaving the instrument.",
      soundItemOne:"Vocal chops and phrases, ready to map and perform",soundItemTwo:"403 built-in instrument sounds across key categories",soundItemThree:"Load a sample or an SFZ multisample bank",soundItemFour:"Save your work and return to the same session",
      loopEyebrow:"KEEP THE MOMENT GOING",loopTitle:"Build the loop\nwhile you play.",loopText:"Record a part, then move straight to the next track. What you already recorded keeps playing as you layer another idea. Stay in Slyce to shape the loop, export a WAV, or drag it into your DAW.",
      loopPointOne:"Six loop tracks",loopPointTwo:"Record and overdub",loopPointThree:"WAV export and DAW drag-out",
      buyEyebrow:"TRY IT IN YOUR SESSION",buyTitle:"Make room for the happy accident.",buyText:"Slyce is an instrument plugin for vocal ideas, playable samples and quick loop sketches. Try the demo in your DAW, or get the full release from Gumroad.",buyNow:"Get Slyce on Gumroad",tryDemo:"Get the free demo",priceNote:"Full version",
      formats:"VST3 on Windows, macOS and Linux · AU on macOS · Standalone builds available",demoTitle:"A demo you can actually try.",demoText:"The demo keeps the instrument features and session saving available. In demo builds, the audio output is muted for 2 seconds once every minute. Enter a valid license under Unlock to remove the demo mute; reinstalling is not required.",licenseText:"After purchase, enter the Gumroad license key and purchase email in the plugin’s Unlock screen. License delivery depends on the product’s Gumroad settings.",
      compatEyebrow:"MADE TO FIT YOUR SETUP",compatTitle:"Open it where you make music.",compatVst:"Windows · macOS · Linux",compatAu:"macOS",compatStandalone:"Available for supported systems",compatMidi:"Controller and computer-keyboard input",footer:"Vocal ideas, made playable.",affiliate:"Affiliate",footerNote:"Built for making music, not managing menus."
    },
    ko: {
      skip:"본문으로 건너뛰기",navWorkflow:"사용 방법",navSound:"사운드",navLooper:"루퍼",navBuy:"Slyce 받기",topCta:"Slyce 받기",
      eyebrow:"보컬 찹 가상악기",headline:"목소리를\n연주 가능한 악기로.",
      heroLede:"Slyce에서는 보컬과 샘플을 건반으로 연주할 수 있습니다. 원하는 구간을 골라 슬라이스하고, 건반으로 연주한 뒤 루프로 이어가세요.",
      standalone:"스탠드얼론",buyCta:"Slyce 받기",listenCta:"보컬 찹 들어보기",caption:"Slyce 안에서 자르고, 다듬고, 바로 연주하세요.",
      stripOne:"보컬 찹",stripTwo:"건반 연주",stripThree:"실시간 루핑",stripFour:"DAW 연동",
      listenEyebrow:"먼저 소리로 확인하세요",listenTitle:"기능 설명보다 소리를 먼저.",listenText:"재생을 누르면 짧은 보컬 찹 예제를 들을 수 있습니다. 소리는 버튼을 누를 때만 불러옵니다.",audioPlay:"샘플 재생",audioPause:"샘플 일시정지",
      workflowEyebrow:"샘플에서 연주까지",workflowTitle:"세 단계면, 바로 연주할 소리로.",workflowText:"에디터와 샘플러, 루프 앱을 따로 오갈 필요 없이 한 악기 안에서 작업하세요.",
      stepOneTitle:"소리 불러오기",stepOneText:"내장 보컬을 고르거나 직접 오디오 샘플을 불러오세요. 하나의 샘플을 건반 음역에 매핑해 바로 연주할 수 있습니다.",
      stepTwoTitle:"자를 구간 찾기",stepTwoText:"트랜지언트 감지로 자연스러운 구간을 찾거나 박자 그리드로 나누세요. 슬라이스를 조정한 뒤 패드나 건반으로 들어볼 수 있습니다.",
      stepThreeTitle:"연주하고 다듬기",stepThreeText:"MIDI 컨트롤러나 컴퓨터 키보드로 연주하세요. 피치와 타이밍을 조절하고 이펙트를 더해 아이디어를 완성할 수 있습니다.",
      soundEyebrow:"보컬 찹만을 위한 악기가 아닙니다",soundTitle:"하나의 악기 안에\n내 소리를 담으세요.",soundText:"내장 사운드로 시작하고, 원하는 소리가 있으면 직접 샘플을 불러오세요. 악기를 벗어나지 않고 피치와 엔벌로프, 크리에이티브 이펙트로 소리를 다듬을 수 있습니다.",
      soundItemOne:"건반에 매핑해 바로 연주하는 보컬 찹과 프레이즈",soundItemTwo:"주요 악기 카테고리의 내장 사운드 403개",soundItemThree:"오디오 샘플 또는 SFZ 멀티샘플 뱅크 불러오기",soundItemFour:"작업을 저장하고 같은 세션에서 이어가기",
      loopEyebrow:"떠오른 순간을 이어가세요",loopTitle:"연주하면서\n루프를 쌓으세요.",loopText:"한 파트를 녹음한 다음 바로 다음 트랙으로 넘어가세요. 앞서 녹음한 트랙은 계속 재생되므로 그 위에 새 아이디어를 쌓을 수 있습니다. Slyce 안에서 다듬고, WAV로 내보내거나 DAW로 드래그하세요.",
      loopPointOne:"6개 루프 트랙",loopPointTwo:"녹음과 오버더빙",loopPointThree:"WAV 내보내기 · DAW로 드래그",
      buyEyebrow:"내 세션에서 직접 써보세요",buyTitle:"우연히 찾은 소리도 곡의 일부로.",buyText:"Slyce는 보컬 아이디어, 직접 불러온 샘플 연주, 빠른 루프 스케치를 위한 가상악기입니다. 먼저 데모를 DAW에서 써보거나 Gumroad에서 정식 버전을 받으세요.",buyNow:"Gumroad에서 Slyce 받기",tryDemo:"무료 데모 받기",priceNote:"정식 버전",
      formats:"Windows·macOS·Linux VST3 · macOS AU · 스탠드얼론 빌드 제공",demoTitle:"기능 제한 없이 먼저 써보세요.",demoText:"데모에서도 악기 기능과 세션 저장을 사용할 수 있습니다. 데모 빌드는 1분마다 2초간 오디오 출력이 음소거됩니다. 플러그인 Unlock에서 유효한 라이선스를 등록하면 제한이 해제되며, 재설치는 필요하지 않습니다.",licenseText:"구매 후 Gumroad 라이선스 키와 구매 이메일을 플러그인의 Unlock 화면에 입력하세요. 키 전달 방식은 Gumroad 상품 설정에 따라 달라집니다.",
      compatEyebrow:"작업 환경에 맞게",compatTitle:"음악 만드는 곳에서 바로 여세요.",compatVst:"Windows · macOS · Linux",compatAu:"macOS",compatStandalone:"지원 시스템용 제공",compatMidi:"MIDI 컨트롤러 · 컴퓨터 키보드",footer:"보컬 아이디어를 연주 가능한 소리로.",affiliate:"제휴",footerNote:"메뉴보다 음악에 집중할 수 있도록."
    }
  };

  const html = document.documentElement;
  const languageButton = document.getElementById("languageToggle");
  let language = (() => {
    try { return localStorage.getItem("slyce-site-language"); } catch (_) { return null; }
  })();
  if (language !== "en" && language !== "ko") language = (navigator.language || "").toLowerCase().startsWith("ko") ? "ko" : "en";

  function setLanguage(next) {
    language = next;
    html.lang = next;
    document.querySelectorAll("[data-copy]").forEach((node) => {
      const value = words[next][node.dataset.copy];
      if (value !== undefined) node.textContent = value;
    });
    languageButton.textContent = next === "ko" ? "EN" : "한국어";
    languageButton.setAttribute("aria-label", next === "ko" ? "Switch language to English" : "한국어로 전환");
    try { localStorage.setItem("slyce-site-language", next); } catch (_) {}
  }
  setLanguage(language);
  languageButton.addEventListener("click", () => setLanguage(language === "ko" ? "en" : "ko"));

  const audio = document.getElementById("sampleAudio");
  const audioCard = document.querySelector(".listen-card");
  const audioButton = document.getElementById("audioToggle");
  const audioLabel = document.querySelector(".audio-label");
  const timeLabel = document.getElementById("audioTime");
  audioButton.setAttribute("aria-label", words[language].audioPlay);
  languageButton.addEventListener("click", () => audioButton.setAttribute("aria-label", words[language].audioPlay));
  audioButton.addEventListener("click", async () => {
    if (!audio.src) audio.src = audio.dataset.src;
    if (audio.paused) {
      try { await audio.play(); } catch (_) { audioLabel.textContent = words[language].audioPlay; }
    } else audio.pause();
  });
  audio.addEventListener("play", () => {
    audioCard.classList.add("is-playing");
    audioLabel.textContent = words[language].audioPause;
    audioButton.setAttribute("aria-label", words[language].audioPause);
  });
  audio.addEventListener("pause", () => {
    audioCard.classList.remove("is-playing");
    audioLabel.textContent = words[language].audioPlay;
    audioButton.setAttribute("aria-label", words[language].audioPlay);
  });
  audio.addEventListener("ended", () => audio.pause());
  audio.addEventListener("timeupdate", () => {
    const seconds = Math.floor(audio.currentTime || 0);
    timeLabel.textContent = `${String(Math.floor(seconds / 60)).padStart(2,"0")}:${String(seconds % 60).padStart(2,"0")}`;
  });
})();

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
namespace slyce::reference {
inline constexpr int width=1536, height=1024;
inline constexpr const char* build="REF-20260920-04";
struct Rect { int x,y,w,h; juce::Rectangle<int> get() const {return {x,y,w,h};} };
inline constexpr std::array<Rect, 3> browserTabs {{ {63,137,92,35},{156,137,91,35},{248,137,81,35} }};
inline constexpr Rect search {65,180,263,34};
inline constexpr Rect browser {62,225,267,463};
inline constexpr Rect importAudio {65,696,263,30};
inline constexpr Rect previous {365,144,30,38};
inline constexpr Rect next {396,144,30,38};
inline constexpr Rect favorite {439,144,38,38};
inline constexpr Rect title {498,140,478,44};
inline constexpr Rect menu {986,144,39,38};
inline constexpr Rect bpm {1053,142,61,39};
inline constexpr Rect key {1118,142,63,39};
inline constexpr std::array<Rect, 4> workspaceTabs {{ {360,205,104,31},{465,205,73,31},{539,205,74,31},{614,205,74,31} }};
inline constexpr Rect sliceCount {1011,207,85,28};
inline constexpr Rect zoomOut {1099,207,37,28};
inline constexpr Rect zoomIn {1138,207,37,28};
inline constexpr Rect wave {370,249,799,165};
inline constexpr Rect play {369,425,59,35};
inline constexpr Rect snap {503,425,78,35};
inline constexpr Rect sensitivity {678,428,114,29};
inline constexpr Rect detect {811,425,86,35};
inline constexpr Rect autoSlice {908,425,76,35};
inline constexpr Rect manual {991,425,82,35};
inline constexpr Rect clear {1087,425,81,35};
inline constexpr std::array<Rect, 8> pads {{ {358,480,200,116},{566,480,200,116},{774,480,200,116},{982,480,200,116},{358,604,200,116},{566,604,200,116},{774,604,200,116},{982,604,200,116} }};
inline constexpr std::array<Rect, 3> rightTabs {{ {1198,132,84,39},{1283,132,100,39},{1384,132,98,39} }};
inline constexpr Rect fxPower {1441,186,28,28};
inline constexpr std::array<Rect, 7> knobs {{ {1230,225,92,84},{1363,225,92,84},{1230,349,92,84},{1363,349,92,84},{1217,665,64,64},{1310,665,64,64},{1398,665,64,64} }};
inline constexpr std::array<Rect, 7> knobLabels {{ {1229,310,94,24},{1362,310,94,24},{1229,434,94,24},{1362,434,94,24},{1209,728,80,23},{1302,728,80,23},{1390,728,80,23} }};
inline constexpr Rect loopPower {1441,485,28,28};
inline constexpr Rect loopWave {1234,521,212,49};
inline constexpr Rect loopPrev {1209,527,23,35};
inline constexpr Rect loopNext {1447,527,25,35};
inline constexpr Rect loopLength {1275,578,97,30};
inline constexpr Rect sync {1382,578,81,30};
inline constexpr Rect keys {201,782,1091,96};
inline constexpr Rect pitch {80,788,35,73};
inline constexpr Rect mod {136,788,35,73};
inline constexpr Rect overlay {358,240,821,510};
inline constexpr std::array<const char*,7> parameterIDs {{"reverb","delay","filterCutoff","drive","attack","release","synthGlide"}};
inline constexpr std::array<const char*,7> captions {{"REVERB","DELAY","FILTER","DRIVE","ATTACK","RELEASE","GLIDE"}};
}

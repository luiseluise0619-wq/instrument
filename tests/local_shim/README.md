# LOCAL TEST SHIM — NOT JUCE

Only for the explicit `SLYCE_TEST_LOCAL_SHIM=ON` smoke-test build.
Do not add this directory to the production plugin include path.

This small compatibility harness implements selected buffer/math/smoothing/type
interfaces to execute the modified DSP loops in an environment without JUCE.
Reverb output and audio file decoding are placeholders. A passing test here does
not establish JUCE API compatibility, actual wet-reverb performance, SFZ decoding,
plugin loading, or full real-time safety. See reports/TEST_REPORT_KO.md.

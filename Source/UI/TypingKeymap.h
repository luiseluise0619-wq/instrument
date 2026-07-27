#pragma once

#include <JuceHeader.h>

/** The computer-keyboard -> semitone mapping, in a header so a test can call
    the real thing.

    It used to live in an anonymous namespace inside PluginEditor.cpp, which
    meant the only way to find out what a key played was to press it and listen.
    That is exactly how "Q plays the last slice" survived: the arithmetic was
    right there, and nobody could run it. */
namespace slyce::keymap
{
    // Two rows, laid out the way every DAW does it: the lower row runs from C
    // and keeps going past B onto , l . ; / , and the upper row starts again
    // one octave up. The rows OVERLAP by an octave - that is why the semitone
    // for each key has to be a table rather than the key's position in the
    // string. The bottom row used to stop at M, so the four keys past it did
    // nothing at all.
    //
    // That overlap makes ',' and 'q' the SAME note. An older comment warned
    // about exactly that ("two keys driving one note means releasing either
    // kills the other's sound") and the table below was written anyway. The
    // collision is real; the editor refcounts note-offs so it is harmless,
    // rather than the table pretending it cannot happen.
    inline const juce::String keys ("zsxdcvgbhnjm,l.;/q2w3er5t6y7ui9o0p[=]");

    inline constexpr int semitone[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,      // z s x d c v g b h n j m
        12, 13, 14, 15, 16,                        // , l . ; /
        12, 13, 14, 15, 16, 17, 18, 19, 20, 21,    // q 2 w 3 e r 5 t 6 y
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31     // 7 u i 9 o 0 p [ = ]
    };

    inline constexpr int numKeys   = (int) (sizeof (semitone) / sizeof (semitone[0]));

    /** Index of 'q' - where the upper row starts. */
    inline constexpr int topRowStart = 17;

    /** The semitone key `i` sends, which depends on the engine.

        Melodic engines want a piano: two rows an octave apart. That is what a
        player expects and what every DAW does, so it stays.

        Chop mode is not a piano. Slices are pads, and an octave is a musical
        interval that knows nothing about how many pieces the sample was cut
        into - an octave is seven white keys, a sample is cut into eight or
        twelve or sixteen. On the eight-slice demo that arithmetic put Q on
        slice 7, the LAST one. Consistent with the mapping, and indistinguish-
        able from a bug to anyone actually using it.

        So in Chop mode each row runs the slice sequence from its own start:
        Q plays the first slice, exactly like Z. Both rows are piano layouts in
        their own right, so a key's position WITHIN its row is its offset. */
    inline int semitoneFor (int i, bool chopMode)
    {
        if (! juce::isPositiveAndBelow (i, numKeys))
            return i;
        if (chopMode && i >= topRowStart)
            return i - topRowStart;
        return semitone[i];
    }
}

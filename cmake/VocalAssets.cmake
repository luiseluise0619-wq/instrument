set(SLYCE_VOCAL_NAMES
    # User-supplied songs are source-separated first.  Only the cleaned vocal
    # banks below are embedded; accompaniment and the retired procedural set
    # are intentionally absent from the release binary.
    premium_glass_tide_a_1.wav
    premium_glass_tide_a_2.wav
    premium_glass_tide_a_3.wav
    premium_glass_tide_a_4.wav
    premium_glass_tide_b_1.wav
    premium_glass_tide_b_2.wav
    premium_glass_tide_b_3.wav
    premium_glass_tide_b_4.wav
    premium_quiet_blue_a_1.wav
    premium_quiet_blue_a_2.wav
    premium_quiet_blue_a_3.wav
    premium_quiet_blue_a_4.wav
    premium_quiet_blue_b_1.wav
    premium_quiet_blue_b_2.wav
    premium_quiet_blue_b_3.wav
    premium_quiet_blue_b_4.wav
    premium_come_undo_me_1.wav
    premium_come_undo_me_2.wav
    premium_come_undo_me_3.wav
    premium_come_undo_me_4.wav
    premium_dark_space_1.wav
    premium_dark_space_2.wav
    premium_dark_space_3.wav
    premium_dark_space_4.wav
)

set(SLYCE_ORIGINAL_VOCALS_DIR "${PROJECT_SOURCE_DIR}/examples" CACHE PATH "Directory holding the owner's original WAVs")
set(SLYCE_VOCAL_FILES "")
foreach(name IN LISTS SLYCE_VOCAL_NAMES)
    if(EXISTS "${SLYCE_ORIGINAL_VOCALS_DIR}/${name}")
        list(APPEND SLYCE_VOCAL_FILES "${SLYCE_ORIGINAL_VOCALS_DIR}/${name}")
    else()
        message(FATAL_ERROR "Missing premium vocal bank: ${name}. Restore the curated release WAV in SLYCE_ORIGINAL_VOCALS_DIR.")
    endif()
endforeach()

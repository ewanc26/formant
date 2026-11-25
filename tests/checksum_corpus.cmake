# Canonical checksum corpus. Renders one sample of every input kind and diffs
# the SHA-256 of each output against tests/checksum_corpus.txt. This catches
# unintended acoustic changes the same way benchmark/expected.txt guards the
# fixed benchmark corpus, but covers front ends as well as voices.
#
# Regenerate the data file deliberately with:
#   cmake -DFORMANT_CLI=... -DFORMANT_SOURCE_DIR=... -DFORMANT_UPDATE_CHECKSUMS=ON \
#         -P tests/checksum_corpus.cmake
# A normal CTest run never writes the corpus; a mismatch is a hard failure.

if(NOT DEFINED FORMANT_CLI OR NOT DEFINED FORMANT_SOURCE_DIR)
    message(FATAL_ERROR "checksum_corpus.cmake needs FORMANT_CLI and FORMANT_SOURCE_DIR")
endif()

set(CORPUS_FILE "${FORMANT_SOURCE_DIR}/tests/checksum_corpus.txt")
set(OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/checksum-corpus")
file(MAKE_DIRECTORY "${OUT_DIR}")

# Each case is: name | expected checksum | CLI arguments. Cases are declared
# here rather than parsed from the data file so argument quoting stays simple;
# the data file holds only the expected values.
set(CASES
    "default"
    "text"
    "phonemes"
    "ipa"
    "xsampa"
    "gibberish"
    "lang_tag"
    "lang_file"
    "voice_file"
    "intonation"
    "ssml"
    "prosody"
    "lexicon"
    "format_wav24"
    "format_raw16"
    "sample_rate"
    "dither"
    "song"
    "frication"
)

set(ARGS_default "")
set(ARGS_text --text "Hello, world.")
set(ARGS_phonemes --phonemes "[h ee l o]")
set(ARGS_ipa --ipa "ˈhəˈloʊ")
set(ARGS_xsampa --xsampa "h@loU")
set(ARGS_gibberish --gibberish "CVC" --seed 7 --count 4)
set(ARGS_lang_tag --lang es --text "hola mundo")
set(ARGS_lang_file --lang-file "${FORMANT_SOURCE_DIR}/examples/velta.lang" --text "velta")
set(ARGS_voice_file --voice-file "${FORMANT_SOURCE_DIR}/examples/warm.voice" --text "Hello, world.")
set(ARGS_intonation --intonation-tune yesno --text "Hello, world.")

# Fixture files for file-based front ends. Kept in the build tree so a normal
# CTest run never writes into the source tree.
file(WRITE "${OUT_DIR}/corpus.ssml"
    "<speak><p>Hello. <phoneme alphabet=\"ipa\" ph=\"ˈhəˈloʊ\"/><prosody rate=\"1.2\">Wide.</prosody><break time=\"250ms\"/>Done.</p></speak>")
file(WRITE "${OUT_DIR}/corpus.prosody" "# exact contour\nf0 0 180\nduration 1 0.25s\namplitude 2 0.8\n")
file(WRITE "${OUT_DIR}/corpus.lex" "word Ewan juːən\nhomograph lead led after=us\n")
set(ARGS_ssml --ssml-file "${OUT_DIR}/corpus.ssml")
set(ARGS_prosody --ipa "ˈhəˈloʊ" --prosody-file "${OUT_DIR}/corpus.prosody")
set(ARGS_lexicon --text "Ewan lead us" --lexicon "${OUT_DIR}/corpus.lex")

set(ARGS_format_wav24 --format wav24 --text "Hello, world.")
set(ARGS_format_raw16 --format raw16 --text "Hello, world.")
set(ARGS_sample_rate --sample-rate 22050 --text "Hello, world.")
set(ARGS_dither --dither 42 --format raw16 --text "Hello, world.")
set(ARGS_song --song twinkle --sample-rate 22050)
set(ARGS_frication --phonemes "[s ee]" --frication-file
    "${FORMANT_SOURCE_DIR}/data/frication/frication_profiles.txt")

function(formant_read_corpus out_var)
    if(NOT EXISTS "${CORPUS_FILE}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    file(STRINGS "${CORPUS_FILE}" lines REGEX "^[^#]")
    set(table "")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "^[ \t]+|[ \t]+$" "" line "${line}")
        if(line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif()
        string(REGEX MATCH "^([^ \t]+)[ \t]+([0-9a-f]+)" match "${line}")
        if(NOT match)
            message(FATAL_ERROR "malformed corpus line: ${line}")
        endif()
        set(table "${table}${CMAKE_MATCH_1}|${CMAKE_MATCH_2};")
    endforeach()
    set(${out_var} "${table}" PARENT_SCOPE)
endfunction()

formant_read_corpus(corpus)

set(updated "")
set(failed 0)
foreach(case IN LISTS CASES)
    set(args "${ARGS_${case}}")
    execute_process(
        COMMAND "${FORMANT_CLI}" ${args} --output "${OUT_DIR}/${case}.wav"
        RESULT_VARIABLE result
        OUTPUT_QUIET ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "corpus case '${case}' failed to render: ${error}")
    endif()
    file(SHA256 "${OUT_DIR}/${case}.wav" actual)

    string(FIND "${corpus}" "${case}|" found)
    if(found EQUAL -1)
        set(expected "")
    else()
        string(REGEX MATCH "${case}\\|([0-9a-f]+)" match "${corpus}")
        set(expected "${CMAKE_MATCH_1}")
    endif()

    if(FORMANT_UPDATE_CHECKSUMS)
        string(APPEND updated "${case} ${actual}\n")
    elseif(NOT actual STREQUAL expected)
        message(STATUS "CHECKSUM MISMATCH ${case}: expected ${expected}, got ${actual}")
        set(failed 1)
    endif()
endforeach()

if(FORMANT_UPDATE_CHECKSUMS)
    file(WRITE "${CORPUS_FILE}" "${updated}")
    message(STATUS "Updated ${CORPUS_FILE} with ${updated}")
elseif(failed)
    message(FATAL_ERROR "checksum corpus mismatch; regenerate deliberately with FORMANT_UPDATE_CHECKSUMS=ON")
endif()

list(LENGTH CASES case_count)
message(STATUS "checksum corpus ${CORPUS_FILE} verified (${case_count} cases)")

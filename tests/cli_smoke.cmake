# Minimal end-to-end check: the CLI must report the configured version and
# write a plausible mono 16-bit WAV.
execute_process(
    COMMAND "${FORMANT_CLI}" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
    ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT version_result EQUAL 0)
    message(FATAL_ERROR "formant --version failed (${version_result}): ${version_error}")
endif()
if(NOT version_output STREQUAL "${FORMANT_EXPECTED_VERSION}")
    message(FATAL_ERROR "CLI version '${version_output}' does not match CMake version '${FORMANT_EXPECTED_VERSION}'")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[h ee l o]" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "formant CLI failed (${result}): ${stderr_text}")
endif()
if(NOT EXISTS "${FORMANT_OUTPUT}")
    message(FATAL_ERROR "formant CLI did not create ${FORMANT_OUTPUT}")
endif()

file(SIZE "${FORMANT_OUTPUT}" output_size)
if(output_size LESS 44)
    message(FATAL_ERROR "WAV output is too short: ${output_size} bytes")
endif()

file(READ "${FORMANT_OUTPUT}" riff OFFSET 0 LIMIT 4 HEX)
file(READ "${FORMANT_OUTPUT}" wave OFFSET 8 LIMIT 4 HEX)
if(NOT riff STREQUAL "52494646")
    message(FATAL_ERROR "missing RIFF magic: ${riff}")
endif()
if(NOT wave STREQUAL "57415645")
    message(FATAL_ERROR "missing WAVE magic: ${wave}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE ipa_result
    ERROR_VARIABLE ipa_error
)
if(NOT ipa_result EQUAL 0)
    message(FATAL_ERROR "formant CLI IPA render failed (${ipa_result}): ${ipa_error}")
endif()
file(SIZE "${FORMANT_OUTPUT}" ipa_size)
if(ipa_size LESS 44)
    message(FATAL_ERROR "IPA WAV output is too short: ${ipa_size} bytes")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --xsampa "h@" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE xsampa_result
    ERROR_VARIABLE xsampa_error
)
if(NOT xsampa_result EQUAL 0)
    message(FATAL_ERROR "formant CLI X-SAMPA render failed (${xsampa_result}): ${xsampa_error}")
endif()
file(SIZE "${FORMANT_OUTPUT}" xsampa_size)
if(xsampa_size LESS 44)
    message(FATAL_ERROR "X-SAMPA WAV output is too short: ${xsampa_size} bytes")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --list-ipa
    RESULT_VARIABLE list_result
    OUTPUT_VARIABLE ipa_symbols
    ERROR_VARIABLE list_error
)
if(NOT list_result EQUAL 0 OR ipa_symbols STREQUAL "")
    message(FATAL_ERROR "formant --list-ipa failed: ${list_error}")
endif()

# Conflicting transcription modes are rejected with an accurate message.
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "hə" --xsampa "h@" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE conflict_result
    ERROR_VARIABLE conflict_error
)
if(conflict_result EQUAL 0)
    message(FATAL_ERROR "formant accepted conflicting transcription modes")
endif()
if(NOT conflict_error MATCHES "use only one of --phonemes, --ipa or --xsampa")
    message(FATAL_ERROR "unexpected conflict message: ${conflict_error}")
endif()

# A transcription mode takes precedence over --text rather than conflict.
execute_process(
    COMMAND "${FORMANT_CLI}" --text "ignored" --ipa "hə" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE precedence_result
    ERROR_VARIABLE precedence_error
)
if(NOT precedence_result EQUAL 0)
    message(FATAL_ERROR "formant rejected --text with --ipa (${precedence_result}): ${precedence_error}")
endif()

# --ssml-file renders a document using the full supported subset.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/smoke.ssml" "<speak><p>Hello. <say-as interpret-as=\"digits\">123</say-as>. <phoneme alphabet=\"ipa\" ph=\"ˈhəˈloʊ\"/><prosody rate=\"1.2\" pitch=\"+2st\" volume=\"1.1\">Wide.</prosody><break time=\"250ms\"/>Done.</p></speak>")
execute_process(
    COMMAND "${FORMANT_CLI}" --ssml-file "${CMAKE_CURRENT_BINARY_DIR}/smoke.ssml" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE ssml_result
    ERROR_VARIABLE ssml_error
)
if(NOT ssml_result EQUAL 0)
    message(FATAL_ERROR "formant CLI SSML render failed (${ssml_result}): ${ssml_error}")
endif()
file(SIZE "${FORMANT_OUTPUT}" ssml_size)
if(ssml_size LESS 44)
    message(FATAL_ERROR "SSML WAV output is too short: ${ssml_size} bytes")
endif()

# Unsupported SSML elements are rejected with the tag name and byte position.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bad.ssml" "<speak><b>bold</b></speak>")
execute_process(
    COMMAND "${FORMANT_CLI}" --ssml-file "${CMAKE_CURRENT_BINARY_DIR}/bad.ssml" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE ssml_bad_result
    ERROR_VARIABLE ssml_bad_error
)
if(ssml_bad_result EQUAL 0)
    message(FATAL_ERROR "formant accepted an unsupported SSML element")
endif()
if(NOT ssml_bad_error MATCHES "unsupported element <b>")
    message(FATAL_ERROR "unexpected SSML error message: ${ssml_bad_error}")
endif()

# --prosody-file layers exact F0, duration and amplitude overrides.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/smoke.prosody" "# exact contour\nf0 0 180\nduration 1 0.25s\namplitude 2 0.8\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --prosody-file "${CMAKE_CURRENT_BINARY_DIR}/smoke.prosody" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE prosody_result
    ERROR_VARIABLE prosody_error
)
if(NOT prosody_result EQUAL 0)
    message(FATAL_ERROR "formant CLI prosody render failed (${prosody_result}): ${prosody_error}")
endif()
file(SIZE "${FORMANT_OUTPUT}" prosody_size)
if(prosody_size LESS 44)
    message(FATAL_ERROR "prosody WAV output is too short: ${prosody_size} bytes")
endif()

# Malformed prosody directives fail with the source line number.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bad.prosody" "wobble 0 1\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --prosody-file "${CMAKE_CURRENT_BINARY_DIR}/bad.prosody" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE prosody_bad_result
    ERROR_VARIABLE prosody_bad_error
)
if(prosody_bad_result EQUAL 0)
    message(FATAL_ERROR "formant accepted a malformed prosody file")
endif()
if(NOT prosody_bad_error MATCHES "prosody line 1")
    message(FATAL_ERROR "unexpected prosody error message: ${prosody_bad_error}")
endif()

# --intonation-file expands symbolic ToBI tones; --intonation-tune selects a
# built-in tune. Both compose with every front end and stay deterministic.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/smoke.intonation" "# symbolic tones\naccent vowel:0 L+H*\naccent vowel:1 !H*\nboundary L-H%\nbreak vowel:last 2\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --intonation-file "${CMAKE_CURRENT_BINARY_DIR}/smoke.intonation" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE intonation_result
    ERROR_VARIABLE intonation_error
)
if(NOT intonation_result EQUAL 0)
    message(FATAL_ERROR "formant CLI intonation render failed (${intonation_result}): ${intonation_error}")
endif()
file(SIZE "${FORMANT_OUTPUT}" intonation_size)
if(intonation_size LESS 44)
    message(FATAL_ERROR "intonation WAV output is too short: ${intonation_size} bytes")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hello world." --intonation-tune yesno --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE tune_result
    ERROR_VARIABLE tune_error
)
if(NOT tune_result EQUAL 0)
    message(FATAL_ERROR "formant CLI intonation tune failed (${tune_result}): ${tune_error}")
endif()

# Malformed intonation directives fail with the source line number.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bad.intonation" "wobble 0 1\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --intonation-file "${CMAKE_CURRENT_BINARY_DIR}/bad.intonation" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE intonation_bad_result
    ERROR_VARIABLE intonation_bad_error
)
if(intonation_bad_result EQUAL 0)
    message(FATAL_ERROR "formant accepted a malformed intonation file")
endif()
if(NOT intonation_bad_error MATCHES "intonation line 1")
    message(FATAL_ERROR "unexpected intonation error message: ${intonation_bad_error}")
endif()

# An unknown named tune is rejected with its name.
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --intonation-tune nope --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE tune_bad_result
    ERROR_VARIABLE tune_bad_error
)
if(tune_bad_result EQUAL 0)
    message(FATAL_ERROR "formant accepted an unknown intonation tune")
endif()
if(NOT tune_bad_error MATCHES "unknown tune 'nope'")
    message(FATAL_ERROR "unexpected tune error message: ${tune_bad_error}")
endif()

# --stdin must be ignored when a transcription mode supplies the phonemes.
# An endless stdin source terminates via SIGPIPE only if the CLI never drains
# it; the buggy version blocks reading it until the CMake timeout kills it.
execute_process(
    COMMAND sh -c "yes | \"${FORMANT_CLI}\" --ipa a --stdin --output \"${FORMANT_OUTPUT}\""
    RESULT_VARIABLE stdin_result
    ERROR_VARIABLE stdin_error
    TIMEOUT 10
)
if(NOT stdin_result EQUAL 0)
    message(FATAL_ERROR "formant waited on stdin despite --ipa override (${stdin_result}): ${stdin_error}")
endif()

# Non-finite numeric values are rejected instead of silently corrupting audio.
foreach(bad_value nan inf infinity -inf)
    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa "a" --pitch "${bad_value}" --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE nonfinite_result
        ERROR_VARIABLE nonfinite_error
    )
    if(nonfinite_result EQUAL 0)
        message(FATAL_ERROR "formant accepted non-finite pitch '${bad_value}'")
    endif()
    if(NOT nonfinite_error MATCHES "invalid number for --pitch")
        message(FATAL_ERROR "unexpected non-finite pitch message: ${nonfinite_error}")
    endif()
endforeach()
foreach(bad_flag --gain --rate --formant-scale --attack --declination)
    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa "a" "${bad_flag}=nan" --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE nonfinite_result
        ERROR_VARIABLE nonfinite_error
    )
    if(nonfinite_result EQUAL 0)
        message(FATAL_ERROR "formant accepted non-finite value for ${bad_flag}")
    endif()
    if(NOT nonfinite_error MATCHES "invalid number for ${bad_flag}")
        message(FATAL_ERROR "unexpected non-finite message for ${bad_flag}: ${nonfinite_error}")
    endif()
endforeach()

# Finite subnormal doubles are valid options: strtod sets ERANGE on underflow,
# but stod turned that into an out_of_range exception and rejected them.
foreach(subnormal_gain 1e-310 5e-324)
    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa a "--gain=${subnormal_gain}"
            --harmonics 1 --breathiness 0 --vibrato-depth 0 --pitch 50
            --sample-rate 22050 --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE subnormal_result
        ERROR_VARIABLE subnormal_error
    )
    if(NOT subnormal_result EQUAL 0)
        message(FATAL_ERROR "formant rejected finite subnormal gain ${subnormal_gain}: ${subnormal_error}")
    endif()
endforeach()

# Out-of-range integers are rejected without an out-of-range double-to-int cast.
foreach(out_of_range 3000000000 2147483648 -3000000000)
    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa "a" --harmonics "${out_of_range}" --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE integer_result
        ERROR_VARIABLE integer_error
    )
    if(integer_result EQUAL 0)
        message(FATAL_ERROR "formant accepted out-of-range harmonics '${out_of_range}'")
    endif()
    if(NOT integer_error MATCHES "expected an integer for --harmonics")
        message(FATAL_ERROR "unexpected integer message: ${integer_error}")
    endif()
endforeach()
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sample-rate 2147483648 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE integer_result
    ERROR_VARIABLE integer_error
)
if(integer_result EQUAL 0)
    message(FATAL_ERROR "formant accepted out-of-range sample rate")
endif()
if(NOT integer_error MATCHES "expected an integer for --sample-rate")
    message(FATAL_ERROR "unexpected sample-rate integer message: ${integer_error}")
endif()

# A near-zero fundamental with many harmonics must render promptly, not loop
# once per harmonic (billions of times) because the Nyquist break is unreachable.
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --pitch 0.000001 --harmonics 2000000000 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE subsonic_result
    ERROR_VARIABLE subsonic_error
    TIMEOUT 20
)
if(NOT subsonic_result EQUAL 0)
    message(FATAL_ERROR "formant hung or failed on a near-zero fundamental (${subsonic_result}): ${subsonic_error}")
endif()

# A standalone comma is the documented pause phoneme, not a separator to be
# discarded. It must add 0.12 s of silence, while an attached comma still acts
# only as a separator. Compare sample counts read from the WAV data header.
function(formant_sample_count spec out_var)
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "${spec}" --voice robot
            --sample-rate 8000 --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE comma_result
        ERROR_VARIABLE comma_error
    )
    if(NOT comma_result EQUAL 0)
        message(FATAL_ERROR "formant failed for '${spec}': ${comma_error}")
    endif()
    file(SIZE "${FORMANT_OUTPUT}" wav_bytes)
    math(EXPR samples "${wav_bytes} / 2 - 22")
    set(${out_var} ${samples} PARENT_SCOPE)
endfunction()
formant_sample_count("[a b]" plain_samples)
formant_sample_count("[a , b]" comma_samples)
formant_sample_count("[a, b]" separator_samples)
# 0.12 s at 8000 Hz is 960 samples; allow small envelope rounding.
math(EXPR comma_delta "${comma_samples} - ${plain_samples}")
if(comma_delta LESS 900)
    message(FATAL_ERROR "standalone comma did not add a pause: ${plain_samples} vs ${comma_samples}")
endif()
if(NOT separator_samples EQUAL plain_samples)
    message(FATAL_ERROR "attached comma changed the phoneme sequence: ${plain_samples} vs ${separator_samples}")
endif()

# Numeric overrides must apply regardless of whether they appear before or
# after --voice. An override seen before --voice used to be erased when the
# preset replaced the whole Voice struct.
function(formant_override_hash out_var)
    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa "a" ${ARGN} --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE override_result
        ERROR_VARIABLE override_error
    )
    if(NOT override_result EQUAL 0)
        message(FATAL_ERROR "formant failed for override test: ${override_error}")
    endif()
    file(SHA256 "${FORMANT_OUTPUT}" rendered_hash)
    set(${out_var} ${rendered_hash} PARENT_SCOPE)
endfunction()
formant_override_hash(override_before_hash --pitch 190 --rate 0.5 --harmonics 12 --voice alto)
formant_override_hash(override_after_hash --voice alto --pitch 190 --rate 0.5 --harmonics 12)
formant_override_hash(preset_only_hash --voice alto)
if(NOT override_before_hash STREQUAL override_after_hash)
    message(FATAL_ERROR "voice overrides depend on flag order: ${override_before_hash} vs ${override_after_hash}")
endif()
if(override_before_hash STREQUAL preset_only_hash)
    message(FATAL_ERROR "overrides before --voice were discarded")
endif()

# The language registry is discoverable, Spanish text renders, and --lang is
# rejected both for unsupported tags and for language-independent input modes.
execute_process(
    COMMAND "${FORMANT_CLI}" --list-languages
    RESULT_VARIABLE languages_result
    OUTPUT_VARIABLE languages_output
    ERROR_VARIABLE languages_error
)
if(NOT languages_result EQUAL 0 OR NOT languages_output MATCHES "Spanish")
    message(FATAL_ERROR "formant --list-languages failed: ${languages_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --lang es --text "Hola, mundo."
        --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE spanish_result
    ERROR_VARIABLE spanish_error
)
if(NOT spanish_result EQUAL 0)
    message(FATAL_ERROR "formant Spanish render failed (${spanish_result}): ${spanish_error}")
endif()
file(SIZE "${FORMANT_OUTPUT}" spanish_size)
if(spanish_size LESS 44)
    message(FATAL_ERROR "Spanish WAV output is too short: ${spanish_size} bytes")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --lang fr --text "bonjour" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE bad_language_result
    ERROR_VARIABLE bad_language_error
)
if(bad_language_result EQUAL 0)
    message(FATAL_ERROR "formant accepted unsupported language 'fr'")
endif()
if(NOT bad_language_error MATCHES "unsupported language tag")
    message(FATAL_ERROR "unexpected unsupported-language message: ${bad_language_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --lang es --ipa "a" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE language_mode_result
    ERROR_VARIABLE language_mode_error
)
if(language_mode_result EQUAL 0)
    message(FATAL_ERROR "formant accepted --lang with --ipa")
endif()
if(NOT language_mode_error MATCHES "--lang only applies to text input")
    message(FATAL_ERROR "unexpected --lang mode message: ${language_mode_error}")
endif()

# A standalone language definition file must drive text to speech. The file
# is data only: a grapheme map plus one lexicon entry is enough.
set(FORMANT_LANGUAGE_FILE "${CMAKE_CURRENT_BINARY_DIR}/smoke.lang")
file(WRITE "${FORMANT_LANGUAGE_FILE}" [=[name Smoke
 tag x-smoke
 phoneme a
 phoneme t
 grapheme a a
 grapheme t t
 word ta ˈta
]=])
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_LANGUAGE_FILE}"
        --text "ta" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE language_file_result
    ERROR_VARIABLE language_file_error
)
if(NOT language_file_result EQUAL 0)
    message(FATAL_ERROR "formant language-file render failed: ${language_file_error}")
endif()
file(SIZE "${FORMANT_OUTPUT}" language_file_size)
if(language_file_size LESS 44)
    message(FATAL_ERROR "language-file WAV output is too short: ${language_file_size} bytes")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --lang es --lang-file "${FORMANT_LANGUAGE_FILE}"
        --text "ta" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE language_conflict_result
    ERROR_VARIABLE language_conflict_error
)
if(language_conflict_result EQUAL 0)
    message(FATAL_ERROR "formant accepted both --lang and --lang-file")
endif()
if(NOT language_conflict_error MATCHES "use only one of --lang and --lang-file")
    message(FATAL_ERROR "unexpected language conflict message: ${language_conflict_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_LANGUAGE_FILE}"
        --ipa "a" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE language_mode_result
    ERROR_VARIABLE language_mode_error
)
if(language_mode_result EQUAL 0)
    message(FATAL_ERROR "formant accepted --lang-file with --ipa")
endif()
if(NOT language_mode_error MATCHES "--lang-file only applies to text input")
    message(FATAL_ERROR "unexpected --lang-file mode message: ${language_mode_error}")
endif()

file(WRITE "${FORMANT_LANGUAGE_FILE}" "grapheme a a\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_LANGUAGE_FILE}"
        --text "a" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE bad_language_file_result
    ERROR_VARIABLE bad_language_file_error
)
if(bad_language_file_result EQUAL 0)
    message(FATAL_ERROR "formant accepted a language file without a name")
endif()
if(NOT bad_language_file_error MATCHES "language definition has no name")
    message(FATAL_ERROR "unexpected language-file error: ${bad_language_file_error}")
endif()

# Trace mode reports ordered rule applications from the same engine.
set(FORMANT_TRACE_FILE "${CMAKE_CURRENT_BINARY_DIR}/trace.lang")
file(WRITE "${FORMANT_TRACE_FILE}" [=[name Trace
 phoneme a
 phoneme t
 grapheme a a
 grapheme t t
 phoneme-rule t > t / # _
]=])
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_TRACE_FILE}" --trace
        --text "ta" --sample-rate 22050 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE trace_result
    OUTPUT_VARIABLE trace_output
    ERROR_VARIABLE trace_error
)
if(NOT trace_result EQUAL 0)
    message(FATAL_ERROR "formant trace render failed: ${trace_error}")
endif()
if(NOT trace_output MATCHES "trace: phoneme-rule 1 at 0: t > t / # _")
    message(FATAL_ERROR "unexpected trace output: ${trace_output}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --trace --text "a" --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE trace_mode_result
    ERROR_VARIABLE trace_mode_error
)
if(trace_mode_result EQUAL 0)
    message(FATAL_ERROR "formant accepted --trace without --lang-file")
endif()
if(NOT trace_mode_error MATCHES "--trace only applies with --lang-file or --lexicon")
    message(FATAL_ERROR "unexpected --trace mode message: ${trace_mode_error}")
endif()

# A user lexicon supplies pronunciations as data and reports its source layer.
set(FORMANT_LEXICON_FILE "${CMAKE_CURRENT_BINARY_DIR}/smoke.lex")
file(WRITE "${FORMANT_LEXICON_FILE}" [=[# names
 word Ewan juːən
 homograph lead led after=us
]=])
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Ewan lead us" --lexicon
        "${FORMANT_LEXICON_FILE}" --trace --sample-rate 22050
        --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE lexicon_result
    OUTPUT_VARIABLE lexicon_output
    ERROR_VARIABLE lexicon_error
)
if(NOT lexicon_result EQUAL 0)
    message(FATAL_ERROR "formant lexicon render failed: ${lexicon_error}")
endif()
if(NOT lexicon_output MATCHES "trace: 1:ewan: lexicon")
    message(FATAL_ERROR "unexpected lexicon trace: ${lexicon_output}")
endif()
if(NOT lexicon_output MATCHES "trace: 2:lead: homograph")
    message(FATAL_ERROR "unexpected homograph trace: ${lexicon_output}")
endif()
file(SIZE "${FORMANT_OUTPUT}" lexicon_size)
if(lexicon_size LESS 44)
    message(FATAL_ERROR "lexicon WAV output is too short: ${lexicon_size} bytes")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --lexicon "${FORMANT_LEXICON_FILE}"
        --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE lexicon_mode_result
    ERROR_VARIABLE lexicon_mode_error
)
if(lexicon_mode_result EQUAL 0)
    message(FATAL_ERROR "formant accepted --lexicon with --ipa")
endif()
if(NOT lexicon_mode_error MATCHES "--lexicon only applies to text input")
    message(FATAL_ERROR "unexpected --lexicon mode message: ${lexicon_mode_error}")
endif()

# Exact timing: --timing infers the caption format from its extension and the
# word boundaries must match the rendered WAV sample count.
set(FORMANT_TIMING_DIR "${CMAKE_CURRENT_BINARY_DIR}/timing-smoke")
file(MAKE_DIRECTORY "${FORMANT_TIMING_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hello, world." --sample-rate 22050
        --output "${FORMANT_OUTPUT}"
        --timing "${FORMANT_TIMING_DIR}/words.json"
        --visemes "${FORMANT_TIMING_DIR}/visemes.json"
    RESULT_VARIABLE timing_result
    OUTPUT_VARIABLE timing_output
    ERROR_VARIABLE timing_error
)
if(NOT timing_result EQUAL 0)
    message(FATAL_ERROR "formant timing render failed (${timing_result}): ${timing_error}")
endif()
if(NOT timing_output MATCHES "Wrote timing to")
    message(FATAL_ERROR "formant did not report the timing output: ${timing_output}")
endif()
if(NOT timing_output MATCHES "Wrote visemes to")
    message(FATAL_ERROR "formant did not report the viseme output: ${timing_output}")
endif()
file(READ "${FORMANT_TIMING_DIR}/words.json" timing_json)
if(NOT timing_json MATCHES "\"format\": \"formant-timing\"")
    message(FATAL_ERROR "invalid timing JSON: ${timing_json}")
endif()
if(NOT timing_json MATCHES "\"label\": \"Hello\"")
    message(FATAL_ERROR "timing JSON is missing the first word: ${timing_json}")
endif()
if(NOT timing_json MATCHES "\"totalSamples\": 26791")
    message(FATAL_ERROR "timing JSON sample count does not match the WAV: ${timing_json}")
endif()
file(READ "${FORMANT_TIMING_DIR}/visemes.json" visemes_json)
if(NOT visemes_json MATCHES "\"format\": \"formant-visemes\"")
    message(FATAL_ERROR "invalid viseme JSON: ${visemes_json}")
endif()
if(NOT visemes_json MATCHES "\"shape\": \"round\"")
    message(FATAL_ERROR "viseme JSON has no lip shapes: ${visemes_json}")
endif()

foreach(ext srt vtt lrc)
    execute_process(
        COMMAND "${FORMANT_CLI}" --text "Hello, world." --sample-rate 22050
            --output "${FORMANT_OUTPUT}"
            --timing "${FORMANT_TIMING_DIR}/words.${ext}"
        RESULT_VARIABLE caption_result
        ERROR_VARIABLE caption_error
    )
    if(NOT caption_result EQUAL 0)
        message(FATAL_ERROR "formant --timing .${ext} failed: ${caption_error}")
    endif()
    file(READ "${FORMANT_TIMING_DIR}/words.${ext}" caption_text)
    if(ext STREQUAL "srt" AND NOT caption_text MATCHES "00:00:00,000 -->")
        message(FATAL_ERROR "invalid SRT output: ${caption_text}")
    endif()
    if(ext STREQUAL "vtt" AND NOT caption_text MATCHES "WEBVTT")
        message(FATAL_ERROR "invalid VTT output: ${caption_text}")
    endif()
    if(ext STREQUAL "lrc" AND NOT caption_text MATCHES "\\[00:00\\.")
        message(FATAL_ERROR "invalid LRC output: ${caption_text}")
    endif()
endforeach()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hello" --output "${FORMANT_OUTPUT}"
        --timing "${FORMANT_TIMING_DIR}/words.bad"
    RESULT_VARIABLE timing_ext_result
    ERROR_VARIABLE timing_ext_error
)
if(timing_ext_result EQUAL 0)
    message(FATAL_ERROR "formant accepted an unsupported timing extension")
endif()
if(NOT timing_ext_error MATCHES "unsupported timing file extension")
    message(FATAL_ERROR "unexpected timing extension error: ${timing_ext_error}")
endif()

# Timing files must be deterministic across runs, just like the WAV output.
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hello, world." --sample-rate 22050
        --output "${FORMANT_OUTPUT}"
        --timing "${FORMANT_TIMING_DIR}/again.json"
    RESULT_VARIABLE timing_again_result
    ERROR_VARIABLE timing_again_error
)
if(NOT timing_again_result EQUAL 0)
    message(FATAL_ERROR "second formant timing render failed: ${timing_again_error}")
endif()
file(SHA256 "${FORMANT_TIMING_DIR}/words.json" timing_first_hash)
file(SHA256 "${FORMANT_TIMING_DIR}/again.json" timing_second_hash)
if(NOT timing_first_hash STREQUAL timing_second_hash)
    message(FATAL_ERROR "timing output is not deterministic: ${timing_first_hash} vs ${timing_second_hash}")
endif()

# Audio output formats: the default WAV path remains byte-identical in shape,
# while 24-bit and float WAV files advertise their format in the fmt chunk and
# raw files contain samples only. All sizes derive from one baseline render.
get_filename_component(FORMANT_OUTPUT_DIR "${FORMANT_OUTPUT}" DIRECTORY)
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sample-rate 22050
        --output "${FORMANT_OUTPUT_DIR}/format16.wav"
    RESULT_VARIABLE format_result
    ERROR_VARIABLE format_error
)
if(NOT format_result EQUAL 0)
    message(FATAL_ERROR "default WAV render failed: ${format_error}")
endif()
file(SIZE "${FORMANT_OUTPUT_DIR}/format16.wav" wav16_bytes)
math(EXPR audio_data_bytes "${wav16_bytes} - 44")
math(EXPR audio_samples "${audio_data_bytes} / 2")
if(audio_samples LESS 1)
    message(FATAL_ERROR "default WAV render produced no samples")
endif()

foreach(format wav24 wav32f raw16 raw24 raw32f)
    set(format_path "${FORMANT_OUTPUT_DIR}/format.${format}")
    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa "a" --sample-rate 22050
            --format "${format}" --output "${format_path}"
        RESULT_VARIABLE format_result
        ERROR_VARIABLE format_error
    )
    if(NOT format_result EQUAL 0)
        message(FATAL_ERROR "--format ${format} render failed: ${format_error}")
    endif()
    file(SIZE "${format_path}" format_bytes)
    if(format STREQUAL "wav24")
        math(EXPR expected_bytes "44 + ${audio_samples} * 3")
        file(READ "${format_path}" fmt_tag OFFSET 20 LIMIT 2 HEX)
        file(READ "${format_path}" bits_per_sample OFFSET 34 LIMIT 2 HEX)
        file(READ "${format_path}" block_align OFFSET 32 LIMIT 2 HEX)
        if(NOT fmt_tag STREQUAL "0100" OR NOT bits_per_sample STREQUAL "1800" OR NOT block_align STREQUAL "0300")
            message(FATAL_ERROR "invalid 24-bit WAV fmt chunk: ${fmt_tag} ${bits_per_sample} ${block_align}")
        endif()
    elseif(format STREQUAL "wav32f")
        math(EXPR expected_bytes "44 + ${audio_samples} * 4")
        file(READ "${format_path}" fmt_tag OFFSET 20 LIMIT 2 HEX)
        file(READ "${format_path}" bits_per_sample OFFSET 34 LIMIT 2 HEX)
        file(READ "${format_path}" block_align OFFSET 32 LIMIT 2 HEX)
        if(NOT fmt_tag STREQUAL "0300" OR NOT bits_per_sample STREQUAL "2000" OR NOT block_align STREQUAL "0400")
            message(FATAL_ERROR "invalid float WAV fmt chunk: ${fmt_tag} ${bits_per_sample} ${block_align}")
        endif()
    elseif(format STREQUAL "raw16")
        math(EXPR expected_bytes "${audio_samples} * 2")
    elseif(format STREQUAL "raw24")
        math(EXPR expected_bytes "${audio_samples} * 3")
    else()
        math(EXPR expected_bytes "${audio_samples} * 4")
    endif()
    if(NOT format_bytes EQUAL expected_bytes)
        message(FATAL_ERROR "--format ${format} wrote ${format_bytes} bytes, expected ${expected_bytes}")
    endif()
endforeach()

execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --format ogg --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE bad_format_result
    ERROR_VARIABLE bad_format_error
)
if(bad_format_result EQUAL 0)
    message(FATAL_ERROR "formant accepted an unknown audio format")
endif()
if(NOT bad_format_error MATCHES "unknown audio format")
    message(FATAL_ERROR "unexpected audio format error: ${bad_format_error}")
endif()

# Deterministic dither is reproducible for a fixed seed, changes the quantized
# bytes, and is rejected for floating-point output before rendering.
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sample-rate 22050 --format raw16
        --dither 42 --output "${FORMANT_OUTPUT_DIR}/dither-a.raw"
    RESULT_VARIABLE dither_result
    ERROR_VARIABLE dither_error
)
if(NOT dither_result EQUAL 0)
    message(FATAL_ERROR "dithered render failed: ${dither_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sample-rate 22050 --format raw16
        --dither=42 --output "${FORMANT_OUTPUT_DIR}/dither-b.raw"
    RESULT_VARIABLE dither_result
    ERROR_VARIABLE dither_error
)
if(NOT dither_result EQUAL 0)
    message(FATAL_ERROR "second dithered render failed: ${dither_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sample-rate 22050 --format raw16
        --dither 43 --output "${FORMANT_OUTPUT_DIR}/dither-c.raw"
    RESULT_VARIABLE dither_result
    ERROR_VARIABLE dither_error
)
if(NOT dither_result EQUAL 0)
    message(FATAL_ERROR "dithered render with another seed failed: ${dither_error}")
endif()
file(SHA256 "${FORMANT_OUTPUT_DIR}/dither-a.raw" dither_hash_a)
file(SHA256 "${FORMANT_OUTPUT_DIR}/dither-b.raw" dither_hash_b)
file(SHA256 "${FORMANT_OUTPUT_DIR}/dither-c.raw" dither_hash_c)
file(SHA256 "${FORMANT_OUTPUT_DIR}/format.raw16" plain_hash)
if(NOT dither_hash_a STREQUAL dither_hash_b)
    message(FATAL_ERROR "dither is not deterministic for a fixed seed")
endif()
if(dither_hash_a STREQUAL dither_hash_c)
    message(FATAL_ERROR "different dither seeds produced identical audio")
endif()
if(dither_hash_a STREQUAL plain_hash)
    message(FATAL_ERROR "dither did not change the integer samples")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --format wav32f --dither 1
        --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE dither_float_result
    ERROR_VARIABLE dither_float_error
)
if(dither_float_result EQUAL 0)
    message(FATAL_ERROR "formant accepted --dither for float output")
endif()
if(NOT dither_float_error MATCHES "dither only applies")
    message(FATAL_ERROR "unexpected dither format error: ${dither_float_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --dither -1 --output "${FORMANT_OUTPUT}"
    RESULT_VARIABLE dither_seed_result
    ERROR_VARIABLE dither_seed_error
)
if(dither_seed_result EQUAL 0)
    message(FATAL_ERROR "formant accepted a negative dither seed")
endif()
if(NOT dither_seed_error MATCHES "unsigned 32-bit seed")
    message(FATAL_ERROR "unexpected dither seed error: ${dither_seed_error}")
endif()

# Raw stdout is byte-clean: progress and trace messages go to stderr, and the
# captured stream is exactly the same size as the equivalent raw file.
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sample-rate 22050 --format raw16
        --output -
    RESULT_VARIABLE stdout_result
    OUTPUT_FILE "${FORMANT_OUTPUT_DIR}/stdout.raw"
    ERROR_FILE "${FORMANT_OUTPUT_DIR}/stdout.err"
)
if(NOT stdout_result EQUAL 0)
    message(FATAL_ERROR "raw stdout render failed: ${stdout_result}")
endif()
file(SIZE "${FORMANT_OUTPUT_DIR}/stdout.raw" stdout_bytes)
if(NOT stdout_bytes EQUAL audio_data_bytes)
    message(FATAL_ERROR "stdout raw PCM is not byte-clean: ${stdout_bytes} vs ${audio_data_bytes}")
endif()
file(READ "${FORMANT_OUTPUT_DIR}/stdout.err" stdout_status)
if(NOT stdout_status MATCHES "Wrote .* samples to stdout")
    message(FATAL_ERROR "stdout render did not report on stderr: ${stdout_status}")
endif()
file(READ "${FORMANT_OUTPUT_DIR}/stdout.raw" stdout_head LIMIT 4 HEX)
file(READ "${FORMANT_OUTPUT_DIR}/format.raw16" raw_head LIMIT 4 HEX)
if(NOT stdout_head STREQUAL raw_head)
    message(FATAL_ERROR "stdout PCM differs from file output: ${stdout_head} vs ${raw_head}")
endif()

# Issue #126: voice definition files and the parameter lab. The parameter
# table and every built-in preset must be expressible as shareable data.
execute_process(
    COMMAND "${FORMANT_CLI}" --list-voice-parameters
    RESULT_VARIABLE voice_parameters_result
    OUTPUT_VARIABLE voice_parameters_output
    ERROR_VARIABLE voice_parameters_error
)
if(NOT voice_parameters_result EQUAL 0)
    message(FATAL_ERROR "formant --list-voice-parameters failed: ${voice_parameters_error}")
endif()
if(NOT voice_parameters_output MATCHES "safe-min"
   OR NOT voice_parameters_output MATCHES "pitch"
   OR NOT voice_parameters_output MATCHES "harmonics")
    message(FATAL_ERROR "voice parameter table is incomplete: ${voice_parameters_output}")
endif()

set(VOICE_LAB_DIR "${FORMANT_OUTPUT_DIR}/voice-lab")
file(MAKE_DIRECTORY "${VOICE_LAB_DIR}")
foreach(preset neutral alto bass child robot whisper)
    set(voice_file "${VOICE_LAB_DIR}/${preset}.voice")
    execute_process(
        COMMAND "${FORMANT_CLI}" --dump-voice "${preset}"
        RESULT_VARIABLE dump_result
        OUTPUT_FILE "${voice_file}"
        ERROR_VARIABLE dump_error
    )
    if(NOT dump_result EQUAL 0)
        message(FATAL_ERROR "--dump-voice ${preset} failed: ${dump_error}")
    endif()
    file(READ "${voice_file}" voice_source)
    if(NOT voice_source MATCHES "schema 1" OR NOT voice_source MATCHES "base ${preset}")
        message(FATAL_ERROR "voice file for ${preset} is not self-describing: ${voice_source}")
    endif()

    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --voice "${preset}" --sample-rate 22050
            --output "${VOICE_LAB_DIR}/compiled.wav"
        RESULT_VARIABLE compiled_result
        ERROR_VARIABLE compiled_error
    )
    execute_process(
        COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --voice-file "${voice_file}" --sample-rate 22050
            --output "${VOICE_LAB_DIR}/file.wav"
        RESULT_VARIABLE file_result
        ERROR_VARIABLE file_error
    )
    if(NOT compiled_result EQUAL 0 OR NOT file_result EQUAL 0)
        message(FATAL_ERROR "preset/file render failed for ${preset}: ${compiled_error} ${file_error}")
    endif()
    file(SHA256 "${VOICE_LAB_DIR}/compiled.wav" compiled_hash)
    file(SHA256 "${VOICE_LAB_DIR}/file.wav" file_hash)
    if(NOT compiled_hash STREQUAL file_hash)
        message(FATAL_ERROR "${preset} voice file does not round-trip byte-identically")
    endif()
endforeach()

file(WRITE "${VOICE_LAB_DIR}/warm.voice"
    "schema 1\nname Warm\nbase alto\npitch 196.5\nbreathiness 0.02\ntaper-ceiling false\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --voice-file "${VOICE_LAB_DIR}/warm.voice"
        --output "${VOICE_LAB_DIR}/warm.wav"
    RESULT_VARIABLE warm_result
    ERROR_VARIABLE warm_error
)
if(NOT warm_result EQUAL 0)
    message(FATAL_ERROR "layered voice file failed: ${warm_error}")
endif()
file(SHA256 "${VOICE_LAB_DIR}/warm.wav" warm_hash)
file(SHA256 "${VOICE_LAB_DIR}/compiled.wav" whisper_hash)
if(warm_hash STREQUAL whisper_hash)
    message(FATAL_ERROR "voice file parameters had no effect")
endif()

file(WRITE "${VOICE_LAB_DIR}/bad-value.voice" "schema 1\npitch 3000\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --voice-file "${VOICE_LAB_DIR}/bad-value.voice"
        --output "${VOICE_LAB_DIR}/bad.wav"
    RESULT_VARIABLE bad_voice_result
    ERROR_VARIABLE bad_voice_error
)
if(bad_voice_result EQUAL 0 OR NOT bad_voice_error MATCHES "voice line 2: invalid value for pitch")
    message(FATAL_ERROR "bad voice value was not rejected early: ${bad_voice_error}")
endif()
if(EXISTS "${VOICE_LAB_DIR}/bad.wav")
    message(FATAL_ERROR "bad voice file wrote audio before failing validation")
endif()
file(WRITE "${VOICE_LAB_DIR}/no-schema.voice" "pitch 120\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --voice-file "${VOICE_LAB_DIR}/no-schema.voice"
        --output "${VOICE_LAB_DIR}/bad.wav"
    RESULT_VARIABLE no_schema_result
    ERROR_VARIABLE no_schema_error
)
if(no_schema_result EQUAL 0 OR NOT no_schema_error MATCHES "requires 'schema 1'")
    message(FATAL_ERROR "unversioned voice file was accepted: ${no_schema_error}")
endif()

# One- and two-parameter sweeps write labeled renders and a deterministic
# machine-readable index mapping each file to its parameter values.
set(SWEEP_DIR "${VOICE_LAB_DIR}/sweep")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sweep "pitch=110:170:4,breathiness=0:0.03:2"
        --lab-dir "${SWEEP_DIR}"
    RESULT_VARIABLE sweep_result
    ERROR_VARIABLE sweep_error
)
if(NOT sweep_result EQUAL 0)
    message(FATAL_ERROR "two-parameter sweep failed: ${sweep_error}")
endif()
file(READ "${SWEEP_DIR}/index.json" sweep_index)
if(NOT sweep_index MATCHES "\"mode\": \"sweep\""
   OR NOT sweep_index MATCHES "\"pitch\""
   OR NOT sweep_index MATCHES "\"breathiness\"")
    message(FATAL_ERROR "sweep index is missing mode or parameters: ${sweep_index}")
endif()
foreach(cell 001 002 003 004 005 006 007 008)
    file(GLOB cell_files "${SWEEP_DIR}/${cell}-*.wav")
    list(LENGTH cell_files cell_count)
    if(NOT cell_count EQUAL 1)
        message(FATAL_ERROR "sweep cell ${cell} did not produce exactly one WAV")
    endif()
endforeach()
file(SHA256 "${SWEEP_DIR}/index.json" sweep_index_hash)
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sweep "pitch=110:170:4,breathiness=0:0.03:2"
        --lab-dir "${SWEEP_DIR}"
    RESULT_VARIABLE sweep_result_again
    ERROR_VARIABLE sweep_error_again
)
if(NOT sweep_result_again EQUAL 0)
    message(FATAL_ERROR "second sweep failed: ${sweep_error_again}")
endif()
file(SHA256 "${SWEEP_DIR}/index.json" sweep_index_hash_again)
if(NOT sweep_index_hash_again STREQUAL sweep_index_hash)
    message(FATAL_ERROR "sweep index is not deterministic")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --sweep "pitch=120:180:3" --pair "neutral,alto"
        --lab-dir "${SWEEP_DIR}"
    RESULT_VARIABLE lab_conflict_result
    ERROR_VARIABLE lab_conflict_error
)
if(lab_conflict_result EQUAL 0 OR NOT lab_conflict_error MATCHES "use only one of --sweep and --pair")
    message(FATAL_ERROR "sweep/pair conflict was not rejected: ${lab_conflict_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --pair "neutral"
        --lab-dir "${VOICE_LAB_DIR}"
    RESULT_VARIABLE bad_pair_result
    ERROR_VARIABLE bad_pair_error
)
if(bad_pair_result EQUAL 0 OR NOT bad_pair_error MATCHES "two voice specifications")
    message(FATAL_ERROR "incomplete --pair spec was accepted: ${bad_pair_error}")
endif()

# Pair mode renders both sides on the identical phoneme sequence, so neutral
# and alto differ while neutral against itself is byte-identical.
set(PAIR_DIR "${VOICE_LAB_DIR}/pair")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --pair "neutral,alto"
        --lab-dir "${PAIR_DIR}"
    RESULT_VARIABLE pair_result
    ERROR_VARIABLE pair_error
)
if(NOT pair_result EQUAL 0)
    message(FATAL_ERROR "pair render failed: ${pair_error}")
endif()
file(READ "${PAIR_DIR}/index.json" pair_index)
if(NOT pair_index MATCHES "\"mode\": \"pair\"" OR NOT pair_index MATCHES "\"a.wav\"")
    message(FATAL_ERROR "pair index is incomplete: ${pair_index}")
endif()
file(SHA256 "${PAIR_DIR}/a.wav" pair_a_hash)
file(SHA256 "${PAIR_DIR}/b.wav" pair_b_hash)
if(pair_a_hash STREQUAL pair_b_hash)
    message(FATAL_ERROR "pair A/B rendered identical audio for different voices")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --pair "neutral,neutral:pitch=200"
        --lab-dir "${VOICE_LAB_DIR}/pair-inline"
    RESULT_VARIABLE pair_inline_result
    ERROR_VARIABLE pair_inline_error
)
if(NOT pair_inline_result EQUAL 0)
    message(FATAL_ERROR "inline pair parameters failed: ${pair_inline_error}")
endif()
file(SHA256 "${VOICE_LAB_DIR}/pair-inline/a.wav" pair_same_hash)
file(SHA256 "${VOICE_LAB_DIR}/pair-inline/b.wav" pair_tuned_hash)
if(pair_same_hash STREQUAL pair_tuned_hash)
    message(FATAL_ERROR "inline pair parameter had no effect")
endif()

message(STATUS "formant CLI smoke test wrote ${output_size} bytes")

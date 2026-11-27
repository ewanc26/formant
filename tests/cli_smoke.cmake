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

# #112: conlang inventories, nearest-feature fallback and typology defaults.
set(FORMANT_FALLBACK_FILE "${CMAKE_CURRENT_BINARY_DIR}/fallback.lang")
file(WRITE "${FORMANT_FALLBACK_FILE}" [=[name Fallback Smoke
lenient
tone simple
stress fixed
rhythm syllable
rate 0.9
phoneme a
phoneme s
grapheme a a
grapheme z z
]=])
execute_process(
    COMMAND "${FORMANT_CLI}" --list-phonemes --lang-file "${FORMANT_FALLBACK_FILE}"
    RESULT_VARIABLE conlang_list_result
    OUTPUT_VARIABLE conlang_list_output
    ERROR_VARIABLE conlang_list_error
)
if(NOT conlang_list_result EQUAL 0)
    message(FATAL_ERROR "conlang --list-phonemes failed: ${conlang_list_error}")
endif()
if(NOT conlang_list_output STREQUAL "a\ns\n")
    message(FATAL_ERROR "--list-phonemes did not print the conlang inventory: ${conlang_list_output}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_FALLBACK_FILE}"
        --list-phonemes
    RESULT_VARIABLE conlang_list2_result
    OUTPUT_VARIABLE conlang_list2_output
    ERROR_VARIABLE conlang_list2_error
)
if(NOT conlang_list2_result EQUAL 0 OR NOT conlang_list2_output STREQUAL conlang_list_output)
    message(FATAL_ERROR "--list-phonemes depends on flag order: ${conlang_list2_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_FALLBACK_FILE}"
        --text "z a" --sample-rate 22050
        --output "${CMAKE_CURRENT_BINARY_DIR}/fallback.wav"
    RESULT_VARIABLE conlang_fallback_result
    OUTPUT_VARIABLE conlang_fallback_output
    ERROR_VARIABLE conlang_fallback_error
)
if(NOT conlang_fallback_result EQUAL 0)
    message(FATAL_ERROR "conlang fallback render failed: ${conlang_fallback_error}")
endif()
if(NOT conlang_fallback_output MATCHES "warning: phoneme \"z\" is outside the Fallback Smoke inventory; using nearest declared phoneme \"s\"")
    message(FATAL_ERROR "missing nearest-feature warning: ${conlang_fallback_output}")
endif()

set(FORMANT_STRICT112_FILE "${CMAKE_CURRENT_BINARY_DIR}/strict112.lang")
file(WRITE "${FORMANT_STRICT112_FILE}" [=[name Strict Smoke
strict
phoneme a
phoneme s
grapheme a a
grapheme z z
]=])
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_STRICT112_FILE}"
        --text "z" --output "${CMAKE_CURRENT_BINARY_DIR}/strict112.wav"
    RESULT_VARIABLE conlang_strict_result
    ERROR_VARIABLE conlang_strict_error
)
if(conlang_strict_result EQUAL 0 OR NOT conlang_strict_error MATCHES "nearest declared phoneme: \"s\"")
    message(FATAL_ERROR "strict inventory error lost its suggestion: ${conlang_strict_error}")
endif()

file(WRITE "${FORMANT_FALLBACK_FILE}" "name Bad\nname Bad\ntone wild\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${FORMANT_FALLBACK_FILE}"
        --text "a" --output "${CMAKE_CURRENT_BINARY_DIR}/bad112.wav"
    RESULT_VARIABLE conlang_bad_result
    ERROR_VARIABLE conlang_bad_error
)
if(conlang_bad_result EQUAL 0 OR NOT conlang_bad_error MATCHES "unknown tone value: wild")
    message(FATAL_ERROR "invalid typology value was accepted: ${conlang_bad_error}")
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

# Issue #134: distributable voice packages carry attribution and a checksum,
# and the shipped gallery round-trips the built-in presets byte-for-byte.
set(PACKAGE_DIR "${VOICE_LAB_DIR}/packages")
file(MAKE_DIRECTORY "${PACKAGE_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --package-voice alto --voice-name "Renamed Alto"
        --voice-author "Test Author" --voice-description "A test package"
        --voice-license CC0-1.0 --voice-version 2.1.0
    OUTPUT_VARIABLE package_text
    RESULT_VARIABLE package_result
    ERROR_VARIABLE package_error
)
if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "--package-voice failed: ${package_error}")
endif()
if(NOT package_text MATCHES "schema 2" OR NOT package_text MATCHES "name Renamed Alto"
        OR NOT package_text MATCHES "author Test Author"
        OR NOT package_text MATCHES "license CC0-1.0"
        OR NOT package_text MATCHES "version 2.1.0"
        OR NOT package_text MATCHES "checksum [0-9a-f][0-9a-f]")
    message(FATAL_ERROR "voice package is incomplete: ${package_text}")
endif()
file(WRITE "${PACKAGE_DIR}/alto.voice" "${package_text}")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-voice "${PACKAGE_DIR}/alto.voice"
    OUTPUT_VARIABLE inspect_text
    RESULT_VARIABLE inspect_result
    ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0)
    message(FATAL_ERROR "--inspect-voice failed: ${inspect_error}")
endif()
if(NOT inspect_text MATCHES "schema\t2" OR NOT inspect_text MATCHES "author\tTest Author"
        OR NOT inspect_text MATCHES "status\tvalid")
    message(FATAL_ERROR "inspect did not report valid package metadata: ${inspect_text}")
endif()
file(SHA256 "${PACKAGE_DIR}/alto.voice" package_hash)
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --voice-file "${PACKAGE_DIR}/alto.voice"
        --output "${PACKAGE_DIR}/compiled.wav" --sample-rate 22050
    RESULT_VARIABLE package_render_result
    ERROR_VARIABLE package_render_error
)
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "ˈhəˈloʊ" --voice alto
        --output "${PACKAGE_DIR}/file.wav" --sample-rate 22050
    RESULT_VARIABLE preset_render_result
    ERROR_VARIABLE preset_render_error
)
if(NOT package_render_result EQUAL 0 OR NOT preset_render_result EQUAL 0)
    message(FATAL_ERROR "package round-trip render failed: ${package_render_error} ${preset_render_error}")
endif()
file(SHA256 "${PACKAGE_DIR}/compiled.wav" package_render_hash)
file(SHA256 "${PACKAGE_DIR}/file.wav" preset_render_hash)
if(NOT package_render_hash STREQUAL preset_render_hash)
    message(FATAL_ERROR "voice package did not reproduce the built-in preset")
endif()

# Tampering with a package is detected before any audio is written.
file(READ "${PACKAGE_DIR}/alto.voice" tamper_text)
string(REPLACE "A test package" "A changed package" tamper_text "${tamper_text}")
file(WRITE "${PACKAGE_DIR}/tampered.voice" "${tamper_text}")
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --voice-file "${PACKAGE_DIR}/tampered.voice"
        --output "${PACKAGE_DIR}/bad.wav"
    RESULT_VARIABLE tamper_result
    ERROR_VARIABLE tamper_error
)
if(tamper_result EQUAL 0 OR NOT tamper_error MATCHES "checksum mismatch")
    message(FATAL_ERROR "tampered voice package was accepted: ${tamper_error}")
endif()
if(EXISTS "${PACKAGE_DIR}/bad.wav")
    message(FATAL_ERROR "tampered package wrote audio before failing verification")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-voice "${PACKAGE_DIR}/tampered.voice"
    OUTPUT_VARIABLE tamper_inspect_text
    RESULT_VARIABLE tamper_inspect_result
    ERROR_VARIABLE tamper_inspect_error
)
if(NOT tamper_inspect_result EQUAL 0 OR NOT tamper_inspect_text MATCHES "status\tinvalid")
    message(FATAL_ERROR "--inspect-voice must report a bad checksum: ${tamper_inspect_error}")
endif()

# A package without a checksum is reported as unsigned by inspection but is
# still rejected by the loader before any audio is written.
file(WRITE "${PACKAGE_DIR}/unsigned.voice"
    "schema 2\nname Unsigned\nauthor Test Author\ndescription No checksum\nlicense CC0-1.0\nversion 1.0.0\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-voice "${PACKAGE_DIR}/unsigned.voice"
    OUTPUT_VARIABLE unsigned_inspect_text
    RESULT_VARIABLE unsigned_inspect_result
    ERROR_VARIABLE unsigned_inspect_error
)
if(NOT unsigned_inspect_result EQUAL 0 OR NOT unsigned_inspect_text MATCHES "status\tunsigned")
    message(FATAL_ERROR "--inspect-voice must report an unsigned package: ${unsigned_inspect_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --voice-file "${PACKAGE_DIR}/unsigned.voice"
        --output "${PACKAGE_DIR}/bad.wav"
    RESULT_VARIABLE unsigned_load_result
    ERROR_VARIABLE unsigned_load_error
)
if(unsigned_load_result EQUAL 0 OR NOT unsigned_load_error MATCHES "requires a checksum")
    message(FATAL_ERROR "unsigned voice package was accepted: ${unsigned_load_error}")
endif()

# Every shipped gallery voice is a valid package and the preset round-trips
# render exactly like their compiled counterparts.
if(DEFINED FORMANT_EXAMPLES_DIR)
    file(GLOB gallery_files "${FORMANT_EXAMPLES_DIR}/voices/*.voice")
    list(LENGTH gallery_files gallery_count)
    if(gallery_count LESS 5)
        message(FATAL_ERROR "expected at least five example voices, found ${gallery_count}")
    endif()
    foreach(gallery_file ${gallery_files})
        execute_process(
            COMMAND "${FORMANT_CLI}" --inspect-voice "${gallery_file}"
            OUTPUT_VARIABLE gallery_inspect
            RESULT_VARIABLE gallery_result
            ERROR_VARIABLE gallery_error
        )
        if(NOT gallery_result EQUAL 0 OR NOT gallery_inspect MATCHES "status\tvalid")
            message(FATAL_ERROR "invalid gallery voice ${gallery_file}: ${gallery_error}")
        endif()
    endforeach()
    foreach(preset neutral alto bass child robot whisper)
        execute_process(
            COMMAND "${FORMANT_CLI}" --ipa "a" --voice "${preset}"
                --sample-rate 22050 --output "${PACKAGE_DIR}/gallery-preset.wav"
            RESULT_VARIABLE gallery_preset_result
        )
        execute_process(
            COMMAND "${FORMANT_CLI}" --ipa "a" --voice-file "${FORMANT_EXAMPLES_DIR}/voices/${preset}.voice"
                --sample-rate 22050 --output "${PACKAGE_DIR}/gallery-file.wav"
            RESULT_VARIABLE gallery_file_result
        )
        if(NOT gallery_preset_result EQUAL 0 OR NOT gallery_file_result EQUAL 0)
            message(FATAL_ERROR "gallery render failed for ${preset}")
        endif()
        file(SHA256 "${PACKAGE_DIR}/gallery-preset.wav" gallery_preset_hash)
        file(SHA256 "${PACKAGE_DIR}/gallery-file.wav" gallery_file_hash)
        if(NOT gallery_preset_hash STREQUAL gallery_file_hash)
            message(FATAL_ERROR "gallery voice ${preset} is not byte-identical to the preset")
        endif()
    endforeach()
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

# --- issue #114: seeded phonotactic gibberish -----------------------------
set(GIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/gibberish")
file(MAKE_DIRECTORY "${GIB_DIR}")

execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --seed 7 --count 4
        --transcript "${GIB_DIR}/first.txt" --output "${GIB_DIR}/first.wav"
    RESULT_VARIABLE gib_first_result
    ERROR_VARIABLE gib_first_error
    OUTPUT_VARIABLE gib_first_stdout
)
if(NOT gib_first_result EQUAL 0)
    message(FATAL_ERROR "gibberish render failed: ${gib_first_error}")
endif()
if(NOT gib_first_stdout MATCHES "Generated 4 word")
    message(FATAL_ERROR "gibberish status missing: ${gib_first_stdout}")
endif()
file(SHA256 "${GIB_DIR}/first.wav" gib_first_hash)

execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --seed 7 --count 4
        --output "${GIB_DIR}/second.wav"
    RESULT_VARIABLE gib_second_result
    ERROR_VARIABLE gib_second_error
)
if(NOT gib_second_result EQUAL 0)
    message(FATAL_ERROR "second gibberish render failed: ${gib_second_error}")
endif()
file(SHA256 "${GIB_DIR}/second.wav" gib_second_hash)
if(NOT gib_second_hash STREQUAL gib_first_hash)
    message(FATAL_ERROR "same seed did not reproduce gibberish audio")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --seed 8 --count 4
        --output "${GIB_DIR}/other.wav"
    RESULT_VARIABLE gib_other_result
    ERROR_VARIABLE gib_other_error
)
if(NOT gib_other_result EQUAL 0)
    message(FATAL_ERROR "different-seed render failed: ${gib_other_error}")
endif()
file(SHA256 "${GIB_DIR}/other.wav" gib_other_hash)
if(gib_other_hash STREQUAL gib_first_hash)
    message(FATAL_ERROR "different seeds produced identical gibberish audio")
endif()

file(READ "${GIB_DIR}/first.txt" gib_transcript)
if(gib_transcript STREQUAL "" OR NOT gib_transcript MATCHES "\n")
    message(FATAL_ERROR "gibberish transcript is missing or not line-per-word")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --list-pattern-help
    RESULT_VARIABLE gib_help_result
    OUTPUT_VARIABLE gib_help_output
)
if(NOT gib_help_result EQUAL 0 OR NOT gib_help_output MATCHES "Awkwords-compatible")
    message(FATAL_ERROR "pattern help is missing: ${gib_help_output}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --seed 7 --count 4 --lang-file
        "${CMAKE_CURRENT_LIST_DIR}/../examples/velta.lang"
        --output "${GIB_DIR}/conlang.wav"
    RESULT_VARIABLE gib_conlang_result
    ERROR_VARIABLE gib_conlang_error
)
if(NOT gib_conlang_result EQUAL 0)
    message(FATAL_ERROR "conlang gibberish render failed: ${gib_conlang_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --seed not-a-number
        --output "${GIB_DIR}/bad.wav"
    RESULT_VARIABLE gib_bad_seed_result
    ERROR_VARIABLE gib_bad_seed_error
)
if(gib_bad_seed_result EQUAL 0 OR NOT gib_bad_seed_error MATCHES "unsigned 32-bit seed for --seed")
    message(FATAL_ERROR "invalid --seed was not rejected: ${gib_bad_seed_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --ipa "a"
        --output "${GIB_DIR}/conflict.wav"
    RESULT_VARIABLE gib_conflict_result
    ERROR_VARIABLE gib_conflict_error
)
if(gib_conflict_result EQUAL 0 OR NOT gib_conflict_error MATCHES "use only one of --gibberish, --phonemes, --ipa or --xsampa")
    message(FATAL_ERROR "gibberish/input-mode conflict was not rejected: ${gib_conflict_error}")
endif()

# #115: statistical/combined gibberish modes.
execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "ignored" --gibberish-style statistical
        --seed 5 --count 3 --output "${GIB_DIR}/stat1.wav"
    RESULT_VARIABLE gib_stat_result
    ERROR_VARIABLE gib_stat_error
)
if(NOT gib_stat_result EQUAL 0)
    message(FATAL_ERROR "statistical gibberish failed: ${gib_stat_error}")
endif()
file(SHA256 "${GIB_DIR}/stat1.wav" gib_stat_hash)
execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "ignored" --gibberish-style statistical
        --seed 5 --count 3 --output "${GIB_DIR}/stat2.wav"
    RESULT_VARIABLE gib_stat2_result
    ERROR_VARIABLE gib_stat2_error
)
if(NOT gib_stat2_result EQUAL 0)
    message(FATAL_ERROR "statistical gibberish second run failed: ${gib_stat2_error}")
endif()
file(SHA256 "${GIB_DIR}/stat2.wav" gib_stat2_hash)
if(NOT gib_stat_hash STREQUAL gib_stat2_hash)
    message(FATAL_ERROR "statistical gibberish not deterministic")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --gibberish-style combined
        --seed 5 --count 3 --output "${GIB_DIR}/combined.wav"
    RESULT_VARIABLE gib_comb_result
    ERROR_VARIABLE gib_comb_error
)
if(NOT gib_comb_result EQUAL 0)
    message(FATAL_ERROR "combined gibberish failed: ${gib_comb_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --gibberish "CVC" --gibberish-style nope
        --seed 5 --count 1 --output "${GIB_DIR}/badstyle.wav"
    RESULT_VARIABLE gib_badstyle_result
    ERROR_VARIABLE gib_badstyle_error
)
if(gib_badstyle_result EQUAL 0 OR NOT gib_badstyle_error MATCHES "unknown gibberish style")
    message(FATAL_ERROR "bad gibberish style was not rejected: ${gib_badstyle_error}")
endif()

# #105: MRT intelligibility battery generation.
set(MRT_DIR "${CMAKE_CURRENT_BINARY_DIR}/mrt-smoke")
file(REMOVE_RECURSE "${MRT_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --mrt "${MRT_DIR}" --voice neutral --rate 1.0 --pitch 120
    RESULT_VARIABLE mrt_result
    ERROR_VARIABLE mrt_error
)
if(NOT mrt_result EQUAL 0)
    message(FATAL_ERROR "--mrt failed: ${mrt_error}")
endif()
foreach(mrt_file response_sheet.csv answer_key.csv README.txt mrt_001_e01_initial_went.wav mrt_300_e50_initial_nun.wav)
    if(NOT EXISTS "${MRT_DIR}/${mrt_file}")
        message(FATAL_ERROR "missing MRT file: ${mrt_file}")
    endif()
endforeach()
file(READ "${MRT_DIR}/response_sheet.csv" mrt_response)
if(NOT mrt_response MATCHES "trial,wav,ensemble,focus,target,response,correct,voice,rate,pitch")
    message(FATAL_ERROR "MRT response sheet header missing")
endif()

# The exact transcription-mode error text is a compatibility contract.
execute_process(
    COMMAND "${FORMANT_CLI}" --ipa "a" --phonemes "[a]" --output "${GIB_DIR}/contract.wav"
    RESULT_VARIABLE gib_contract_result
    ERROR_VARIABLE gib_contract_error
)
if(gib_contract_result EQUAL 0 OR NOT gib_contract_error MATCHES "use only one of --phonemes, --ipa or --xsampa \\(each overrides --text and --stdin\\)")
    message(FATAL_ERROR "mutual-exclusion message changed: ${gib_contract_error}")
endif()

# #124: data-defined nonverbal vocalizations.
execute_process(
    COMMAND "${FORMANT_CLI}" --list-nvv
    RESULT_VARIABLE nvv_list_result
    OUTPUT_VARIABLE nvv_list_output
    ERROR_VARIABLE nvv_list_error
)
if(NOT nvv_list_result EQUAL 0)
    message(FATAL_ERROR "--list-nvv failed: ${nvv_list_error}")
endif()
foreach(nvv_name laugh sigh breath-in breath-out throat-clear)
    if(NOT nvv_list_output MATCHES "${nvv_name}")
        message(FATAL_ERROR "--list-nvv is missing ${nvv_name}: ${nvv_list_output}")
    endif()
endforeach()
if(NOT nvv_list_output MATCHES "nervous")
    message(FATAL_ERROR "--list-nvv is missing styles: ${nvv_list_output}")
endif()

set(NVV_DIR "${CMAKE_CURRENT_BINARY_DIR}/nvv")
file(MAKE_DIRECTORY "${NVV_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hello [laugh] world."
        --sample-rate 22050 --output "${NVV_DIR}/laugh.wav"
    RESULT_VARIABLE nvv_inline_result
    ERROR_VARIABLE nvv_inline_error
)
if(NOT nvv_inline_result EQUAL 0)
    message(FATAL_ERROR "inline NVV render failed: ${nvv_inline_error}")
endif()
file(SIZE "${NVV_DIR}/laugh.wav" nvv_inline_size)
if(nvv_inline_size LESS 100)
    message(FATAL_ERROR "inline NVV WAV is too short")
endif()

# Styles change the rendered bytes, and equal inputs are byte-identical.
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hi [sigh]" --nvv-style nervous
        --sample-rate 22050 --output "${NVV_DIR}/nervous.wav"
    RESULT_VARIABLE nvv_style_result
    ERROR_VARIABLE nvv_style_error
)
if(NOT nvv_style_result EQUAL 0)
    message(FATAL_ERROR "NVV style render failed: ${nvv_style_error}")
endif()
file(SHA256 "${NVV_DIR}/nervous.wav" nvv_nervous_hash)
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hi [sigh]" --nvv-style nervous
        --sample-rate 22050 --output "${NVV_DIR}/nervous2.wav"
    RESULT_VARIABLE nvv_style2_result
    ERROR_VARIABLE nvv_style2_error
)
if(NOT nvv_style2_result EQUAL 0)
    message(FATAL_ERROR "NVV style rerun failed: ${nvv_style2_error}")
endif()
file(SHA256 "${NVV_DIR}/nervous2.wav" nvv_nervous2_hash)
if(NOT nvv_nervous_hash STREQUAL nvv_nervous2_hash)
    message(FATAL_ERROR "NVV render is not deterministic")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hi [sigh]" --nvv-style warm
        --sample-rate 22050 --output "${NVV_DIR}/warm.wav"
    RESULT_VARIABLE nvv_warm_result
    ERROR_VARIABLE nvv_warm_error
)
if(NOT nvv_warm_result EQUAL 0)
    message(FATAL_ERROR "NVV warm style failed: ${nvv_warm_error}")
endif()
file(SHA256 "${NVV_DIR}/warm.wav" nvv_warm_hash)
if(nvv_warm_hash STREQUAL nvv_nervous_hash)
    message(FATAL_ERROR "NVV styles produced identical audio")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "Hi [nope]" --output "${NVV_DIR}/bad.wav"
    RESULT_VARIABLE nvv_bad_result
    ERROR_VARIABLE nvv_bad_error
)
if(nvv_bad_result EQUAL 0 OR NOT nvv_bad_error MATCHES "unknown nonverbal vocalization: 'nope'")
    message(FATAL_ERROR "unknown NVV token was not rejected: ${nvv_bad_error}")
endif()

file(WRITE "${NVV_DIR}/smoke.ssml" "<speak>Hi <nvv name=\"laugh\"/> there.</speak>")
execute_process(
    COMMAND "${FORMANT_CLI}" --ssml-file "${NVV_DIR}/smoke.ssml"
        --sample-rate 22050 --output "${NVV_DIR}/ssml.wav"
    RESULT_VARIABLE nvv_ssml_result
    ERROR_VARIABLE nvv_ssml_error
)
if(NOT nvv_ssml_result EQUAL 0)
    message(FATAL_ERROR "SSML <nvv> render failed: ${nvv_ssml_error}")
endif()
file(WRITE "${NVV_DIR}/bad.ssml" "<speak><nvv name=\"nope\"/></speak>")
execute_process(
    COMMAND "${FORMANT_CLI}" --ssml-file "${NVV_DIR}/bad.ssml"
        --output "${NVV_DIR}/ssmlbad.wav"
    RESULT_VARIABLE nvv_ssml_bad_result
    ERROR_VARIABLE nvv_ssml_bad_error
)
if(nvv_ssml_bad_result EQUAL 0 OR NOT nvv_ssml_bad_error MATCHES "SSML at byte")
    message(FATAL_ERROR "unknown SSML NVV was not rejected: ${nvv_ssml_bad_error}")
endif()

file(WRITE "${NVV_DIR}/custom.nvv" "nvv snort | test\ns 0.05\na 0.06 1.1 0.8\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --nvv-file "${NVV_DIR}/custom.nvv"
        --text "Hi [snort]" --sample-rate 22050
        --output "${NVV_DIR}/custom.wav"
    RESULT_VARIABLE nvv_custom_result
    ERROR_VARIABLE nvv_custom_error
)
if(NOT nvv_custom_result EQUAL 0)
    message(FATAL_ERROR "custom NVV render failed: ${nvv_custom_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --nvv-file "${NVV_DIR}/custom.nvv" --list-nvv
    RESULT_VARIABLE nvv_custom_list_result
    OUTPUT_VARIABLE nvv_custom_list_output
    ERROR_VARIABLE nvv_custom_list_error
)
if(NOT nvv_custom_list_result EQUAL 0 OR NOT nvv_custom_list_output MATCHES "snort")
    message(FATAL_ERROR "custom NVV not listed: ${nvv_custom_list_error}")
endif()

# #143: breath-group planning is opt-in, data-driven and deterministic.
set(BREATH_DIR "${CMAKE_CURRENT_BINARY_DIR}/breath-smoke")
file(MAKE_DIRECTORY "${BREATH_DIR}")

execute_process(
    COMMAND "${FORMANT_CLI}" --list-breath
    RESULT_VARIABLE breath_list_result
    OUTPUT_VARIABLE breath_list_output
    ERROR_VARIABLE breath_list_error
)
if(NOT breath_list_result EQUAL 0 OR NOT breath_list_output MATCHES "narrative")
    message(FATAL_ERROR "--list-breath failed: ${breath_list_error}")
endif()
if(NOT breath_list_output MATCHES "declination 0.965")
    message(FATAL_ERROR "--list-breath did not report the resolved policy")
endif()

set(BREATH_TEXT "One two three four five six seven eight nine ten eleven twelve, alpha beta gamma delta.")
execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing off
        --sample-rate 22050 --output "${BREATH_DIR}/off.wav"
    RESULT_VARIABLE breath_off_result
    ERROR_VARIABLE breath_off_error
)
execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing silent
        --sample-rate 22050 --output "${BREATH_DIR}/silent.wav"
    RESULT_VARIABLE breath_silent_result
    ERROR_VARIABLE breath_silent_error
)
execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing auto
        --sample-rate 22050 --output "${BREATH_DIR}/auto.wav"
    RESULT_VARIABLE breath_auto_result
    ERROR_VARIABLE breath_auto_error
)
if(NOT breath_off_result EQUAL 0 OR NOT breath_silent_result EQUAL 0 OR
   NOT breath_auto_result EQUAL 0)
    message(FATAL_ERROR "breathing modes failed: ${breath_off_error} ${breath_silent_error} ${breath_auto_error}")
endif()
file(SHA256 "${BREATH_DIR}/off.wav" breath_off_hash)
file(SHA256 "${BREATH_DIR}/silent.wav" breath_silent_hash)
file(SHA256 "${BREATH_DIR}/auto.wav" breath_auto_hash)
if(breath_off_hash STREQUAL breath_silent_hash OR
   breath_off_hash STREQUAL breath_auto_hash OR
   breath_silent_hash STREQUAL breath_auto_hash)
    message(FATAL_ERROR "breathing modes did not change the planned audio")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing auto
        --sample-rate 22050 --output "${BREATH_DIR}/auto2.wav"
    RESULT_VARIABLE breath_again_result
    ERROR_VARIABLE breath_again_error
)
file(SHA256 "${BREATH_DIR}/auto2.wav" breath_auto2_hash)
if(NOT breath_again_result EQUAL 0 OR NOT breath_auto_hash STREQUAL breath_auto2_hash)
    message(FATAL_ERROR "breath planning is not deterministic: ${breath_again_error}")
endif()

# An author-placed NVV breath is preserved, not duplicated by the planner.
execute_process(
    COMMAND "${FORMANT_CLI}"
        --text "One two three four five six seven eight nine ten eleven twelve [breath-in] alpha beta gamma delta."
        --breathing auto --sample-rate 22050
        --output "${BREATH_DIR}/explicit.wav"
    RESULT_VARIABLE breath_explicit_result
    OUTPUT_VARIABLE breath_explicit_status
    ERROR_VARIABLE breath_explicit_error
)
if(NOT breath_explicit_result EQUAL 0)
    message(FATAL_ERROR "explicit breath render failed: ${breath_explicit_error}")
endif()
if(NOT breath_explicit_status MATCHES "0 breath\\(es\\)")
    message(FATAL_ERROR "explicit breath was duplicated: ${breath_explicit_status} ${breath_explicit_error}")
endif()

file(WRITE "${BREATH_DIR}/custom.txt" "language default\n  max-words 16\nstyle narrative\n  depth-long 1.30\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing auto
        --breath-style narrative --breath-file "${BREATH_DIR}/custom.txt"
        --sample-rate 22050 --output "${BREATH_DIR}/custom.wav"
    RESULT_VARIABLE breath_custom_result
    ERROR_VARIABLE breath_custom_error
)
if(NOT breath_custom_result EQUAL 0)
    message(FATAL_ERROR "custom breath policy failed: ${breath_custom_error}")
endif()

file(WRITE "${BREATH_DIR}/bad.txt" "language default\n  wobble 3\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing auto
        --breath-file "${BREATH_DIR}/bad.txt" --output "${BREATH_DIR}/bad.wav"
    RESULT_VARIABLE breath_bad_result
    ERROR_VARIABLE breath_bad_error
)
if(breath_bad_result EQUAL 0 OR NOT breath_bad_error MATCHES "breath line 2")
    message(FATAL_ERROR "malformed breath policy was not rejected: ${breath_bad_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing loud
        --output "${BREATH_DIR}/bad.wav"
    RESULT_VARIABLE breath_mode_result
    ERROR_VARIABLE breath_mode_error
)
if(breath_mode_result EQUAL 0 OR NOT breath_mode_error MATCHES "invalid breathing mode 'loud'")
    message(FATAL_ERROR "invalid breathing mode was not rejected: ${breath_mode_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "${BREATH_TEXT}" --breathing auto
        --breath-style manic --output "${BREATH_DIR}/bad.wav"
    RESULT_VARIABLE breath_style_result
    ERROR_VARIABLE breath_style_error
)
if(breath_style_result EQUAL 0 OR NOT breath_style_error MATCHES "unknown breath style: 'manic'")
    message(FATAL_ERROR "unknown breath style was not rejected: ${breath_style_error}")
endif()

# #177: the growing neural layer is opt-in, deterministic and reloadable.
set(NEURAL_DIR "${CMAKE_CURRENT_BINARY_DIR}/neural-smoke")
file(MAKE_DIRECTORY "${NEURAL_DIR}")
file(WRITE "${NEURAL_DIR}/samples.txt"
    "sample 0 0 0 0 1.2 1.3\n# one learned correction\n")
file(WRITE "${NEURAL_DIR}/bad-samples.txt"
    "sample 0 0 0 0 1.2 1.3\nsample nope 0 0 0 1.1 1.1\n")

execute_process(
    COMMAND "${FORMANT_CLI}" --neural-train "${NEURAL_DIR}/samples.txt"
        --neural-save "${NEURAL_DIR}/model1.state" --list-neural
    RESULT_VARIABLE neural_train_result
    OUTPUT_VARIABLE neural_train_status
    ERROR_VARIABLE neural_train_error
)
if(NOT neural_train_result EQUAL 0)
    message(FATAL_ERROR "neural training failed: ${neural_train_error}")
endif()
if(NOT neural_train_status MATCHES "units 1")
    message(FATAL_ERROR "neural training did not report one unit: ${neural_train_status} ${neural_train_error}")
endif()

file(READ "${NEURAL_DIR}/model1.state" neural_state)
if(NOT neural_state MATCHES "^formant-neural 1")
    message(FATAL_ERROR "neural state has no version header")
endif()
if(NOT neural_state MATCHES "unit 0 ")
    message(FATAL_ERROR "neural state has no trained unit")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --neural-train "${NEURAL_DIR}/samples.txt"
        --neural-save "${NEURAL_DIR}/model2.state" --list-neural
    RESULT_VARIABLE neural_repeat_result
    ERROR_VARIABLE neural_repeat_error
)
if(NOT neural_repeat_result EQUAL 0)
    message(FATAL_ERROR "repeated neural training failed: ${neural_repeat_error}")
endif()
file(SHA256 "${NEURAL_DIR}/model1.state" neural_hash1)
file(SHA256 "${NEURAL_DIR}/model2.state" neural_hash2)
if(NOT neural_hash1 STREQUAL neural_hash2)
    message(FATAL_ERROR "neural training is not deterministic")
endif()

file(WRITE "${NEURAL_DIR}/later.txt"
    "sample 1 1 1 1 0.8 1.1\nsample 0 1 0 1 1.3 0.9\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --neural-model "${NEURAL_DIR}/model1.state"
        --neural-train "${NEURAL_DIR}/later.txt"
        --neural-save "${NEURAL_DIR}/continued1.state" --list-neural
    RESULT_VARIABLE neural_continue_result
    ERROR_VARIABLE neural_continue_error
)
if(NOT neural_continue_result EQUAL 0)
    message(FATAL_ERROR "continued neural training failed: ${neural_continue_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --neural-model "${NEURAL_DIR}/model1.state"
        --neural-train "${NEURAL_DIR}/later.txt"
        --neural-save "${NEURAL_DIR}/continued2.state" --list-neural
    RESULT_VARIABLE neural_replay_result
    ERROR_VARIABLE neural_replay_error
)
if(NOT neural_replay_result EQUAL 0)
    message(FATAL_ERROR "replayed neural training failed: ${neural_replay_error}")
endif()
file(SHA256 "${NEURAL_DIR}/continued1.state" neural_continue_hash1)
file(SHA256 "${NEURAL_DIR}/continued2.state" neural_continue_hash2)
if(NOT neural_continue_hash1 STREQUAL neural_continue_hash2)
    message(FATAL_ERROR "continuing from a saved neural state is not deterministic")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[h ee l o]" --sample-rate 22050
        --output "${NEURAL_DIR}/plain.wav"
    RESULT_VARIABLE neural_plain_result
    ERROR_VARIABLE neural_plain_error
)
if(NOT neural_plain_result EQUAL 0)
    message(FATAL_ERROR "plain neural comparison render failed: ${neural_plain_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[h ee l o]" --sample-rate 22050
        --neural-model "${NEURAL_DIR}/model1.state" --neural-apply
        --output "${NEURAL_DIR}/learned.wav"
    RESULT_VARIABLE neural_apply_result
    ERROR_VARIABLE neural_apply_error
)
if(NOT neural_apply_result EQUAL 0)
    message(FATAL_ERROR "neural apply render failed: ${neural_apply_error}")
endif()
file(SHA256 "${NEURAL_DIR}/plain.wav" neural_plain_hash)
file(SHA256 "${NEURAL_DIR}/learned.wav" neural_learned_hash)
if(neural_plain_hash STREQUAL neural_learned_hash)
    message(FATAL_ERROR "applying a trained neural model did not change the audio")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --neural-train "${NEURAL_DIR}/bad-samples.txt"
        --list-neural
    RESULT_VARIABLE neural_bad_samples_result
    ERROR_VARIABLE neural_bad_samples_error
)
if(neural_bad_samples_result EQUAL 0 OR NOT neural_bad_samples_error MATCHES "neural line 2")
    message(FATAL_ERROR "malformed neural samples were not rejected with a line number: ${neural_bad_samples_error}")
endif()

file(WRITE "${NEURAL_DIR}/bad.state" "${neural_state}bogus\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --neural-model "${NEURAL_DIR}/bad.state"
        --list-neural
    RESULT_VARIABLE neural_bad_state_result
    ERROR_VARIABLE neural_bad_state_error
)
if(neural_bad_state_result EQUAL 0 OR NOT neural_bad_state_error MATCHES "neural line 1[0-9]")
    message(FATAL_ERROR "malformed neural state was not rejected with a line number: ${neural_bad_state_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --neural-apply --output "${NEURAL_DIR}/unused.wav"
    RESULT_VARIABLE neural_orphan_result
    ERROR_VARIABLE neural_orphan_error
)
if(neural_orphan_result EQUAL 0 OR NOT neural_orphan_error MATCHES "requires --neural-model or --neural-train")
    message(FATAL_ERROR "orphan --neural-apply was not rejected: ${neural_orphan_error}")
endif()

# #125: affect styles are versioned data, opt-in and deterministic. The
# built-in table ships with the binary; user files layer over it and checksums
# protect shared packages.
set(AFFECT_DIR "${CMAKE_CURRENT_BINARY_DIR}/affect-smoke")
file(MAKE_DIRECTORY "${AFFECT_DIR}")

execute_process(
    COMMAND "${FORMANT_CLI}" --list-styles
    RESULT_VARIABLE affect_list_result
    OUTPUT_VARIABLE affect_list_output
    ERROR_VARIABLE affect_list_error
)
if(NOT affect_list_result EQUAL 0
        OR NOT affect_list_output MATCHES "schema.1"
        OR NOT affect_list_output MATCHES "excited"
        OR NOT affect_list_output MATCHES "whispering"
        OR NOT affect_list_output MATCHES "Emphasis")
    message(FATAL_ERROR "--list-styles did not report the built-in table: ${affect_list_output} ${affect_list_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --list-voice-parameters
    RESULT_VARIABLE affect_parameters_result
    OUTPUT_VARIABLE affect_parameters_output
    ERROR_VARIABLE affect_parameters_error
)
if(NOT affect_parameters_result EQUAL 0
        OR NOT affect_parameters_output MATCHES "jitter-cents"
        OR NOT affect_parameters_output MATCHES "shimmer")
    message(FATAL_ERROR "affect voice parameters are missing: ${affect_parameters_output} ${affect_parameters_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --dump-style excited
    RESULT_VARIABLE affect_dump_result
    OUTPUT_VARIABLE affect_dump
    ERROR_VARIABLE affect_dump_error
)
if(NOT affect_dump_result EQUAL 0 OR NOT affect_dump MATCHES "schema 1")
    message(FATAL_ERROR "--dump-style failed: ${affect_dump_error}")
endif()
if(NOT affect_dump MATCHES "style excited"
        OR NOT affect_dump MATCHES "checksum [0-9a-f]+")
    message(FATAL_ERROR "exported style is not versioned or checksummed: ${affect_dump}")
endif()
file(WRITE "${AFFECT_DIR}/dumped.affect" "${affect_dump}")

execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-style "${AFFECT_DIR}/dumped.affect"
    RESULT_VARIABLE dumped_inspect_result
    OUTPUT_VARIABLE dumped_inspect
    ERROR_VARIABLE dumped_inspect_error
)
if(NOT dumped_inspect_result EQUAL 0 OR NOT dumped_inspect MATCHES "status.valid")
    message(FATAL_ERROR "generated affect checksum did not validate: ${dumped_inspect} ${dumped_inspect_error}")
endif()

file(READ "${AFFECT_DIR}/dumped.affect" tampered_affect)
file(WRITE "${AFFECT_DIR}/tampered.affect" "${tampered_affect}# tampered comment\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-style "${AFFECT_DIR}/tampered.affect"
    RESULT_VARIABLE tampered_inspect_result
    OUTPUT_VARIABLE tampered_inspect
    ERROR_VARIABLE tampered_inspect_error
)
if(NOT tampered_inspect_result EQUAL 0 OR NOT tampered_inspect MATCHES "status.invalid")
    message(FATAL_ERROR "tampered affect file was not detected: ${tampered_inspect} ${tampered_inspect_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --style-file "${AFFECT_DIR}/tampered.affect"
        --style excited --text "Hello." --output "${AFFECT_DIR}/tampered.wav"
    RESULT_VARIABLE tampered_render_result
    ERROR_VARIABLE tampered_render_error
)
if(tampered_render_result EQUAL 0 OR NOT tampered_render_error MATCHES "affect checksum mismatch")
    message(FATAL_ERROR "tampered affect checksum was not enforced: ${tampered_render_error}")
endif()

set(BRIGHT_STYLE "${FORMANT_EXAMPLES_DIR}/affect/bright.affect")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-style "${BRIGHT_STYLE}"
    RESULT_VARIABLE bright_inspect_result
    OUTPUT_VARIABLE bright_inspect
    ERROR_VARIABLE bright_inspect_error
)
if(NOT bright_inspect_result EQUAL 0
        OR NOT bright_inspect MATCHES "name.Bright"
        OR NOT bright_inspect MATCHES "status.valid")
    message(FATAL_ERROR "shipped example style is invalid: ${bright_inspect} ${bright_inspect_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --list-styles --style-file "${BRIGHT_STYLE}"
    RESULT_VARIABLE bright_list_result
    OUTPUT_VARIABLE bright_list_output
    ERROR_VARIABLE bright_list_error
)
if(NOT bright_list_result EQUAL 0 OR NOT bright_list_output MATCHES "bright")
    message(FATAL_ERROR "shipped example style did not layer over the built-ins: ${bright_list_output} ${bright_list_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "I really like this." --sample-rate 22050
        --output "${AFFECT_DIR}/plain.wav"
    RESULT_VARIABLE affect_plain_result
    ERROR_VARIABLE affect_plain_error
)
if(NOT affect_plain_result EQUAL 0)
    message(FATAL_ERROR "plain affect comparison render failed: ${affect_plain_error}")
endif()
foreach(affect_pass 1 2)
    execute_process(
        COMMAND "${FORMANT_CLI}" --text "I really like this." --sample-rate 22050
            --style excited --output "${AFFECT_DIR}/excited${affect_pass}.wav"
        RESULT_VARIABLE affect_render_result
        ERROR_VARIABLE affect_render_error
    )
    if(NOT affect_render_result EQUAL 0)
        message(FATAL_ERROR "styled render failed: ${affect_render_error}")
    endif()
endforeach()
file(SHA256 "${AFFECT_DIR}/plain.wav" affect_plain_hash)
file(SHA256 "${AFFECT_DIR}/excited1.wav" affect_excited_hash1)
file(SHA256 "${AFFECT_DIR}/excited2.wav" affect_excited_hash2)
if(affect_plain_hash STREQUAL affect_excited_hash1)
    message(FATAL_ERROR "applying an affect style did not change the audio")
endif()
if(NOT affect_excited_hash1 STREQUAL affect_excited_hash2)
    message(FATAL_ERROR "affect styling is not deterministic")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "I really like this." --sample-rate 22050
        --arousal 0.8 --valence 0.7 --dominance 0.5
        --output "${AFFECT_DIR}/coordinates.wav"
    RESULT_VARIABLE affect_coordinates_result
    ERROR_VARIABLE affect_coordinates_error
)
if(NOT affect_coordinates_result EQUAL 0)
    message(FATAL_ERROR "continuous affect render failed: ${affect_coordinates_error}")
endif()
file(SHA256 "${AFFECT_DIR}/coordinates.wav" affect_coordinates_hash)
if(NOT affect_coordinates_hash STREQUAL affect_excited_hash1)
    message(FATAL_ERROR "PAD coordinates matching a named preset did not resolve identically")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --pitch 200 --style excited
        --text "I really like this." --sample-rate 22050
        --output "${AFFECT_DIR}/flag-first.wav"
    RESULT_VARIABLE affect_flag_first_result
    ERROR_VARIABLE affect_flag_first_error
)
execute_process(
    COMMAND "${FORMANT_CLI}" --style excited --pitch 200
        --text "I really like this." --sample-rate 22050
        --output "${AFFECT_DIR}/style-first.wav"
    RESULT_VARIABLE affect_style_first_result
    ERROR_VARIABLE affect_style_first_error
)
if(NOT affect_flag_first_result EQUAL 0 OR NOT affect_style_first_result EQUAL 0)
    message(FATAL_ERROR "flag-order comparison render failed: ${affect_flag_first_error} ${affect_style_first_error}")
endif()
file(SHA256 "${AFFECT_DIR}/flag-first.wav" affect_flag_first_hash)
file(SHA256 "${AFFECT_DIR}/style-first.wav" affect_style_first_hash)
if(NOT affect_flag_first_hash STREQUAL affect_style_first_hash)
    message(FATAL_ERROR "explicit voice flags are not replayed after --style")
endif()

file(WRITE "${AFFECT_DIR}/plain.ssml" "<speak>I really like this.</speak>\n")
file(WRITE "${AFFECT_DIR}/emphasis.ssml"
    "<speak>I <emphasis level=\"strong\">really</emphasis> like this.</speak>\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --ssml-file "${AFFECT_DIR}/plain.ssml"
        --sample-rate 22050 --output "${AFFECT_DIR}/plain-ssml.wav"
    RESULT_VARIABLE emphasis_plain_result
    ERROR_VARIABLE emphasis_plain_error
)
execute_process(
    COMMAND "${FORMANT_CLI}" --ssml-file "${AFFECT_DIR}/emphasis.ssml"
        --sample-rate 22050 --output "${AFFECT_DIR}/emphasis.wav"
    RESULT_VARIABLE emphasis_result
    ERROR_VARIABLE emphasis_error
)
if(NOT emphasis_plain_result EQUAL 0 OR NOT emphasis_result EQUAL 0)
    message(FATAL_ERROR "SSML emphasis comparison render failed: ${emphasis_plain_error} ${emphasis_error}")
endif()
file(SHA256 "${AFFECT_DIR}/plain-ssml.wav" emphasis_plain_hash)
file(SHA256 "${AFFECT_DIR}/emphasis.wav" emphasis_hash)
if(emphasis_plain_hash STREQUAL emphasis_hash)
    message(FATAL_ERROR "<emphasis> did not change the rendered audio")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --text "Extreme." --sample-rate 22050
        --arousal 1 --valence -1 --dominance 1
        --gain 4 --jitter 50 --shimmer 0.25
        --output "${AFFECT_DIR}/extreme.wav"
    RESULT_VARIABLE affect_extreme_result
    ERROR_VARIABLE affect_extreme_error
)
if(NOT affect_extreme_result EQUAL 0)
    message(FATAL_ERROR "extreme affect render failed: ${affect_extreme_error}")
endif()
file(SIZE "${AFFECT_DIR}/extreme.wav" affect_extreme_size)
if(affect_extreme_size LESS 44)
    message(FATAL_ERROR "extreme affect render produced no audio")
endif()

# Issue #123: opt-in singing mode is data-driven, deterministic and additive.
execute_process(
    COMMAND "${FORMANT_CLI}" --list-songs
    RESULT_VARIABLE list_songs_result
    OUTPUT_VARIABLE list_songs_output
    ERROR_VARIABLE list_songs_error
)
if(NOT list_songs_result EQUAL 0 OR NOT list_songs_output MATCHES "twinkle")
    message(FATAL_ERROR "formant --list-songs failed: ${list_songs_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --list-scales
    RESULT_VARIABLE list_scales_result
    OUTPUT_VARIABLE list_scales_output
    ERROR_VARIABLE list_scales_error
)
if(NOT list_scales_result EQUAL 0
   OR NOT list_scales_output MATCHES "chromatic"
   OR NOT list_scales_output MATCHES "quarter")
    message(FATAL_ERROR "formant --list-scales failed: ${list_scales_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --dump-song twinkle
    RESULT_VARIABLE dump_song_result
    OUTPUT_VARIABLE dump_song_output
    ERROR_VARIABLE dump_song_error
)
if(NOT dump_song_result EQUAL 0 OR NOT dump_song_output MATCHES "schema 1")
    message(FATAL_ERROR "formant --dump-song failed: ${dump_song_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-song "${FORMANT_SOURCE_DIR}/data/songs/twinkle.song"
    RESULT_VARIABLE inspect_song_result
    OUTPUT_VARIABLE inspect_song_output
    ERROR_VARIABLE inspect_song_error
)
if(NOT inspect_song_result EQUAL 0 OR NOT inspect_song_output MATCHES "status\tvalid")
    message(FATAL_ERROR "formant --inspect-song failed: ${inspect_song_error}")
endif()

set(SONG_DIR "${FORMANT_OUTPUT_DIR}/song")
file(MAKE_DIRECTORY "${SONG_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --song twinkle --sample-rate 22050
        --output "${SONG_DIR}/twinkle.wav"
    RESULT_VARIABLE song_result
    ERROR_VARIABLE song_error
)
if(NOT song_result EQUAL 0)
    message(FATAL_ERROR "built-in song render failed: ${song_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --song twinkle --sample-rate 22050
        --output "${SONG_DIR}/twinkle-again.wav"
    RESULT_VARIABLE song_again_result
    ERROR_VARIABLE song_again_error
)
if(NOT song_again_result EQUAL 0)
    message(FATAL_ERROR "second built-in song render failed: ${song_again_error}")
endif()
file(SHA256 "${SONG_DIR}/twinkle.wav" song_hash)
file(SHA256 "${SONG_DIR}/twinkle-again.wav" song_again_hash)
if(NOT song_hash STREQUAL song_again_hash)
    message(FATAL_ERROR "song rendering is not deterministic")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --song-file "${FORMANT_SOURCE_DIR}/data/songs/twinkle.song"
        --sample-rate 22050 --output "${SONG_DIR}/file.wav"
    RESULT_VARIABLE song_file_result
    ERROR_VARIABLE song_file_error
)
if(NOT song_file_result EQUAL 0)
    message(FATAL_ERROR "song file render failed: ${song_file_error}")
endif()
file(SHA256 "${SONG_DIR}/file.wav" song_file_hash)
if(NOT song_file_hash STREQUAL song_hash)
    message(FATAL_ERROR "built-in and file songs differ")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --song twinkle --a4 432 --sample-rate 22050
        --output "${SONG_DIR}/retuned.wav"
    RESULT_VARIABLE a4_result
    ERROR_VARIABLE a4_error
)
if(NOT a4_result EQUAL 0)
    message(FATAL_ERROR "A4 override render failed: ${a4_error}")
endif()
file(SHA256 "${SONG_DIR}/retuned.wav" retuned_hash)
if(retuned_hash STREQUAL song_hash)
    message(FATAL_ERROR "--a4 did not change the song pitch")
endif()
file(WRITE "${SONG_DIR}/scale-demo.song"
    "schema 1\nname scale-demo\na4 440\nscale chromatic\nnote degree:1@4 0.30 ah\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --song-file "${SONG_DIR}/scale-demo.song"
        --scale quarter --sample-rate 22050
        --output "${SONG_DIR}/quarter.wav"
    RESULT_VARIABLE quarter_result
    ERROR_VARIABLE quarter_error
)
if(NOT quarter_result EQUAL 0)
    message(FATAL_ERROR "quarter-tone scale render failed: ${quarter_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --song-file "${SONG_DIR}/scale-demo.song"
        --scale chromatic --sample-rate 22050
        --output "${SONG_DIR}/chromatic-scale.wav"
    RESULT_VARIABLE chromatic_scale_result
    ERROR_VARIABLE chromatic_scale_error
)
if(NOT chromatic_scale_result EQUAL 0)
    message(FATAL_ERROR "chromatic scale render failed: ${chromatic_scale_error}")
endif()
file(SHA256 "${SONG_DIR}/quarter.wav" quarter_hash)
file(SHA256 "${SONG_DIR}/chromatic-scale.wav" chromatic_scale_hash)
if(quarter_hash STREQUAL chromatic_scale_hash)
    message(FATAL_ERROR "--scale quarter resolved to the chromatic pitches")
endif()

file(WRITE "${SONG_DIR}/detuned.scl"
    "schema 1\nname detuned\nperiod 1200\nreference 900\ndegree C 0\ndegree C# 80\ndegree D 200\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --dump-scale quarter
    RESULT_VARIABLE dump_scale_result
    OUTPUT_VARIABLE dump_scale_output
    ERROR_VARIABLE dump_scale_error
)
if(NOT dump_scale_result EQUAL 0 OR NOT dump_scale_output MATCHES "schema 1")
    message(FATAL_ERROR "formant --dump-scale failed: ${dump_scale_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-scale "${FORMANT_SOURCE_DIR}/data/scales/quarter.scl"
    RESULT_VARIABLE inspect_scale_result
    OUTPUT_VARIABLE inspect_scale_output
    ERROR_VARIABLE inspect_scale_error
)
if(NOT inspect_scale_result EQUAL 0 OR NOT inspect_scale_output MATCHES "status\tvalid")
    message(FATAL_ERROR "formant --inspect-scale failed: ${inspect_scale_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --song-file "${SONG_DIR}/scale-demo.song"
        --scale-file "${SONG_DIR}/detuned.scl" --sample-rate 22050
        --output "${SONG_DIR}/detuned.wav"
    RESULT_VARIABLE detuned_result
    ERROR_VARIABLE detuned_error
)
if(NOT detuned_result EQUAL 0)
    message(FATAL_ERROR "--scale-file render failed: ${detuned_error}")
endif()
file(SHA256 "${SONG_DIR}/detuned.wav" detuned_hash)
if(detuned_hash STREQUAL chromatic_scale_hash)
    message(FATAL_ERROR "--scale-file did not change the resolved pitch")
endif()

foreach(bad_args "--song;twinkle;--text;nope" "--song;twinkle;--song-file;x" "--a4;432" "--scale;quarter")
    separate_arguments(bad_command UNIX_COMMAND "${bad_args}")
    execute_process(
        COMMAND "${FORMANT_CLI}" ${bad_command} --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE song_conflict_result
        ERROR_VARIABLE song_conflict_error
    )
    if(song_conflict_result EQUAL 0)
        message(FATAL_ERROR "formant accepted invalid song options: ${bad_args}")
    endif()
endforeach()

# ---- #101: data-defined frication envelopes -------------------------------
set(FRICATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/frication-smoke")
file(MAKE_DIRECTORY "${FRICATION_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --list-frication
    RESULT_VARIABLE list_frication_result
    OUTPUT_VARIABLE list_frication_output
    ERROR_VARIABLE list_frication_error
)
if(NOT list_frication_result EQUAL 0 OR NOT list_frication_output MATCHES "Frication profiles")
    message(FATAL_ERROR "formant --list-frication failed: ${list_frication_error}")
endif()
foreach(profile s sh f)
    if(NOT list_frication_output MATCHES "  ${profile}\t")
        message(FATAL_ERROR "--list-frication omitted profile ${profile}")
    endif()
endforeach()
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-frication "${FORMANT_SOURCE_DIR}/data/frication/frication_profiles.txt"
    RESULT_VARIABLE inspect_frication_result
    OUTPUT_VARIABLE inspect_frication_output
    ERROR_VARIABLE inspect_frication_error
)
if(NOT inspect_frication_result EQUAL 0 OR NOT inspect_frication_output MATCHES "status\tvalid")
    message(FATAL_ERROR "formant --inspect-frication failed: ${inspect_frication_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --dump-frication s
    RESULT_VARIABLE dump_frication_result
    OUTPUT_VARIABLE dump_frication_output
    ERROR_VARIABLE dump_frication_error
)
if(NOT dump_frication_result EQUAL 0 OR NOT dump_frication_output MATCHES "profile s ")
    message(FATAL_ERROR "formant --dump-frication failed: ${dump_frication_error}")
endif()
file(WRITE "${FRICATION_DIR}/dumped.frication" "${dump_frication_output}")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-frication "${FRICATION_DIR}/dumped.frication"
    RESULT_VARIABLE dumped_inspect_result
    OUTPUT_VARIABLE dumped_inspect_output
    ERROR_VARIABLE dumped_inspect_error
)
if(NOT dumped_inspect_result EQUAL 0 OR NOT dumped_inspect_output MATCHES "status\tvalid")
    message(FATAL_ERROR "dumped frication table failed checksum: ${dumped_inspect_error}")
endif()
file(READ "${FORMANT_SOURCE_DIR}/data/frication/frication_profiles.txt" frication_builtin)
file(WRITE "${FRICATION_DIR}/tampered.frication" "${frication_builtin}\n# tamper\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[s ee]" --frication-file "${FRICATION_DIR}/tampered.frication"
        --output "${FRICATION_DIR}/tampered.wav"
    RESULT_VARIABLE frication_tamper_result
    ERROR_VARIABLE frication_tamper_error
)
if(frication_tamper_result EQUAL 0 OR NOT frication_tamper_error MATCHES "checksum mismatch")
    message(FATAL_ERROR "frication checksum mismatch was not reported")
endif()
foreach(profile s sh f)
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[${profile} ee]" --frication-file
            "${FORMANT_SOURCE_DIR}/data/frication/frication_profiles.txt"
            --format raw16 --output "${FRICATION_DIR}/${profile}.raw"
        RESULT_VARIABLE shaped_result
        ERROR_VARIABLE shaped_error
    )
    if(NOT shaped_result EQUAL 0)
        message(FATAL_ERROR "--frication-file render failed: ${shaped_error}")
    endif()
    file(SHA256 "${FRICATION_DIR}/${profile}.raw" shaped_${profile}_hash)
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[${profile} ee]" --frication-file
            "${FORMANT_SOURCE_DIR}/data/frication/frication_profiles.txt"
            --format raw16 --output "${FRICATION_DIR}/${profile}-again.raw"
        RESULT_VARIABLE shaped_again_result
    )
    if(NOT shaped_again_result EQUAL 0)
        message(FATAL_ERROR "repeat --frication-file render failed")
    endif()
    file(SHA256 "${FRICATION_DIR}/${profile}-again.raw" shaped_again_hash)
    if(NOT shaped_${profile}_hash STREQUAL shaped_again_hash)
        message(FATAL_ERROR "--frication render is not deterministic for ${profile}")
    endif()
endforeach()
if(shaped_s_hash STREQUAL shaped_sh_hash OR shaped_s_hash STREQUAL shaped_f_hash
        OR shaped_sh_hash STREQUAL shaped_f_hash)
    message(FATAL_ERROR "frication profiles did not produce distinguishable output")
endif()
foreach(bad_args "--frication;s;--frication-file;${FORMANT_SOURCE_DIR}/data/frication/frication_profiles.txt" "--frication;bogus")
    separate_arguments(bad_command UNIX_COMMAND "${bad_args}")
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[s ee]" ${bad_command} --output "${FORMANT_OUTPUT}"
        RESULT_VARIABLE frication_conflict_result
        ERROR_VARIABLE frication_conflict_error
    )
    if(frication_conflict_result EQUAL 0)
        message(FATAL_ERROR "formant accepted invalid frication options: ${bad_args}")
    endif()
endforeach()

# ---- #140: overtone and subharmonic register data -------------------------
set(REGISTER_DIR "${CMAKE_CURRENT_BINARY_DIR}/register-smoke")
file(MAKE_DIRECTORY "${REGISTER_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --list-registers
    RESULT_VARIABLE list_registers_result
    OUTPUT_VARIABLE list_registers_output
    ERROR_VARIABLE list_registers_error
)
if(NOT list_registers_result EQUAL 0 OR NOT list_registers_output MATCHES "Registers \\(2\\)")
    message(FATAL_ERROR "formant --list-registers failed: ${list_registers_error}")
endif()
foreach(register_name sygyt kargyraa)
    if(NOT list_registers_output MATCHES "  ${register_name}\t")
        message(FATAL_ERROR "--list-registers omitted ${register_name}")
    endif()
endforeach()
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-register "${FORMANT_SOURCE_DIR}/data/registers/register_presets.txt"
    RESULT_VARIABLE inspect_register_result
    OUTPUT_VARIABLE inspect_register_output
    ERROR_VARIABLE inspect_register_error
)
if(NOT inspect_register_result EQUAL 0 OR NOT inspect_register_output MATCHES "status\tvalid")
    message(FATAL_ERROR "formant --inspect-register failed: ${inspect_register_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --dump-register sygyt
    RESULT_VARIABLE dump_register_result
    OUTPUT_VARIABLE dump_register_output
    ERROR_VARIABLE dump_register_error
)
if(NOT dump_register_result EQUAL 0 OR NOT dump_register_output MATCHES "register sygyt ")
    message(FATAL_ERROR "formant --dump-register failed: ${dump_register_error}")
endif()
file(WRITE "${REGISTER_DIR}/dumped.register" "${dump_register_output}")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-register "${REGISTER_DIR}/dumped.register"
    RESULT_VARIABLE dumped_inspect_result
    OUTPUT_VARIABLE dumped_inspect_output
    ERROR_VARIABLE dumped_inspect_error
)
if(NOT dumped_inspect_result EQUAL 0 OR NOT dumped_inspect_output MATCHES "status\tvalid")
    message(FATAL_ERROR "dumped register table failed checksum: ${dumped_inspect_error}")
endif()

file(READ "${FORMANT_SOURCE_DIR}/data/registers/register_presets.txt" register_builtin)
file(WRITE "${REGISTER_DIR}/tampered.register" "${register_builtin}\n# tamper\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[ee]" --register-file "${REGISTER_DIR}/tampered.register"
        --output "${REGISTER_DIR}/tampered.wav"
    RESULT_VARIABLE register_tamper_result
    ERROR_VARIABLE register_tamper_error
)
if(register_tamper_result EQUAL 0 OR NOT register_tamper_error MATCHES "checksum mismatch")
    message(FATAL_ERROR "register checksum mismatch was not reported")
endif()

# A dumped preset is a shareable single-preset file. --register-file applies
# it directly; --register NAME remains the built-in/layered table selector.
foreach(case_name builtin file)
    if(case_name STREQUAL "builtin")
        set(select_args --register sygyt)
    else()
        set(select_args --register-file "${REGISTER_DIR}/dumped.register")
    endif()
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[ee]" ${select_args}
            --format raw16 --output "${REGISTER_DIR}/${case_name}.raw"
        RESULT_VARIABLE register_render_result
        ERROR_VARIABLE register_render_error
    )
    if(NOT register_render_result EQUAL 0)
        message(FATAL_ERROR "register render failed for ${case_name}: ${register_render_error}")
    endif()
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[ee]" ${select_args}
            --format raw16 --output "${REGISTER_DIR}/${case_name}-again.raw"
        RESULT_VARIABLE register_repeat_result
    )
    if(NOT register_repeat_result EQUAL 0)
        message(FATAL_ERROR "repeat register render failed for ${case_name}")
    endif()
    file(SHA256 "${REGISTER_DIR}/${case_name}.raw" register_${case_name}_hash)
    file(SHA256 "${REGISTER_DIR}/${case_name}-again.raw" register_again_hash)
    if(NOT register_${case_name}_hash STREQUAL register_again_hash)
        message(FATAL_ERROR "register render is not deterministic for ${case_name}")
    endif()
endforeach()
if(NOT register_builtin_hash STREQUAL register_file_hash)
    message(FATAL_ERROR "a dumped register preset did not match --register")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[ee]" --output "${REGISTER_DIR}/plain.raw"
        --format raw16
    RESULT_VARIABLE register_plain_result
)
if(NOT register_plain_result EQUAL 0)
    message(FATAL_ERROR "plain register comparison render failed")
endif()
file(SHA256 "${REGISTER_DIR}/plain.raw" register_plain_hash)
if(register_plain_hash STREQUAL register_builtin_hash)
    message(FATAL_ERROR "--register sygyt did not change the audio")
endif()

# A user file can retune a built-in name without code changes or a rebuild.
file(WRITE "${REGISTER_DIR}/retune-body.register"
    "schema 1\nname unit-overlay\nregister sygyt 8 9 40 0 0\n")
file(SHA256 "${REGISTER_DIR}/retune-body.register" retune_digest)
file(WRITE "${REGISTER_DIR}/retune.register"
    "schema 1\nname unit-overlay\nregister sygyt 8 9 40 0 0\nchecksum ${retune_digest}\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --register-file "${REGISTER_DIR}/retune.register"
        --dump-register sygyt
    RESULT_VARIABLE retuned_dump_result
    OUTPUT_VARIABLE retuned_dump_output
    ERROR_VARIABLE retuned_dump_error
)
if(NOT retuned_dump_result EQUAL 0 OR NOT retuned_dump_output MATCHES "register sygyt 8 ")
    message(FATAL_ERROR "register overlay did not retune the built-in: ${retuned_dump_error}")
endif()

# A shareable file describes exactly one preset; conflicts and unknowns fail.
file(WRITE "${REGISTER_DIR}/two-body.register"
    "schema 1\nname two\nregister one 6 12 20 0 0\nregister two 0 1 120 0.5 28\n")
file(SHA256 "${REGISTER_DIR}/two-body.register" two_digest)
file(WRITE "${REGISTER_DIR}/two.register"
    "schema 1\nname two\nregister one 6 12 20 0 0\nregister two 0 1 120 0.5 28\nchecksum ${two_digest}\n")
execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[ee]" --register-file "${REGISTER_DIR}/two.register"
        --output "${REGISTER_DIR}/two.wav"
    RESULT_VARIABLE register_two_result
    ERROR_VARIABLE register_two_error
)
if(register_two_result EQUAL 0 OR NOT register_two_error MATCHES "exactly one preset")
    message(FATAL_ERROR "register file with two presets was accepted")
endif()
foreach(bad_args "--register;sygyt;--register-file;${REGISTER_DIR}/dumped.register" "--register;bogus")
    separate_arguments(bad_command UNIX_COMMAND "${bad_args}")
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[ee]" ${bad_command} --output "${REGISTER_DIR}/bad.wav"
        RESULT_VARIABLE register_conflict_result
        ERROR_VARIABLE register_conflict_error
    )
    if(register_conflict_result EQUAL 0)
        message(FATAL_ERROR "formant accepted invalid register options: ${bad_args}")
    endif()
endforeach()

# Registers are Voice levers and therefore compose with the singing front end.
execute_process(
    COMMAND "${FORMANT_CLI}" --song twinkle --sample-rate 22050 --format raw16
        --output "${REGISTER_DIR}/song-plain.raw"
    RESULT_VARIABLE register_song_plain_result
    ERROR_VARIABLE register_song_plain_error
)
if(NOT register_song_plain_result EQUAL 0)
    message(FATAL_ERROR "plain song render failed: ${register_song_plain_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --song twinkle --register kargyraa --sample-rate 22050
        --format raw16 --output "${REGISTER_DIR}/song-register.raw"
    RESULT_VARIABLE register_song_result
    ERROR_VARIABLE register_song_error
)
if(NOT register_song_result EQUAL 0)
    message(FATAL_ERROR "song and register did not compose: ${register_song_error}")
endif()
file(SHA256 "${REGISTER_DIR}/song-plain.raw" register_song_plain_hash)
file(SHA256 "${REGISTER_DIR}/song-register.raw" register_song_hash)
if(register_song_plain_hash STREQUAL register_song_hash)
    message(FATAL_ERROR "a register did not change the singing render")
endif()

# ---- #144: rhythm classes as versioned duration data ----------------------
set(RHYTHM_DIR "${CMAKE_CURRENT_BINARY_DIR}/rhythm-smoke")
file(MAKE_DIRECTORY "${RHYTHM_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --list-rhythms
    RESULT_VARIABLE list_rhythms_result
    OUTPUT_VARIABLE list_rhythms_output
    ERROR_VARIABLE list_rhythms_error
)
if(NOT list_rhythms_result EQUAL 0 OR NOT list_rhythms_output MATCHES "Rhythm profiles \\(5\\)")
    message(FATAL_ERROR "formant --list-rhythms failed: ${list_rhythms_error}")
endif()
foreach(rhythm_name stress syllable mora en es)
    if(NOT list_rhythms_output MATCHES "  ${rhythm_name}\t")
        message(FATAL_ERROR "--list-rhythms omitted ${rhythm_name}")
    endif()
endforeach()

execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-rhythm "${FORMANT_SOURCE_DIR}/data/rhythm/rhythm_profiles.txt"
    RESULT_VARIABLE inspect_rhythm_result
    OUTPUT_VARIABLE inspect_rhythm_output
    ERROR_VARIABLE inspect_rhythm_error
)
if(NOT inspect_rhythm_result EQUAL 0 OR NOT inspect_rhythm_output MATCHES "status\tvalid")
    message(FATAL_ERROR "formant --inspect-rhythm failed: ${inspect_rhythm_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --dump-rhythm stress
    RESULT_VARIABLE dump_rhythm_result
    OUTPUT_VARIABLE dump_rhythm_output
    ERROR_VARIABLE dump_rhythm_error
)
if(NOT dump_rhythm_result EQUAL 0 OR NOT dump_rhythm_output MATCHES "rhythm stress stress ")
    message(FATAL_ERROR "formant --dump-rhythm failed: ${dump_rhythm_error}")
endif()
file(WRITE "${RHYTHM_DIR}/dumped.rhythm" "${dump_rhythm_output}")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-rhythm "${RHYTHM_DIR}/dumped.rhythm"
    RESULT_VARIABLE dumped_rhythm_inspect_result
    OUTPUT_VARIABLE dumped_rhythm_inspect_output
    ERROR_VARIABLE dumped_rhythm_inspect_error
)
if(NOT dumped_rhythm_inspect_result EQUAL 0 OR NOT dumped_rhythm_inspect_output MATCHES "status\tvalid")
    message(FATAL_ERROR "dumped rhythm table failed checksum: ${dumped_rhythm_inspect_error}")
endif()

# One profile is shareable; the full built-in table is not a --rhythm-file.
foreach(case_name builtin file)
    if(case_name STREQUAL "builtin")
        set(rhythm_select_args --rhythm stress)
    else()
        set(rhythm_select_args --rhythm-file "${RHYTHM_DIR}/dumped.rhythm")
    endif()
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[ee s ee s ee]" ${rhythm_select_args}
            --format raw16 --output "${RHYTHM_DIR}/${case_name}.raw"
        RESULT_VARIABLE rhythm_render_result
        ERROR_VARIABLE rhythm_render_error
    )
    if(NOT rhythm_render_result EQUAL 0)
        message(FATAL_ERROR "rhythm render failed for ${case_name}: ${rhythm_render_error}")
    endif()
    file(SHA256 "${RHYTHM_DIR}/${case_name}.raw" rhythm_${case_name}_hash)
endforeach()
if(NOT rhythm_builtin_hash STREQUAL rhythm_file_hash)
    message(FATAL_ERROR "--rhythm-file did not reproduce the built-in profile")
endif()

# The same text moved under different named classes, with deterministic bytes.
set(RHYTHM_TEXT "The quick brown fox jumps over the lazy dog.")
foreach(case_name plain stress syllable)
    if(case_name STREQUAL "plain")
        set(rhythm_text_args "")
    else()
        set(rhythm_text_args --rhythm ${case_name})
    endif()
    execute_process(
        COMMAND "${FORMANT_CLI}" --text "${RHYTHM_TEXT}" ${rhythm_text_args}
            --format raw16 --output "${RHYTHM_DIR}/text-${case_name}.raw"
        RESULT_VARIABLE rhythm_text_result
        OUTPUT_VARIABLE rhythm_text_status
        ERROR_VARIABLE rhythm_text_error
    )
    if(NOT rhythm_text_result EQUAL 0)
        message(FATAL_ERROR "rhythm text render failed for ${case_name}: ${rhythm_text_error}")
    endif()
    file(SHA256 "${RHYTHM_DIR}/text-${case_name}.raw" rhythm_text_${case_name}_hash)
    if(NOT case_name STREQUAL "plain" AND NOT rhythm_text_status MATCHES "V nPVI [0-9.]+ -> [0-9.]+")
        message(FATAL_ERROR "rhythm status omitted PVI movement for ${case_name}: ${rhythm_text_status}")
    endif()
endforeach()
if(rhythm_text_plain_hash STREQUAL rhythm_text_stress_hash
        OR rhythm_text_plain_hash STREQUAL rhythm_text_syllable_hash
        OR rhythm_text_stress_hash STREQUAL rhythm_text_syllable_hash)
    message(FATAL_ERROR "rhythm classes did not change the same-text render")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --text "${RHYTHM_TEXT}" --rhythm stress
        --format raw16 --output "${RHYTHM_DIR}/text-stress-again.raw"
    RESULT_VARIABLE rhythm_repeat_result
    ERROR_VARIABLE rhythm_repeat_error
)
if(NOT rhythm_repeat_result EQUAL 0)
    message(FATAL_ERROR "repeat rhythm render failed: ${rhythm_repeat_error}")
endif()
file(SHA256 "${RHYTHM_DIR}/text-stress-again.raw" rhythm_repeat_hash)
if(NOT rhythm_repeat_hash STREQUAL rhythm_text_stress_hash)
    message(FATAL_ERROR "--rhythm render is not deterministic")
endif()

# Built-in language calibrations resolve through --rhythm auto.
foreach(lang_name en es)
    execute_process(
        COMMAND "${FORMANT_CLI}" --lang ${lang_name} --text "${RHYTHM_TEXT}"
            --rhythm auto --format raw16 --output "${RHYTHM_DIR}/auto-${lang_name}.raw"
        RESULT_VARIABLE rhythm_auto_result
        OUTPUT_VARIABLE rhythm_auto_status
        ERROR_VARIABLE rhythm_auto_error
    )
    if(NOT rhythm_auto_result EQUAL 0 OR NOT rhythm_auto_status MATCHES "Applied rhythm '${lang_name}'")
        message(FATAL_ERROR "--rhythm auto did not resolve ${lang_name}: ${rhythm_auto_error}")
    endif()
endforeach()

# Conlang dials live in the language file and take precedence under auto.
set(RHYTHM_LANG_FILE "${RHYTHM_DIR}/rhythm.lang")
file(WRITE "${RHYTHM_LANG_FILE}" [=[name Rhythm Smoke
tag x-rhythm
lenient
rhythm stress
rhythm-vocalic-npvi 52 62
rhythm-intervocalic-rpvi 50 60
rhythm-strength 1
phoneme a
phoneme e
phoneme s
grapheme a a
grapheme e e
grapheme s s
]=])
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${RHYTHM_LANG_FILE}" --text "a s a e s a"
        --rhythm auto --format raw16 --output "${RHYTHM_DIR}/conlang-auto.raw"
    RESULT_VARIABLE conlang_rhythm_result
    OUTPUT_VARIABLE conlang_rhythm_status
    ERROR_VARIABLE conlang_rhythm_error
)
if(NOT conlang_rhythm_result EQUAL 0 OR NOT conlang_rhythm_status MATCHES "Applied rhythm 'x-rhythm'")
    message(FATAL_ERROR "conlang rhythm dials were not applied: ${conlang_rhythm_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --lang-file "${RHYTHM_LANG_FILE}" --text "a s a e s a"
        --format raw16 --output "${RHYTHM_DIR}/conlang-plain.raw"
    RESULT_VARIABLE conlang_plain_result
    ERROR_VARIABLE conlang_plain_error
)
if(NOT conlang_plain_result EQUAL 0)
    message(FATAL_ERROR "plain conlang render failed: ${conlang_plain_error}")
endif()
file(SHA256 "${RHYTHM_DIR}/conlang-auto.raw" conlang_rhythm_hash)
file(SHA256 "${RHYTHM_DIR}/conlang-plain.raw" conlang_plain_hash)
if(conlang_rhythm_hash STREQUAL conlang_plain_hash)
    message(FATAL_ERROR "conlang rhythm dials did not change the render")
endif()

# Checksum tampering, exclusive flags, unknown names and song ownership fail.
file(READ "${FORMANT_SOURCE_DIR}/data/rhythm/rhythm_profiles.txt" rhythm_builtin)
file(WRITE "${RHYTHM_DIR}/tampered.rhythm" "${rhythm_builtin}\n# tamper\n")
foreach(bad_args
        "--rhythm;stress;--rhythm-file;${RHYTHM_DIR}/dumped.rhythm"
        "--rhythm;stress;--rhythm-file;${RHYTHM_DIR}/tampered.rhythm"
        "--rhythm;bogus"
        "--rhythm-file;${FORMANT_SOURCE_DIR}/data/rhythm/rhythm_profiles.txt")
    separate_arguments(bad_command UNIX_COMMAND "${bad_args}")
    execute_process(
        COMMAND "${FORMANT_CLI}" --phonemes "[ee s ee s ee]" ${bad_command}
            --output "${RHYTHM_DIR}/bad.wav"
        RESULT_VARIABLE rhythm_conflict_result
        ERROR_VARIABLE rhythm_conflict_error
    )
    if(rhythm_conflict_result EQUAL 0)
        message(FATAL_ERROR "formant accepted invalid rhythm options: ${bad_args}")
    endif()
endforeach()
execute_process(
    COMMAND "${FORMANT_CLI}" --phonemes "[ee s ee s ee]"
        --rhythm-file "${RHYTHM_DIR}/tampered.rhythm"
        --output "${RHYTHM_DIR}/tampered.wav"
    RESULT_VARIABLE rhythm_tamper_result
    ERROR_VARIABLE rhythm_tamper_error
)
if(rhythm_tamper_result EQUAL 0 OR NOT rhythm_tamper_error MATCHES "checksum mismatch")
    message(FATAL_ERROR "rhythm checksum mismatch was not reported: ${rhythm_tamper_error}")
endif()
execute_process(
    COMMAND "${FORMANT_CLI}" --song twinkle --rhythm stress
        --output "${RHYTHM_DIR}/song-rhythm.wav"
    RESULT_VARIABLE rhythm_song_result
    ERROR_VARIABLE rhythm_song_error
)
if(rhythm_song_result EQUAL 0 OR NOT rhythm_song_error MATCHES "song mode owns its F0 contour")
    message(FATAL_ERROR "song mode accepted a rhythm layer: ${rhythm_song_error}")
endif()

# ---- #135: foreign-accent profiles as versioned phoneme-stream data -------
set(ACCENT_DIR "${CMAKE_CURRENT_BINARY_DIR}/accent-smoke")
file(MAKE_DIRECTORY "${ACCENT_DIR}")
execute_process(
    COMMAND "${FORMANT_CLI}" --list-accents
    RESULT_VARIABLE list_accents_result
    OUTPUT_VARIABLE list_accents_output
    ERROR_VARIABLE list_accents_error
)
if(NOT list_accents_result EQUAL 0 OR NOT list_accents_output MATCHES "Accent profiles \\(2\\)")
    message(FATAL_ERROR "formant --list-accents failed: ${list_accents_error}")
endif()
if(NOT list_accents_output MATCHES "  en-on-es\ten -> es\t"
        OR NOT list_accents_output MATCHES "  es-on-en\tes -> en\t")
    message(FATAL_ERROR "--list-accents omitted a built-in: ${list_accents_output}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-accent "${FORMANT_SOURCE_DIR}/data/accent/accent_profiles.txt"
    RESULT_VARIABLE inspect_accent_result
    OUTPUT_VARIABLE inspect_accent_output
    ERROR_VARIABLE inspect_accent_error
)
if(NOT inspect_accent_result EQUAL 0 OR NOT inspect_accent_output MATCHES "profiles\t2\n"
        OR NOT inspect_accent_output MATCHES "status\tvalid")
    message(FATAL_ERROR "formant --inspect-accent failed: ${inspect_accent_output}${inspect_accent_error}")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --dump-accent es-on-en
    RESULT_VARIABLE dump_accent_result
    OUTPUT_VARIABLE dump_accent_output
    ERROR_VARIABLE dump_accent_error
)
if(NOT dump_accent_result EQUAL 0 OR NOT dump_accent_output MATCHES "accent es-on-en es en\n"
        OR dump_accent_output MATCHES "accent en-on-es")
    message(FATAL_ERROR "formant --dump-accent failed: ${dump_accent_error}")
endif()
file(WRITE "${ACCENT_DIR}/es-on-en.accent" "${dump_accent_output}")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-accent "${ACCENT_DIR}/es-on-en.accent"
    RESULT_VARIABLE dumped_accent_result
    OUTPUT_VARIABLE dumped_accent_output
    ERROR_VARIABLE dumped_accent_error
)
if(NOT dumped_accent_result EQUAL 0 OR NOT dumped_accent_output MATCHES "profiles\t1\n"
        OR NOT dumped_accent_output MATCHES "status\tvalid")
    message(FATAL_ERROR "dumped accent failed checksum: ${dumped_accent_output}${dumped_accent_error}")
endif()

# The authoring template ships unsigned; inspect reports it, and a signed
# copy renders through --accent-file.
set(ACCENT_EXAMPLE "${FORMANT_SOURCE_DIR}/examples/accent/fictional.accent")
execute_process(
    COMMAND "${FORMANT_CLI}" --inspect-accent "${ACCENT_EXAMPLE}"
    RESULT_VARIABLE example_accent_result
    OUTPUT_VARIABLE example_accent_output
    ERROR_VARIABLE example_accent_error
)
if(NOT example_accent_result EQUAL 0 OR NOT example_accent_output MATCHES "status\tinvalid")
    message(FATAL_ERROR "template accent inspect failed: ${example_accent_output}${example_accent_error}")
endif()
string(REGEX MATCH "computed\t([0-9a-f]+)" _ "${example_accent_output}")
set(example_accent_checksum "${CMAKE_MATCH_1}")
file(READ "${ACCENT_EXAMPLE}" example_accent_text)
# Only the directive line: the template's comments mention the placeholder too.
string(REPLACE "\nchecksum PLACEHOLDER" "\nchecksum ${example_accent_checksum}"
    example_accent_signed "${example_accent_text}")
file(WRITE "${ACCENT_DIR}/starfall.accent" "${example_accent_signed}")
execute_process(
    COMMAND "${FORMANT_CLI}" --text "Is this the best stop?"
        --accent-file "${ACCENT_DIR}/starfall.accent"
        --format raw16 --output "${ACCENT_DIR}/starfall.raw"
    RESULT_VARIABLE starfall_result
    OUTPUT_VARIABLE starfall_status
    ERROR_VARIABLE starfall_error
)
if(NOT starfall_result EQUAL 0 OR NOT starfall_status MATCHES "Applied accent 'starfall-on-en'")
    message(FATAL_ERROR "signed template accent did not render: ${starfall_error}")
endif()

# Built-in and shared-file selection give identical bytes; the accent changes
# the render; repeats are deterministic; the profile's rhythm convention
# applies unless an explicit --rhythm wins.
set(ACCENT_TEXT "Think of the best stop, very cup.")
foreach(case_name plain builtin again file rhythm)
    if(case_name STREQUAL "plain")
        set(accent_args "")
    elseif(case_name STREQUAL "file")
        set(accent_args --accent-file "${ACCENT_DIR}/es-on-en.accent")
    elseif(case_name STREQUAL "rhythm")
        set(accent_args --accent es-on-en --rhythm stress)
    else()
        set(accent_args --accent es-on-en)
    endif()
    execute_process(
        COMMAND "${FORMANT_CLI}" --text "${ACCENT_TEXT}" ${accent_args}
            --format raw16 --output "${ACCENT_DIR}/${case_name}.raw"
        RESULT_VARIABLE accent_render_result
        OUTPUT_VARIABLE accent_render_status
        ERROR_VARIABLE accent_render_error
    )
    if(NOT accent_render_result EQUAL 0)
        message(FATAL_ERROR "accent render failed for ${case_name}: ${accent_render_error}")
    endif()
    if(case_name STREQUAL "builtin"
            AND NOT accent_render_status MATCHES "Applied rhythm 'syllable' from accent 'es-on-en'")
        message(FATAL_ERROR "accent rhythm convention was not applied: ${accent_render_status}")
    endif()
    if(case_name STREQUAL "rhythm"
            AND (NOT accent_render_status MATCHES "Applied rhythm 'stress' \\("
                 OR accent_render_status MATCHES "from accent"))
        message(FATAL_ERROR "explicit --rhythm did not win over the accent: ${accent_render_status}")
    endif()
    file(SHA256 "${ACCENT_DIR}/${case_name}.raw" accent_${case_name}_hash)
endforeach()
if(accent_plain_hash STREQUAL accent_builtin_hash)
    message(FATAL_ERROR "--accent es-on-en did not change the render")
endif()
if(NOT accent_builtin_hash STREQUAL accent_again_hash)
    message(FATAL_ERROR "--accent render is not deterministic")
endif()
if(NOT accent_builtin_hash STREQUAL accent_file_hash)
    message(FATAL_ERROR "--accent-file did not reproduce the built-in profile")
endif()
if(accent_builtin_hash STREQUAL accent_rhythm_hash)
    message(FATAL_ERROR "explicit --rhythm did not change the accented render")
endif()

execute_process(
    COMMAND "${FORMANT_CLI}" --lang es --text "pero tiene un tren" --accent en-on-es
        --format raw16 --output "${ACCENT_DIR}/en-on-es.raw"
    RESULT_VARIABLE en_on_es_result
    OUTPUT_VARIABLE en_on_es_status
    ERROR_VARIABLE en_on_es_error
)
if(NOT en_on_es_result EQUAL 0 OR NOT en_on_es_status MATCHES "Applied accent 'en-on-es' \\([0-9]+ -> [0-9]+ segments\\)")
    message(FATAL_ERROR "--accent en-on-es failed: ${en_on_es_error}")
endif()

# Errors: exclusive flags, unknown names, tampered and unsigned-template
# checksums, multi-profile files and song ownership. No output is created.
file(WRITE "${ACCENT_DIR}/tampered.accent" "${dump_accent_output}# tamper\n")
foreach(error_case
        "use only one of --accent and --accent-file|--accent;es-on-en;--accent-file;${ACCENT_DIR}/es-on-en.accent"
        "unknown accent profile 'bogus'|--accent;bogus"
        "checksum mismatch|--accent-file;${ACCENT_DIR}/tampered.accent"
        "64 hexadecimal digits|--accent-file;${ACCENT_EXAMPLE}"
        "exactly one profile|--accent-file;${FORMANT_SOURCE_DIR}/data/accent/accent_profiles.txt"
        "cannot open accent file|--accent-file;${ACCENT_DIR}/missing.accent"
        "unknown accent profile: bogus|--dump-accent;bogus")
    string(FIND "${error_case}" "|" split_at)
    string(SUBSTRING "${error_case}" 0 ${split_at} expected_error)
    math(EXPR args_at "${split_at} + 1")
    string(SUBSTRING "${error_case}" ${args_at} -1 bad_args)
    file(REMOVE "${ACCENT_DIR}/bad.wav")
    execute_process(
        COMMAND "${FORMANT_CLI}" --text "stop" ${bad_args}
            --output "${ACCENT_DIR}/bad.wav"
        RESULT_VARIABLE accent_error_result
        ERROR_VARIABLE accent_error_text
    )
    if(accent_error_result EQUAL 0 OR NOT accent_error_text MATCHES "${expected_error}")
        message(FATAL_ERROR "accent error '${expected_error}' was not reported for ${bad_args}: ${accent_error_text}")
    endif()
    if(EXISTS "${ACCENT_DIR}/bad.wav")
        message(FATAL_ERROR "invalid accent options still wrote output: ${bad_args}")
    endif()
endforeach()
execute_process(
    COMMAND "${FORMANT_CLI}" --song twinkle --accent es-on-en
        --output "${ACCENT_DIR}/song-accent.wav"
    RESULT_VARIABLE accent_song_result
    ERROR_VARIABLE accent_song_error
)
if(accent_song_result EQUAL 0 OR NOT accent_song_error MATCHES "rhythm or accent")
    message(FATAL_ERROR "song mode accepted an accent layer: ${accent_song_error}")
endif()

message(STATUS "formant CLI smoke test wrote ${output_size} bytes")

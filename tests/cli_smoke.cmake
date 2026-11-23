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

message(STATUS "formant CLI smoke test wrote ${output_size} bytes")

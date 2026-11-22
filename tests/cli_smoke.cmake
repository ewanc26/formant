# Minimal end-to-end check: the CLI must write a plausible mono 16-bit WAV.
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

message(STATUS "formant CLI smoke test wrote ${output_size} bytes")

# Smoke test for the fixed-corpus benchmark harness. Determinism is the
# contract under test here, so the report's checksum column must be all "ok";
# timing values are intentionally not asserted.

execute_process(
    COMMAND "${FORMANT_CLI}" --benchmark --benchmark-repeats 1
            --benchmark-report "${FORMANT_REPORT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "benchmark failed: ${result}\n${output}\n${errors}")
endif()

file(READ "${FORMANT_REPORT}" report)
string(REGEX REPLACE "\r?\n" ";" lines "${report}")

set(cases neutral alto bass child robot whisper)
foreach(name IN LISTS cases)
    if(NOT report MATCHES "(^|\n)${name}\t")
        message(FATAL_ERROR "benchmark report is missing case ${name}")
    endif()
endforeach()

if(report MATCHES "\tFAIL\n")
    message(FATAL_ERROR "benchmark reported a checksum mismatch:\n${report}")
endif()

foreach(field "rtf" "samples_s" "f0_hz" "harmonics" "noise_partials")
    if(NOT report MATCHES "${field}")
        message(FATAL_ERROR "benchmark report is missing the ${field} column")
    endif()
endforeach()

message(STATUS "benchmark smoke test passed")

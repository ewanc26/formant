cmake_minimum_required(VERSION 3.20)

project(
  formant
  VERSION 0.30.0
  DESCRIPTION "A customisable sine-wave voice synthesis engine"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(FORMANT_SANITIZE "Enable address and undefined-behaviour sanitizers" OFF)
option(FORMANT_BUILD_TESTS "Build the test suite" ON)
option(FORMANT_INSTALL "Install the library and CLI" ON)

add_library(formant_core STATIC
    src/voice.cpp
    src/voice_file.cpp
    src/hash.cpp
    src/transpose.cpp
    src/phoneme.cpp
    src/gibberish.cpp
    src/gibberish_ngram.cpp
    src/language.cpp
    src/lexicon.cpp
    src/ipa.cpp
    src/synthesizer.cpp
    src/schedule.cpp
    src/ssml.cpp
    src/prosody_file.cpp
    src/intonation.cpp
    src/mrt.cpp
    src/timing.cpp
    src/wav.cpp
    src/metrics.cpp
    src/sine_kernel.cpp
)
add_library(formant::formant ALIAS formant_core)
target_include_directories(formant_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(formant_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

if(FORMANT_SANITIZE)
    target_compile_options(formant_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(formant_core PUBLIC -fsanitize=address,undefined)
endif()

configure_file(
    src/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/formant/version.h
    @ONLY
)

add_executable(formant src/cli/main.cpp benchmark/benchmark.cpp)
target_link_libraries(formant PRIVATE formant_core)
target_include_directories(formant PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if(FORMANT_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(formant_tests tests/formant_tests.cpp)
        target_link_libraries(formant_tests PRIVATE formant_core)
        add_test(NAME formant_unit_tests COMMAND formant_tests)

        # #98: PHOIBLE conformance. Reads the committed segment table and
        # coverage summary; PHOIBLE itself is a development-time data source.
        add_executable(formant_phoible_tests tests/formant_phoible_tests.cpp)
        target_link_libraries(formant_phoible_tests PRIVATE formant_core)
        target_compile_definitions(formant_phoible_tests PRIVATE
            FORMANT_PHOIBLE_TABLE="${CMAKE_CURRENT_SOURCE_DIR}/data/phoible/segments.tsv"
            FORMANT_PHOIBLE_COVERAGE="${CMAKE_CURRENT_SOURCE_DIR}/data/phoible/coverage.txt")
        add_test(NAME formant_phoible_conformance COMMAND formant_phoible_tests)

        add_executable(formant_formant_verification
            tests/formant_verification.cpp)
        target_link_libraries(formant_formant_verification PRIVATE formant_core)
        target_compile_definitions(formant_formant_verification PRIVATE
            FORMANT_METRICS_BASELINE="${CMAKE_CURRENT_SOURCE_DIR}/tests/metrics_baseline.txt")
        add_test(NAME formant_formant_verification
            COMMAND formant_formant_verification
                --report ${CMAKE_CURRENT_BINARY_DIR}/metrics-report.txt)

        add_test(NAME formant_metrics_regression
            COMMAND formant_formant_verification --regression-check)

        add_executable(formant_sine_kernel tests/formant_sine_kernel.cpp)
        target_link_libraries(formant_sine_kernel PRIVATE formant_core)
        target_include_directories(formant_sine_kernel PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        add_test(NAME formant_sine_kernel COMMAND formant_sine_kernel)

        add_test(NAME formant_cli_help COMMAND formant --help)
        add_test(NAME formant_cli_voices COMMAND formant --list-voices)
        add_test(NAME formant_cli_render COMMAND
            ${CMAKE_COMMAND}
            -DFORMANT_CLI=$<TARGET_FILE:formant>
            -DFORMANT_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/smoke.wav
            -DFORMANT_EXPECTED_VERSION=${PROJECT_VERSION}
            -DFORMANT_EXAMPLES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)

        add_test(NAME formant_benchmark COMMAND
            ${CMAKE_COMMAND}
            -DFORMANT_CLI=$<TARGET_FILE:formant>
            -DFORMANT_REPORT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-report.tsv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_smoke.cmake)

        add_test(NAME formant_checksum_corpus COMMAND
            ${CMAKE_COMMAND}
            -DFORMANT_CLI=$<TARGET_FILE:formant>
            -DFORMANT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/checksum_corpus.cmake)

        add_executable(formant_property_tests tests/formant_property_tests.cpp)
        target_link_libraries(formant_property_tests PRIVATE formant_core)
        # Instrumented renders are ~25x slower, so keep the sanitizer job well
        # inside ctest's 300s per-test timeout while still exercising all five
        # properties on Linux.
        if(FORMANT_SANITIZE)
            add_test(NAME formant_property_tests COMMAND formant_property_tests
                --seeds 24)
        else()
            add_test(NAME formant_property_tests COMMAND formant_property_tests)
        endif()

        # The long campaign is built by default (nightly.yml invokes it
        # directly); a small slice also runs in the fast PR gate.
        add_executable(formant_fuzz tests/formant_fuzz.cpp)
        target_link_libraries(formant_fuzz PRIVATE formant_core)
        if(FORMANT_SANITIZE)
            add_test(NAME formant_fuzz_smoke COMMAND formant_fuzz
                --scripts 20 --seed-base 1)
        else()
            add_test(NAME formant_fuzz_smoke COMMAND formant_fuzz
                --scripts 200 --seed-base 1)
        endif()
    endif()
endif()

# Offline formant-trajectory verification (issue #104). Deliberately opt-in:
# EXCLUDE_FROM_ALL keeps the default build unchanged, there is no add_test, and
# the golden fixture is only rewritten by an explicit --write-golden run.
add_executable(formant_verify_tool EXCLUDE_FROM_ALL tests/formant_verify.cpp)
target_link_libraries(formant_verify_tool PRIVATE formant_core)
target_include_directories(formant_verify_tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(formant_verify_tool PRIVATE
    FORMANT_TRAJECTORY_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/formant_trajectory_golden.txt")
add_custom_target(formant_verify
    COMMAND $<TARGET_FILE:formant_verify_tool>
            --report ${CMAKE_CURRENT_BINARY_DIR}/formant-verify-report.txt
    DEPENDS formant_verify_tool
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Offline formant-trajectory drift report")

if(FORMANT_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS formant_core formant
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY data/phoible/ DESTINATION ${CMAKE_INSTALL_DATADIR}/formant/phoible)
endif()

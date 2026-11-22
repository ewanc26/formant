cmake_minimum_required(VERSION 3.20)

project(
  formant
  VERSION 0.4.7
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
    src/phoneme.cpp
    src/ipa.cpp
    src/synthesizer.cpp
    src/wav.cpp
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

add_executable(formant src/cli/main.cpp)
target_link_libraries(formant PRIVATE formant_core)
target_include_directories(formant PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

if(FORMANT_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(formant_tests tests/formant_tests.cpp)
        target_link_libraries(formant_tests PRIVATE formant_core)
        add_test(NAME formant_unit_tests COMMAND formant_tests)

        add_test(NAME formant_cli_help COMMAND formant --help)
        add_test(NAME formant_cli_voices COMMAND formant --list-voices)
        add_test(NAME formant_cli_render COMMAND
            ${CMAKE_COMMAND}
            -DFORMANT_CLI=$<TARGET_FILE:formant>
            -DFORMANT_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/smoke.wav
            -DFORMANT_EXPECTED_VERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)
    endif()
endif()

if(FORMANT_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS formant_core formant
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

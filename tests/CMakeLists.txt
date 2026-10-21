# Unit tests (doctest) per library plus CLI integration tests driven by CTest.
# Every test here runs on macOS and Windows CI; nothing may depend on audio hardware,
# a display, the working directory or the user's home.

function(radiff_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE radiff::doctest radiff::compiler_settings)
    target_compile_definitions(${name} PRIVATE
        RADIFF_TEST_TMP_DIR="${CMAKE_BINARY_DIR}/test-tmp"
        RADIFF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        RADIFF_FACTORY_PRESET_DIR="${CMAKE_SOURCE_DIR}/presets/factory")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-tmp)

radiff_add_test(test_core
    core/test_engine.cpp
    core/test_parameters.cpp
    core/test_dsp_primitives.cpp
    core/test_input_stage.cpp
    core/test_eq.cpp
    core/test_expander.cpp
    core/test_compressor.cpp
    core/test_deesser.cpp
    core/test_presence.cpp
    core/test_analysis.cpp)
target_link_libraries(test_core PRIVATE radiff::core)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)

radiff_add_test(test_audioio  audioio/test_wav.cpp)
target_link_libraries(test_audioio PRIVATE radiff::audioio)

radiff_add_test(test_presets  presets/test_preset.cpp)
target_link_libraries(test_presets PRIVATE radiff::presets radiff::json)

radiff_add_test(test_app      app/test_transactional.cpp app/test_file_processor.cpp)
target_link_libraries(test_app PRIVATE radiff::app)

# ---------------------------------------------------------------------------
# CLI integration tests. radiff_testtool generates and verifies WAV files so the
# tests are identical on every OS (no shell scripts, no sox, no python).
# ---------------------------------------------------------------------------
add_executable(radiff_testtool integration/testtool.cpp)
target_link_libraries(radiff_testtool PRIVATE radiff::audioio radiff::presets radiff::compiler_settings)

if(RADIFF_BUILD_CLI)
    add_subdirectory(integration)
endif()

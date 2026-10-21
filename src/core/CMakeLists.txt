# radiff::core - the DSP engine.
#
# RULES (enforced by review and by the fact that this target links nothing else):
#   * No platform headers, no framework headers, no file I/O, no allocation in process().
#   * Only the C++ standard library. No <filesystem>, no threads.
#   * No #ifdef _WIN32 / __APPLE__ - the engine is identical on every platform.

add_library(radiff_core STATIC
    src/Engine.cpp
    src/Parameters.cpp
    src/modules/InputStage.cpp
    src/modules/Equalizer.cpp
    src/modules/Expander.cpp
    src/modules/Compressor.cpp
)
add_library(radiff::core ALIAS radiff_core)

target_include_directories(radiff_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(radiff_core PRIVATE radiff::compiler_settings)
target_compile_features(radiff_core PUBLIC cxx_std_20)

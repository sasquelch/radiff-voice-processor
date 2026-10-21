# radiff::audioio - portable WAV reading and writing. No platform code; all file
# access goes through std::filesystem paths supplied by callers.

add_library(radiff_audioio STATIC
    src/WavFile.cpp
)
add_library(radiff::audioio ALIAS radiff_audioio)

target_include_directories(radiff_audioio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(radiff_audioio
    PUBLIC  radiff::core radiff::platform
    PRIVATE radiff::compiler_settings)

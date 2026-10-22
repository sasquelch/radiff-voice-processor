# radiff::audioio - portable WAV reading and writing. No platform code; all file
# access goes through std::filesystem paths supplied by callers.

add_library(radiff_audioio STATIC
    src/WavFile.cpp
    src/Mp3File.cpp
    src/minimp3_impl.c
)
# Foreign decoder code: compile without our warning set (it is not ours to restyle).
set_source_files_properties(src/minimp3_impl.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
add_library(radiff::audioio ALIAS radiff_audioio)

target_include_directories(radiff_audioio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(radiff_audioio
    PUBLIC  radiff::core radiff::platform
    PRIVATE radiff::compiler_settings radiff::minimp3)

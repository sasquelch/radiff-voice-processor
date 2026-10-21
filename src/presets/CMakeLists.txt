# radiff::presets - the portable .radvoice.json preset format.

add_library(radiff_presets STATIC
    src/Preset.cpp
)
add_library(radiff::presets ALIAS radiff_presets)

target_include_directories(radiff_presets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(radiff_presets
    PUBLIC  radiff::core radiff::platform
    PRIVATE radiff::json radiff::compiler_settings)

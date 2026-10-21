# radiff::ui - toolkit-independent helpers shared by the GUI hosts (standalone app, later the
# plug-in editors): preset library bookkeeping, background file jobs, parameter text.
# No iPlug2/IGraphics here, so it compiles with the strict RaDiff warning set.

add_library(radiff_ui STATIC
    src/PresetLibrary.cpp
    src/JobRunner.cpp
    src/ParamText.cpp
)
add_library(radiff::ui ALIAS radiff_ui)

target_include_directories(radiff_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(radiff_ui
    PUBLIC  radiff::app radiff::presets radiff::platform radiff::core
    PRIVATE radiff::compiler_settings)
find_package(Threads REQUIRED)
target_link_libraries(radiff_ui PUBLIC Threads::Threads)

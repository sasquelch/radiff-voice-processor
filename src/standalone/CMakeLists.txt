# RaDiff Voice Processor - standalone application (iPlug2 APP target, Phase 4).
#
# The iPlug2 CMake support is included HERE (not at the top level) because iPlug2.cmake sets
# directory-scoped globals (CMAKE_CXX_STANDARD, MSVC runtime, folders). Everything above this
# directory stays untouched by it. Our own GUI glue is compiled with the normal warning set but
# NOT with -Werror: iPlug2's headers emit warnings we cannot fix. The toolkit-independent parts
# live in radiff::ui (src/ui), which does use the strict settings.

set(IPLUG2_DIR ${RADIFF_IPLUG2_DIR})
set(IPLUG2_CXX_STANDARD 20 CACHE STRING "C++ standard for iPlug2" FORCE)
# Keep the build tree self-contained: no copy/symlink of the .app into ~/Applications.
set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "iPlug2: deploy built targets to system folders" FORCE)

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

set(RADIFF_STANDALONE_SOURCES
    RaDiffVoiceApp.cpp
    RaDiffVoiceApp.h
    RaDiffControls.h
    config.h
    resources/resource.h)

set(RADIFF_STANDALONE_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Roboto-Regular.ttf)

# Factory presets: the source of truth is presets/factory; they are bundled read-only into the
# app (macOS: Contents/Resources/presets, Windows: <exe dir>/presets).
file(GLOB RADIFF_FACTORY_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/presets/factory/*.radvoice.json")

iplug_add_plugin(RaDiffVoice
    FORMATS APP
    SOURCES ${RADIFF_STANDALONE_SOURCES}
    RESOURCES ${RADIFF_STANDALONE_RESOURCES}
    LINK radiff::ui radiff::app radiff::presets radiff::platform radiff::core
)

set(_app RaDiffVoice-app)

# Warnings on, but not fatal (see header comment); C++20 for our files.
if(MSVC)
    target_compile_options(${_app} PRIVATE /W3)
else()
    target_compile_options(${_app} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)
endif()
target_compile_definitions(${_app} PRIVATE
    RADIFF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RADIFF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RADIFF_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    RADIFF_VERSION_STRING="${PROJECT_VERSION}"
    # Debug builds started from the build tree find the factory presets in the source tree.
    $<$<CONFIG:Debug>:RADIFF_DEV_FACTORY_PRESETS="${CMAKE_SOURCE_DIR}/presets/factory">)

if(APPLE)
    target_sources(${_app} PRIVATE ${RADIFF_FACTORY_PRESET_FILES})
    set_source_files_properties(${RADIFF_FACTORY_PRESET_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/presets")
    # iPlug2's Info.plist template has no high-resolution flag; our plist sets it (Retina).
else()
    add_custom_command(TARGET ${_app} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_app}>/presets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RADIFF_FACTORY_PRESET_FILES} "$<TARGET_FILE_DIR:${_app}>/presets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RADIFF_STANDALONE_RESOURCES} "$<TARGET_FILE_DIR:${_app}>"
        COMMENT "Copying factory presets and fonts next to RaDiffVoice.exe")
endif()

message(STATUS "RaDiff standalone: target ${_app} -> ${CMAKE_BINARY_DIR}/out")

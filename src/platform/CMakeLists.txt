# radiff::platform - the ONLY library allowed to contain OS-specific code.
#
# Everything above it (audioio, presets, app, cli, ui) programs against the portable
# interfaces in include/radiff/platform/. Each OS supplies one implementation file.

set(RADIFF_PLATFORM_SOURCES src/Paths.cpp)

if(WIN32)
    list(APPEND RADIFF_PLATFORM_SOURCES windows/FileSystemWindows.cpp windows/ProcessWindows.cpp windows/WindowsPaths.cpp)
elseif(APPLE)
    list(APPEND RADIFF_PLATFORM_SOURCES posix/FileSystemPosix.cpp posix/ProcessPosix.cpp macos/MacOSPaths.mm)
else()
    list(APPEND RADIFF_PLATFORM_SOURCES posix/FileSystemPosix.cpp posix/ProcessPosix.cpp posix/PosixPaths.cpp)
endif()

add_library(radiff_platform STATIC ${RADIFF_PLATFORM_SOURCES})
add_library(radiff::platform ALIAS radiff_platform)

target_include_directories(radiff_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(radiff_platform PRIVATE radiff::compiler_settings)

if(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(macos/MacOSPaths.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(radiff_platform PUBLIC "-framework Foundation")
endif()

if(WIN32)
    target_link_libraries(radiff_platform PUBLIC shell32 ole32)
endif()

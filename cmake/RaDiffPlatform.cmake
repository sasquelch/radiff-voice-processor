# Platform settings that must be configured BEFORE project().
#
# macOS
#   Minimum deployment target: macOS 11.0 (Big Sur).
#   Rationale: first macOS release with Apple Silicon support, so a single Universal
#   binary covers every supported machine; both the VST3 SDK 3.8 and iPlug2 support it.
#   Universal (arm64 + x86_64) builds are opt-in via RADIFF_UNIVERSAL=ON, used for release.
#
# Windows
#   Target: Windows 10/11 x64 with MSVC (Visual Studio 2022 toolset) and the Windows SDK.
#   WINVER / _WIN32_WINNT are pinned to Windows 10 in RaDiffCompilerSettings.cmake.

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)

    option(RADIFF_UNIVERSAL "Build Universal macOS binaries (arm64 + x86_64)" OFF)
    if(RADIFF_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
endif()

if(WIN32)
    # Make MSVC runtime selection explicit and consistent across all targets.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# Always produce position-independent code: plug-ins are shared libraries that link the
# same static core libraries as the executables.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

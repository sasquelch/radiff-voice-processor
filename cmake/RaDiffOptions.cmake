# Build options. Everything is ON by default where the platform supports it, so a
# fresh checkout builds the full product. CI and developers can narrow the build.

option(RADIFF_BUILD_CLI        "Build the headless RaDiffVoiceCLI processor" ON)
option(RADIFF_BUILD_TESTS      "Build unit and integration tests"            ON)
if(APPLE OR WIN32)
    set(RADIFF_STANDALONE_DEFAULT ON)
else()
    set(RADIFF_STANDALONE_DEFAULT OFF)   # iPlug2 has no Linux desktop backend
endif()
option(RADIFF_BUILD_STANDALONE "Build the standalone GUI application (Phase 4, iPlug2)" ${RADIFF_STANDALONE_DEFAULT})
option(RADIFF_BUILD_VST3       "Build the VST3 plug-in (Phase 6)"               OFF)
option(RADIFF_BUILD_AU         "Build the AUv2 plug-in, macOS only (Phase 6)"   OFF)
option(RADIFF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"            ON)
option(RADIFF_ENABLE_ASAN      "Enable AddressSanitizer/UBSan (Clang/GCC only)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep all build products in predictable places so packaging scripts are simple.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

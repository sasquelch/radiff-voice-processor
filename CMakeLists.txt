cmake_minimum_required(VERSION 3.25)

# Deployment target and architectures must be set before project() on Apple.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/RaDiffPlatform.cmake)

project(RaDiffVoiceProcessor
    VERSION 0.1.0
    DESCRIPTION "RaDiff Voice Processor - cross-platform broadcast voice processing"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(RaDiffOptions)
include(RaDiffCompilerSettings)

# ---------------------------------------------------------------------------
# Third-party dependencies (all pinned as git submodules; see third_party/README.md)
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# Shared, platform-independent libraries
# ---------------------------------------------------------------------------
add_subdirectory(src/core)       # radiff::core     - DSP engine (no platform or framework deps)
add_subdirectory(src/platform)   # radiff::platform - OS abstractions behind portable interfaces
add_subdirectory(src/audioio)    # radiff::audioio  - WAV reading/writing
add_subdirectory(src/presets)    # radiff::presets  - .radvoice.json preset format
add_subdirectory(src/app)        # radiff::app      - shared processing/batch/transactional output logic

# ---------------------------------------------------------------------------
# Executables and plug-ins
# ---------------------------------------------------------------------------
if(RADIFF_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(RADIFF_BUILD_STANDALONE)
    add_subdirectory(src/standalone)   # Phase 5
endif()

if(RADIFF_BUILD_VST3)
    add_subdirectory(src/plugins/vst3) # Phase 6
endif()

if(RADIFF_BUILD_AU AND APPLE)
    add_subdirectory(src/plugins/au)   # Phase 6
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(RADIFF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(RaDiffSummary)

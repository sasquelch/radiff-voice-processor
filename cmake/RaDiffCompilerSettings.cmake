# An INTERFACE target that every RaDiff target links to for consistent compiler flags.
# Third-party code does NOT link this, so their warnings do not break the build.

add_library(radiff_compiler_settings INTERFACE)
add_library(radiff::compiler_settings ALIAS radiff_compiler_settings)

target_compile_features(radiff_compiler_settings INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(radiff_compiler_settings INTERFACE
        /W4
        /permissive-
        /utf-8          # source and execution charset are UTF-8 on every platform
        /Zc:__cplusplus
        /Zc:preprocessor
        /EHsc
        /MP
        /fp:precise
        $<$<BOOL:${RADIFF_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_compile_definitions(radiff_compiler_settings INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0A00   # Windows 10
        WINVER=0x0A00
    )
else()
    target_compile_options(radiff_compiler_settings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wnull-dereference
        -Wdouble-promotion -Wformat=2
        -fno-fast-math      # DSP output must be deterministic within documented tolerances
        $<$<BOOL:${RADIFF_WARNINGS_AS_ERRORS}>:-Werror>
    )
    if(RADIFF_ENABLE_ASAN)
        target_compile_options(radiff_compiler_settings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(radiff_compiler_settings INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# Platform identification for the (few) places that need it - the platform library only.
if(APPLE)
    target_compile_definitions(radiff_compiler_settings INTERFACE RADIFF_PLATFORM_MACOS=1)
elseif(WIN32)
    target_compile_definitions(radiff_compiler_settings INTERFACE RADIFF_PLATFORM_WINDOWS=1)
else()
    target_compile_definitions(radiff_compiler_settings INTERFACE RADIFF_PLATFORM_POSIX=1)
endif()

# Version information shared by every artifact.
target_compile_definitions(radiff_compiler_settings INTERFACE
    RADIFF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RADIFF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RADIFF_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    RADIFF_VERSION_STRING="${PROJECT_VERSION}"
)

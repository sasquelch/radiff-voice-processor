# Pinned third-party dependencies. Versions live in the submodule commits and are
# documented in third_party/README.md. Only header-only / small libraries are wired
# in here; iPlug2 and the VST3 SDK are integrated by the plug-in/standalone targets.

# nlohmann/json 3.12.0 (MIT) - header only
add_library(radiff_nlohmann_json INTERFACE)
target_include_directories(radiff_nlohmann_json SYSTEM INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/nlohmann_json/single_include)
target_compile_definitions(radiff_nlohmann_json INTERFACE JSON_USE_IMPLICIT_CONVERSIONS=0)
add_library(radiff::json ALIAS radiff_nlohmann_json)

# doctest 2.5.3 (MIT) - header only test framework
add_library(radiff_doctest INTERFACE)
target_include_directories(radiff_doctest SYSTEM INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/doctest/doctest)
add_library(radiff::doctest ALIAS radiff_doctest)

# minimp3 (CC0 / public domain) - MP3 decoding for INPUT files only. Header only; the single
# implementation TU is compiled inside radiff::audioio with warnings off (foreign code).
add_library(radiff_minimp3 INTERFACE)
target_include_directories(radiff_minimp3 SYSTEM INTERFACE ${CMAKE_CURRENT_LIST_DIR}/minimp3)
target_compile_definitions(radiff_minimp3 INTERFACE MINIMP3_FLOAT_OUTPUT=1 MINIMP3_NO_STDIO=1)
add_library(radiff::minimp3 ALIAS radiff_minimp3)

# Paths for the plug-in/standalone targets (Phase 5/6).
set(RADIFF_IPLUG2_DIR  ${CMAKE_CURRENT_LIST_DIR}/iPlug2  CACHE PATH "iPlug2 checkout")
set(RADIFF_VST3SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/vst3sdk CACHE PATH "VST3 SDK checkout")

foreach(dir IN ITEMS nlohmann_json doctest vst3sdk iPlug2 minimp3)
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/${dir}/.git AND NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/${dir}/README.md)
        message(FATAL_ERROR
            "third_party/${dir} is empty. Run:\n"
            "  git submodule update --init --recursive\n"
            "or scripts/bootstrap-mac.sh")
    endif()
endforeach()

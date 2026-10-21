# radiff::app - shared application logic used by the CLI, standalone app and the
# PlayoutONE integration: file processing jobs, transactional output and exit codes.

add_library(radiff_app STATIC
    src/FileProcessor.cpp
    src/TransactionalFile.cpp
)
add_library(radiff::app ALIAS radiff_app)

target_include_directories(radiff_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(radiff_app
    PUBLIC  radiff::core radiff::audioio radiff::presets radiff::platform
    PRIVATE radiff::compiler_settings)

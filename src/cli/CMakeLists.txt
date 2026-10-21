# RaDiffVoiceCLI - headless command-line processor. Same source, same arguments,
# same exit codes on macOS (RaDiffVoiceCLI) and Windows (RaDiffVoiceCLI.exe).

add_executable(RaDiffVoiceCLI main.cpp)
target_link_libraries(RaDiffVoiceCLI PRIVATE radiff::app radiff::json radiff::compiler_settings)
set_target_properties(RaDiffVoiceCLI PROPERTIES OUTPUT_NAME "RaDiffVoiceCLI")

install(TARGETS RaDiffVoiceCLI RUNTIME DESTINATION bin COMPONENT cli)

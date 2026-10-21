set(T ${CMAKE_BINARY_DIR}/test-tmp/integration)
file(MAKE_DIRECTORY ${T})
set(CLI $<TARGET_FILE:RaDiffVoiceCLI>)
set(TOOL $<TARGET_FILE:radiff_testtool>)
set(FACTORY ${CMAKE_SOURCE_DIR}/presets/factory)

# Unicode path on purpose: exercises std::filesystem / UTF-16 handling on Windows.
set(UNICODE_NAME "tëst ünïcödé 音声")

# A "unity" preset: every stage off, so gain expectations are exact.
set(UNITY_SETS --set input.dcBlockEnabled=false --set input.highPassEnabled=false --set eq.enabled=false
               --set expander.enabled=false --set compressor1.enabled=false --set compressor2.enabled=false)

# --- setup fixture: generate inputs and presets ------------------------------------
add_test(NAME cli_setup_generate
    COMMAND ${TOOL} gen-wav --out "${T}/sine_mono_48k.wav" --rate 48000 --channels 1 --seconds 1.5 --format pcm24 --freq 440 --amp 0.5)
add_test(NAME cli_setup_generate_stereo
    COMMAND ${TOOL} gen-wav --out "${T}/sine_stereo_44k.wav" --rate 44100 --channels 2 --seconds 1 --format pcm16 --freq 1000 --amp 0.25)
add_test(NAME cli_setup_generate_unicode
    COMMAND ${TOOL} gen-wav --out "${T}/${UNICODE_NAME}.wav" --rate 48000 --channels 1 --seconds 0.5 --format float32 --freq 220 --amp 0.5)
add_test(NAME cli_setup_preset
    COMMAND ${TOOL} gen-preset --out "${T}/unity-minus6.radvoice.json" --name "Unity minus 6" ${UNITY_SETS} --set output.gainDb=-6)
set_tests_properties(cli_setup_generate cli_setup_generate_stereo cli_setup_generate_unicode cli_setup_preset
    PROPERTIES FIXTURES_SETUP cli_inputs)

# --- behaviour -----------------------------------------------------------------------
add_test(NAME cli_version COMMAND ${CLI} --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^[0-9]+\\.[0-9]+\\.[0-9]+")
add_test(NAME cli_help COMMAND ${CLI} --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")
add_test(NAME cli_list_params COMMAND ${CLI} --list-params)
set_tests_properties(cli_list_params PROPERTIES PASS_REGULAR_EXPRESSION "expander.thresholdDbfs")

# CTest cannot assert a specific exit code directly; the testtool wraps the command.
add_test(NAME cli_exit_code_usage      COMMAND ${TOOL} expect-exit 2 -- ${CLI} --bogus)
add_test(NAME cli_exit_code_bad_set    COMMAND ${TOOL} expect-exit 2 -- ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/x.wav" --set expander.nope=1)
add_test(NAME cli_exit_code_input      COMMAND ${TOOL} expect-exit 3 -- ${CLI} --input "${T}/does-not-exist.wav" --output "${T}/x.wav")
add_test(NAME cli_exit_code_preset     COMMAND ${TOOL} expect-exit 4 -- ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/x.wav" --preset "${T}/missing.radvoice.json")
add_test(NAME cli_exit_code_same_file  COMMAND ${TOOL} expect-exit 2 -- ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/sine_mono_48k.wav" --overwrite)

add_test(NAME cli_process_mono_preset
    COMMAND ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/out/mono_minus6.wav" --preset "${T}/unity-minus6.radvoice.json" --overwrite)
add_test(NAME cli_verify_mono_preset
    COMMAND ${TOOL} verify-wav --file "${T}/out/mono_minus6.wav" --rate 48000 --channels 1 --frames 72000 --format pcm24 --peak-db -12.02 --tol-db 0.05)

add_test(NAME cli_process_stereo_override
    COMMAND ${CLI} --input "${T}/sine_stereo_44k.wav" --output "${T}/out/stereo.wav" ${UNITY_SETS} --set output.gainDb=6 --format float32 --overwrite)
add_test(NAME cli_verify_stereo_override
    COMMAND ${TOOL} verify-wav --file "${T}/out/stereo.wav" --rate 44100 --channels 2 --frames 44100 --format float32 --peak-db -6.02 --tol-db 0.05)

add_test(NAME cli_process_unicode_path
    COMMAND ${CLI} --input "${T}/${UNICODE_NAME}.wav" --output "${T}/out/${UNICODE_NAME} out.wav" --overwrite --json-result)
set_tests_properties(cli_process_unicode_path PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\":true")
add_test(NAME cli_verify_unicode_path
    COMMAND ${TOOL} verify-wav --file "${T}/out/${UNICODE_NAME} out.wav" --rate 48000 --channels 1 --frames 24000 --format float32)

# Block size must not change the output (hosts use arbitrary block sizes) - full default chain.
add_test(NAME cli_process_block_64
    COMMAND ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/out/block64.wav" --block 64 --set expander.lookaheadMs=3 --format float32 --overwrite)
add_test(NAME cli_process_block_4096
    COMMAND ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/out/block4096.wav" --block 4096 --set expander.lookaheadMs=3 --format float32 --overwrite)
add_test(NAME cli_compare_block_sizes
    COMMAND ${TOOL} compare-wav --a "${T}/out/block64.wav" --b "${T}/out/block4096.wav" --tol 1e-6)

# Factory balanced preset actually processes: a -6 dBFS tone is compressed (-24 dBFS thr, 2.5:1).
add_test(NAME cli_process_factory_balanced
    COMMAND ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/out/balanced.wav" --preset "${FACTORY}/Broadcast Voice - Balanced.radvoice.json" --overwrite --verbose)
add_test(NAME cli_verify_factory_balanced
    COMMAND ${TOOL} verify-wav --file "${T}/out/balanced.wav" --rate 48000 --channels 1 --frames 72000 --format pcm24 --peak-db -15.0 --tol-db 0.3 --from-seconds 0.5)

# Transactional output: an existing output must be preserved when processing fails,
# and never replaced without --overwrite.
add_test(NAME cli_setup_existing_output
    COMMAND ${TOOL} gen-wav --out "${T}/out/preserve.wav" --rate 8000 --channels 1 --seconds 0.1 --format pcm16 --freq 100 --amp 0.1)
add_test(NAME cli_fail_leaves_existing_output
    COMMAND ${TOOL} expect-exit 3 -- ${CLI} --input "${T}/does-not-exist.wav" --output "${T}/out/preserve.wav" --overwrite)
add_test(NAME cli_no_overwrite_exit_6
    COMMAND ${TOOL} expect-exit 6 -- ${CLI} --input "${T}/sine_mono_48k.wav" --output "${T}/out/preserve.wav")
add_test(NAME cli_verify_existing_output_preserved
    COMMAND ${TOOL} verify-wav --file "${T}/out/preserve.wav" --rate 8000 --channels 1 --frames 800 --format pcm16)

set_tests_properties(
    cli_exit_code_bad_set cli_exit_code_input cli_exit_code_preset cli_exit_code_same_file
    cli_process_mono_preset cli_process_stereo_override cli_process_unicode_path
    cli_process_block_64 cli_process_block_4096 cli_process_factory_balanced cli_setup_existing_output
    PROPERTIES FIXTURES_REQUIRED cli_inputs)
set_tests_properties(cli_verify_mono_preset      PROPERTIES DEPENDS cli_process_mono_preset)
set_tests_properties(cli_verify_stereo_override  PROPERTIES DEPENDS cli_process_stereo_override)
set_tests_properties(cli_verify_unicode_path     PROPERTIES DEPENDS cli_process_unicode_path)
set_tests_properties(cli_compare_block_sizes     PROPERTIES DEPENDS "cli_process_block_64;cli_process_block_4096")
set_tests_properties(cli_verify_factory_balanced PROPERTIES DEPENDS cli_process_factory_balanced)
set_tests_properties(cli_fail_leaves_existing_output PROPERTIES DEPENDS cli_setup_existing_output)
set_tests_properties(cli_no_overwrite_exit_6     PROPERTIES DEPENDS cli_fail_leaves_existing_output)
set_tests_properties(cli_verify_existing_output_preserved PROPERTIES DEPENDS cli_no_overwrite_exit_6)

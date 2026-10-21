#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace radiff::platform {

/// UTF-8 <-> std::filesystem::path conversions that are correct on every OS.
/// On Windows path::native() is UTF-16; on POSIX it is bytes (assumed UTF-8).
/// All RaDiff code passes paths around as UTF-8 std::string or std::filesystem::path and
/// never as char* with an unknown encoding.
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view utf8);
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

/// Returns an absolute, lexically normalised path. Does not require the path to exist.
/// Relative paths are resolved against the current working directory, which the CLI
/// treats as unknown - callers should prefer absolute paths.
[[nodiscard]] std::filesystem::path absoluteNormalized(const std::filesystem::path& path);

/// Per-user application data directory for RaDiff (presets, settings).
///   macOS:   ~/Library/Application Support/RaDiff Voice Processor
///   Windows: %APPDATA%\RaDiff Voice Processor
///   Linux:   $XDG_DATA_HOME/radiff-voice-processor (dev/CI convenience only)
/// The directory is NOT created by this call.
[[nodiscard]] std::filesystem::path userDataDirectory();

/// Temporary directory suitable for scratch files.
[[nodiscard]] std::filesystem::path temporaryDirectory();

} // namespace radiff::platform

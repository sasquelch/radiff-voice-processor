#pragma once

#include <string>
#include <vector>

namespace radiff::platform {

/// Returns the process command-line arguments as UTF-8 strings regardless of OS.
/// On Windows this ignores argv and re-reads the wide command line so that
/// non-ASCII paths survive. On POSIX argv is already bytes (UTF-8 on macOS).
[[nodiscard]] std::vector<std::string> commandLineUtf8(int argc, char** argv);

/// Prepares the console for UTF-8 output (sets the Windows console code page). No-op elsewhere.
void prepareConsoleUtf8() noexcept;

} // namespace radiff::platform

namespace radiff::platform {

/// Runs a child process synchronously and returns its exit code, or -1 if it could not
/// be started. `args[0]` is the executable path; all strings are UTF-8. The child
/// inherits stdin/stdout/stderr.
[[nodiscard]] int runProcess(const std::vector<std::string>& args);

} // namespace radiff::platform

#include "radiff/platform/Paths.h"

#include <cstdlib>

namespace radiff::platform {

std::filesystem::path userDataDirectory() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "radiff-voice-processor";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local" / "share" / "radiff-voice-processor";
    return std::filesystem::temp_directory_path() / "radiff-voice-processor";
}

std::filesystem::path temporaryDirectory() { return std::filesystem::temp_directory_path(); }

} // namespace radiff::platform

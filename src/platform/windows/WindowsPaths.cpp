#include "radiff/platform/Paths.h"

#include <windows.h>
#include <shlobj.h>

namespace radiff::platform {

std::filesystem::path userDataDirectory() {
    PWSTR p = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &p)) && p)
        result = std::filesystem::path(p) / L"RaDiff Voice Processor";
    else
        result = std::filesystem::temp_directory_path() / L"RaDiff Voice Processor";
    if (p)
        ::CoTaskMemFree(p);
    return result;
}

std::filesystem::path temporaryDirectory() { return std::filesystem::temp_directory_path(); }

} // namespace radiff::platform

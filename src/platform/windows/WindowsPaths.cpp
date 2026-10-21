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

std::filesystem::path bundledResourceDirectory() {
    // Long-path aware: grow the buffer until the full module path fits.
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return {};
        if (n < buf.size() - 1) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path temporaryDirectory() { return std::filesystem::temp_directory_path(); }

} // namespace radiff::platform

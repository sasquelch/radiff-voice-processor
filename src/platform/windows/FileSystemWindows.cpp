#include "radiff/platform/FileSystem.h"
#include "radiff/platform/Paths.h"

#include <random>
#include <string>

#include <windows.h>

namespace radiff::platform {

namespace {

std::string lastErrorMessage(DWORD err) {
    LPWSTR buffer = nullptr;
    const DWORD len = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                       reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string msg;
    if (len > 0 && buffer) {
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
        msg.resize(static_cast<std::size_t>(needed));
        ::WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(len), msg.data(), needed, nullptr, nullptr);
        ::LocalFree(buffer);
        while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n'))
            msg.pop_back();
    }
    return msg;
}

FsResult winResult(const char* what, const std::filesystem::path& p) {
    const DWORD err = ::GetLastError();
    return FsResult::failure(std::error_code(static_cast<int>(err), std::system_category()),
                             std::string(what) + " failed for '" + pathToUtf8(p) + "': " + lastErrorMessage(err));
}

} // namespace

FsResult flushToDisk(const std::filesystem::path& file) {
    HANDLE h = ::CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return winResult("CreateFileW", file);
    const BOOL ok = ::FlushFileBuffers(h);
    const FsResult r = ok ? FsResult::success() : winResult("FlushFileBuffers", file);
    ::CloseHandle(h);
    return r;
}

FsResult atomicReplace(const std::filesystem::path& source, const std::filesystem::path& destination) {
    // Clear the hidden attribute on the temp file so the final file is visible.
    ::SetFileAttributesW(source.c_str(), FILE_ATTRIBUTE_NORMAL);

    const DWORD destAttrs = ::GetFileAttributesW(destination.c_str());
    if (destAttrs != INVALID_FILE_ATTRIBUTES) {
        if (::ReplaceFileW(destination.c_str(), source.c_str(), nullptr,
                           REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                           nullptr, nullptr))
            return FsResult::success();
        // Fall through to MoveFileExW (e.g. destination on a filesystem without ReplaceFile support).
    }
    if (::MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return FsResult::success();
    return winResult("MoveFileExW", destination);
}

std::filesystem::path makeSiblingTempPath(const std::filesystem::path& finalPath) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    const auto token = rng();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(token));
    const auto name = std::string(".") + pathToUtf8(finalPath.filename()) + ".radiff-tmp-" + buf;
    return finalPath.parent_path() / pathFromUtf8(name);
}

void setHidden(const std::filesystem::path& file) noexcept {
    const DWORD attrs = ::GetFileAttributesW(file.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
        ::SetFileAttributesW(file.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
}

} // namespace radiff::platform

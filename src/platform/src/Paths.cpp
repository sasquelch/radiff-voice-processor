#include "radiff/platform/Paths.h"

namespace radiff::platform {

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    // std::u8string guarantees the UTF-8 -> native conversion on Windows (UTF-16) and
    // is a byte-copy on POSIX.
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path absoluteNormalized(const std::filesystem::path& path) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec)
        abs = path;
    return abs.lexically_normal();
}

} // namespace radiff::platform

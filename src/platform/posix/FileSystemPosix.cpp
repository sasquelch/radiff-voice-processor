#include "radiff/platform/FileSystem.h"
#include "radiff/platform/Paths.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <random>
#include <unistd.h>

namespace radiff::platform {

namespace {

FsResult errnoResult(const char* what, const std::filesystem::path& p) {
    const int err = errno;
    return FsResult::failure(std::error_code(err, std::generic_category()),
                             std::string(what) + " failed for '" + pathToUtf8(p) + "': " + std::strerror(err));
}

FsResult fsyncPath(const std::filesystem::path& p, int openFlags) {
    const int fd = ::open(p.c_str(), openFlags);
    if (fd < 0)
        return errnoResult("open", p);
#if defined(__APPLE__)
    // On macOS fsync() only flushes to the drive cache; F_FULLFSYNC asks the drive to
    // commit to stable storage, which is what "transactional" output needs.
    if (::fcntl(fd, F_FULLFSYNC) != 0 && ::fsync(fd) != 0) {
#else
    if (::fsync(fd) != 0) {
#endif
        auto r = errnoResult("fsync", p);
        ::close(fd);
        return r;
    }
    ::close(fd);
    return FsResult::success();
}

} // namespace

FsResult flushToDisk(const std::filesystem::path& file) { return fsyncPath(file, O_RDONLY); }

FsResult atomicReplace(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (::rename(source.c_str(), destination.c_str()) != 0)
        return errnoResult("rename", destination);
    // Make the directory entry durable too.
    auto dir = destination.parent_path();
    if (dir.empty())
        dir = ".";
    return fsyncPath(dir, O_RDONLY);
}

std::filesystem::path makeSiblingTempPath(const std::filesystem::path& finalPath) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    const auto token = rng();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(token));
    const auto name = std::string(".") + pathToUtf8(finalPath.filename()) + ".radiff-tmp-" + buf;
    return finalPath.parent_path() / pathFromUtf8(name);
}

void setHidden(const std::filesystem::path&) noexcept {
    // Leading-dot names are already hidden on POSIX.
}

} // namespace radiff::platform

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

namespace radiff::platform {

/// Result of a filesystem operation. `ok()` is true on success; otherwise `message`
/// holds a human-readable, UTF-8 description suitable for logs and the UI.
struct FsResult {
    std::error_code code{};
    std::string message;
    [[nodiscard]] bool ok() const noexcept { return !code; }
    static FsResult success() { return {}; }
    static FsResult failure(std::error_code ec, std::string msg) { return {ec, std::move(msg)}; }
};

/// Flushes a file's contents and metadata to stable storage (fsync / FlushFileBuffers).
FsResult flushToDisk(const std::filesystem::path& file);

/// Atomically replaces `destination` with `source` (both must be on the same volume).
///   POSIX/macOS: rename(2), which is atomic on APFS/HFS+; the directory is fsynced after.
///   Windows:     ReplaceFileW when the destination exists (keeps ACLs/attributes),
///                otherwise MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH.
/// On failure `source` is left in place so the caller can clean up or retry.
FsResult atomicReplace(const std::filesystem::path& source, const std::filesystem::path& destination);

/// Returns a unique temporary file path in the same directory as `finalPath`, so that
/// atomicReplace() is guaranteed to be a same-volume operation. The file is not created.
/// Pattern: ".<name>.radiff-tmp-<random>" (hidden on POSIX; marked hidden on Windows by
/// TransactionalFileWriter).
[[nodiscard]] std::filesystem::path makeSiblingTempPath(const std::filesystem::path& finalPath);

/// Marks a file as hidden where the OS supports the concept (Windows attribute). No-op elsewhere.
void setHidden(const std::filesystem::path& file) noexcept;

} // namespace radiff::platform

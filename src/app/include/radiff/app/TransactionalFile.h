#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace radiff::app {

/// Writes a file so that the final path either contains the complete new content or
/// is untouched - never a partial file. Sequence:
///   1. write to a hidden temp file in the destination directory
///   2. flush to stable storage
///   3. caller-supplied validation of the temp file
///   4. atomic replace (rename / ReplaceFileW)
/// If any step fails the temp file is removed and the original (if any) is preserved.
struct TransactionalWriteResult {
    std::string error; ///< empty = success
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

using WriteFn = std::function<std::string(const std::filesystem::path& tempPath)>;    ///< returns error or ""
using ValidateFn = std::function<std::string(const std::filesystem::path& tempPath)>; ///< returns error or ""

TransactionalWriteResult writeFileTransactionally(const std::filesystem::path& finalPath, const WriteFn& write,
                                                  const ValidateFn& validate);

} // namespace radiff::app

#include "radiff/app/TransactionalFile.h"
#include "radiff/platform/FileSystem.h"
#include "radiff/platform/Paths.h"

namespace radiff::app {

TransactionalWriteResult writeFileTransactionally(const std::filesystem::path& finalPath, const WriteFn& write,
                                                  const ValidateFn& validate) {
    TransactionalWriteResult result;
    std::error_code ec;

    const auto dir = finalPath.parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            result.error = "Cannot create output directory '" + platform::pathToUtf8(dir) + "': " + ec.message();
            return result;
        }
    }
    if (std::filesystem::is_directory(finalPath, ec)) {
        result.error = "Output path is a directory: " + platform::pathToUtf8(finalPath);
        return result;
    }

    const auto temp = platform::makeSiblingTempPath(finalPath);
    auto cleanup = [&] { std::filesystem::remove(temp, ec); };

    if (auto err = write(temp); !err.empty()) {
        cleanup();
        result.error = err;
        return result;
    }
    platform::setHidden(temp);

    if (auto fs = platform::flushToDisk(temp); !fs.ok()) {
        cleanup();
        result.error = fs.message;
        return result;
    }
    if (validate) {
        if (auto err = validate(temp); !err.empty()) {
            cleanup();
            result.error = "Output validation failed: " + err;
            return result;
        }
    }
    if (auto fs = platform::atomicReplace(temp, finalPath); !fs.ok()) {
        cleanup();
        result.error = fs.message;
        return result;
    }
    return result;
}

} // namespace radiff::app

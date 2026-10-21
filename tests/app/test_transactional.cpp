#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "radiff/app/TransactionalFile.h"
#include "radiff/platform/Paths.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace radiff;

namespace {
std::filesystem::path tmpDir() {
    auto d = platform::pathFromUtf8(RADIFF_TEST_TMP_DIR) / "transactional";
    std::filesystem::create_directories(d);
    return d;
}
std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
std::size_t leftovers(const std::filesystem::path& dir) {
    std::size_t n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (platform::pathToUtf8(e.path().filename()).find("radiff-tmp") != std::string::npos)
            ++n;
    return n;
}
} // namespace

TEST_CASE("successful write replaces previous content atomically") {
    const auto target = tmpDir() / "a.txt";
    { std::ofstream(target) << "old"; }
    auto r = app::writeFileTransactionally(
        target, [](const std::filesystem::path& t) { std::ofstream(t) << "new"; return std::string{}; },
        [](const std::filesystem::path& t) { return slurp(t) == "new" ? std::string{} : "bad"; });
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(slurp(target) == "new");
    CHECK(leftovers(tmpDir()) == 0);
}

TEST_CASE("failed validation preserves the original and removes the temp file") {
    const auto target = tmpDir() / "b.txt";
    { std::ofstream(target) << "keep me"; }
    auto r = app::writeFileTransactionally(
        target, [](const std::filesystem::path& t) { std::ofstream(t) << "broken"; return std::string{}; },
        [](const std::filesystem::path&) { return std::string("corrupt"); });
    CHECK_FALSE(r.ok());
    CHECK(r.error.find("corrupt") != std::string::npos);
    CHECK(slurp(target) == "keep me");
    CHECK(leftovers(tmpDir()) == 0);
}

TEST_CASE("failed write preserves the original") {
    const auto target = tmpDir() / "c.txt";
    { std::ofstream(target) << "keep me"; }
    auto r = app::writeFileTransactionally(
        target, [](const std::filesystem::path&) { return std::string("disk full"); }, {});
    CHECK_FALSE(r.ok());
    CHECK(slurp(target) == "keep me");
    CHECK(leftovers(tmpDir()) == 0);
}

TEST_CASE("creates missing parent directories and refuses directories as targets") {
    const auto target = tmpDir() / "nested" / "deeper" / "d.txt";
    auto r = app::writeFileTransactionally(
        target, [](const std::filesystem::path& t) { std::ofstream(t) << "x"; return std::string{}; }, {});
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(slurp(target) == "x");

    auto r2 = app::writeFileTransactionally(tmpDir() / "nested", [](const std::filesystem::path&) { return std::string{}; }, {});
    CHECK_FALSE(r2.ok());
}

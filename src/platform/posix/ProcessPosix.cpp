#include "radiff/platform/Process.h"

namespace radiff::platform {

std::vector<std::string> commandLineUtf8(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc : 0));
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i] ? argv[i] : "");
    return args;
}

void prepareConsoleUtf8() noexcept {}

} // namespace radiff::platform

#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

namespace radiff::platform {

int runProcess(const std::vector<std::string>& args) {
    if (args.empty())
        return -1;
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    if (::posix_spawn(&pid, args[0].c_str(), nullptr, nullptr, argv.data(), environ) != 0)
        return -1;
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

} // namespace radiff::platform

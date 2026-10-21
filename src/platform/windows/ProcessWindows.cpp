#include "radiff/platform/Process.h"

#include <windows.h>
#include <shellapi.h>

namespace radiff::platform {

std::vector<std::string> commandLineUtf8(int, char**) {
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!wargv)
        return args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s;
        if (needed > 1) {
            s.resize(static_cast<std::size_t>(needed - 1));
            ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), needed, nullptr, nullptr);
        }
        args.push_back(std::move(s));
    }
    ::LocalFree(wargv);
    return args;
}

void prepareConsoleUtf8() noexcept {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
}

} // namespace radiff::platform

namespace radiff::platform {

namespace {

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty())
        return {};
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), needed);
    return w;
}

// Quote one argument following the MSVC CRT parsing rules so CommandLineToArgvW
// in the child reproduces it exactly (handles spaces, quotes and backslashes).
void appendQuoted(std::wstring& cmd, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += *it;
        } else {
            cmd.append(backslashes, L'\\');
            cmd += *it;
        }
    }
    cmd += L'"';
}

} // namespace

int runProcess(const std::vector<std::string>& args) {
    if (args.empty())
        return -1;
    std::wstring cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i)
            cmd += L' ';
        appendQuoted(cmd, toWide(args[i]));
    }
    const std::wstring exe = toWide(args[0]);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // CreateProcessW may modify the command line buffer, so pass a writable copy.
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (!::CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
        return -1;
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    const BOOL ok = ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return ok ? static_cast<int>(code) : -1;
}

} // namespace radiff::platform

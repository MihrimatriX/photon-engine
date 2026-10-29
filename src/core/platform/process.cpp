// process.cpp — runProcess: POSIX (fork/execvp, poll) ve Windows (CreateProcessW, okuma thread'i).
#include "core/platform/process.h"

#include <algorithm>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace photon {

namespace {
constexpr size_t kMaxOutput = 1 << 20;
}

std::string quoteWindowsArgument(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            out.append(backslashes * 2 + 1, '\\'); // tırnaktan önceki ters bölüler ikiye katlanır
            out.push_back('"');
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            out.push_back(c);
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, '\\'); // kapanış tırnağından önce
    out.push_back('"');
    return out;
}

#if defined(_WIN32)

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
} // namespace

ProcessResult runProcess(const std::vector<std::string>& argv, int timeoutSeconds) {
    ProcessResult r;
    if (argv.empty()) return r;
    std::string cmd;
    for (const std::string& a : argv) cmd += (cmd.empty() ? "" : " ") + quoteWindowsArgument(a);
    std::wstring wcmd = widen(cmd);

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return r;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    const std::wstring app = widen(argv[0]);
    const BOOL ok = CreateProcessW(app.c_str(), wcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return r;
    }
    r.started = true;
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0)
            if (r.output.size() < kMaxOutput) r.output.append(buf, n);
    });
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutSeconds > 0 ? static_cast<DWORD>(timeoutSeconds) * 1000 : INFINITE);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        r.timedOut = true;
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    reader.join();
    CloseHandle(readPipe);
    return r;
}

#else

ProcessResult runProcess(const std::vector<std::string>& argv, int timeoutSeconds) {
    ProcessResult r;
    if (argv.empty()) return r;
    int fds[2];
    if (::pipe(fds) != 0) return r;
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return r;
    }
    if (pid == 0) {
        // Çocuk: kendi süreç grubunda (süre aşımında alt süreçleriyle birlikte sonlandırılır);
        // stdout+stderr boruya, stdin kapalı; kabuksuz execvp.
        ::setpgid(0, 0);
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        const int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0) ::dup2(devnull, STDIN_FILENO);
        ::execvp(args[0], args.data());
        ::_exit(127);
    }
    ::close(fds[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds > 0 ? timeoutSeconds : 1 << 30);
    char buf[4096];
    bool open = true;
    while (open) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            ::kill(-pid, SIGKILL); // tüm süreç grubu (DCC'nin başlattığı alt süreçler dahil)
            ::kill(pid, SIGKILL);
            r.timedOut = true;
            break;
        }
        pollfd p{fds[0], POLLIN, 0};
        const int pr = ::poll(&p, 1, static_cast<int>(std::min<long long>(left, 200)));
        if (pr < 0 && errno != EINTR) break;
        if (pr > 0) {
            const ssize_t n = ::read(fds[0], buf, sizeof(buf));
            if (n <= 0) open = false;
            else if (r.output.size() < kMaxOutput) r.output.append(buf, static_cast<size_t>(n));
        }
    }
    ::close(fds[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status)) {
        r.exitCode = WEXITSTATUS(status);
        r.started = r.exitCode != 127 || !r.output.empty();
    } else {
        r.started = true;
    }
    return r;
}

#endif

} // namespace photon

#include "platform.hpp"

#include <cstdlib>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace platform {

fs::path executableDir() {
    std::error_code ec;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n > 0) return fs::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) return fs::canonical(buf, ec).parent_path();
#else
    fs::path p = fs::canonical("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return fs::current_path(ec);
}

fs::path homeDir() {
#if defined(_WIN32)
    if (const char* p = std::getenv("USERPROFILE")) return p;
#endif
    if (const char* p = std::getenv("HOME")) return p;
    return ".";
}

fs::path cacheDir() {
#if defined(_WIN32)
    if (const char* p = std::getenv("LOCALAPPDATA")) return p;
    return homeDir() / "AppData" / "Local";
#elif defined(__APPLE__)
    return homeDir() / "Library" / "Caches";
#else
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    return xdg && *xdg ? fs::path(xdg) : homeDir() / ".cache";
#endif
}

fs::path dataDir() {
#if defined(_WIN32)
    if (const char* p = std::getenv("APPDATA")) return p;
    return homeDir() / "AppData" / "Roaming";
#elif defined(__APPLE__)
    return homeDir() / "Library" / "Application Support";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    return xdg && *xdg ? fs::path(xdg) : homeDir() / ".local" / "share";
#endif
}

std::string findExecutable(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (!path) return "";
#if defined(_WIN32)
    const char sep = ';';
    const std::vector<std::string> exts = {".exe", ".cmd", ".bat", ""};
#else
    const char sep = ':';
    const std::vector<std::string> exts = {""};
#endif
    std::string p(path);
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find(sep, start);
        if (end == std::string::npos) end = p.size();
        std::string dir = p.substr(start, end - start);
        if (!dir.empty()) {
            for (const auto& ext : exts) {
                std::error_code ec;
                fs::path cand = fs::path(dir) / (name + ext);
                if (fs::is_regular_file(cand, ec)) return cand.string();
            }
        }
        start = end + 1;
    }
    return "";
}

std::string quoteArg(const std::string& arg) {
#if defined(_WIN32)
    std::string r = "\"";
    for (char c : arg) r += c == '"' ? std::string("\\\"") : std::string(1, c);
    return r + "\"";
#else
    std::string r = "'";
    for (char c : arg) r += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return r + "'";
#endif
}

#if defined(_WIN32)
// cmd.exe strips one pair of outer quotes: wrap the whole line so quoted arguments survive.
static std::string wrap(const std::string& cmd) { return "\"" + cmd + "\""; }
static const char* NUL_OUT = " >NUL 2>&1";
static const char* NUL_ERR = " 2>NUL";
#else
static std::string wrap(const std::string& cmd) { return cmd; }
static const char* NUL_OUT = " >/dev/null 2>&1";
static const char* NUL_ERR = " 2>/dev/null";
#endif

int run(const std::string& cmd) { return std::system(wrap(cmd + NUL_OUT).c_str()); }

FILE* openRead(const std::string& cmd) {
#if defined(_WIN32)
    return _popen(wrap(cmd + NUL_ERR).c_str(), "r");
#else
    return popen((cmd + NUL_ERR).c_str(), "r");
#endif
}

FILE* openWrite(const std::string& cmd) {
#if defined(_WIN32)
    return _popen(wrap(cmd).c_str(), "wb");
#else
    return popen(cmd.c_str(), "w");
#endif
}

int closeRead(FILE* f) {
#if defined(_WIN32)
    return _pclose(f);
#else
    return pclose(f);
#endif
}

void lineBufferStdout() {
#if defined(_WIN32)
    std::setvbuf(stdout, nullptr, _IONBF, 0); // MSVC has no line buffering (and rejects size 0)
#else
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
}

} // namespace platform

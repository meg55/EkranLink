#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#include <windows.h>

namespace ekran {

namespace {
std::mutex g_mutex;
std::function<void(const std::string&)> g_sink;
}  // namespace

void setLogSink(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

bool logToFile(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    const bool big = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 1 << 20;
    FILE* f = _wfopen(path.c_str(), big ? L"w" : L"a");
    if (!f) return false;
    setLogSink([f](const std::string& s) {
        std::time_t t = std::time(nullptr);
        char ts[32];
        std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        std::fprintf(f, "%s  %s\n", ts, s.c_str());
        std::fflush(f);
    });
    return true;
}

void logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_sink) {
        g_sink(buf);
    } else {
        std::fputs(buf, stderr);
        std::fputc('\n', stderr);
    }
}

}  // namespace ekran

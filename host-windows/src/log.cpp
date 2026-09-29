#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace ekran {

namespace {
std::mutex g_mutex;
std::function<void(const std::string&)> g_sink;
}  // namespace

void setLogSink(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
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

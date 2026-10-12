#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#include <fcntl.h>
#include <io.h>
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

namespace {

// Günlüğü bağ izlemeden açar. Hizmet ve ajan SYSTEM olarak yazar (ve 1 MB'ta keser): günlüğün yerine konmuş
// sembolik bağ, bağlantı noktası ya da sabit bağ başka bir dosyayı yazdırırdı. Öyleyse bağ silinip yenisi açılır.
HANDLE openLogFile(const std::wstring& path) {
    for (DWORD disp : {DWORD(OPEN_ALWAYS), DWORD(CREATE_NEW)}) {
        HANDLE h = CreateFileW(path.c_str(), FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, disp, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION fi{};
        if (h != INVALID_HANDLE_VALUE && GetFileInformationByHandle(h, &fi) &&
            !(fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && fi.nNumberOfLinks == 1) {
            if (fi.nFileSizeHigh || fi.nFileSizeLow > 1 << 20) SetEndOfFile(h);  // baştan başla
            return h;
        }
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        const DWORD a = GetFileAttributesW(path.c_str());
        if (disp == CREATE_NEW || a == INVALID_FILE_ATTRIBUTES) break;
        if (h == INVALID_HANDLE_VALUE && !(a & FILE_ATTRIBUTE_REPARSE_POINT)) break;  // ör. izin yok
        // Bağın kendisi silinir (DeleteFileW / RemoveDirectoryW bağı izlemez)
        if (a & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path.c_str());
        else DeleteFileW(path.c_str());
    }
    return INVALID_HANDLE_VALUE;
}

}  // namespace

bool logToFile(const std::wstring& path) {
    HANDLE h = openLogFile(path);
    if (h == INVALID_HANDLE_VALUE) return false;
    const int fd = _open_osfhandle(intptr_t(h), _O_APPEND | _O_TEXT);
    if (fd == -1) {
        CloseHandle(h);
        return false;
    }
    FILE* f = _fdopen(fd, "a");
    if (!f) {
        _close(fd);
        return false;
    }
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

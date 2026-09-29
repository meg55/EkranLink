// Günlük: konsol sürümünde stderr'e, tepsi sürümünde dosyaya yazılır.
#pragma once

#include <functional>
#include <string>

namespace ekran {

void setLogSink(std::function<void(const std::string&)> sink);
// Günlüğü dosyaya yönlendir (zaman damgalı; 1 MB'ı geçince baştan başlar)
bool logToFile(const std::wstring& path);
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace ekran

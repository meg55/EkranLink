// Günlük: konsol sürümünde stderr'e, tepsi sürümünde dosyaya yazılır.
#pragma once

#include <functional>
#include <string>

namespace ekran {

void setLogSink(std::function<void(const std::string&)> sink);
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace ekran

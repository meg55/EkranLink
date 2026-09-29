// Küçük yardımcılar: COM akıllı işaretçisi, HRESULT kontrolü, zaman.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <utility>

#include "log.h"

namespace ekran {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ~ComPtr() { reset(); }
    ComPtr& operator=(ComPtr o) noexcept { std::swap(p_, o.p_); return *this; }

    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** put() { reset(); return &p_; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }

    template <typename U>
    HRESULT as(ComPtr<U>& out) const {
        return p_ ? p_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out.put())) : E_POINTER;
    }

private:
    T* p_ = nullptr;
};

// Başarısızsa mesaj yazar ve false döner.
inline bool check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        logf("HATA: %s (0x%08lx)", what, static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

// Monoton saat, mikrosaniye
inline int64_t nowUs() {
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart / freq * 1000000 + (c.QuadPart % freq) * 1000000 / freq;
}

}  // namespace ekran

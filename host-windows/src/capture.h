// DXGI Desktop Duplication ile ekran yakalama. Görüntü GPU'da kalır; imleç ayrı raporlanır.
#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <string>
#include <vector>

#include "com.h"
#include "orientation.h"

namespace ekran {

struct GpuDevice {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutput1> output;
    LUID adapterLuid{};
    unsigned vendorId = 0;  // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
    RECT desktopRect{};  // çıkışın sanal masaüstündeki yeri (fiziksel piksel)
};

// outputIndex: tüm ekran kartlarındaki ekranlar sırasıyla 0, 1, 2...
bool createDeviceForOutput(int outputIndex, GpuDevice& out);
// Ada göre (\\.\DISPLAYn) — sanal monitör için
bool createDeviceForOutputName(const std::wstring& gdiName, GpuDevice& out);

struct CursorState {
    bool visible = false;
    int x = 0, y = 0;  // imleç resminin sol üstü, çıkışın masaüstüne göre (döndürülmemiş)
    bool posChanged = false;
    bool shapeChanged = false;
    int w = 0, h = 0, hotX = 0, hotY = 0;
    std::vector<uint32_t> argb;  // masaüstü yönünde
};

class DesktopCapture {
public:
    enum class Result { Timeout, Updated, Lost, Error };

    bool init(GpuDevice& gpu);
    // En fazla timeoutMs bekler. Yeni görüntü gelirse `desk` dokusuna kopyalar ve imageChanged=true yapar.
    Result acquire(unsigned timeoutMs, bool& imageChanged, CursorState& cursor);

    // Gelen görüntünün boyutu (panel yönü; döndürülmüş ekranda masaüstünün tersi)
    int width() const { return width_; }
    int height() const { return height_; }
    const Orientation& orientation() const { return orient_; }
    ID3D11Texture2D* desk() const { return desk_.get(); }

private:
    void updateCursorShape(const DXGI_OUTDUPL_FRAME_INFO& info, CursorState& cursor);

    GpuDevice* gpu_ = nullptr;
    ComPtr<IDXGIOutputDuplication> dup_;
    ComPtr<ID3D11Texture2D> desk_;
    std::vector<uint8_t> shapeBuf_;
    int width_ = 0, height_ = 0;
    Orientation orient_;
};

}  // namespace ekran

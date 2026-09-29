#include "capture.h"
#include "log.h"

#include <cstdio>

namespace ekran {

namespace {

bool createDevice(int outputIndex, const std::wstring* gdiName, GpuDevice& out) {
    ComPtr<IDXGIFactory1> factory;
    if (!check(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put())),
               "CreateDXGIFactory1"))
        return false;

    int seen = 0;
    for (UINT a = 0;; a++) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, adapter.put()) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT o = 0;; o++) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, output.put()) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC od;
            output->GetDesc(&od);
            const int index = seen++;
            if (gdiName ? _wcsicmp(od.DeviceName, gdiName->c_str()) != 0 : index != outputIndex) continue;

            DXGI_ADAPTER_DESC1 ad;
            adapter->GetDesc1(&ad);
            logf("Ekran %d: %ls  (%ld,%ld %ldx%ld) — %ls", index, od.DeviceName,
                        od.DesktopCoordinates.left, od.DesktopCoordinates.top,
                        od.DesktopCoordinates.right - od.DesktopCoordinates.left,
                        od.DesktopCoordinates.bottom - od.DesktopCoordinates.top, ad.Description);

            const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
            // VIDEO_SUPPORT: GPU'nun video işlemcisi (ölçekleme/renk dönüşümü) ve kodlayıcı için
            // PREVENT_INTERNAL_THREADING_OPTIMIZATIONS: sürücünün kendi iş parçacığı havuzunu ve tamponlarını kapatır;
            // kare başına birkaç çağrı yapan bu iş yükü için gereksiz, bellek ve iş parçacığı tasarrufu sağlar.
            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT |
                         D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
            HRESULT hr = D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels,
                                           ARRAYSIZE(levels), D3D11_SDK_VERSION, out.device.put(), nullptr,
                                           out.context.put());
            if (!check(hr, "D3D11CreateDevice")) return false;

            // Kodlayıcı cihazı başka iş parçacığından da kullanır.
            ComPtr<ID3D10Multithread> mt;
            if (SUCCEEDED(out.device.as(mt))) mt->SetMultithreadProtected(TRUE);

            if (!check(output.as(out.output), "IDXGIOutput1")) return false;
            out.adapterLuid = ad.AdapterLuid;
            out.vendorId = ad.VendorId;
            out.desktopRect = od.DesktopCoordinates;
            return true;
        }
    }
    if (gdiName) logf("HATA: %ls ekranı bulunamadı", gdiName->c_str());
    else logf("HATA: %d numaralı ekran bulunamadı (toplam %d)", outputIndex, seen);
    return false;
}

}  // namespace

bool createDeviceForOutput(int outputIndex, GpuDevice& out) { return createDevice(outputIndex, nullptr, out); }

bool createDeviceForOutputName(const std::wstring& gdiName, GpuDevice& out) { return createDevice(-1, &gdiName, out); }

bool DesktopCapture::init(GpuDevice& gpu) {
    gpu_ = &gpu;
    dup_.reset();
    HRESULT hr = gpu.output->DuplicateOutput(gpu.device.get(), dup_.put());
    if (!check(hr, "DuplicateOutput")) return false;

    DXGI_OUTDUPL_DESC dd;
    dup_->GetDesc(&dd);
    // ModeDesc masaüstü (döndürülmüş) boyutudur; gelen görüntü ise panel yönündedir (bkz. orientation.h).
    switch (dd.Rotation) {
        case DXGI_MODE_ROTATION_ROTATE90: orient_.rotation = 90; break;
        case DXGI_MODE_ROTATION_ROTATE180: orient_.rotation = 180; break;
        case DXGI_MODE_ROTATION_ROTATE270: orient_.rotation = 270; break;
        default: orient_.rotation = 0; break;
    }
    orient_.deskW = int(dd.ModeDesc.Width);
    orient_.deskH = int(dd.ModeDesc.Height);
    const bool sideways = orient_.rotation == 90 || orient_.rotation == 270;
    width_ = sideways ? orient_.deskH : orient_.deskW;
    height_ = sideways ? orient_.deskW : orient_.deskH;
    // Döndürme ya da ekran yerleşimi değişmiş olabilir: dokunma eşlemesi için masaüstündeki yeri tazele
    DXGI_OUTPUT_DESC od;
    if (SUCCEEDED(gpu.output->GetDesc(&od))) gpu.desktopRect = od.DesktopCoordinates;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width_);
    td.Height = UINT(height_);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desk_.reset();
    return check(gpu.device->CreateTexture2D(&td, nullptr, desk_.put()), "masaüstü dokusu");
}

DesktopCapture::Result DesktopCapture::acquire(unsigned timeoutMs, bool& imageChanged, CursorState& cursor) {
    imageChanged = false;
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> res;
    HRESULT hr = dup_->AcquireNextFrame(timeoutMs, &info, res.put());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return Result::Timeout;
    if (hr == DXGI_ERROR_ACCESS_LOST) return Result::Lost;  // çözünürlük değişti, UAC, tam ekran oyun...
    if (!check(hr, "AcquireNextFrame")) return Result::Error;

    if (info.LastPresentTime.QuadPart != 0 && info.AccumulatedFrames > 0) {
        ComPtr<ID3D11Texture2D> tex;
        if (SUCCEEDED(res.as(tex))) {
            gpu_->context->CopyResource(desk_.get(), tex.get());  // GPU içinde kopya
            imageChanged = true;
        }
    }

    if (info.LastMouseUpdateTime.QuadPart != 0) {
        bool vis = info.PointerPosition.Visible != FALSE;
        int x = info.PointerPosition.Position.x, y = info.PointerPosition.Position.y;
        if (vis != cursor.visible || x != cursor.x || y != cursor.y) {
            cursor.visible = vis;
            cursor.x = x;
            cursor.y = y;
            cursor.posChanged = true;
        }
    }
    if (info.PointerShapeBufferSize > 0) updateCursorShape(info, cursor);

    dup_->ReleaseFrame();
    return Result::Updated;
}

// Windows imleç biçimlerini (tek renk / renkli / maskeli) düz ARGB'ye çevirir.
// XOR (tersine çevir) pikselleri tablette ekran içeriğini bilmediğimiz için siyah kenarlı beyaz çizilir.
void DesktopCapture::updateCursorShape(const DXGI_OUTDUPL_FRAME_INFO& info, CursorState& cursor) {
    shapeBuf_.resize(info.PointerShapeBufferSize);
    UINT needed = 0;
    DXGI_OUTDUPL_POINTER_SHAPE_INFO si{};
    if (FAILED(dup_->GetFramePointerShape(UINT(shapeBuf_.size()), shapeBuf_.data(), &needed, &si))) return;

    const bool mono = si.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
    const int w = int(si.Width);
    const int h = mono ? int(si.Height / 2) : int(si.Height);
    const uint8_t* src = shapeBuf_.data();
    std::vector<uint32_t> out(size_t(w) * h, 0);
    std::vector<uint8_t> inverted(size_t(w) * h, 0);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t px = 0;
            bool inv = false;
            if (mono) {
                const uint8_t bit = uint8_t(0x80 >> (x & 7));
                const bool andBit = src[y * si.Pitch + x / 8] & bit;
                const bool xorBit = src[(y + h) * si.Pitch + x / 8] & bit;
                if (!andBit) px = xorBit ? 0xFFFFFFFF : 0xFF000000;
                else inv = xorBit;
            } else {
                const uint8_t* p = src + y * si.Pitch + x * 4;  // B G R A
                const uint32_t rgb = (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
                if (si.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
                    px = (uint32_t(p[3]) << 24) | rgb;
                } else if (p[3] == 0) {
                    px = 0xFF000000 | rgb;  // maskeli: opak piksel
                } else {
                    inv = rgb != 0;  // maskeli: XOR piksel
                }
            }
            if (inv) px = 0xFFFFFFFF;
            out[size_t(y) * w + x] = px;
            inverted[size_t(y) * w + x] = inv;
        }
    }
    // XOR piksellerin çevresine siyah kenar (açık ve koyu zeminde görünsün)
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (!inverted[size_t(y) * w + x]) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    uint32_t& n = out[size_t(ny) * w + nx];
                    if ((n >> 24) == 0) n = 0xFF000000;
                }
        }
    }

    cursor.w = w;
    cursor.h = h;
    cursor.hotX = int(si.HotSpot.x);
    cursor.hotY = int(si.HotSpot.y);
    cursor.argb = std::move(out);
    cursor.shapeChanged = true;
}

}  // namespace ekran

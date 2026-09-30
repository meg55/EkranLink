// GPU'nun video işlemcisiyle ölçekleme + BGRA→NV12 renk dönüşümü (shader/CPU kullanmadan).
#pragma once

#include <d3d11.h>

#include <vector>

#include "com.h"

namespace ekran {

class Scaler {
public:
    // Kaynak oranı korunur; içerik dstW×dstH içine ortalanır, kalan siyah. rotation: kaynağı saat yönünde
    // bu kadar çevir (0/90/180/270; ekran kartı desteklemiyorsa çevrilmez, rotated() false döner).
    bool init(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* src, int srcW, int srcH,
              int dstW, int dstH, int rotation = 0);
    bool rotated() const { return rotated_; }
    bool process(ID3D11Texture2D* dstNv12);

    RECT content() const { return content_; }

private:
    ComPtr<ID3D11VideoDevice> vdev_;
    ComPtr<ID3D11VideoContext> vctx_;
    ComPtr<ID3D11VideoProcessorEnumerator> enum_;
    ComPtr<ID3D11VideoProcessor> vp_;
    ComPtr<ID3D11VideoProcessorInputView> inView_;
    struct OutView {
        ID3D11Texture2D* tex;
        ComPtr<ID3D11VideoProcessorOutputView> view;
    };
    std::vector<OutView> outViews_;
    RECT content_{};
    bool rotated_ = false;
};

}  // namespace ekran

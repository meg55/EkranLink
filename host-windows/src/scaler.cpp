#include "scaler.h"

namespace ekran {

bool Scaler::init(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* src, int srcW, int srcH,
                  int dstW, int dstH) {
    if (!check(device->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void**>(vdev_.put())),
               "ID3D11VideoDevice"))
        return false;
    if (!check(context->QueryInterface(__uuidof(ID3D11VideoContext), reinterpret_cast<void**>(vctx_.put())),
               "ID3D11VideoContext"))
        return false;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
    cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cd.InputWidth = UINT(srcW);
    cd.InputHeight = UINT(srcH);
    cd.OutputWidth = UINT(dstW);
    cd.OutputHeight = UINT(dstH);
    cd.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
    if (!check(vdev_->CreateVideoProcessorEnumerator(&cd, enum_.put()), "VideoProcessorEnumerator")) return false;
    if (!check(vdev_->CreateVideoProcessor(enum_.get(), 0, vp_.put()), "VideoProcessor")) return false;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd{};
    ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    if (!check(vdev_->CreateVideoProcessorInputView(src, enum_.get(), &ivd, inView_.put()), "VP giriş görünümü"))
        return false;

    // Oranı koru (letterbox). NV12 için çift koordinat.
    int w = dstW, h = dstH;
    if (int64_t(srcW) * dstH > int64_t(dstW) * srcH) h = int(int64_t(dstW) * srcH / srcW);
    else w = int(int64_t(dstH) * srcW / srcH);
    w &= ~1;
    h &= ~1;
    content_.left = ((dstW - w) / 2) & ~1;
    content_.top = ((dstH - h) / 2) & ~1;
    content_.right = content_.left + w;
    content_.bottom = content_.top + h;

    RECT srcRect{0, 0, srcW, srcH};
    RECT dstRect{0, 0, dstW, dstH};
    vctx_->VideoProcessorSetStreamSourceRect(vp_.get(), 0, TRUE, &srcRect);
    vctx_->VideoProcessorSetStreamDestRect(vp_.get(), 0, TRUE, &content_);
    vctx_->VideoProcessorSetOutputTargetRect(vp_.get(), TRUE, &dstRect);
    vctx_->VideoProcessorSetStreamFrameFormat(vp_.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    vctx_->VideoProcessorSetStreamAutoProcessingMode(vp_.get(), 0, FALSE);

    // Giriş: tam aralık RGB. Çıkış: BT.709, sınırlı aralık YCbCr (H.264 varsayılanı).
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE in{};
    in.RGB_Range = 0;
    vctx_->VideoProcessorSetStreamColorSpace(vp_.get(), 0, &in);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE out{};
    out.YCbCr_Matrix = 1;
    out.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    vctx_->VideoProcessorSetOutputColorSpace(vp_.get(), &out);

    D3D11_VIDEO_COLOR black{};
    black.YCbCr.Y = 16.0f / 255.0f;
    black.YCbCr.Cb = 0.5f;
    black.YCbCr.Cr = 0.5f;
    black.YCbCr.A = 1.0f;
    vctx_->VideoProcessorSetOutputBackgroundColor(vp_.get(), TRUE, &black);
    return true;
}

bool Scaler::process(ID3D11Texture2D* dstNv12) {
    ID3D11VideoProcessorOutputView* ov = nullptr;
    for (auto& v : outViews_)
        if (v.tex == dstNv12) ov = v.view.get();
    if (!ov) {
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd{};
        ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        OutView v{dstNv12, {}};
        if (!check(vdev_->CreateVideoProcessorOutputView(dstNv12, enum_.get(), &ovd, v.view.put()),
                   "VP çıkış görünümü"))
            return false;
        ov = v.view.get();
        outViews_.push_back(std::move(v));
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inView_.get();
    return check(vctx_->VideoProcessorBlt(vp_.get(), ov, 0, 1, &stream), "VideoProcessorBlt");
}

}  // namespace ekran

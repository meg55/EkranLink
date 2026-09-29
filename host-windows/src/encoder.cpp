#include "encoder.h"
#include "log.h"

#include <mferror.h>

#include <cstdio>
#include <cstring>

// CODECAPI_* GUID'lerini bu dosyada tanımla.
#include <initguid.h>
#include <codecapi.h>

namespace ekran {

namespace {

bool hasNalType(const std::vector<uint8_t>& d, int type) {
    for (size_t i = 0; i + 3 < d.size(); i++)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1F) == type) return true;
    return false;
}

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

UINT64 pack(UINT32 hi, UINT32 lo) { return (UINT64(hi) << 32) | lo; }

}  // namespace

bool Encoder::init(ID3D11Device* device, unsigned vendorId, int width, int height, int fps, int kbps,
                   OutputFn onOutput) {
    fps_ = fps;
    onOutput_ = std::move(onOutput);
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    MFT_REGISTER_TYPE_INFO inType{MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO outType{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** acts = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                           &inType, &outType, &acts, &count);
    if (!check(hr, "MFTEnumEx") || count == 0) {
        logf("HATA: donanım H.264 kodlayıcı bulunamadı");
        return false;
    }

    // Ekranın bağlı olduğu GPU'nun kodlayıcısını tercih et (ör. "VEN_10DE" = NVIDIA).
    wchar_t want[16];
    swprintf(want, 16, L"VEN_%04X", vendorId);
    UINT32 pick = 0;
    for (UINT32 i = 0; i < count; i++) {
        wchar_t* vendor = nullptr;
        UINT32 len = 0;
        if (SUCCEEDED(acts[i]->GetAllocatedString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, &vendor, &len))) {
            bool match = _wcsicmp(vendor, want) == 0;
            CoTaskMemFree(vendor);
            if (match) { pick = i; break; }
        }
    }
    wchar_t* friendly = nullptr;
    UINT32 flen = 0;
    if (SUCCEEDED(acts[pick]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendly, &flen))) {
        name_ = narrow(friendly);
        CoTaskMemFree(friendly);
    }
    hr = acts[pick]->ActivateObject(__uuidof(IMFTransform), reinterpret_cast<void**>(mft_.put()));
    for (UINT32 i = 0; i < count; i++) acts[i]->Release();
    CoTaskMemFree(acts);
    if (!check(hr, "kodlayıcı etkinleştirme")) return false;

    ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(mft_->GetAttributes(attrs.put()))) {
        attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    }
    if (!check(mft_.as(events_), "IMFMediaEventGenerator (asenkron MFT)")) return false;

    UINT token = 0;
    if (!check(MFCreateDXGIDeviceManager(&token, devManager_.put()), "MFCreateDXGIDeviceManager")) return false;
    if (!check(devManager_->ResetDevice(device, token), "DXGIDeviceManager::ResetDevice")) return false;
    if (!check(mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(devManager_.get())),
               "SET_D3D_MANAGER"))
        return false;

    if (mft_->GetStreamIDs(1, &inId_, 1, &outId_) == E_NOTIMPL) inId_ = outId_ = 0;

    if (SUCCEEDED(mft_.as(codecApi_))) {
        // Düşük gecikmeli VBR: değişmeyen karelere bit harcanmaz (CBR boş karelere de bant doldurur).
        VARIANT rc;
        VariantInit(&rc);
        rc.vt = VT_UI4;
        rc.ulVal = eAVEncCommonRateControlMode_LowDelayVBR;
        if (FAILED(codecApi_->SetValue(&CODECAPI_AVEncCommonRateControlMode, &rc)))
            setCodecApi(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR, "CBR");
        setCodecApi(CODECAPI_AVEncCommonMeanBitRate, UINT32(kbps) * 1000, "bit hızı");
        setCodecApi(CODECAPI_AVEncCommonMaxBitRate, UINT32(kbps) * 1500, "tepe bit hızı");
        setCodecApi(CODECAPI_AVEncMPVGOPSize, UINT32(fps) * 60, "GOP");
        // Baseline profilde B-kare zaten yok; yine de iste (bazı kodlayıcılar reddeder, sorun değil).
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_UI4;
        v.ulVal = 0;
        codecApi_->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &v);
        v.vt = VT_BOOL;
        v.boolVal = VARIANT_TRUE;
        if (FAILED(codecApi_->SetValue(&CODECAPI_AVLowLatencyMode, &v)))
            logf("uyarı: düşük gecikme modu ayarlanamadı");
    }

    ComPtr<IMFMediaType> out;
    MFCreateMediaType(out.put());
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, UINT32(kbps) * 1000);
    out->SetUINT64(MF_MT_FRAME_SIZE, pack(UINT32(width), UINT32(height)));
    out->SetUINT64(MF_MT_FRAME_RATE, pack(UINT32(fps), 1));
    out->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, pack(1, 1));
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base);  // eski çözücüler için
    if (!check(mft_->SetOutputType(outId_, out.get(), 0), "çıkış türü (H.264)")) return false;

    ComPtr<IMFMediaType> in;
    MFCreateMediaType(in.put());
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    in->SetUINT64(MF_MT_FRAME_SIZE, pack(UINT32(width), UINT32(height)));
    in->SetUINT64(MF_MT_FRAME_RATE, pack(UINT32(fps), 1));
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (!check(mft_->SetInputType(inId_, in.get(), 0), "giriş türü (NV12)")) return false;

    ComPtr<IMFMediaType> cur;
    if (SUCCEEDED(mft_->GetOutputCurrentType(outId_, cur.put()))) {
        UINT32 n = 0;
        if (SUCCEEDED(cur->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &n)) && n > 0) {
            seqHeader_.resize(n);
            cur->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, seqHeader_.data(), n, nullptr);
        }
    }

    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    running_ = true;
    thread_ = std::thread([this] { eventLoop(); });
    return true;
}

void Encoder::setCodecApi(const GUID& key, UINT32 value, const char* label) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI4;
    v.ulVal = value;
    if (FAILED(codecApi_->SetValue(&key, &v))) logf("uyarı: kodlayıcı ayarı reddedildi: %s", label);
}

bool Encoder::submit(ID3D11Texture2D* nv12, int64_t ptsUs, bool forceKey) {
    if (needInput_.load() <= 0) return false;

    ComPtr<IMFMediaBuffer> buf;
    if (!check(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), nv12, 0, FALSE, buf.put()),
               "MFCreateDXGISurfaceBuffer"))
        return false;
    ComPtr<IMF2DBuffer> buf2d;
    DWORD len = 0;
    if (SUCCEEDED(buf.as(buf2d)) && SUCCEEDED(buf2d->GetContiguousLength(&len))) buf->SetCurrentLength(len);

    ComPtr<IMFSample> sample;
    MFCreateSample(sample.put());
    sample->AddBuffer(buf.get());
    sample->SetSampleTime(ptsUs * 10);
    sample->SetSampleDuration(10'000'000 / fps_);

    if (forceKey && codecApi_) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_UI4;
        v.ulVal = 1;
        codecApi_->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &v);
    }

    needInput_--;
    return check(mft_->ProcessInput(inId_, sample.get(), 0), "ProcessInput");
}

void Encoder::eventLoop() {
    while (running_) {
        ComPtr<IMFMediaEvent> ev;
        HRESULT hr = events_->GetEvent(0, ev.put());
        if (FAILED(hr)) break;  // MF_E_SHUTDOWN
        MediaEventType type = MEUnknown;
        ev->GetType(&type);
        if (type == METransformNeedInput) {
            needInput_++;
            SetEvent(wake_);
        } else if (type == METransformHaveOutput) {
            drainOutput();
        }
    }
}

void Encoder::drainOutput() {
    MFT_OUTPUT_STREAM_INFO si{};
    mft_->GetOutputStreamInfo(outId_, &si);
    const bool provides = si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES);

    MFT_OUTPUT_DATA_BUFFER ob{};
    ob.dwStreamID = outId_;
    ComPtr<IMFSample> own;
    if (!provides) {
        ComPtr<IMFMediaBuffer> b;
        MFCreateMemoryBuffer(si.cbSize ? si.cbSize : 1 << 20, b.put());
        MFCreateSample(own.put());
        own->AddBuffer(b.get());
        ob.pSample = own.get();
    }
    DWORD status = 0;
    HRESULT hr = mft_->ProcessOutput(0, 1, &ob, &status);
    if (ob.pEvents) ob.pEvents->Release();

    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> t;
        if (SUCCEEDED(mft_->GetOutputAvailableType(outId_, 0, t.put()))) mft_->SetOutputType(outId_, t.get(), 0);
        return;
    }
    if (FAILED(hr) || !ob.pSample) return;

    IMFSample* sample = ob.pSample;
    EncodedPacket pkt;
    LONGLONG t = 0;
    sample->GetSampleTime(&t);
    pkt.ptsUs = t / 10;
    ComPtr<IMFMediaBuffer> cb;
    if (SUCCEEDED(sample->ConvertToContiguousBuffer(cb.put()))) {
        BYTE* p = nullptr;
        DWORD n = 0;
        if (SUCCEEDED(cb->Lock(&p, nullptr, &n))) {
            pkt.data.assign(p, p + n);
            cb->Unlock();
        }
    }
    if (provides) sample->Release();

    pkt.key = hasNalType(pkt.data, 5);
    if (pkt.key && !hasNalType(pkt.data, 7) && !seqHeader_.empty())
        pkt.data.insert(pkt.data.begin(), seqHeader_.begin(), seqHeader_.end());
    if (!pkt.data.empty() && onOutput_) onOutput_(std::move(pkt));
}

void Encoder::shutdown() {
    if (!mft_) return;
    running_ = false;
    ComPtr<IMFShutdown> sd;
    if (SUCCEEDED(mft_.as(sd))) sd->Shutdown();  // GetEvent'i MF_E_SHUTDOWN ile uyandırır
    if (thread_.joinable()) thread_.join();
    events_.reset();
    codecApi_.reset();
    mft_.reset();
    devManager_.reset();
    if (wake_) {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
}

}  // namespace ekran

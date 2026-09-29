// Media Foundation donanım H.264 kodlayıcısı (NVIDIA / AMD / Intel tek kod yoluyla).
// Girdi GPU'daki NV12 dokusudur; CPU'ya kopya yapılmaz.
#pragma once

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>  // ICodecAPI (codecapi.h GUID'leri encoder.cpp'de tanımlanır)

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "com.h"

#ifdef __MINGW32__
// MinGW başlıklarında eksik: ICodecAPI arayüz kimliği
__CRT_UUID_DECL(ICodecAPI, 0x901db4c7, 0x31ce, 0x41a2, 0x85, 0xdc, 0x8f, 0xa0, 0xbf, 0x41, 0xb8, 0xda)
#endif

namespace ekran {

struct EncodedPacket {
    std::vector<uint8_t> data;  // Annex-B, anahtar karede SPS/PPS dahil
    int64_t ptsUs = 0;
    bool key = false;
};

class Encoder {
public:
    using OutputFn = std::function<void(EncodedPacket&&)>;

    ~Encoder() { shutdown(); }

    bool init(ID3D11Device* device, unsigned vendorId, int width, int height, int fps, int kbps, OutputFn onOutput);
    void shutdown();

    bool canAccept() const { return needInput_.load() > 0; }
    bool submit(ID3D11Texture2D* nv12, int64_t ptsUs, bool forceKey);
    HANDLE wakeEvent() const { return wake_; }  // kodlayıcı yeni girdi istediğinde tetiklenir
    const std::string& name() const { return name_; }

private:
    void eventLoop();
    void drainOutput();
    void setCodecApi(const GUID& key, UINT32 value, const char* label);

    ComPtr<IMFTransform> mft_;
    ComPtr<IMFMediaEventGenerator> events_;
    ComPtr<ICodecAPI> codecApi_;
    ComPtr<IMFDXGIDeviceManager> devManager_;
    DWORD inId_ = 0, outId_ = 0;
    int fps_ = 60;
    OutputFn onOutput_;
    std::vector<uint8_t> seqHeader_;
    std::string name_;
    std::atomic<int> needInput_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
    HANDLE wake_ = nullptr;
};

}  // namespace ekran

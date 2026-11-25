#include "VideoPlayer.h"

#include "../Core/EngineLog.h"
#include "../UI/HephaestusUI/UIDynamicImages.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <filesystem>
#endif

namespace Hephaestus {

#ifdef _WIN32

namespace {

using Microsoft::WRL::ComPtr;

// The media engine's event sink: only whether it has ended or failed matters here.
class EngineNotify final : public IMFMediaEngineNotify {
public:
    std::atomic<bool> ended{false};
    std::atomic<bool> failed{false};

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *object = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_Refs; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG refs = --m_Refs;
        if (refs == 0) delete this;
        return refs;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override {
        if (event == MF_MEDIA_ENGINE_EVENT_ENDED) ended = true;
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) failed = true;
        return S_OK;
    }

private:
    std::atomic<ULONG> m_Refs{1};
};

} // namespace

struct VideoPlayer::Impl {
    bool comStarted = false, mfStarted = false;
    EngineNotify* notify = nullptr;
    ComPtr<IMFMediaEngine> engine;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> frame;
    DWORD width = 0, height = 0;

    ~Impl() {
        if (engine) {
            engine->Shutdown();
            engine.Reset();
        }
        frame.Reset();
        wic.Reset();
        if (notify) notify->Release();
        if (mfStarted) MFShutdown();
        if (comStarted) CoUninitialize();
    }
};

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    Stop();
}

bool VideoPlayer::Open(const std::string& path, const std::string& imageId, float volume, std::string& error) {
    Stop();
    auto impl = std::make_unique<Impl>();
    // The calling thread may already have chosen its COM apartment (the editor's is single threaded); either works.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl->comStarted = SUCCEEDED(com);
    if (FAILED(MFStartup(MF_VERSION))) {
        error = "Media Foundation is not available";
        return false;
    }
    impl->mfStarted = true;
    ComPtr<IMFMediaEngineClassFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl->wic)))) {
        error = "cannot create the media engine";
        return false;
    }
    impl->notify = new EngineNotify();
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 2)) || FAILED(attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, impl->notify)) ||
        FAILED(attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, 87 /* DXGI_FORMAT_B8G8R8A8_UNORM */))) {
        error = "cannot set up the media engine";
        return false;
    }
    // No window and no DXGI device: frame-server mode, with frames copied out through WIC.
    if (FAILED(factory->CreateInstance(0, attributes.Get(), &impl->engine))) {
        error = "cannot create the media engine";
        return false;
    }
    const std::wstring url = std::filesystem::path(std::filesystem::u8path(path)).wstring();
    BSTR source = SysAllocString(url.c_str());
    const HRESULT set = impl->engine->SetSource(source);
    SysFreeString(source);
    if (FAILED(set)) {
        error = "cannot open " + path;
        return false;
    }
    impl->engine->SetVolume(volume < 0.0f ? 0.0 : volume > 1.0f ? 1.0 : volume);
    if (FAILED(impl->engine->Play())) {
        error = "cannot play " + path;
        return false;
    }
    m_Impl = std::move(impl);
    m_ImageId = imageId;
    const uint8_t black[4] = {0, 0, 0, 255}; // something to show (and resolve) until the first frame arrives
    HephaestusUI::UIDynamicImages::Instance().Set(m_ImageId, black, 1, 1);
    return true;
}

bool VideoPlayer::Update() {
    if (!m_Impl) return false;
    Impl& impl = *m_Impl;
    if (impl.notify->failed) return false;
    if (impl.width == 0) { // the size is known once the stream's metadata has loaded
        DWORD w = 0, h = 0;
        if (SUCCEEDED(impl.engine->GetNativeVideoSize(&w, &h)) && w > 0 && h > 0) {
            impl.width = w;
            impl.height = h;
            if (FAILED(impl.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppBGRA, WICBitmapCacheOnLoad, &impl.frame))) {
                return false;
            }
            m_Pixels.assign(static_cast<std::size_t>(w) * h * 4, 0);
            EngineLog::Write(EngineLogLevel::Info, "Video " + m_ImageId + ": " + std::to_string(w) + "x" + std::to_string(h), "Video");
        }
    }
    LONGLONG pts = 0;
    if (impl.frame && impl.engine->OnVideoStreamTick(&pts) == S_OK) {
        const RECT dest{0, 0, static_cast<LONG>(impl.width), static_cast<LONG>(impl.height)};
        const MFARGB black{0, 0, 0, 255};
        if (SUCCEEDED(impl.engine->TransferVideoFrame(impl.frame.Get(), nullptr, &dest, &black))) {
            ComPtr<IWICBitmapLock> lock;
            const WICRect all{0, 0, static_cast<INT>(impl.width), static_cast<INT>(impl.height)};
            if (SUCCEEDED(impl.frame->Lock(&all, WICBitmapLockRead, &lock))) {
                UINT stride = 0, size = 0;
                BYTE* data = nullptr;
                lock->GetStride(&stride);
                lock->GetDataPointer(&size, &data);
                for (DWORD y = 0; y < impl.height && data; ++y) {
                    const BYTE* row = data + static_cast<std::size_t>(y) * stride;
                    uint8_t* out = m_Pixels.data() + static_cast<std::size_t>(y) * impl.width * 4;
                    for (DWORD x = 0; x < impl.width; ++x) { // BGRA to RGBA, opaque
                        out[x * 4 + 0] = row[x * 4 + 2];
                        out[x * 4 + 1] = row[x * 4 + 1];
                        out[x * 4 + 2] = row[x * 4 + 0];
                        out[x * 4 + 3] = 255;
                    }
                }
                lock.Reset();
                HephaestusUI::UIDynamicImages::Instance().Set(m_ImageId, m_Pixels.data(), impl.width, impl.height);
                ++m_Frames;
            }
        }
    }
    return !(impl.notify->ended || impl.engine->IsEnded());
}

void VideoPlayer::Stop() {
    if (m_Impl) EngineLog::Write(EngineLogLevel::Info, "Video " + m_ImageId + ": stopped after " + std::to_string(m_Frames) + " frames", "Video");
    m_Frames = 0;
    if (!m_ImageId.empty()) HephaestusUI::UIDynamicImages::Instance().Remove(m_ImageId);
    m_ImageId.clear();
    m_Impl.reset();
}

bool VideoPlayer::IsPlaying() const {
    return m_Impl && !m_Impl->notify->failed && !m_Impl->notify->ended;
}

#else // no video backend on this platform yet

struct VideoPlayer::Impl {};

VideoPlayer::VideoPlayer() = default;
VideoPlayer::~VideoPlayer() = default;

bool VideoPlayer::Open(const std::string&, const std::string&, float, std::string& error) {
    error = "video playback is not available on this platform";
    return false;
}

bool VideoPlayer::Update() { return false; }
void VideoPlayer::Stop() {}
bool VideoPlayer::IsPlaying() const { return false; }

#endif

} // namespace Hephaestus

#include "Engine.h"

#include "Log.h"
#include "Paths.h"
#include "capture/Camera.h"
#include "compositor/Compositor.h"
#include "flow/Flow.h"
#include "flow/NvofFlow.h"
#include "gpu/Gpu.h"
#include "pacing/Pacer.h"
#include "vision/AutoFramer.h"
#include "vision/Vision.h"

#include "Denoise.h"
#include "LumaStats.h"
#include "SmallLuma.h"
#include "SmallRgb.h"

#include <mfapi.h>
#include <mferror.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace smoothcam
{
namespace
{
constexpr size_t kRingSize = 4;
constexpr size_t kMaxPending = 6;
constexpr Hns kSafetyMargin = 4 * kHnsPerMs;
// Cameras in their low-light mode often drop to 7-15 fps. Interpolation keeps
// going down to ~5 fps (the delay grows with the gap); below that the gaps are
// too large to bridge and it pauses.
constexpr Hns kMaxInterpolationInterval = 210 * kHnsPerMs;
constexpr Hns kMaxDelay = 260 * kHnsPerMs;
constexpr wchar_t kSlowCameraMessage[] = L"Camera is extremely slow (under 5 fps) - smoothing paused";

// Person segmentation model input (MediaPipe selfie segmenter, landscape).
constexpr uint32_t kSegWidth = 256;
constexpr uint32_t kSegHeight = 144;

struct Slot
{
    Nv12Texture frame;   // as delivered by the camera
    PlanarFrame clean;   // after noise reduction
    bool cleaned = false;
    LumaPyramid pyramid;
    Texture forward;  // refined: previous frame -> this frame, on previous frame's grid
    Texture backward; // refined: this frame -> previous frame, on this frame's grid
    bool hasFlow = false;
    bool valid = false;
    uint64_t id = 0;
    Hns time = 0;

    FrameView View() const { return cleaned ? clean.View() : frame.View(); }
};

// Copies small textures to the CPU without stalling: each copy is read back a
// frame or two later, once the GPU has finished it.
class ReadbackRing
{
public:
    HRESULT Ensure(ID3D11Device* device, uint32_t w, uint32_t h, DXGI_FORMAT format)
    {
        if (m_stage[0].width == w && m_stage[0].height == h && m_stage[0].format == format && m_stage[0])
            return S_OK;
        for (auto& s : m_stage)
            RETURN_IF_FAILED(s.CreateStaging(device, w, h, format));
        m_pending = {};
        return S_OK;
    }

    void Push(ID3D11DeviceContext* context, ID3D11Texture2D* source)
    {
        context->CopyResource(m_stage[m_write].tex.get(), source);
        m_pending[m_write] = true;
        m_write = (m_write + 1) % m_stage.size();
    }

    template <class F>
    bool Pop(ID3D11DeviceContext* context, F&& consume)
    {
        for (size_t k = 0; k < m_stage.size(); ++k)
        {
            const size_t i = (m_write + k) % m_stage.size(); // oldest first
            if (!m_pending[i])
                continue;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT hr = context->Map(m_stage[i].tex.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING || FAILED(hr))
                return false;
            consume(static_cast<const uint8_t*>(mapped.pData), m_stage[i].width, m_stage[i].height, mapped.RowPitch);
            context->Unmap(m_stage[i].tex.get(), 0);
            m_pending[i] = false;
            return true;
        }
        return false;
    }

private:
    std::array<Texture, 3> m_stage;
    std::array<bool, 3> m_pending{};
    size_t m_write = 0;
};

struct DenoiseCb
{
    uint32_t w, h;
    float temporal;
    float spatial;
    uint32_t hasHistory;
    uint32_t hasFlow;
    float sigma;
    float pad;
};

struct StatsCb
{
    uint32_t w, h;
    float rate;
    float pad;
};

struct SmallCb
{
    uint32_t w, h;
    uint32_t bt709;
    uint32_t pad;
};

struct Captured
{
    ComPtr<IMFSample> sample;
    Hns time = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Smoothed value with a time constant measured in samples.
struct Ema
{
    double value = 0;
    bool primed = false;
    void Add(double v, double k)
    {
        value = primed ? value + (v - value) * k : v;
        primed = true;
    }
};
} // namespace

struct Engine::Impl
{
    OutputFormat format;
    Settings settings;

    std::unique_ptr<GpuDevice> gpu;
    PyramidBuilder pyramidBuilder;
    FlowRefiner refiner;
    Compositor compositor;
    std::unique_ptr<FlowEstimator> estimator;
    std::wstring flowName;
    int workLevel = 0;
    int stepDown = 0; // automatic quality reductions after sustained overload
    int slowFrames = 0;

    std::array<Slot, kRingSize> ring;
    uint64_t nextId = 1;
    IntervalEstimator interval;

    // Filters
    ComPtr<ID3D11ComputeShader> denoiseShader, statsShader, smallRgbShader, smallLumaShader;
    ComPtr<ID3D11SamplerState> sampler;
    ConstantBuffer<DenoiseCb> denoiseCb;
    ConstantBuffer<StatsCb> statsCb;
    ConstantBuffer<SmallCb> smallCb;
    Texture lumaStats; // 1x1 smoothed mean luma for the low-light boost

    // Vision (face detection, person segmentation) and what it drives
    std::unique_ptr<VisionWorker> vision;
    Texture smallRgb, smallLuma;
    ReadbackRing rgbReadback, lumaReadback;
    Texture personMask;
    bool maskValid = false;
    AutoFramer framer;
    uint64_t ingestCount = 0;

    // Camera thread state.
    CameraCapture camera;
    std::thread supervisor;
    wil::unique_event stopEvent{wil::EventOptions::ManualReset};
    wil::unique_event errorEvent;
    wil::unique_event reopenEvent;
    std::atomic<bool> cameraOpen{false};
    std::atomic<double> cameraFps{30};

    // Shared between the camera callback and Render().
    std::mutex lock;
    std::deque<Captured> pending;
    TimestampMapper mapper;
    Settings pendingSettings;
    bool settingsDirty = false;

    // Stats.
    mutable std::mutex statsLock;
    EngineStats stats;
    Ema gpuMs;
    Ema copyMs; // part of gpuMs spent copying pixels on the CPU
    Ema outputInterval;
    Hns lastRender = 0;
    Hns lastAddedLatency = 0;
    bool slowCamera = false;
    bool mfStarted = false;

    ~Impl() { StopCamera(); }

    void SetMessage(std::wstring message)
    {
        std::lock_guard g(statsLock);
        if (stats.message != message)
        {
            if (!message.empty())
                SC_LOG_INFO(L"Engine: %s", message.c_str());
            stats.message = std::move(message);
        }
    }

    // ---- camera thread -------------------------------------------------

    void OnFrame(HRESULT hr, IMFSample* sample, Hns deviceTime)
    {
        if (FAILED(hr) || !sample)
        {
            SC_LOG_ERROR(L"Camera: stream error 0x%08X", hr);
            errorEvent.SetEvent();
            return;
        }
        const Hns arrival = QpcNow();
        std::lock_guard g(lock);
        Captured c;
        c.sample = sample;
        c.time = mapper.Map(deviceTime, arrival);
        c.width = camera.Width();
        c.height = camera.Height();
        pending.push_back(std::move(c));
        while (pending.size() > kMaxPending)
            pending.pop_front();
    }

    void SupervisorLoop()
    {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (WaitForSingleObject(stopEvent.get(), 0) != WAIT_OBJECT_0)
        {
            if (!camera.IsOpen())
            {
                Settings s;
                {
                    std::lock_guard g(lock);
                    s = settingsDirty ? pendingSettings : settings;
                    mapper.Reset();
                    pending.clear();
                }
                const HRESULT hr =
                    camera.Open(s.device, format.width, format.height,
                                static_cast<double>(format.fpsNum) / format.fpsDen, gpu->Manager(), s.steadyFps,
                                [this](HRESULT h, IMFSample* smp, Hns t) { OnFrame(h, smp, t); });
                if (FAILED(hr))
                {
                    cameraOpen = false;
                    wchar_t msg[96];
                    swprintf_s(msg, hr == MF_E_NO_CAPTURE_DEVICES_AVAILABLE ? L"No camera found"
                                                                             : L"Camera not available (0x%08X)",
                               hr);
                    SetMessage(msg);
                    if (stopEvent.wait(1500))
                        break;
                    continue;
                }
                cameraOpen = true;
                cameraFps = camera.NativeFps() > 0 ? camera.NativeFps() : 30.0;
                {
                    std::lock_guard g(statsLock);
                    stats.camera = camera.Name();
                }
                SetMessage(L"");
            }

            HANDLE events[] = {stopEvent.get(), errorEvent.get(), reopenEvent.get()};
            const DWORD w = WaitForMultipleObjects(3, events, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0)
                break;
            camera.Close();
            cameraOpen = false;
            if (w == WAIT_OBJECT_0 + 1)
            {
                SetMessage(L"Camera disconnected - retrying");
                if (stopEvent.wait(1000))
                    break;
            }
        }
        camera.Close();
        cameraOpen = false;
        if (SUCCEEDED(co))
            CoUninitialize();
    }

    void StopCamera()
    {
        if (supervisor.joinable())
        {
            stopEvent.SetEvent();
            supervisor.join();
        }
    }

    // ---- GPU side --------------------------------------------------------

    void RebuildEstimator()
    {
        estimator.reset();
        for (auto& s : ring)
            s.hasFlow = false;

        const Mode mode = settings.mode;
        workLevel = (mode == Mode::Balanced || stepDown >= 1) ? 1 : 0;

        if (mode == Mode::Off)
        {
            flowName = L"Off";
            return;
        }
        if (settings.engine == EngineChoice::Basic || stepDown >= 2)
        {
            flowName = L"Basic (blend)";
            return;
        }

        const bool wantNvidia = settings.engine == EngineChoice::Auto || settings.engine == EngineChoice::Nvidia;
        if (wantNvidia && gpu->Info().IsNvidia())
        {
            auto nvof = std::make_unique<NvofFlow>(*gpu, workLevel);
            if (SUCCEEDED(nvof->Init()))
                estimator = std::move(nvof);
            else
                SC_LOG_INFO(L"Engine: NVIDIA Optical Flow unavailable, using GPU compute");
        }
        // Block matching on WARP (a CPU rasterizer) is too slow for real time
        // unless the user explicitly asked for it.
        if (!estimator && (!gpu->Info().software || settings.engine == EngineChoice::Gpu))
        {
            auto compute = std::make_unique<ComputeFlow>(*gpu, workLevel);
            if (SUCCEEDED(compute->Init()))
                estimator = std::move(compute);
        }
        flowName = estimator ? estimator->Name() : L"Basic (blend)";
        SC_LOG_INFO(L"Engine: motion estimation = %s, work level %d", flowName.c_str(), workLevel);
    }

    void ApplyPendingSettings()
    {
        Settings next;
        {
            std::lock_guard g(lock);
            if (!settingsDirty)
                return;
            next = pendingSettings;
            settingsDirty = false;
        }
        const bool rebuild = next.mode != settings.mode || next.engine != settings.engine;
        settings = next;
        log::SetLevel(static_cast<log::Level>(settings.logLevel));
        if (rebuild)
        {
            stepDown = 0;
            slowFrames = 0;
            RebuildEstimator();
        }
        ConfigureVision();
    }

    HRESULT InitFilters()
    {
        RETURN_IF_FAILED(gpu->CreateShader(g_Denoise, sizeof(g_Denoise), denoiseShader));
        RETURN_IF_FAILED(gpu->CreateShader(g_LumaStats, sizeof(g_LumaStats), statsShader));
        RETURN_IF_FAILED(gpu->CreateShader(g_SmallRgb, sizeof(g_SmallRgb), smallRgbShader));
        RETURN_IF_FAILED(gpu->CreateShader(g_SmallLuma, sizeof(g_SmallLuma), smallLumaShader));
        RETURN_IF_FAILED(CreateLinearClampSampler(gpu->Device(), sampler));
        RETURN_IF_FAILED(denoiseCb.Create(gpu->Device()));
        RETURN_IF_FAILED(statsCb.Create(gpu->Device()));
        RETURN_IF_FAILED(smallCb.Create(gpu->Device()));
        RETURN_IF_FAILED(lumaStats.Create(gpu->Device(), 1, 1, DXGI_FORMAT_R32_FLOAT));
        const float zero[4] = {};
        gpu->Context()->ClearUnorderedAccessViewFloat(lumaStats.uav.get(), zero);
        ConfigureVision();
        return S_OK;
    }

    void ConfigureVision()
    {
        const bool faces = settings.autoFrame != AutoFrame::Off;
        const bool segmentation = settings.background != Background::Off;
        framer.SetMode(settings.autoFrame);
        if (!faces && !segmentation)
        {
            vision.reset();
            maskValid = false;
            return;
        }
        if (!vision)
            vision = std::make_unique<VisionWorker>();
        vision->Configure(faces, segmentation, ModuleDirectory() / L"models" / L"selfie_segmenter.onnx");
        if (!segmentation)
            maskValid = false;
    }

    // Noise reduction for the newest frame; `prev` is the frame before it (or null).
    void Denoise(Slot& slot, const Slot* prev)
    {
        slot.cleaned = false;
        float temporal = 0, spatial = 0, sigma = 0.03f;
        switch (settings.noiseReduction)
        {
        case NoiseReduction::Off: return;
        case NoiseReduction::Low: temporal = 0.5f; spatial = 0.0f; sigma = 0.03f; break;
        case NoiseReduction::Medium: temporal = 0.7f; spatial = 0.5f; sigma = 0.045f; break;
        case NoiseReduction::High: temporal = 0.82f; spatial = 1.0f; sigma = 0.06f; break;
        }
        const uint32_t w = slot.frame.width, h = slot.frame.height;
        if (FAILED(slot.clean.Create(gpu->Device(), w, h)))
            return;
        const bool history = prev && prev->cleaned && prev->clean.y.width == w && prev->clean.y.height == h;
        denoiseCb.Update(gpu->Context(),
                         {w, h, temporal, spatial, history ? 1u : 0u, slot.hasFlow ? 1u : 0u, sigma, 0});
        RunCompute(gpu->Context(), denoiseShader.get(), denoiseCb.Get(),
                   {slot.frame.srvY.get(), slot.frame.srvUV.get(), history ? prev->clean.y.srv.get() : nullptr,
                    history ? prev->clean.uv.srv.get() : nullptr, slot.hasFlow ? slot.backward.srv.get() : nullptr},
                   {slot.clean.y.uav.get(), slot.clean.uv.uav.get()}, sampler.get(), (w + 1) / 2, (h + 1) / 2);
        slot.cleaned = true;
    }

    // Average brightness for the low-light boost, from a small pyramid level.
    void UpdateLumaStats(const Slot& slot)
    {
        if (settings.lowLight == LowLightBoost::Off)
            return;
        const Texture* level = &slot.pyramid.level[0];
        for (const auto& l : slot.pyramid.level)
            if (l.width >= 64)
                level = &l;
        statsCb.Update(gpu->Context(), {level->width, level->height, 0.08f, 0});
        RunCompute(gpu->Context(), statsShader.get(), statsCb.Get(), {level->srv.get()}, {lumaStats.uav.get()},
                   nullptr, 16, 16, 16);
    }

    // Feeds the vision worker small images and collects its results.
    void UpdateVision(const Slot& slot)
    {
        if (!vision)
            return;
        auto* device = gpu->Device();
        auto* context = gpu->Context();
        const FrameView view = slot.View();
        ++ingestCount;

        if (settings.background != Background::Off)
        {
            if (!smallRgb)
            {
                smallRgb.Create(device, kSegWidth, kSegHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
                rgbReadback.Ensure(device, kSegWidth, kSegHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
            }
            smallCb.Update(context, {kSegWidth, kSegHeight, view.height >= 720 ? 1u : 0u, 0});
            RunCompute(context, smallRgbShader.get(), smallCb.Get(), {view.srvY, view.srvUV}, {smallRgb.uav.get()},
                       sampler.get(), kSegWidth, kSegHeight);
            rgbReadback.Pop(context, [&](const uint8_t* p, uint32_t w, uint32_t h, uint32_t stride) {
                vision->SubmitRgba(p, w, h, stride);
            });
            rgbReadback.Push(context, smallRgb.tex.get());

            std::vector<uint8_t> mask;
            uint32_t mw = 0, mh = 0;
            if (vision->TakeMask(mask, mw, mh))
            {
                if (personMask.width != mw || personMask.height != mh)
                    personMask.Create(device, mw, mh, DXGI_FORMAT_R8_UNORM, false);
                context->UpdateSubresource(personMask.tex.get(), 0, nullptr, mask.data(), mw, 0);
                maskValid = true;
            }
        }

        // Faces: ~10 times a second is plenty for framing.
        if (settings.autoFrame != AutoFrame::Off && ingestCount % 3 == 0)
        {
            const uint32_t lw = std::min(480u, view.width / 2) & ~1u;
            const uint32_t lh = std::max(2u, (lw * view.height / std::max(1u, view.width)) & ~1u);
            if (smallLuma.width != lw || smallLuma.height != lh)
            {
                smallLuma.Create(device, lw, lh, DXGI_FORMAT_R8_UNORM);
                lumaReadback.Ensure(device, lw, lh, DXGI_FORMAT_R8_UNORM);
            }
            smallCb.Update(context, {lw, lh, 0, 0});
            RunCompute(context, smallLumaShader.get(), smallCb.Get(), {view.srvY}, {smallLuma.uav.get()},
                       sampler.get(), lw, lh);
            lumaReadback.Pop(context, [&](const uint8_t* p, uint32_t w, uint32_t h, uint32_t stride) {
                vision->SubmitLuma(p, w, h, stride);
            });
            lumaReadback.Push(context, smallLuma.tex.get());
        }

        const std::wstring problem = vision->Problem();
        if (!problem.empty())
            SetMessage(problem);
    }

    Slot* FindSlot(uint64_t id)
    {
        auto& s = ring[id % kRingSize];
        return s.valid && s.id == id ? &s : nullptr;
    }

    HRESULT Upload(const Captured& c, Slot& slot)
    {
        auto* device = gpu->Device();
        auto* context = gpu->Context();
        if (!slot.frame || slot.frame.width != c.width || slot.frame.height != c.height)
            RETURN_IF_FAILED(slot.frame.Create(device, c.width, c.height));

        ComPtr<IMFMediaBuffer> buffer;
        RETURN_IF_FAILED(c.sample->GetBufferByIndex(0, &buffer));

        ComPtr<IMFDXGIBuffer> dxgi;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&dxgi))))
        {
            ComPtr<ID3D11Texture2D> tex;
            UINT sub = 0;
            RETURN_IF_FAILED(dxgi->GetResource(IID_PPV_ARGS(&tex)));
            RETURN_IF_FAILED(dxgi->GetSubresourceIndex(&sub));
            D3D11_TEXTURE2D_DESC desc{};
            tex->GetDesc(&desc);
            RETURN_HR_IF(MF_E_INVALIDMEDIATYPE, desc.Format != DXGI_FORMAT_NV12);
            const D3D11_BOX box{0, 0, 0, c.width, c.height, 1};
            context->CopySubresourceRegion(slot.frame.tex.get(), 0, 0, 0, 0, tex.get(), sub, &box);
            return S_OK;
        }

        // System-memory frame: NV12 with the chroma plane right after the luma.
        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* data = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))) && SUCCEEDED(buffer2d->Lock2D(&data, &pitch)))
        {
            context->UpdateSubresource(slot.frame.tex.get(), 0, nullptr, data, static_cast<UINT>(pitch), 0);
            buffer2d->Unlock2D();
            return S_OK;
        }
        DWORD length = 0;
        RETURN_IF_FAILED(buffer->Lock(&data, nullptr, &length));
        context->UpdateSubresource(slot.frame.tex.get(), 0, nullptr, data, c.width, 0);
        buffer->Unlock();
        return S_OK;
    }

    void Ingest()
    {
        std::deque<Captured> incoming;
        {
            std::lock_guard g(lock);
            incoming.swap(pending);
        }
        for (auto& c : incoming)
        {
            const uint64_t id = nextId++;
            Slot& slot = ring[id % kRingSize];
            slot.valid = false;
            slot.hasFlow = false;
            slot.cleaned = false;
            if (FAILED(Upload(c, slot)))
                continue;
            if (FAILED(slot.pyramid.Ensure(gpu->Device(), c.width, c.height)))
                continue;
            pyramidBuilder.Build(gpu->Context(), slot.frame.srvY.get(), slot.pyramid);
            slot.id = id;
            slot.time = c.time;
            slot.valid = true;
            interval.Add(c.time);

            Slot* prev = FindSlot(id - 1);
            if (prev && (prev->frame.width != c.width || prev->frame.height != c.height))
                prev = nullptr;
            if (estimator && prev)
                EstimatePair(*prev, slot);
            Denoise(slot, prev);
            UpdateLumaStats(slot);
            UpdateVision(slot);
        }
    }

    void EstimatePair(Slot& prev, Slot& slot)
    {
        RawFlow raw;
        HRESULT hr = estimator->Estimate(prev.pyramid, slot.pyramid, prev.hasFlow ? &prev.forward : nullptr, raw);
        if (FAILED(hr))
        {
            SC_LOG_ERROR(L"Engine: %s failed (0x%08X), falling back", estimator->Name(), hr);
            const bool wasNvof = dynamic_cast<NvofFlow*>(estimator.get()) != nullptr;
            estimator.reset();
            if (wasNvof)
            {
                auto compute = std::make_unique<smoothcam::ComputeFlow>(*gpu, workLevel);
                if (SUCCEEDED(compute->Init()))
                    estimator = std::move(compute);
            }
            flowName = estimator ? estimator->Name() : L"Basic (blend)";
            return;
        }
        auto* device = gpu->Device();
        const auto& f = *raw.forward;
        if (slot.forward.width != f.width || slot.forward.height != f.height)
        {
            if (FAILED(slot.forward.Create(device, f.width, f.height, DXGI_FORMAT_R16G16B16A16_FLOAT)) ||
                FAILED(slot.backward.Create(device, f.width, f.height, DXGI_FORMAT_R16G16B16A16_FLOAT)))
                return;
        }
        const int level = estimator->WorkLevel();
        refiner.Run(gpu->Context(), *raw.forward, *raw.backward, prev.pyramid.level[level],
                    slot.pyramid.level[level], slot.frame.width, slot.frame.height, slot.forward);
        refiner.Run(gpu->Context(), *raw.backward, *raw.forward, slot.pyramid.level[level],
                    prev.pyramid.level[level], slot.frame.width, slot.frame.height, slot.backward);
        slot.hasFlow = true;
    }

    void ComputeCrop(const FrameView& src, ComposeParams& p) const
    {
        const double srcAspect = static_cast<double>(src.width) / src.height;
        const double outAspect = static_cast<double>(format.width) / format.height;
        if (srcAspect > outAspect + 1e-3)
        {
            p.cropScale[0] = static_cast<float>(outAspect / srcAspect);
            p.cropOffset[0] = (1.0f - p.cropScale[0]) * 0.5f;
        }
        else if (srcAspect < outAspect - 1e-3)
        {
            p.cropScale[1] = static_cast<float>(srcAspect / outAspect);
            p.cropOffset[1] = (1.0f - p.cropScale[1]) * 0.5f;
        }
    }

    // RCAS strength in stops (lower = stronger), or -1 for none.
    float SharpnessFor(const FrameView& src, const ComposeParams& p) const
    {
        switch (settings.sharpening)
        {
        case Sharpening::Off: return -1.0f;
        case Sharpening::Low: return 0.8f;
        case Sharpening::Medium: return 0.35f;
        case Sharpening::High: return 0.0f;
        case Sharpening::Auto:
        default: {
            // Only when upscaling (incl. auto-framing zoom): FSR's upscaler is
            // designed to be followed by RCAS.
            const bool upscaling = format.width > src.width * p.cropScale[0] * 1.1f ||
                                   format.height > src.height * p.cropScale[1] * 1.1f;
            return upscaling ? 0.8f : -1.0f;
        }
        }
    }

    Hns SourceInterval() const
    {
        if (interval.HasEstimate())
            return interval.Interval();
        return static_cast<Hns>(kHnsPerSecond / std::max(1.0, cameraFps.load()));
    }

    HRESULT Render(Hns outputTime, uint8_t* dst, LONG pitch, DWORD capacity)
    {
        ApplyPendingSettings();
        RETURN_IF_FAILED(compositor.EnsureOutput(format.width, format.height, format.rgb32));

        const Hns started = QpcNow();
        GpuDevice::ScopedLock gpuLock(*gpu);
        Ingest();

        // How far behind "now" we render. Interpolation needs the next real
        // frame to have arrived, so we trail by one source interval plus the
        // camera's delivery jitter.
        Hns jitter;
        {
            std::lock_guard g(lock);
            jitter = mapper.ArrivalJitter();
        }
        const Hns sourceInterval = SourceInterval();
        const bool cameraTooSlow = interval.HasEstimate() && sourceInterval > kMaxInterpolationInterval;
        if (cameraTooSlow != slowCamera)
        {
            slowCamera = cameraTooSlow;
            SetMessage(slowCamera ? kSlowCameraMessage : L"");
        }
        const bool interpolating =
            (settings.mode == Mode::Quality || settings.mode == Mode::Balanced) && !cameraTooSlow;
        Hns delay = jitter + kSafetyMargin;
        if (interpolating)
            delay += sourceInterval;
        delay = std::min<Hns>(delay, kMaxDelay);
        // "Off", or interpolation paused: show the newest frame as soon as it exists.
        if (settings.mode == Mode::Off || cameraTooSlow)
            delay = 0;

        std::vector<SourceFrame> frames;
        for (const auto& s : ring)
            if (s.valid)
                frames.push_back({s.id, s.time});
        std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) { return a.time < b.time; });

        PaceOptions options;
        options.allowExtrapolation = settings.mode == Mode::LowLatency && estimator != nullptr && !cameraTooSlow;
        const Hns virtualTime = outputTime - delay;
        const PaceDecision d = DecideFrame(frames, virtualTime, options);

        if (d.kind == PaceDecision::Kind::None)
        {
            compositor.FillBlank(dst, pitch, capacity);
            return S_OK;
        }

        Slot* prev = FindSlot(d.prevId);
        Slot* next = FindSlot(d.nextId);
        if (!prev || !next)
            return E_UNEXPECTED;

        const FrameView nextView = next->View();
        ComposeParams p;
        p.compare = settings.compare && settings.mode != Mode::Off;
        ComputeCrop(nextView, p);

        // Auto-framing narrows the crop around the face; the compositor then
        // upscales it back to the output size with FSR, so zooming stays sharp.
        if (settings.autoFrame != AutoFrame::Off && vision)
        {
            Hns facesTime = 0;
            const auto faces = vision->Faces(facesTime);
            const CropRect base{p.cropOffset[0], p.cropOffset[1], p.cropScale[0], p.cropScale[1]};
            const CropRect crop =
                framer.Update(faces, facesTime, outputTime, base,
                              static_cast<float>(nextView.width) / nextView.height,
                              static_cast<float>(format.width) / format.height);
            p.cropOffset[0] = crop.x;
            p.cropOffset[1] = crop.y;
            p.cropScale[0] = crop.w;
            p.cropScale[1] = crop.h;
        }
        p.sharpness = SharpnessFor(nextView, p);
        p.useNis = settings.upscaler == Upscaler::Nis;

        if (settings.lowLight != LowLightBoost::Off)
        {
            p.lowLightTarget = settings.lowLight == LowLightBoost::Strong ? 0.48f : 0.40f;
            p.lowLightMaxGain = settings.lowLight == LowLightBoost::Strong ? 5.0f : 3.0f;
            p.lumaStats = lumaStats.srv.get();
        }
        if (settings.background != Background::Off && maskValid)
        {
            p.personMask = personMask.srv.get();
            p.blurPasses = settings.background == Background::StrongBlur ? 3 : 1;
        }
        const Texture* flowF = nullptr;
        const Texture* flowB = nullptr;

        switch (d.kind)
        {
        case PaceDecision::Kind::Hold:
            p.mode = ComposeMode::Hold;
            break;
        case PaceDecision::Kind::Interpolate:
            p.alpha = d.alpha;
            if (estimator && next->hasFlow && d.nextId == d.prevId + 1)
            {
                p.mode = ComposeMode::Interpolate;
                flowF = &next->forward;
                flowB = &next->backward;
            }
            else
            {
                p.mode = ComposeMode::Blend;
            }
            break;
        case PaceDecision::Kind::Extrapolate:
            p.alpha = d.alpha;
            if (next->hasFlow)
            {
                p.mode = ComposeMode::Extrapolate;
                flowB = &next->backward;
            }
            else
            {
                p.mode = ComposeMode::Hold;
                prev = next;
            }
            break;
        default:
            break;
        }

        // Frames of different sizes (camera switched) can't be mixed.
        if (prev->frame.width != next->frame.width || prev->frame.height != next->frame.height)
        {
            p.mode = ComposeMode::Hold;
            prev = next;
        }

        compositor.Compose(prev->View(), nextView, flowF, flowB, p);
        const HRESULT hr = compositor.Readback(dst, pitch, capacity);

        const Hns finished = QpcNow();
        copyMs.Add(compositor.LastCopyMs(), 0.05);
        UpdateStats(outputTime, delay, finished - started);
        return hr;
    }

    void UpdateStats(Hns outputTime, Hns delay, Hns workTime)
    {
        gpuMs.Add(HnsToMs(workTime), 0.05);
        if (lastRender)
            outputInterval.Add(static_cast<double>(outputTime - lastRender), 0.05);
        lastRender = outputTime;

        // Sustained overload: step quality down rather than drop frames. Only
        // the GPU part counts - a lighter motion estimate can't make the pixel
        // copy (big at 4K) any faster.
        const double budgetMs = HnsToMs(format.FrameDuration());
        if (estimator && gpuMs.value - copyMs.value > budgetMs * 0.7)
        {
            if (++slowFrames > static_cast<int>(2 * format.fpsNum / std::max(1u, format.fpsDen)) && stepDown < 2)
            {
                ++stepDown;
                slowFrames = 0;
                SetMessage(stepDown == 1 ? L"GPU busy - switched to Balanced" : L"GPU busy - switched to Basic");
                RebuildEstimator();
            }
        }
        else
        {
            slowFrames = 0;
        }

        std::lock_guard g(statsLock);
        stats.cameraOpen = cameraOpen;
        stats.inputFps = interval.HasEstimate() ? interval.Fps() : 0.0;
        stats.outputFps = outputInterval.primed && outputInterval.value > 0 ? kHnsPerSecond / outputInterval.value : 0;
        stats.gpuMs = gpuMs.value;
        stats.addedLatencyMs = HnsToMs(delay) + gpuMs.value;
        stats.flowEngine = flowName;
        std::wstring filters;
        auto add = [&](const wchar_t* name) {
            if (!filters.empty())
                filters += L", ";
            filters += name;
        };
        if (settings.noiseReduction != NoiseReduction::Off)
            add(L"noise reduction");
        if (settings.lowLight != LowLightBoost::Off)
            add(L"low-light boost");
        if (settings.background != Background::Off)
            add(maskValid ? L"background blur" : L"background blur (starting)");
        if (settings.autoFrame != AutoFrame::Off)
            add(L"auto-framing");
        stats.filters = filters;
        if (compositor.Upscaled())
        {
            wchar_t text[80];
            swprintf_s(text, L"%ux%u \u2192 %ux%u (%s)", compositor.RenderWidth(), compositor.RenderHeight(),
                       compositor.Width(), compositor.Height(), compositor.UpscalerName());
            stats.upscale = text;
        }
        else
        {
            stats.upscale.clear();
        }
    }
};

Engine::Engine() : m_impl(std::make_unique<Impl>()) {}

Engine::~Engine()
{
    Stop();
}

HRESULT Engine::Start(const OutputFormat& format, const Settings& settings)
{
    Stop();
    m_impl = std::make_unique<Impl>();
    auto& s = *m_impl;
    s.format = format;
    s.settings = settings;
    s.errorEvent.create();
    s.reopenEvent.create();

    RETURN_IF_FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    s.mfStarted = true;

    RETURN_IF_FAILED(GpuDevice::Create(s.gpu));
    RETURN_IF_FAILED(s.pyramidBuilder.Init(*s.gpu));
    RETURN_IF_FAILED(s.refiner.Init(*s.gpu));
    RETURN_IF_FAILED(s.compositor.Init(*s.gpu));
    RETURN_IF_FAILED(s.compositor.EnsureOutput(format.width, format.height, format.rgb32));
    s.RebuildEstimator();
    RETURN_IF_FAILED(s.InitFilters());
    {
        std::lock_guard g(s.statsLock);
        s.stats.adapter = s.gpu->Info().name;
        s.stats.flowEngine = s.flowName;
    }

    SC_LOG_INFO(L"Engine: start %ux%u @ %u/%u %s, mode %s", format.width, format.height, format.fpsNum,
                format.fpsDen, format.rgb32 ? L"RGB32" : L"NV12", ToString(settings.mode));
    s.supervisor = std::thread([impl = m_impl.get()] { impl->SupervisorLoop(); });
    return S_OK;
}

void Engine::Stop()
{
    if (!m_impl)
        return;
    m_impl->StopCamera();
    const bool mf = m_impl->mfStarted;
    m_impl.reset();
    if (mf)
        MFShutdown();
    m_impl = std::make_unique<Impl>();
}

void Engine::UpdateSettings(const Settings& settings)
{
    auto& s = *m_impl;
    std::lock_guard g(s.lock);
    const Settings& current = s.settingsDirty ? s.pendingSettings : s.settings;
    const bool reopen = current.device != settings.device || current.steadyFps != settings.steadyFps;
    s.pendingSettings = settings;
    s.settingsDirty = true;
    if (reopen && s.reopenEvent)
        s.reopenEvent.SetEvent();
}

HRESULT Engine::Render(Hns outputTime, uint8_t* dst, LONG pitch, DWORD capacity)
{
    if (!m_impl->gpu)
        return E_NOT_VALID_STATE;
    return m_impl->Render(outputTime, dst, pitch, capacity);
}

EngineStats Engine::Stats() const
{
    std::lock_guard g(m_impl->statsLock);
    auto stats = m_impl->stats;
    stats.cameraOpen = m_impl->cameraOpen;
    return stats;
}

OutputFormat Engine::Format() const
{
    return m_impl->format;
}
} // namespace smoothcam

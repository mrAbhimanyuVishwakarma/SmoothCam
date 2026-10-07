#include "Vision.h"

#include "Log.h"

#include <unknwn.h>
#include <MemoryBuffer.h>

#include <winrt/Windows.AI.MachineLearning.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.FaceAnalysis.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace smoothcam
{
namespace ml = winrt::Windows::AI::MachineLearning;
namespace imaging = winrt::Windows::Graphics::Imaging;
using winrt::Windows::Media::FaceAnalysis::FaceDetector;

namespace
{
struct Image
{
    std::vector<uint8_t> pixels; // tightly packed
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
};

void CopyImage(Image& dst, const uint8_t* src, uint32_t w, uint32_t h, uint32_t stride, uint32_t channels)
{
    dst.width = w;
    dst.height = h;
    dst.channels = channels;
    dst.pixels.resize(static_cast<size_t>(w) * h * channels);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(dst.pixels.data() + static_cast<size_t>(y) * w * channels, src + static_cast<size_t>(y) * stride,
               static_cast<size_t>(w) * channels);
}

// Bilinear sample of one channel, (x, y) in pixels.
float Sample(const Image& img, float x, float y, uint32_t c)
{
    x = std::clamp(x, 0.0f, img.width - 1.0f);
    y = std::clamp(y, 0.0f, img.height - 1.0f);
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
    const float fx = x - x0, fy = y - y0;
    auto at = [&](uint32_t px, uint32_t py) {
        return static_cast<float>(img.pixels[(static_cast<size_t>(py) * img.width + px) * img.channels + c]);
    };
    return (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
}
} // namespace

struct VisionWorker::Impl
{
    mutable std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool stop = false;

    // Configuration
    bool wantFaces = false;
    bool wantSegmentation = false;
    std::filesystem::path modelPath;

    // Inputs (latest only)
    Image luma;
    Image rgba;
    bool hasLuma = false;
    bool hasRgba = false;

    // Results
    std::vector<FaceBox> faces;
    Hns facesTime = 0;
    std::vector<uint8_t> mask;
    uint32_t maskW = 0;
    uint32_t maskH = 0;
    bool maskFresh = false;
    std::wstring problem;

    // Worker-thread state
    FaceDetector detector{nullptr};
    bool detectorFailed = false;
    ml::LearningModel model{nullptr};
    ml::LearningModelSession session{nullptr};
    std::wstring inputName, outputName;
    std::vector<int64_t> inputShape;
    bool nchw = false;
    bool modelFailed = false;
    std::vector<float> smoothedMask;

    void SetProblem(std::wstring text)
    {
        std::lock_guard g(lock);
        if (problem != text && !text.empty())
            SC_LOG_INFO(L"Vision: %s", text.c_str());
        problem = std::move(text);
    }

    void Run()
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        for (;;)
        {
            Image l, c;
            bool doFaces = false, doSeg = false;
            {
                std::unique_lock g(lock);
                wake.wait(g, [&] { return stop || (hasLuma && wantFaces) || (hasRgba && wantSegmentation); });
                if (stop)
                    break;
                if (hasLuma && wantFaces)
                {
                    std::swap(l, luma);
                    hasLuma = false;
                    doFaces = true;
                }
                if (hasRgba && wantSegmentation)
                {
                    std::swap(c, rgba);
                    hasRgba = false;
                    doSeg = true;
                }
            }
            try
            {
                if (doFaces)
                    DetectFaces(l);
                if (doSeg)
                    Segment(c);
            }
            catch (const winrt::hresult_error& e)
            {
                SC_LOG_ERROR(L"Vision: %s (0x%08X)", e.message().c_str(), static_cast<uint32_t>(e.code()));
            }
        }
        detector = nullptr;
        session = nullptr;
        model = nullptr;
        winrt::uninit_apartment();
    }

    void DetectFaces(const Image& img)
    {
        if (detectorFailed)
            return;
        if (!detector)
        {
            try
            {
                if (!FaceDetector::IsSupported())
                    throw winrt::hresult_error(E_NOTIMPL);
                detector = FaceDetector::CreateAsync().get();
                detector.MinDetectableFaceSize(imaging::BitmapSize{24, 24});
                SC_LOG_INFO(L"Vision: face detector ready");
            }
            catch (...)
            {
                detectorFailed = true;
                SetProblem(L"Auto-framing isn't available on this PC (no face detector)");
                return;
            }
        }

        imaging::SoftwareBitmap bitmap(imaging::BitmapPixelFormat::Gray8, static_cast<int32_t>(img.width),
                                       static_cast<int32_t>(img.height));
        {
            auto buffer = bitmap.LockBuffer(imaging::BitmapBufferAccessMode::Write);
            const auto plane = buffer.GetPlaneDescription(0);
            auto reference = buffer.CreateReference();
            auto access = reference.as<::Windows::Foundation::IMemoryBufferByteAccess>();
            uint8_t* data = nullptr;
            UINT32 capacity = 0;
            winrt::check_hresult(access->GetBuffer(&data, &capacity));
            for (uint32_t y = 0; y < img.height; ++y)
                memcpy(data + plane.StartIndex + static_cast<size_t>(y) * plane.Stride,
                       img.pixels.data() + static_cast<size_t>(y) * img.width, img.width);
        }

        const auto detected = detector.DetectFacesAsync(bitmap).get();
        std::vector<FaceBox> result;
        for (const auto& face : detected)
        {
            const auto b = face.FaceBox();
            result.push_back({static_cast<float>(b.X) / img.width, static_cast<float>(b.Y) / img.height,
                              static_cast<float>(b.Width) / img.width, static_cast<float>(b.Height) / img.height});
        }
        std::lock_guard g(lock);
        faces = std::move(result);
        facesTime = QpcNow();
    }

    bool LoadModel()
    {
        if (session)
            return true;
        if (modelFailed)
            return false;
        try
        {
            if (!std::filesystem::exists(modelPath))
                throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), L"model file missing");
            model = ml::LearningModel::LoadFromFilePath(modelPath.wstring());
            try
            {
                session = ml::LearningModelSession(model,
                                                   ml::LearningModelDevice(ml::LearningModelDeviceKind::DirectXHighPerformance));
            }
            catch (...)
            {
                // No usable DirectX 12 device for Windows ML: the model is small enough for the CPU.
                session = ml::LearningModelSession(model, ml::LearningModelDevice(ml::LearningModelDeviceKind::Cpu));
            }
            const auto input = model.InputFeatures().GetAt(0).as<ml::TensorFeatureDescriptor>();
            inputName = input.Name();
            outputName = model.OutputFeatures().GetAt(0).Name();
            inputShape.clear();
            for (auto d : input.Shape())
                inputShape.push_back(d);
            if (inputShape.size() != 4)
                throw winrt::hresult_error(E_UNEXPECTED, L"unexpected model input shape");
            nchw = inputShape[1] == 3;
            SC_LOG_INFO(L"Vision: segmentation model ready (%lldx%lldx%lldx%lld)", inputShape[0], inputShape[1],
                        inputShape[2], inputShape[3]);
            return true;
        }
        catch (const winrt::hresult_error& e)
        {
            modelFailed = true;
            session = nullptr;
            SC_LOG_ERROR(L"Vision: loading %s failed: %s (0x%08X)", modelPath.c_str(), e.message().c_str(),
                         static_cast<uint32_t>(e.code()));
            SetProblem(L"Background blur isn't available (model couldn't be loaded)");
            return false;
        }
    }

    void Segment(const Image& img)
    {
        if (!LoadModel())
            return;
        const uint32_t h = static_cast<uint32_t>(nchw ? inputShape[2] : inputShape[1]);
        const uint32_t w = static_cast<uint32_t>(nchw ? inputShape[3] : inputShape[2]);

        // RGBA8 -> float RGB in [0, 1], resized to the model's input.
        std::vector<float> input(static_cast<size_t>(w) * h * 3);
        const float sx = static_cast<float>(img.width) / w;
        const float sy = static_cast<float>(img.height) / h;
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                const float px = (x + 0.5f) * sx - 0.5f;
                const float py = (y + 0.5f) * sy - 0.5f;
                for (uint32_t c = 0; c < 3; ++c)
                {
                    const float v = Sample(img, px, py, c) / 255.0f;
                    if (nchw)
                        input[(static_cast<size_t>(c) * h + y) * w + x] = v;
                    else
                        input[(static_cast<size_t>(y) * w + x) * 3 + c] = v;
                }
            }
        }

        ml::LearningModelBinding binding(session);
        binding.Bind(inputName, ml::TensorFloat::CreateFromArray(inputShape, input));
        const auto results = session.Evaluate(binding, L"");
        const auto output = results.Outputs().Lookup(outputName).as<ml::TensorFloat>();
        const auto values = output.GetAsVectorView();
        const uint32_t count = values.Size();
        if (count < static_cast<uint32_t>(w) * h)
            return;

        // Some exports emit 2 channels (background, person); take the person one.
        const uint32_t channels = count / (w * h);
        const bool first = smoothedMask.size() != static_cast<size_t>(w) * h;
        smoothedMask.resize(static_cast<size_t>(w) * h, 0.0f);
        std::vector<uint8_t> out(static_cast<size_t>(w) * h);
        for (uint32_t i = 0; i < w * h; ++i)
        {
            float p = values.GetAt(channels == 1 ? i : i * channels + (channels - 1));
            if (p < 0.0f || p > 1.0f)
                p = 1.0f / (1.0f + std::exp(-p)); // logits
            // Light temporal smoothing hides flicker on hair and hands.
            smoothedMask[i] = first ? p : smoothedMask[i] * 0.4f + p * 0.6f;
            out[i] = static_cast<uint8_t>(std::clamp(smoothedMask[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        std::lock_guard g(lock);
        mask = std::move(out);
        maskW = w;
        maskH = h;
        maskFresh = true;
    }
};

VisionWorker::VisionWorker() : m_impl(std::make_unique<Impl>())
{
    m_impl->thread = std::thread([impl = m_impl.get()] { impl->Run(); });
}

VisionWorker::~VisionWorker()
{
    {
        std::lock_guard g(m_impl->lock);
        m_impl->stop = true;
    }
    m_impl->wake.notify_all();
    if (m_impl->thread.joinable())
        m_impl->thread.join();
}

void VisionWorker::Configure(bool faces, bool segmentation, const std::filesystem::path& modelPath)
{
    std::lock_guard g(m_impl->lock);
    m_impl->wantFaces = faces;
    m_impl->wantSegmentation = segmentation;
    m_impl->modelPath = modelPath;
    if (!segmentation)
        m_impl->maskFresh = false;
}

void VisionWorker::SubmitLuma(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride)
{
    {
        std::lock_guard g(m_impl->lock);
        if (!m_impl->wantFaces)
            return;
        CopyImage(m_impl->luma, pixels, width, height, stride, 1);
        m_impl->hasLuma = true;
    }
    m_impl->wake.notify_one();
}

void VisionWorker::SubmitRgba(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride)
{
    {
        std::lock_guard g(m_impl->lock);
        if (!m_impl->wantSegmentation)
            return;
        CopyImage(m_impl->rgba, pixels, width, height, stride, 4);
        m_impl->hasRgba = true;
    }
    m_impl->wake.notify_one();
}

bool VisionWorker::TakeMask(std::vector<uint8_t>& mask, uint32_t& width, uint32_t& height)
{
    std::lock_guard g(m_impl->lock);
    if (!m_impl->maskFresh)
        return false;
    mask = m_impl->mask;
    width = m_impl->maskW;
    height = m_impl->maskH;
    m_impl->maskFresh = false;
    return true;
}

std::vector<FaceBox> VisionWorker::Faces(Hns& when) const
{
    std::lock_guard g(m_impl->lock);
    when = m_impl->facesTime;
    return m_impl->faces;
}

std::wstring VisionWorker::Problem() const
{
    std::lock_guard g(m_impl->lock);
    return m_impl->problem;
}
} // namespace smoothcam

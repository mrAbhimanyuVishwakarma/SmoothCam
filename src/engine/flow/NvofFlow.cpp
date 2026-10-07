#include "NvofFlow.h"

#include "Log.h"
#include "NvofConvert.h"

// Resolves <cuda.h> to flow/cudalite/cuda.h (see CMakeLists).
#include <nvOpticalFlowCuda.h>

#include <array>

namespace smoothcam
{
namespace
{
#define CUDA_FN(name) decltype(&Stub_##name) name = nullptr

// Prototypes only exist to give decltype() a type; nothing calls them.
CUresult __stdcall Stub_cuInit(unsigned int);
CUresult __stdcall Stub_cuD3D11GetDevice(CUdevice*, IDXGIAdapter*);
CUresult __stdcall Stub_cuCtxCreate(CUcontext*, unsigned int, CUdevice);
CUresult __stdcall Stub_cuCtxDestroy(CUcontext);
CUresult __stdcall Stub_cuCtxPushCurrent(CUcontext);
CUresult __stdcall Stub_cuCtxPopCurrent(CUcontext*);
CUresult __stdcall Stub_cuCtxSynchronize();
CUresult __stdcall Stub_cuGraphicsD3D11RegisterResource(CUgraphicsResource*, ID3D11Resource*, unsigned int);
CUresult __stdcall Stub_cuGraphicsUnregisterResource(CUgraphicsResource);
CUresult __stdcall Stub_cuGraphicsMapResources(unsigned int, CUgraphicsResource*, CUstream);
CUresult __stdcall Stub_cuGraphicsUnmapResources(unsigned int, CUgraphicsResource*, CUstream);
CUresult __stdcall Stub_cuGraphicsSubResourceGetMappedArray(CUarray*, CUgraphicsResource, unsigned int,
                                                            unsigned int);
CUresult __stdcall Stub_cuMemcpy2D(const CUDA_MEMCPY2D*);

struct CudaApi
{
    HMODULE module = nullptr;
    CUDA_FN(cuInit);
    CUDA_FN(cuD3D11GetDevice);
    CUDA_FN(cuCtxCreate);
    CUDA_FN(cuCtxDestroy);
    CUDA_FN(cuCtxPushCurrent);
    CUDA_FN(cuCtxPopCurrent);
    CUDA_FN(cuCtxSynchronize);
    CUDA_FN(cuGraphicsD3D11RegisterResource);
    CUDA_FN(cuGraphicsUnregisterResource);
    CUDA_FN(cuGraphicsMapResources);
    CUDA_FN(cuGraphicsUnmapResources);
    CUDA_FN(cuGraphicsSubResourceGetMappedArray);
    CUDA_FN(cuMemcpy2D);

    bool Load()
    {
        module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            return false;
        auto get = [this](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(module, name));
            return fn != nullptr;
        };
        return get(cuInit, "cuInit") && get(cuD3D11GetDevice, "cuD3D11GetDevice") &&
               get(cuCtxCreate, "cuCtxCreate_v2") && get(cuCtxDestroy, "cuCtxDestroy_v2") &&
               get(cuCtxPushCurrent, "cuCtxPushCurrent_v2") && get(cuCtxPopCurrent, "cuCtxPopCurrent_v2") &&
               get(cuCtxSynchronize, "cuCtxSynchronize") &&
               get(cuGraphicsD3D11RegisterResource, "cuGraphicsD3D11RegisterResource") &&
               get(cuGraphicsUnregisterResource, "cuGraphicsUnregisterResource") &&
               get(cuGraphicsMapResources, "cuGraphicsMapResources") &&
               get(cuGraphicsUnmapResources, "cuGraphicsUnmapResources") &&
               get(cuGraphicsSubResourceGetMappedArray, "cuGraphicsSubResourceGetMappedArray") &&
               get(cuMemcpy2D, "cuMemcpy2D_v2");
    }

    ~CudaApi()
    {
        if (module)
            FreeLibrary(module);
    }
};

using PFN_NvOFAPICreateInstanceCuda = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_CUDA_API_FUNCTION_LIST*);

#define CU_CHECK(expr)                                                                                    \
    do                                                                                                    \
    {                                                                                                     \
        const CUresult _r = (expr);                                                                       \
        if (_r != CUDA_SUCCESS)                                                                           \
        {                                                                                                 \
            SC_LOG_ERROR(L"NVOF: %S failed (%d)", #expr, _r);                                             \
            return E_FAIL;                                                                                \
        }                                                                                                 \
    } while (0)

#define OF_CHECK(expr)                                                                                    \
    do                                                                                                    \
    {                                                                                                     \
        const NV_OF_STATUS _s = (expr);                                                                   \
        if (_s != NV_OF_SUCCESS)                                                                          \
        {                                                                                                 \
            SC_LOG_ERROR(L"NVOF: %S failed (%d)", #expr, static_cast<int>(_s));                           \
            return E_FAIL;                                                                                \
        }                                                                                                 \
    } while (0)

struct Session
{
    NvOFHandle handle = nullptr;
    std::array<NvOFGPUBufferHandle, 2> input{};
    std::array<CUdeviceptr, 2> inputPtr{};
    std::array<uint32_t, 2> inputPitch{};
    NvOFGPUBufferHandle output = nullptr;
    CUdeviceptr outputPtr = 0;
    uint32_t outputPitch = 0;
};

struct ConvertParams
{
    uint32_t w, h;
    float scale;
    float pad;
};
} // namespace

struct NvofFlow::Impl
{
    CudaApi cuda;
    HMODULE ofModule = nullptr;
    NV_OF_CUDA_API_FUNCTION_LIST of{};
    CUcontext context = nullptr;

    // One session per direction so each keeps its own temporal hints.
    Session forward;
    Session backward;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t gridW = 0;
    uint32_t gridH = 0;

    // D3D side: inputs copied from the pyramid, raw S10.5 outputs, converted fields.
    Texture inA, inB, outF, outB;
    std::array<CUgraphicsResource, 4> registered{};
    Texture forwardField, backwardField;

    ComPtr<ID3D11ComputeShader> convert;
    ConstantBuffer<ConvertParams> convertCb;

    ~Impl()
    {
        if (context)
        {
            cuda.cuCtxPushCurrent(context);
            DestroySessions();
            UnregisterAll();
            CUcontext popped = nullptr;
            cuda.cuCtxPopCurrent(&popped);
            cuda.cuCtxDestroy(context);
        }
        if (ofModule)
            FreeLibrary(ofModule);
    }

    void DestroySession(Session& s)
    {
        for (auto& b : s.input)
            if (b)
                of.nvOFDestroyGPUBufferCuda(b);
        if (s.output)
            of.nvOFDestroyGPUBufferCuda(s.output);
        if (s.handle)
            of.nvOFDestroy(s.handle);
        s = Session{};
    }

    void DestroySessions()
    {
        DestroySession(forward);
        DestroySession(backward);
    }

    void UnregisterAll()
    {
        for (auto& r : registered)
        {
            if (r)
                cuda.cuGraphicsUnregisterResource(r);
            r = nullptr;
        }
    }

    HRESULT CreateSession(Session& s, NV_OF_PERF_LEVEL perf)
    {
        OF_CHECK(of.nvCreateOpticalFlowCuda(context, &s.handle));

        NV_OF_INIT_PARAMS init{};
        init.width = width;
        init.height = height;
        init.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
        init.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
        init.mode = NV_OF_MODE_OPTICALFLOW;
        init.perfLevel = perf;
        init.enableExternalHints = NV_OF_FALSE;
        init.enableOutputCost = NV_OF_FALSE;
        init.disparityRange = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
        init.enableRoi = NV_OF_FALSE;
        OF_CHECK(of.nvOFInit(s.handle, &init));

        NV_OF_BUFFER_DESCRIPTOR in{width, height, NV_OF_BUFFER_USAGE_INPUT, NV_OF_BUFFER_FORMAT_GRAYSCALE8};
        for (int i = 0; i < 2; ++i)
        {
            OF_CHECK(of.nvOFCreateGPUBufferCuda(s.handle, &in, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &s.input[i]));
            s.inputPtr[i] = of.nvOFGPUBufferGetCUdeviceptr(s.input[i]);
            NV_OF_CUDA_BUFFER_STRIDE_INFO stride{};
            OF_CHECK(of.nvOFGPUBufferGetStrideInfo(s.input[i], &stride));
            s.inputPitch[i] = stride.strideInfo[0].strideXInBytes;
        }

        NV_OF_BUFFER_DESCRIPTOR out{gridW, gridH, NV_OF_BUFFER_USAGE_OUTPUT, NV_OF_BUFFER_FORMAT_SHORT2};
        OF_CHECK(of.nvOFCreateGPUBufferCuda(s.handle, &out, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &s.output));
        s.outputPtr = of.nvOFGPUBufferGetCUdeviceptr(s.output);
        NV_OF_CUDA_BUFFER_STRIDE_INFO stride{};
        OF_CHECK(of.nvOFGPUBufferGetStrideInfo(s.output, &stride));
        s.outputPitch = stride.strideInfo[0].strideXInBytes;
        return S_OK;
    }

    HRESULT CopyArrayToDevice(CUarray src, CUdeviceptr dst, uint32_t pitch, uint32_t widthBytes, uint32_t rows)
    {
        CUDA_MEMCPY2D c{};
        c.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        c.srcArray = src;
        c.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        c.dstDevice = dst;
        c.dstPitch = pitch;
        c.WidthInBytes = widthBytes;
        c.Height = rows;
        CU_CHECK(cuda.cuMemcpy2D(&c));
        return S_OK;
    }

    HRESULT CopyDeviceToArray(CUdeviceptr src, uint32_t pitch, CUarray dst, uint32_t widthBytes, uint32_t rows)
    {
        CUDA_MEMCPY2D c{};
        c.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        c.srcDevice = src;
        c.srcPitch = pitch;
        c.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        c.dstArray = dst;
        c.WidthInBytes = widthBytes;
        c.Height = rows;
        CU_CHECK(cuda.cuMemcpy2D(&c));
        return S_OK;
    }

    HRESULT Execute(Session& s)
    {
        NV_OF_EXECUTE_INPUT_PARAMS in{};
        in.inputFrame = s.input[0];
        in.referenceFrame = s.input[1];
        in.disableTemporalHints = NV_OF_FALSE;
        NV_OF_EXECUTE_OUTPUT_PARAMS out{};
        out.outputBuffer = s.output;
        OF_CHECK(of.nvOFExecute(s.handle, &in, &out));
        return S_OK;
    }
};

bool IsNvofRuntimePresent()
{
    HMODULE m = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m)
        return false;
    const bool ok = GetProcAddress(m, "NvOFAPICreateInstanceCuda") != nullptr;
    FreeLibrary(m);
    return ok;
}

NvofFlow::NvofFlow(const GpuDevice& gpu, int workLevel) : m_gpu(gpu), m_level(workLevel) {}

NvofFlow::~NvofFlow() = default;

HRESULT NvofFlow::Init()
{
    if (!m_gpu.Info().IsNvidia())
        return E_NOTIMPL;

    auto impl = std::make_unique<Impl>();
    if (!impl->cuda.Load())
    {
        SC_LOG_INFO(L"NVOF: nvcuda.dll not available");
        return E_NOTIMPL;
    }
    impl->ofModule = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!impl->ofModule)
    {
        SC_LOG_INFO(L"NVOF: nvofapi64.dll not available");
        return E_NOTIMPL;
    }
    auto create = reinterpret_cast<PFN_NvOFAPICreateInstanceCuda>(
        GetProcAddress(impl->ofModule, "NvOFAPICreateInstanceCuda"));
    if (!create)
        return E_NOTIMPL;
    OF_CHECK(create(NV_OF_API_VERSION, &impl->of));

    CU_CHECK(impl->cuda.cuInit(0));
    CUdevice device = 0;
    // Fails if our D3D adapter isn't an NVIDIA CUDA device (e.g. iGPU picked).
    CU_CHECK(impl->cuda.cuD3D11GetDevice(&device, m_gpu.Adapter()));
    CU_CHECK(impl->cuda.cuCtxCreate(&impl->context, 0, device));
    CUcontext popped = nullptr;
    impl->cuda.cuCtxPopCurrent(&popped);

    RETURN_IF_FAILED(m_gpu.CreateShader(g_NvofConvert, sizeof(g_NvofConvert), impl->convert));
    RETURN_IF_FAILED(impl->convertCb.Create(m_gpu.Device()));

    m_impl = std::move(impl);
    SC_LOG_INFO(L"NVOF: initialized");
    return S_OK;
}

HRESULT NvofFlow::Estimate(const LumaPyramid& a, const LumaPyramid& b, const Texture*, RawFlow& out)
{
    auto& s = *m_impl;
    auto& cuda = s.cuda;
    const auto& la = a.level[m_level];
    const auto& lb = b.level[m_level];
    auto* device = m_gpu.Device();
    auto* context = m_gpu.Context();

    struct ContextScope
    {
        CudaApi& api;
        explicit ContextScope(CudaApi& a, CUcontext c) : api(a) { api.cuCtxPushCurrent(c); }
        ~ContextScope()
        {
            CUcontext popped = nullptr;
            api.cuCtxPopCurrent(&popped);
        }
    } scope(cuda, s.context);

    if (s.width != la.width || s.height != la.height)
    {
        s.DestroySessions();
        s.UnregisterAll();
        s.width = la.width;
        s.height = la.height;
        s.gridW = (s.width + 3) / 4;
        s.gridH = (s.height + 3) / 4;
        const auto perf = m_level == 0 ? NV_OF_PERF_LEVEL_MEDIUM : NV_OF_PERF_LEVEL_FAST;
        RETURN_IF_FAILED(s.CreateSession(s.forward, perf));
        RETURN_IF_FAILED(s.CreateSession(s.backward, perf));

        RETURN_IF_FAILED(s.inA.Create(device, s.width, s.height, DXGI_FORMAT_R8_UNORM, false));
        RETURN_IF_FAILED(s.inB.Create(device, s.width, s.height, DXGI_FORMAT_R8_UNORM, false));
        RETURN_IF_FAILED(s.outF.Create(device, s.gridW, s.gridH, DXGI_FORMAT_R16G16_SINT, false));
        RETURN_IF_FAILED(s.outB.Create(device, s.gridW, s.gridH, DXGI_FORMAT_R16G16_SINT, false));
        RETURN_IF_FAILED(s.forwardField.Create(device, s.gridW, s.gridH, DXGI_FORMAT_R16G16_FLOAT));
        RETURN_IF_FAILED(s.backwardField.Create(device, s.gridW, s.gridH, DXGI_FORMAT_R16G16_FLOAT));

        ID3D11Resource* textures[] = {s.inA.tex.get(), s.inB.tex.get(), s.outF.tex.get(), s.outB.tex.get()};
        for (size_t i = 0; i < 4; ++i)
            CU_CHECK(cuda.cuGraphicsD3D11RegisterResource(&s.registered[i], textures[i], 0));
        SC_LOG_INFO(L"NVOF: sessions %ux%u, grid %ux%u", s.width, s.height, s.gridW, s.gridH);
    }

    context->CopyResource(s.inA.tex.get(), la.tex.get());
    context->CopyResource(s.inB.tex.get(), lb.tex.get());

    // Map waits for the D3D copies above; unmap orders CUDA before later D3D work.
    CU_CHECK(cuda.cuGraphicsMapResources(4, s.registered.data(), nullptr));
    HRESULT hr = [&]() -> HRESULT {
        std::array<CUarray, 4> arrays{};
        for (size_t i = 0; i < 4; ++i)
            CU_CHECK(cuda.cuGraphicsSubResourceGetMappedArray(&arrays[i], s.registered[i], 0, 0));

        // forward: input A, reference B. backward: input B, reference A.
        RETURN_IF_FAILED(s.CopyArrayToDevice(arrays[0], s.forward.inputPtr[0], s.forward.inputPitch[0], s.width, s.height));
        RETURN_IF_FAILED(s.CopyArrayToDevice(arrays[1], s.forward.inputPtr[1], s.forward.inputPitch[1], s.width, s.height));
        RETURN_IF_FAILED(s.CopyArrayToDevice(arrays[1], s.backward.inputPtr[0], s.backward.inputPitch[0], s.width, s.height));
        RETURN_IF_FAILED(s.CopyArrayToDevice(arrays[0], s.backward.inputPtr[1], s.backward.inputPitch[1], s.width, s.height));

        RETURN_IF_FAILED(s.Execute(s.forward));
        RETURN_IF_FAILED(s.Execute(s.backward));

        RETURN_IF_FAILED(s.CopyDeviceToArray(s.forward.outputPtr, s.forward.outputPitch, arrays[2], s.gridW * 4, s.gridH));
        RETURN_IF_FAILED(s.CopyDeviceToArray(s.backward.outputPtr, s.backward.outputPitch, arrays[3], s.gridW * 4, s.gridH));
        return S_OK;
    }();
    cuda.cuGraphicsUnmapResources(4, s.registered.data(), nullptr);
    RETURN_IF_FAILED(hr);

    const float scale = kSign / 32.0f * static_cast<float>(1u << (m_level + 1));
    s.convertCb.Update(context, {s.gridW, s.gridH, scale, 0});
    RunCompute(context, s.convert.get(), s.convertCb.Get(), {s.outF.srv.get()}, {s.forwardField.uav.get()}, nullptr,
               s.gridW, s.gridH);
    RunCompute(context, s.convert.get(), s.convertCb.Get(), {s.outB.srv.get()}, {s.backwardField.uav.get()},
               nullptr, s.gridW, s.gridH);

    out.forward = &s.forwardField;
    out.backward = &s.backwardField;
    return S_OK;
}
} // namespace smoothcam

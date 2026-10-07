#include "CameraReader.h"

#include "capture/Camera.h"

#include <mfapi.h>
#include <mferror.h>

#include <wil/result.h>

#include <cmath>
#include <cstring>

namespace smoothcam
{
namespace
{
constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
}

HRESULT CameraReader::Open(uint32_t preferredWidth, bool nv12)
{
    Close();
    m_nv12 = nv12;
    const GUID wanted = nv12 ? MFVideoFormat_NV12 : MFVideoFormat_RGB32;
    std::wstring link;
    for (const auto& cam : EnumerateAllCameras())
    {
        if (IsSmoothCamDevice(cam.name, {}))
        {
            link = cam.symbolicLink;
            m_name = cam.name;
            break;
        }
    }
    RETURN_HR_IF(MF_E_NO_CAPTURE_DEVICES_AVAILABLE, link.empty());

    wil::com_ptr_nothrow<IMFAttributes> attrs;
    RETURN_IF_FAILED(MFCreateAttributes(&attrs, 2));
    RETURN_IF_FAILED(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    RETURN_IF_FAILED(attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link.c_str()));
    RETURN_IF_FAILED(MFCreateDeviceSource(attrs.get(), &m_source));
    RETURN_IF_FAILED(MFCreateSourceReaderFromMediaSource(m_source.get(), nullptr, &m_reader));

    wil::com_ptr_nothrow<IMFMediaType> best;
    double bestScore = -1e18;
    for (DWORD i = 0;; ++i)
    {
        wil::com_ptr_nothrow<IMFMediaType> type;
        if (FAILED(m_reader->GetNativeMediaType(kVideoStream, i, &type)))
            break;
        GUID subtype{};
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (subtype != wanted)
            continue;
        UINT32 w = 0, h = 0, num = 0, den = 1;
        MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &num, &den);
        const double fps = den ? static_cast<double>(num) / den : 0;
        // preferredWidth 0 = take the first (default) format, like most apps do.
        const double score = preferredWidth == 0 ? -static_cast<double>(i)
                                                 : fps * 1e4 - std::abs(static_cast<double>(w) - preferredWidth);
        if (score > bestScore)
        {
            bestScore = score;
            best = type;
            m_width = w;
            m_height = h;
            m_fps = fps;
        }
    }
    RETURN_HR_IF(MF_E_INVALIDMEDIATYPE, !best);
    RETURN_IF_FAILED(m_reader->SetCurrentMediaType(kVideoStream, nullptr, best.get()));
    UINT32 stride = 0;
    m_stride = SUCCEEDED(best->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<LONG>(stride)
                                                                        : static_cast<LONG>(m_width * (nv12 ? 1 : 4));
    return S_OK;
}

void CameraReader::Close()
{
    m_reader.reset();
    if (m_source)
    {
        m_source->Shutdown();
        m_source.reset();
    }
}

HRESULT CameraReader::Read(Frame& frame)
{
    RETURN_HR_IF(E_NOT_VALID_STATE, !m_reader);
    for (;;)
    {
        DWORD flags = 0;
        LONGLONG ts = 0;
        wil::com_ptr_nothrow<IMFSample> sample;
        RETURN_IF_FAILED(m_reader->ReadSample(kVideoStream, 0, nullptr, &flags, &ts, &sample));
        RETURN_HR_IF(MF_E_END_OF_STREAM, (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) != 0);
        if (!sample)
            continue; // stream tick

        wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
        RETURN_IF_FAILED(sample->ConvertToContiguousBuffer(&buffer));
        BYTE* data = nullptr;
        DWORD length = 0;
        RETURN_IF_FAILED(buffer->Lock(&data, nullptr, &length));
        frame.width = m_width;
        frame.height = m_height;
        frame.timestamp = ts;
        const LONG absStride = std::abs(m_stride);
        if (m_nv12)
        {
            // Y rows then interleaved UV rows, both with the same stride.
            const uint32_t rows = m_height + (m_height + 1) / 2;
            frame.pixels.resize(static_cast<size_t>(m_width) * rows);
            if (m_stride > 0 && length >= static_cast<DWORD>(absStride) * rows)
            {
                for (uint32_t y = 0; y < rows; ++y)
                    memcpy(frame.pixels.data() + static_cast<size_t>(y) * m_width,
                           data + static_cast<size_t>(y) * absStride, m_width);
            }
            buffer->Unlock();
            return S_OK;
        }
        const uint32_t row = m_width * 4;
        frame.pixels.resize(static_cast<size_t>(row) * m_height);
        if (length >= static_cast<DWORD>(absStride) * m_height)
        {
            for (uint32_t y = 0; y < m_height; ++y)
            {
                // Negative stride = bottom-up buffer.
                const uint32_t srcRow = m_stride < 0 ? m_height - 1 - y : y;
                memcpy(frame.pixels.data() + static_cast<size_t>(y) * row, data + static_cast<size_t>(srcRow) * absStride,
                       row);
            }
        }
        buffer->Unlock();
        return S_OK;
    }
}
} // namespace smoothcam

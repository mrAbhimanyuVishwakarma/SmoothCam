#include "capture/Camera.h"

#include <mfapi.h>

#include <gtest/gtest.h>

using namespace smoothcam;

TEST(CameraFormat, PrefersCoveringResolutionThenFrameRate)
{
    std::vector<CameraFormat> f = {
        {640, 480, 30, MFVideoFormat_YUY2},
        {1280, 720, 30, MFVideoFormat_MJPG},
        {1920, 1080, 30, MFVideoFormat_MJPG},
        {1920, 1080, 5, MFVideoFormat_YUY2},
        {1280, 720, 10, MFVideoFormat_YUY2},
    };
    EXPECT_EQ(ChooseCameraFormat(f, 1280, 720, 60), 1u);
    EXPECT_EQ(ChooseCameraFormat(f, 1920, 1080, 60), 2u);
}

TEST(CameraFormat, TakesNative60WhenAvailable)
{
    std::vector<CameraFormat> f = {
        {1280, 720, 30, MFVideoFormat_YUY2},
        {1280, 720, 60, MFVideoFormat_MJPG},
    };
    EXPECT_EQ(ChooseCameraFormat(f, 1280, 720, 60), 1u);
    // ...but not when we only output 30.
    EXPECT_EQ(ChooseCameraFormat(f, 1280, 720, 30), 0u);
}

TEST(CameraFormat, FallsBackToLargestWhenNothingCovers)
{
    std::vector<CameraFormat> f = {
        {640, 480, 30, MFVideoFormat_YUY2},
        {1280, 720, 30, MFVideoFormat_MJPG},
    };
    EXPECT_EQ(ChooseCameraFormat(f, 1920, 1080, 60), 1u);
}

TEST(CameraFormat, SkipsUnusableFormats)
{
    std::vector<CameraFormat> f = {
        {1920, 1080, 30, MFVideoFormat_H264},
        {1280, 720, 30, MFVideoFormat_NV12},
    };
    EXPECT_EQ(ChooseCameraFormat(f, 1920, 1080, 60), 1u);
    EXPECT_EQ(ChooseCameraFormat({}, 1280, 720, 60), SIZE_MAX);
}

TEST(CameraFormat, ExcludesOnlyItselfAsSource)
{
    EXPECT_TRUE(IsSmoothCamDevice(L"SmoothCam (Windows Virtual Camera)", L""));
    // Other virtual cameras (a phone via Phone Link, OBS) are valid inputs.
    EXPECT_FALSE(IsSmoothCamDevice(L"Phone Link camera", L"\\\\?\\SWD#VCAMDEVAPI#{abc}"));
    EXPECT_TRUE(IsVirtualCamera(L"\\\\?\\SWD#VCAMDEVAPI#{abc}"));
    EXPECT_FALSE(IsVirtualCamera(L"\\\\?\\USB#VID_04F2&PID_B7E0"));
    EXPECT_FALSE(IsSmoothCamDevice(L"Integrated Camera", L"\\\\?\\USB#VID_04F2&PID_B7E0"));
}

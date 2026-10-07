# third_party

## nvof/ - NVIDIA Optical Flow interface headers

`nvOpticalFlowCommon.h` and `nvOpticalFlowCuda.h`, copied unchanged from
[NVIDIA/NVIDIAOpticalFlowSDK](https://github.com/NVIDIA/NVIDIAOpticalFlowSDK) at the commit in
`nvof/COMMIT`. They're BSD-3-Clause licensed (see the header comments and
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)), so they live in the repo and nobody needs an
NVIDIA developer account to build SmoothCam.

They only declare the API. At runtime SmoothCam loads `nvofapi64.dll` and `nvcuda.dll` from the
user's NVIDIA display driver, if there is one - no NVIDIA binaries are shipped.

`nvOpticalFlowCuda.h` includes `<cuda.h>`. Instead of requiring the CUDA Toolkit, the build points
that include at `src/engine/flow/cudalite/cuda.h`, which declares the handful of driver API types
and functions we use.

To build without the NVIDIA path at all: `cmake --preset x64-release -DSMOOTHCAM_WITH_NVOF=OFF`.

## fsr/ - AMD FidelityFX Super Resolution 1

`ffx_a.h` and `ffx_fsr1.h`, copied unchanged from
[GPUOpen-Effects/FidelityFX-FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR) at the commit in
`fsr/COMMIT` (MIT, see `fsr/LICENSE.txt`). The shaders `FsrEasu.hlsl` and `FsrRcas.hlsl` include
them, and `FsrConstants.cpp` uses their CPU half to compute the shader constants.

## cas/ - AMD FidelityFX CAS

`ffx_cas.h`, copied unchanged from
[GPUOpen-Effects/FidelityFX-CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS) at the commit in
`cas/COMMIT` (MIT, see `cas/LICENSE.txt`). It builds on the `ffx_a.h` already in `fsr/`.
`CasUpscale.hlsl` includes it, and `FsrConstants.cpp` uses its CPU half for the constants.

## nis/ - NVIDIA Image Scaling SDK

`NIS_Scaler.h` and `NIS_Config.h` from
[NVIDIAGameWorks/NVIDIAImageScaling](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling) at the
commit in `nis/COMMIT` (MIT, see `nis/LICENSE.txt`). `NIS_Config.h` is unchanged. `NIS_Scaler.h` has
one small, marked change: an `NIS_LUMA_ONLY` switch so its NV12 path writes scaled luma instead of
RGB, because SmoothCam upscales the chroma plane separately. `NisScaler.hlsl` includes it, and
`NisConstants.cpp` uses `NIS_Config.h` for the constants and filter tables.

## assets/models/ - person segmentation model

`selfie_segmenter.onnx` is Google's MediaPipe Selfie Segmentation (landscape), Apache-2.0, prepared
for Windows ML by `scripts/prepare-model.py` (which downloads the onnx-community export, converts it
and checks the result against the original). Its license text is next to it.

## Fetched at configure time

- [WIL](https://github.com/microsoft/wil) (MIT) - header-only, pinned tag in `cmake/Dependencies.cmake`
- [GoogleTest](https://github.com/google/googletest) (BSD-3-Clause) - tests only

# Third-party notices

SmoothCam includes, builds against, or adapts the components listed below. Each entry says
whether it ends up in the installed product. Keep this file in sync with the code: if you adapt
code from somewhere, add it here in the same commit.

SmoothCam ships no third-party binaries. The NVIDIA Optical Flow runtime (`nvofapi64.dll`,
`nvcuda.dll`) is part of the NVIDIA display driver already on the user's PC and is only used if
present. Face detection (`Windows.Media.FaceAnalysis`) and model inference (Windows ML) are parts
of Windows itself.

Everything SmoothCam includes is under a permissive license (MIT, BSD-3-Clause, Apache-2.0) that
allows free public redistribution, including in commercial products, as long as these notices are
kept.

---

## NVIDIA Optical Flow SDK interface headers

- Files: `third_party/nvof/nvOpticalFlowCommon.h`, `third_party/nvof/nvOpticalFlowCuda.h`
- Source: https://github.com/NVIDIA/NVIDIAOpticalFlowSDK
- Shipped: compiled into SmoothCamSource.dll and SmoothCam.exe (declarations only)

```
Copyright(c) 2020, NVIDIA CORPORATION. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and / or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## AMD FidelityFX Super Resolution 1 (FSR 1)

- Files: `third_party/fsr/ffx_a.h`, `third_party/fsr/ffx_fsr1.h`
- Source: https://github.com/GPUOpen-Effects/FidelityFX-FSR
- Shipped: compiled into SmoothCamSource.dll (the EASU upscaler and RCAS sharpener shaders)

```
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## AMD FidelityFX Contrast Adaptive Sharpening (CAS)

- File: `third_party/cas/ffx_cas.h` (uses `ffx_a.h` from the FSR 1 section above)
- Source: https://github.com/GPUOpen-Effects/FidelityFX-CAS
- Shipped: compiled into SmoothCamSource.dll (the CAS upscaler shader, also the "Auto" upscaler)

```
Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## NVIDIA Image Scaling SDK (NIS) v1.0.3

- Files: `third_party/nis/NIS_Scaler.h`, `third_party/nis/NIS_Config.h`
- Source: https://github.com/NVIDIAGameWorks/NVIDIAImageScaling
- Modifications: `NIS_Scaler.h` gained an `NIS_LUMA_ONLY` option so the NV12 path outputs scaled luma
  only (marked "SmoothCam modification" in the file). `NIS_Config.h` is unchanged.
- Shipped: compiled into SmoothCamSource.dll (the optional NIS upscaler shader and its filter tables)

```
The MIT License(MIT)

Copyright(c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## MediaPipe Selfie Segmentation model (landscape)

- File: `models/selfie_segmenter.onnx` (installed next to SmoothCamSource.dll)
- Source: Google MediaPipe, https://ai.google.dev/edge/mediapipe/solutions/vision/image_segmenter,
  via the ONNX export at https://huggingface.co/onnx-community/mediapipe_selfie_segmentation_landscape
- License: Apache License 2.0 - full text in `models/LICENSE-selfie_segmenter.txt`
- Modifications: converted for older Windows ML runtimes by `scripts/prepare-model.py`
  (HardSwish rewritten as x * HardSigmoid(x), explicit Resize inputs, opset 12, batch size 1).
  The outputs are numerically identical to the original export (max difference 6e-8).
- Shipped: yes, used only for the optional background blur.

Copyright Google LLC. Licensed under the Apache License, Version 2.0.

## Windows Implementation Libraries (WIL)

- Source: https://github.com/microsoft/wil
- Shipped: header-only, compiled into SmoothCam.exe and SmoothCamSource.dll

```
MIT License

Copyright (c) Microsoft Corporation. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Windows-Camera samples (VirtualCamera)

- Source: https://github.com/microsoft/Windows-Camera
- Shipped: the structure of SmoothCam's media source, stream and activator
  (`src/vcam`) follows this sample; the wording of attributes and sensor profiles is adapted
  from it.

```
MIT License

Copyright (c) Microsoft Corporation. All rights reserved.

(Full MIT license text as above.)
```

## VCamSample

- Source: https://github.com/smourier/VCamSample
- Shipped: SmoothCam's COM registration and sample-request handling (`src/vcam`) is adapted from
  this sample.

```
MIT License

Copyright (c) 2024-2026 Simon Mourier

(Full MIT license text as above.)
```

## Inno Setup

- Source: https://jrsoftware.org/isinfo.php
- License: Inno Setup License (free, including for commercial use)
- Shipped: only the setup stub inside the installer executable.
- Copyright (C) 1997-2026 Jordan Russell. Portions Copyright (C) 2000-2026 Martijn Laan.

## GoogleTest (tests only)

- Source: https://github.com/google/googletest
- License: BSD-3-Clause
- Shipped: no.

---

## Reference material (no code included)

Studied for ideas only.

- AMD FidelityFX SDK - https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK (MIT)
- Practical-RIFE - https://github.com/hzwer/Practical-RIFE (MIT)
- FFmpeg `fruc_vulkan` - https://ffmpeg.org (LGPL; approach only, no code)
- G. de Haan et al., "True-motion estimation with 3-D recursive search block matching", IEEE
  Trans. Circuits Syst. Video Technol., 1993 - the predictive-candidate idea behind
  `BlockMatch.hlsl`.

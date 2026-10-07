# SmoothCam — Implementation Plan

_Last updated: 8 Oct 2026 · Status: **v0.0.1 released (first public build)** · Website: https://smoothcam.vercel.app · Platform: Windows 11, x64 (ARM64 optional)_

This is the working plan for SmoothCam. It's split into phases that each end in something
runnable, so the project never sits in a half-broken state for long. Tick boxes as you go.

---

## 0. Progress

The beta (`dist/SmoothCam_beta.exe`) implements phases 1–6 and was verified end to end on a
Windows 11 26H2 laptop (RTX 4060 Laptop + Intel UHD, built-in 1080p30 camera):

| Check | Result |
|---|---|
| Unit tests (pacing, settings, camera modes, GPU flow on synthetic motion) | 27/27 pass on the RTX 4060 and on WARP |
| Interpolated vs true middle frame (synthetic) | PSNR 99 dB (plain blend: 21.9 dB) |
| Installed camera, Quality / NVIDIA Optical Flow, 720p | 59.99 fps out from ~28 fps, interval 16.67 ms ± 3 ms, +59 ms |
| Low latency mode | 60.00 fps, +19 ms |
| GPU compute engine (non-NVIDIA path) | 59.98 fps, 5.8 ms per frame |
| Open/close cycles, camera taken over by another app | clean restart / automatic reconnect |
| Silent install, `--register`, settings window, preview, compare view | working |

**Beta 2 additions** (`dist/SmoothCam_beta2.exe`):

| Check | Result |
|---|---|
| FSR 1 upscale vs bilinear on a 2x-downscaled test image | 44.0 dB vs 42.2 dB PSNR (29/29 tests pass) |
| 1080p camera upscaled to 1440p / 4K, 60 fps | 3.0 ms / 5.1 ms per frame |
| New window: resize, full screen, hide panel, panel left/right | working, preview drawn with Direct2D |

**Beta 3 additions** (`dist/SmoothCam_beta3.exe`):

| Check | Result |
|---|---|
| Settings reach apps: only the chosen fps is advertised, chosen size is the default type | fixed (an extra 30 fps type let some apps pick 30) |
| Resolution list from the camera's own formats, `2.1MP 16:9 (1920x1080) (default)`, upscaled 2K/4K | working |
| Noise reduction (motion-compensated temporal + RCAS denoise), live dark-room camera | frame-to-frame noise 0.30 → 0.01 |
| Low-light boost (GPU luma stats → gain with shoulder curve) | unit test: mean 40 → ~75, bright 150 unchanged |
| Background blur (MediaPipe selfie segmenter on Windows ML, GPU then CPU) | unit test: person sharp, background detail < 10%; live: clean outline |
| Auto-framing (Windows FaceDetector + eased crop + FSR) | live: Close-up 1.25× at laptop distance, centred; no jitter |
| All effects at 2560×1440, live camera (lights on, ~28 fps in) | 60 fps out, 5.5 ms GPU per frame, 0 failures |
| 4K through the installed camera, NV12 (what Zoom/Teams/browsers take) | 59.98 fps, p99 21 ms (was 44-48 fps before the copy fix) |
| NVIDIA Image Scaling vs FSR 1 vs bilinear, 2x test image | 44.8 / 44.0 / 42.2 dB PSNR; live 4K: 6.4 vs 4.7 ms per frame, both 60 fps |
| Framing grids, collapse-to-settings while another app streams, desktop-shortcut task | working |
| Unit tests | 35/35 pass |

**v0.0.1 - first public release** (`dist/SmoothCam_v0.0.1.exe`), verified through the installed
camera with `--probe --nv12 --width 0` (what Zoom/Teams/browsers do):

| Check | Result |
|---|---|
| 4K with Auto (CAS), FSR 1, NIS, CAS, Lanczos | 59.6–60.0 fps each |
| 2K and 4K, noise + low light + blur + close-up + compare all on | 60.0 fps, p99 26 ms |
| SmoothCam switched off | plain camera 1920x1080 @ 30 fps, engine "Off", +6 ms |
| Upscaler off with 4K selected | falls back to the camera's own 1920x1080 |
| Low latency, 1080p | 60 fps, +23 ms |
| Background blur on the live camera | edge follows hair/shoulders (joint bilateral), no halo (mask-weighted blur) |
| Auto-framing | unit test: per-frame step change < 0.002, no overshoot (spring) |
| Upscalers on real images (`bench --upscale-image`) | photo: FSR 46.2 > CAS 45.8 > NIS 44.8 > Lanczos 44.6 > bilinear 42.2 dB; webcam: Lanczos 52.5 > CAS 52.0 = NIS 52.0 > FSR 51.5 > bilinear 50.8 dB |
| Unit tests | 37/37 pass |
| Another virtual camera as input | works in-process (bench), **not** inside Frame Server: MFCreateDeviceSource gives E_NOINTERFACE, creating its source directly gives MF_E_ATTRIBUTENOTFOUND. Documented as a limitation; UI says so |

Effects choices (researched for beta 3): face detection uses the inbox
`Windows.Media.FaceAnalysis.FaceDetector` (no model to ship, runs on the CPU on a small grayscale copy every third frame);
segmentation uses Google's MediaPipe selfie segmenter (Apache-2.0, 460 KB) through inbox Windows
ML, so no ONNX Runtime DLL is redistributed. Rejected: NVIDIA Maxine/Broadcast (RTX-only,
proprietary), Windows Studio Effects (NPU-only, not callable by third parties), RVM/MODNet (bigger
and slower for the same result on a talking head).

Upscaler choice (researched for beta 2): **AMD FSR 1 (EASU + RCAS)** - MIT, every GPU, purely
spatial so zero added frames. Considered and rejected: NVIDIA RTX Video Super Resolution SDK
(RTX-only, proprietary DLLs and redistribution terms), NVIDIA Maxine VFX (large AI models,
proprietary), AI upscalers like Real-ESRGAN (too slow/heavy for 60 fps on most GPUs). NVIDIA Image
Scaling (MIT) would have worked equally well; FSR 1 was picked for its noise-aware sharpener.

Still open before calling it 1.0: Teams/Zoom/OBS/browser checks by hand (S1, S4), a clean-VM
install/uninstall, the soak/TDR/sleep tests in Phase 7, accessibility pass, code signing, and the
Store listing.

Changes from the original plan, found while building:

- **NVIDIA path uses the CUDA interface.** NVIDIA publishes the Optical Flow interface headers
  (BSD-3-Clause) on GitHub with the CUDA interface only; the D3D11 interface header is SDK-download
  only. So `NvofFlow` registers D3D11 textures with CUDA (`cuGraphicsD3D11RegisterResource`) and
  loads `nvcuda.dll`/`nvofapi64.dll` from the driver at runtime. A tiny `cudalite/cuda.h` replaces
  the CUDA Toolkit. Headers are vendored, so building needs no NVIDIA account.
- **WARP falls back to blend**, not block matching - a CPU rasterizer can't do the search in real time.
- **Toolchain:** Visual Studio Build Tools 2026 (MSVC 14.51) instead of 2022; both work.
- **Frame Server can't hand us a usable D3D manager reliably across clients**, so the source
  renders on its own device and delivers system-memory samples (one readback per frame, ~0.5 ms).
- **No Mica backdrop:** standard Win32 controls become hard to read on Mica, so the window uses
  solid light/dark colors that follow the Windows theme.
- **Resolution picker is back (beta 3):** with only "max size" settings, apps kept picking the
  smallest or a 30 fps type, so the setting looked broken. Now the picked size and rate are the
  default type and the only rate advertised.
- **"Steady in low light" became "Camera low-light mode"** (inverted). The old wording read as
  "turn on for better low light" but did the opposite; software low-light boost replaced the need
  for it, and interpolation now continues down to ~5 fps instead of pausing below 11.

---

## 1. What we're building

SmoothCam is a **virtual webcam for Windows 11**. It opens your real webcam, generates the
in-between frames on the GPU (motion-compensated frame interpolation), and exposes a new
camera called **"SmoothCam"** that runs at a steady 30 / 48 / 50 / 60 fps. Any app that lets you
pick a camera (Teams, Zoom, Google Meet, Discord, OBS, the Windows Camera app) just selects
"SmoothCam" instead of the physical webcam.

### Goals

- 24 / 25 / 30 fps input (and the 15 fps that webcams drop to in low light) → user-selected output up to **60 fps**.
- Added latency: **≤ 1 source frame + 8 ms** in Quality mode, **≤ 8 ms** in Low-latency mode.
- Light: installer **< 5 MB**, UI idle RAM **< 40 MB**, GPU time **< 4 ms per output frame** at 1080p on a mid-range GPU.
- Works on every Windows 11 PC: NVIDIA hardware path, generic GPU path for AMD/Intel, CPU fallback for software adapters.
- Fully offline. No network calls, no telemetry, no account.
- Shipped once: a signed `.exe` installer (admin), published on GitHub Releases and the Microsoft Store.

### Non-goals for v1

More than 60 fps, upscaling or super-resolution, background blur and other effects, audio processing,
AI/neural models (they would add 20+ MB to a tool meant to be tiny), macOS/Linux, an auto-updater.

---

## 2. Constraints to know upfront

1. **Latency can't be zero, and the reason is unavoidable.** To draw a frame halfway between frame N and
   N+1, we need N+1, so the output trails the camera by one source interval (33.3 ms at 30 fps,
   41.7 ms at 24 fps) plus a few ms of GPU work. Low-latency mode *extrapolates* from the
   last two frames instead. It adds no wait, but sudden motion produces more artifacts.
   - Video and audio drift: a virtual camera carries video only, so the mic reaches the other side
     ~35–45 ms ahead of the picture. ITU-R BT.1359 puts the detectability threshold for audio-leading
     at about 45 ms, so this is mostly invisible, but we document it in the README.
2. **Interpolation can't invent detail.** Motion blur baked into a 30 fps exposure stays blurry, and a fast
   hand wave can warp. We handle that with confidence masks and blend fallbacks rather than
   pretending it doesn't happen.
3. **Webcams lie about frame rate.** Most drop to ~15 fps in low light because auto-exposure lengthens the
   shutter. We pace from timestamps (not a fixed 2×), and offer a "Keep frame rate steady" toggle
   that asks the driver to prioritise frame rate over exposure where supported.
4. **"No updates ever" is a design goal, not a guarantee.** A Windows or GPU-driver change could still break
   something. We keep that risk low by using only documented, stable public APIs, downloading nothing at
   runtime, pinning the toolchain, and timestamping signatures so they stay valid after the
   certificate expires.
5. **Store listing of an EXE requires a real code-signing certificate** (chained to the Microsoft Trusted
   Root Program). Self-signed is rejected. This is the one unavoidable cost of the project, covered in §9.

---

## 3. Architecture

```
                         Windows Frame Server (svchost, LOCAL SERVICE)
                        ┌───────────────────────────────────────────────────────────┐
 Physical webcam ──────►│ SmoothCamSource.dll  (Media Foundation media source)        │
 (USB / built-in)       │                                                           │
                        │  Capture ── IMFSourceReader → NV12 D3D11 textures + ts    │
                        │     │                                                     │
                        │  Frame queue (ring of 4, timestamps)                      │
                        │     │                                                     │
                        │  Flow estimator ── NVOF (NVIDIA)  |  Compute (any GPU)    │
                        │     │                             |  None (CPU fallback)  │
                        │  Compositor ── HLSL warp + occlusion-aware blend @ phase α │
                        │     │                                                     │
                        │  Pacer ── fixed 60/50/48/30 Hz clock → NV12 / RGB32 samples│──► Teams, Zoom,
                        └──────────────────────────▲────────────────────────────────┘    Meet, OBS, Camera
                                                   │ reads
                       C:\ProgramData\SmoothCam\settings.ini
                                                   ▲ writes
                       SmoothCam.exe  (settings, preview, diagnostics, register/unregister)
```

### Why this shape

- **`MFCreateVirtualCamera` (Windows 11 only):** this is the documented, modern way to add a camera.
  No kernel driver and no DirectShow filter hacks, and Frame Server shares the camera with every app.
- **The pipeline runs *inside* the media source** (the "wrapper" pattern from Microsoft's Windows-Camera
  VirtualCamera sample). The camera works even when the UI isn't open, the webcam only powers on while
  an app is actually using SmoothCam, and no inter-process frame transport is needed.
- **Admin installer.** Frame Server runs as `LOCAL SERVICE` and can't read DLLs in user profile
  folders (a known VCamSample issue). An elevated install lets us:
  put the DLL in `Program Files`, register the COM class in `HKLM`, register the camera with
  `MFVirtualCameraLifetime_System` + `MFVirtualCameraAccess_AllUsers` so it survives reboots, and create
  `ProgramData\SmoothCam` with the right ACLs.
- **Two flow providers, one compositor.** This is the same split FFmpeg's `fruc_vulkan` filter landed on in 2026:
  hardware optical flow gives the motion vectors, and our own shader does the compositing. It deliberately avoids
  NVIDIA's proprietary NvOFFRUC library, whose redistribution terms blocked FFmpeg's 2023 attempt. The
  NVOF runtime (`nvofapi64.dll`) ships inside the NVIDIA driver, so **we redistribute nothing from NVIDIA**.
- **D3D11, not D3D12.** Media Foundation interop is native (`IMFDXGIDeviceManager`), NVOF has a D3D11
  interface, and the code is half the size. Windows 11 requires a DX12-class GPU, so feature level 11_0 is a
  safe floor on every supported PC.
- **Plain Win32 UI, not WinUI/Electron/ImGui.** Tiny, no runtime to ship, and native controls get
  Narrator/UI Automation support for free (ImGui would not).

### Modules

| Module | Path | Builds to | Notes |
|---|---|---|---|
| core | `src/core` | static lib | settings, logging, QPC timing, version info. No UI or D3D deps. |
| engine | `src/engine` | static lib | capture, flow, compositor, pacer. Testable offline on recorded clips. |
| vcam | `src/vcam` | `SmoothCamSource.dll` | COM media source loaded by Frame Server. |
| app | `src/app` | `SmoothCam.exe` | control panel, preview, diagnostics, `--register/--unregister` CLI. |
| shaders | `src/engine/shaders` | `.cso` embedded as resources | compiled at build time with `dxc`/`fxc`. |
| installer | `installer/` | `SmoothCam-1.0.0-x64-setup.exe` | Inno Setup 6 script. |
| tests | `tests/` | `smoothcam_tests.exe` + quality harness | GoogleTest; not shipped. |

### Engine details

**Capture**
- `IMFSourceReader` on the camera chosen in settings (stored by symbolic link, not friendly name).
- Prefer native NV12/YUY2. For MJPEG (most 1080p30 USB webcams), let MF decode with a D3D manager attached,
  so it uses hardware MJPEG decode where available.
- Pick the highest-fps native mode at the chosen resolution.
- **Exclude SmoothCam itself from the device list.** Otherwise you get a feedback loop.

**Timing and pacing**
- Use sample timestamps, never arrival time. Track the real source interval with an EMA so jitter and
  low-light rate changes are absorbed.
- Output clock ticks at exactly 1/fps from a fixed epoch. For output time `T`, render virtual time
  `V = T − D`, where `D` = measured source interval + ~2 ms margin (Quality) or ≈ 0 (Low-latency).
- Find frames `k, k+1` with `t_k ≤ V < t_k+1`. The phase is `α = (V − t_k) / (t_k+1 − t_k)`.
  - If `α` is within 0.02 of 0 or 1, pass the original frame through untouched (crisp, and costs nothing).
  - Odd ratios fall out naturally, e.g. 24 → 60 cycles α through 0, .4, .8, .2, .6.
- Low-latency mode extrapolates from `k` using flow `k−1 → k` scaled by α (clamped). On low confidence it repeats the frame.
- Output queue depth is 2 and drops the oldest frame. We never block the client app.

**Flow estimators** (pluggable behind `IFlowEstimator`)

| Provider | Hardware | How |
|---|---|---|
| `NvofFlow` | NVIDIA Turing or newer (GTX 16, RTX 20–50) | `NvOFAPICreateInstanceD3D11`, 4×4 grid, perf level MEDIUM (FAST in Balanced), forward + backward pass on luma. |
| `ComputeFlow` | any D3D11 GPU (AMD, Intel, older NVIDIA) | Luma pyramid → coarse-to-fine 8×8 block matching (±3 px per level, SAD + smoothness penalty against neighbour predictors, in the spirit of 3-D recursive search) → 3×3 vector median → forward + backward → consistency check gives per-block confidence. Runs at ½ res (Quality) or ¼ res (Balanced / integrated GPUs). |
| `NoFlow` | WARP / software adapter, broken drivers | Cross-fade blend or frame repeat. Still paced correctly. |

**Compositor** (one HLSL compute shader shared by all providers, NV12 in → NV12 out)
- For output pixel `p` at phase α, find its source in the previous frame by fixed-point iteration
  `x ← p − α·F_fwd(x)` (4–6 iterations). Do the same in the next frame with `y ← p − (1−α)·F_bwd(y)`.
- Blend `prev(x)` and `next(y)` with weights `(1−α)·conf_f` and `α·conf_b`. If both confidences are low,
  fall back to a plain blend of `prev(p)` and `next(p)`.
- **Scene-cut / chaos guard:** if mean luma difference spikes or global confidence drops below ~30 %, repeat the nearest
  frame for that interval. One frame of judder is better than a smeared one.
- Chroma is processed at half resolution using the same flow, halved.

**Output formats exposed to apps:** NV12 and RGB32 at 640×480, 1280×720 and 1920×1080, at the selected fps.
No mirroring (apps mirror themselves).

**Settings and logs**
- `C:\ProgramData\SmoothCam\settings.ini` holds plain `key=value` pairs. The installer grants Users *Modify* and
  `LOCAL SERVICE` *Read*. The engine picks up changes via `ReadDirectoryChangesW`.
- Keys: `device`, `width`, `height`, `out_fps`, `mode` (quality | balanced | lowlatency | off),
  `engine` (auto | nvidia | gpu | cpu), `steady_fps`, `compare` (split-screen A/B), `log_level`.
- Logs go to `C:\ProgramData\SmoothCam\logs\`, rolling at 1 MB × 3. They contain no frames and no personal data, only device names
  and timings.

**Performance budget at 1080p** (targets, measured in Phase 7)

| Stage | RTX 3060 (NVOF) | Intel Iris Xe (Compute) |
|---|---|---|
| Upload / colour convert | 0.3 ms | 0.6 ms |
| Flow, fwd + bwd | ~1.0 ms | ~3.0 ms (¼ res) |
| Composite | 0.4 ms | 1.5 ms |
| Readback to NV12 sample | 0.5 ms | 1.0 ms |
| **Total per output frame** | **~2.2 ms** | **~6.1 ms** |

The frame budget at 60 fps is 16.7 ms, so both have headroom.

---

## 4. Tech stack and dependencies

Toolchain is pinned so the final build is reproducible years later:
Visual Studio 2022/2026 Build Tools (v143/v145), CMake ≥ 3.28 + Ninja, Windows SDK 10.0.26100, C++20, static CRT (`/MT`, so no VC++ redist).

| Dependency | License | How it's used | Shipped? |
|---|---|---|---|
| [microsoft/wil](https://github.com/microsoft/wil) | MIT | RAII / error helpers, header-only | compiled in → notice required |
| NVIDIA Optical Flow SDK headers ([GitHub](https://github.com/NVIDIA/NVIDIAOpticalFlowSDK)) | BSD-3-Clause | NVOF API declarations, vendored in `third_party/nvof` | compiled in. Runtime comes from the user's driver. |
| [GoogleTest](https://github.com/google/googletest) | BSD-3-Clause | unit tests | no |
| [Inno Setup 6](https://jrsoftware.org/isinfo.php) | Inno Setup License (free, commercial use OK) | installer build tool | no (only its stub inside the setup.exe) |
| Windows SDK (MF, D3D11, DXGI, DWM) | part of Windows | everything else | no |

**Reference projects** (read for ideas. No code copied unless noted in `THIRD_PARTY_NOTICES.md`)

| Project | License | What we learn from it |
|---|---|---|
| [microsoft/Windows-Camera](https://github.com/microsoft/Windows-Camera) — `Samples/VirtualCamera` | MIT | media source structure, wrapping a hardware camera, registration |
| [smourier/VCamSample](https://github.com/smourier/VCamSample) | MIT | minimal `MFCreateVirtualCamera` source, GPU/CPU sample paths, Frame Server pitfalls |
| [NVIDIA Optical Flow SDK](https://developer.nvidia.com/optical-flow-sdk) samples | NVIDIA | NVOF D3D11 setup, buffer formats, perf levels |
| FFmpeg `fruc_vulkan` (2026) | LGPL-2.1+ | the "hardware vectors + own compositor" split. **Approach only, no code.** |
| [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) optical flow | MIT | GPU block-matching structure |
| [hzwer/Practical-RIFE](https://github.com/hzwer/Practical-RIFE) | MIT | quality yardstick only, compared offline in the test harness |
| OBS Studio virtual camera | GPL-2.0 | behaviour reference only. **Never copy code** (GPL would infect the project). |
| Windows-classic-samples DirectShow base classes | MIT | only if the DirectShow shim from Phase 0 / S4 is needed |

House rule: if code is adapted from an MIT/BSD source, its copyright header stays in the file and
`THIRD_PARTY_NOTICES.md` is updated **in the same commit**.

---

## 5. Repository layout

```
SmoothCam/
├── .github/
│   ├── ISSUE_TEMPLATE/bug_report.md
│   └── workflows/build.yml          # Phase 1
├── assets/icons/                    # app + tray icons (original artwork)
├── docs/
│   ├── images/                      # README screenshots
│   └── privacy-policy.md            # hosted via GitHub Pages for the Store listing
├── installer/
│   └── SmoothCam.iss                # Phase 6
├── scripts/                         # build.ps1, sign.ps1, package.ps1
├── src/
│   ├── app/                         # SmoothCam.exe — Win32 UI, preview, CLI
│   ├── core/                        # settings, logging, timing, version
│   ├── engine/
│   │   ├── capture/                 # IMFSourceReader wrapper, format negotiation
│   │   ├── flow/                    # IFlowEstimator, NvofFlow, ComputeFlow, NoFlow
│   │   ├── compositor/              # warp/blend dispatch, scene-cut guard
│   │   ├── pacing/                  # clock, phase calc, output queue
│   │   └── shaders/                 # *.hlsl
│   └── vcam/                        # SmoothCamSource.dll — IMFMediaSource, streams, COM reg
├── tests/
│   ├── unit/                        # pacing maths, settings parser, flow on synthetic images
│   └── quality/                     # offline PSNR/SSIM harness on recorded clips
├── third_party/
│   └── README.md                    # how to fetch the NVIDIA OF SDK headers (not committed)
├── CMakeLists.txt / CMakePresets.json   # Phase 1
├── CHANGELOG.md
├── ImplimentationPlan.md
├── LICENSE
├── README.md
└── THIRD_PARTY_NOTICES.md
```

---

## 6. Phases

Estimates assume one developer, roughly full-time. Each phase ends in a tagged, buildable state.

### Phase 0 — Feasibility spikes (1–1.5 weeks)

Kill the risky unknowns before writing production code. Spike code lives on a throwaway branch.

- [ ] **S1 – Virtual camera end-to-end.** Build the Windows-Camera VirtualCamera sample (or VCamSample), copy the DLL to
      `C:\Program Files\SmoothCamSpike\`, and register it from an elevated process with System lifetime + AllUsers.
      Confirm it shows up and streams in the Camera app, Teams (new), Zoom, Meet in Edge and Chrome, Discord and OBS.
      **Make a real call to a second account**: VCamSample reports Teams showing the preview but not sending it.
- [x] **S2 – Wrap a physical camera** from inside the media source (inside Frame Server). Check there's no deadlock, the
      camera releases when the last client disconnects, and our own virtual camera is excluded from enumeration.
- [x] **S3 – GPU inside Frame Server.** Create a D3D11 device and an NVOF instance from the `LOCAL SERVICE` / session-0
      context. On a hybrid (Optimus) laptop, confirm we can choose the discrete adapter.
- [ ] **S4 – DirectShow visibility.** Check whether DirectShow-only apps (older Zoom builds, some OBS setups) see the MF
      virtual camera. If not, budget +1 week for a small DirectShow source shim built on the MIT base classes.
- [x] **S5 – Quality prototype.** Run block-matching flow and the compositor offline on 5 recorded clips and eyeball the results
      against a simple blend. (Python + OpenCV is fine here. It isn't shipped.)

**Exit:** a go/no-go note for each spike recorded in §11. If S1 fails on Teams, investigate before anything else.

### Phase 1 — Foundation (0.5 week)

- [ ] `git init`, branch `main`, first commit with this skeleton.
- [x] CMake + presets (`x64-debug`, `x64-release`, optionally `arm64-release`), static CRT, `/W4 /permissive- /guard:cf`,
      Spectre mitigations, CET shadow stack compat, ASLR/DEP defaults.
- [x] `src/core`: version header generated from CMake, `Settings` (ini read/write + change notification), logger
      (rolling file + ETW TraceLogging provider), QPC clock helpers.
- [x] GoogleTest wired in via `FetchContent`, with a first unit test on the settings parser.
- [x] GitHub Actions: build Debug + Release on `windows-2022`, run tests, upload artifacts.
- [ ] `.clang-format` enforced in CI (format check job).

**Deliverable:** CI is green on an empty-but-real project.

### Phase 2 — Virtual camera, passthrough only (1.5 weeks)

- [x] `src/vcam`: `IMFMediaSource` + `IMFMediaStream` + `IMFMediaEventGenerator`, plus `IKsControl` stubs Frame Server expects.
      Implement `DllGetClassObject`, `DllRegisterServer` and `DllUnregisterServer` (HKLM, `ThreadingModel=Both`).
- [x] `src/engine/capture`: open the selected camera, negotiate format, get NV12 textures with timestamps. Handle MJPEG.
- [x] Output passthrough frames (no interpolation yet) as NV12 + RGB32 at 480p/720p/1080p.
- [x] `SmoothCam.exe --register` / `--unregister` / `--diag` (prints GPUs, NVOF availability, cameras, current
      registration).
- [x] Device-lost handling: camera unplugged mid-stream → show a "camera disconnected" frame and auto-recover on replug.

**Deliverable:** a "SmoothCam" camera in Teams/Zoom showing the real webcam at its native rate.

### Phase 3 — Timing and pacing (1 week)

- [x] `src/engine/pacing`: fixed-rate output clock, source-interval EMA, phase computation, pass-through window.
- [x] Frame repeat at the target fps as the baseline "engine" (`NoFlow` in repeat mode).
- [x] Unit tests: 24→60, 25→60, 30→60, 30→48, variable 15–30 input, timestamp discontinuities, clock drift over 1 hour.
- [ ] Latency measurement: a debug overlay that burns QPC time into the frame, plus a manual test (phone filming a stopwatch and the
      screen) to confirm the added latency matches the formula.
- [x] "Keep frame rate steady" toggle via the camera's exposure/frame-rate priority controls where the driver supports them.

**Deliverable:** a perfectly paced 60 fps stream (repeated frames), with measured latency documented.

### Phase 4 — Interpolation engines (3 weeks)

- [x] `IFlowEstimator` interface + `NoFlow` blend mode.
- [x] `ComputeFlow`: luma pyramid, block matching, vector median, fwd/bwd consistency → confidence texture.
- [x] Compositor shader: fixed-point warp, confidence-weighted blend, half-res chroma, scene-cut guard.
- [x] `NvofFlow`: dynamic load of `nvofapi64.dll` (absent on non-NVIDIA machines, so no hard link), capability query,
      grid size and perf level by mode, upscaling flow to compositor format.
- [x] Low-latency extrapolation mode.
- [x] Auto engine selection: NVOF if available → Compute if the adapter isn't WARP → NoFlow. Allow manual override.
- [x] Automatic step-down: if GPU time stays above 70 % of the frame budget for 2 s, drop the flow resolution, then the mode, and log it.
- [ ] `tests/quality` harness: record clips at **60 fps**, drop every other frame, interpolate them back, and compute PSNR/SSIM against
      the real dropped frames. Clips: talking head, head turn, hand wave, patterned shirt, low-light noise, moving background.
      **Targets:** SSIM ≥ 0.95 on talking-head clips, never worse than plain blend on any clip.

**Deliverable:** Teams shows a smooth 60 fps SmoothCam on NVIDIA, AMD and Intel machines.

### Phase 5 — Control app (1.5 weeks)

- [x] Win32 window, Common Controls v6, per-monitor DPI v2, Mica backdrop (`DWMWA_SYSTEMBACKDROP_TYPE`), dark/light
      following the system.
- [x] Controls: camera, resolution, output fps (30/48/50/60), mode (Quality / Balanced / Low latency / Off),
      engine (Auto shows what was detected), "Keep frame rate steady", "Compare (split screen)".
- [x] Live preview by opening the SmoothCam virtual camera itself, so you see exactly what Teams sees.
- [x] Stats line: `In 29.97 fps · Out 60.00 fps · +36 ms · GPU 1.8 ms · NVIDIA Optical Flow`.
- [x] About dialog: version, license, third-party notices, privacy statement, "not affiliated with NVIDIA/Microsoft".
- [ ] Accessibility: full keyboard navigation, tab order, labels for Narrator, high-contrast theme check.
- [x] Single-instance and no tray/auto-start. The camera works without the app running, so it doesn't need to sit in the background.

**Deliverable:** a usable app you could hand to a non-technical friend.

### Phase 6 — Installer, admin integration and signing (1 week)

- [x] `installer/SmoothCam.iss` (Inno Setup 6):
  - `PrivilegesRequired=admin`, `ArchitecturesAllowed=x64compatible`, `ArchitecturesInstallIn64BitMode=x64compatible`,
    `MinVersion=10.0.22000` (Windows 11 only).
  - Install `SmoothCam.exe`, `SmoothCamSource.dll`, `LICENSE.txt` and `THIRD_PARTY_NOTICES.txt` to `{autopf}\SmoothCam`.
  - Register the COM DLL (`regserver` flag), then run `SmoothCam.exe --register`.
  - Create `{commonappdata}\SmoothCam` and set ACLs with `icacls` (Users: Modify, `*S-1-5-19` LOCAL SERVICE: Read).
  - Start-menu shortcut, Add/Remove Programs entry with publisher, support URL and version.
  - Uninstall: `--unregister`, unregister COM, remove files; ask before deleting settings/logs.
  - Silent switches work: `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART` (the Store submission needs them).
  - Upgrade-in-place over an older build (same `AppId` GUID, never change it).
- [x] `scripts/sign.ps1`: `signtool sign /fd SHA256 /tr <RFC3161 TSA> /td SHA256` on every PE **and** the setup.exe.
- [ ] Clean-VM test: fresh Windows 11 VM → install → use in Teams → uninstall → no leftovers (camera gone, registry clean).
- [ ] If an `.msi` is ever needed (company IT / Intune), WiX is the option. Read the current WiX EULA first
      (recent versions added an Open Source Maintenance Fee for some commercial users).

**Deliverable:** `SmoothCam-1.0.0-x64-setup.exe`, signed, 2–5 MB.

### Phase 7 — Hardening and test pass (1.5 weeks)

See the test matrix in §7. In addition:
- [ ] 8-hour soak test with flat memory and handle counts (watch in Task Manager / `perfmon`).
- [ ] GPU reset (TDR) recovery: force one (`dxcap -forcetdr`) mid-call, and the stream must come back on its own.
- [ ] Sleep/resume, fast user switching, camera unplug/replug, camera already in use by another app,
      two apps opening SmoothCam at once.
- [ ] Hybrid GPU laptop: on battery vs plugged in, iGPU vs dGPU.
- [ ] Crash handling: a crash in the media source must not take down Frame Server for other cameras. Wrap the engine
      in a failure barrier and drop to passthrough after repeated errors.
- [ ] Static analysis (`/analyze`) clean and no warnings at `/W4`.
- [ ] Run the quality harness on final settings and paste the numbers into the README.

**Deliverable:** release candidate `v1.0.0-rc1`.

### Phase 8 — Release (1 week, plus certificate lead time)

- [ ] Finish the legal checklist (§8) and the Store checklist (§9).
- [ ] README screenshots and a short GIF (original content only).
- [ ] Privacy policy published via GitHub Pages.
- [ ] Tag `v1.0.0` and create a GitHub Release with the signed installer and a SHA-256 checksum. **This URL is permanent**, because the Store
      submission points at it.
- [ ] Submit to Microsoft Store (EXE app). Allow ~3 business days for certification.
- [x] Archive the build environment: note exact VS, SDK, CMake and NVIDIA SDK versions in `CHANGELOG.md` so v1.0 can be rebuilt.

**Total: about 12 weeks.**

### Definition of done (v1.0)

- Smooth 60 fps from a 30 fps webcam in Teams, Zoom, Meet (Edge/Chrome), Discord, OBS and the Camera app.
- Added latency in line with §2 and measured, not guessed.
- Works on NVIDIA, AMD and Intel GPUs and degrades gracefully on software adapters.
- Signed installer that installs, upgrades and uninstalls cleanly in silent mode.
- All third-party notices present. No GPL code. Privacy policy live.

---

## 7. Test matrix

| Area | Cases |
|---|---|
| Apps | Windows Camera, Teams (new), Zoom, Google Meet in Edge and Chrome, Discord, OBS Studio, Skype/Webex if available |
| GPUs | NVIDIA GTX 16xx (Turing, no RTX), RTX 20/30/40/50, AMD RDNA2/RDNA3 (incl. iGPU), Intel Iris Xe, Intel Arc, Basic Render Driver (WARP) |
| Cameras | 1080p30 MJPEG USB (e.g. Logitech C920-class), 720p30 (C270-class), laptop built-in, 24 fps-capable camera, a camera that drops to 15 fps in low light |
| Ratios | 24→60, 25→50, 25→60, 30→48, 30→60, 15→60, and "Off" passthrough |
| System | 100/150/200 % DPI, light/dark/high-contrast, standard user (non-admin) running the app, fresh VM install, upgrade from rc |
| Faults | unplug/replug, camera busy, TDR, sleep/resume, user switch, settings file deleted or corrupted |

---

## 8. Legal and licensing checklist

- [ ] `LICENSE`: MIT, with your legal name in the copyright line (currently "SmoothCam contributors").
- [x] `THIRD_PARTY_NOTICES.md` complete; shipped next to the exe as `.txt` and shown in the About dialog.
- [x] **NVIDIA Optical Flow SDK license:** read it in full. Confirm (a) using the headers in a distributed binary is allowed,
      (b) whether attribution text is required, (c) that we redistribute **no** NVIDIA binaries (we don't, because the runtime
      comes with the driver). If anything is unclear, make the NVOF path a build option. The app works without it.
- [x] **Trademarks:** NVIDIA, GeForce, RTX, Windows, Microsoft Teams, Zoom etc. are used only to describe compatibility.
      No logos. Add "not affiliated with or endorsed by" wording in the README, the About dialog and the Store listing.
- [ ] **Name check:** search the Microsoft Store, GitHub and trademark databases (USPTO, EUIPO, IP India) for "SmoothCam".
      A popular Skyrim camera mod already uses the name on GitHub/Nexus. Not necessarily a blocker, but consider a
      distinctive Store display name (e.g. "SmoothCam – 60 FPS Webcam").
- [x] **Model license:** MediaPipe selfie segmenter is Apache-2.0; license text ships in `models/`, modifications
      listed in THIRD_PARTY_NOTICES. Everything shipped is MIT / BSD-3-Clause / Apache-2.0, so the app can be
      redistributed publicly, including commercially.
- [x] **Clean-room rule:** no code from GPL/LGPL projects (OBS, FFmpeg). Ideas are fine; code is not.
- [ ] **Patents:** the techniques used here (block matching, 3-D recursive search, motion-compensated interpolation) are
      decades old. Still, avoid copying any specific commercial implementation. _(Not legal advice. If the Store
      listing becomes commercial, a short consult is cheap insurance.)_
- [x] **Privacy policy** (`docs/privacy-policy.md`): camera frames never leave the PC, nothing is collected, logs stay local.
      Required by the Store for camera apps.
- [ ] Icons, screenshots and promo images are your own work, or used under a license that permits commercial use.
- [ ] Code-signing certificate issued in your legal name.

---

## 9. Microsoft Store and signing checklist

**Account:** Partner Center individual registration has been free since September 2025 (company accounts are still a one-time
fee). Sign up at storedeveloper.microsoft.com.

**Code signing** (required for EXE/MSI submissions, must chain to the Microsoft Trusted Root Program):
- Option A: **Azure Artifact Signing** (formerly Trusted Signing). Cheap monthly plan with no hardware token, but individual
  eligibility is limited to certain countries, so check yours first.
- Option B: **OV code-signing certificate** from a CA (Certum, Sectigo, SSL.com, …). Since 2023 the key has to sit on
  a hardware token or cloud HSM. Certum has a discounted certificate for open-source developers.
- For a one-time release, a 1-year certificate is enough. With an RFC 3161 timestamp the signature stays valid
  after the certificate expires.

**Submission:**
- [ ] New product → **EXE or MSI app** → reserve name.
- [ ] Package URL: the versioned GitHub Release asset URL (it must never change or be replaced).
- [ ] Architecture x64, silent install args `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`.
- [x] Offline installer only (it is, with no downloads during setup) and no bundled third-party offers.
- [ ] Listing: description, ≥ 1 screenshot (1366×768 or larger), app icon, category *Photo & video*, support contact, privacy URL.
- [ ] Age rating questionnaire (IARC).
- [ ] Note in the "Notes for certification" that the app installs a system virtual camera and needs admin.

---

## 10. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Some app (e.g. Teams) previews but doesn't transmit the MF virtual camera | Medium | High | Spike S1 with real calls. Investigate sample attributes/formats early. |
| NVOF can't initialise inside Frame Server (session 0) | Medium | Low | The Compute path works on every GPU, including NVIDIA. |
| DirectShow-only apps can't see the camera | Medium | Medium | DirectShow source shim (MIT base classes), +1 week. |
| Warp artifacts bother users | Medium | Medium | Confidence fallback, scene-cut guard, Balanced/Off modes, honest README. |
| Webcam frame rate collapses in low light | High | Medium | Timestamp pacing, steady-fps toggle, README tip about lighting. |
| Signing certificate delayed (identity checks take days to weeks) | Medium | Medium | Order it during Phase 5. |
| A future Windows update changes Frame Server behaviour | Low | High | Only documented APIs. A hotfix release may still be unavoidable, so keep the build env archived. |
| NVIDIA SDK terms turn out restrictive | Low | Low | NVOF is optional at build time. Ship without it. |

---

## 11. Decision log

| # | Decision | Why |
|---|---|---|
| D1 | Windows 11 MF virtual camera, not a DirectShow filter or kernel driver | Documented, shared by Frame Server, no driver signing |
| D2 | Pipeline inside the media source, not in the app | Works with the app closed, no IPC, camera powers on only when used |
| D3 | Admin EXE installer (Inno Setup), not MSIX | Frame Server must read the DLL from Program Files, the system-lifetime camera needs admin, and the user wants .exe |
| D4 | NVOF vectors + own compositor, not NvOFFRUC | No proprietary DLL to redistribute, one compositor for all GPUs |
| D5 | D3D11, not D3D12 | Native MF interop, NVOF D3D11 API, less code |
| D6 | Win32 UI, not WinUI 3/Electron/ImGui | Size, zero runtime deps, accessibility for free |
| D7 | No AI model for *interpolation* | 20+ MB, heavier GPU load. Classical flow is good enough for talking heads. |
| D8 | MIT license for the project | Simple and compatible with every dependency we use |
| D9 | No telemetry, no network | Privacy, simpler Store review, nothing to maintain after release |
| D10 | NVOF through the CUDA interface, headers vendored | Public BSD headers, no SDK download, still no NVIDIA binaries shipped |
| D12 | FSR 1 for upscaling, luma only (chroma bilinear) | Detail lives in luma; halves the cost |
| D13 | Preview via Direct2D DC render target | HWND render target output didn't show in the dialog child on the hybrid-GPU test laptop |
| D14 | Inbox FaceDetector + Windows ML for effects, 460 KB Apache-2.0 model | Nothing extra to redistribute, works on every PC (GPU or CPU), effects are optional and degrade to off |
| D15 | Effects stay in the virtual camera pipeline, grids only in the preview | Apps get the effects; grids are a composition aid, never wanted in a call |
| D16 | NVIDIA Image Scaling as a second, optional upscaler (luma only, <= 2x, else FSR) | Requested after looking at Lossless Scaling; NIS is the MIT-licensed scaler such tools offer. LSFG/LS1 are closed source and can't be used |
| D17 | Parallel CPU copy for big frames; overload step-down ignores copy time | 4K RGB32 copies took ~6-8 ms and wrongly triggered Balanced mode |
| D18 | Upscaler setting: Auto (CAS <= 2x, else FSR), Off, FSR 1, NIS, CAS, Lanczos | No scaler won on both photos and webcam frames; CAS is second on both and the lightest. Off removes upscaled sizes entirely |
| D19 | On/off switch = `ProcessingSettings()` passthrough + camera's own format advertised | One place decides "plain camera"; apps that open the camera while it's off get the camera's native size and rate |
| D20 | Compare view paints the raw camera frame in a final pass | The left half must be untouched by denoise, effects and zoom to be an honest comparison |
| D21 | Joint bilateral mask upsampling + background-only (premultiplied) blur | Removes blobby edges and halos without a bigger model |
| D22 | Spring (critically damped) auto-framing | First-order easing jumps in velocity; a spring starts and stops gently |
| D23 | Only SmoothCam itself is excluded as an input; other virtual cameras are listed but flagged | Phone Link cameras were hidden; Windows still blocks vcam-in-vcam inside Frame Server |
| D11 | Inno Setup `.exe` named `SmoothCam_beta` / `SmoothCam_beta2` / `SmoothCam_beta3` / `SmoothCam_v0.0.1` | Requested; `--register` does COM + camera + ProgramData ACL in one elevated step |
| S2, S3 | Done | Wrapping the camera and running D3D11 + NVOF inside Frame Server both work |
| S1, S4 | Pending | Camera works through Frame Server for MF clients; Teams/OBS/DirectShow still to try by hand |

---

## 12. References

- MFCreateVirtualCamera — https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera
- Windows-Camera samples (VirtualCamera) — https://github.com/microsoft/Windows-Camera
- VCamSample — https://github.com/smourier/VCamSample
- NVIDIA Optical Flow SDK — https://developer.nvidia.com/optical-flow-sdk · docs: https://docs.nvidia.com/video-technologies/optical-flow-sdk/
- NVIDIA blog, FRUC on Ada — https://developer.nvidia.com/blog/harnessing-the-nvidia-ada-architecture-for-frame-rate-up-conversion-in-the-nvidia-optical-flow-sdk
- FFmpeg `fruc_vulkan` write-up — https://xenospectrum.com/en/ffmpeg-vulkan-fruc-frame-interpolation/
- AMD FidelityFX SDK — https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- Practical-RIFE — https://github.com/hzwer/Practical-RIFE
- WIL — https://github.com/microsoft/wil
- Inno Setup — https://jrsoftware.org/isinfo.php
- Store: submitting MSI/EXE apps — https://learn.microsoft.com/windows/apps/publish/faq/submit-your-app
- Code signing options — https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- Free individual Store registration — https://blogs.windows.com/windowsdeveloper/2025/09/10/free-developer-registration-for-individual-developers-on-microsoft-store/
- ITU-R BT.1359 (audio/video sync thresholds) — https://www.itu.int/rec/R-REC-BT.1359

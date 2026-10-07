<p align="center">
  <img src="assets/icons/SmoothCam-256.png" width="112" alt="SmoothCam logo">
</p>

# SmoothCam

Make a 30 fps webcam look like 60, and a 1080p one look like 4K.

**Website: [smoothcam.vercel.app](https://smoothcam.vercel.app)** · [Download](../../releases/latest) ·
[Report a problem](../../issues)

SmoothCam is a free, open-source Windows 11 app that sits between your webcam and whatever you use
for calls or streaming. It generates the frames your camera never captured (the ones *in
between*), can upscale the picture past the camera's own resolution, cleans up noise, brightens
dark rooms, blurs your background and keeps your face framed. Teams, Zoom, Meet, Discord or OBS
just see a camera called **SmoothCam**. Everything runs locally on your PC.

> **Version 0.0.1, the first public release.** It works end to end on the machines it's been
> tested on (see [Tested on](#tested-on)), but it hasn't met many different cameras and GPUs yet.
> Bug reports are very welcome. The design notes are in [ImplimentationPlan.md](ImplimentationPlan.md).

<!-- TODO: add docs/images/main-window.png once there's a screenshot without a person in it -->

---

## Why

Most laptop and USB webcams top out at 30 fps, some at 24, and nearly all of them quietly drop
lower once the room gets dim. Next to a phone or a proper camera that looks choppy, grainy and
soft, especially when you move or talk with your hands.

Games solved the frame-rate part years ago with frame generation, and tools like Lossless Scaling
brought it to everything else on screen. Webcams never got the same treatment, so SmoothCam does it
for your camera, and adds the other things a cheap webcam needs.

## Features

| | What it does |
|---|---|
| **One-click on/off** | A switch at the top of the window. Off = every app gets your plain camera, untouched |
| **Smooth frame rate** | Up to 60 fps from a 24/30 fps camera, by estimating motion and drawing the in-between frames |
| **Resolution up to 4K** | Any size your camera supports, or upscaled 2K / 4K with your choice of five upscalers |
| **Noise reduction** | Removes grain and colour noise, using the same motion data so moving things don't smear |
| **Low-light boost** | Brightens dim rooms in software, without making the camera slow down |
| **Background blur** | On-device person segmentation; the edge follows your hair and shoulders, with no halo |
| **Auto-framing** | Follows your face and zooms like a camera operator: smooth starts and stops, no jitter |
| **Compare view** | Your camera exactly as it is on the left, SmoothCam on the right, each labelled |
| **Framing grids** | Rule of thirds, golden ratio, crosshairs or square in the preview, to line yourself up |

## How it works

1. SmoothCam adds a camera called **SmoothCam** to Windows using the built-in Windows 11 virtual
   camera support. No drivers are involved.
2. When an app opens it, SmoothCam opens your real webcam behind the scenes. The camera light only
   comes on while something is actually using SmoothCam.
3. For every pair of real frames it estimates motion on the GPU:
   - on **NVIDIA** cards (GTX 16 series and newer) with the card's optical-flow hardware,
   - on **AMD, Intel and everything else** with its own compute shader (coarse-to-fine block matching).
4. Each output frame then goes through a short GPU pipeline:
   interpolate → low-light boost → upscale → background blur → sharpen.
   Stages you haven't turned on are skipped entirely.

Where motion doesn't make sense (someone walks in front of the camera, the lights switch on), it
falls back to a soft blend for that region rather than showing a smeared one.

Face detection uses Windows' own face detector, and the background model runs through Windows ML,
which is also part of Windows. There's nothing extra to install, and it all stays on your PC.

### Upscaling

If you pick a bigger picture than the camera delivers, SmoothCam builds each frame at the camera's
real resolution and then upscales it. All five upscalers are open source, run on any GPU, and look at
one frame at a time, so none of them adds a frame of delay:

| Upscaler | What it is | Notes |
|---|---|---|
| **Auto** (default) | AMD CAS up to 2x, FSR 1 beyond | The best all-rounder in our tests |
| **AMD FSR 1** | [FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR): edge-adaptive upscale + noise-aware sharpening | Any ratio. Best on detailed, well-lit pictures |
| **NVIDIA NIS** | [NVIDIA Image Scaling](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling): directional scaler with built-in sharpening, the open scaler Lossless Scaling also offers | Up to 2x, then FSR. Runs on any GPU |
| **AMD CAS** | [FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS): contrast-adaptive upscale + sharpen in one pass | Up to 2x, then FSR. The lightest |
| **Lanczos** | The classic Lanczos-3 resampler, with anti-ringing | Any ratio. Best on soft webcam footage |
| **Off** | No upscaler | Only your camera's real resolutions are offered |

How close each gets to the original after halving a picture and scaling it back up (higher is
better; measured with `smoothcam_bench --upscale-image`):

| | Plain stretch | FSR 1 | NIS | CAS | Lanczos |
|---|---|---|---|---|---|
| Detailed photo | 42.2 dB | **46.2 dB** | 44.8 dB | 45.8 dB | 44.6 dB |
| Webcam frame | 50.8 dB | 51.5 dB | 52.0 dB | 52.0 dB | **52.5 dB** |

Apps that take NV12 (Zoom, Teams, browsers, the Windows Camera app) get a steady 60 fps at 4K on the
test laptop with every upscaler. Auto-framing uses the same upscaler, so zooming in stays sharp.
Upscaling can't recover detail the camera never captured; it gives cleaner edges than a plain
stretch, not a better lens.

## Requirements

- Windows 11 (the virtual camera API doesn't exist on Windows 10)
- Any DirectX 11 GPU, which every Windows 11 PC already has
- A webcam
- Admin rights to install, because it registers a camera for all users

An NVIDIA GPU is not required. It's just the fastest path.

## Install

Download `SmoothCam_v0.0.1.exe` from the [Releases](../../releases) page or
[smoothcam.vercel.app](https://smoothcam.vercel.app) and run it. Windows will ask for admin
permission; SmoothCam needs it to register the camera system-wide. The installer offers to put a
shortcut on your desktop.

The installer isn't code-signed yet, so Windows will show a SmartScreen warning ("Windows protected
your PC"). Click **More info → Run anyway**, or build it yourself (below). Each release lists the
installer's SHA-256 so you can check the download.

Silent install and uninstall, for IT folks:

```
SmoothCam_v0.0.1.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
"C:\Program Files\SmoothCam\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

## Using it

1. Open **SmoothCam** from the Start menu or the desktop. The live view shows exactly what other
   apps will receive.
2. Pick your settings in the panel (see below).
3. In Teams / Zoom / Meet / OBS, choose **SmoothCam** as your camera.

You can close the SmoothCam window afterwards; the camera keeps working without it.

### The window

- **On/off switch** (top right of the panel): off sends every app your plain camera, at its own
  resolution and frame rate, with no processing. If another app is using SmoothCam right then,
  you're asked first.
- **Hide preview** shrinks the window to just the settings. This happens **automatically while
  another app is using SmoothCam**, so the preview never competes with your call for the camera. It
  comes back by itself when the call ends.
- **Full screen:** press **F11**, double-click the video, or use the button under it. **Esc** leaves.
- **Hide settings** gives the whole window to the video. **Right-click the video** to put the
  settings panel on the left or the right.
- **Website** opens [smoothcam.vercel.app](https://smoothcam.vercel.app) in your browser.
- The window remembers the panel side, whether it was hidden, and your framing grid.

### Settings

| Setting | Notes |
|---|---|
| Camera | Your webcam. Virtual cameras (Phone Link, OBS) are listed but usually can't feed SmoothCam, see the FAQ |
| Frame rate | Your camera's own rate is marked *(default)*. Choose up to 60 fps; apps get exactly this rate |
| Mode | **Quality** (best), **Balanced** (lighter), **Low latency** (predicts instead of waiting), **Off** (no interpolation) |
| Resolution | e.g. `2.1MP 16:9 (1920x1080) (default)`. Sizes above your camera's are marked *Upscaled*. Apps get the size you pick as their default |
| Upscaler | **Auto**, **Off**, **AMD FSR 1**, **NVIDIA NIS**, **AMD CAS** or **Lanczos**, see [Upscaling](#upscaling) |
| Sharpening | **Auto** sharpens only when upscaling or zooming; Low / Medium / High always sharpen |
| Noise reduction | Low / Medium / High. Most useful in dim rooms |
| Low-light boost | **Auto** brightens only when the picture is dark. **Strong** for really dark rooms |
| Background | **Blur** or **Strong blur** |
| Auto-framing | **Follow face** (gentle) or **Close-up** (tighter) |
| Motion estimation | Automatic uses NVIDIA hardware when available, otherwise the GPU compute path |
| Grid | Shown in the preview only, never sent to other apps |
| Camera low-light mode | Lets the *camera itself* use longer exposures: brighter, but many cameras then drop to 7–15 fps. Most people get a better result leaving this off and using **Low-light boost** |
| Compare view | Left half: your camera exactly as it is (its own resolution and frame rate, no effects). Right half: SmoothCam. The preview labels both, e.g. *Camera 1920x1080 · 30 fps* and *SmoothCam 3840x2160 · 60 fps* |

Frame rate and resolution changes reach other apps the next time they open the camera (in most
apps: turn the camera off and on in the call). Everything else applies instantly.

### Modes and delay

| Mode | What it does | Added delay* |
|---|---|---|
| Quality | Waits for the next real frame and interpolates between the two | ~1 camera frame + a few ms |
| Balanced | Same, with lighter motion estimation | ~1 camera frame |
| Low latency | Predicts forward from the last frames instead of waiting | ~23 ms |
| Off | Passes the camera through, paced to the output rate | ~5 ms |

\* Measured on the test machine with a camera running at about 28 fps.

## Tested on

| | |
|---|---|
| OS | Windows 11 26H2 (build 26300) |
| GPUs | NVIDIA GeForce RTX 4060 Laptop + Intel UHD (hybrid laptop) |
| Camera | built-in 1080p30 webcam (NV12) |

Measured through the installed camera, opened the way apps open it (`SmoothCam.exe --probe --nv12`):

| Check | Result |
|---|---|
| 1080p, Quality | 60 fps from a ~28 fps camera, frame interval 16.7 ms |
| 4K with each upscaler (Auto, FSR 1, NIS, CAS, Lanczos) | 59.6–60.0 fps |
| 2K and 4K with noise reduction, low-light boost, blur, close-up and compare all on | 60.0 fps |
| Low latency mode | 60 fps, +23 ms |
| SmoothCam switched off | the plain camera: 1920x1080 at 30 fps |
| Upscaler off with 4K selected | falls back to the camera's own 1920x1080 |

The effects also have GPU unit tests on controlled images (37 tests). Teams, Zoom and OBS still
need to be tried by hand; if you do, please report how it went.

## FAQ

**Does it add lag?**
A little in Quality mode, and it's unavoidable: to draw a frame between two real ones, you need the
second one. At 30 fps that's about 33 ms plus a few ms of processing, which is very hard to notice
in a call. Low latency mode cuts it to roughly 20 ms.

**Will it make a bad webcam look sharp?**
It makes motion smoother and edges cleaner, and noise reduction plus low-light boost help a lot in
dim rooms. It can't invent detail the camera never captured. Good lighting still beats everything.

**Can I use my phone as the camera (Phone Link)?**
Not yet. Windows connects a phone as a *virtual* camera, and SmoothCam itself runs inside Windows'
camera service, where Windows doesn't let one virtual camera read another. SmoothCam lists such
cameras marked "(virtual - may not work)" and tells you clearly if one can't be opened. A USB
webcam, or a phone app that shows up as a USB/UVC webcam, works.

**My camera drops to 7–10 fps when the room is dark.**
That's the camera's own low-light behaviour. Turn **Camera low-light mode** off and use
**Low-light boost** instead to keep it at full speed (`SmoothCam.exe --diag` shows whether your
camera supports that). If it still drops, SmoothCam keeps smoothing down to about 5 fps, with more
delay the bigger the gap.

**4K in an app that asks for RGB32 isn't a full 60 fps.**
Most apps take NV12, where 4K runs at 60. A 4K RGB32 stream is 2.7x bigger and Windows hands it to
the app one frame at a time; on the test laptop that tops out around 35–55 fps. SmoothCam's own
preview stays at 2K for that reason and says so.

**Two apps can't use SmoothCam at the same time with different settings.**
That's how Windows shares every camera: one app controls the format. SmoothCam's own window steps
aside automatically when another app is using it.

**Does anything get uploaded?**
No. SmoothCam has no network code at all. Frames never leave your PC, and there's no telemetry and
no account. Face detection only finds *where* a face is; it doesn't recognise anyone. The Website
button just opens your browser. See the [privacy policy](docs/privacy-policy.md).

**An app doesn't show the SmoothCam camera.**
Restart that app; most only look for cameras at startup. If it's still missing, open SmoothCam and
click **Repair camera…**, or run `SmoothCam.exe --diag` and attach the output to an issue.

**How do I remove it?**
Settings → Apps → Installed apps → SmoothCam → Uninstall. The virtual camera goes with it.

## Command line

```
SmoothCam.exe --diag                 GPUs, cameras, registration and live status
SmoothCam.exe --probe [secs] [--width W] [--nv12] [--snap DIR]
                                     open SmoothCam like an app would and measure it
                                     (--width 0 takes the default format, like most apps;
                                      --nv12 reads NV12 like Zoom/Teams instead of RGB32)
SmoothCam.exe --register             (re)register the camera, asks for admin
SmoothCam.exe --unregister           remove it, asks for admin
```

Logs live in `C:\ProgramData\SmoothCam\logs`.

## Building from source

You'll need:

- Visual Studio 2022 or 2026 (Build Tools are enough) with **Desktop development with C++** and a
  Windows 11 SDK
- CMake 3.28+ and Ninja (both come with the VS C++ workload)
- [Inno Setup 6](https://jrsoftware.org/isinfo.php), only for the installer

WIL and GoogleTest are fetched by CMake. The NVIDIA and AMD headers and the segmentation model are
in the repo, so no CUDA Toolkit or SDK downloads are needed.

```powershell
.\scripts\build.ps1 -Test              # release build + unit tests
.\scripts\build.ps1 -Installer         # ... and dist\SmoothCam_v0.0.1.exe
```

Or by hand from an *x64 Native Tools* prompt:

```
cmake --preset x64-release
cmake --build --preset x64-release
ctest --preset x64-release
```

`build\x64-release\bin\smoothcam_bench.exe` runs the whole engine against your webcam in-process
(no install, no admin) and can save output frames. `--vision-image PIC` runs face detection and
segmentation on a still picture, `--upscale-image PIC` compares the upscalers, and `--camera NAME`
picks a camera. All handy when working on the shaders.

## Project layout

```
src/core        settings, logging, timing (no Windows media code)
src/engine      camera capture, motion estimation, effects, compositing, pacing
  shaders/      the HLSL compute shaders
  vision/       face detection, person segmentation, auto-framing
src/vcam        SmoothCamSource.dll, the media source Windows loads for the camera
src/app         SmoothCam.exe: window, preview, command-line tools
installer/      Inno Setup script and artwork
assets/         icon, segmentation model and its license
scripts/        build, packaging and model-preparation scripts
tests/          unit tests (incl. GPU tests on synthetic images) and the bench tool
third_party/    NVIDIA Optical Flow headers (BSD-3-Clause); AMD FSR 1, AMD CAS, NVIDIA NIS headers (MIT)
```

## Credits

SmoothCam stands on other people's work and documentation:

- Microsoft's [Windows-Camera samples](https://github.com/microsoft/Windows-Camera) and the Media
  Foundation virtual camera docs
- Simon Mourier's [VCamSample](https://github.com/smourier/VCamSample), the clearest small example of
  a Windows 11 virtual camera out there
- NVIDIA's [Optical Flow SDK](https://github.com/NVIDIA/NVIDIAOpticalFlowSDK) and
  [Image Scaling SDK](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
- AMD's [FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) and
  [FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS)
- Google's [MediaPipe Selfie Segmentation](https://ai.google.dev/edge/mediapipe/solutions/vision/image_segmenter)
  model, via the [onnx-community](https://huggingface.co/onnx-community/mediapipe_selfie_segmentation_landscape) export
- [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/), for showing how much
  people want frame generation and upscaling outside games (no code from it; it's closed source)
- The FFmpeg developers, whose `fruc_vulkan` work showed that hardware motion vectors plus a simple
  compositor go a long way
- Gerard de Haan's 3-D recursive search papers, for the block-matching idea
- Microsoft's [WIL](https://github.com/microsoft/wil), and [Inno Setup](https://jrsoftware.org) by
  Jordan Russell and Martijn Laan

Full license texts are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Everything SmoothCam
includes is under the MIT, BSD-3-Clause or Apache-2.0 license, so anyone can use and share it freely.

## License

MIT, see [LICENSE](LICENSE).

NVIDIA, GeForce and RTX are trademarks of NVIDIA Corporation. AMD and FidelityFX are trademarks of
Advanced Micro Devices, Inc. Google and MediaPipe are trademarks of Google LLC. Windows and
Microsoft Teams are trademarks of Microsoft. Zoom is a trademark of Zoom Communications. Lossless
Scaling is a product of its respective owner. SmoothCam is an independent project and isn't
affiliated with or endorsed by any of them.

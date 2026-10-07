# Changelog

All notable changes to this project are documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[SemVer](https://semver.org/).

## [0.0.1] - 2026-10-08

First public release. Everything below is new to the public; the internal betas it grew out of are
listed further down for reference.

### Added
- **On/off switch** at the top of the window. Off sends every app the plain camera at its own
  resolution and frame rate; if another app is using SmoothCam at that moment you're asked first.
- **Compare view** shows the camera exactly as it is (its own resolution and frame rate, before
  noise reduction, effects and zoom) on the left and SmoothCam on the right, with labels such as
  *Camera 1920x1080 · 30 fps* / *SmoothCam 3840x2160 · 60 fps* in the preview.
- **Upscaler** setting with five open-source options plus Off: Auto (AMD CAS up to 2x, FSR 1
  beyond), AMD FSR 1, NVIDIA Image Scaling, AMD CAS, and Lanczos-3 with anti-ringing. Off offers only
  the camera's real resolutions.
- **Website** button and About-box link to [smoothcam.vercel.app](https://smoothcam.vercel.app).
- Other virtual cameras are listed as inputs, marked "(virtual - may not work)", with a clear
  message when Windows won't let SmoothCam open one.
- `--probe --nv12` measures the format Zoom/Teams/browsers take; bench `--upscale-image`,
  `--camera NAME`, `--upscaler`.

### Improved
- **Background blur**: the mask edge now snaps to the real outline (joint bilateral upsampling of the
  segmentation mask), the blur no longer leaks the person into the background (no halo), the mask
  follows movement immediately instead of trailing, and the model sees a brightened copy of the
  picture in dark rooms.
- **Auto-framing** moves on a critically damped spring with a smoothed face box: no sudden starts,
  no overshoot, no jitter. Close-up frames tighter (face ~65% of the height) so it also zooms at
  laptop distance.
- **4K**: frame copies are split across CPU cores and no longer count as "GPU busy" (which used to
  force Balanced mode). 4K NV12 runs at a steady 60 fps on the test laptop with every upscaler.
- Settings panel: switch beside the title, "Hide preview" on its own row, settings that don't apply
  while SmoothCam is off are greyed out.

### Fixed
- Virtual cameras (Phone Link, OBS, ...) were hidden from the camera list because every virtual
  camera was mistaken for SmoothCam itself.
- "Automatic (first camera found)" prefers real cameras over idle virtual ones.

### Known limitations
- A phone connected with Phone Link (or any other virtual camera) usually can't be SmoothCam's
  input: Windows doesn't let one virtual camera read another inside its camera service.
- 4K in RGB32 (rarely requested by apps) reaches about 35–55 fps rather than 60.
- The installer isn't code-signed yet, so SmartScreen warns on first run.

### Build environment
- Visual Studio Build Tools 2026 (MSVC 14.51), Windows SDK 10.0.26100, CMake 4.4, Ninja 1.13
- WIL v1.0.250325.1, GoogleTest 1.17.0
- NVIDIA Optical Flow headers from NVIDIA/NVIDIAOpticalFlowSDK @ edb50da (API 2.0)
- AMD FidelityFX FSR 1 headers from GPUOpen-Effects/FidelityFX-FSR @ a21ffb8
- AMD FidelityFX CAS header from GPUOpen-Effects/FidelityFX-CAS @ 9fabcc9
- NVIDIA Image Scaling SDK v1.0.3 from NVIDIAGameWorks/NVIDIAImageScaling @ 35e13ba, with a marked
  luma-only change in NIS_Scaler.h
- MediaPipe selfie segmentation (landscape) from onnx-community, converted by
  scripts/prepare-model.py, sha256 fdbcf42144433b5ecd2144ff67693224fd28fe4b14fe65856888bdf737f06188
- Inno Setup 6

## Internal betas (not published)

### Beta 3 - 2026-10-08
- Settings reach apps: only the chosen frame rate is offered (an extra 30 fps type let some apps pick
  30) and the chosen resolution is the default format. The preview restarts on format changes.
- Resolution list built from what the camera supports (`2.1MP 16:9 (1920x1080) (default)`), plus
  upscaled 2K/4K; frame-rate list starts at the camera's own rate, marked *(default)*.
- Effects: motion-compensated noise reduction, low-light boost, background blur (MediaPipe on
  Windows ML), auto-framing (Windows face detection); framing grids in the preview.
- The window collapses to the settings while another app uses SmoothCam.
- Installer offers a desktop shortcut.
- Smoothing continues down to ~5 fps (camera low-light mode); "Keep frame rate steady" became
  "Camera low-light mode".
- NVIDIA Image Scaling as a second upscaler.

### Beta 2 - 2026-10-07
- Upscaling with AMD FSR 1 (EASU + RCAS denoise), ~3 ms at 1440p and ~5 ms at 4K.
- Redesigned window: large live view, settings beside it, full screen, hide settings, panel on
  either side; Direct2D preview.
- 4K RGB32 media types no longer overflow the bitrate attribute.

### Beta 1 - 2026-10-07
- Windows 11 virtual camera wrapping the physical webcam inside Frame Server.
- Frame interpolation to 30/48/50/60 fps: NVIDIA Optical Flow, GPU compute block matching, or a
  basic blend on software adapters; confidence-aware compositor.
- Quality, Balanced, Low latency and Off modes; compare view; timestamp-based pacing; automatic
  quality step-down.
- Settings window with live preview, dark mode, per-monitor DPI; `--diag`, `--probe`,
  `--register`, `--unregister`; Inno Setup installer with silent install/uninstall.

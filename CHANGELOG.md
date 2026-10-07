# Changelog

All notable changes to this project are documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[SemVer](https://semver.org/).

## [1.2.0-beta3] - 2026-10-08

### Fixed
- **Settings now reach apps.** SmoothCam used to offer a 30 fps fallback next to the chosen rate,
  and some apps picked it; now only the chosen frame rate is offered, and the chosen resolution is
  the default format.
- The preview opens at the selected resolution and frame rate and restarts when they change, so
  changes are visible immediately.
- Sharpening levels are stronger and clearly distinguishable.
- Smoothing keeps working when the camera's own low-light mode drops it to 7-15 fps (it used to
  pause below 11 fps); it now only pauses below ~5 fps.

### Added
- Resolution list built from what the camera supports, labelled like
  `2.1MP 16:9 (1920x1080) (default)`, plus upscaled 2K/4K; 4:3 sizes supported.
- Frame-rate list starts at the camera's own rate, marked *(default)*.
- Effects: noise reduction (motion-compensated temporal + spatial), low-light boost, background
  blur (MediaPipe selfie segmentation via Windows ML), auto-framing (Windows face detection + FSR
  zoom).
- Framing grid in the preview: rule of thirds, golden ratio, crosshairs, square.
- The window collapses to the settings panel while another app uses SmoothCam, with a manual
  Hide/Show preview button.
- Installer offers a desktop shortcut.
- `--probe --width 0` measures the format apps get by default; bench `--vision-image`.

### Changed
- "Keep frame rate steady" became "Camera low-light mode" (same setting, inverted, clearer).

## [1.1.0-beta2] - 2026-10-07

### Added
- **Upscaling** with AMD FidelityFX Super Resolution 1: frames are built at the camera's real
  resolution, then upscaled (EASU) and optionally sharpened (RCAS, denoise variant). Spatial only,
  so no added frames of delay; ~3 ms per frame at 1440p and ~5 ms at 4K on an RTX 4060 Laptop GPU.
- "Output resolution" setting (camera native / up to 1080p / 1440p / 4K) and "Sharpening"
  (auto / off / low / medium / high). 1440p and 4K modes are now offered to apps on request.
- Redesigned window: large live view with the settings panel beside it, resizable, full screen
  (F11, double-click, or button; Esc leaves), hide-settings mode, and the panel can sit on either
  side (right-click the video). The preview starts automatically and is drawn with Direct2D.
- Live status shows when and how much the picture is being upscaled.
- `--probe --width W` to test a specific output size.

### Fixed
- 4K RGB32 media types no longer overflow the bitrate attribute.

## [1.0.0-beta] - 2026-10-07

First public beta.

### Added
- Windows 11 virtual camera ("SmoothCam") registered system-wide; wraps the chosen physical
  webcam inside Frame Server, so it works with the settings window closed.
- Frame interpolation to 30/48/50/60 fps:
  - NVIDIA Optical Flow hardware (Turing and newer) through the driver's CUDA interface,
  - GPU compute block matching for every other D3D11 GPU,
  - basic blend for software adapters.
- Confidence-aware compositor (forward/backward consistency + photometric check).
- Quality, Balanced, Low latency and Off modes; compare (split screen) view.
- Timestamp-based pacing that follows the camera's real frame rate, including low-light drops.
- Automatic quality step-down when the GPU can't keep up.
- Interpolation pauses (with a status message) when the camera falls below ~11 fps in dim light,
  instead of adding 100+ ms of delay.
- Settings window with live preview, status, repair, dark mode, per-monitor DPI.
- `--diag`, `--probe`, `--register`, `--unregister` command-line tools.
- Inno Setup installer (`SmoothCam_beta.exe`) with silent install/uninstall.
- Unit tests for pacing, settings, camera mode selection and GPU motion estimation.

### Build environment
- Visual Studio Build Tools 2026 (MSVC 14.51), Windows SDK 10.0.26100, CMake 4.4, Ninja 1.13
- WIL v1.0.250325.1, GoogleTest 1.17.0
- NVIDIA Optical Flow headers from NVIDIA/NVIDIAOpticalFlowSDK @ edb50da (API 2.0)
- AMD FidelityFX FSR 1 headers from GPUOpen-Effects/FidelityFX-FSR @ a21ffb8 (beta2)
- MediaPipe selfie segmentation (landscape) from onnx-community, converted by
  scripts/prepare-model.py, sha256 fdbcf42144433b5ecd2144ff67693224fd28fe4b14fe65856888bdf737f06188 (beta3)
- Inno Setup 6

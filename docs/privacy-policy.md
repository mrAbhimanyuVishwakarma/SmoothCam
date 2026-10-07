# SmoothCam privacy policy

_Effective: 8 October 2026 (version 0.0.1) · https://smoothcam.vercel.app_

SmoothCam processes video from your webcam to produce a smoother video feed. Here is everything it
does with your data:

- **Camera frames** are processed in memory on your own computer and handed to the app you choose
  (Teams, Zoom, etc.). SmoothCam does not save, record, or transmit them.
- **Auto-framing and background blur** look for a face and a person outline in the video, using
  Windows' built-in face detector and a small model that runs on your PC. Nothing is recognized or
  identified (it only finds *where* a face is), nothing is stored, and nothing leaves your computer.
- **No network access.** SmoothCam contains no code that connects to the internet. There is no
  telemetry, analytics, crash upload, advertising, or account. The **Website** button only opens
  https://smoothcam.vercel.app in your own browser.
- **Settings** are stored in `C:\ProgramData\SmoothCam\settings.ini` on your computer.
- **Logs** are stored in `C:\ProgramData\SmoothCam\logs\`. They contain timings, GPU and camera
  device names, and error messages — never images. They stay on your computer unless you choose to
  attach them to a bug report yourself.

Uninstalling SmoothCam removes the program, and optionally its settings and logs.

What the apps you use SmoothCam with (Teams, Zoom, and so on) do with the video is covered by their
own privacy policies.

Questions: open an issue on the project's GitHub page.

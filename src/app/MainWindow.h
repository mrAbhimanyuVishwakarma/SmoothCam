#pragma once

#include "CameraReader.h"
#include "PreviewRenderer.h"
#include "Settings.h"
#include "capture/Camera.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace smoothcam
{
// The main window: a large live view of the SmoothCam camera with the settings
// panel beside it. Resizable, can go full screen, the panel can be hidden or
// moved to either side, and the preview can be collapsed away (automatically
// when another app is using SmoothCam, so the two don't fight over the camera).
//
// It's a modeless dialog so native controls give keyboard navigation and
// screen-reader support for free; MainWindow::Layout() positions everything.
class MainWindow
{
public:
    static int Run(HINSTANCE instance);

private:
    struct ResolutionChoice
    {
        uint32_t width = 0;
        uint32_t height = 0;
        bool isDefault = false;
        std::wstring label;
    };

    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK AboutProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PreviewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR Handle(UINT message, WPARAM wParam, LPARAM lParam);

    void OnInit();
    void OnDestroy();
    void OnCommand(int id, int code);
    void FillControls();
    void FillCameraDependentControls();
    void QueryCameraDefaults(Settings& s) const;
    void ReadControls(int changedId);
    void UpdateHints();
    void RefreshStatus();
    void RefreshRegistration();
    void Repair();
    void ShowAbout();

    // Layout and window modes
    void Layout();
    RECT PanelRect() const;
    void ToggleFullscreen();
    void SetPanelVisible(bool visible);
    void SetPanelOnLeft(bool left);
    void SetCollapsed(bool collapsed, bool automatic);
    void ShowContextMenu(POINT screen);
    void LoadUiState();
    void SaveUiState() const;
    int Scale(int dip) const { return MulDiv(dip, static_cast<int>(m_dpi), 96); }

    // Preview
    void StartPreview();
    void StopPreview();
    void PreviewLoop(uint32_t preferredWidth);
    void PaintPreview(HDC dc);

    void DrawEnableSwitch(const DRAWITEMSTRUCT& item) const;
    void OnEnableClicked();
    void UpdateEnabledControls();

    void CreateFonts();
    void ApplyTheme();
    HBRUSH OnColor(HDC dc, HWND control);
    bool IsInPanel(HWND control) const;

    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_preview = nullptr;
    UINT m_dpi = 96;
    Settings m_settings;
    std::vector<CameraInfo> m_cameras;
    std::vector<ResolutionChoice> m_resolutions;
    std::vector<uint32_t> m_fpsChoices;
    bool m_filling = false;
    bool m_saveWarned = false;

    // Window modes
    bool m_panelVisible = true;
    bool m_panelOnLeft = false;
    bool m_fullscreen = false;
    bool m_collapsed = false;
    bool m_autoCollapsed = false; // collapsed because another app took the camera
    bool m_userExpanded = false;  // user asked for the preview despite that
    int m_idleTicks = 0;
    RECT m_expandedRect{};
    WINDOWPLACEMENT m_savedPlacement{sizeof(WINDOWPLACEMENT)};
    LONG_PTR m_savedStyle = 0;
    FramingGrid m_grid = FramingGrid::Off;

    // Preview
    PreviewRenderer m_renderer;
    std::thread m_previewThread;
    std::atomic<bool> m_previewRunning{false};
    std::atomic<bool> m_previewLost{false}; // another app took the camera over
    std::atomic<bool> m_framePosted{false};
    std::mutex m_frameLock;
    CameraReader::Frame m_frame;
    bool m_hasFrame = false;
    bool m_frameChanged = false;
    ULONGLONG m_previewStoppedAt = 0;
    std::wstring m_previewMessage;

    // Look
    bool m_dark = false;
    HBRUSH m_baseBrush = nullptr;  // camera area
    HBRUSH m_panelBrush = nullptr; // settings panel
    COLORREF m_baseColor = 0;
    COLORREF m_panelColor = 0;
    COLORREF m_textColor = 0;
    COLORREF m_subtleColor = 0;
    COLORREF m_lineColor = 0;
    HFONT m_titleFont = nullptr;
    HFONT m_headerFont = nullptr;
};
} // namespace smoothcam

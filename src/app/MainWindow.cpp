#include "MainWindow.h"

#include "Log.h"
#include "Paths.h"
#include "Registration.h"
#include "Status.h"
#include "Version.h"
#include "flow/NvofFlow.h"
#include "resource.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <numeric>
#include <set>

namespace smoothcam
{
namespace
{
constexpr UINT WM_APP_FRAME = WM_APP + 1;
constexpr UINT_PTR kStatusTimer = 1;
constexpr wchar_t kPreviewClass[] = L"SmoothCamPreview";
constexpr wchar_t kUiKey[] = L"Software\\SmoothCam";

// Layout metrics, in DIPs.
constexpr int kPanelWidth = 340;
constexpr int kPad = 20;
constexpr int kMargin = 12;
constexpr int kButtonHeight = 30;

struct ModeChoice
{
    Mode mode;
    const wchar_t* label;
    const wchar_t* hint;
};
constexpr ModeChoice kModes[] = {
    {Mode::Quality, L"Quality", L"Builds the frames in between two real ones. Adds about one camera frame of delay."},
    {Mode::Balanced, L"Balanced", L"Lighter motion estimation. Good for older laptops and integrated graphics."},
    {Mode::LowLatency, L"Low latency", L"Predicts motion forward instead of waiting. Least delay, more artifacts."},
    {Mode::Off, L"Off", L"No smoothing: the camera passed through, paced to the frame rate."},
};

template <class T>
struct Choice
{
    T value;
    const wchar_t* label;
};

constexpr Choice<EngineChoice> kEngines[] = {
    {EngineChoice::Auto, L"Automatic"},
    {EngineChoice::Nvidia, L"NVIDIA Optical Flow"},
    {EngineChoice::Gpu, L"GPU compute"},
    {EngineChoice::Basic, L"Basic blend"},
};
constexpr Choice<Sharpening> kSharpness[] = {
    {Sharpening::Auto, L"Auto"},     {Sharpening::Off, L"Off"},   {Sharpening::Low, L"Low"},
    {Sharpening::Medium, L"Medium"}, {Sharpening::High, L"High"},
};
constexpr Choice<NoiseReduction> kNoise[] = {
    {NoiseReduction::Off, L"Off"},
    {NoiseReduction::Low, L"Low"},
    {NoiseReduction::Medium, L"Medium"},
    {NoiseReduction::High, L"High"},
};
constexpr Choice<LowLightBoost> kLowLight[] = {
    {LowLightBoost::Off, L"Off"},
    {LowLightBoost::Auto, L"Auto"},
    {LowLightBoost::Strong, L"Strong"},
};
constexpr Choice<Background> kBackgrounds[] = {
    {Background::Off, L"Off"},
    {Background::Blur, L"Blur"},
    {Background::StrongBlur, L"Strong blur"},
};
constexpr Choice<AutoFrame> kAutoFrames[] = {
    {AutoFrame::Off, L"Off"},
    {AutoFrame::Wide, L"Follow face"},
    {AutoFrame::Close, L"Close-up"},
};
constexpr Choice<FramingGrid> kGrids[] = {
    {FramingGrid::Off, L"Off"},
    {FramingGrid::RuleOfThirds, L"Rule of thirds"},
    {FramingGrid::GoldenRatio, L"Golden ratio"},
    {FramingGrid::Crosshairs, L"Crosshairs"},
    {FramingGrid::Square, L"Square"},
};

// Controls that live in the settings panel (everything else is camera area).
constexpr int kPanelControls[] = {
    IDC_LOGO,           IDC_TITLE,          IDC_SUBTITLE,         IDC_COLLAPSE,    IDC_LABEL_CAMERA,
    IDC_CAMERA,         IDC_LABEL_FPS,      IDC_FPS,              IDC_LABEL_MODE,  IDC_MODE,
    IDC_MODE_HINT,      IDC_LABEL_RES,      IDC_RESOLUTION,       IDC_LABEL_ENGINE, IDC_ENGINE,
    IDC_LABEL_SHARP,    IDC_SHARPNESS,      IDC_EFFECTS_HEADER,   IDC_LABEL_NOISE, IDC_NOISE,
    IDC_LABEL_LOWLIGHT, IDC_LOWLIGHT,       IDC_LABEL_BACKGROUND, IDC_BACKGROUND,  IDC_LABEL_AUTOFRAME,
    IDC_AUTOFRAME,      IDC_LABEL_GRID,     IDC_GRID,             IDC_STEADY,      IDC_COMPARE,
    IDC_FPS_NOTE,       IDC_REG_STATUS,     IDC_ABOUT,            IDC_REPAIR,
};

constexpr int kCombos[] = {IDC_CAMERA, IDC_FPS,      IDC_MODE,       IDC_RESOLUTION, IDC_ENGINE, IDC_SHARPNESS,
                           IDC_NOISE,  IDC_LOWLIGHT, IDC_BACKGROUND, IDC_AUTOFRAME,  IDC_GRID};

bool SystemUsesDarkTheme()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

std::wstring LoadTextResource(HINSTANCE instance, int id)
{
    HRSRC res = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res)
        return {};
    HGLOBAL data = LoadResource(instance, res);
    const auto* bytes = static_cast<const char*>(LockResource(data));
    const DWORD size = SizeofResource(instance, res);
    std::wstring text = FromUtf8(std::string_view(bytes, size));
    // Edit controls want CRLF.
    std::wstring out;
    out.reserve(text.size() + text.size() / 20);
    for (wchar_t c : text)
    {
        if (c == L'\n' && (out.empty() || out.back() != L'\r'))
            out += L'\r';
        out += c;
    }
    return out;
}

int ComboAdd(HWND combo, const std::wstring& text)
{
    return static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
}

DWORD ReadUiValue(const wchar_t* name, DWORD fallback)
{
    DWORD value = fallback;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kUiKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return fallback;
    return value;
}

void WriteUiValue(const wchar_t* name, DWORD value)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, kUiKey, name, REG_DWORD, &value, sizeof(value));
}

std::wstring AspectName(uint32_t w, uint32_t h)
{
    const double a = static_cast<double>(w) / h;
    if (std::abs(a - 16.0 / 9.0) < 0.02)
        return L"16:9";
    if (std::abs(a - 4.0 / 3.0) < 0.02)
        return L"4:3";
    if (std::abs(a - 16.0 / 10.0) < 0.02)
        return L"16:10";
    if (std::abs(a - 5.0 / 4.0) < 0.02)
        return L"5:4";
    const uint32_t g = std::gcd(w, h);
    return std::to_wstring(w / g) + L":" + std::to_wstring(h / g);
}

std::wstring MegapixelLabel(uint32_t w, uint32_t h)
{
    wchar_t text[64];
    swprintf_s(text, L"%.1fMP %s (%ux%u)", w * static_cast<double>(h) / 1e6, AspectName(w, h).c_str(), w, h);
    return text;
}
} // namespace

// ---- window plumbing ----------------------------------------------------------------

int MainWindow::Run(HINSTANCE instance)
{
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPreviewClass;
    RegisterClassExW(&wc);

    MainWindow window;
    window.m_instance = instance;
    HWND hwnd = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DialogProc,
                                   reinterpret_cast<LPARAM>(&window));
    if (!hwnd)
        return static_cast<int>(GetLastError());
    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        // F11 anywhere in the window toggles full screen.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F11)
        {
            window.ToggleFullscreen();
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

INT_PTR CALLBACK MainWindow::DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;
    if (message == WM_INITDIALOG)
    {
        self = reinterpret_cast<MainWindow*>(lParam);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    }
    return self ? self->Handle(message, wParam, lParam) : FALSE;
}

LRESULT CALLBACK MainWindow::PreviewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (self)
            self->PaintPreview(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK:
        SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_PREVIEW, STN_DBLCLK), reinterpret_cast<LPARAM>(hwnd));
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

INT_PTR MainWindow::Handle(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        OnInit();
        return TRUE;

    case WM_COMMAND:
        OnCommand(LOWORD(wParam), HIWORD(wParam));
        return TRUE;

    case WM_TIMER:
        if (wParam == kStatusTimer)
            RefreshStatus();
        return TRUE;

    case WM_APP_FRAME:
        m_framePosted = false;
        InvalidateRect(m_preview, nullptr, FALSE);
        if (!m_previewRunning)
        {
            SetDlgItemTextW(m_hwnd, IDC_PREVIEW_BTN, L"Start &preview");
            // Another app took the camera over: step aside.
            if (m_previewLost.exchange(false) && !m_collapsed)
                SetCollapsed(true, true);
        }
        return TRUE;

    case WM_SIZE:
        Layout();
        return TRUE;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = Scale(m_collapsed ? kPanelWidth + 16 : (m_panelVisible ? kPanelWidth : 0) + 420);
        mmi->ptMinTrackSize.y = Scale(m_panelVisible || m_collapsed ? 760 : 360);
        return TRUE;
    }

    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        FillRect(dc, &rc, m_baseBrush);
        if (m_panelVisible || m_collapsed)
        {
            RECT panel = PanelRect();
            FillRect(dc, &panel, m_panelBrush);
            if (!m_collapsed)
            {
                // Hairline between the camera area and the panel.
                RECT line = panel;
                if (m_panelOnLeft)
                    line.left = line.right - 1;
                else
                    line.right = line.left + 1;
                HBRUSH lineBrush = CreateSolidBrush(m_lineColor);
                FillRect(dc, &line, lineBrush);
                DeleteObject(lineBrush);
            }
        }
        SetWindowLongPtrW(m_hwnd, DWLP_MSGRESULT, 1);
        return TRUE;
    }

    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wParam) == m_preview)
        {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x == -1 && pt.y == -1)
            {
                RECT rc;
                GetWindowRect(m_preview, &rc);
                pt = {(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
            }
            ShowContextMenu(pt);
            return TRUE;
        }
        break;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return reinterpret_cast<INT_PTR>(OnColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)
            ApplyTheme();
        break;

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wParam);
        CreateFonts();
        m_renderer.SetDpi(m_dpi);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return TRUE;
    }

    case WM_CLOSE:
        DestroyWindow(m_hwnd);
        return TRUE;

    case WM_DESTROY:
        OnDestroy();
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

void MainWindow::OnInit()
{
    m_settings = Settings::Load(SettingsPath());
    m_dpi = GetDpiForWindow(m_hwnd);
    m_preview = GetDlgItem(m_hwnd, IDC_PREVIEW);
    SetWindowLongPtrW(m_preview, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    if (const HRESULT hr = m_renderer.Init(m_preview); FAILED(hr))
        SC_LOG_ERROR(L"Preview: Direct2D init failed (0x%08X)", hr);
    LoadUiState();
    m_renderer.SetGrid(m_grid);

    HICON big = static_cast<HICON>(LoadImageW(m_instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    HICON smallIcon = nullptr;
    LoadIconWithScaleDown(m_instance, MAKEINTRESOURCEW(IDI_APP), GetSystemMetrics(SM_CXSMICON),
                          GetSystemMetrics(SM_CYSMICON), &smallIcon);
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));

    // Start at a comfortable size, centered on the work area.
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT work = mi.rcWork;
    const int ww = std::min<int>(Scale(1240), (work.right - work.left) * 9 / 10);
    const int wh = std::min<int>(Scale(840), (work.bottom - work.top) * 19 / 20);
    SetWindowPos(m_hwnd, nullptr, work.left + ((work.right - work.left) - ww) / 2,
                 work.top + ((work.bottom - work.top) - wh) / 2, ww, wh, SWP_NOZORDER);

    CreateFonts();
    ApplyTheme();
    FillControls();
    RefreshRegistration();
    Layout();
    SetTimer(m_hwnd, kStatusTimer, 700, nullptr);

    // If another app is already using SmoothCam, don't fight it for the
    // camera: show only the settings until it's done.
    const auto st = Status::Load(StatusPath());
    if (st.streaming && st.IsFresh())
        SetCollapsed(true, true);
    else
        StartPreview();
    RefreshStatus();
}

void MainWindow::OnDestroy()
{
    KillTimer(m_hwnd, kStatusTimer);
    StopPreview();
    SaveUiState();
    for (HFONT f : {m_titleFont, m_headerFont})
        if (f)
            DeleteObject(f);
    if (m_baseBrush)
        DeleteObject(m_baseBrush);
    if (m_panelBrush)
        DeleteObject(m_panelBrush);
}

void MainWindow::CreateFonts()
{
    for (HFONT* f : {&m_titleFont, &m_headerFont})
        if (*f)
            DeleteObject(*f);
    m_titleFont = CreateFontW(-MulDiv(15, static_cast<int>(m_dpi), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Segoe UI Variable Display");
    m_headerFont = CreateFontW(-MulDiv(10, static_cast<int>(m_dpi), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    SendDlgItemMessageW(m_hwnd, IDC_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(m_titleFont), TRUE);
    SendDlgItemMessageW(m_hwnd, IDC_EFFECTS_HEADER, WM_SETFONT, reinterpret_cast<WPARAM>(m_headerFont), TRUE);

    HICON logo = nullptr;
    if (SUCCEEDED(LoadIconWithScaleDown(m_instance, MAKEINTRESOURCEW(IDI_APP), Scale(40), Scale(40), &logo)))
        SendDlgItemMessageW(m_hwnd, IDC_LOGO, STM_SETICON, reinterpret_cast<WPARAM>(logo), 0);
}

void MainWindow::ApplyTheme()
{
    m_dark = SystemUsesDarkTheme();
    const BOOL dark = m_dark;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    m_baseColor = m_dark ? RGB(24, 24, 26) : RGB(232, 232, 236);
    m_panelColor = m_dark ? RGB(36, 36, 38) : RGB(251, 251, 252);
    m_textColor = m_dark ? RGB(240, 240, 240) : RGB(26, 26, 26);
    m_subtleColor = m_dark ? RGB(165, 165, 170) : RGB(96, 96, 100);
    m_lineColor = m_dark ? RGB(56, 56, 60) : RGB(214, 214, 220);
    if (m_baseBrush)
        DeleteObject(m_baseBrush);
    if (m_panelBrush)
        DeleteObject(m_panelBrush);
    m_baseBrush = CreateSolidBrush(m_baseColor);
    m_panelBrush = CreateSolidBrush(m_panelColor);

    for (int id : kCombos)
        SetWindowTheme(GetDlgItem(m_hwnd, id), m_dark ? L"DarkMode_CFD" : nullptr, nullptr);
    for (int id : {IDC_PREVIEW_BTN, IDC_FULLSCREEN, IDC_TOGGLE_PANEL, IDC_REPAIR, IDC_ABOUT, IDC_COLLAPSE})
        SetWindowTheme(GetDlgItem(m_hwnd, id), m_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    // Themed checkboxes ignore the text color; in dark mode draw them classic.
    for (int id : {IDC_STEADY, IDC_COMPARE})
        SetWindowTheme(GetDlgItem(m_hwnd, id), m_dark ? L"" : nullptr, m_dark ? L"" : nullptr);

    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

bool MainWindow::IsInPanel(HWND control) const
{
    const int id = control ? GetDlgCtrlID(control) : 0;
    if (id == IDC_STATUS)
        return m_collapsed; // the status line moves into the panel when collapsed
    return std::find(std::begin(kPanelControls), std::end(kPanelControls), id) != std::end(kPanelControls);
}

HBRUSH MainWindow::OnColor(HDC dc, HWND control)
{
    const int id = control ? GetDlgCtrlID(control) : 0;
    const bool subtle = id == IDC_SUBTITLE || id == IDC_MODE_HINT || id == IDC_FPS_NOTE || id == IDC_REG_STATUS ||
                        id == IDC_STATUS;
    const bool panel = IsInPanel(control);
    SetTextColor(dc, subtle ? m_subtleColor : m_textColor);
    SetBkColor(dc, panel ? m_panelColor : m_baseColor);
    return panel ? m_panelBrush : m_baseBrush;
}

// ---- layout -------------------------------------------------------------------------

RECT MainWindow::PanelRect() const
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    if (m_collapsed)
        return rc;
    if (!m_panelVisible)
        return RECT{0, 0, 0, 0};
    const int w = Scale(kPanelWidth);
    return m_panelOnLeft ? RECT{0, 0, w, rc.bottom} : RECT{rc.right - w, 0, rc.right, rc.bottom};
}

void MainWindow::Layout()
{
    RECT client;
    GetClientRect(m_hwnd, &client);
    if (client.right <= 0 || client.bottom <= 0)
        return;

    const int line = std::max(Scale(16), static_cast<int>(SendDlgItemMessageW(m_hwnd, IDC_CAMERA, CB_GETITEMHEIGHT,
                                                                                static_cast<WPARAM>(-1), 0)));
    const int comboH = line + Scale(10);
    const int buttonH = Scale(kButtonHeight);

    HDWP dwp = BeginDeferWindowPos(60);
    auto place = [&](int id, int x, int y, int w, int h, bool visible = true) {
        HWND ctl = GetDlgItem(m_hwnd, id);
        dwp = DeferWindowPos(dwp, ctl, nullptr, x, y, std::max(0, w), std::max(0, h),
                             SWP_NOZORDER | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    };

    // Settings panel.
    const RECT panel = PanelRect();
    const bool show = m_panelVisible || m_collapsed;
    const int px = panel.left + Scale(kPad);
    const int pw = std::min<int>((panel.right - panel.left) - 2 * Scale(kPad), Scale(kPanelWidth - 2 * kPad));
    int y = Scale(kPad - 4);
    place(IDC_LOGO, px, y, Scale(40), Scale(40), show);
    place(IDC_TITLE, px + Scale(50), y - Scale(2), pw - Scale(50) - Scale(104), Scale(28), show);
    place(IDC_COLLAPSE, px + pw - Scale(100), y, Scale(100), Scale(26), show);
    place(IDC_SUBTITLE, px + Scale(50), y + Scale(26), pw - Scale(50), line, show);
    y += Scale(56);

    auto single = [&](int labelId, int comboId) {
        place(labelId, px, y, pw, line, show);
        y += line + Scale(3);
        // A combo's height in SetWindowPos is its drop-down extent.
        place(comboId, px, y, pw, Scale(320), show);
        y += comboH + Scale(10);
    };
    auto pair = [&](int labelA, int comboA, int labelB, int comboB) {
        const int half = (pw - Scale(12)) / 2;
        place(labelA, px, y, half, line, show);
        place(labelB, px + half + Scale(12), y, half, line, show);
        y += line + Scale(3);
        place(comboA, px, y, half, Scale(320), show);
        place(comboB, px + half + Scale(12), y, half, Scale(320), show);
        y += comboH + Scale(10);
    };

    single(IDC_LABEL_CAMERA, IDC_CAMERA);
    pair(IDC_LABEL_FPS, IDC_FPS, IDC_LABEL_MODE, IDC_MODE);
    place(IDC_MODE_HINT, px, y - Scale(4), pw, line * 2, show);
    y += line * 2 + Scale(6);
    single(IDC_LABEL_RES, IDC_RESOLUTION);
    pair(IDC_LABEL_ENGINE, IDC_ENGINE, IDC_LABEL_SHARP, IDC_SHARPNESS);

    place(IDC_EFFECTS_HEADER, px, y + Scale(2), pw, line + Scale(4), show);
    y += line + Scale(10);
    pair(IDC_LABEL_NOISE, IDC_NOISE, IDC_LABEL_LOWLIGHT, IDC_LOWLIGHT);
    pair(IDC_LABEL_BACKGROUND, IDC_BACKGROUND, IDC_LABEL_AUTOFRAME, IDC_AUTOFRAME);
    single(IDC_LABEL_GRID, IDC_GRID);
    place(IDC_STEADY, px, y, pw, line + Scale(4), show);
    y += line + Scale(8);
    place(IDC_COMPARE, px, y, pw, line + Scale(4), show);
    y += line + Scale(10);

    const int bottom = client.bottom - Scale(kPad);
    place(IDC_ABOUT, px, bottom - buttonH, Scale(88), buttonH, show);
    place(IDC_REPAIR, px + pw - Scale(140), bottom - buttonH, Scale(140), buttonH, show);
    const int regTop = bottom - buttonH - Scale(8) - line * 2;
    place(IDC_REG_STATUS, px, regTop, pw, line * 2, show);
    if (m_collapsed)
    {
        // No camera area: the live status goes into the panel.
        place(IDC_FPS_NOTE, px, y, pw, line * 2, false);
        place(IDC_STATUS, px, y, pw, std::max(line, regTop - y - Scale(6)), true);
    }
    else
    {
        place(IDC_FPS_NOTE, px, y, pw, std::max(line, regTop - y - Scale(6)), show);
    }

    // Camera area: the live view, with a strip of buttons and status under it.
    const bool cameraArea = !m_collapsed;
    const int camLeft = (m_panelVisible && m_panelOnLeft ? panel.right : 0) + Scale(kMargin);
    const int camRight = (m_panelVisible && !m_panelOnLeft ? panel.left : client.right) - Scale(kMargin);
    const int stripTop = client.bottom - Scale(kMargin) - buttonH;
    place(IDC_PREVIEW, camLeft, Scale(kMargin), camRight - camLeft, stripTop - Scale(10) - Scale(kMargin), cameraArea);

    int x = camLeft;
    place(IDC_PREVIEW_BTN, x, stripTop, Scale(124), buttonH, cameraArea);
    x += Scale(124) + Scale(8);
    place(IDC_FULLSCREEN, x, stripTop, Scale(124), buttonH, cameraArea);
    x += Scale(124) + Scale(8);
    place(IDC_TOGGLE_PANEL, x, stripTop, Scale(124), buttonH, cameraArea);
    x += Scale(124) + Scale(14);
    if (cameraArea)
        place(IDC_STATUS, x, stripTop + (buttonH - line * 2) / 2, camRight - x, line * 2, camRight - x > Scale(80));

    EndDeferWindowPos(dwp);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void MainWindow::ToggleFullscreen()
{
    if (m_collapsed)
        return; // nothing to show full screen
    if (!m_fullscreen)
    {
        m_savedPlacement.length = sizeof(m_savedPlacement);
        GetWindowPlacement(m_hwnd, &m_savedPlacement);
        m_savedStyle = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE,
                          (m_savedStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                                            WS_MAXIMIZE)) | WS_POPUP);
        m_fullscreen = true;
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        SetDlgItemTextW(m_hwnd, IDC_FULLSCREEN, L"Exit f&ull screen");
    }
    else
    {
        m_fullscreen = false;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, m_savedStyle);
        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        SetDlgItemTextW(m_hwnd, IDC_FULLSCREEN, L"F&ull screen");
    }
    Layout();
}

void MainWindow::SetPanelVisible(bool visible)
{
    m_panelVisible = visible;
    SetDlgItemTextW(m_hwnd, IDC_TOGGLE_PANEL, visible ? L"Hide se&ttings" : L"Show se&ttings");
    Layout();
    SaveUiState();
}

void MainWindow::SetPanelOnLeft(bool left)
{
    m_panelOnLeft = left;
    Layout();
    SaveUiState();
}

void MainWindow::SetCollapsed(bool collapsed, bool automatic)
{
    if (collapsed == m_collapsed)
        return;
    if (collapsed)
    {
        if (m_fullscreen)
            ToggleFullscreen();
        StopPreview();
        GetWindowRect(m_hwnd, &m_expandedRect);
        m_collapsed = true;
        m_autoCollapsed = automatic;
        m_idleTicks = 0;
        SetDlgItemTextW(m_hwnd, IDC_COLLAPSE, L"Show pre&view");
        // Shrink the window to just the panel, keeping its right edge in place
        // when the panel was on the right.
        RECT frame{0, 0, Scale(kPanelWidth), 100};
        AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE)), m_dpi);
        const int width = frame.right - frame.left;
        const int left = m_panelOnLeft ? m_expandedRect.left : m_expandedRect.right - width;
        SetWindowPos(m_hwnd, nullptr, left, m_expandedRect.top, width, m_expandedRect.bottom - m_expandedRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    else
    {
        m_collapsed = false;
        m_autoCollapsed = false;
        SetDlgItemTextW(m_hwnd, IDC_COLLAPSE, L"Hide pre&view");
        if (m_expandedRect.right > m_expandedRect.left)
            SetWindowPos(m_hwnd, nullptr, m_expandedRect.left, m_expandedRect.top,
                         m_expandedRect.right - m_expandedRect.left, m_expandedRect.bottom - m_expandedRect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        StartPreview();
    }
    Layout();
    RefreshStatus();
}

void MainWindow::ShowContextMenu(POINT screen)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (m_fullscreen ? MF_CHECKED : 0), IDM_FULLSCREEN, L"Full screen\tF11");
    AppendMenuW(menu, MF_STRING | (m_panelVisible ? 0 : MF_CHECKED), IDM_TOGGLE_PANEL, L"Hide settings");
    AppendMenuW(menu, MF_STRING | (m_panelOnLeft ? MF_CHECKED : 0), IDM_PANEL_LEFT, L"Settings on the left");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_PREVIEW, m_previewRunning ? L"Stop preview" : L"Start preview");
    const int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd)
        OnCommand(cmd, 0);
}

void MainWindow::LoadUiState()
{
    m_panelOnLeft = ReadUiValue(L"PanelOnLeft", 0) != 0;
    m_panelVisible = ReadUiValue(L"PanelHidden", 0) == 0;
    m_grid = static_cast<FramingGrid>(std::min<DWORD>(ReadUiValue(L"FramingGrid", 0), 4));
    SetDlgItemTextW(m_hwnd, IDC_TOGGLE_PANEL, m_panelVisible ? L"Hide se&ttings" : L"Show se&ttings");
}

void MainWindow::SaveUiState() const
{
    WriteUiValue(L"PanelOnLeft", m_panelOnLeft ? 1 : 0);
    WriteUiValue(L"PanelHidden", m_panelVisible ? 0 : 1);
    WriteUiValue(L"FramingGrid", static_cast<DWORD>(m_grid));
}

// ---- settings -----------------------------------------------------------------------

void MainWindow::QueryCameraDefaults(Settings& s) const
{
    // The camera's "default" is its largest resolution at its best frame rate.
    auto link = s.device;
    if (link.empty() && !m_cameras.empty())
        link = m_cameras.front().symbolicLink;
    std::vector<CameraFormat> formats;
    if (link.empty() || FAILED(EnumerateCameraFormats(link, formats)) || formats.empty())
        return;
    double bestFps = 0;
    for (const auto& f : formats)
        bestFps = std::max(bestFps, f.fps);
    s.maxWidth = s.maxHeight = 0;
    for (const auto& f : formats)
    {
        if (f.fps + 0.5 >= bestFps &&
            static_cast<uint64_t>(f.width) * f.height > static_cast<uint64_t>(s.maxWidth) * s.maxHeight)
        {
            s.maxWidth = f.width;
            s.maxHeight = f.height;
        }
    }
    s.cameraFps = static_cast<uint32_t>(std::lround(bestFps));
}

void MainWindow::FillCameraDependentControls()
{
    m_filling = true;

    // Resolutions: everything the camera delivers at full frame rate, plus
    // larger sizes reached by upscaling.
    m_resolutions.clear();
    std::vector<CameraFormat> formats;
    auto link = m_settings.device;
    if (link.empty() && !m_cameras.empty())
        link = m_cameras.front().symbolicLink;
    if (!link.empty())
        EnumerateCameraFormats(link, formats);
    const uint32_t defW = m_settings.maxWidth ? m_settings.maxWidth : 1280;
    const uint32_t defH = m_settings.maxHeight ? m_settings.maxHeight : 720;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    std::vector<ResolutionChoice> native;
    for (const auto& f : formats)
    {
        if (f.width < 640 || ((f.width | f.height) & 1u) || f.fps + 0.5 < m_settings.cameraFps)
            continue;
        if (!seen.insert({f.width, f.height}).second)
            continue;
        ResolutionChoice c{f.width, f.height, f.width == defW && f.height == defH, MegapixelLabel(f.width, f.height)};
        if (c.isDefault)
            c.label += L" (default)";
        native.push_back(c);
    }
    if (native.empty())
        native.push_back({defW, defH, true, MegapixelLabel(defW, defH) + L" (default)"});
    // 16:9 first, then other shapes; largest first within each.
    std::sort(native.begin(), native.end(), [](const auto& a, const auto& b) {
        const bool wa = std::abs(static_cast<double>(a.width) / a.height - 16.0 / 9.0) < 0.02;
        const bool wb = std::abs(static_cast<double>(b.width) / b.height - 16.0 / 9.0) < 0.02;
        if (wa != wb)
            return wa;
        return static_cast<uint64_t>(a.width) * a.height > static_cast<uint64_t>(b.width) * b.height;
    });
    if (defH < 2160)
        m_resolutions.push_back({3840, 2160, false, L"Upscaled 16:9 (3840x2160) 4K"});
    if (defH < 1440)
        m_resolutions.push_back({2560, 1440, false, L"Upscaled 16:9 (2560x1440) 2K"});
    if (defH < 1080)
        m_resolutions.push_back({1920, 1080, false, L"Upscaled 16:9 (1920x1080) Full HD"});
    m_resolutions.insert(m_resolutions.end(), native.begin(), native.end());

    HWND res = GetDlgItem(m_hwnd, IDC_RESOLUTION);
    SendMessageW(res, CB_RESETCONTENT, 0, 0);
    int selected = -1;
    int defaultIndex = 0;
    for (size_t i = 0; i < m_resolutions.size(); ++i)
    {
        const auto& r = m_resolutions[i];
        ComboAdd(res, r.label);
        if (r.isDefault)
            defaultIndex = static_cast<int>(i);
        if (m_settings.outputWidth && r.width == m_settings.outputWidth && r.height == m_settings.outputHeight)
            selected = static_cast<int>(i);
    }
    SendMessageW(res, CB_SETCURSEL, selected >= 0 ? selected : defaultIndex, 0);

    // Frame rates: the camera's own rate (default) up to 60.
    const uint32_t camFps = m_settings.cameraFps ? m_settings.cameraFps : 30;
    m_fpsChoices = {camFps};
    for (uint32_t f : {30u, 48u, 50u, 60u})
        if (f > camFps)
            m_fpsChoices.push_back(f);
    HWND fps = GetDlgItem(m_hwnd, IDC_FPS);
    SendMessageW(fps, CB_RESETCONTENT, 0, 0);
    int fpsSel = static_cast<int>(m_fpsChoices.size()) - 1;
    for (size_t i = 0; i < m_fpsChoices.size(); ++i)
    {
        ComboAdd(fps, std::to_wstring(m_fpsChoices[i]) + (i == 0 ? L" fps (default)" : L" fps"));
        if (m_fpsChoices[i] == m_settings.outputFps)
            fpsSel = static_cast<int>(i);
    }
    SendMessageW(fps, CB_SETCURSEL, fpsSel, 0);
    m_filling = false;
}

void MainWindow::FillControls()
{
    m_filling = true;

    HWND camera = GetDlgItem(m_hwnd, IDC_CAMERA);
    SendMessageW(camera, CB_RESETCONTENT, 0, 0);
    ComboAdd(camera, L"Automatic (first camera found)");
    m_cameras = EnumerateCameras();
    int cameraIndex = 0;
    for (size_t i = 0; i < m_cameras.size(); ++i)
    {
        ComboAdd(camera, m_cameras[i].name);
        if (_wcsicmp(m_cameras[i].symbolicLink.c_str(), m_settings.device.c_str()) == 0)
            cameraIndex = static_cast<int>(i + 1);
    }
    if (cameraIndex == 0 && !m_settings.device.empty())
    {
        // The configured camera is unplugged right now - keep it selected.
        ComboAdd(camera, m_settings.deviceName + L" (not connected)");
        cameraIndex = static_cast<int>(m_cameras.size() + 1);
    }
    SendMessageW(camera, CB_SETCURSEL, cameraIndex, 0);

    // Learn the camera's defaults once (or after upgrading from a build that
    // didn't record them).
    if (!m_settings.cameraFps || !m_settings.maxWidth)
    {
        Settings s = m_settings;
        QueryCameraDefaults(s);
        if (!(s == m_settings))
        {
            m_settings = s;
            m_settings.Save(SettingsPath());
        }
    }

    auto fill = [&](int id, const auto& items, auto selected, auto labelOf) {
        HWND combo = GetDlgItem(m_hwnd, id);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < std::size(items); ++i)
        {
            ComboAdd(combo, labelOf(items[i]));
            if (items[i].value == selected)
                SendMessageW(combo, CB_SETCURSEL, i, 0);
        }
    };
    auto plain = [](const auto& c) { return std::wstring(c.label); };

    HWND mode = GetDlgItem(m_hwnd, IDC_MODE);
    SendMessageW(mode, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < std::size(kModes); ++i)
    {
        ComboAdd(mode, kModes[i].label);
        if (kModes[i].mode == m_settings.mode)
            SendMessageW(mode, CB_SETCURSEL, i, 0);
    }
    const bool nvof = IsNvofRuntimePresent();
    fill(IDC_ENGINE, kEngines, m_settings.engine, [nvof](const auto& e) {
        std::wstring label = e.label;
        if (e.value == EngineChoice::Nvidia && !nvof)
            label += L" (n/a)";
        return label;
    });
    fill(IDC_SHARPNESS, kSharpness, m_settings.sharpening, plain);
    fill(IDC_NOISE, kNoise, m_settings.noiseReduction, plain);
    fill(IDC_LOWLIGHT, kLowLight, m_settings.lowLight, plain);
    fill(IDC_BACKGROUND, kBackgrounds, m_settings.background, plain);
    fill(IDC_AUTOFRAME, kAutoFrames, m_settings.autoFrame, plain);
    fill(IDC_GRID, kGrids, m_grid, plain);

    // Checked = let the camera slow down in dim light (the opposite of steady).
    CheckDlgButton(m_hwnd, IDC_STEADY, m_settings.steadyFps ? BST_UNCHECKED : BST_CHECKED);
    CheckDlgButton(m_hwnd, IDC_COMPARE, m_settings.compare ? BST_CHECKED : BST_UNCHECKED);
    m_filling = false;
    FillCameraDependentControls();
    UpdateHints();
}

void MainWindow::UpdateHints()
{
    const int mode = static_cast<int>(SendDlgItemMessageW(m_hwnd, IDC_MODE, CB_GETCURSEL, 0, 0));
    if (mode >= 0 && mode < static_cast<int>(std::size(kModes)))
        SetDlgItemTextW(m_hwnd, IDC_MODE_HINT, kModes[mode].hint);
}

void MainWindow::ReadControls(int changedId)
{
    Settings s = m_settings;
    auto selected = [&](int id) { return static_cast<int>(SendDlgItemMessageW(m_hwnd, id, CB_GETCURSEL, 0, 0)); };

    const int cam = selected(IDC_CAMERA);
    if (cam == 0)
    {
        s.device.clear();
        s.deviceName.clear();
    }
    else if (cam > 0 && cam <= static_cast<int>(m_cameras.size()))
    {
        s.device = m_cameras[cam - 1].symbolicLink;
        s.deviceName = m_cameras[cam - 1].name;
    }
    if (int i = selected(IDC_FPS); i >= 0 && i < static_cast<int>(m_fpsChoices.size()))
        s.outputFps = m_fpsChoices[i];
    if (int i = selected(IDC_MODE); i >= 0)
        s.mode = kModes[i].mode;
    if (int i = selected(IDC_RESOLUTION); i >= 0 && i < static_cast<int>(m_resolutions.size()))
    {
        const auto& r = m_resolutions[i];
        // The camera default is stored as 0x0 so it follows the camera.
        s.outputWidth = r.isDefault ? 0 : r.width;
        s.outputHeight = r.isDefault ? 0 : r.height;
    }
    if (int i = selected(IDC_ENGINE); i >= 0)
        s.engine = kEngines[i].value;
    if (int i = selected(IDC_SHARPNESS); i >= 0)
        s.sharpening = kSharpness[i].value;
    if (int i = selected(IDC_NOISE); i >= 0)
        s.noiseReduction = kNoise[i].value;
    if (int i = selected(IDC_LOWLIGHT); i >= 0)
        s.lowLight = kLowLight[i].value;
    if (int i = selected(IDC_BACKGROUND); i >= 0)
        s.background = kBackgrounds[i].value;
    if (int i = selected(IDC_AUTOFRAME); i >= 0)
        s.autoFrame = kAutoFrames[i].value;
    s.steadyFps = IsDlgButtonChecked(m_hwnd, IDC_STEADY) != BST_CHECKED;
    s.compare = IsDlgButtonChecked(m_hwnd, IDC_COMPARE) == BST_CHECKED;

    if (changedId == IDC_CAMERA)
    {
        s.outputWidth = s.outputHeight = 0;
        QueryCameraDefaults(s);
        // Keep the user's frame rate if the new camera can be smoothed to it.
        s.outputFps = std::max(s.outputFps, s.cameraFps);
    }

    if (s == m_settings)
        return;
    const bool formatChanged = s.outputFps != m_settings.outputFps || s.outputWidth != m_settings.outputWidth ||
                               s.outputHeight != m_settings.outputHeight || s.device != m_settings.device;
    m_settings = s;
    if (!m_settings.Save(SettingsPath()) && !m_saveWarned)
    {
        m_saveWarned = true;
        MessageBoxW(m_hwnd,
                    L"SmoothCam couldn't save its settings. Reinstalling SmoothCam usually fixes the folder "
                    L"permissions.",
                    L"SmoothCam", MB_OK | MB_ICONWARNING);
    }
    if (changedId == IDC_CAMERA)
        FillCameraDependentControls();
    UpdateHints();

    if (formatChanged)
    {
        SetDlgItemTextW(m_hwnd, IDC_FPS_NOTE,
                        L"Frame rate and resolution apply to apps when they next open the camera. The preview "
                        L"has already switched.");
        // Reopen the preview so the new format is what it (and every app from
        // now on) actually gets.
        if (m_previewRunning)
            StartPreview();
    }
}

void MainWindow::OnCommand(int id, int code)
{
    switch (id)
    {
    case IDC_CAMERA:
    case IDC_FPS:
    case IDC_MODE:
    case IDC_ENGINE:
    case IDC_RESOLUTION:
    case IDC_SHARPNESS:
    case IDC_NOISE:
    case IDC_LOWLIGHT:
    case IDC_BACKGROUND:
    case IDC_AUTOFRAME:
        if (code == CBN_SELCHANGE && !m_filling)
            ReadControls(id);
        break;
    case IDC_GRID:
        if (code == CBN_SELCHANGE && !m_filling)
        {
            const int i = static_cast<int>(SendDlgItemMessageW(m_hwnd, IDC_GRID, CB_GETCURSEL, 0, 0));
            if (i >= 0)
            {
                m_grid = kGrids[i].value;
                m_renderer.SetGrid(m_grid);
                SaveUiState();
                InvalidateRect(m_preview, nullptr, FALSE);
            }
        }
        break;
    case IDC_STEADY:
    case IDC_COMPARE:
        if (code == BN_CLICKED && !m_filling)
            ReadControls(id);
        break;
    case IDC_PREVIEW:
        if (code == STN_DBLCLK)
            ToggleFullscreen();
        break;
    case IDC_PREVIEW_BTN:
    case IDM_PREVIEW:
        if (m_previewRunning)
            StopPreview();
        else
            StartPreview();
        break;
    case IDC_FULLSCREEN:
    case IDM_FULLSCREEN:
        ToggleFullscreen();
        break;
    case IDC_TOGGLE_PANEL:
    case IDM_TOGGLE_PANEL:
        SetPanelVisible(!m_panelVisible);
        break;
    case IDM_PANEL_LEFT:
        SetPanelOnLeft(!m_panelOnLeft);
        break;
    case IDC_COLLAPSE:
        m_userExpanded = m_collapsed;
        SetCollapsed(!m_collapsed, false);
        break;
    case IDC_REPAIR:
        Repair();
        break;
    case IDC_ABOUT:
        ShowAbout();
        break;
    case IDCANCEL:
        // Esc leaves full screen; it never closes the window.
        if (m_fullscreen)
            ToggleFullscreen();
        break;
    }
}

void MainWindow::RefreshStatus()
{
    const auto st = Status::Load(StatusPath());
    const bool busy = st.streaming && st.IsFresh();

    // Another app started using SmoothCam while our preview was off: get out
    // of its way. (Right after we stop our own preview the status file still
    // says "streaming" for a moment, so give that a few seconds to settle.)
    const bool recentlyStopped = GetTickCount64() - m_previewStoppedAt < 4000;
    if (busy && !m_previewRunning && !m_collapsed && !recentlyStopped && !m_userExpanded)
        SetCollapsed(true, true);

    // ...and come back once it's done.
    if (m_autoCollapsed)
    {
        m_idleTicks = busy ? 0 : m_idleTicks + 1;
        if (m_idleTicks >= 3)
            SetCollapsed(false, true);
    }
    if (!busy)
        m_userExpanded = false;

    std::wstring text;
    if (busy)
    {
        wchar_t line[300];
        swprintf_s(line, L"%ux%u • camera %.0f fps → %.0f fps • +%.0f ms • GPU %.1f ms • %s",
                   st.width, st.height, st.inputFps, st.outputFps, st.addedLatencyMs, st.gpuMs, st.engine.c_str());
        text = line;
        if (m_collapsed && !m_previewRunning)
            text = L"In use by another app. The preview is hidden so it doesn't disturb it.\n" + text;
        std::wstring extra;
        if (!st.upscale.empty())
            extra = L"Upscaling " + st.upscale;
        if (!st.filters.empty())
            extra += (extra.empty() ? L"" : L" • ") + st.filters;
        if (!st.message.empty())
            extra += (extra.empty() ? L"" : L" • ") + st.message;
        if (!extra.empty())
            text += L"\n" + extra;
        // The preview itself stops at 2K (see StartPreview), so say what apps get.
        uint32_t ow = 0, oh = 0;
        EffectiveOutputSize(m_settings, ow, oh);
        if (m_previewRunning && ow > st.width)
        {
            swprintf_s(line, L"\nPreview runs at %ux%u to stay light. Apps get %ux%u.", st.width, st.height, ow, oh);
            text += line;
        }
    }
    else
    {
        text = L"Not in use. In Teams, Zoom, Meet, Discord or OBS, choose “SmoothCam” as your camera.";
    }
    wchar_t current[600] = L"";
    GetDlgItemTextW(m_hwnd, IDC_STATUS, current, static_cast<int>(std::size(current)));
    if (text != current)
        SetDlgItemTextW(m_hwnd, IDC_STATUS, text.c_str());
}

void MainWindow::RefreshRegistration()
{
    const auto reg = QueryRegistration();
    if (reg.comRegistered && reg.cameraPresent)
        SetDlgItemTextW(m_hwnd, IDC_REG_STATUS, (L"Installed as “" + reg.cameraName + L"”").c_str());
    else
        SetDlgItemTextW(m_hwnd, IDC_REG_STATUS, L"The SmoothCam camera isn't registered. Click Repair to fix it.");
}

void MainWindow::Repair()
{
    const bool wasRunning = m_previewRunning;
    StopPreview();
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const DWORD code = RunElevated(L"--register");
    if (code != 0)
    {
        wchar_t msg[200];
        swprintf_s(msg, L"Registering the camera failed (0x%08X).\nRun SmoothCam.exe --diag for details.", code);
        MessageBoxW(m_hwnd, msg, L"SmoothCam", MB_OK | MB_ICONWARNING);
    }
    RefreshRegistration();
    if (wasRunning)
        StartPreview();
}

// ---- preview ------------------------------------------------------------------------

void MainWindow::StartPreview()
{
    StopPreview();
    if (m_collapsed)
        return;
    {
        std::lock_guard g(m_frameLock);
        m_hasFrame = false;
        m_previewMessage = L"Starting the camera…";
    }
    // Open SmoothCam at exactly the resolution apps will get (capped at 2K to
    // keep the preview light; 4K is still what apps receive).
    uint32_t w = 0, h = 0;
    EffectiveOutputSize(m_settings, w, h);
    const uint32_t width = std::min(w, 2560u);
    m_previewLost = false;
    m_previewRunning = true;
    SetDlgItemTextW(m_hwnd, IDC_PREVIEW_BTN, L"Stop &preview");
    m_previewThread = std::thread([this, width] { PreviewLoop(width); });
    InvalidateRect(m_preview, nullptr, FALSE);
}

void MainWindow::StopPreview()
{
    const bool wasRunning = m_previewRunning || m_previewThread.joinable();
    m_previewRunning = false;
    if (m_previewThread.joinable())
        m_previewThread.join();
    if (wasRunning)
        m_previewStoppedAt = GetTickCount64();
    {
        std::lock_guard g(m_frameLock);
        m_hasFrame = false;
        m_previewMessage = L"Preview is off.\nApps can still use SmoothCam while this is off.";
    }
    if (m_hwnd)
    {
        SetDlgItemTextW(m_hwnd, IDC_PREVIEW_BTN, L"Start &preview");
        InvalidateRect(m_preview, nullptr, FALSE);
    }
}

void MainWindow::PreviewLoop(uint32_t preferredWidth)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    {
        CameraReader reader;
        HRESULT hr = reader.Open(preferredWidth);
        if (FAILED(hr))
        {
            std::lock_guard g(m_frameLock);
            if (hr == MF_E_NO_CAPTURE_DEVICES_AVAILABLE)
            {
                m_previewMessage = L"The SmoothCam camera isn't installed.\nClick “Repair camera”.";
            }
            else
            {
                m_previewMessage = L"Another app is using SmoothCam right now.";
                m_previewLost = true;
            }
            m_previewRunning = false;
        }
        CameraReader::Frame frame;
        while (m_previewRunning)
        {
            if (FAILED(reader.Read(frame)))
            {
                std::lock_guard g(m_frameLock);
                m_previewMessage = L"Another app took over the camera.";
                m_hasFrame = false;
                m_previewLost = true;
                m_previewRunning = false;
                break;
            }
            {
                std::lock_guard g(m_frameLock);
                std::swap(m_frame, frame);
                m_hasFrame = true;
                m_frameChanged = true;
            }
            if (!m_framePosted.exchange(true))
                PostMessageW(m_hwnd, WM_APP_FRAME, 0, 0);
        }
    }
    PostMessageW(m_hwnd, WM_APP_FRAME, 0, 0);
    MFShutdown();
    if (SUCCEEDED(co))
        CoUninitialize();
}

void MainWindow::PaintPreview(HDC dc)
{
    std::lock_guard g(m_frameLock);
    const bool changed = m_frameChanged;
    m_frameChanged = false;
    if (m_hasFrame)
        m_renderer.Paint(dc, m_frame.pixels.data(), m_frame.width, m_frame.height, changed, {});
    else
        m_renderer.Paint(dc, nullptr, 0, 0, false, m_previewMessage);
}

// ---- about -------------------------------------------------------------------------

void MainWindow::ShowAbout()
{
    DialogBoxParamW(m_instance, MAKEINTRESOURCEW(IDD_ABOUT), m_hwnd, AboutProc, reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK MainWindow::AboutProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG: {
        auto* self = reinterpret_cast<MainWindow*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
        SendDlgItemMessageW(hwnd, IDC_ABOUT_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(self->m_titleFont), TRUE);
        RECT r{0, 0, 24, 24};
        MapDialogRect(hwnd, &r);
        HICON logo = nullptr;
        if (SUCCEEDED(LoadIconWithScaleDown(self->m_instance, MAKEINTRESOURCEW(IDI_APP), r.right, r.bottom, &logo)))
            SendDlgItemMessageW(hwnd, IDC_ABOUT_LOGO, STM_SETICON, reinterpret_cast<WPARAM>(logo), 0);

        std::wstring text = L"Version " SMOOTHCAM_VERSION_WSTRING L" (beta)\n"
                            L"Copyright © 2026 SmoothCam contributors. MIT License.\n\n"
                            L"Everything runs on this PC: no network access, no telemetry, no account.\n\n"
                            L"Uses AMD FidelityFX Super Resolution 1, Google's MediaPipe selfie segmentation "
                            L"model and Windows' built-in face detection. NVIDIA, AMD and Google are trademarks of "
                            L"their owners; SmoothCam is independent and not affiliated with them or Microsoft.";
        SetDlgItemTextW(hwnd, IDC_ABOUT_TEXT, text.c_str());

        std::wstring notices = LoadTextResource(self->m_instance, IDR_LICENSE);
        notices += L"\r\n\r\n";
        notices += LoadTextResource(self->m_instance, IDR_NOTICES);
        SetDlgItemTextW(hwnd, IDC_ABOUT_NOTICES, notices.c_str());

        const BOOL dark = self->m_dark;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        if (self->m_dark)
        {
            SetWindowTheme(GetDlgItem(hwnd, IDC_ABOUT_NOTICES), L"DarkMode_Explorer", nullptr);
            SetWindowTheme(GetDlgItem(hwnd, IDOK), L"DarkMode_Explorer", nullptr);
        }
        return TRUE;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, DWLP_USER));
        if (self)
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, self->m_textColor);
            SetBkColor(dc, self->m_panelColor);
            return reinterpret_cast<INT_PTR>(self->m_panelBrush);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hwnd, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
} // namespace smoothcam

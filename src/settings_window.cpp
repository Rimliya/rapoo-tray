#include "settings_window.h"
#include "profile_manager.h"
#include "battery_estimator.h"
#include "device_manager.h"
#include "dpi_window.h"
#include "osd_window.h"
#include "rapoo_protocol.h"
#include "tray_menu.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace Settings {
namespace {

constexpr const WCHAR* WINDOW_CLASS = L"RapooProfilesSettingsWnd";
constexpr int WINDOW_WIDTH = 1000;
constexpr int WINDOW_HEIGHT = 780;

enum ControlId {
    IDC_PROFILE_LIST = 3001,
    IDC_ADD_PROFILE,
    IDC_DELETE_PROFILE,
    IDC_PROFILE_NAME,
    IDC_POLLING_RATE,
    IDC_SLEEP_TIME,
    IDC_DEFAULT_PROFILE,
    IDC_APP_PATH,
    IDC_BROWSE_APP,
    IDC_CLEAR_APP,
    IDC_AUTO_SWITCH,
    IDC_OSD_ENABLED,
    IDC_SAVE,
    IDC_CLOSE,
    IDC_STATUS,
    IDC_RF_FULL,
    IDC_MODEL_SELECT,
    IDC_OPEN_DPI,
    IDC_DPI_X,
    IDC_DPI_Y,
    IDC_CAPTURE_DPI,
    IDC_TAB_PROFILES,
    IDC_TAB_AUTOMATION,
    IDC_TAB_DPI
};

constexpr UINT_PTR TIMER_ADVANCED_INIT = 0x4101;
constexpr UINT_PTR TIMER_ADVANCED_REFRESH = 0x4102;

enum class SettingsTab {
    Profiles = 0,
    Automation = 1,
    Dpi = 2
};

HWND g_hwnd = NULL;
HWND g_hOwner = NULL;
HWND g_hProfileList = NULL;
HWND g_hTitle = NULL;
HWND g_hSubtitle = NULL;
HWND g_hStatus = NULL;
HWND g_hModelCombo = NULL;
HFONT g_fontNormal = NULL;
HFONT g_fontBold = NULL;
HFONT g_fontTitle = NULL;
HFONT g_fontSmall = NULL;
int g_dpi = 96;
int g_selectedId = 0;
bool g_loading = false;
bool g_dirty = false;
bool g_refreshingList = false;
bool g_statusGood = true;
bool g_classRegistered = false;
bool g_dark = false;
bool g_applyingTheme = false;
HBRUSH g_brushBg = NULL;
HBRUSH g_brushSurface = NULL;
HBRUSH g_brushField = NULL;
HIMAGELIST g_profileRowImageList = NULL;
std::vector<HWND> g_hProfilePageControls;
std::vector<HWND> g_automationPageControls;
HWND g_hDpiPage = NULL;
HWND g_hTabButtons[3] = {0};
SettingsTab g_activeTab = SettingsTab::Profiles;
std::wstring g_headerStatus = L"正在连接鼠标…";
bool g_checkDefault = false;
bool g_checkAutoSwitch = true;
bool g_checkOsd = true;
bool g_checkRfFull = false;
Device::State g_deviceState;
bool g_deviceStateValid = false;
WCHAR g_lastAutoModelName[64] = {0};

int S(int value) {
    return MulDiv(value, g_dpi, 96);
}

COLORREF ColorBg() { return g_dark ? RGB(24, 26, 32) : RGB(255, 255, 255); }
COLORREF ColorSurface() { return g_dark ? RGB(30, 33, 42) : RGB(247, 249, 252); }
COLORREF ColorField() { return g_dark ? RGB(37, 41, 51) : RGB(242, 245, 250); }
COLORREF ColorBorder() { return g_dark ? RGB(50, 54, 65) : RGB(205, 212, 222); }
COLORREF ColorHover() { return g_dark ? RGB(52, 58, 72) : RGB(232, 238, 248); }
COLORREF ColorText() { return g_dark ? RGB(235, 240, 248) : RGB(20, 24, 32); }
COLORREF ColorMuted() { return g_dark ? RGB(140, 150, 165) : RGB(105, 115, 130); }
COLORREF ColorAccent() { return g_dark ? RGB(96, 205, 255) : RGB(0, 110, 215); }
COLORREF ColorButton() { return g_dark ? RGB(38, 42, 52) : RGB(240, 243, 248); }
COLORREF ColorAccentFill() { return g_dark ? RGB(55, 103, 145) : RGB(218, 232, 255); }
COLORREF ColorDanger() { return g_dark ? RGB(235, 92, 92) : RGB(190, 48, 48); }

void GetCurrentDpiDefaults(int& x, int& y) {
    const Device::State state = Device::GetCurrentState();
    BatteryEstimate::DpiSpec spec;
    BatteryEstimate::GetDpiSpecForState(state, spec);

    x = 800;
    y = 800;
    if (Device::IsDpiConfigValid() && state.dpiX > 0) {
        x = std::clamp(state.dpiX, spec.minDpi, spec.maxDpi);
        y = std::clamp(state.dpiY > 0 ? state.dpiY : state.dpiX, spec.minDpi, spec.maxDpi);
    }
}

std::wstring FormatDpiValue(const Profiles::Definition& profile) {
    int x = profile.dpiX;
    int y = profile.dpiY;
    if (x <= 0 || y <= 0) GetCurrentDpiDefaults(x, y);
    if (x == y) return std::to_wstring(x);
    return std::to_wstring(x) + L"/" + std::to_wstring(y);
}

UINT GetSystemDpiSafe() {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef UINT (WINAPI *pfnGetDpiForSystem)();
        pfnGetDpiForSystem fn = (pfnGetDpiForSystem)GetProcAddress(hUser, "GetDpiForSystem");
        if (fn) {
            UINT dpi = fn();
            if (dpi > 0) return dpi;
        }
    }
    HDC dc = GetDC(NULL);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return dpi > 0 ? dpi : 96;
}



HFONT MakeFont(int px, int weight) {
    return CreateFontW(
        -S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI"
    );
}

void SetText(HWND control, const WCHAR* text) {
    if (control) SetWindowTextW(control, text ? text : L"");
}

std::wstring GetText(HWND control) {
    WCHAR buffer[2048] = {0};
    if (control) GetWindowTextW(control, buffer, ARRAYSIZE(buffer));
    return buffer;
}

void SetStatus(const WCHAR* text, bool good = true, bool clearDirty = false) {
    g_statusGood = good;
    if (clearDirty) g_dirty = false;
    SetText(g_hStatus, text);
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
}

HFONT GetNormalFont() {
    return g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

RECT TabRect(int index) {
    static const int xs[] = { 28, 190, 314 };
    static const int widths[] = { 154, 116, 116 };
    if (index < 0 || index > 2) return RECT{0, 0, 0, 0};
    return RECT{ S(xs[index]), S(94), S(xs[index] + widths[index]), S(130) };
}

int HitTestTab(POINT point) {
    for (int i = 0; i < 3; ++i) {
        RECT tab = TabRect(i);
        if (PtInRect(&tab, point)) return i;
    }
    return -1;
}

void TrackPageControl(std::vector<HWND>& group, HWND control) {
    if (control) group.push_back(control);
}

void ShowPageControls() {
    for (HWND control : g_hProfilePageControls) {
        ShowWindow(control, g_activeTab == SettingsTab::Profiles ? SW_SHOWNA : SW_HIDE);
    }
    for (HWND control : g_automationPageControls) {
        ShowWindow(control, g_activeTab == SettingsTab::Automation ? SW_SHOWNA : SW_HIDE);
    }
    if (g_hDpiPage && IsWindow(g_hDpiPage)) {
        ShowWindow(g_hDpiPage, g_activeTab == SettingsTab::Dpi ? SW_SHOWNA : SW_HIDE);
    }
    if (g_hStatus) ShowWindow(g_hStatus, g_activeTab == SettingsTab::Dpi ? SW_HIDE : SW_SHOWNA);
    HWND save = g_hwnd ? GetDlgItem(g_hwnd, IDC_SAVE) : NULL;
    if (save) ShowWindow(save, g_activeTab == SettingsTab::Dpi ? SW_HIDE : SW_SHOWNA);
}

void SwitchTab(SettingsTab tab) {
    if (tab == g_activeTab) return;
    g_activeTab = tab;
    if (tab == SettingsTab::Profiles) {
        SetStatus(g_dirty ? L"有未保存的更改" : L"选择配置进行编辑，或新增一个配置", true);
    } else if (tab == SettingsTab::Automation) {
        SetStatus(L"自动化和全局选项会随当前配置一起保存", true);
    }
    if (tab == SettingsTab::Dpi) {
        if (!g_hDpiPage || !IsWindow(g_hDpiPage)) {
            g_hDpiPage = DpiWindow::CreateEmbedded(g_hwnd, S(28), S(152), S(944), S(526));
        }
        if (g_hDpiPage) {
            ShowWindow(g_hDpiPage, SW_SHOWNA);
            DpiWindow::RefreshEmbedded();
            SetFocus(g_hDpiPage);
        }
    }
    ShowPageControls();
    for (int i = 0; i < 3; ++i) {
        if (!g_hTabButtons[i]) continue;
        SendMessageW(g_hTabButtons[i], WM_SETFONT,
            (WPARAM)((int)tab == i ? g_fontBold : g_fontNormal), TRUE);
        InvalidateRect(g_hTabButtons[i], NULL, TRUE);
    }
    RECT page = { S(20), S(146), S(980), S(686) };
    InvalidateRect(g_hwnd, &page, FALSE);
}

HWND CreateControl(const WCHAR* className, const WCHAR* text, DWORD style, DWORD exStyle, int x, int y, int w, int h, int id) {
    HWND control = CreateWindowExW(
        exStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        S(x), S(y), S(w), S(h),
        g_hwnd,
        (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE),
        NULL
    );
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)GetNormalFont(), TRUE);
    return control;
}

HWND CreateText(const WCHAR* text, int x, int y, int w, int h, int id = 0, DWORD style = SS_LEFT) {
    return CreateControl(L"STATIC", text, style, 0, x, y, w, h, id);
}

LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);

HWND CreateButton(const WCHAR* text, int x, int y, int w, int h, int id) {
    HWND button = CreateControl(L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, 0, x, y, w, h, id);
    if (button) SetWindowSubclass(button, ButtonSubclassProc, 1, 0);
    return button;
}

HWND CreateCheck(const WCHAR* text, int x, int y, int w, int h, int id) {
    HWND check = CreateControl(L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, 0, x, y, w, h, id);
    if (check) SetWindowSubclass(check, ButtonSubclassProc, 1, 0);
    return check;
}

void ApplyFonts() {
    EnumChildWindows(g_hwnd, [](HWND child, LPARAM) -> BOOL {
        if (child == g_hTitle) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
        } else if (child == g_hSubtitle || child == g_hStatus) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
        } else {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
        }
        return TRUE;
    }, 0);
}

bool IsCheckControlId(int id) {
    return id == IDC_DEFAULT_PROFILE || id == IDC_AUTO_SWITCH ||
           id == IDC_OSD_ENABLED || id == IDC_RF_FULL;
}

bool GetOwnedChecked(int id) {
    if (id == IDC_DEFAULT_PROFILE) return g_checkDefault;
    if (id == IDC_AUTO_SWITCH) return g_checkAutoSwitch;
    if (id == IDC_OSD_ENABLED) return g_checkOsd;
    if (id == IDC_RF_FULL) return g_checkRfFull;
    return false;
}

void SetOwnedChecked(int id, bool checked) {
    if (id == IDC_DEFAULT_PROFILE) g_checkDefault = checked;
    else if (id == IDC_AUTO_SWITCH) g_checkAutoSwitch = checked;
    else if (id == IDC_OSD_ENABLED) g_checkOsd = checked;
    else if (id == IDC_RF_FULL) g_checkRfFull = checked;
    else return;
    HWND control = g_hwnd ? GetDlgItem(g_hwnd, id) : NULL;
    if (control) InvalidateRect(control, NULL, TRUE);
}

void RebuildBrushes() {
    if (g_brushBg) DeleteObject(g_brushBg);
    if (g_brushSurface) DeleteObject(g_brushSurface);
    if (g_brushField) DeleteObject(g_brushField);
    g_brushBg = CreateSolidBrush(ColorBg());
    g_brushSurface = CreateSolidBrush(ColorSurface());
    g_brushField = CreateSolidBrush(ColorField());
}

void ApplyNativeTheme() {
    if (!g_hwnd) return;
    const WCHAR* explorerTheme = g_dark ? L"DarkMode_Explorer" : L"Explorer";
    const WCHAR* editTheme = g_dark ? L"DarkMode_CFD" : L"Explorer";
    SetWindowTheme(g_hwnd, explorerTheme, NULL);

    HWND controls[] = {
        GetDlgItem(g_hwnd, IDC_PROFILE_NAME),
        GetDlgItem(g_hwnd, IDC_POLLING_RATE),
        GetDlgItem(g_hwnd, IDC_SLEEP_TIME),
        GetDlgItem(g_hwnd, IDC_DPI_X),
        GetDlgItem(g_hwnd, IDC_DPI_Y),
        GetDlgItem(g_hwnd, IDC_APP_PATH),
        g_hModelCombo,
        g_hProfileList
    };
    for (HWND control : controls) {
        if (control) SetWindowTheme(control, editTheme, NULL);
    }

    if (g_hProfileList) {
        ListView_SetBkColor(g_hProfileList, ColorField());
        ListView_SetTextBkColor(g_hProfileList, ColorField());
        ListView_SetTextColor(g_hProfileList, ColorText());
        HWND header = ListView_GetHeader(g_hProfileList);
        if (header) SetWindowTheme(header, explorerTheme, NULL);
    }
}

void UpdateTheme() {
    if (!g_hwnd || g_applyingTheme) return;
    g_applyingTheme = true;
    g_dark = Tray::IsDarkMode();
    RebuildBrushes();
    RECT rc = {0};
    GetWindowRect(g_hwnd, &rc);
    Tray::ApplyModernWindowStyle(g_hwnd, g_dark, rc.right - rc.left, rc.bottom - rc.top);
    ApplyNativeTheme();
    if (g_hDpiPage && IsWindow(g_hDpiPage)) {
        SendMessageW(g_hDpiPage, WM_THEMECHANGED, 0, 0);
    }
    RedrawWindow(g_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    g_applyingTheme = false;
}

void FillRounded(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawSection(HDC dc, int x, int y, int w, int h, const WCHAR* title) {
    RECT section = { S(x), S(y), S(x + w), S(y + h) };
    FillRounded(dc, section, ColorSurface(), ColorBorder(), S(12));

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, g_fontBold ? g_fontBold : GetNormalFont());

    RECT dot = { section.left + S(16), section.top + S(17), section.left + S(22), section.top + S(23) };
    HBRUSH accentBrush = CreateSolidBrush(ColorAccent());
    HGDIOBJ oldBrush = SelectObject(dc, accentBrush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(accentBrush);

    SetTextColor(dc, ColorText());
    RECT titleRect = { section.left + S(29), section.top + S(6), section.right - S(12), section.top + S(30) };
    DrawTextW(dc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
}

void DrawLabel(HDC dc, const WCHAR* text, int x, int y, int w, int h, HFONT font, COLORREF color) {
    HGDIOBJ oldFont = SelectObject(dc, font ? font : GetNormalFont());
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT rect = { S(x), S(y), S(x + w), S(y + h) };
    DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

void DrawOwnerButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    RECT rc = item->rcItem;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool focused = (item->itemState & ODS_FOCUS) != 0;
    bool isCheck = IsCheckControlId((int)item->CtlID);
    bool checked = isCheck ? GetOwnedChecked((int)item->CtlID) : false;
    bool hovered = false;

    POINT cursor = {0};
    GetCursorPos(&cursor);
    HWND underCursor = WindowFromPoint(cursor);
    if (underCursor == item->hwndItem) hovered = true;

    WCHAR text[512] = {0};
    GetWindowTextW(item->hwndItem, text, ARRAYSIZE(text));

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_fontNormal ? g_fontNormal : GetNormalFont());

    if (isCheck) {
        HBRUSH cardBackground = CreateSolidBrush(ColorSurface());
        FillRect(dc, &rc, cardBackground);
        DeleteObject(cardBackground);
        if (hovered && !disabled) {
            RECT hover = rc;
            hover.right = std::min(hover.right, hover.left + S(330));
            FillRounded(dc, hover, ColorHover(), ColorHover(), S(6));
        }
        int box = S(17);
        int boxTop = rc.top + ((rc.bottom - rc.top) - box) / 2;
        int boxLeft = rc.left + S(2);
        RECT boxRect = { boxLeft, boxTop, boxLeft + box, boxTop + box };
        COLORREF fill = checked ? ColorAccent() : ColorField();
        COLORREF border = checked ? ColorAccent() : ColorBorder();
        FillRounded(dc, boxRect, fill, border, S(4));

        if (checked) {
            HPEN checkPen = CreatePen(PS_SOLID, std::max(1, S(2)),
                g_dark ? RGB(10, 14, 20) : RGB(255, 255, 255));
            HGDIOBJ oldPen = SelectObject(dc, checkPen);
            MoveToEx(dc, boxRect.left + S(4), boxRect.top + S(9), NULL);
            LineTo(dc, boxRect.left + S(7), boxRect.top + S(12));
            LineTo(dc, boxRect.left + S(13), boxRect.top + S(5));
            SelectObject(dc, oldPen);
            DeleteObject(checkPen);
        }

        SetTextColor(dc, disabled ? ColorMuted() : ColorText());
        RECT textRect = { boxRect.right + S(9), rc.top, rc.right - S(4), rc.bottom };
        DrawTextW(dc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (focused) DrawFocusRect(dc, &rc);
        return;
    }

    COLORREF fill = pressed ? ColorHover() : (hovered ? ColorHover() : ColorButton());
    COLORREF textColor = ColorText();
    if (item->CtlID == IDC_SAVE) {
        fill = disabled ? ColorMuted() : ColorAccent();
        textColor = g_dark ? RGB(10, 14, 20) : RGB(255, 255, 255);
    }
    if (disabled) {
        fill = ColorSurface();
        textColor = ColorMuted();
    }

    FillRounded(dc, rc, fill, pressed ? ColorAccent() : ColorBorder(), S(7));
    SetTextColor(dc, textColor);
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (focused && !disabled) DrawFocusRect(dc, &rc);
}

LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&track);
        InvalidateRect(hWnd, NULL, FALSE);
    } else if (msg == WM_MOUSELEAVE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(hWnd, NULL, FALSE);
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void ToggleOwnedCheck(int id) {
    SetOwnedChecked(id, !GetOwnedChecked(id));
}

void UpdateAutoModelLabel() {
    if (!g_hModelCombo) return;
    if (_wcsicmp(g_lastAutoModelName, g_deviceState.modelName) == 0) return;
    StringCchCopyW(g_lastAutoModelName, ARRAYSIZE(g_lastAutoModelName), g_deviceState.modelName);

    int current = (int)SendMessageW(g_hModelCombo, CB_GETCURSEL, 0, 0);
    LRESULT currentData = current >= 0 ? SendMessageW(g_hModelCombo, CB_GETITEMDATA, current, 0) : 0;
    std::wstring label = L"自动识别";
    if (g_deviceState.modelName[0] && _wcsicmp(g_deviceState.modelName, L"通用") != 0) {
        label += L"（";
        label += g_deviceState.modelName;
        label += L"）";
    }

    SendMessageW(g_hModelCombo, CB_DELETESTRING, 0, 0);
    SendMessageW(g_hModelCombo, CB_INSERTSTRING, 0, (LPARAM)label.c_str());
    SendMessageW(g_hModelCombo, CB_SETITEMDATA, 0, 0);

    int count = (int)SendMessageW(g_hModelCombo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        if (SendMessageW(g_hModelCombo, CB_GETITEMDATA, i, 0) == currentData) {
            SendMessageW(g_hModelCombo, CB_SETCURSEL, i, 0);
            break;
        }
    }
}

void SelectSavedModel() {
    if (!g_hModelCombo) return;
    std::wstring saved = BatteryEstimate::LoadOverride();
    const auto& models = BatteryEstimate::Models();
    int selected = 0;
    for (size_t i = 0; i < models.size(); ++i) {
        if (_wcsicmp(models[i].key.c_str(), saved.c_str()) == 0) {
            selected = (int)i;
            break;
        }
    }
    SendMessageW(g_hModelCombo, CB_SETCURSEL, selected, 0);
}

void RefreshDeviceState() {
    if (!g_hwnd || !Device::IsStarted()) return;
    g_deviceState = Device::GetCurrentState();
    g_deviceStateValid = true;
    UpdateAutoModelLabel();
    if (g_deviceState.rfAvailable) SetOwnedChecked(IDC_RF_FULL, g_deviceState.rfFull);
    HWND rfControl = GetDlgItem(g_hwnd, IDC_RF_FULL);
    // Writing the strategy only needs a live control endpoint. A transient
    // register-read timeout must not make the checkbox look disconnected.
    if (rfControl) EnableWindow(rfControl, g_deviceState.isConnected ? TRUE : FALSE);

    WCHAR status[220] = {0};
    if (!g_deviceState.isConnected) {
        StringCchCopyW(status, ARRAYSIZE(status), L"● 设备休眠 / 未连接");
    } else {
        StringCchPrintfW(status, ARRAYSIZE(status), L"● %s · %s · 已连接 · %d%%",
            g_deviceState.modelName[0] ? g_deviceState.modelName : L"通用",
            g_deviceState.isWired ? L"USB 有线" : L"2.4G 无线",
            g_deviceState.battery);
    }
    g_headerStatus = status;

    RECT header = { S(60), S(42), S(790), S(76) };
    InvalidateRect(g_hwnd, &header, FALSE);
    RECT advanced = { S(496), S(346), S(972), S(658) };
    InvalidateRect(g_hwnd, &advanced, FALSE);
}

void DrawBatteryEstimate(HDC dc) {
    BatteryEstimate::Result estimate = BatteryEstimate::Calculate(g_deviceState);
    if (!estimate.available) {
        DrawLabel(dc, L"续航数据暂不可用", 512, 552, 422, 22, g_fontSmall, ColorMuted());
        return;
    }

    WCHAR text[160] = {0};
    if (!g_deviceStateValid) {
        StringCchPrintfW(text, ARRAYSIZE(text), L"满电约 %d–%d h · 连接后更新", estimate.fullMinHours, estimate.fullMaxHours);
    } else if (g_deviceState.isCharging) {
        StringCchPrintfW(text, ARRAYSIZE(text), L"满电约 %d–%d h · 充电中", estimate.fullMinHours, estimate.fullMaxHours);
    } else {
        StringCchPrintfW(
            text,
            ARRAYSIZE(text),
            L"满电约 %d–%d h · 当前 %d%% 约 %d–%d h",
            estimate.fullMinHours,
            estimate.fullMaxHours,
            g_deviceState.battery,
            estimate.remainingMinHours,
            estimate.remainingMaxHours
        );
    }
    DrawLabel(dc, text, 512, 552, 422, 22, g_fontSmall, ColorAccent());
}

void DrawTabButton(const DRAWITEMSTRUCT* item) {
    if (!item) return;
    int index = (int)item->CtlID - IDC_TAB_PROFILES;
    if (index < 0 || index > 2) return;
    static const WCHAR* labels[] = { L"配置档案", L"自动化", L"DPI 档位" };
    RECT tab = item->rcItem;
    bool active = (int)g_activeTab == index;
    POINT cursor = {0};
    GetCursorPos(&cursor);
    bool hovered = WindowFromPoint(cursor) == item->hwndItem;
    COLORREF fill = active ? ColorAccentFill() : (hovered ? ColorHover() : ColorField());
    COLORREF border = active ? ColorAccent() : ColorBorder();
    HBRUSH background = CreateSolidBrush(ColorBg());
    FillRect(item->hDC, &tab, background);
    DeleteObject(background);
    FillRounded(item->hDC, tab, fill, border, S(9));
    HGDIOBJ oldFont = SelectObject(item->hDC, active ? g_fontBold : g_fontNormal);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, active ? ColorAccent() : (hovered ? ColorText() : ColorMuted()));
    DrawTextW(item->hDC, labels[index], -1, &tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(item->hDC, oldFont);
}

void PaintHeader(HDC dc) {
    HICON icon = LoadIconW((HINSTANCE)GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    if (icon) DrawIconEx(dc, S(28), S(20), icon, S(34), S(34), 0, NULL, DI_NORMAL);

    DrawLabel(dc, L"配置中心", 72, 14, 360, 34, g_fontTitle, ColorText());
    COLORREF statusColor = ColorMuted();
    if (g_deviceStateValid && g_deviceState.isConnected) {
        statusColor = g_dark ? RGB(80, 220, 135) : RGB(16, 145, 60);
    }
    DrawLabel(dc, g_headerStatus.c_str(), 72, 48, 650, 22, g_fontSmall, statusColor);
    DrawLabel(dc, L"rapoo-tray", 812, 24, 160, 22, g_fontSmall, ColorMuted());
}

void PaintProfilesPage(HDC dc) {
    DrawSection(dc, 28, 152, 390, 524, L"配置档案");
    DrawSection(dc, 436, 152, 536, 524, L"配置详情");

    RECT listFrame = { S(44), S(202), S(402), S(548) };
    FillRounded(dc, listFrame, ColorField(), ColorBorder(), S(9));
    RECT listHeader = { S(46), S(204), S(400), S(232) };
    HBRUSH headerBrush = CreateSolidBrush(g_dark ? RGB(36, 39, 48) : RGB(238, 241, 246));
    FillRect(dc, &listHeader, headerBrush);
    DeleteObject(headerBrush);
    HPEN line = CreatePen(PS_SOLID, 1, ColorBorder());
    HGDIOBJ oldPen = SelectObject(dc, line);
    MoveToEx(dc, listHeader.left, listHeader.bottom - 1, NULL);
    LineTo(dc, listHeader.right, listHeader.bottom - 1);
    SelectObject(dc, oldPen);
    DeleteObject(line);

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, g_fontSmall);
    SetTextColor(dc, ColorMuted());
    const int headerX[] = { 4, 104, 160, 220, 296 };
    const int headerW[] = { 100, 56, 60, 76, 42 };
    const WCHAR* headers[] = { L"名称", L"Hz", L"DPI", L"程序", L"状态" };
    for (int i = 0; i < 5; ++i) {
        RECT cell = { listHeader.left + S(headerX[i]), listHeader.top,
                      listHeader.left + S(headerX[i] + headerW[i]), listHeader.bottom };
        DrawTextW(dc, headers[i], -1, &cell, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
    DrawLabel(dc, L"每个配置同时保存回报率、DPI 与休眠时间", 44, 602, 350, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"切换配置时会一次性应用到鼠标。", 44, 624, 350, 22, g_fontSmall, ColorMuted());

    RECT fields[] = {
        { S(460), S(224), S(948), S(258) },
        { S(460), S(296), S(696), S(330) },
        { S(714), S(296), S(948), S(330) },
        { S(506), S(440), S(646), S(474) },
        { S(702), S(440), S(840), S(474) }
    };
    for (const RECT& field : fields) FillRounded(dc, field, ColorField(), ColorBorder(), S(7));

    DrawLabel(dc, L"名称", 460, 198, 140, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"回报率", 460, 270, 140, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"休眠时间", 714, 270, 140, 22, g_fontSmall, ColorMuted());

    HPEN divider = CreatePen(PS_SOLID, 1, ColorBorder());
    oldPen = SelectObject(dc, divider);
    MoveToEx(dc, S(460), S(378), NULL);
    LineTo(dc, S(948), S(378));
    SelectObject(dc, oldPen);
    DeleteObject(divider);

    DrawLabel(dc, L"DPI 参数", 460, 384, 140, 24, g_fontBold, ColorText());
    DrawLabel(dc, L"随配置一起切换", 566, 386, 180, 20, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"X 轴", 460, 414, 40, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"Y 轴", 656, 414, 40, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"在这里管理档位列表；档位编辑器已合并到上方标签页。", 460, 526, 488, 20, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"配置只保存当前 X/Y 数值，不会在未保存时改写鼠标。", 460, 548, 488, 20, g_fontSmall, ColorMuted());
}

void PaintAutomationPage(HDC dc) {
    DrawSection(dc, 28, 152, 944, 178, L"前台程序自动切换");
    DrawLabel(dc, L"关联程序", 44, 190, 140, 22, g_fontNormal, ColorText());
    RECT appField = { S(44), S(214), S(826), S(250) };
    FillRounded(dc, appField, ColorField(), ColorBorder(), S(7));
    DrawLabel(dc, L"当该程序进入前台（含无边框 / 独占全屏）时，自动应用当前配置；离开后恢复默认配置。",
        44, 258, 880, 22, g_fontSmall, ColorMuted());

    DrawSection(dc, 28, 346, 456, 312, L"全局选项");
    DrawLabel(dc, L"控制自动化的总开关；关闭后不影响托盘里的手动设置。",
        44, 500, 410, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"悬浮窗会显示设备状态和配置切换结果。",
        44, 524, 410, 22, g_fontSmall, ColorMuted());

    DrawSection(dc, 496, 346, 476, 312, L"高级设置");
    DrawLabel(dc, L"满格射频会增加功耗；不勾选时使用智能调节。",
        512, 426, 430, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"鼠标型号", 512, 462, 90, 26, g_fontNormal, ColorText());
    RECT modelField = { S(512), S(486), S(934), S(522) };
    FillRounded(dc, modelField, ColorField(), ColorBorder(), S(7));
    DrawBatteryEstimate(dc);
}

void PaintSettingsWindow(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hWnd, &ps);
    RECT rc = {0};
    GetClientRect(hWnd, &rc);

    HBRUSH background = CreateSolidBrush(ColorBg());
    FillRect(dc, &rc, background);
    DeleteObject(background);

    PaintHeader(dc);

    if (g_activeTab == SettingsTab::Profiles) PaintProfilesPage(dc);
    else if (g_activeTab == SettingsTab::Automation) PaintAutomationPage(dc);

    HPEN footerLine = CreatePen(PS_SOLID, 1, ColorBorder());
    HGDIOBJ oldPen = SelectObject(dc, footerLine);
    MoveToEx(dc, S(28), S(690), NULL);
    LineTo(dc, S(972), S(690));
    SelectObject(dc, oldPen);
    DeleteObject(footerLine);

    EndPaint(hWnd, &ps);
}
void MarkDirty() {
    if (g_loading) return;
    g_dirty = true;
    SetStatus(L"有未保存的更改", true);
}

void EnableClearButton(bool enabled);

int SelectedProfileId() {
    if (!g_hProfileList) return 0;
    int item = ListView_GetNextItem(g_hProfileList, -1, LVNI_SELECTED);
    if (item < 0) return 0;
    LVITEMW lv = {0};
    lv.mask = LVIF_PARAM;
    lv.iItem = item;
    if (!ListView_GetItem(g_hProfileList, &lv)) return 0;
    return (int)lv.lParam;
}

void FindPollingSelection(int hz) {
    HWND combo = GetDlgItem(g_hwnd, IDC_POLLING_RATE);
    int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        if ((int)SendMessageW(combo, CB_GETITEMDATA, i, 0) == hz) {
            SendMessageW(combo, CB_SETCURSEL, i, 0);
            return;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void LoadEditor(int profileId) {
    Profiles::Definition profile;
    if (!Profiles::Find(profileId, profile)) return;

    g_loading = true;
    g_selectedId = profileId;
    SetText(GetDlgItem(g_hwnd, IDC_PROFILE_NAME), profile.name.c_str());
    FindPollingSelection(profile.pollingHz);

    WCHAR sleepText[32] = {0};
    StringCchPrintfW(sleepText, ARRAYSIZE(sleepText), L"%d", profile.sleepMinutes);
    SetText(GetDlgItem(g_hwnd, IDC_SLEEP_TIME), sleepText);

    int dpiX = profile.dpiX;
    int dpiY = profile.dpiY;
    if (dpiX <= 0 || dpiY <= 0) GetCurrentDpiDefaults(dpiX, dpiY);
    WCHAR dpiXText[32] = {0};
    WCHAR dpiYText[32] = {0};
    StringCchPrintfW(dpiXText, ARRAYSIZE(dpiXText), L"%d", dpiX);
    StringCchPrintfW(dpiYText, ARRAYSIZE(dpiYText), L"%d", dpiY);
    SetText(GetDlgItem(g_hwnd, IDC_DPI_X), dpiXText);
    SetText(GetDlgItem(g_hwnd, IDC_DPI_Y), dpiYText);

    SetText(GetDlgItem(g_hwnd, IDC_APP_PATH), profile.appPath.c_str());
    EnableClearButton(!profile.appPath.empty());
    SetOwnedChecked(IDC_DEFAULT_PROFILE, profileId == Profiles::DefaultId());
    g_dirty = false;
    g_loading = false;
}

void RefreshList(int selectId) {
    if (!g_hProfileList) return;
    g_refreshingList = true;
    SendMessageW(g_hProfileList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hProfileList);

    const auto profiles = Profiles::GetAll();
    int selectedRow = -1;
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& profile = profiles[i];
        std::wstring status;
        if (profile.id == Profiles::ActiveId()) status = L"当前";
        else if (profile.id == Profiles::DefaultId()) status = L"默认";

        LVITEMW lv = {0};
        lv.mask = LVIF_TEXT | LVIF_PARAM;
        lv.iItem = (int)i;
        lv.pszText = (LPWSTR)profile.name.c_str();
        lv.lParam = (LPARAM)profile.id;
        int row = ListView_InsertItem(g_hProfileList, &lv);

        WCHAR rate[32] = {0};
        StringCchPrintfW(rate, ARRAYSIZE(rate), L"%d", profile.pollingHz);
        ListView_SetItemText(g_hProfileList, row, 1, rate);

        std::wstring dpi = FormatDpiValue(profile);
        ListView_SetItemText(g_hProfileList, row, 2, (LPWSTR)dpi.c_str());

        std::wstring app = profile.appName.empty() ? L"—" : profile.appName;
        ListView_SetItemText(g_hProfileList, row, 3, (LPWSTR)app.c_str());
        ListView_SetItemText(g_hProfileList, row, 4, (LPWSTR)status.c_str());

        if (profile.id == selectId) selectedRow = row;
    }

    if (selectedRow >= 0) {
        ListView_SetItemState(g_hProfileList, selectedRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_hProfileList, selectedRow, FALSE);
    }
    SendMessageW(g_hProfileList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hProfileList, NULL, TRUE);
    g_refreshingList = false;
}

bool CommitEditor(bool showErrors) {
    if (!g_dirty) return true;
    Profiles::Definition profile;
    if (g_selectedId == 0 || !Profiles::Find(g_selectedId, profile)) return false;

    std::wstring name = GetText(GetDlgItem(g_hwnd, IDC_PROFILE_NAME));
    if (name.empty()) {
        if (showErrors) MessageBoxW(g_hwnd, L"配置名称不能为空。", L"无法保存", MB_OK | MB_ICONWARNING);
        return false;
    }

    HWND pollCombo = GetDlgItem(g_hwnd, IDC_POLLING_RATE);
    int pollIndex = (int)SendMessageW(pollCombo, CB_GETCURSEL, 0, 0);
    if (pollIndex == CB_ERR) pollIndex = 0;
    int polling = (int)SendMessageW(pollCombo, CB_GETITEMDATA, pollIndex, 0);
    profile.pollingHz = Profiles::NormalizePollingRate(polling);

    BOOL translated = FALSE;
    UINT sleepMinutes = GetDlgItemInt(g_hwnd, IDC_SLEEP_TIME, &translated, FALSE);
    if (!translated) sleepMinutes = 10;
    sleepMinutes = (UINT)Rapoo::ClampSleepMinutes((int)sleepMinutes);
    profile.sleepMinutes = (int)sleepMinutes;

    BOOL dpiTranslatedX = FALSE;
    BOOL dpiTranslatedY = FALSE;
    UINT dpiX = GetDlgItemInt(g_hwnd, IDC_DPI_X, &dpiTranslatedX, FALSE);
    UINT dpiY = GetDlgItemInt(g_hwnd, IDC_DPI_Y, &dpiTranslatedY, FALSE);
    if (!dpiTranslatedX || !dpiTranslatedY || dpiX == 0 || dpiY == 0 || dpiX > 65535 || dpiY > 65535) {
        if (showErrors) {
            MessageBoxW(g_hwnd, L"DPI 必须是 1–65535 之间的数值。", L"无法保存", MB_OK | MB_ICONWARNING);
        }
        return false;
    }
    profile.dpiX = (int)dpiX;
    profile.dpiY = (int)dpiY;

    profile.name = name;
    profile.appPath = GetText(GetDlgItem(g_hwnd, IDC_APP_PATH));
    if (!Profiles::Update(profile)) {
        if (showErrors) {
            MessageBoxW(
                g_hwnd,
                L"配置保存失败。名称不能为空，也不能与其他配置重名。",
                L"无法保存",
                MB_OK | MB_ICONWARNING
            );
        }
        return false;
    }

    bool keptDefault = false;
    if (GetOwnedChecked(IDC_DEFAULT_PROFILE)) {
        Profiles::SetDefault(profile.id);
    } else if (profile.id == Profiles::DefaultId()) {
        // Exactly one profile must always remain the fallback profile.
        SetOwnedChecked(IDC_DEFAULT_PROFILE, true);
        keptDefault = true;
    }
    Profiles::SetAutoSwitchEnabled(GetOwnedChecked(IDC_AUTO_SWITCH));
    Osd::SetEnabled(GetOwnedChecked(IDC_OSD_ENABLED));

    g_dirty = false;
    RefreshList(profile.id);
    LoadEditor(profile.id);
    if (keptDefault) {
        SetStatus(L"已保存；请在另一个配置中勾选“设为默认”来更改回退项", true);
    } else {
        SetStatus(L"配置已保存并应用", true);
    }
    return true;
}

void EnableClearButton(bool enabled) {
    HWND clear = GetDlgItem(g_hwnd, IDC_CLEAR_APP);
    if (clear) EnableWindow(clear, enabled ? TRUE : FALSE);
}

void AddProfile() {
    if (!CommitEditor(true)) return;
    std::wstring name = Profiles::MakeUniqueName();
    int dpiX = 800;
    int dpiY = 800;
    GetCurrentDpiDefaults(dpiX, dpiY);
    int id = Profiles::Create(name, 1000, 10, dpiX, dpiY);
    if (id == 0) {
        MessageBoxW(g_hwnd, L"无法新增配置，最多可创建 16 个配置。", L"新增失败", MB_OK | MB_ICONWARNING);
        return;
    }
    RefreshList(id);
    LoadEditor(id);
    SetStatus(L"已新增配置，请修改名称和参数", true);
    HWND nameEdit = GetDlgItem(g_hwnd, IDC_PROFILE_NAME);
    SetFocus(nameEdit);
    SendMessageW(nameEdit, EM_SETSEL, 0, -1);
}

void DeleteProfile() {
    if (!CommitEditor(true)) return;
    Profiles::Definition profile;
    if (g_selectedId == 0 || !Profiles::Find(g_selectedId, profile)) return;

    std::wstring prompt = L"确定删除配置“" + profile.name + L"”吗？";
    if (MessageBoxW(g_hwnd, prompt.c_str(), L"删除配置", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!Profiles::Delete(g_selectedId)) {
        MessageBoxW(g_hwnd, L"至少需要保留一个配置。", L"无法删除", MB_OK | MB_ICONWARNING);
        return;
    }

    int nextId = Profiles::ActiveId();
    Profiles::Definition next;
    if (!Profiles::Find(nextId, next)) nextId = Profiles::DefaultId();
    RefreshList(nextId);
    LoadEditor(nextId);
    SetStatus(L"配置已删除", true);
}

void CaptureCurrentDpi() {
    const Device::State state = Device::GetCurrentState();
    if (!state.isConnected || !Device::IsDpiConfigValid() || state.dpiX <= 0) {
        SetStatus(L"暂未读取到鼠标 DPI，请唤醒设备后重试", false);
        return;
    }

    BatteryEstimate::DpiSpec spec;
    BatteryEstimate::GetDpiSpecForState(state, spec);
    int dpiX = std::clamp(state.dpiX, spec.minDpi, spec.maxDpi);
    int dpiY = std::clamp(state.dpiY > 0 ? state.dpiY : state.dpiX, spec.minDpi, spec.maxDpi);
    WCHAR textX[32] = {0};
    WCHAR textY[32] = {0};
    StringCchPrintfW(textX, ARRAYSIZE(textX), L"%d", dpiX);
    StringCchPrintfW(textY, ARRAYSIZE(textY), L"%d", dpiY);
    SetText(GetDlgItem(g_hwnd, IDC_DPI_X), textX);
    SetText(GetDlgItem(g_hwnd, IDC_DPI_Y), textY);
    MarkDirty();
    SetStatus(L"已读取鼠标当前 DPI，保存后写入此档案", true);
}

void BrowseApp() {
    WCHAR path[32768] = {0};
    std::wstring current = GetText(GetDlgItem(g_hwnd, IDC_APP_PATH));
    if (!current.empty()) {
        StringCchCopyW(path, ARRAYSIZE(path), current.c_str());
    }

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"可执行程序 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = ARRAYSIZE(path);
    ofn.lpstrTitle = L"选择需要自动切换配置的程序";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        SetText(GetDlgItem(g_hwnd, IDC_APP_PATH), path);
        EnableClearButton(true);
        MarkDirty();
    }
}

void ClearApp() {
    SetText(GetDlgItem(g_hwnd, IDC_APP_PATH), L"");
    EnableClearButton(false);
    MarkDirty();
}

void CreateControls() {
    g_hProfilePageControls.clear();
    g_automationPageControls.clear();
    g_hTitle = NULL;
    g_hSubtitle = NULL;

    static const WCHAR* tabLabels[] = { L"配置档案", L"自动化", L"DPI 档位" };
    static const int tabX[] = { 28, 190, 314 };
    static const int tabW[] = { 154, 116, 116 };
    for (int i = 0; i < 3; ++i) {
        g_hTabButtons[i] = CreateButton(tabLabels[i], tabX[i], 94, tabW[i], 36,
            IDC_TAB_PROFILES + i);
        if (g_hTabButtons[i]) {
            SendMessageW(g_hTabButtons[i], WM_SETFONT,
                (WPARAM)(i == 0 ? g_fontBold : g_fontNormal), TRUE);
        }
    }

    g_hProfileList = CreateControl(
        WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
        0, 46, 232, 354, 314, IDC_PROFILE_LIST);
    ListView_SetExtendedListViewStyle(g_hProfileList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = (LPWSTR)L"名称";
    col.cx = S(100);
    ListView_InsertColumn(g_hProfileList, 0, &col);
    col.pszText = (LPWSTR)L"Hz";
    col.cx = S(56);
    ListView_InsertColumn(g_hProfileList, 1, &col);
    col.pszText = (LPWSTR)L"DPI";
    col.cx = S(60);
    ListView_InsertColumn(g_hProfileList, 2, &col);
    col.pszText = (LPWSTR)L"程序";
    col.cx = S(76);
    ListView_InsertColumn(g_hProfileList, 3, &col);
    col.pszText = (LPWSTR)L"状态";
    col.cx = S(42);
    ListView_InsertColumn(g_hProfileList, 4, &col);

    g_profileRowImageList = ImageList_Create(1, S(30), ILC_COLOR32, 1, 0);
    if (g_profileRowImageList) {
        HDC screen = GetDC(g_hwnd);
        HBITMAP rowBitmap = screen ? CreateCompatibleBitmap(screen, 1, S(30)) : NULL;
        if (screen) ReleaseDC(g_hwnd, screen);
        if (rowBitmap) {
            ImageList_Add(g_profileRowImageList, rowBitmap, NULL);
            DeleteObject(rowBitmap);
        }
        ListView_SetImageList(g_hProfileList, g_profileRowImageList, LVSIL_SMALL);
    }

    TrackPageControl(g_hProfilePageControls, g_hProfileList);
    TrackPageControl(g_hProfilePageControls, CreateButton(L"新增配置", 44, 562, 100, 32, IDC_ADD_PROFILE));
    TrackPageControl(g_hProfilePageControls, CreateButton(L"删除配置", 152, 562, 100, 32, IDC_DELETE_PROFILE));

    TrackPageControl(g_hProfilePageControls,
        CreateControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0, 468, 224, 472, 34, IDC_PROFILE_NAME));

    HWND pollCombo = CreateControl(
        L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 468, 300, 220, 170, IDC_POLLING_RATE);
    const int rates[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
    for (int rate : rates) {
        WCHAR label[32] = {0};
        StringCchPrintfW(label, ARRAYSIZE(label), L"%d Hz", rate);
        int index = (int)SendMessageW(pollCombo, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(pollCombo, CB_SETITEMDATA, index, rate);
    }
    SendMessageW(pollCombo, CB_SETITEMHEIGHT, (WPARAM)-1, S(26));
    TrackPageControl(g_hProfilePageControls, pollCombo);

    HWND sleepCombo = CreateControl(
        L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL | ES_NUMBER,
        0, 722, 300, 218, 170, IDC_SLEEP_TIME);
    const int sleeps[] = { 2, 5, 10, 15, 30, 60, 90, 120 };
    for (int minutes : sleeps) {
        WCHAR label[32] = {0};
        StringCchPrintfW(label, ARRAYSIZE(label), L"%d", minutes);
        SendMessageW(sleepCombo, CB_ADDSTRING, 0, (LPARAM)label);
    }
    SendMessageW(sleepCombo, CB_SETITEMHEIGHT, (WPARAM)-1, S(26));
    TrackPageControl(g_hProfilePageControls, sleepCombo);

    TrackPageControl(g_hProfilePageControls,
        CreateCheck(L"设为默认配置（离开关联程序后恢复）", 460, 338, 488, 28, IDC_DEFAULT_PROFILE));

    TrackPageControl(g_hProfilePageControls,
        CreateControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 0, 514, 440, 124, 34, IDC_DPI_X));
    TrackPageControl(g_hProfilePageControls,
        CreateControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 0, 710, 440, 122, 34, IDC_DPI_Y));
    TrackPageControl(g_hProfilePageControls,
        CreateButton(L"读取当前", 852, 438, 96, 38, IDC_CAPTURE_DPI));
    TrackPageControl(g_hProfilePageControls,
        CreateButton(L"打开 DPI 档位", 460, 482, 488, 36, IDC_OPEN_DPI));

    HWND appPath = CreateControl(
        L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY,
        0, 52, 222, 766, 26, IDC_APP_PATH);
    TrackPageControl(g_automationPageControls, appPath);
    TrackPageControl(g_automationPageControls, CreateButton(L"浏览…", 836, 214, 64, 36, IDC_BROWSE_APP));
    TrackPageControl(g_automationPageControls, CreateButton(L"清除", 908, 214, 48, 36, IDC_CLEAR_APP));

    TrackPageControl(g_automationPageControls,
        CreateCheck(L"启用前台程序自动切换", 44, 386, 420, 32, IDC_AUTO_SWITCH));
    TrackPageControl(g_automationPageControls,
        CreateCheck(L"显示悬浮窗提示 (OSD)", 44, 434, 420, 32, IDC_OSD_ENABLED));

    TrackPageControl(g_automationPageControls,
        CreateCheck(L"满格射频（不勾选 = 智能调节）", 512, 388, 430, 32, IDC_RF_FULL));
    g_hModelCombo = CreateControl(
        L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 520, 490, 406, 170, IDC_MODEL_SELECT);
    if (g_hModelCombo) {
        const auto& models = BatteryEstimate::Models();
        for (size_t i = 0; i < models.size(); ++i) {
            int index = (int)SendMessageW(g_hModelCombo, CB_ADDSTRING, 0, (LPARAM)models[i].label.c_str());
            SendMessageW(g_hModelCombo, CB_SETITEMDATA, index, i);
        }
        SendMessageW(g_hModelCombo, CB_SETITEMHEIGHT, (WPARAM)-1, S(26));
        SelectSavedModel();
    }
    TrackPageControl(g_automationPageControls, g_hModelCombo);

    g_hStatus = CreateText(L"", 28, 704, 700, 28, IDC_STATUS);
    HWND save = CreateButton(L"保存并应用", 742, 702, 128, 38, IDC_SAVE);
    CreateButton(L"关闭", 884, 702, 88, 38, IDC_CLOSE);

    ApplyFonts();
    if (g_hStatus) SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    if (save) SendMessageW(save, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

    SetOwnedChecked(IDC_AUTO_SWITCH, Profiles::AutoSwitchEnabled());
    SetOwnedChecked(IDC_OSD_ENABLED, Osd::IsEnabled());
    EnableClearButton(false);
    ShowPageControls();
}
void OnProfileSelectionChanged() {
    if (g_refreshingList) return;
    int id = SelectedProfileId();
    if (id == 0 || id == g_selectedId) return;

    int previousId = g_selectedId;
    if (!CommitEditor(true)) {
        RefreshList(previousId);
        LoadEditor(previousId);
        return;
    }
    LoadEditor(id);
    SetStatus(L"选择配置后可直接修改；点击“保存并应用”生效", true);
}

LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hWnd;
            g_dark = Tray::IsDarkMode();
            RebuildBrushes();
            g_fontNormal = MakeFont(13, FW_NORMAL);
            g_fontBold = MakeFont(13, FW_SEMIBOLD);
            g_fontTitle = MakeFont(22, FW_BOLD);
            g_fontSmall = MakeFont(11, FW_NORMAL);
            g_activeTab = SettingsTab::Profiles;
            CreateControls();
            UpdateTheme();
            DragAcceptFiles(hWnd, TRUE);

            RefreshList(Profiles::ActiveId());
            LoadEditor(Profiles::ActiveId());
            RefreshDeviceState();
            SetTimer(hWnd, TIMER_ADVANCED_INIT, 150, NULL);
            SetTimer(hWnd, TIMER_ADVANCED_REFRESH, 1000, NULL);
            SetStatus(L"选择配置进行编辑，或新增一个配置", true);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintSettingsWindow(hWnd);
            return 0;

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = (DRAWITEMSTRUCT*)lParam;
            if (item && item->CtlType == ODT_BUTTON) {
                if (item->CtlID >= IDC_TAB_PROFILES && item->CtlID <= IDC_TAB_DPI) {
                    DrawTabButton(item);
                } else {
                    DrawOwnerButton(item);
                }
                return TRUE;
            }
            break;
        }

        case WM_SETTINGCHANGE:
            UpdateTheme();
            return 0;

        case WM_TIMER: {
            if (wParam == TIMER_ADVANCED_INIT) {
                KillTimer(hWnd, TIMER_ADVANCED_INIT);
                if (Device::IsStarted()) Device::RefreshRfState();
                RefreshDeviceState();
                return 0;
            }
            if (wParam == TIMER_ADVANCED_REFRESH) {
                RefreshDeviceState();
                return 0;
            }
            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            if (notification == BN_CLICKED && id >= IDC_TAB_PROFILES && id <= IDC_TAB_DPI) {
                SwitchTab((SettingsTab)(id - IDC_TAB_PROFILES));
                return 0;
            }

            if (notification == BN_CLICKED && IsCheckControlId(id)) {
                ToggleOwnedCheck(id);
                if (id == IDC_AUTO_SWITCH) {
                    Profiles::SetAutoSwitchEnabled(GetOwnedChecked(IDC_AUTO_SWITCH));
                    SetStatus(L"自动切换设置已更新", true);
                } else if (id == IDC_OSD_ENABLED) {
                    Osd::SetEnabled(GetOwnedChecked(IDC_OSD_ENABLED));
                    SetStatus(L"悬浮窗设置已更新", true);
                } else if (id == IDC_RF_FULL) {
                    bool full = GetOwnedChecked(IDC_RF_FULL);
                    if (!Device::SetRfFull(full)) {
                        ToggleOwnedCheck(IDC_RF_FULL);
                        SetStatus(L"射频策略写入失败，请唤醒鼠标后重试", false);
                    } else {
                        RefreshDeviceState();
                        SetStatus(full ? L"已切换为满格射频" : L"已切换为智能调节", true);
                    }
                } else {
                    MarkDirty();
                }
                return 0;
            }

            if (id == IDC_MODEL_SELECT && notification == CBN_SELCHANGE) {
                int index = (int)SendMessageW(g_hModelCombo, CB_GETCURSEL, 0, 0);
                const auto& models = BatteryEstimate::Models();
                if (index >= 0 && index < (int)models.size()) {
                    BatteryEstimate::SaveOverride(models[index].key);
                    RefreshDeviceState();
                    InvalidateRect(hWnd, NULL, FALSE);
                    SetStatus(L"续航估算型号已更新", true);
                }
                return 0;
            }

            if (id == IDC_OPEN_DPI && notification == BN_CLICKED) {
                SwitchTab(SettingsTab::Dpi);
                return 0;
            }
            if (id == IDC_CAPTURE_DPI && notification == BN_CLICKED) {
                CaptureCurrentDpi();
                return 0;
            }
            if (id == IDC_ADD_PROFILE && notification == BN_CLICKED) {
                AddProfile();
                return 0;
            }
            if (id == IDC_DELETE_PROFILE && notification == BN_CLICKED) {
                DeleteProfile();
                return 0;
            }
            if (id == IDC_BROWSE_APP && notification == BN_CLICKED) {
                BrowseApp();
                return 0;
            }
            if (id == IDC_CLEAR_APP && notification == BN_CLICKED) {
                ClearApp();
                return 0;
            }
            if (id == IDC_SAVE && notification == BN_CLICKED) {
                g_dirty = true;
                CommitEditor(true);
                return 0;
            }
            if ((id == IDC_CLOSE && notification == BN_CLICKED) || id == IDCANCEL) {
                SendMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if ((id == IDC_PROFILE_NAME && notification == EN_CHANGE) ||
                (id == IDC_POLLING_RATE && notification == CBN_SELCHANGE) ||
                (id == IDC_SLEEP_TIME && notification == CBN_EDITCHANGE) ||
                (id == IDC_SLEEP_TIME && notification == CBN_SELCHANGE) ||
                (id == IDC_DPI_X && notification == EN_CHANGE) ||
                (id == IDC_DPI_Y && notification == EN_CHANGE)) {
                MarkDirty();
                return 0;
            }
            break;
        }

        case WM_NOTIFY: {
            LPNMHDR header = (LPNMHDR)lParam;
            if (header && header->idFrom == IDC_PROFILE_LIST && header->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW draw = (LPNMLVCUSTOMDRAW)lParam;
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
                    draw->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
                    draw->clrTextBk = selected ? ColorAccentFill() : ColorField();
                    draw->clrText = selected ? ColorAccent() : ColorText();
                    return CDRF_NEWFONT;
                }
            }
            if (header && header->idFrom == IDC_PROFILE_LIST && header->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW change = (LPNMLISTVIEW)lParam;
                if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED)) {
                    OnProfileSelectionChanged();
                }
            }
            break;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wParam;
            WCHAR path[32768] = {0};
            if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) > 0) {
                SetText(GetDlgItem(hWnd, IDC_APP_PATH), path);
                EnableClearButton(true);
                MarkDirty();
            }
            DragFinish(drop);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            HWND control = (HWND)lParam;
            int id = control ? GetDlgCtrlID(control) : 0;
            SetBkMode(dc, TRANSPARENT);
            if (id == IDC_APP_PATH) {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, ColorField());
                SetTextColor(dc, ColorText());
                return (LRESULT)(g_brushField ? g_brushField : GetStockObject(WHITE_BRUSH));
            }
            if (control == g_hSubtitle) {
                SetTextColor(dc, ColorMuted());
            } else if (control == g_hStatus && !g_statusGood) {
                SetTextColor(dc, ColorDanger());
            } else if (control == g_hStatus) {
                SetTextColor(dc, ColorAccent());
            } else {
                SetTextColor(dc, ColorText());
            }
            bool onCard = control != g_hTitle && control != g_hSubtitle && control != g_hStatus;
            HBRUSH background = onCard ? g_brushSurface : g_brushBg;
            return (LRESULT)(background ? background : GetStockObject(WHITE_BRUSH));
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, ColorText());
            SetBkColor(dc, ColorField());
            return (LRESULT)(g_brushField ? g_brushField : GetStockObject(WHITE_BRUSH));
        }

        case WM_CTLCOLORBTN: {
            return (LRESULT)(g_brushSurface ? g_brushSurface : GetStockObject(WHITE_BRUSH));
        }

        case WM_CLOSE: {
            if (!CommitEditor(true)) return 0;
            if (DpiWindow::HasUnsavedChanges()) {
                int result = MessageBoxW(hWnd, L"DPI 设置尚未应用，是否立即应用？",
                    L"DPI 设置", MB_YESNOCANCEL | MB_ICONQUESTION);
                if (result == IDCANCEL) return 0;
                if (result == IDYES && !DpiWindow::ApplyPendingChanges()) return 0;
            }
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_DESTROY: {
            KillTimer(hWnd, TIMER_ADVANCED_INIT);
            KillTimer(hWnd, TIMER_ADVANCED_REFRESH);
            DpiWindow::Cleanup();
            if (g_hProfileList) ListView_SetImageList(g_hProfileList, NULL, LVSIL_SMALL);
            if (g_profileRowImageList) ImageList_Destroy(g_profileRowImageList);
            g_profileRowImageList = NULL;
            if (g_brushBg) DeleteObject(g_brushBg);
            if (g_brushSurface) DeleteObject(g_brushSurface);
            if (g_brushField) DeleteObject(g_brushField);
            g_brushBg = g_brushSurface = g_brushField = NULL;
            if (g_fontNormal) DeleteObject(g_fontNormal);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            g_fontNormal = g_fontBold = g_fontTitle = g_fontSmall = NULL;
            g_hwnd = NULL;
            g_hProfileList = NULL;
            g_hModelCombo = NULL;
            g_hDpiPage = NULL;
            g_hProfilePageControls.clear();
            g_automationPageControls.clear();
            g_activeTab = SettingsTab::Profiles;
            g_lastAutoModelName[0] = 0;
            g_hTitle = g_hSubtitle = g_hStatus = NULL;
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

void Show(HWND owner) {
    Profiles::Initialize();
    if (g_hwnd && IsWindow(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwnd);
        return;
    }

    g_hOwner = owner;
    g_dpi = (int)GetSystemDpiSafe();
    INITCOMMONCONTROLSEX common = { sizeof(common), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&common);

    if (!g_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = (HINSTANCE)GetModuleHandleW(NULL);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = WINDOW_CLASS;
        if (!RegisterClassExW(&wc)) return;
        g_classRegistered = true;
    }

    RECT work = {0};
    if (owner && IsWindow(owner)) {
        HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(monitor, &mi)) work = mi.rcWork;
    }
    if (IsRectEmpty(&work)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    // Keep the fixed layout usable on 768p / high-DPI laptops.
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    int maxDpiByWidth = MulDiv(96, std::max(320, workWidth - 64), WINDOW_WIDTH);
    int maxDpiByHeight = MulDiv(96, std::max(240, workHeight - 64), WINDOW_HEIGHT);
    g_dpi = std::max(1, std::min(g_dpi, std::min(maxDpiByWidth, maxDpiByHeight)));

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_APPWINDOW;
    RECT rc = { 0, 0, S(WINDOW_WIDTH), S(WINDOW_HEIGHT) };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;

    HWND hwnd = CreateWindowExW(
        exStyle,
        WINDOW_CLASS,
        L"配置中心 - rapoo-tray",
        style,
        x, y, width, height,
        owner, NULL, (HINSTANCE)GetModuleHandleW(NULL), NULL
    );
    if (hwnd) {
        g_hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(GetDlgItem(hwnd, IDC_PROFILE_NAME));
    }
}

bool PreTranslateMessage(MSG* msg) {
    if (!g_hwnd || !IsWindow(g_hwnd) || !msg) return false;
    if (msg->hwnd == g_hwnd || IsChild(g_hwnd, msg->hwnd)) {
        if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN) {
            HWND focused = GetFocus();
            int focusedId = focused ? GetDlgCtrlID(focused) : 0;
            if (focusedId == IDC_POLLING_RATE || focusedId == IDC_SLEEP_TIME) {
                if (SendMessageW(focused, CB_GETDROPPEDSTATE, 0, 0)) {
                    return IsDialogMessageW(g_hwnd, msg) != FALSE;
                }
            }
            WCHAR className[32] = {0};
            if (focused) GetClassNameW(focused, className, ARRAYSIZE(className));
            if (wcscmp(className, L"Button") == 0) {
                SendMessageW(focused, BM_CLICK, 0, 0);
            } else {
                SendMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_SAVE, BN_CLICKED), 0);
            }
            return true;
        }
        return IsDialogMessageW(g_hwnd, msg) != FALSE;
    }
    return false;
}

void Cleanup() {
    if (g_hwnd && IsWindow(g_hwnd)) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
}

} // namespace Settings

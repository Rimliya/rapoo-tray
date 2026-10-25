#include "dpi_window.h"
#include "battery_estimator.h"
#include "device_manager.h"
#include "tray_menu.h"

#include <commctrl.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace DpiWindow {
namespace {

constexpr const WCHAR* WINDOW_CLASS = L"RapooDpiEditorWnd";
constexpr int WINDOW_WIDTH = 820;
constexpr int WINDOW_HEIGHT = 520;

enum ControlId {
    IDC_DPI_EDIT = 5001,
    IDC_ADD_GEAR,
    IDC_DELETE_GEAR,
    IDC_MINUS,
    IDC_PLUS,
    IDC_APPLY,
    IDC_CLOSE
};

HWND g_hwnd = NULL;
HWND g_hOwner = NULL;
HWND g_hEdit = NULL;
HFONT g_fontTitle = NULL;
HFONT g_fontBig = NULL;
HFONT g_fontBold = NULL;
HFONT g_fontNormal = NULL;
HFONT g_fontSmall = NULL;
HBRUSH g_brushBg = NULL;
HBRUSH g_brushSurface = NULL;
HBRUSH g_brushField = NULL;
bool g_dark = false;
bool g_applyingTheme = false;
bool g_classRegistered = false;
bool g_updatingEdit = false;
bool g_dirty = false;
bool g_draggingSlider = false;
bool g_connected = false;
bool g_readFromDevice = false;
int g_dpi = 96;
int g_hoverGear = -1;
int g_selectedIndex = 0;
int g_activeUiIndex = 0;
Device::DpiConfig g_config;
std::vector<int> g_hardwareSlotMap;
BatteryEstimate::DpiSpec g_spec;
std::wstring g_modelKey;
std::wstring g_modelLabel;
std::wstring g_statusText;
std::wstring g_lastSyncedEdit;

int S(int value) { return MulDiv(value, g_dpi, 96); }

COLORREF ColorBg() { return g_dark ? RGB(24, 26, 32) : RGB(255, 255, 255); }
COLORREF ColorSurface() { return g_dark ? RGB(30, 33, 42) : RGB(247, 249, 252); }
COLORREF ColorField() { return g_dark ? RGB(37, 41, 51) : RGB(242, 245, 250); }
COLORREF ColorBorder() { return g_dark ? RGB(50, 54, 65) : RGB(205, 212, 222); }
COLORREF ColorHover() { return g_dark ? RGB(52, 58, 72) : RGB(232, 238, 248); }
COLORREF ColorText() { return g_dark ? RGB(235, 240, 248) : RGB(20, 24, 32); }
COLORREF ColorMuted() { return g_dark ? RGB(140, 150, 165) : RGB(105, 115, 130); }
COLORREF ColorAccent() { return g_dark ? RGB(96, 205, 255) : RGB(0, 110, 215); }
COLORREF ColorButton() { return g_dark ? RGB(38, 42, 52) : RGB(240, 243, 248); }

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
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

void RebuildBrushes() {
    if (g_brushBg) DeleteObject(g_brushBg);
    if (g_brushSurface) DeleteObject(g_brushSurface);
    if (g_brushField) DeleteObject(g_brushField);
    g_brushBg = CreateSolidBrush(ColorBg());
    g_brushSurface = CreateSolidBrush(ColorSurface());
    g_brushField = CreateSolidBrush(ColorField());
}

void SetStatus(const WCHAR* text) {
    g_statusText = text ? text : L"";
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
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
    HGDIOBJ oldFont = SelectObject(dc, g_fontBold ? g_fontBold : GetStockObject(DEFAULT_GUI_FONT));
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

void DrawLabel(HDC dc, const WCHAR* text, int x, int y, int w, int h, HFONT font, COLORREF color, DWORD flags = DT_LEFT) {
    HGDIOBJ oldFont = SelectObject(dc, font ? font : g_fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT rect = { S(x), S(y), S(x + w), S(y + h) };
    DrawTextW(dc, text, -1, &rect, flags | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}void DrawOwnerButton(const DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool focused = (item->itemState & ODS_FOCUS) != 0;
    POINT cursor = {0};
    GetCursorPos(&cursor);
    bool hovered = WindowFromPoint(cursor) == item->hwndItem;
    WCHAR text[128] = {0};
    GetWindowTextW(item->hwndItem, text, ARRAYSIZE(text));
    HDC dc = item->hDC;
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, g_fontNormal);
    COLORREF fill = (pressed || hovered) ? ColorHover() : ColorButton();
    COLORREF border = pressed ? ColorAccent() : ColorBorder();
    COLORREF textColor = ColorText();
    if (item->CtlID == IDC_APPLY) {
        fill = ColorAccent();
        textColor = g_dark ? RGB(10, 14, 20) : RGB(255, 255, 255);
    }
    if (disabled) {
        fill = ColorSurface();
        textColor = ColorMuted();
    }
    FillRounded(dc, rc, fill, border, S(7));
    SetTextColor(dc, textColor);
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (focused && !disabled) DrawFocusRect(dc, &rc);
    SelectObject(dc, oldFont);
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

void ApplyNativeTheme() {
    if (!g_hwnd) return;
    const WCHAR* theme = g_dark ? L"DarkMode_Explorer" : L"Explorer";
    const WCHAR* editTheme = g_dark ? L"DarkMode_CFD" : L"Explorer";
    SetWindowTheme(g_hwnd, theme, NULL);
    if (g_hEdit) SetWindowTheme(g_hEdit, editTheme, NULL);
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
    RedrawWindow(g_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    g_applyingTheme = false;
}

std::wstring CacheSubkey() {
    return L"Software\\rapoo-tray\\DpiProfiles\\" + (g_modelKey.empty() ? L"generic" : g_modelKey);
}

bool LoadCache(Device::DpiConfig& config) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CacheSubkey().c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD count = 0, index = 0, type = 0, size = sizeof(DWORD);
    bool ok = RegQueryValueExW(hKey, L"Count", NULL, &type, (LPBYTE)&count, &size) == ERROR_SUCCESS && type == REG_DWORD;
    size = sizeof(DWORD);
    ok = ok && RegQueryValueExW(hKey, L"Index", NULL, &type, (LPBYTE)&index, &size) == ERROR_SUCCESS && type == REG_DWORD;
    if (!ok || count < 1 || count > (DWORD)g_spec.maxSlots) {
        RegCloseKey(hKey);
        return false;
    }
    std::vector<int> x(count), y(count);
    size = count * sizeof(int);
    ok = RegQueryValueExW(hKey, L"ValuesX", NULL, &type, (LPBYTE)x.data(), &size) == ERROR_SUCCESS && type == REG_BINARY;
    size = count * sizeof(int);
    ok = ok && RegQueryValueExW(hKey, L"ValuesY", NULL, &type, (LPBYTE)y.data(), &size) == ERROR_SUCCESS && type == REG_BINARY;
    RegCloseKey(hKey);
    if (!ok) return false;
    config.currentIndex = std::clamp((int)index, 0, (int)count - 1);
    config.dpiX = x;
    config.dpiY = y;
    return true;
}

void SaveCache() {
    if (g_config.dpiX.empty()) return;
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CacheSubkey().c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) return;
    DWORD count = (DWORD)g_config.dpiX.size();
    DWORD index = (DWORD)std::clamp(g_config.currentIndex, 0, (int)count - 1);
    RegSetValueExW(hKey, L"Count", 0, REG_DWORD, (const BYTE*)&count, sizeof(count));
    RegSetValueExW(hKey, L"Index", 0, REG_DWORD, (const BYTE*)&index, sizeof(index));
    RegSetValueExW(hKey, L"ValuesX", 0, REG_BINARY, (const BYTE*)g_config.dpiX.data(), count * sizeof(int));
    RegSetValueExW(hKey, L"ValuesY", 0, REG_BINARY, (const BYTE*)g_config.dpiY.data(), count * sizeof(int));
    RegCloseKey(hKey);
}

int SnapDpi(int value) {
    value = std::clamp(value, g_spec.minDpi, g_spec.maxDpi);
    int step = std::max(1, g_spec.step);
    int steps = (value - g_spec.minDpi + step / 2) / step;
    return std::clamp(g_spec.minDpi + steps * step, g_spec.minDpi, g_spec.maxDpi);
}

std::vector<int> DefaultGearValues() {
    const int candidates[] = { 400, 800, 1200, 1600, 2400, 3200, 6400, 12800, 30000 };
    std::vector<int> values;
    for (int v : candidates) {
        int snapped = SnapDpi(v);
        if (std::find(values.begin(), values.end(), snapped) == values.end()) values.push_back(snapped);
        if ((int)values.size() >= g_spec.maxSlots) break;
    }
    if (values.empty()) values.push_back(g_spec.minDpi);
    std::sort(values.begin(), values.end());
    return values;
}

void NormalizeLoaded(Device::DpiConfig config, int hardwareIndex, bool preserveHardwareSlots) {
    struct Gear { int x; int y; int original; };
    std::vector<Gear> gears;
    int count = std::min({ (int)config.dpiX.size(), (int)config.dpiY.size(), g_spec.maxSlots, Device::MAX_DPI_SLOTS });
    for (int i = 0; i < count; ++i) {
        int x = std::clamp(config.dpiX[i], g_spec.minDpi, g_spec.maxDpi);
        int y = std::clamp(config.dpiY[i], g_spec.minDpi, g_spec.maxDpi);
        gears.push_back({ x, y, i });
    }
    std::sort(gears.begin(), gears.end(), [](const Gear& a, const Gear& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    g_config.dpiX.clear();
    g_config.dpiY.clear();
    g_hardwareSlotMap.clear();
    for (const auto& gear : gears) {
        bool duplicate = !g_config.dpiX.empty() && g_config.dpiX.back() == gear.x && g_config.dpiY.back() == gear.y;
        if (duplicate) continue;
        g_config.dpiX.push_back(gear.x);
        g_config.dpiY.push_back(gear.y);
        g_hardwareSlotMap.push_back(gear.original);
    }
    int active = 0;
    if (preserveHardwareSlots) {
        for (int i = 0; i < (int)g_hardwareSlotMap.size(); ++i) {
            if (g_hardwareSlotMap[i] == hardwareIndex) { active = i; break; }
        }
    } else {
        active = std::clamp(hardwareIndex, 0, (int)g_config.dpiX.size() - 1);
        for (int i = 0; i < (int)g_hardwareSlotMap.size(); ++i) g_hardwareSlotMap[i] = i;
    }
    g_activeUiIndex = active;
    g_selectedIndex = active;
    g_config.currentIndex = active;
}

void SortEditable(int activeX, int activeY, int selectedX) {
    struct Gear { int x; int y; int original; };
    std::vector<Gear> gears;
    for (int i = 0; i < (int)g_config.dpiX.size(); ++i) gears.push_back({ g_config.dpiX[i], g_config.dpiY[i], i });
    std::sort(gears.begin(), gears.end(), [](const Gear& a, const Gear& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    g_config.dpiX.clear();
    g_config.dpiY.clear();
    g_hardwareSlotMap.clear();
    for (const auto& gear : gears) {
        bool duplicate = !g_config.dpiX.empty() && g_config.dpiX.back() == gear.x && g_config.dpiY.back() == gear.y;
        if (duplicate) continue;
        g_config.dpiX.push_back(gear.x);
        g_config.dpiY.push_back(gear.y);
        g_hardwareSlotMap.push_back((int)g_config.dpiX.size() - 1);
    }
    int active = 0;
    for (int i = 0; i < (int)g_config.dpiX.size(); ++i) {
        if (g_config.dpiX[i] == activeX && g_config.dpiY[i] == activeY) { active = i; break; }
    }
    int selected = active;
    for (int i = 0; i < (int)g_config.dpiX.size(); ++i) {
        if (g_config.dpiX[i] == selectedX) { selected = i; break; }
    }
    g_activeUiIndex = active;
    g_selectedIndex = selected;
    g_config.currentIndex = active;
}void SyncEditor() {
    if (!g_hEdit || g_config.dpiX.empty()) return;
    g_selectedIndex = std::clamp(g_selectedIndex, 0, (int)g_config.dpiX.size() - 1);
    g_updatingEdit = true;
    WCHAR value[32] = {0};
    StringCchPrintfW(value, ARRAYSIZE(value), L"%d", g_config.dpiX[g_selectedIndex]);
    SetWindowTextW(g_hEdit, value);
    g_lastSyncedEdit = value;
    g_updatingEdit = false;
}

void LoadConfig() {
    Device::State state;
    if (Device::IsStarted()) state = Device::GetCurrentState();
    BatteryEstimate::GetDpiSpecForState(state, g_spec, &g_modelKey, &g_modelLabel);
    g_connected = state.isConnected && Device::IsStarted();
    g_dirty = false;
    g_readFromDevice = false;
    Device::DpiConfig config;
    if (g_connected && Device::ReadDpiConfig(config)) {
        NormalizeLoaded(config, config.currentIndex, true);
        g_readFromDevice = true;
        SaveCache();
        SetStatus(L"已读取鼠标内置 DPI；未保存前不会写入");
    } else if (LoadCache(config)) {
        NormalizeLoaded(config, config.currentIndex, false);
        SetStatus(g_connected ? L"硬件读取失败，已载入本地缓存" : L"未连接鼠标，当前显示本地缓存");
    } else {
        config.dpiX = DefaultGearValues();
        config.dpiY = config.dpiX;
        config.currentIndex = 0;
        NormalizeLoaded(config, 0, false);
        SetStatus(g_connected ? L"硬件读取失败，当前显示型号预设" : L"未连接鼠标，当前显示型号预设");
    }
    SyncEditor();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
}

int SliderLeft() { return S(300); }
int SliderRight() { return S(776); }
int SliderY() { return S(206); }

double SliderRatioForDpi(int dpi) {
    double minValue = std::max(1, g_spec.minDpi);
    double maxValue = std::max(minValue + 1.0, (double)g_spec.maxDpi);
    double value = std::clamp((double)dpi, minValue, maxValue);
    return (std::log(value) - std::log(minValue)) / (std::log(maxValue) - std::log(minValue));
}

int SliderXForDpi(int dpi) {
    int left = SliderLeft();
    int right = SliderRight();
    return left + (int)std::lround(SliderRatioForDpi(dpi) * (right - left));
}

int DpiForSliderX(int x) {
    int left = SliderLeft();
    int right = SliderRight();
    double t = std::clamp((double)(x - left) / (double)(right - left), 0.0, 1.0);
    double minValue = std::max(1, g_spec.minDpi);
    double maxValue = std::max(minValue + 1.0, (double)g_spec.maxDpi);
    double value = std::exp(std::log(minValue) + t * (std::log(maxValue) - std::log(minValue)));
    return SnapDpi((int)std::lround(value));
}

RECT GearRect(int index) {
    int col = index % 2;
    int row = index / 2;
    int x = 40 + col * 108;
    int y = 158 + row * 44;
    RECT r = { S(x), S(y), S(x + 96), S(y + 34) };
    return r;
}

int HitTestGear(int x, int y) {
    POINT pt = { x, y };
    for (int i = 0; i < (int)g_config.dpiX.size(); ++i) {
        RECT r = GearRect(i);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

void DrawGearPills(HDC dc) {
    HGDIOBJ oldFont = SelectObject(dc, g_fontNormal);
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < (int)g_config.dpiX.size(); ++i) {
        RECT r = GearRect(i);
        bool selected = (i == g_selectedIndex);
        bool active = (i == g_activeUiIndex);
        bool hover = (i == g_hoverGear);
        COLORREF fill = selected ? ColorAccent() : (hover ? ColorHover() : ColorField());
        COLORREF border = (selected || active) ? ColorAccent() : ColorBorder();
        COLORREF textColor = selected ? (g_dark ? RGB(10, 14, 20) : RGB(255, 255, 255)) : ColorText();
        FillRounded(dc, r, fill, border, S(12));
        WCHAR value[32] = {0};
        StringCchPrintfW(value, ARRAYSIZE(value), L"%d", g_config.dpiX[i]);
        SetTextColor(dc, textColor);
        DrawTextW(dc, value, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (active && !selected) {
            SetTextColor(dc, ColorAccent());
            RECT marker = { r.right - S(13), r.top + S(3), r.right - S(4), r.bottom };
            DrawTextW(dc, L"•", -1, &marker, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    SelectObject(dc, oldFont);
}

void DrawSlider(HDC dc) {
    int left = SliderLeft();
    int right = SliderRight();
    int y = SliderY();
    int trackH = S(10);
    RECT track = { left, y - trackH / 2, right, y + trackH / 2 };
    FillRounded(dc, track, ColorField(), ColorBorder(), S(8));
    int thumbX = SliderXForDpi(g_config.dpiX[g_selectedIndex]);
    int segments = 80;
    for (int i = 0; i < segments; ++i) {
        int x0 = left + (right - left) * i / segments;
        int x1 = left + (right - left) * (i + 1) / segments + 1;
        if (x0 > thumbX) break;
        double t = (double)i / segments;
        int r = (int)std::lround(66 + 70 * t);
        int g = (int)std::lround(140 + 85 * t);
        int b = (int)std::lround(255 - 175 * t);
        HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
        RECT part = { x0, track.top, std::min(x1, thumbX), track.bottom };
        FillRect(dc, &part, brush);
        DeleteObject(brush);
    }
    HPEN line = CreatePen(PS_SOLID, S(2), ColorAccent());
    HGDIOBJ oldPen = SelectObject(dc, line);
    MoveToEx(dc, thumbX, y - S(18), NULL);
    LineTo(dc, thumbX, y + S(18));
    SelectObject(dc, oldPen);
    DeleteObject(line);
    int radius = S(8);
    HBRUSH fill = CreateSolidBrush(ColorAccent());
    HPEN border = CreatePen(PS_SOLID, S(2), ColorBg());
    HGDIOBJ oldFill = SelectObject(dc, fill);
    HGDIOBJ oldBorder = SelectObject(dc, border);
    Ellipse(dc, thumbX - radius, y - radius, thumbX + radius + 1, y + radius + 1);
    SelectObject(dc, oldFill);
    SelectObject(dc, oldBorder);
    DeleteObject(fill);
    DeleteObject(border);
    WCHAR selected[32] = {0};
    StringCchPrintfW(selected, ARRAYSIZE(selected), L"%d", g_config.dpiX[g_selectedIndex]);
    RECT labelRect = { thumbX - S(55), y - S(45), thumbX + S(55), y - S(20) };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ColorAccent());
    HGDIOBJ oldFont = SelectObject(dc, g_fontBold);
    DrawTextW(dc, selected, -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i <= 4; ++i) {
        int x = left + (int)std::lround((i / 4.0) * (right - left));
        int dpi = DpiForSliderX(x);
        WCHAR tick[32] = {0};
        StringCchPrintfW(tick, ARRAYSIZE(tick), L"%d", dpi);
        RECT tickRect = { x - S(42), y + S(17), x + S(42), y + S(39) };
        SetTextColor(dc, ColorMuted());
        SelectObject(dc, g_fontSmall);
        DrawTextW(dc, tick, -1, &tickRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, oldFont);
}

void DrawUi(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hWnd, &ps);
    RECT rc = {0};
    GetClientRect(hWnd, &rc);
    HBRUSH background = CreateSolidBrush(ColorBg());
    FillRect(dc, &rc, background);
    DeleteObject(background);
    DrawSection(dc, 24, 84, 240, 350, L"DPI 档位");
    DrawSection(dc, 280, 84, 516, 350, L"DPI 设置");
    if (g_config.dpiX.empty()) {
        EndPaint(hWnd, &ps);
        return;
    }

    WCHAR modelText[128] = {0};
    StringCchPrintfW(modelText, ARRAYSIZE(modelText), L"型号：%s", g_modelLabel.c_str());
    DrawLabel(dc, modelText, 296, 105, 400, 24, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"选择档位后可直接修改；保存会写入鼠标板载存储", 296, 128, 470, 20, g_fontSmall, ColorMuted());
    WCHAR bigValue[32] = {0};
    StringCchPrintfW(bigValue, ARRAYSIZE(bigValue), L"%d DPI", g_config.dpiX[g_selectedIndex]);
    DrawLabel(dc, bigValue, 296, 154, 300, 40, g_fontBig, ColorAccent());
    DrawGearPills(dc);
    DrawSlider(dc);
    WCHAR rangeText[128] = {0};
    StringCchPrintfW(rangeText, ARRAYSIZE(rangeText), L"范围 %d–%d DPI · 步进 %d · 最多 %d 档",
        g_spec.minDpi, g_spec.maxDpi, g_spec.step, g_spec.maxSlots);
    DrawLabel(dc, rangeText, 296, 247, 470, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"精确 DPI", 296, 281, 76, 28, g_fontNormal, ColorText());
    RECT field = { S(378), S(277), S(538), S(309) };
    FillRounded(dc, field, ColorField(), ColorBorder(), S(6));
    WCHAR activeText[128] = {0};
    StringCchPrintfW(activeText, ARRAYSIZE(activeText), L"当前生效：%d DPI（第 %d 档）",
        g_config.dpiX[g_activeUiIndex], g_activeUiIndex + 1);
    DrawLabel(dc, activeText, 296, 326, 470, 22, g_fontSmall, ColorMuted());
    const WCHAR* storageText = g_readFromDevice
        ? L"已从鼠标读取当前生效 DPI；窗口打开和程序启动都不会写入默认值。"
        : L"当前未从鼠标读取到 DPI；请连接设备后再保存。";
    DrawLabel(dc, storageText, 296, 350, 470, 24, g_fontSmall, g_readFromDevice ? ColorMuted() : RGB(235, 170, 95));
    DrawLabel(dc, g_statusText.c_str(), 24, 458, 540, 24, g_fontSmall, ColorMuted());

    HPEN border = CreatePen(PS_SOLID, 1, ColorBorder());
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 0, 0, rc.right, rc.bottom, S(12), S(12));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(border);
    EndPaint(hWnd, &ps);
}void CommitEditorValue(bool markDirty) {
    if (!g_hEdit || g_config.dpiX.empty()) return;
    WCHAR text[32] = {0};
    GetWindowTextW(g_hEdit, text, ARRAYSIZE(text));
    wchar_t* end = NULL;
    long parsed = wcstol(text, &end, 10);
    if (!end || end == text) {
        SyncEditor();
        return;
    }
    int next = SnapDpi((int)parsed);
    if (g_selectedIndex < 0 || g_selectedIndex >= (int)g_config.dpiX.size()) return;
    if (g_config.dpiX[g_selectedIndex] == next && g_config.dpiY[g_selectedIndex] == next) {
        SyncEditor();
        return;
    }
    int activeX = g_config.dpiX[g_activeUiIndex];
    int activeY = g_config.dpiY[g_activeUiIndex];
    if (g_selectedIndex == g_activeUiIndex) {
        activeX = next;
        activeY = next;
    }
    g_config.dpiX[g_selectedIndex] = next;
    g_config.dpiY[g_selectedIndex] = next;
    SortEditable(activeX, activeY, next);
    if (markDirty) g_dirty = true;
    SyncEditor();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
}

void SelectGear(int index, bool allowImmediateSwitch) {
    if (index < 0 || index >= (int)g_config.dpiX.size()) return;
    CommitEditorValue(true);
    if (index >= (int)g_config.dpiX.size()) return;
    int previousActive = g_activeUiIndex;
    g_selectedIndex = index;
    SyncEditor();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
    if (!allowImmediateSwitch || g_dirty || !g_connected || index == previousActive) return;
    int hardwareIndex = index < (int)g_hardwareSlotMap.size() ? g_hardwareSlotMap[index] : index;
    if (Device::SetDpiIndex(hardwareIndex)) {
        g_activeUiIndex = index;
        g_config.currentIndex = index;
        SaveCache();
        SetStatus(L"已切换到该 DPI 档位");
    } else {
        SetStatus(L"DPI 档位切换失败，请确认鼠标已连接");
    }
}

void AddGear() {
    CommitEditorValue(false);
    if ((int)g_config.dpiX.size() >= g_spec.maxSlots) {
        SetStatus(L"已达到该型号允许的最大 DPI 档位数");
        return;
    }
    std::vector<int> candidates = { 400, 800, 1200, 1600, 2400, 3200, 6400, 12800, 30000 };
    int candidate = 0;
    for (int raw : candidates) {
        int value = SnapDpi(raw);
        if (std::find(g_config.dpiX.begin(), g_config.dpiX.end(), value) == g_config.dpiX.end()) {
            candidate = value;
            break;
        }
    }
    if (candidate == 0) candidate = SnapDpi(g_config.dpiX[g_selectedIndex] + g_spec.step * 100);
    if (std::find(g_config.dpiX.begin(), g_config.dpiX.end(), candidate) != g_config.dpiX.end()) {
        SetStatus(L"没有可新增的默认 DPI，请先调整现有档位");
        return;
    }
    int activeX = g_config.dpiX[g_activeUiIndex];
    int activeY = g_config.dpiY[g_activeUiIndex];
    g_config.dpiX.push_back(candidate);
    g_config.dpiY.push_back(candidate);
    SortEditable(activeX, activeY, candidate);
    g_dirty = true;
    SyncEditor();
    SetStatus(L"已新增档位，保存后写入鼠标");
    InvalidateRect(g_hwnd, NULL, TRUE);
}

void DeleteGear() {
    CommitEditorValue(false);
    if ((int)g_config.dpiX.size() <= 1) {
        SetStatus(L"至少需要保留一个 DPI 档位");
        return;
    }
    int activeX = g_config.dpiX[g_activeUiIndex];
    int activeY = g_config.dpiY[g_activeUiIndex];
    int removeIndex = g_selectedIndex;
    g_config.dpiX.erase(g_config.dpiX.begin() + removeIndex);
    g_config.dpiY.erase(g_config.dpiY.begin() + removeIndex);
    int nextActiveX = activeX;
    int nextActiveY = activeY;
    if (removeIndex == g_activeUiIndex) {
        int newActive = std::min(removeIndex, (int)g_config.dpiX.size() - 1);
        nextActiveX = g_config.dpiX[newActive];
        nextActiveY = g_config.dpiY[newActive];
    }
    int nextSelected = std::min(removeIndex, (int)g_config.dpiX.size() - 1);
    int nextSelectedX = g_config.dpiX[nextSelected];
    SortEditable(nextActiveX, nextActiveY, nextSelectedX);
    g_dirty = true;
    SyncEditor();
    SetStatus(L"已删除档位，保存后写入鼠标");
    InvalidateRect(g_hwnd, NULL, TRUE);
}

void ApplyConfig() {
    CommitEditorValue(true);
    if (!g_connected) {
        SetStatus(L"鼠标未连接，无法写入 DPI");
        return;
    }
    g_config.currentIndex = g_selectedIndex;
    if (!Device::WriteDpiConfig(g_config)) {
        SetStatus(L"DPI 写入或回读校验失败，鼠标设置未完全应用");
        return;
    }
    for (int i = 0; i < (int)g_config.dpiX.size(); ++i) g_hardwareSlotMap[i] = i;
    g_activeUiIndex = g_selectedIndex;
    g_config.currentIndex = g_selectedIndex;
    g_dirty = false;
    SaveCache();
    SetStatus(L"DPI 已保存并写入鼠标板载存储");
    InvalidateRect(g_hwnd, NULL, TRUE);
}

HWND CreateButtonControl(const WCHAR* text, int x, int y, int w, int h, int id) {
    HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        S(x), S(y), S(w), S(h), g_hwnd, (HMENU)(INT_PTR)id, (HINSTANCE)GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE), NULL);
    if (button) {
        SendMessageW(button, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
        SetWindowSubclass(button, ButtonSubclassProc, 1, 0);
    }
    return button;
}

void CreateControls() {
    CreateButtonControl(L"+ 添加", 40, 118, 100, 28, IDC_ADD_GEAR);
    CreateButtonControl(L"删除", 148, 118, 96, 28, IDC_DELETE_GEAR);
    g_hEdit = CreateWindowExW(0, L"EDIT", L"800", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
        S(378), S(277), S(160), S(32), g_hwnd, (HMENU)IDC_DPI_EDIT, (HINSTANCE)GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE), NULL);
    if (g_hEdit) {
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
        SendMessageW(g_hEdit, EM_SETLIMITTEXT, 5, 0);
    }
    CreateButtonControl(L"−", 548, 277, 38, 32, IDC_MINUS);
    CreateButtonControl(L"+", 594, 277, 38, 32, IDC_PLUS);
    CreateButtonControl(L"保存并应用", 594, 462, 120, 32, IDC_APPLY);
    CreateButtonControl(L"关闭", 724, 462, 72, 32, IDC_CLOSE);
}

void AdjustSelected(int delta) {
    if (g_config.dpiX.empty()) return;
    int current = g_config.dpiX[g_selectedIndex];
    int next = SnapDpi(current + delta * g_spec.step);
    g_config.dpiX[g_selectedIndex] = next;
    g_config.dpiY[g_selectedIndex] = next;
    int activeX = g_config.dpiX[g_activeUiIndex];
    int activeY = g_config.dpiY[g_activeUiIndex];
    if (g_selectedIndex == g_activeUiIndex) {
        activeX = next;
        activeY = next;
    }
    SortEditable(activeX, activeY, next);
    g_dirty = true;
    SyncEditor();
    InvalidateRect(g_hwnd, NULL, TRUE);
}

bool SaveAndMaybeClose() {
    ApplyConfig();
    return !g_dirty;
}LRESULT CALLBACK DpiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_hwnd = hWnd;
            g_dark = Tray::IsDarkMode();
            RebuildBrushes();
            g_fontTitle = MakeFont(20, FW_BOLD);
            g_fontBig = MakeFont(26, FW_BOLD);
            g_fontBold = MakeFont(13, FW_SEMIBOLD);
            g_fontNormal = MakeFont(13, FW_NORMAL);
            g_fontSmall = MakeFont(11, FW_NORMAL);
            CreateControls();
            LoadConfig();
            UpdateTheme();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            DrawUi(hWnd);
            return 0;

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = (DRAWITEMSTRUCT*)lParam;
            if (item && item->CtlType == ODT_BUTTON) {
                DrawOwnerButton(item);
                return TRUE;
            }
            break;
        }

        case WM_SETTINGCHANGE:
            UpdateTheme();
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (id == IDC_ADD_GEAR && code == BN_CLICKED) { AddGear(); return 0; }
            if (id == IDC_DELETE_GEAR && code == BN_CLICKED) { DeleteGear(); return 0; }
            if (id == IDC_MINUS && code == BN_CLICKED) { AdjustSelected(-1); return 0; }
            if (id == IDC_PLUS && code == BN_CLICKED) { AdjustSelected(1); return 0; }
            if (id == IDC_APPLY && code == BN_CLICKED) { ApplyConfig(); return 0; }
            if ((id == IDC_CLOSE && code == BN_CLICKED) || id == IDCANCEL) {
                SendMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (id == IDC_DPI_EDIT && code == EN_CHANGE && !g_updatingEdit) {
                WCHAR text[32] = {0};
                GetWindowTextW(g_hEdit, text, ARRAYSIZE(text));
                if (g_lastSyncedEdit != text) {
                    g_dirty = true;
                    SetStatus(L"DPI 已修改，保存后写入鼠标");
                }
                return 0;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int gear = HitTestGear(x, y);
            if (gear >= 0) {
                SelectGear(gear, true);
                return 0;
            }
            RECT track = { SliderLeft(), SliderY() - S(18), SliderRight(), SliderY() + S(18) };
            POINT point = { x, y };
            if (PtInRect(&track, point)) {
                g_draggingSlider = true;
                SetCapture(hWnd);
                g_dirty = true;
                int next = DpiForSliderX(x);
                int activeX = g_config.dpiX[g_activeUiIndex];
                int activeY = g_config.dpiY[g_activeUiIndex];
                if (g_selectedIndex == g_activeUiIndex) { activeX = next; activeY = next; }
                g_config.dpiX[g_selectedIndex] = next;
                g_config.dpiY[g_selectedIndex] = next;
                SortEditable(activeX, activeY, next);
                SyncEditor();
                InvalidateRect(hWnd, NULL, TRUE);
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (g_draggingSlider) {
                int next = DpiForSliderX(x);
                if (next != g_config.dpiX[g_selectedIndex]) {
                    g_dirty = true;
                    int activeX = g_config.dpiX[g_activeUiIndex];
                    int activeY = g_config.dpiY[g_activeUiIndex];
                    if (g_selectedIndex == g_activeUiIndex) { activeX = next; activeY = next; }
                    g_config.dpiX[g_selectedIndex] = next;
                    g_config.dpiY[g_selectedIndex] = next;
                    SortEditable(activeX, activeY, next);
                    SyncEditor();
                    InvalidateRect(hWnd, NULL, TRUE);
                }
                return 0;
            }
            int hover = HitTestGear(x, y);
            if (hover != g_hoverGear) {
                g_hoverGear = hover;
                InvalidateRect(hWnd, NULL, FALSE);
                TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hWnd, 0 };
                TrackMouseEvent(&track);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_hoverGear != -1) {
                g_hoverGear = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (g_draggingSlider) {
                g_draggingSlider = false;
                ReleaseCapture();
                CommitEditorValue(true);
                return 0;
            }
            break;

        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, ColorText());
            SetBkColor(dc, ColorField());
            return (LRESULT)(g_brushField ? g_brushField : GetStockObject(WHITE_BRUSH));
        }

        case WM_CTLCOLORBTN:
            return (LRESULT)(g_brushBg ? g_brushBg : GetStockObject(WHITE_BRUSH));

        case WM_CLOSE:
            if (g_dirty) {
                int result = MessageBoxW(hWnd, L"DPI 档位尚未保存，是否保存后关闭？", L"DPI 设置", MB_YESNOCANCEL | MB_ICONQUESTION);
                if (result == IDCANCEL) return 0;
                if (result == IDYES && !SaveAndMaybeClose()) return 0;
            }
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            if (g_brushBg) DeleteObject(g_brushBg);
            if (g_brushSurface) DeleteObject(g_brushSurface);
            if (g_brushField) DeleteObject(g_brushField);
            g_brushBg = g_brushSurface = g_brushField = NULL;
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontBig) DeleteObject(g_fontBig);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_fontNormal) DeleteObject(g_fontNormal);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            g_fontTitle = g_fontBig = g_fontBold = g_fontNormal = g_fontSmall = NULL;
            g_hwnd = NULL;
            g_hEdit = NULL;
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

void Show(HWND owner) {
    if (g_hwnd && IsWindow(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwnd);
        LoadConfig();
        return;
    }
    INITCOMMONCONTROLSEX common = { sizeof(common), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&common);
    if (!g_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DpiWndProc;
        wc.hInstance = (HINSTANCE)GetModuleHandleW(NULL);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = WINDOW_CLASS;
        if (!RegisterClassExW(&wc)) return;
        g_classRegistered = true;
    }
    g_hOwner = owner;
    g_dpi = (int)GetSystemDpiSafe();
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;
    int maxDpiW = MulDiv(96, std::max(320, workW - 64), WINDOW_WIDTH);
    int maxDpiH = MulDiv(96, std::max(240, workH - 64), WINDOW_HEIGHT);
    g_dpi = std::max(1, std::min(g_dpi, std::min(maxDpiW, maxDpiH)));
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT rc = { 0, 0, S(WINDOW_WIDTH), S(WINDOW_HEIGHT) };
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_APPWINDOW);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int x = work.left + (workW - width) / 2;
    int y = work.top + (workH - height) / 2;
    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        WINDOW_CLASS,
        L"DPI 设置 - rapoo-tray",
        style,
        x, y, width, height,
        owner, NULL, (HINSTANCE)GetModuleHandleW(NULL), NULL
    );
    if (g_hwnd) {
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        UpdateWindow(g_hwnd);
        SetForegroundWindow(g_hwnd);
    }
}

void Cleanup() {
    if (g_hwnd && IsWindow(g_hwnd)) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
}

bool PreTranslateMessage(MSG* msg) {
    if (!g_hwnd || !IsWindow(g_hwnd) || !msg) return false;
    if (msg->hwnd == g_hwnd || IsChild(g_hwnd, msg->hwnd)) {
        if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN) {
            HWND focused = GetFocus();
            if (focused && GetDlgCtrlID(focused) == IDC_DPI_EDIT) {
                CommitEditorValue(true);
                return true;
            }
        }
        return IsDialogMessageW(g_hwnd, msg) != FALSE;
    }
    return false;
}

} // namespace DpiWindow
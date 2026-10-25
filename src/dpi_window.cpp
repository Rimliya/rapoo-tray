#include "dpi_window.h"
#include "battery_estimator.h"
#include "device_manager.h"

#include <commctrl.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace DpiWindow {
namespace {

constexpr const WCHAR* WINDOW_CLASS = L"RapooDpiEditorWnd";
constexpr const WCHAR* REG_SUBKEY = L"Software\\rapoo-tray\\DpiProfiles";
constexpr int WINDOW_WIDTH = 820;
constexpr int WINDOW_HEIGHT = 520;
constexpr int SLOT_BUTTON_BASE = 7001;
constexpr int IDC_ADD_SLOT = 7010;
constexpr int IDC_DELETE_SLOT = 7011;
constexpr int IDC_DPI_X = 7012;
constexpr int IDC_DPI_Y = 7013;
constexpr int IDC_DPI_SLIDER = 7014;
constexpr int IDC_INDEPENDENT = 7015;
constexpr int IDC_APPLY = 7016;
constexpr int IDC_CLOSE = 7017;
constexpr int IDC_MINUS = 7018;
constexpr int IDC_PLUS = 7019;
constexpr UINT_PTR TIMER_CONNECTION = 0x7201;

struct Gear {
    int x = 800;
    int y = 800;
    int id = 0;
};

HWND g_hwnd = NULL;
HWND g_slotButtons[Device::MAX_DPI_SLOTS] = {0};
HWND g_hX = NULL;
HWND g_hY = NULL;
HWND g_hSlider = NULL;
HWND g_hIndependent = NULL;
HWND g_hTitle = NULL;
HWND g_hSubtitle = NULL;
HWND g_hDetail = NULL;
HWND g_hStatus = NULL;
HFONT g_fontTitle = NULL;
HFONT g_fontBold = NULL;
HFONT g_fontNormal = NULL;
HFONT g_fontSmall = NULL;
HBRUSH g_brushBg = NULL;
HBRUSH g_brushSurface = NULL;
HBRUSH g_brushField = NULL;
bool g_classRegistered = false;
bool g_dark = false;
bool g_loading = false;
bool g_dirty = false;
bool g_connected = false;
bool g_readFromDevice = false;
bool g_independent = false;
bool g_statusGood = true;
int g_dpi = 96;
int g_selectedId = 0;
int g_activeId = 0;
int g_nextGearId = 1;
BatteryEstimate::DpiSpec g_spec;
std::wstring g_modelKey;
std::wstring g_modelLabel;
std::wstring g_statusText;
std::vector<Gear> g_gears;

int S(int value) { return MulDiv(value, g_dpi, 96); }
COLORREF ColorBg() { return g_dark ? RGB(24, 26, 32) : RGB(255, 255, 255); }
COLORREF ColorSurface() { return g_dark ? RGB(30, 33, 42) : RGB(247, 249, 252); }
COLORREF ColorField() { return g_dark ? RGB(37, 41, 51) : RGB(242, 245, 250); }
COLORREF ColorBorder() { return g_dark ? RGB(50, 54, 65) : RGB(205, 212, 222); }
COLORREF ColorText() { return g_dark ? RGB(235, 240, 248) : RGB(20, 24, 32); }
COLORREF ColorMuted() { return g_dark ? RGB(140, 150, 165) : RGB(105, 115, 130); }
COLORREF ColorAccent() { return g_dark ? RGB(96, 205, 255) : RGB(0, 110, 215); }
COLORREF ColorAccentFill() { return g_dark ? RGB(55, 103, 145) : RGB(218, 232, 255); }

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

void UpdateTheme() {
    HKEY hKey = NULL;
    bool dark = true;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 1, size = sizeof(value), type = 0;
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, &type,
                (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            dark = value == 0;
        }
        RegCloseKey(hKey);
    }
    g_dark = dark;
    RebuildBrushes();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
}

int SnapDpi(int value) {
    value = std::clamp(value, g_spec.minDpi, g_spec.maxDpi);
    int step = std::max(1, g_spec.step);
    int steps = (value - g_spec.minDpi + step / 2) / step;
    return std::clamp(g_spec.minDpi + steps * step, g_spec.minDpi, g_spec.maxDpi);
}

void SetStatus(const WCHAR* text, bool good = true) {
    g_statusText = text ? text : L"";
    g_statusGood = good;
    if (g_hStatus) SetWindowTextW(g_hStatus, g_statusText.c_str());
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
    RECT section = {S(x), S(y), S(x + w), S(y + h)};
    FillRounded(dc, section, ColorSurface(), ColorBorder(), S(12));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, g_fontBold ? g_fontBold : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, ColorText());
    RECT titleRect = {section.left + S(16), section.top + S(4), section.right - S(16), section.top + S(30)};
    DrawTextW(dc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
}

void DrawLabel(HDC dc, const WCHAR* text, int x, int y, int w, int h,
               HFONT font, COLORREF color, DWORD flags = DT_LEFT) {
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, font ? font : g_fontNormal);
    SetTextColor(dc, color);
    RECT rc = {S(x), S(y), S(x + w), S(y + h)};
    DrawTextW(dc, text, -1, &rc, flags | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

HWND CreateControl(const WCHAR* cls, const WCHAR* text, DWORD style, DWORD exStyle,
                   int x, int y, int w, int h, int id) {
    HWND control = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        S(x), S(y), S(w), S(h), g_hwnd, (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE), NULL);
    if (control) SendMessageW(control, WM_SETFONT,
        (WPARAM)(g_fontNormal ? g_fontNormal : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

HWND CreateButton(const WCHAR* text, int x, int y, int w, int h, int id) {
    return CreateControl(L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, 0, x, y, w, h, id);
}

int SelectedIndex() {
    for (int i = 0; i < (int)g_gears.size(); ++i) {
        if (g_gears[(size_t)i].id == g_selectedId) return i;
    }
    return -1;
}

int ActiveIndex() {
    for (int i = 0; i < (int)g_gears.size(); ++i) {
        if (g_gears[(size_t)i].id == g_activeId) return i;
    }
    return 0;
}

void SortGears() {
    std::sort(g_gears.begin(), g_gears.end(), [](const Gear& a, const Gear& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.id < b.id;
    });
}

std::wstring CacheKeyFor(const std::wstring& modelKey) {
    std::wstring key = modelKey.empty() ? L"generic" : modelKey;
    for (WCHAR& ch : key) {
        if (!(iswalnum(ch) || ch == L'-' || ch == L'_')) ch = L'_';
    }
    return std::wstring(REG_SUBKEY) + L"\\" + key;
}

std::wstring CacheKey() {
    return CacheKeyFor(g_modelKey);
}

bool LoadQuickCache(const std::wstring& modelKey, int limit,
                    std::vector<QuickSlot>& slots, int& activeIndex) {
    slots.clear();
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CacheKeyFor(modelKey).c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD count = 0, active = 0, type = 0, size = sizeof(DWORD);
    bool ok = RegQueryValueExW(hKey, L"Count", NULL, &type, (LPBYTE)&count, &size) == ERROR_SUCCESS;
    size = sizeof(DWORD);
    ok = ok && RegQueryValueExW(hKey, L"Active", NULL, &type, (LPBYTE)&active, &size) == ERROR_SUCCESS;
    count = std::clamp<DWORD>(count, 1, (DWORD)std::max(1, limit));
    std::vector<int> x(count), y(count);
    size = count * sizeof(int);
    ok = ok && RegQueryValueExW(hKey, L"ValuesX", NULL, &type, (LPBYTE)x.data(), &size) == ERROR_SUCCESS;
    size = count * sizeof(int);
    ok = ok && RegQueryValueExW(hKey, L"ValuesY", NULL, &type, (LPBYTE)y.data(), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    if (!ok) return false;

    for (DWORD i = 0; i < count; ++i) {
        QuickSlot slot;
        slot.x = x[i];
        slot.y = y[i];
        slots.push_back(slot);
    }
    activeIndex = std::clamp((int)active, 0, (int)slots.size() - 1);
    return !slots.empty();
}

void SaveQuickCache(const std::wstring& modelKey, const std::vector<QuickSlot>& slots, int activeIndex) {
    if (slots.empty()) return;
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CacheKeyFor(modelKey).c_str(), 0, NULL, 0, KEY_WRITE,
            NULL, &hKey, NULL) != ERROR_SUCCESS) return;
    DWORD count = (DWORD)slots.size();
    DWORD active = (DWORD)std::clamp(activeIndex, 0, (int)slots.size() - 1);
    std::vector<int> x(count), y(count);
    for (DWORD i = 0; i < count; ++i) {
        x[i] = slots[(size_t)i].x;
        y[i] = slots[(size_t)i].y;
    }
    RegSetValueExW(hKey, L"Count", 0, REG_DWORD, (const BYTE*)&count, sizeof(count));
    RegSetValueExW(hKey, L"Active", 0, REG_DWORD, (const BYTE*)&active, sizeof(active));
    RegSetValueExW(hKey, L"ValuesX", 0, REG_BINARY, (const BYTE*)x.data(), count * sizeof(int));
    RegSetValueExW(hKey, L"ValuesY", 0, REG_BINARY, (const BYTE*)y.data(), count * sizeof(int));
    RegCloseKey(hKey);
}

bool LoadCache(int& activeIndex) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CacheKey().c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD count = 0, active = 0, type = 0, size = sizeof(DWORD);
    bool ok = RegQueryValueExW(hKey, L"Count", NULL, &type, (LPBYTE)&count, &size) == ERROR_SUCCESS;
    size = sizeof(DWORD);
    ok = ok && RegQueryValueExW(hKey, L"Active", NULL, &type, (LPBYTE)&active, &size) == ERROR_SUCCESS;
    count = std::clamp<DWORD>(count, 1, (DWORD)g_spec.maxSlots);
    count = std::min<DWORD>(count, Device::MAX_DPI_SLOTS);
    std::vector<int> x(count), y(count);
    size = count * sizeof(int);
    ok = ok && RegQueryValueExW(hKey, L"ValuesX", NULL, &type, (LPBYTE)x.data(), &size) == ERROR_SUCCESS;
    size = count * sizeof(int);
    ok = ok && RegQueryValueExW(hKey, L"ValuesY", NULL, &type, (LPBYTE)y.data(), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    if (!ok) return false;

    g_gears.clear();
    for (DWORD i = 0; i < count; ++i) {
        Gear gear;
        gear.x = std::clamp(x[i], g_spec.minDpi, g_spec.maxDpi);
        gear.y = std::clamp(y[i], g_spec.minDpi, g_spec.maxDpi);
        gear.id = g_nextGearId++;
        g_gears.push_back(gear);
    }
    SortGears();
    activeIndex = std::clamp((int)active, 0, (int)g_gears.size() - 1);
    return true;
}

void SaveCache(int activeIndex) {
    if (g_gears.empty()) return;
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CacheKey().c_str(), 0, NULL, 0, KEY_WRITE,
            NULL, &hKey, NULL) != ERROR_SUCCESS) return;
    DWORD count = (DWORD)g_gears.size();
    DWORD active = (DWORD)std::clamp(activeIndex, 0, (int)g_gears.size() - 1);
    std::vector<int> x(count), y(count);
    for (DWORD i = 0; i < count; ++i) {
        x[i] = g_gears[(size_t)i].x;
        y[i] = g_gears[(size_t)i].y;
    }
    RegSetValueExW(hKey, L"Count", 0, REG_DWORD, (const BYTE*)&count, sizeof(count));
    RegSetValueExW(hKey, L"Active", 0, REG_DWORD, (const BYTE*)&active, sizeof(active));
    RegSetValueExW(hKey, L"ValuesX", 0, REG_BINARY, (const BYTE*)x.data(), count * sizeof(int));
    RegSetValueExW(hKey, L"ValuesY", 0, REG_BINARY, (const BYTE*)y.data(), count * sizeof(int));
    RegCloseKey(hKey);
}

void BuildDefaults() {
    g_gears.clear();
    const int candidates[] = {400, 800, 1200, 1600, 2400, 3200, 6400, 12800, 30000, 60000};
    for (int candidate : candidates) {
        int value = SnapDpi(candidate);
        bool duplicate = false;
        for (const Gear& gear : g_gears) duplicate = duplicate || gear.x == value;
        if (duplicate) continue;
        Gear gear;
        gear.x = gear.y = value;
        gear.id = g_nextGearId++;
        g_gears.push_back(gear);
        if ((int)g_gears.size() >= std::min(g_spec.maxSlots, Device::MAX_DPI_SLOTS)) break;
    }
    if (g_gears.empty()) {
        Gear gear;
        gear.x = gear.y = g_spec.minDpi;
        gear.id = g_nextGearId++;
        g_gears.push_back(gear);
    }
    SortGears();
    int active = 0;
    for (int i = 0; i < (int)g_gears.size(); ++i) {
        if (g_gears[(size_t)i].x == 800) { active = i; break; }
    }
    g_activeId = g_gears[(size_t)active].id;
    g_selectedId = g_activeId;
}

void SetSliderRange() {
    if (!g_hSlider) return;
    int steps = std::max(1, (g_spec.maxDpi - g_spec.minDpi) / std::max(1, g_spec.step));
    SendMessageW(g_hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, steps));
    SendMessageW(g_hSlider, TBM_SETTICFREQ, std::max(1, steps / 10), 0);
    SendMessageW(g_hSlider, TBM_SETLINESIZE, 1, 0);
    SendMessageW(g_hSlider, TBM_SETPAGESIZE, 10, 0);
}

void SyncControls() {
    int index = SelectedIndex();
    if (index < 0) return;
    Gear& gear = g_gears[(size_t)index];
    g_loading = true;
    WCHAR text[32] = {0};
    StringCchPrintfW(text, ARRAYSIZE(text), L"%d", gear.x);
    SetWindowTextW(g_hX, text);
    StringCchPrintfW(text, ARRAYSIZE(text), L"%d", g_independent ? gear.y : gear.x);
    SetWindowTextW(g_hY, text);
    EnableWindow(g_hY, g_independent ? TRUE : FALSE);
    int steps = std::max(0, (gear.x - g_spec.minDpi) / std::max(1, g_spec.step));
    SendMessageW(g_hSlider, TBM_SETPOS, TRUE, steps);
    SendMessageW(g_hIndependent, BM_SETCHECK, g_independent ? BST_CHECKED : BST_UNCHECKED, 0);
    g_loading = false;

    WCHAR detail[180] = {0};
    int active = ActiveIndex();
    StringCchPrintfW(detail, ARRAYSIZE(detail), L"正在编辑第 %d 档 · 鼠标当前第 %d 档使用 %d DPI",
        index + 1, active + 1, g_gears[(size_t)active].x);
    if (!g_connected) {
        StringCchPrintfW(detail, ARRAYSIZE(detail), L"正在编辑第 %d 档 · 鼠标当前未连接", index + 1);
    }
    SetWindowTextW(g_hDetail, detail);
}

bool ReadControlInt(HWND control, int& value) {
    WCHAR buffer[32] = {0};
    if (!control || GetWindowTextW(control, buffer, ARRAYSIZE(buffer)) <= 0) return false;
    WCHAR* end = nullptr;
    long parsed = wcstol(buffer, &end, 10);
    if (end == buffer) return false;
    while (end && iswspace(*end)) ++end;
    if (end && *end != L'\0') return false;
    value = (int)parsed;
    return true;
}

bool CommitControls(bool showErrors) {
    int index = SelectedIndex();
    if (index < 0) return false;
    int x = 0, y = 0;
    if (!ReadControlInt(g_hX, x)) {
        if (showErrors) MessageBoxW(g_hwnd, L"请输入有效的 DPI 数值。", L"DPI 设置", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (g_independent) {
        if (!ReadControlInt(g_hY, y)) {
            if (showErrors) MessageBoxW(g_hwnd, L"请输入有效的纵向 DPI 数值。", L"DPI 设置", MB_OK | MB_ICONWARNING);
            return false;
        }
    } else {
        y = x;
    }
    x = SnapDpi(x);
    y = SnapDpi(y);
    Gear& gear = g_gears[(size_t)index];
    if (gear.x != x || gear.y != y) {
        gear.x = x;
        gear.y = y;
        g_dirty = true;
    }
    return true;
}

void RebuildSlots() {
    SortGears();
    for (int i = 0; i < Device::MAX_DPI_SLOTS; ++i) {
        HWND button = g_slotButtons[i];
        if (!button) continue;
        if (i >= (int)g_gears.size()) {
            ShowWindow(button, SW_HIDE);
            continue;
        }
        int col = i % 2;
        int row = i / 2;
        SetWindowPos(button, NULL, S(40 + col * 116), S(142 + row * 52),
            S(106), S(38), SWP_NOZORDER | SWP_SHOWWINDOW);
        WCHAR text[32] = {0};
        StringCchPrintfW(text, ARRAYSIZE(text), L"%d", g_gears[(size_t)i].x);
        SetWindowTextW(button, text);
        InvalidateRect(button, NULL, TRUE);
    }
    if (g_hwnd) {
        InvalidateRect(g_hwnd, NULL, FALSE);
        EnableWindow(GetDlgItem(g_hwnd, IDC_ADD_SLOT),
            (int)g_gears.size() < std::min(g_spec.maxSlots, Device::MAX_DPI_SLOTS));
        EnableWindow(GetDlgItem(g_hwnd, IDC_DELETE_SLOT), g_gears.size() > 1);
    }
    SyncControls();
}

void LoadConfig() {
    Device::State state = Device::GetCurrentState();
    g_connected = state.isConnected;
    g_modelKey.clear();
    g_modelLabel.clear();
    if (!BatteryEstimate::GetDpiSpecForState(state, g_spec, &g_modelKey, &g_modelLabel)) {
        g_spec = BatteryEstimate::DpiSpec();
        g_modelKey = L"generic";
        g_modelLabel = L"通用";
    }
    g_spec.maxSlots = std::clamp(g_spec.maxSlots, 1, Device::MAX_DPI_SLOTS);
    if (g_hSubtitle) {
        WCHAR subtitle[220] = {0};
        StringCchPrintfW(subtitle, ARRAYSIZE(subtitle),
            L"%s · 按型号限制 DPI 范围，档位可自由增删并按数值升序排列",
            g_modelLabel.empty() ? L"通用" : g_modelLabel.c_str());
        SetWindowTextW(g_hSubtitle, subtitle);
    }
    SetSliderRange();

    g_gears.clear();
    g_nextGearId = 1;
    g_readFromDevice = false;
    bool loaded = false;
    int active = 0;

    if (g_connected) {
        Device::DpiConfig hardware;
        if (Device::ReadDpiConfig(hardware) && !hardware.dpiX.empty()) {
            int count = std::min({(int)hardware.dpiX.size(), (int)hardware.dpiY.size(), g_spec.maxSlots});
            for (int i = 0; i < count; ++i) {
                Gear gear;
                gear.x = std::clamp(hardware.dpiX[(size_t)i], g_spec.minDpi, g_spec.maxDpi);
                gear.y = std::clamp(hardware.dpiY[(size_t)i], g_spec.minDpi, g_spec.maxDpi);
                gear.id = g_nextGearId++;
                g_gears.push_back(gear);
            }
            if (!g_gears.empty()) {
                active = std::clamp(hardware.currentIndex, 0, (int)g_gears.size() - 1);
                g_activeId = g_gears[(size_t)active].id;
                loaded = true;
                g_readFromDevice = true;
            }
        }
    }

    if (!loaded && LoadCache(active) && !g_gears.empty()) loaded = true;
    if (!loaded || g_gears.empty()) {
        BuildDefaults();
        active = ActiveIndex();
    }

    SortGears();
    g_activeId = g_gears[(size_t)std::clamp(active, 0, (int)g_gears.size() - 1)].id;
    g_selectedId = g_activeId;
    g_dirty = false;
    g_independent = false;
    for (const Gear& gear : g_gears) {
        if (gear.x != gear.y) { g_independent = true; break; }
    }
    RebuildSlots();

    if (g_connected && g_readFromDevice) {
        SetStatus(L"已读取鼠标当前 DPI 档位；修改后点击“应用并切换”写入鼠标", true);
    } else if (g_connected) {
        SetStatus(L"未能读取当前配置，已载入本机缓存；应用前请确认数值", false);
    } else {
        SetStatus(L"鼠标未连接：可编辑并保存本机缓存，连接后再应用", false);
    }
}

void SelectGear(int index) {
    if (index < 0 || index >= (int)g_gears.size()) return;
    if (!CommitControls(false)) return;
    g_selectedId = g_gears[(size_t)index].id;
    SyncControls();
    for (HWND button : g_slotButtons) if (button) InvalidateRect(button, NULL, TRUE);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

void AddGear() {
    if (!CommitControls(true)) return;
    int limit = std::min(g_spec.maxSlots, Device::MAX_DPI_SLOTS);
    if ((int)g_gears.size() >= limit) {
        MessageBoxW(g_hwnd, L"当前型号已达到最大 DPI 档位数量。", L"DPI 设置", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const int candidates[] = {400, 800, 1200, 1600, 2400, 3200, 6400, 12800, 30000, 60000};
    int value = 0;
    for (int candidate : candidates) {
        int snapped = SnapDpi(candidate);
        bool used = false;
        for (const Gear& gear : g_gears) used = used || gear.x == snapped;
        if (!used) { value = snapped; break; }
    }
    if (value == 0) {
        value = SnapDpi(g_spec.maxDpi);
        for (int offset = 0; offset <= 20; ++offset) {
            int candidate = SnapDpi(g_spec.maxDpi - offset * g_spec.step);
            bool used = false;
            for (const Gear& gear : g_gears) used = used || gear.x == candidate;
            if (!used) { value = candidate; break; }
        }
    }
    Gear gear;
    gear.x = gear.y = value;
    gear.id = g_nextGearId++;
    g_gears.push_back(gear);
    g_selectedId = gear.id;
    g_dirty = true;
    SortGears();
    RebuildSlots();
    SetStatus(L"已添加档位，可修改数值后应用", true);
}

void DeleteGear() {
    if (g_gears.size() <= 1) {
        MessageBoxW(g_hwnd, L"至少需要保留一个 DPI 档位。", L"DPI 设置", MB_OK | MB_ICONINFORMATION);
        return;
    }
    CommitControls(false);
    int index = SelectedIndex();
    if (index < 0) return;
    bool deletingActive = g_gears[(size_t)index].id == g_activeId;
    g_gears.erase(g_gears.begin() + index);
    index = std::min(index, (int)g_gears.size() - 1);
    g_selectedId = g_gears[(size_t)index].id;
    if (deletingActive) g_activeId = g_selectedId;
    g_dirty = true;
    RebuildSlots();
    SetStatus(L"已删除档位，应用后写入鼠标", true);
}

void AdjustValue(int delta) {
    if (!CommitControls(true)) return;
    int index = SelectedIndex();
    if (index < 0) return;
    Gear& gear = g_gears[(size_t)index];
    int value = SnapDpi(gear.x + delta * std::max(1, g_spec.step));
    gear.x = value;
    if (!g_independent) gear.y = value;
    g_dirty = true;
    SyncControls();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

void ApplyConfig() {
    if (!CommitControls(true)) return;
    SortGears();
    int selected = SelectedIndex();
    if (selected < 0) return;
    int previousActiveId = g_activeId;

    Device::DpiConfig config;
    config.currentIndex = selected;
    for (const Gear& gear : g_gears) {
        config.dpiX.push_back(gear.x);
        config.dpiY.push_back(gear.y);
    }

    if (!g_connected) {
        g_activeId = g_selectedId;
        SaveCache(config.currentIndex);
        g_dirty = false;
        RebuildSlots();
        SetStatus(L"已保存到本机缓存；请连接鼠标后再次点击“应用并切换”", false);
        return;
    }

    if (!Device::WriteDpiConfig(config)) {
        g_activeId = previousActiveId;
        SetStatus(L"写入失败，请唤醒鼠标后重试；本机配置未丢失", false);
        MessageBoxW(g_hwnd, L"DPI 写入失败。请移动鼠标唤醒设备，或检查是否有其他雷柏软件正在占用设备。",
            L"DPI 设置", MB_OK | MB_ICONERROR);
        return;
    }

    g_activeId = g_selectedId;
    SaveCache(config.currentIndex);
    g_dirty = false;
    RebuildSlots();
    SetStatus(L"已写入鼠标，并切换到所选 DPI 档位", true);
}

void PaintWindow(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hWnd, &ps);
    RECT rc = {0};
    GetClientRect(hWnd, &rc);
    HBRUSH bg = CreateSolidBrush(ColorBg());
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    DrawSection(dc, 24, 82, 260, 356, L"DPI 档位");
    DrawSection(dc, 300, 82, 496, 356, L"DPI 设置");

    WCHAR range[128] = {0};
    StringCchPrintfW(range, ARRAYSIZE(range), L"%d–%d DPI · 步进 %d · 最多 %d 档",
        g_spec.minDpi, g_spec.maxDpi, g_spec.step,
        std::min(g_spec.maxSlots, Device::MAX_DPI_SLOTS));
    DrawLabel(dc, range, 40, 112, 228, 22, g_fontSmall, ColorMuted());
    DrawLabel(dc, L"横向 X", 330, 143, 100, 28, g_fontNormal, ColorText());
    DrawLabel(dc, L"纵向 Y", 330, 183, 100, 28, g_fontNormal, ColorText());
    DrawLabel(dc, L"拖动滑块可连续调整；档位会按 DPI 从小到大排列。", 330, 306, 430, 22,
        g_fontSmall, ColorMuted());
    DrawLabel(dc, L"修改 DPI 不会重置鼠标，只有点击应用后才会写入设备。", 330, 332, 430, 22,
        g_fontSmall, ColorMuted());

    HPEN border = CreatePen(PS_SOLID, 1, ColorBorder());
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 0, 0, rc.right, rc.bottom, S(12), S(12));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(border);
    EndPaint(hWnd, &ps);
}

LRESULT DrawSlotButton(const DRAWITEMSTRUCT* item) {
    int slot = (int)item->CtlID - SLOT_BUTTON_BASE;
    if (slot < 0 || slot >= (int)g_gears.size()) return 0;
    const Gear& gear = g_gears[(size_t)slot];
    bool selected = gear.id == g_selectedId;
    bool active = gear.id == g_activeId;
    COLORREF fill = selected ? ColorAccentFill() : ColorField();
    COLORREF border = selected ? ColorAccent() : ColorBorder();
    RECT rect = item->rcItem;
    FillRounded(item->hDC, rect, fill, border, S(9));
    WCHAR text[32] = {0};
    StringCchPrintfW(text, ARRAYSIZE(text), L"%d", gear.x);
    HGDIOBJ oldFont = SelectObject(item->hDC, g_fontBold ? g_fontBold : g_fontNormal);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, selected ? ColorAccent() : ColorText());
    RECT textRect = rect;
    if (active) textRect.right -= S(28);
    DrawTextW(item->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (active) {
        HBRUSH dot = CreateSolidBrush(ColorAccent());
        HGDIOBJ oldBrush = SelectObject(item->hDC, dot);
        HGDIOBJ oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
        int cx = rect.right - S(15);
        int cy = rect.top + S(12);
        Ellipse(item->hDC, cx - S(4), cy - S(4), cx + S(4), cy + S(4));
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(dot);
    }
    SelectObject(item->hDC, oldFont);
    if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &rect);
    return 1;
}

void CreateControls() {
    g_fontTitle = MakeFont(20, FW_BOLD);
    g_fontBold = MakeFont(14, FW_SEMIBOLD);
    g_fontNormal = MakeFont(13, FW_NORMAL);
    g_fontSmall = MakeFont(12, FW_NORMAL);

    g_hTitle = CreateControl(L"STATIC", L"DPI 设置", SS_LEFT, 0, 24, 14, 500, 32, 0);
    SendMessageW(g_hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    g_hSubtitle = CreateControl(L"STATIC", L"", SS_LEFT, 0, 24, 46, 760, 24, 0);
    SendMessageW(g_hSubtitle, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_hDetail = CreateControl(L"STATIC", L"", SS_LEFT, 0, 330, 112, 430, 24, 0);
    SendMessageW(g_hDetail, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    for (int i = 0; i < Device::MAX_DPI_SLOTS; ++i) {
        g_slotButtons[i] = CreateControl(L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW,
            0, 40, 142 + (i / 2) * 52, 106, 38, SLOT_BUTTON_BASE + i);
    }
    HWND add = CreateButton(L"+ 添加档位", 40, 350, 208, 30, IDC_ADD_SLOT);
    HWND del = CreateButton(L"删除当前档位", 40, 388, 208, 30, IDC_DELETE_SLOT);
    SendMessageW(add, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    SendMessageW(del, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_hX = CreateControl(L"EDIT", L"800", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
        WS_EX_CLIENTEDGE, 520, 139, 116, 30, IDC_DPI_X);
    g_hY = CreateControl(L"EDIT", L"800", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
        WS_EX_CLIENTEDGE, 520, 179, 116, 30, IDC_DPI_Y);
    CreateButton(L"−", 644, 139, 32, 30, IDC_MINUS);
    CreateButton(L"+", 684, 139, 32, 30, IDC_PLUS);
    SendMessageW(GetDlgItem(g_hwnd, IDC_MINUS), WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    SendMessageW(GetDlgItem(g_hwnd, IDC_PLUS), WM_SETFONT, (WPARAM)g_fontBold, TRUE);

    g_hSlider = CreateControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
        0, 330, 218, 436, 38, IDC_DPI_SLIDER);
    SendMessageW(g_hSlider, TBM_SETPAGESIZE, 10, 0);
    SetWindowTheme(g_hSlider, g_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);

    g_hIndependent = CreateControl(L"BUTTON", L"横向 X 与纵向 Y 独立设置",
        WS_TABSTOP | BS_AUTOCHECKBOX, 0, 330, 274, 300, 26, IDC_INDEPENDENT);
    SendMessageW(g_hIndependent, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);

    g_hStatus = CreateControl(L"STATIC", L"", SS_LEFT, 0, 24, 450, 550, 26, 0);
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    HWND apply = CreateButton(L"应用并切换", 598, 446, 112, 32, IDC_APPLY);
    HWND close = CreateButton(L"关闭", 720, 446, 76, 32, IDC_CLOSE);
    SendMessageW(apply, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
    SendMessageW(close, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
}

LRESULT CALLBACK DpiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_hwnd = hWnd;
            UpdateTheme();
            g_modelKey = L"generic";
            g_modelLabel = L"通用";
            g_spec = BatteryEstimate::DpiSpec();
            CreateControls();
            LoadConfig();
            SetTimer(hWnd, TIMER_CONNECTION, 1000, NULL);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintWindow(hWnd);
            return 0;

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* item = (const DRAWITEMSTRUCT*)lParam;
            if (item && item->CtlID >= SLOT_BUTTON_BASE &&
                item->CtlID < SLOT_BUTTON_BASE + Device::MAX_DPI_SLOTS) {
                return DrawSlotButton(item);
            }
            break;
        }

        case WM_HSCROLL: {
            if ((HWND)lParam == g_hSlider && !g_loading) {
                int index = SelectedIndex();
                if (index >= 0) {
                    int steps = (int)SendMessageW(g_hSlider, TBM_GETPOS, 0, 0);
                    int value = SnapDpi(g_spec.minDpi + steps * std::max(1, g_spec.step));
                    Gear& gear = g_gears[(size_t)index];
                    gear.x = value;
                    if (!g_independent) gear.y = value;
                    g_dirty = true;
                    g_loading = true;
                    WCHAR text[32] = {0};
                    StringCchPrintfW(text, ARRAYSIZE(text), L"%d", value);
                    SetWindowTextW(g_hX, text);
                    SetWindowTextW(g_hY, text);
                    g_loading = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (id >= SLOT_BUTTON_BASE && id < SLOT_BUTTON_BASE + Device::MAX_DPI_SLOTS) {
                if (code == BN_CLICKED) SelectGear(id - SLOT_BUTTON_BASE);
                return 0;
            }
            if (id == IDC_ADD_SLOT && code == BN_CLICKED) { AddGear(); return 0; }
            if (id == IDC_DELETE_SLOT && code == BN_CLICKED) { DeleteGear(); return 0; }
            if (id == IDC_MINUS && code == BN_CLICKED) { AdjustValue(-1); return 0; }
            if (id == IDC_PLUS && code == BN_CLICKED) { AdjustValue(1); return 0; }
            if (id == IDC_APPLY && code == BN_CLICKED) { ApplyConfig(); return 0; }
            if (id == IDC_CLOSE && code == BN_CLICKED) { SendMessageW(hWnd, WM_CLOSE, 0, 0); return 0; }
            if (id == IDC_INDEPENDENT && code == BN_CLICKED) {
                if (!CommitControls(false)) return 0;
                g_independent = SendMessageW(g_hIndependent, BM_GETCHECK, 0, 0) == BST_CHECKED;
                int index = SelectedIndex();
                if (index >= 0 && !g_independent) g_gears[(size_t)index].y = g_gears[(size_t)index].x;
                g_dirty = true;
                SyncControls();
                return 0;
            }
            if ((id == IDC_DPI_X || id == IDC_DPI_Y) && code == EN_CHANGE && !g_loading) {
                int value = 0;
                if (ReadControlInt(id == IDC_DPI_X ? g_hX : g_hY, value)) {
                    int index = SelectedIndex();
                    if (index >= 0) {
                        Gear& gear = g_gears[(size_t)index];
                        int snapped = SnapDpi(value);
                        if (id == IDC_DPI_X) {
                            gear.x = snapped;
                            if (!g_independent) gear.y = snapped;
                        } else {
                            gear.y = snapped;
                        }
                        g_dirty = true;
                        int steps = std::max(0, (gear.x - g_spec.minDpi) / std::max(1, g_spec.step));
                        g_loading = true;
                        SendMessageW(g_hSlider, TBM_SETPOS, TRUE, steps);
                        if (!g_independent) {
                            WCHAR text[32] = {0};
                            StringCchPrintfW(text, ARRAYSIZE(text), L"%d", gear.x);
                            SetWindowTextW(g_hY, text);
                        }
                        g_loading = false;
                    }
                }
                return 0;
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == TIMER_CONNECTION && !g_dirty) {
                bool wasConnected = g_connected;
                g_connected = Device::GetCurrentState().isConnected;
                if (wasConnected != g_connected) {
                    SetStatus(g_connected ? L"鼠标已连接；修改后可写入设备" :
                        L"鼠标连接已断开；编辑内容不会自动写入", g_connected);
                    SyncControls();
                }
            }
            return 0;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            UpdateTheme();
            if (g_hSlider) SetWindowTheme(g_hSlider, g_dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            HWND control = (HWND)lParam;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, control == g_hStatus && !g_statusGood ? RGB(225, 78, 78) : ColorText());
            bool onBackground = control == g_hTitle || control == g_hSubtitle || control == g_hStatus;
            HBRUSH brush = onBackground ? g_brushBg : g_brushSurface;
            return (LRESULT)(brush ? brush : GetStockObject(WHITE_BRUSH));
        }

        case WM_CTLCOLORBTN: {
            return (LRESULT)(g_brushSurface ? g_brushSurface : GetStockObject(WHITE_BRUSH));
        }

        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wParam;
            HWND control = (HWND)lParam;
            SetTextColor(dc, control == g_hY && !IsWindowEnabled(g_hY) ? ColorMuted() : ColorText());
            SetBkColor(dc, ColorField());
            return (LRESULT)(g_brushField ? g_brushField : GetStockObject(WHITE_BRUSH));
        }

        case WM_CLOSE: {
            if (g_dirty) {
                int result = MessageBoxW(hWnd, L"DPI 设置尚未应用，是否立即应用？",
                    L"DPI 设置", MB_YESNOCANCEL | MB_ICONQUESTION);
                if (result == IDCANCEL) return 0;
                if (result == IDYES) {
                    ApplyConfig();
                    if (g_dirty) return 0;
                }
            }
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hWnd, TIMER_CONNECTION);
            if (g_brushBg) DeleteObject(g_brushBg);
            if (g_brushSurface) DeleteObject(g_brushSurface);
            if (g_brushField) DeleteObject(g_brushField);
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_fontNormal) DeleteObject(g_fontNormal);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            g_brushBg = g_brushSurface = g_brushField = NULL;
            g_fontTitle = g_fontBold = g_fontNormal = g_fontSmall = NULL;
            ZeroMemory(g_slotButtons, sizeof(g_slotButtons));
            g_hX = g_hY = g_hSlider = g_hIndependent = NULL;
            g_hTitle = g_hSubtitle = g_hDetail = g_hStatus = NULL;
            g_hwnd = NULL;
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

bool GetQuickSlots(QuickSlotList& out) {
    out = QuickSlotList();
    Device::State state = Device::GetCurrentState();
    BatteryEstimate::DpiSpec spec;
    if (!BatteryEstimate::GetDpiSpecForState(state, spec, &out.modelKey, &out.modelLabel)) {
        return false;
    }
    int limit = std::min(std::max(1, spec.maxSlots), Device::MAX_DPI_SLOTS);

    if (state.isConnected) {
        Device::DpiConfig hardware;
        if (Device::ReadDpiConfig(hardware) && !hardware.dpiX.empty()) {
            int count = std::min({(int)hardware.dpiX.size(), (int)hardware.dpiY.size(), limit});
            for (int i = 0; i < count; ++i) {
                QuickSlot slot;
                slot.x = std::clamp(hardware.dpiX[(size_t)i], spec.minDpi, spec.maxDpi);
                slot.y = std::clamp(hardware.dpiY[(size_t)i], spec.minDpi, spec.maxDpi);
                out.slots.push_back(slot);
            }
            if (!out.slots.empty()) {
                out.activeIndex = std::clamp(hardware.currentIndex, 0, (int)out.slots.size() - 1);
                out.fromDevice = true;
                SaveQuickCache(out.modelKey, out.slots, out.activeIndex);
                return true;
            }
        }
    }

    if (LoadQuickCache(out.modelKey, limit, out.slots, out.activeIndex)) {
        for (QuickSlot& slot : out.slots) {
            slot.x = std::clamp(slot.x, spec.minDpi, spec.maxDpi);
            slot.y = std::clamp(slot.y, spec.minDpi, spec.maxDpi);
        }
        return true;
    }

    // First run (or a sleeping mouse without a cache) can still expose the live
    // DPI value already present in the status packet.
    if (state.dpiX > 0) {
        QuickSlot slot;
        slot.x = std::clamp(state.dpiX, spec.minDpi, spec.maxDpi);
        slot.y = std::clamp(state.dpiY > 0 ? state.dpiY : state.dpiX, spec.minDpi, spec.maxDpi);
        out.slots.push_back(slot);
        out.activeIndex = 0;
        return true;
    }
    return false;
}

bool SelectQuickSlot(int index) {
    if (index < 0 || !Device::GetCurrentState().isConnected) return false;
    return Device::SetDpiIndex(index);
}

void Show(HWND owner) {
    if (g_hwnd && IsWindow(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwnd);
        if (!g_dirty) LoadConfig();
        return;
    }
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    if (!g_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DpiWndProc;
        wc.hInstance = (HINSTANCE)GetModuleHandleW(NULL);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = WINDOW_CLASS;
        if (!RegisterClassExW(&wc)) return;
        g_classRegistered = true;
    }

    g_dpi = (int)GetSystemDpiSafe();
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;
    int maxDpiW = MulDiv(96, std::max(320, workW - 64), WINDOW_WIDTH);
    int maxDpiH = MulDiv(96, std::max(240, workH - 64), WINDOW_HEIGHT);
    g_dpi = std::max(1, std::min(g_dpi, std::min(maxDpiW, maxDpiH)));

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT rc = {0, 0, S(WINDOW_WIDTH), S(WINDOW_HEIGHT)};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_APPWINDOW);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int x = work.left + (workW - width) / 2;
    int y = work.top + (workH - height) / 2;
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"DPI 设置 - rapoo-tray",
        style, x, y, width, height, owner, NULL, (HINSTANCE)GetModuleHandleW(NULL), NULL);
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
            if (focused == g_hX || focused == g_hY) {
                CommitControls(true);
                return true;
            }
        }
        return IsDialogMessageW(g_hwnd, msg) != FALSE;
    }
    return false;
}

} // namespace DpiWindow

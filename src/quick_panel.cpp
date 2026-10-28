#include "quick_panel.h"

#include "device_manager.h"
#include "dpi_window.h"
#include "osd_config.h"
#include "profile_manager.h"
#include "rapoo_protocol.h"

#include <dwmapi.h>
#include <strsafe.h>
#include <winver.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

namespace QuickPanel {
namespace {

enum class TargetKind {
    None,
    Close,
    Profile,
    Dpi,
    Performance,
    Battery,
    Polling,
};

struct HitTarget {
    RECT rect = {0};
    TargetKind kind = TargetKind::None;
    int value = 0;
};

struct Palette {
    COLORREF paper;
    COLORREF panel;
    COLORREF ink;
    COLORREF ink2;
    COLORREF rule;
    COLORREF chip;
    COLORREF signal;
    COLORREF onSignal;
    COLORREF commandBar;
    COLORREF commandText;
    COLORREF commandMuted;
};

constexpr int MAX_VISIBLE_PROFILES = 5;
constexpr int COLUMN_WIDTH = 200;
constexpr int COLUMN_GAP = 16;
constexpr int OUTER_PADDING = 20;
constexpr int TOP_STRIP_HEIGHT = 3;
constexpr int COMMAND_BAR_HEIGHT = 39;
constexpr int HEADER_HEIGHT = 108;
constexpr int SECTION_TITLE_HEIGHT = 48;
constexpr int ROW_HEIGHT = 40;
constexpr int FOOTER_HEIGHT = 48;
constexpr int CUT = 7;

constexpr COLORREF KEY_COLOR = RGB(1, 2, 3);
constexpr COLORREF SIGNAL_ICE = RGB(0x7D, 0xE6, 0xFF);
constexpr COLORREF ON_SIGNAL = RGB(0x16, 0x16, 0x16);
constexpr COLORREF COMMAND_BAR = RGB(0x16, 0x16, 0x16);
constexpr COLORREF COMMAND_TEXT = RGB(0xED, 0xED, 0xED);
constexpr COLORREF TRI_MAGENTA = RGB(0xFF, 0x00, 0xF0);
constexpr COLORREF TRI_MINT = RGB(0x00, 0xFF, 0xA2);

constexpr int IDR_SAIRA_WIDE_SEMI_BOLD = 201;
constexpr int IDR_SAIRA_WIDE_EXTRA_BOLD = 202;
constexpr int IDR_SAIRA_FIELD = 203;

HWND g_window = NULL;
HWND g_mainWindow = NULL;
HINSTANCE g_instance = NULL;
bool g_initialized = false;
bool g_hotkeyRegistered = false;
bool g_visible = false;
OsdConfig::Config g_config;
Device::State g_state;
std::vector<Profiles::Definition> g_profiles;
DpiWindow::QuickSlotList g_dpi;
std::vector<HitTarget> g_hits;
std::vector<HANDLE> g_fontHandles;
TargetKind g_hoverKind = TargetKind::None;
int g_hoverValue = -1;
TargetKind g_focusKind = TargetKind::None;
int g_focusValue = 0;
int g_profileScroll = 0;
int g_width = 672;
int g_height = 432;
std::wstring g_message = L"当前项已标记；点击或按 Enter 切换";

int Scale(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

UINT WindowDpi(HWND window) {
    typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn getDpiForWindow = NULL;
    static bool resolved = false;
    if (!resolved) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) getDpiForWindow = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
        resolved = true;
    }
    UINT dpi = getDpiForWindow && window ? getDpiForWindow(window) : 0;
    return dpi ? dpi : 96;
}

bool IsSystemDark() {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) != ERROR_SUCCESS) return true;
    DWORD value = 1;
    DWORD type = 0;
    DWORD size = sizeof(value);
    bool dark = true;
    if (RegQueryValueExW(key, L"SystemUsesLightTheme", NULL, &type, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
        dark = value == 0;
    }
    RegCloseKey(key);
    return dark;
}

bool IsHighContrast() {
    HIGHCONTRASTW settings = {sizeof(settings)};
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0)) return false;
    return (settings.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

Palette MakePalette(bool dark) {
    if (IsHighContrast()) {
        return Palette{
            GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_GRAYTEXT),
            GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_BTNFACE),
            GetSysColor(COLOR_HIGHLIGHT),
            GetSysColor(COLOR_HIGHLIGHTTEXT),
            GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_WINDOW)
        };
    }
    if (dark) {
        return Palette{
            RGB(0x19, 0x19, 0x19),
            RGB(0x22, 0x22, 0x22),
            RGB(0xED, 0xED, 0xED),
            RGB(0xA3, 0xA3, 0xA3),
            RGB(0x35, 0x35, 0x35),
            RGB(0x2E, 0x2E, 0x2E),
            SIGNAL_ICE,
            ON_SIGNAL,
            COMMAND_BAR,
            COMMAND_TEXT,
            RGB(0xA3, 0xA3, 0xA3)
        };
    }
    return Palette{
        RGB(0xF4, 0xF4, 0xF2),
        RGB(0xFF, 0xFF, 0xFF),
        RGB(0x16, 0x16, 0x16),
        RGB(0x5C, 0x5C, 0x5C),
        RGB(0xD9, 0xD9, 0xD6),
        RGB(0xE4, 0xE4, 0xE0),
        SIGNAL_ICE,
        ON_SIGNAL,
        COMMAND_BAR,
        COMMAND_TEXT,
        RGB(0xA3, 0xA3, 0xA3)
    };
}

COLORREF Blend(COLORREF from, COLORREF to, int percentFrom) {
    percentFrom = std::clamp(percentFrom, 0, 100);
    const int percentTo = 100 - percentFrom;
    return RGB(
        (GetRValue(from) * percentFrom + GetRValue(to) * percentTo) / 100,
        (GetGValue(from) * percentFrom + GetGValue(to) * percentTo) / 100,
        (GetBValue(from) * percentFrom + GetBValue(to) * percentTo) / 100);
}

void FillSolid(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void FillCutRect(HDC dc, const RECT& rect, int cut, COLORREF color) {
    POINT points[5] = {
        {rect.left, rect.top},
        {rect.right, rect.top},
        {rect.right, rect.bottom - cut},
        {rect.right - cut, rect.bottom},
        {rect.left, rect.bottom},
    };
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, ARRAYSIZE(points));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void FrameSolid(HDC dc, const RECT& rect, COLORREF color, int width = 1) {
    HBRUSH brush = CreateSolidBrush(color);
    RECT current = rect;
    for (int i = 0; i < width; ++i) {
        FrameRect(dc, &current, brush);
        InflateRect(&current, -1, -1);
    }
    DeleteObject(brush);
}

void FrameCutRect(HDC dc, const RECT& rect, int cut, COLORREF color, int width) {
    POINT points[5] = {
        {rect.left, rect.top},
        {rect.right, rect.top},
        {rect.right, rect.bottom - cut},
        {rect.right - cut, rect.bottom},
        {rect.left, rect.bottom},
    };
    HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, ARRAYSIZE(points));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawHLine(HDC dc, const RECT& rect, int y, COLORREF color) {
    DrawLine(dc, rect.left, y, rect.right, y, color);
}

void DrawHatchBand(HDC dc, const RECT& rect, COLORREF color, int step) {
    int saved = SaveDC(dc);
    IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int height = rect.bottom - rect.top;
    for (int x = rect.left - height; x < rect.right; x += std::max(3, step)) {
        MoveToEx(dc, x, rect.bottom, NULL);
        LineTo(dc, x + height, rect.top);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    RestoreDC(dc, saved);
}

void DrawTextLine(HDC dc, const std::wstring& text, RECT rect, COLORREF color,
                  HFONT font, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, flags | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

HFONT MakeFont(int points, UINT dpi, int weight, const WCHAR* face) {
    return CreateFontW(-Scale(points, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, face);
}

std::wstring TwoDigits(int value) {
    WCHAR text[16] = {0};
    StringCchPrintfW(text, ARRAYSIZE(text), L"%02d", value);
    return text;
}

std::wstring FormatDpiText(const DpiWindow::QuickSlot& slot) {
    WCHAR text[64] = {0};
    if (slot.x == slot.y) StringCchPrintfW(text, ARRAYSIZE(text), L"%d DPI", slot.x);
    else StringCchPrintfW(text, ARRAYSIZE(text), L"%d × %d DPI", slot.x, slot.y);
    return text;
}

std::wstring AppVersion() {
    WCHAR path[MAX_PATH] = {0};
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return L"v--";
    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size) return L"v--";
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return L"v--";
    VS_FIXEDFILEINFO* info = NULL;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", (void**)&info, &infoSize) || !info) return L"v--";
    WCHAR version[48] = {0};
    StringCchPrintfW(version, ARRAYSIZE(version), L"v%d.%d.%d",
        HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS));
    return version;
}

void LoadEmbeddedFonts(HINSTANCE instance) {
    const int ids[] = {IDR_SAIRA_WIDE_SEMI_BOLD, IDR_SAIRA_WIDE_EXTRA_BOLD, IDR_SAIRA_FIELD};
    for (int id : ids) {
        HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!resource) continue;
        HGLOBAL loaded = LoadResource(instance, resource);
        if (!loaded) continue;
        const void* data = LockResource(loaded);
        DWORD size = SizeofResource(instance, resource);
        if (!data || !size) continue;
        DWORD fontCount = 0;
        HANDLE handle = AddFontMemResourceEx(const_cast<void*>(data), size, NULL, &fontCount);
        if (handle) g_fontHandles.push_back(handle);
    }
}

void UnloadEmbeddedFonts() {
    for (HANDLE handle : g_fontHandles) RemoveFontMemResourceEx(handle);
    g_fontHandles.clear();
}

void EnsureProfileScroll() {
    int maximum = std::max(0, (int)g_profiles.size() - MAX_VISIBLE_PROFILES);
    g_profileScroll = std::clamp(g_profileScroll, 0, maximum);
}

std::vector<TargetKind> VisibleSections() {
    std::vector<TargetKind> sections;
    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) sections.push_back(TargetKind::Profile);
    if (g_config.panelItems & OsdConfig::PANEL_DPI) sections.push_back(TargetKind::Dpi);
    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) sections.push_back(TargetKind::Performance);
    return sections;
}

int SectionCount(TargetKind kind) {
    if (kind == TargetKind::Profile) return (int)g_profiles.size();
    if (kind == TargetKind::Dpi) return std::min(Device::MAX_DPI_SLOTS, (int)g_dpi.slots.size());
    if (kind == TargetKind::Performance) return Rapoo::PERF_FURY_GAMING + 1;
    return 0;
}

bool ItemEnabled(TargetKind kind, int value) {
    if (kind == TargetKind::Performance) return Rapoo::IsPerfModeAllowed(value, g_state.pollingHz);
    return value >= 0 && value < SectionCount(kind);
}

bool ItemActive(TargetKind kind, int value) {
    if (kind == TargetKind::Profile && value >= 0 && value < (int)g_profiles.size()) {
        return g_profiles[(size_t)value].id == Profiles::ActiveId();
    }
    if (kind == TargetKind::Dpi) return value == g_dpi.activeIndex;
    if (kind == TargetKind::Performance) return g_state.perfModeKnown && value == g_state.perfMode;
    return false;
}

int PreferredValue(TargetKind kind) {
    if (kind == TargetKind::Profile) {
        const int active = Profiles::ActiveId();
        for (int i = 0; i < (int)g_profiles.size(); ++i) if (g_profiles[(size_t)i].id == active) return i;
        return 0;
    }
    if (kind == TargetKind::Dpi) return std::clamp(g_dpi.activeIndex, 0, std::max(0, SectionCount(kind) - 1));
    if (kind == TargetKind::Performance) {
        if (g_state.perfModeKnown && ItemEnabled(kind, g_state.perfMode)) return g_state.perfMode;
        for (int mode = 0; mode <= Rapoo::PERF_FURY_GAMING; ++mode) if (ItemEnabled(kind, mode)) return mode;
    }
    return 0;
}

void RevealFocusedProfile() {
    if (g_focusKind != TargetKind::Profile) return;
    if (g_focusValue < g_profileScroll) g_profileScroll = g_focusValue;
    if (g_focusValue >= g_profileScroll + MAX_VISIBLE_PROFILES) g_profileScroll = g_focusValue - MAX_VISIBLE_PROFILES + 1;
    EnsureProfileScroll();
}

void ResetKeyboardFocus() {
    const std::vector<TargetKind> sections = VisibleSections();
    g_focusKind = sections.empty() ? TargetKind::None : sections.front();
    g_focusValue = PreferredValue(g_focusKind);
    RevealFocusedProfile();
}

void AddHit(const RECT& rect, TargetKind kind, int value) {
    HitTarget hit;
    hit.rect = rect;
    hit.kind = kind;
    hit.value = value;
    g_hits.push_back(hit);
}

void BuildLayout(HWND window) {
    g_hits.clear();
    UINT dpi = WindowDpi(window);
    auto S = [dpi](int value) { return Scale(value, dpi); };
    const int visibleSections = (int)VisibleSections().size();
    const int contentWidth = visibleSections > 0
        ? visibleSections * COLUMN_WIDTH + std::max(0, visibleSections - 1) * COLUMN_GAP
        : 420;
    g_width = S(contentWidth + OUTER_PADDING * 2);

    int maxRows = 1;
    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) {
        maxRows = std::max(maxRows, std::min(MAX_VISIBLE_PROFILES, SectionCount(TargetKind::Profile)));
    }
    if (g_config.panelItems & OsdConfig::PANEL_DPI) {
        maxRows = std::max(maxRows, SectionCount(TargetKind::Dpi));
    }
    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) {
        maxRows = std::max(maxRows, Rapoo::PERF_FURY_GAMING + 1);
    }
    g_height = S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT + maxRows * ROW_HEIGHT + FOOTER_HEIGHT);

    RECT closeRect = {g_width - S(OUTER_PADDING + 34), S(8), g_width - S(OUTER_PADDING), S(42)};
    AddHit(closeRect, TargetKind::Close, 0);

    int x = S(OUTER_PADDING);
    const int itemTop = S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT);
    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) {
        EnsureProfileScroll();
        const int count = std::min(MAX_VISIBLE_PROFILES, SectionCount(TargetKind::Profile));
        for (int i = 0; i < count; ++i) {
            RECT row = {x, itemTop + i * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            AddHit(row, TargetKind::Profile, g_profileScroll + i);
        }
        x += S(COLUMN_WIDTH + COLUMN_GAP);
    }
    if (g_config.panelItems & OsdConfig::PANEL_DPI) {
        const int count = SectionCount(TargetKind::Dpi);
        for (int i = 0; i < count; ++i) {
            RECT row = {x, itemTop + i * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            AddHit(row, TargetKind::Dpi, i);
        }
        x += S(COLUMN_WIDTH + COLUMN_GAP);
    }
    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) {
        for (int mode = 0; mode <= Rapoo::PERF_FURY_GAMING; ++mode) {
            if (!ItemEnabled(TargetKind::Performance, mode)) continue;
            RECT row = {x, itemTop + mode * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (mode + 1) * S(ROW_HEIGHT)};
            AddHit(row, TargetKind::Performance, mode);
        }
    }
}

std::pair<TargetKind, int> HitTest(POINT point) {
    for (const HitTarget& hit : g_hits) {
        if (PtInRect(&hit.rect, point)) return {hit.kind, hit.value};
    }
    return {TargetKind::None, -1};
}

void ReloadData(HWND window) {
    g_state = Device::GetCurrentState();
    g_profiles = Profiles::GetAll();
    if (!DpiWindow::GetQuickSlots(g_dpi)) {
        g_dpi = DpiWindow::QuickSlotList();
        DpiWindow::QuickSlot slot;
        slot.x = std::max(1, g_state.dpiX);
        slot.y = std::max(1, g_state.dpiY);
        g_dpi.slots.push_back(slot);
        g_dpi.activeIndex = 0;
    }
    EnsureProfileScroll();
    BuildLayout(window);
}

void ApplyProfile(int index) {
    if (index < 0 || index >= (int)g_profiles.size()) return;
    const int id = g_profiles[(size_t)index].id;
    const bool ok = Profiles::Select(id, false);
    g_message = ok ? L"配置已切换并写入鼠标"
        : (Profiles::ActiveId() == id ? L"配置已选中；设备离线，连接后应用"
                                      : L"配置切换失败；请唤醒鼠标后重试");
}

void ApplyDpi(int index) {
    if (index < 0 || index >= (int)g_dpi.slots.size()) return;
    const DpiWindow::QuickSlot slot = g_dpi.slots[(size_t)index];
    const bool selected = DpiWindow::SelectQuickSlot(index);
    const bool saved = selected && Profiles::SetCurrentDpi(slot.x, slot.y);
    g_message = saved ? L"DPI 档位已切换并保存到当前配置"
        : (selected ? L"DPI 已切换；当前配置同步失败" : L"DPI 切换失败；请唤醒鼠标后重试");
}

void ApplyPerformance(int mode) {
    if (!Rapoo::IsValidPerfMode(mode) || !ItemEnabled(TargetKind::Performance, mode)) return;
    const bool ok = Profiles::SetCurrentPerformanceMode(mode);
    g_message = ok ? L"性能模式已切换并保存到当前配置" : L"性能模式切换失败；请唤醒鼠标后重试";
}

void ApplyFocused() {
    if (g_focusKind == TargetKind::Profile) ApplyProfile(g_focusValue);
    else if (g_focusKind == TargetKind::Dpi) ApplyDpi(g_focusValue);
    else if (g_focusKind == TargetKind::Performance) ApplyPerformance(g_focusValue);
}

void MoveFocusVertical(int direction) {
    const int count = SectionCount(g_focusKind);
    if (count <= 0) return;
    for (int step = 1; step <= count; ++step) {
        int candidate = (g_focusValue + direction * step) % count;
        if (candidate < 0) candidate += count;
        if (ItemEnabled(g_focusKind, candidate)) {
            g_focusValue = candidate;
            RevealFocusedProfile();
            BuildLayout(g_window);
            InvalidateRect(g_window, NULL, FALSE);
            return;
        }
    }
}

void MoveFocusHorizontal(int direction) {
    const std::vector<TargetKind> sections = VisibleSections();
    if (sections.empty()) return;
    int current = 0;
    for (int i = 0; i < (int)sections.size(); ++i) if (sections[(size_t)i] == g_focusKind) current = i;
    current = (current + direction + (int)sections.size()) % (int)sections.size();
    g_focusKind = sections[(size_t)current];
    g_focusValue = PreferredValue(g_focusKind);
    RevealFocusedProfile();
    BuildLayout(g_window);
    InvalidateRect(g_window, NULL, FALSE);
}

bool IsFocused(TargetKind kind, int value) {
    return GetFocus() == g_window && g_focusKind == kind && g_focusValue == value;
}

void Paint(HWND window) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    RECT client;
    GetClientRect(window, &client);
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
    UINT dpi = WindowDpi(window);
    auto S = [dpi](int value) { return Scale(value, dpi); };
    const bool highContrast = IsHighContrast();
    const Palette p = MakePalette(IsSystemDark());

    FillSolid(buffer, client, KEY_COLOR);
    FillSolid(buffer, client, p.paper);
    FrameSolid(buffer, client, p.rule);

    const int commandTop = S(TOP_STRIP_HEIGHT);
    const int commandBottom = S(COMMAND_BAR_HEIGHT + TOP_STRIP_HEIGHT);
    RECT command = {0, commandTop, client.right, commandBottom};
    FillSolid(buffer, command, p.commandBar);
    if (!highContrast) {
        RECT hatch = {client.right - S(270), commandTop + S(5), client.right - S(94), commandBottom - S(4)};
        DrawHatchBand(buffer, hatch, Blend(p.commandMuted, p.commandBar, 18), S(9));
    }
    const int third = client.right / 3;
    RECT stripe1 = {0, 0, third, S(TOP_STRIP_HEIGHT)};
    RECT stripe2 = {third, 0, third * 2, S(TOP_STRIP_HEIGHT)};
    RECT stripe3 = {third * 2, 0, client.right, S(TOP_STRIP_HEIGHT)};
    FillSolid(buffer, stripe1, highContrast ? p.ink : TRI_MAGENTA);
    FillSolid(buffer, stripe2, p.signal);
    FillSolid(buffer, stripe3, highContrast ? p.ink : TRI_MINT);

    HFONT titleFont = MakeFont(16, dpi, FW_EXTRABOLD, L"Rapoo Saira");
    HFONT eyebrowFont = MakeFont(8, dpi, FW_SEMIBOLD, L"Rapoo Saira");
    HFONT fieldFont = MakeFont(9, dpi, FW_SEMIBOLD, L"Rapoo Saira");
    HFONT figureFont = MakeFont(18, dpi, FW_EXTRABOLD, L"Rapoo Saira");
    HFONT bodyFont = MakeFont(12, dpi, FW_BOLD, L"Microsoft YaHei UI");
    HFONT bodyMediumFont = MakeFont(11, dpi, FW_SEMIBOLD, L"Microsoft YaHei UI");
    HFONT smallFont = MakeFont(9, dpi, FW_NORMAL, L"Microsoft YaHei UI");
    HFONT closeFont = MakeFont(16, dpi, FW_NORMAL, L"Rapoo Saira");

    RECT titleRect = {S(OUTER_PADDING), commandTop, client.right - S(190), commandBottom};
    DrawTextLine(buffer, L"RAPOO // QUICK CONTROL", titleRect, p.commandText, titleFont);
    const std::wstring version = AppVersion();
    RECT versionRect = {client.right - S(148), commandTop, client.right - S(62), commandBottom};
    DrawTextLine(buffer, version, versionRect, p.commandMuted, fieldFont,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    RECT closeRect = {client.right - S(OUTER_PADDING + 34), S(8), client.right - S(OUTER_PADDING), S(42)};
    if (g_hoverKind == TargetKind::Close || IsFocused(TargetKind::Close, 0)) {
        FillCutRect(buffer, closeRect, S(CUT), p.signal);
    }
    const COLORREF closeInk = (g_hoverKind == TargetKind::Close || IsFocused(TargetKind::Close, 0)) ? p.onSignal : p.commandText;
    DrawTextLine(buffer, L"×", closeRect, closeInk, closeFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int statusTop = commandBottom;
    RECT statusRect = {0, statusTop, client.right, S(HEADER_HEIGHT)};
    FillSolid(buffer, statusRect, p.panel);
    DrawHLine(buffer, statusRect, S(HEADER_HEIGHT) - 1, p.rule);

    int statusX = S(OUTER_PADDING);
    const int statusRight = client.right - S(OUTER_PADDING);
    const int statusContentWidth = statusRight - statusX;
    struct StatusCell { TargetKind kind; int weight; };
    std::vector<StatusCell> cells;
    if (g_config.panelItems & OsdConfig::PANEL_DETAILS) cells.push_back({TargetKind::Profile, 2});
    if (g_config.panelItems & OsdConfig::PANEL_BATTERY) cells.push_back({TargetKind::Battery, 1});
    if (g_config.panelItems & OsdConfig::PANEL_POLLING) cells.push_back({TargetKind::Dpi, 1});
    int totalWeight = 0;
    for (const auto& cell : cells) totalWeight += cell.weight;
    if (cells.empty()) {
        RECT empty = {statusX, statusTop + S(10), statusRight, S(HEADER_HEIGHT) - S(8)};
        DrawTextLine(buffer, L"在设置中心启用设备、电量或回报率显示", empty, p.ink2, bodyMediumFont);
    } else {
        for (size_t i = 0; i < cells.size(); ++i) {
            const int width = statusContentWidth * cells[i].weight / totalWeight;
            RECT cell = {statusX, statusTop + S(8), statusX + width, S(HEADER_HEIGHT) - S(7)};
            if (i > 0) DrawLine(buffer, cell.left, statusTop + S(10), cell.left, S(HEADER_HEIGHT) - S(11), p.rule);
            if (cells[i].kind == TargetKind::Profile) {
                RECT label = {cell.left + S(8), cell.top, cell.right - S(8), cell.top + S(14)};
                DrawTextLine(buffer, L"// DEVICE", label, p.ink2, eyebrowFont);
                const WCHAR* model = g_state.modelName[0] ? g_state.modelName : L"雷柏游戏鼠标";
                RECT value = {cell.left + S(8), cell.top + S(13), cell.right - S(8), cell.top + S(35)};
                DrawTextLine(buffer, model, value, p.ink, bodyFont);
                std::wstring mode = g_state.isConnected ? (g_state.isWired ? L"USB WIRED" : L"2.4G WIRELESS") : L"OFFLINE";
                mode += g_state.isConnected ? L" | CONNECTED" : L" | DISCONNECTED";
                RECT sub = {cell.left + S(8), cell.top + S(32), cell.right - S(8), cell.bottom};
                DrawTextLine(buffer, mode, sub, p.ink2, fieldFont);
            } else if (cells[i].kind == TargetKind::Battery) {
                RECT label = {cell.left + S(8), cell.top, cell.right - S(8), cell.top + S(14)};
                DrawTextLine(buffer, L"// BATTERY", label, p.ink2, eyebrowFont);
                WCHAR value[32] = {0};
                StringCchPrintfW(value, ARRAYSIZE(value), L"%d%%", g_state.battery);
                RECT figure = {cell.left + S(8), cell.top + S(12), cell.right - S(8), cell.top + S(38)};
                DrawTextLine(buffer, value, figure, p.ink, figureFont);
                if (g_state.isCharging) {
                    RECT charging = {cell.left + S(8), cell.top + S(37), cell.right - S(8), cell.bottom};
                    DrawTextLine(buffer, L"CHARGING", charging, p.ink2, fieldFont);
                }
            } else {
                RECT label = {cell.left + S(8), cell.top, cell.right - S(8), cell.top + S(14)};
                DrawTextLine(buffer, L"// POLLING", label, p.ink2, eyebrowFont);
                WCHAR value[32] = {0};
                StringCchPrintfW(value, ARRAYSIZE(value), L"%d HZ", g_state.pollingHz);
                RECT figure = {cell.left + S(8), cell.top + S(12), cell.right - S(8), cell.top + S(38)};
                DrawTextLine(buffer, value, figure, p.ink, figureFont);
            }
            statusX += width;
        }
    }

    const int bodyTop = S(HEADER_HEIGHT);
    const int footerTop = client.bottom - S(FOOTER_HEIGHT);
    RECT bodyRect = {0, bodyTop, client.right, footerTop};
    FillSolid(buffer, bodyRect, p.paper);
    DrawHLine(buffer, bodyRect, bodyTop, p.rule);
    DrawHLine(buffer, bodyRect, S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT) - 1, p.rule);

    int columnX = S(OUTER_PADDING);
    int sectionIndex = 0;
    auto drawSectionHeader = [&](TargetKind kind, const WCHAR* eyebrow, const WCHAR* chinese) {
        if (sectionIndex > 0) DrawLine(buffer, columnX - S(COLUMN_GAP / 2), bodyTop, columnX - S(COLUMN_GAP / 2), footerTop, p.rule);
        RECT eyebrowRect = {columnX, bodyTop + S(8), columnX + S(COLUMN_WIDTH) - S(52), bodyTop + S(24)};
        DrawTextLine(buffer, eyebrow, eyebrowRect, p.ink2, eyebrowFont);
        RECT titleRect = {columnX, bodyTop + S(23), columnX + S(COLUMN_WIDTH), bodyTop + S(44)};
        DrawTextLine(buffer, chinese, titleRect, p.ink, bodyFont);
        const int count = SectionCount(kind);
        const int total = kind == TargetKind::Profile ? 16 : (kind == TargetKind::Dpi ? Device::MAX_DPI_SLOTS : 6);
        WCHAR counter[24] = {0};
        StringCchPrintfW(counter, ARRAYSIZE(counter), L"%02d / %02d", count, total);
        RECT countRect = {columnX + S(COLUMN_WIDTH) - S(72), bodyTop + S(8), columnX + S(COLUMN_WIDTH), bodyTop + S(24)};
        DrawTextLine(buffer, counter, countRect, p.ink2, fieldFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (kind == TargetKind::Profile && count > MAX_VISIBLE_PROFILES) {
            RECT scrollRect = {columnX + S(COLUMN_WIDTH) - S(72), bodyTop + S(24), columnX + S(COLUMN_WIDTH), bodyTop + S(40)};
            DrawTextLine(buffer, L"SCROLL", scrollRect, p.ink2, eyebrowFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        ++sectionIndex;
    };

    auto drawRow = [&](const RECT& row, TargetKind kind, int value, const std::wstring& index,
                       const std::wstring& primary, const std::wstring& meta) {
        const bool active = ItemActive(kind, value);
        const bool enabled = ItemEnabled(kind, value);
        const bool hovered = g_hoverKind == kind && g_hoverValue == value;
        const bool focused = IsFocused(kind, value);
        COLORREF text = active ? p.onSignal : (enabled ? p.ink : p.ink2);
        COLORREF secondary = active ? p.onSignal : p.ink2;
        if (active) {
            FillCutRect(buffer, row, S(CUT), p.signal);
        } else if (hovered && enabled) {
            FillCutRect(buffer, row, S(CUT), p.chip);
            RECT trace = {row.left, row.top + S(4), row.left + S(4), row.bottom - S(4)};
            FillSolid(buffer, trace, p.signal);
            text = p.ink;
        }
        DrawHLine(buffer, row, row.bottom - 1, p.rule);
        if (!active) {
            DrawLine(buffer, row.right - S(6), row.bottom - 1, row.right - 1, row.bottom - S(6), p.rule);
        }
        const int shift = (hovered && !active && enabled) ? S(4) : 0;
        RECT indexRect = {row.left + S(12) + shift, row.top, row.left + S(42) + shift, row.bottom};
        DrawTextLine(buffer, index, indexRect, secondary, fieldFont);
        RECT primaryRect = {row.left + S(46) + shift, row.top, row.right - S(68), row.bottom};
        DrawTextLine(buffer, primary, primaryRect, text, bodyMediumFont);
        RECT metaRect = {row.right - S(74), row.top, row.right - S(12), row.bottom};
        DrawTextLine(buffer, meta, metaRect, secondary, fieldFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (focused) {
            RECT focusRect = {row.left + S(2), row.top + S(2), row.right - S(2), row.bottom - S(3)};
            FrameCutRect(buffer, focusRect, S(CUT) - S(2), active ? p.onSignal : p.ink, S(2));
        }
    };

    const int itemTop = S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT);
    columnX = S(OUTER_PADDING);
    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) {
        drawSectionHeader(TargetKind::Profile, L"// PROFILE", L"配置档案");
        const int count = std::min(MAX_VISIBLE_PROFILES, SectionCount(TargetKind::Profile));
        for (int i = 0; i < count; ++i) {
            const int value = g_profileScroll + i;
            const auto& profile = g_profiles[(size_t)value];
            RECT row = {columnX, itemTop + i * S(ROW_HEIGHT), columnX + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            WCHAR meta[32] = {0};
            StringCchPrintfW(meta, ARRAYSIZE(meta), L"%d HZ", profile.pollingHz);
            drawRow(row, TargetKind::Profile, value, TwoDigits(value + 1), profile.name, meta);
        }
        if (count == 0) {
            RECT empty = {columnX, itemTop, columnX + S(COLUMN_WIDTH), itemTop + S(ROW_HEIGHT)};
            DrawTextLine(buffer, L"尚未创建配置档案。", empty, p.ink2, smallFont);
        }
        columnX += S(COLUMN_WIDTH + COLUMN_GAP);
    }

    if (g_config.panelItems & OsdConfig::PANEL_DPI) {
        drawSectionHeader(TargetKind::Dpi, L"// DPI GEAR", L"DPI 档位");
        const int count = SectionCount(TargetKind::Dpi);
        for (int i = 0; i < count; ++i) {
            RECT row = {columnX, itemTop + i * S(ROW_HEIGHT), columnX + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            WCHAR meta[32] = {0};
            StringCchPrintfW(meta, ARRAYSIZE(meta), L"GEAR %02d", i + 1);
            drawRow(row, TargetKind::Dpi, i, TwoDigits(i + 1), FormatDpiText(g_dpi.slots[(size_t)i]), meta);
        }
        if (count == 0) {
            RECT empty = {columnX, itemTop, columnX + S(COLUMN_WIDTH), itemTop + S(ROW_HEIGHT)};
            DrawTextLine(buffer, L"DPI 数据尚未就绪。", empty, p.ink2, smallFont);
        }
        columnX += S(COLUMN_WIDTH + COLUMN_GAP);
    }

    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) {
        drawSectionHeader(TargetKind::Performance, L"// SENSOR MODE", L"性能模式");
        for (int mode = 0; mode <= Rapoo::PERF_FURY_GAMING; ++mode) {
            RECT row = {columnX, itemTop + mode * S(ROW_HEIGHT), columnX + S(COLUMN_WIDTH), itemTop + (mode + 1) * S(ROW_HEIGHT)};
            const bool active = ItemActive(TargetKind::Performance, mode);
            const bool enabled = ItemEnabled(TargetKind::Performance, mode);
            std::wstring meta = !enabled ? L"N/A" : (active ? L"CURRENT" : L"");
            drawRow(row, TargetKind::Performance, mode, TwoDigits(mode + 1), Rapoo::PerfModeName(mode), meta);
        }
    }
    RECT footer = {0, footerTop, client.right, client.bottom};
    FillSolid(buffer, footer, p.panel);
    DrawHLine(buffer, footer, footerTop, p.rule);
    RECT trace = {S(OUTER_PADDING), footerTop + S(16), S(OUTER_PADDING + 4), footerTop + S(32)};
    FillSolid(buffer, trace, p.signal);
    RECT messageLabel = {S(OUTER_PADDING + 12), footerTop + S(8), client.right - S(220), footerTop + S(22)};
    DrawTextLine(buffer, L"// LAST ACTION", messageLabel, p.ink2, eyebrowFont);
    RECT message = {S(OUTER_PADDING + 12), footerTop + S(21), client.right - S(220), footerTop + S(40)};
    DrawTextLine(buffer, g_message, message, p.ink, smallFont);

    const std::wstring hotkey = OsdConfig::FormatHotkey(g_config.hotkeyModifiers, g_config.hotkeyVirtualKey);
    RECT hotkeyRect = {client.right - S(210), footerTop + S(8), client.right - S(OUTER_PADDING), footerTop + S(22)};
    DrawTextLine(buffer, L"// HOTKEY", hotkeyRect, p.ink2, eyebrowFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT controls = {client.right - S(210), footerTop + S(21), client.right - S(OUTER_PADDING), footerTop + S(40)};
    const std::wstring footerKeys = hotkey + L"  |  ESC";
    DrawTextLine(buffer, footerKeys, controls, p.ink, fieldFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    DeleteObject(titleFont);
    DeleteObject(eyebrowFont);
    DeleteObject(fieldFont);
    DeleteObject(figureFont);
    DeleteObject(bodyFont);
    DeleteObject(bodyMediumFont);
    DeleteObject(smallFont);
    DeleteObject(closeFont);
    SelectObject(buffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window, &ps);
}

void PositionWindow(HWND window) {
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    int x = cursor.x - g_width / 2;
    int y = cursor.y + 18;
    if (y + g_height > info.rcWork.bottom) y = cursor.y - g_height - 18;
    x = std::clamp(x, (int)info.rcWork.left + 8, (int)info.rcWork.right - g_width - 8);
    y = std::clamp(y, (int)info.rcWork.top + 8, (int)info.rcWork.bottom - g_height - 8);
    SetWindowPos(window, HWND_TOPMOST, x, y, g_width, g_height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            InvalidateRect(window, NULL, TRUE);
            return 0;
        case WM_MOUSEMOVE: {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            auto [kind, value] = HitTest(point);
            if (kind != g_hoverKind || value != g_hoverValue) {
                g_hoverKind = kind;
                g_hoverValue = value;
                InvalidateRect(window, NULL, FALSE);
            }
            if (kind != TargetKind::None) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
                SetCursor(LoadCursorW(NULL, IDC_HAND));
            } else {
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_hoverKind = TargetKind::None;
            g_hoverValue = -1;
            InvalidateRect(window, NULL, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            auto [kind, value] = HitTest(point);
            if (kind == TargetKind::Close) {
                Hide();
                return 0;
            }
            if (kind != TargetKind::None) {
                g_focusKind = kind;
                g_focusValue = value;
                if (kind == TargetKind::Profile) ApplyProfile(value);
                else if (kind == TargetKind::Dpi) ApplyDpi(value);
                else if (kind == TargetKind::Performance) ApplyPerformance(value);
                ReloadData(window);
                InvalidateRect(window, NULL, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!(g_config.panelItems & OsdConfig::PANEL_PROFILES)) return 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_profileScroll += delta > 0 ? -1 : 1;
            EnsureProfileScroll();
            BuildLayout(window);
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                Hide();
                return 0;
            }
            if (wParam == VK_UP) {
                MoveFocusVertical(-1);
                return 0;
            }
            if (wParam == VK_DOWN) {
                MoveFocusVertical(1);
                return 0;
            }
            if (wParam == VK_LEFT) {
                MoveFocusHorizontal(-1);
                return 0;
            }
            if (wParam == VK_RIGHT || wParam == VK_TAB) {
                MoveFocusHorizontal((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                ApplyFocused();
                ReloadData(window);
                InvalidateRect(window, NULL, FALSE);
                return 0;
            }
            break;
        case WM_PAINT:
            Paint(window);
            return 0;
        case WM_DESTROY:
            g_window = NULL;
            g_visible = false;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void RegisterConfiguredHotkey() {
    if (g_hotkeyRegistered && g_mainWindow) {
        UnregisterHotKey(g_mainWindow, HOTKEY_ID);
        g_hotkeyRegistered = false;
    }
    if (!g_config.quickPanelEnabled || !g_mainWindow) return;
    if (RegisterHotKey(g_mainWindow, HOTKEY_ID,
            g_config.hotkeyModifiers | MOD_NOREPEAT, g_config.hotkeyVirtualKey)) {
        g_hotkeyRegistered = true;
        return;
    }
    if (RegisterHotKey(g_mainWindow, HOTKEY_ID,
            g_config.hotkeyModifiers, g_config.hotkeyVirtualKey)) {
        g_hotkeyRegistered = true;
    }
}

} // namespace

bool Init(HINSTANCE hInstance, HWND mainWindow) {
    if (g_initialized) return true;
    g_instance = hInstance;
    g_mainWindow = mainWindow;
    g_config = OsdConfig::Load();
    LoadEmbeddedFonts(hInstance);

    WNDCLASSEXW windowClass = {0};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"RapooQuickPanelWnd";
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        UnloadEmbeddedFonts();
        return false;
    }

    g_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        windowClass.lpszClassName, L"RapooQuickPanel", WS_POPUP,
        0, 0, g_width, g_height, NULL, NULL, hInstance, NULL);
    if (!g_window) {
        UnloadEmbeddedFonts();
        return false;
    }
    SetLayeredWindowAttributes(g_window, KEY_COLOR, 255, LWA_COLORKEY);
    DWORD corner = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(g_window, 33, &corner, sizeof(corner));
    BOOL dark = IsSystemDark();
    DwmSetWindowAttribute(g_window, 20, &dark, sizeof(dark));

    g_initialized = true;
    RegisterConfiguredHotkey();
    return true;
}

void Cleanup() {
    if (g_hotkeyRegistered && g_mainWindow) UnregisterHotKey(g_mainWindow, HOTKEY_ID);
    g_hotkeyRegistered = false;
    if (g_window && IsWindow(g_window)) DestroyWindow(g_window);
    g_window = NULL;
    g_initialized = false;
    g_visible = false;
    UnloadEmbeddedFonts();
}

bool ReloadConfig() {
    g_config = OsdConfig::Load();
    RegisterConfiguredHotkey();
    if (!g_config.quickPanelEnabled && g_visible) Hide();
    if (g_window) {
        BuildLayout(g_window);
        InvalidateRect(g_window, NULL, FALSE);
    }
    return g_hotkeyRegistered || !g_config.quickPanelEnabled;
}

void Toggle() {
    if (g_visible) Hide();
    else Show(false);
}

void Show(bool preview) {
    if (!g_window) return;
    if (!preview && !g_config.quickPanelEnabled) return;
    ReloadData(g_window);
    ResetKeyboardFocus();
    g_hoverKind = TargetKind::None;
    g_hoverValue = -1;
    PositionWindow(g_window);
    g_visible = true;
    ShowWindow(g_window, SW_SHOWNORMAL);
    SetForegroundWindow(g_window);
    SetFocus(g_window);
    InvalidateRect(g_window, NULL, TRUE);
}

void Hide() {
    if (!g_window) return;
    ShowWindow(g_window, SW_HIDE);
    g_visible = false;
}

bool IsVisible() {
    return g_visible;
}

bool HotkeyRegistered() {
    return g_hotkeyRegistered;
}

HWND GetWindowHandle() {
    return g_window;
}

} // namespace QuickPanel

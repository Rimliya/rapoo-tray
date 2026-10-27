#include "quick_panel.h"

#include "device_manager.h"
#include "dpi_window.h"
#include "osd_config.h"
#include "profile_manager.h"
#include "rapoo_protocol.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <strsafe.h>

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
};

struct HitTarget {
    RECT rect = {0};
    TargetKind kind = TargetKind::None;
    int value = 0;
};

constexpr int MAX_VISIBLE_PROFILES = 5;
constexpr int COLUMN_WIDTH = 190;
constexpr int COLUMN_GAP = 12;
constexpr int PADDING = 18;
constexpr int HEADER_HEIGHT = 82;
constexpr int SECTION_TITLE_HEIGHT = 28;
constexpr int ROW_HEIGHT = 32;
constexpr int FOOTER_HEIGHT = 42;
constexpr COLORREF KEY_COLOR = RGB(8, 10, 14);
constexpr COLORREF PANEL_COLOR = RGB(20, 24, 31);
constexpr COLORREF PANEL_BORDER = RGB(63, 72, 88);
constexpr COLORREF TEXT_PRIMARY = RGB(242, 247, 252);
constexpr COLORREF TEXT_SECONDARY = RGB(157, 171, 190);
constexpr COLORREF TEXT_MUTED = RGB(104, 117, 136);
constexpr COLORREF SIGNAL = RGB(119, 224, 255);
constexpr COLORREF SIGNAL_DIM = RGB(40, 74, 88);
constexpr COLORREF HOVER = RGB(36, 43, 54);
constexpr COLORREF ACTIVE = RGB(43, 53, 66);
constexpr COLORREF DANGER = RGB(255, 132, 114);
constexpr COLORREF SUCCESS = RGB(94, 217, 143);

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
TargetKind g_hoverKind = TargetKind::None;
int g_hoverValue = -1;
int g_profileScroll = 0;
int g_width = 630;
int g_height = 360;
ULONGLONG g_shownAt = 0;
std::wstring g_message = L"点击任意项目即可立即切换";

int Scale(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

UINT WindowDpi(HWND window) {
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
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

void FillRoundRect(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextLine(HDC dc, const std::wstring& text, RECT rect, COLORREF color,
                  HFONT font, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

std::wstring FormatDpiText(const DpiWindow::QuickSlot& slot) {
    WCHAR text[64] = {0};
    if (slot.x == slot.y) StringCchPrintfW(text, ARRAYSIZE(text), L"%d DPI", slot.x);
    else StringCchPrintfW(text, ARRAYSIZE(text), L"%d × %d DPI", slot.x, slot.y);
    return text;
}

int VisibleSectionCount() {
    int count = 0;
    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) ++count;
    if (g_config.panelItems & OsdConfig::PANEL_DPI) ++count;
    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) ++count;
    return count;
}

void EnsureProfileScroll() {
    int maximum = std::max(0, (int)g_profiles.size() - MAX_VISIBLE_PROFILES);
    g_profileScroll = std::clamp(g_profileScroll, 0, maximum);
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
    const int visibleSections = VisibleSectionCount();
    const int contentWidth = visibleSections > 0
        ? visibleSections * S(COLUMN_WIDTH) + std::max(0, visibleSections - 1) * S(COLUMN_GAP)
        : S(360);
    g_width = std::max(S(340), contentWidth + S(PADDING) * 2);
    int maxRows = 1;
    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) {
        maxRows = std::max(maxRows, std::min(MAX_VISIBLE_PROFILES, (int)g_profiles.size()));
    }
    if (g_config.panelItems & OsdConfig::PANEL_DPI) {
        maxRows = std::max(maxRows, std::min(Device::MAX_DPI_SLOTS, (int)g_dpi.slots.size()));
    }
    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) {
        maxRows = std::max(maxRows, Rapoo::PERF_FURY_GAMING + 1);
    }
    g_height = S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT + maxRows * ROW_HEIGHT + FOOTER_HEIGHT + 20);

    RECT closeRect = {g_width - S(PADDING + 24), S(12), g_width - S(PADDING), S(36)};
    AddHit(closeRect, TargetKind::Close, 0);

    int x = S(PADDING);
    const int itemTop = S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT);
    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) {
        EnsureProfileScroll();
        int count = std::min(MAX_VISIBLE_PROFILES, (int)g_profiles.size());
        for (int i = 0; i < count; ++i) {
            RECT row = {x, itemTop + i * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            AddHit(row, TargetKind::Profile, g_profileScroll + i);
        }
        x += S(COLUMN_WIDTH + COLUMN_GAP);
    }
    if (g_config.panelItems & OsdConfig::PANEL_DPI) {
        int count = std::min(Device::MAX_DPI_SLOTS, (int)g_dpi.slots.size());
        for (int i = 0; i < count; ++i) {
            RECT row = {x, itemTop + i * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            AddHit(row, TargetKind::Dpi, i);
        }
        x += S(COLUMN_WIDTH + COLUMN_GAP);
    }
    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) {
        for (int mode = 0; mode <= Rapoo::PERF_FURY_GAMING; ++mode) {
            RECT row = {x, itemTop + mode * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (mode + 1) * S(ROW_HEIGHT)};
            AddHit(row, TargetKind::Performance, mode);
        }
    }
}

std::pair<TargetKind, int> HitTest(const RECT& client, POINT point) {
    (void)client;
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
    g_message = ok ? L"配置已切换并写入鼠标" : (Profiles::ActiveId() == id ? L"配置已选中，设备离线时将在连接后应用" : L"配置切换失败，请唤醒鼠标重试");
}

void ApplyDpi(int index) {
    if (index < 0 || index >= (int)g_dpi.slots.size()) return;
    const DpiWindow::QuickSlot slot = g_dpi.slots[(size_t)index];
    const bool selected = DpiWindow::SelectQuickSlot(index);
    const bool saved = selected && Profiles::SetCurrentDpi(slot.x, slot.y);
    g_message = saved ? L"DPI 档位已切换" : (selected ? L"DPI 已切换，但当前配置同步失败" : L"DPI 切换失败，请唤醒鼠标重试");
}

void ApplyPerformance(int mode) {
    if (!Rapoo::IsValidPerfMode(mode)) return;
    const bool ok = Profiles::SetCurrentPerformanceMode(mode);
    g_message = ok ? L"性能模式已切换并保存到当前配置" : L"性能模式切换失败，请唤醒鼠标重试";
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

    HBRUSH keyBrush = CreateSolidBrush(KEY_COLOR);
    FillRect(buffer, &client, keyBrush);
    DeleteObject(keyBrush);

    RECT card = {S(2), S(2), client.right - S(2), client.bottom - S(2)};
    FillRoundRect(buffer, card, S(14), PANEL_COLOR);
    HBRUSH border = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN borderPen = CreatePen(PS_SOLID, 1, PANEL_BORDER);
    HGDIOBJ oldBrush = SelectObject(buffer, border);
    HGDIOBJ oldPen = SelectObject(buffer, borderPen);
    RoundRect(buffer, card.left, card.top, card.right, card.bottom, S(14), S(14));
    SelectObject(buffer, oldBrush);
    SelectObject(buffer, oldPen);
    DeleteObject(borderPen);

    HFONT titleFont = CreateFontW(-S(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
    HFONT bodyFont = CreateFontW(-S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
    HFONT smallFont = CreateFontW(-S(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
    HFONT labelFont = CreateFontW(-S(9), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Bahnschrift");

    RECT titleRect = {S(PADDING), S(12), client.right - S(50), S(36)};
    DrawTextLine(buffer, L"RAPOO // QUICK CONTROL", titleRect, TEXT_PRIMARY, titleFont);
    RECT closeRect = {client.right - S(PADDING + 24), S(10), client.right - S(PADDING), S(36)};
    if (g_hoverKind == TargetKind::Close) FillRoundRect(buffer, closeRect, S(6), HOVER);
    DrawTextLine(buffer, L"×", closeRect, TEXT_SECONDARY, bodyFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int badgeX = S(PADDING);
    const int badgeY = S(45);
    auto drawBadge = [&](const std::wstring& text, COLORREF color) {
        SIZE size = {0};
        HGDIOBJ oldFont = SelectObject(buffer, smallFont);
        GetTextExtentPoint32W(buffer, text.c_str(), (int)text.size(), &size);
        SelectObject(buffer, oldFont);
        int width = size.cx + S(16);
        RECT badge = {badgeX, badgeY, badgeX + width, badgeY + S(23)};
        FillRoundRect(buffer, badge, S(5), RGB(31, 37, 47));
        RECT textRect = badge;
        textRect.left += S(8);
        textRect.right -= S(8);
        DrawTextLine(buffer, text, textRect, color, smallFont);
        badgeX += width + S(7);
    };
    if (g_config.panelItems & OsdConfig::PANEL_DETAILS) {
        const WCHAR* model = g_state.modelName[0] ? g_state.modelName : L"雷柏游戏鼠标";
        std::wstring modelText = model;
        if (!g_state.isConnected) modelText += L" · OFFLINE";
        else modelText += g_state.isWired ? L" · USB" : L" · 2.4G";
        drawBadge(modelText, g_state.isConnected ? TEXT_PRIMARY : TEXT_MUTED);
    }
    if (g_config.panelItems & OsdConfig::PANEL_BATTERY) {
        WCHAR battery[32] = {0};
        StringCchPrintfW(battery, ARRAYSIZE(battery), L"BATTERY %d%%", g_state.battery);
        drawBadge(battery, g_state.isCharging ? SUCCESS : (g_state.battery <= 20 ? DANGER : TEXT_SECONDARY));
    }
    if (g_config.panelItems & OsdConfig::PANEL_POLLING) {
        WCHAR polling[32] = {0};
        StringCchPrintfW(polling, ARRAYSIZE(polling), L"%d HZ", g_state.pollingHz);
        drawBadge(polling, SIGNAL);
    }

    int x = S(PADDING);
    const int itemTop = S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT);
    auto drawSectionTitle = [&](const WCHAR* text, int columnX) {
        RECT rect = {columnX, S(HEADER_HEIGHT - 7), columnX + S(COLUMN_WIDTH), S(HEADER_HEIGHT + SECTION_TITLE_HEIGHT - 9)};
        DrawTextLine(buffer, text, rect, SIGNAL, labelFont);
    };
    auto drawRowBackground = [&](const RECT& row, bool active, bool hovered) {
        if (active || hovered) {
            RECT inset = {row.left, row.top + S(1), row.right, row.bottom - S(1)};
            FillRoundRect(buffer, inset, S(4), active ? ACTIVE : HOVER);
        }
        if (active) {
            RECT marker = {row.left, row.top + S(5), row.left + S(3), row.bottom - S(5)};
            FillRoundRect(buffer, marker, 0, SIGNAL);
        }
    };

    if (g_config.panelItems & OsdConfig::PANEL_PROFILES) {
        drawSectionTitle(L"// PROFILE", x);
        int count = std::min(MAX_VISIBLE_PROFILES, (int)g_profiles.size());
        for (int i = 0; i < count; ++i) {
            const int index = g_profileScroll + i;
            const auto& profile = g_profiles[(size_t)index];
            RECT row = {x, itemTop + i * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            bool active = profile.id == Profiles::ActiveId();
            bool hovered = g_hoverKind == TargetKind::Profile && g_hoverValue == index;
            drawRowBackground(row, active, hovered);
            RECT nameRect = {row.left + S(11), row.top, row.right - S(48), row.bottom};
            DrawTextLine(buffer, profile.name, nameRect, active ? TEXT_PRIMARY : TEXT_SECONDARY, bodyFont);
            WCHAR hz[24] = {0};
            StringCchPrintfW(hz, ARRAYSIZE(hz), L"%d Hz", profile.pollingHz);
            RECT hzRect = {row.right - S(52), row.top, row.right - S(8), row.bottom};
            DrawTextLine(buffer, hz, hzRect, active ? SIGNAL : TEXT_MUTED, labelFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        if (g_profiles.empty()) {
            RECT empty = {x, itemTop, x + S(COLUMN_WIDTH), itemTop + S(ROW_HEIGHT)};
            DrawTextLine(buffer, L"暂无配置", empty, TEXT_MUTED, smallFont);
        } else if (g_profiles.size() > MAX_VISIBLE_PROFILES) {
            WCHAR more[64] = {0};
            StringCchPrintfW(more, ARRAYSIZE(more), L"滚轮浏览 %d–%d / %d",
                g_profileScroll + 1, g_profileScroll + std::min<int>(MAX_VISIBLE_PROFILES, (int)g_profiles.size() - g_profileScroll),
                (int)g_profiles.size());
            RECT moreRect = {x, itemTop + MAX_VISIBLE_PROFILES * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + MAX_VISIBLE_PROFILES * S(ROW_HEIGHT) + S(18)};
            DrawTextLine(buffer, more, moreRect, TEXT_MUTED, smallFont);
        }
        x += S(COLUMN_WIDTH + COLUMN_GAP);
    }

    if (g_config.panelItems & OsdConfig::PANEL_DPI) {
        drawSectionTitle(L"// DPI GEAR", x);
        int count = std::min(Device::MAX_DPI_SLOTS, (int)g_dpi.slots.size());
        for (int i = 0; i < count; ++i) {
            RECT row = {x, itemTop + i * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (i + 1) * S(ROW_HEIGHT)};
            bool active = i == g_dpi.activeIndex;
            bool hovered = g_hoverKind == TargetKind::Dpi && g_hoverValue == i;
            drawRowBackground(row, active, hovered);
            RECT textRect = {row.left + S(11), row.top, row.right - S(52), row.bottom};
            DrawTextLine(buffer, FormatDpiText(g_dpi.slots[(size_t)i]), textRect,
                         active ? TEXT_PRIMARY : TEXT_SECONDARY, bodyFont);
            WCHAR gear[24] = {0};
            StringCchPrintfW(gear, ARRAYSIZE(gear), L"GEAR %d", i + 1);
            RECT gearRect = {row.right - S(68), row.top, row.right - S(8), row.bottom};
            DrawTextLine(buffer, gear, gearRect, active ? SIGNAL : TEXT_MUTED, labelFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        if (count == 0) {
            RECT empty = {x, itemTop, x + S(COLUMN_WIDTH), itemTop + S(ROW_HEIGHT)};
            DrawTextLine(buffer, L"DPI 数据尚未就绪", empty, TEXT_MUTED, smallFont);
        }
        x += S(COLUMN_WIDTH + COLUMN_GAP);
    }

    if (g_config.panelItems & OsdConfig::PANEL_PERFORMANCE) {
        drawSectionTitle(L"// SENSOR MODE", x);
        for (int mode = 0; mode <= Rapoo::PERF_FURY_GAMING; ++mode) {
            RECT row = {x, itemTop + mode * S(ROW_HEIGHT), x + S(COLUMN_WIDTH), itemTop + (mode + 1) * S(ROW_HEIGHT)};
            bool active = g_state.perfModeKnown && mode == g_state.perfMode;
            bool allowed = Rapoo::IsPerfModeAllowed(mode, g_state.pollingHz);
            bool hovered = g_hoverKind == TargetKind::Performance && g_hoverValue == mode && allowed;
            drawRowBackground(row, active, hovered);
            RECT textRect = {row.left + S(11), row.top, row.right - S(10), row.bottom};
            COLORREF color = !allowed ? TEXT_MUTED : (active ? TEXT_PRIMARY : TEXT_SECONDARY);
            DrawTextLine(buffer, Rapoo::PerfModeName(mode), textRect, color, bodyFont);
        }
    }

    RECT footerLine = {S(PADDING), client.bottom - S(FOOTER_HEIGHT), client.right - S(PADDING), client.bottom - S(FOOTER_HEIGHT) + 1};
    HBRUSH lineBrush = CreateSolidBrush(PANEL_BORDER);
    FillRect(buffer, &footerLine, lineBrush);
    DeleteObject(lineBrush);
    RECT messageRect = {S(PADDING), client.bottom - S(FOOTER_HEIGHT) + S(8), client.right - S(250), client.bottom - S(8)};
    DrawTextLine(buffer, g_message, messageRect, TEXT_SECONDARY, smallFont);
    WCHAR hint[128] = {0};
    StringCchPrintfW(hint, ARRAYSIZE(hint), L"%s · Esc 关闭",
        OsdConfig::FormatHotkey(g_config.hotkeyModifiers, g_config.hotkeyVirtualKey).c_str());
    RECT hintRect = {client.right - S(270), client.bottom - S(FOOTER_HEIGHT) + S(8), client.right - S(PADDING), client.bottom - S(8)};
    DrawTextLine(buffer, hint, hintRect, TEXT_MUTED, smallFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
    DeleteObject(smallFont);
    DeleteObject(labelFont);
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
            if (LOWORD(wParam) == WA_INACTIVE && g_visible && GetTickCount64() - g_shownAt > 500) Hide();
            return 0;
        case WM_MOUSEMOVE: {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT client;
            GetClientRect(window, &client);
            auto [kind, value] = HitTest(client, point);
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
            RECT client;
            GetClientRect(window, &client);
            auto [kind, value] = HitTest(client, point);
            if (kind == TargetKind::Close) {
                Hide();
                return 0;
            }
            if (kind == TargetKind::Profile) ApplyProfile(value);
            else if (kind == TargetKind::Dpi) ApplyDpi(value);
            else if (kind == TargetKind::Performance) ApplyPerformance(value);
            if (kind != TargetKind::None) {
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

    WNDCLASSEXW windowClass = {0};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"RapooQuickPanelWnd";
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    g_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        windowClass.lpszClassName, L"RapooQuickPanel", WS_POPUP,
        0, 0, g_width, g_height, NULL, NULL, hInstance, NULL);
    if (!g_window) return false;
    SetLayeredWindowAttributes(g_window, KEY_COLOR, 248, LWA_COLORKEY | LWA_ALPHA);
    DWORD corner = 2;
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
    PositionWindow(g_window);
    g_visible = true;
    g_shownAt = GetTickCount64();
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

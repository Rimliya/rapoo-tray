#include "about_window.h"
#include "tray_menu.h"

#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <winhttp.h>
#include <winver.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <new>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "winhttp.lib")

namespace About {
namespace {

constexpr const WCHAR* WINDOW_CLASS = L"RapooTrayAboutWnd";
constexpr const WCHAR* SOURCE_URL = L"https://github.com/Rimliya/rapoo-tray";
constexpr const WCHAR* SOURCE_LABEL = L"GitHub · Rimliya/rapoo-tray";
constexpr const WCHAR* RELEASES_URL = L"https://github.com/Rimliya/rapoo-tray/releases/latest";
constexpr int WINDOW_WIDTH = 500;
constexpr int WINDOW_HEIGHT = 342;
constexpr UINT WM_APP_UPDATE_FINISHED = WM_APP + 0x2701;

enum ControlId {
    IDC_SOURCE = 4101,
    IDC_CHECK_UPDATE,
    IDC_CLOSE,
    IDC_STATUS
};

enum class StatusKind {
    Neutral,
    Accent,
    Error
};

struct VersionNumber {
    int part[4] = { 0, 0, 0, 0 };
};

struct UpdateCheckResult {
    bool success = false;
    bool newer = false;
    std::wstring latestTag;
    std::wstring releaseUrl;
    std::wstring error;
};

struct UpdateCheckContext {
    HWND hwnd = NULL;
    std::wstring currentVersion;
};

HWND g_hwnd = NULL;
HWND g_hOwner = NULL;
HWND g_hSourceLink = NULL;
HWND g_hStatus = NULL;
HWND g_hCheckButton = NULL;
HWND g_hCloseButton = NULL;
HFONT g_fontNormal = NULL;
HFONT g_fontBold = NULL;
HFONT g_fontTitle = NULL;
HFONT g_fontSmall = NULL;
HFONT g_fontLink = NULL;
HBRUSH g_brushBg = NULL;
HBRUSH g_brushSurface = NULL;
HBRUSH g_brushField = NULL;
HICON g_icon = NULL;
int g_dpi = 96;
bool g_dark = false;
bool g_classRegistered = false;
bool g_checking = false;
bool g_updateAvailable = false;
std::wstring g_version;
std::wstring g_releaseUrl = RELEASES_URL;
std::wstring g_statusText;
StatusKind g_statusKind = StatusKind::Neutral;

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
COLORREF ColorError() { return g_dark ? RGB(255, 125, 125) : RGB(190, 48, 48); }

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

UINT GetMonitorDpiSafe(HMONITOR monitor) {
    HMODULE shcore = LoadLibraryExW(L"shcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (shcore) {
        typedef HRESULT (WINAPI *pfnGetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
        pfnGetDpiForMonitor fn =
            (pfnGetDpiForMonitor)GetProcAddress(shcore, "GetDpiForMonitor");
        if (fn && monitor) {
            UINT dpiX = 96;
            UINT dpiY = 96;
            if (SUCCEEDED(fn(monitor, 0, &dpiX, &dpiY)) && dpiX > 0) {
                FreeLibrary(shcore);
                return dpiX;
            }
        }
        FreeLibrary(shcore);
    }
    return GetSystemDpiSafe();
}

HFONT MakeFont(int px, int weight, bool underline = false) {
    LOGFONTW lf = {0};
    lf.lfHeight = -S(px);
    lf.lfWeight = weight;
    lf.lfUnderline = underline ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    StringCchCopyW(lf.lfFaceName, ARRAYSIZE(lf.lfFaceName), L"Microsoft YaHei UI");
    return CreateFontIndirectW(&lf);
}

void RebuildBrushes() {
    if (g_brushBg) DeleteObject(g_brushBg);
    if (g_brushSurface) DeleteObject(g_brushSurface);
    if (g_brushField) DeleteObject(g_brushField);
    g_brushBg = CreateSolidBrush(ColorBg());
    g_brushSurface = CreateSolidBrush(ColorSurface());
    g_brushField = CreateSolidBrush(ColorField());
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

void DrawTextLine(HDC dc, const WCHAR* text, RECT rect, HFONT font, COLORREF color,
                  UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    HGDIOBJ oldFont = SelectObject(dc, font ? font : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, flags);
    SelectObject(dc, oldFont);
}

void SetStatus(const WCHAR* text, StatusKind kind) {
    g_statusText = text ? text : L"";
    g_statusKind = kind;
    if (g_hStatus) SetWindowTextW(g_hStatus, g_statusText.c_str());
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

void UpdateTheme() {
    if (!g_hwnd) return;
    g_dark = Tray::IsDarkMode();
    RebuildBrushes();
    RECT rc = {0};
    GetWindowRect(g_hwnd, &rc);
    Tray::ApplyModernWindowStyle(g_hwnd, g_dark, rc.right - rc.left, rc.bottom - rc.top);
    RedrawWindow(g_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

std::wstring GetExecutableVersion() {
    WCHAR path[MAX_PATH] = {0};
    if (!GetModuleFileNameW(NULL, path, ARRAYSIZE(path))) return L"未知";

    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size) return L"未知";

    std::wstring data(size, L'\0');
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return L"未知";

    VS_FIXEDFILEINFO* info = NULL;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", (LPVOID*)&info, &infoSize) || !info) return L"未知";

    int major = HIWORD(info->dwFileVersionMS);
    int minor = LOWORD(info->dwFileVersionMS);
    int patch = HIWORD(info->dwFileVersionLS);
    int build = LOWORD(info->dwFileVersionLS);

    WCHAR buffer[64] = {0};
    if (build > 0) {
        StringCchPrintfW(buffer, ARRAYSIZE(buffer), L"%d.%d.%d.%d", major, minor, patch, build);
    } else {
        StringCchPrintfW(buffer, ARRAYSIZE(buffer), L"%d.%d.%d", major, minor, patch);
    }
    return buffer;
}

bool ParseVersion(const std::wstring& text, VersionNumber& out) {
    const WCHAR* p = text.c_str();
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'v' || *p == L'V') ++p;

    VersionNumber parsed;
    int count = swscanf(p, L"%d.%d.%d.%d",
        &parsed.part[0], &parsed.part[1], &parsed.part[2], &parsed.part[3]);
    if (count < 3) return false;
    out = parsed;
    return true;
}

int CompareVersions(const VersionNumber& a, const VersionNumber& b) {
    for (int i = 0; i < 4; ++i) {
        if (a.part[i] < b.part[i]) return -1;
        if (a.part[i] > b.part[i]) return 1;
    }
    return 0;
}

std::wstring WidenUtf8(const std::string& text) {
    if (text.empty()) return L"";
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0);
    if (length <= 0) return L"";
    std::wstring result((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), length);
    return result;
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";

    std::string result;
    for (++pos; pos < json.size(); ++pos) {
        char ch = json[pos];
        if (ch == '"') return result;
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (++pos >= json.size()) break;
        switch (json[pos]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: break;
        }
    }
    return "";
}

struct InternetHandle {
    HINTERNET value = NULL;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    ~InternetHandle() {
        if (value) WinHttpCloseHandle(value);
    }
};

bool QueryLatestRelease(std::wstring& latestTag, std::wstring& releaseUrl, std::wstring& error) {
    InternetHandle session(WinHttpOpen(
        L"rapoo-tray-update-check/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.value) {
        error = L"无法初始化网络连接";
        return false;
    }

    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 8000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    InternetHandle connection(WinHttpConnect(
        session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) {
        error = L"无法连接 GitHub";
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(
        connection.value,
        L"GET",
        L"/repos/Rimliya/rapoo-tray/releases/latest",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request.value) {
        error = L"无法创建更新请求";
        return false;
    }

    const WCHAR* headers =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";

    if (!WinHttpSendRequest(request.value, headers, (DWORD)-1L,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, NULL)) {
        error = L"无法连接更新服务";
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.value,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = L"更新服务暂时不可用";
        return false;
    }

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            error = L"读取更新信息失败";
            return false;
        }
        if (!available) break;

        DWORD toRead = std::min<DWORD>(available, 8192);
        std::string chunk(toRead, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk.data(), toRead, &read)) {
            error = L"读取更新信息失败";
            return false;
        }
        if (!read) break;
        chunk.resize(read);
        body += chunk;
        if (body.size() > 1024 * 1024) {
            error = L"更新信息过大";
            return false;
        }
    }

    latestTag = WidenUtf8(ExtractJsonString(body, "tag_name"));
    releaseUrl = WidenUtf8(ExtractJsonString(body, "html_url"));
    if (latestTag.empty()) {
        error = L"未找到最新版本信息";
        return false;
    }
    if (releaseUrl.empty()) releaseUrl = RELEASES_URL;
    return true;
}

DWORD WINAPI UpdateCheckThread(LPVOID parameter) {
    UpdateCheckContext* context = static_cast<UpdateCheckContext*>(parameter);
    UpdateCheckResult* result = new (std::nothrow) UpdateCheckResult();
    if (!result) {
        if (context) {
            HWND hwnd = context->hwnd;
            delete context;
            PostMessageW(hwnd, WM_APP_UPDATE_FINISHED, 0, 0);
        }
        return 0;
    }

    std::wstring latestTag;
    std::wstring releaseUrl;
    std::wstring error;
    result->success = QueryLatestRelease(latestTag, releaseUrl, error);
    result->latestTag = latestTag;
    result->releaseUrl = releaseUrl;
    result->error = error;

    if (result->success) {
        VersionNumber current;
        VersionNumber latest;
        if (!ParseVersion(context->currentVersion, current) || !ParseVersion(latestTag, latest)) {
            result->success = false;
            result->error = L"版本信息格式无效";
        } else {
            result->newer = CompareVersions(latest, current) > 0;
        }
    }

    HWND hwnd = context ? context->hwnd : NULL;
    delete context;
    if (!PostMessageW(hwnd, WM_APP_UPDATE_FINISHED, 0, (LPARAM)result)) {
        delete result;
    }
    return 0;
}

void StartUpdateCheck() {
    if (g_checking) return;

    VersionNumber current;
    if (!ParseVersion(g_version, current)) {
        SetStatus(L"无法读取当前版本号", StatusKind::Error);
        return;
    }

    g_checking = true;
    g_updateAvailable = false;
    SetStatus(L"正在检查 GitHub 最新发行版…", StatusKind::Neutral);
    SetWindowTextW(g_hCheckButton, L"检查中…");
    EnableWindow(g_hCheckButton, FALSE);

    UpdateCheckContext* context = new (std::nothrow) UpdateCheckContext();
    if (!context) {
        g_checking = false;
        EnableWindow(g_hCheckButton, TRUE);
        SetWindowTextW(g_hCheckButton, L"检查更新");
        SetStatus(L"无法启动更新检查", StatusKind::Error);
        return;
    }

    context->hwnd = g_hwnd;
    context->currentVersion = g_version;
    HANDLE thread = CreateThread(NULL, 0, UpdateCheckThread, context, 0, NULL);
    if (!thread) {
        delete context;
        g_checking = false;
        EnableWindow(g_hCheckButton, TRUE);
        SetWindowTextW(g_hCheckButton, L"检查更新");
        SetStatus(L"无法启动更新检查", StatusKind::Error);
        return;
    }
    CloseHandle(thread);
}

void OpenUrl(const WCHAR* url) {
    HINSTANCE result = ShellExecuteW(g_hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        SetStatus(L"无法打开链接，请检查默认浏览器", StatusKind::Error);
    }
}

void PaintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);

    HDC memory = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memory, bitmap);

    HBRUSH background = CreateSolidBrush(ColorBg());
    FillRect(memory, &client, background);
    DeleteObject(background);

    if (g_icon) {
        DrawIconEx(memory, S(24), S(22), g_icon, S(64), S(64), 0, NULL, DI_NORMAL);
    } else {
        RECT logo = { S(24), S(22), S(88), S(86) };
        FillRounded(memory, logo, ColorAccent(), ColorAccent(), S(14));
        DrawTextLine(memory, L"R", logo, g_fontTitle,
            g_dark ? RGB(10, 14, 20) : RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT titleRect = { S(104), S(22), S(460), S(55) };
    DrawTextLine(memory, L"rapoo-tray", titleRect, g_fontTitle, ColorText(),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    std::wstring versionText = L"版本 " + g_version;
    SIZE versionSize = {0};
    HGDIOBJ oldFont = SelectObject(memory, g_fontSmall);
    GetTextExtentPoint32W(memory, versionText.c_str(), (int)versionText.size(), &versionSize);
    SelectObject(memory, oldFont);
    RECT versionPill = { S(104), S(58), S(104) + versionSize.cx + S(24), S(82) };
    FillRounded(memory, versionPill, ColorField(), ColorBorder(), S(12));
    DrawTextLine(memory, versionText.c_str(), versionPill, g_fontSmall, ColorAccent(),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT subtitle = { S(104), S(86), S(466), S(110) };
    DrawTextLine(memory, L"雷柏 VT 系列游戏鼠标托盘工具", subtitle, g_fontNormal,
                 ColorMuted(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT card = { S(24), S(118), S(476), S(234) };
    FillRounded(memory, card, ColorSurface(), ColorBorder(), S(12));

    HBRUSH dotBrush = CreateSolidBrush(ColorAccent());
    HGDIOBJ oldBrush = SelectObject(memory, dotBrush);
    HGDIOBJ oldPen = SelectObject(memory, GetStockObject(NULL_PEN));
    Ellipse(memory, S(40), S(136), S(48), S(144));
    SelectObject(memory, oldBrush);
    SelectObject(memory, oldPen);
    DeleteObject(dotBrush);

    RECT sourceLabel = { S(56), S(128), S(142), S(152) };
    DrawTextLine(memory, L"项目源码", sourceLabel, g_fontBold, ColorMuted());

    HPEN separatorPen = CreatePen(PS_SOLID, 1, ColorBorder());
    HGDIOBJ oldLinePen = SelectObject(memory, separatorPen);
    MoveToEx(memory, S(40), S(168), NULL);
    LineTo(memory, S(460), S(168));
    SelectObject(memory, oldLinePen);
    DeleteObject(separatorPen);

    RECT footerLeft = { S(40), S(180), S(270), S(218) };
    DrawTextLine(memory, L"轻量 · 原生 · 开源", footerLeft, g_fontNormal, ColorText());
    RECT footerRight = { S(282), S(180), S(460), S(218) };
    DrawTextLine(memory, L"MIT License · © 2026 Iris", footerRight, g_fontSmall, ColorMuted(),
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(hdc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(hwnd, &ps);
}

void DrawOwnerButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    RECT rc = item->rcItem;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool focused = (item->itemState & ODS_FOCUS) != 0;
    bool primary = item->CtlID == IDC_CHECK_UPDATE;

    POINT cursor = {0};
    GetCursorPos(&cursor);
    bool hovered = WindowFromPoint(cursor) == item->hwndItem;

    WCHAR text[128] = {0};
    GetWindowTextW(item->hwndItem, text, ARRAYSIZE(text));

    COLORREF fill;
    COLORREF textColor;
    COLORREF border;
    if (disabled) {
        fill = ColorSurface();
        textColor = ColorMuted();
        border = ColorBorder();
    } else if (primary) {
        fill = pressed ? (g_dark ? RGB(73, 180, 230) : RGB(0, 92, 185)) : ColorAccent();
        textColor = g_dark ? RGB(10, 14, 20) : RGB(255, 255, 255);
        border = ColorAccent();
    } else {
        fill = (pressed || hovered) ? ColorHover() : ColorButton();
        textColor = ColorText();
        border = pressed ? ColorAccent() : ColorBorder();
    }

    FillRounded(dc, rc, fill, border, S(7));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, g_fontNormal);
    SetTextColor(dc, textColor);
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    if (focused && !disabled) DrawFocusRect(dc, &rc);
}

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        InvalidateRect(hwnd, NULL, FALSE);
    } else if (msg == WM_MOUSELEAVE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(hwnd, NULL, FALSE);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

HWND CreateOwnerButton(HWND parent, const WCHAR* text, int id, int x, int y, int w, int h) {
    HWND button = CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        S(x), S(y), S(w), S(h),
        parent, (HMENU)(INT_PTR)id, (HINSTANCE)GetModuleHandleW(NULL), NULL);
    if (button) {
        SendMessageW(button, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
        SetWindowSubclass(button, ButtonSubclassProc, 1, 0);
    }
    return button;
}

void CreateControls(HWND hwnd) {
    g_hSourceLink = CreateWindowExW(
        0, L"STATIC", SOURCE_LABEL,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
        S(146), S(128), S(310), S(24),
        hwnd, (HMENU)(INT_PTR)IDC_SOURCE, (HINSTANCE)GetModuleHandleW(NULL), NULL);
    if (g_hSourceLink) SendMessageW(g_hSourceLink, WM_SETFONT, (WPARAM)g_fontLink, TRUE);

    g_hStatus = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        S(24), S(244), S(452), S(26),
        hwnd, (HMENU)(INT_PTR)IDC_STATUS, (HINSTANCE)GetModuleHandleW(NULL), NULL);
    if (g_hStatus) SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_hCheckButton = CreateOwnerButton(hwnd, L"检查更新", IDC_CHECK_UPDATE, 24, 286, 124, 34);
    g_hCloseButton = CreateOwnerButton(hwnd, L"关闭", IDC_CLOSE, 390, 286, 86, 34);
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            g_dark = Tray::IsDarkMode();
            g_version = GetExecutableVersion();
            g_statusKind = StatusKind::Neutral;
            g_statusText = L"检查 GitHub Releases 上是否有新版本";
            g_icon = LoadIconW((HINSTANCE)GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));

            g_fontNormal = MakeFont(13, FW_NORMAL);
            g_fontBold = MakeFont(13, FW_SEMIBOLD);
            g_fontTitle = MakeFont(24, FW_BOLD);
            g_fontSmall = MakeFont(11, FW_NORMAL);
            g_fontLink = MakeFont(12, FW_NORMAL, true);
            RebuildBrushes();
            CreateControls(hwnd);
            SetWindowTextW(g_hStatus, g_statusText.c_str());
            UpdateTheme();
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintWindow(hwnd);
            return 0;

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = (DRAWITEMSTRUCT*)lParam;
            if (item && item->CtlType == ODT_BUTTON) {
                DrawOwnerButton(item);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            HWND control = (HWND)lParam;
            int id = control ? GetDlgCtrlID(control) : 0;
            SetBkMode(dc, TRANSPARENT);
            if (id == IDC_SOURCE) {
                SetTextColor(dc, ColorAccent());
                return (LRESULT)g_brushSurface;
            }
            if (id == IDC_STATUS) {
                if (g_statusKind == StatusKind::Error) SetTextColor(dc, ColorError());
                else if (g_statusKind == StatusKind::Accent) SetTextColor(dc, ColorAccent());
                else SetTextColor(dc, ColorMuted());
                return (LRESULT)g_brushBg;
            }
            SetTextColor(dc, ColorText());
            return (LRESULT)g_brushBg;
        }

        case WM_CTLCOLORBTN:
            return (LRESULT)g_brushBg;

        case WM_SETCURSOR: {
            HWND target = (HWND)wParam;
            if (LOWORD(lParam) == HTCLIENT && target == g_hSourceLink) {
                SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32649))); // IDC_HAND
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);
            if (id == IDC_SOURCE && notification == STN_CLICKED) {
                OpenUrl(SOURCE_URL);
                return 0;
            }
            if (id == IDC_CHECK_UPDATE && notification == BN_CLICKED) {
                if (g_updateAvailable) OpenUrl(g_releaseUrl.c_str());
                else StartUpdateCheck();
                return 0;
            }
            if (id == IDC_CLOSE && notification == BN_CLICKED) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_APP_UPDATE_FINISHED: {
            g_checking = false;
            EnableWindow(g_hCheckButton, TRUE);

            UpdateCheckResult* result = (UpdateCheckResult*)lParam;
            if (!result) {
                SetWindowTextW(g_hCheckButton, L"重新检查");
                SetStatus(L"更新检查失败，请稍后重试", StatusKind::Error);
                return 0;
            }

            if (!result->success) {
                SetWindowTextW(g_hCheckButton, L"重新检查");
                SetStatus(result->error.empty() ? L"更新检查失败，请稍后重试" : result->error.c_str(),
                          StatusKind::Error);
                delete result;
                return 0;
            }

            if (result->newer) {
                g_updateAvailable = true;
                g_releaseUrl = result->releaseUrl.empty() ? RELEASES_URL : result->releaseUrl;
                WCHAR text[192] = {0};
                StringCchPrintfW(text, ARRAYSIZE(text), L"发现新版本 %s，点击前往下载",
                                 result->latestTag.c_str());
                SetWindowTextW(g_hCheckButton, L"前往下载");
                SetStatus(text, StatusKind::Accent);
            } else {
                g_updateAvailable = false;
                SetWindowTextW(g_hCheckButton, L"再次检查");
                WCHAR text[192] = {0};
                StringCchPrintfW(text, ARRAYSIZE(text), L"已是最新版本（GitHub 最新 %s）",
                                 result->latestTag.c_str());
                SetStatus(text, StatusKind::Accent);
            }
            delete result;
            return 0;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            UpdateTheme();
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            if (g_brushBg) DeleteObject(g_brushBg);
            if (g_brushSurface) DeleteObject(g_brushSurface);
            if (g_brushField) DeleteObject(g_brushField);
            g_brushBg = g_brushSurface = g_brushField = NULL;
            if (g_fontNormal) DeleteObject(g_fontNormal);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            if (g_fontLink) DeleteObject(g_fontLink);
            g_fontNormal = g_fontBold = g_fontTitle = g_fontSmall = g_fontLink = NULL;
            g_hwnd = NULL;
            g_hSourceLink = g_hStatus = g_hCheckButton = g_hCloseButton = NULL;
            g_checking = false;
            g_updateAvailable = false;
            g_icon = NULL;
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void Show(HWND owner) {
    if (g_hwnd && IsWindow(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwnd);
        return;
    }

    g_hOwner = owner;

    HINSTANCE instance = (HINSTANCE)GetModuleHandleW(NULL);
    if (!g_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AboutWndProc;
        wc.hInstance = instance;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        wc.hbrBackground = NULL;
        wc.lpszClassName = WINDOW_CLASS;
        if (!RegisterClassExW(&wc)) return;
        g_classRegistered = true;
    }

    POINT cursor = {0};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT work = {0};
    if (GetMonitorInfoW(monitor, &mi)) work = mi.rcWork;
    if (IsRectEmpty(&work)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    g_dpi = (int)GetMonitorDpiSafe(monitor);
    g_dpi = std::min(g_dpi, MulDiv(96, std::max(320, (int)(work.right - work.left) - 40), WINDOW_WIDTH));
    g_dpi = std::min(g_dpi, MulDiv(96, std::max(260, (int)(work.bottom - work.top) - 40), WINDOW_HEIGHT));
    g_dpi = std::max(1, g_dpi);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_APPWINDOW;
    RECT rc = { 0, 0, S(WINDOW_WIDTH), S(WINDOW_HEIGHT) };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;

    HWND hwnd = CreateWindowExW(
        exStyle, WINDOW_CLASS, L"关于 rapoo-tray",
        style, x, y, width, height,
        owner, NULL, instance, NULL);
    if (hwnd) {
        g_hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
}

bool PreTranslateMessage(MSG* msg) {
    if (!g_hwnd || !IsWindow(g_hwnd) || !msg) return false;
    if (msg->hwnd == g_hwnd || IsChild(g_hwnd, msg->hwnd)) {
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

} // namespace About
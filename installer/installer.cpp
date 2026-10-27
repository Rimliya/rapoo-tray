#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <strsafe.h>
#include <stdbool.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <algorithm>
#include <string>
#include <vector>

#define IDR_APP_PAYLOAD      1001
#define IDR_SETTINGS_PAYLOAD 1002
#define IDR_SETTINGS_CONFIG  1003
#define IDR_WPF_UI           1004
#define IDR_WPF_UI_ABSTRACTIONS 1005
#define IDR_SYSTEM_MEMORY    1006
#define IDR_SYSTEM_BUFFERS   1007
#define IDR_SYSTEM_NUMERICS  1008
#define IDR_SYSTEM_UNSAFE    1009

#define IDC_BTN_INSTALL      2001
#define IDC_BTN_CANCEL       2002
#define IDC_CHK_AUTORUN      2003
#define IDC_CHK_DESKTOP      2004
#define IDC_CHK_LAUNCH       2005
#define IDC_EDT_PATH         2006
#define IDC_BTN_BROWSE       2007

static const WCHAR* APP_VERSION = L"1.14.0";

static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hChkAutoRun = NULL;
static HWND g_hChkDesktop = NULL;
static HWND g_hChkLaunch = NULL;
static HWND g_hEdtPath = NULL;
static HWND g_hBtnBrowse = NULL;
static HWND g_hBtnInstall = NULL;
static HWND g_hBtnCancel = NULL;
static HWND g_hHoverControl = NULL;
static bool g_isUpdate = false;
static bool g_isPreview = false;
static bool g_installComplete = false;
static bool g_optAutoRun = true;
static bool g_optDesktop = true;
static bool g_optLaunch = true;
static int g_progressPercent = -1;
static WCHAR g_detectedInstallDir[MAX_PATH] = {0};
static std::wstring g_statusText = L"准备就绪";
static std::wstring g_completedPath;

static HFONT g_hFontEyebrow = NULL;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontSection = NULL;
static HFONT g_hFontBody = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontButton = NULL;
static HFONT g_hFontBrand = NULL;
static HFONT g_hFontReadout = NULL;
static HFONT g_hFontWindowButton = NULL;
static HICON g_hIconLarge = NULL;
static HICON g_hIconSmall = NULL;
static HBRUSH g_hBrushPaper = NULL;
static HBRUSH g_hBrushPanel = NULL;
static HBRUSH g_hBrushInk = NULL;
static HBRUSH g_hBrushSignal = NULL;
static HBRUSH g_hBrushChip = NULL;
static HBRUSH g_hBrushRule = NULL;
static HBRUSH g_hBrushMuted = NULL;
static HBRUSH g_hBrushGrid = NULL;
static HBRUSH g_hBrushHatch = NULL;

static const COLORREF CLR_PAPER = RGB(240, 243, 245);
static const COLORREF CLR_PANEL = RGB(255, 255, 255);
static const COLORREF CLR_INK = RGB(22, 22, 22);
static const COLORREF CLR_MUTED = RGB(92, 92, 92);
static const COLORREF CLR_RULE = RGB(211, 218, 223);
static const COLORREF CLR_CHIP = RGB(224, 230, 234);
static const COLORREF CLR_SIGNAL = RGB(125, 230, 255);
static const COLORREF CLR_ON_SIGNAL = RGB(22, 22, 22);
static const COLORREF CLR_GRID = RGB(232, 236, 239);
static const COLORREF CLR_COMMAND_TEXT = RGB(237, 237, 237);

static int g_dpi = 96;
static double g_scale = 1.0;

static RECT g_rcCard = {0};
static RECT g_rcPathFrame = {0};
static RECT g_rcBrowse = {0};
static RECT g_rcInstall = {0};
static RECT g_rcCancel = {0};
static RECT g_rcClose = {0};
static RECT g_rcMinimize = {0};
static RECT g_rcCheckAutoRun = {0};
static RECT g_rcCheckDesktop = {0};
static RECT g_rcCheckLaunch = {0};

static const WCHAR* REG_RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* REG_UNINSTALL = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\rapoo-tray";
static const WCHAR* APP_NAME = L"rapoo-tray";

static void SetInstallControlsEnabled(bool enabled);

// The installer must stop every running tray instance and verify that the
// executable locks are released before it replaces any files.
static bool IsAppProcessName(const WCHAR* name) {
    return name && (_wcsicmp(name, L"rapoo-tray.exe") == 0 ||
                    _wcsicmp(name, L"RapooVT7Tray.exe") == 0);
}

static bool TerminateAppProcess(DWORD* outError = NULL) {
    DWORD lastError = 0;
    for (int round = 0; round < 5; ++round) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            if (outError) *outError = GetLastError();
            return false;
        }

        bool found = false;
        PROCESSENTRY32W entry = {0};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (!IsAppProcessName(entry.szExeFile)) continue;
                found = true;
                HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE, entry.th32ProcessID);
                if (!process) {
                    lastError = GetLastError();
                    continue;
                }
                if (!TerminateProcess(process, 0)) {
                    lastError = GetLastError();
                }
                DWORD wait = WaitForSingleObject(process, 1500);
                if (wait != WAIT_OBJECT_0) lastError = ERROR_TIMEOUT;
                CloseHandle(process);
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);

        if (!found) {
            Sleep(150);
            if (outError) *outError = 0;
            return true;
        }
        Sleep(300);
    }

    if (outError) *outError = lastError ? lastError : ERROR_TIMEOUT;
    return false;
}

// Create Shell Shortcut (.lnk)
static bool CreateShellShortcut(LPCWSTR targetPath, LPCWSTR lnkPath, LPCWSTR description, LPCWSTR iconPath) {
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (FAILED(hr)) return false;

    psl->SetPath(targetPath);
    psl->SetDescription(description);
    if (iconPath && iconPath[0] != L'\0') {
        psl->SetIconLocation(iconPath, 0);
    }

    IPersistFile* ppf = NULL;
    hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
    if (SUCCEEDED(hr)) {
        hr = ppf->Save(lnkPath, TRUE);
        ppf->Release();
    }
    psl->Release();
    return SUCCEEDED(hr);
}

// Extract payload resource to file
static bool ExtractPayloadToFile(WORD resourceId, LPCWSTR destPath) {
    HRSRC hRes = FindResourceW(g_hInstance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hMem = LoadResource(g_hInstance, hRes);
    if (!hMem) return false;

    DWORD size = SizeofResource(g_hInstance, hRes);
    LPVOID pData = LockResource(hMem);
    if (!pData || size == 0) return false;

    HANDLE hFile = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, pData, size, &written, NULL);
    CloseHandle(hFile);
    return (ok && written == size);
}

// Get installation target directory: %LOCALAPPDATA%\Programs\rapoo-tray
static bool GetInstallDir(WCHAR* outDir, DWORD maxLen) {
    WCHAR localApp[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp))) {
        return false;
    }
    StringCchPrintfW(outDir, maxLen, L"%s\\Programs\\rapoo-tray", localApp);
    return true;
}

// Uninstaller: read install dir saved by the installer at install time
static bool GetSavedInstallDir(WCHAR* outDir, DWORD maxLen) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_UNINSTALL, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD len = maxLen * sizeof(WCHAR);
    LSTATUS st = RegQueryValueExW(hKey, L"InstallLocation", NULL, NULL, (LPBYTE)outDir, &len);
    RegCloseKey(hKey);
    return (st == ERROR_SUCCESS && outDir[0] != 0);
}

static bool DirectoryHasApp(const WCHAR* dir) {
    if (!dir || !dir[0]) return false;
    WCHAR exe[MAX_PATH] = {0};
    StringCchPrintfW(exe, ARRAYSIZE(exe), L"%s\\rapoo-tray.exe", dir);
    DWORD attrs = GetFileAttributesW(exe);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Older/custom installations may only have the Run entry. Recover the directory
// from the executable path so updates never ask the user to choose it again.
static bool GetRunInstallDir(WCHAR* outDir, DWORD maxLen) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    WCHAR command[MAX_PATH * 2] = {0};
    DWORD size = sizeof(command), type = 0;
    LSTATUS st = RegQueryValueExW(hKey, APP_NAME, NULL, &type, (LPBYTE)command, &size);
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS || type != REG_SZ || !command[0]) return false;

    WCHAR* exe = command;
    while (*exe == L' ' || *exe == L'\t') ++exe;
    WCHAR* end = exe + wcslen(exe);
    while (end > exe && (end[-1] == L' ' || end[-1] == L'\t')) *--end = 0;
    if (*exe == L'"') ++exe;
    WCHAR* closingQuote = wcschr(exe, L'"');
    if (closingQuote) *closingQuote = 0;
    else {
        WCHAR* exeSuffix = wcsstr(exe, L".exe");
        if (exeSuffix) exeSuffix[4] = 0;
    }
    WCHAR* slash = wcsrchr(exe, L'\\');
    if (!slash) return false;
    *slash = 0;
    if (!exe[0]) return false;
    StringCchCopyW(outDir, maxLen, exe);
    return true;
}

static bool GetShortcutTargetDir(const WCHAR* lnkPath, WCHAR* outDir, DWORD maxLen) {
    if (!lnkPath || GetFileAttributesW(lnkPath) == INVALID_FILE_ATTRIBUTES) return false;
    IShellLinkW* link = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
            IID_IShellLinkW, (LPVOID*)&link))) return false;
    IPersistFile* file = NULL;
    bool found = false;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (LPVOID*)&file)) && file) {
        if (SUCCEEDED(file->Load(lnkPath, STGM_READ))) {
            WCHAR target[MAX_PATH] = {0};
            if (SUCCEEDED(link->GetPath(target, ARRAYSIZE(target), NULL, SLGP_RAWPATH))) {
                WCHAR* slash = wcsrchr(target, L'\\');
                if (slash) {
                    *slash = 0;
                    StringCchCopyW(outDir, maxLen, target);
                    found = DirectoryHasApp(outDir);
                }
            }
        }
        file->Release();
    }
    link->Release();
    return found;
}

static bool GetShortcutInstallDir(WCHAR* outDir, DWORD maxLen) {
    const int folders[] = {CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS, CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY};
    for (int folder : folders) {
        WCHAR dir[MAX_PATH] = {0};
        if (FAILED(SHGetFolderPathW(NULL, folder, NULL, 0, dir))) continue;
        WCHAR lnk[MAX_PATH] = {0};
        StringCchPrintfW(lnk, ARRAYSIZE(lnk), L"%s\\rapoo-tray.lnk", dir);
        if (GetShortcutTargetDir(lnk, outDir, maxLen)) return true;
    }
    return false;
}

static bool GetRunningAppDir(WCHAR* outDir, DWORD maxLen) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry = {0};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"rapoo-tray.exe") != 0 &&
                _wcsicmp(entry.szExeFile, L"RapooVT7Tray.exe") != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;
            WCHAR exe[MAX_PATH] = {0};
            DWORD size = ARRAYSIZE(exe);
            if (QueryFullProcessImageNameW(process, 0, exe, &size)) {
                WCHAR* slash = wcsrchr(exe, L'\\');
                if (slash) {
                    *slash = 0;
                    StringCchCopyW(outDir, maxLen, exe);
                    found = DirectoryHasApp(outDir);
                }
            }
            CloseHandle(process);
            if (found) break;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

static bool DetectInstalledDir(WCHAR* outDir, DWORD maxLen) {
    if (GetSavedInstallDir(outDir, maxLen) && DirectoryHasApp(outDir)) return true;
    if (GetRunInstallDir(outDir, maxLen) && DirectoryHasApp(outDir)) return true;
    if (GetRunningAppDir(outDir, maxLen)) return true;
    if (GetShortcutInstallDir(outDir, maxLen)) return true;
    if (GetInstallDir(outDir, maxLen) && DirectoryHasApp(outDir)) return true;
    return false;
}

static bool RunEntryExists() {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    WCHAR value[MAX_PATH * 2] = {0};
    DWORD size = sizeof(value), type = 0;
    LSTATUS st = RegQueryValueExW(hKey, APP_NAME, NULL, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);
    return st == ERROR_SUCCESS && value[0] != 0;
}

// Browse-for-folder dialog -> selected path or empty string on cancel
static bool BrowseForFolder(HWND hWnd, WCHAR* outDir, DWORD maxLen) {
    WCHAR buf[MAX_PATH] = {0};
    BROWSEINFOW bi = {0};
    bi.hwndOwner = hWnd;
    bi.lpszTitle = L"选择安装文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    bool ok = SHGetPathFromIDListW(pidl, buf) && buf[0] != 0;
    CoTaskMemFree(pidl);
    if (ok) StringCchCopyNW(outDir, maxLen, buf, maxLen - 1);
    return ok;
}

// ---------------------------------------------------------------------------
// Installer surface: one-page Endfield-flavoured industrial control panel.
// All labels are painted in a double-buffered client area; only the path edit,
// three switches and two actions remain native child controls.
// ---------------------------------------------------------------------------

static int S(int value) {
    return (int)(value >= 0 ? value * g_scale + 0.5 : value * g_scale - 0.5);
}

static UINT QuerySystemDpi() {
    HDC screen = GetDC(NULL);
    UINT dpi = screen ? (UINT)GetDeviceCaps(screen, LOGPIXELSX) : 96u;
    if (screen) ReleaseDC(NULL, screen);
    return dpi ? dpi : 96u;
}

static HFONT CreateUiFont(int logicalSize, int weight, const WCHAR* face) {
    return CreateFontW(-S(logicalSize), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, face);
}

static void DestroyUiResources() {
    HFONT* fonts[] = { &g_hFontEyebrow, &g_hFontTitle, &g_hFontSection, &g_hFontBody,
                       &g_hFontSmall, &g_hFontButton, &g_hFontBrand, &g_hFontReadout,
                       &g_hFontWindowButton };
    for (HFONT* font : fonts) {
        if (*font) DeleteObject(*font);
        *font = NULL;
    }
    HBRUSH* brushes[] = { &g_hBrushPaper, &g_hBrushPanel, &g_hBrushInk, &g_hBrushSignal,
                          &g_hBrushChip, &g_hBrushRule, &g_hBrushMuted, &g_hBrushGrid,
                          &g_hBrushHatch };
    for (HBRUSH* brush : brushes) {
        if (*brush) DeleteObject(*brush);
        *brush = NULL;
    }
    if (g_hIconLarge) DestroyIcon(g_hIconLarge);
    if (g_hIconSmall) DestroyIcon(g_hIconSmall);
    g_hIconLarge = NULL;
    g_hIconSmall = NULL;
}

static void CreateUiResources(HWND hWnd) {
    DestroyUiResources();
    g_hBrushPaper = CreateSolidBrush(CLR_PAPER);
    g_hBrushPanel = CreateSolidBrush(CLR_PANEL);
    g_hBrushInk = CreateSolidBrush(CLR_INK);
    g_hBrushSignal = CreateSolidBrush(CLR_SIGNAL);
    g_hBrushChip = CreateSolidBrush(CLR_CHIP);
    g_hBrushRule = CreateSolidBrush(CLR_RULE);
    g_hBrushMuted = CreateSolidBrush(CLR_MUTED);
    g_hBrushGrid = CreateSolidBrush(CLR_GRID);
    g_hBrushHatch = CreateSolidBrush(RGB(239, 242, 244));

    g_hFontEyebrow = CreateUiFont(10, FW_SEMIBOLD, L"Segoe UI");
    g_hFontTitle = CreateUiFont(28, FW_BOLD, L"Microsoft YaHei UI");
    g_hFontSection = CreateUiFont(16, FW_BOLD, L"Microsoft YaHei UI");
    g_hFontBody = CreateUiFont(12, FW_NORMAL, L"Microsoft YaHei UI");
    g_hFontSmall = CreateUiFont(10, FW_NORMAL, L"Microsoft YaHei UI");
    g_hFontButton = CreateUiFont(13, FW_SEMIBOLD, L"Microsoft YaHei UI");
    g_hFontBrand = CreateUiFont(15, FW_HEAVY, L"Segoe UI");
    g_hFontReadout = CreateUiFont(11, FW_SEMIBOLD, L"Segoe UI");
    g_hFontWindowButton = CreateUiFont(13, FW_NORMAL, L"Segoe UI Symbol");

    HICON icon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        S(32), S(32), LR_DEFAULTCOLOR);
    g_hIconLarge = icon ? icon : LoadIconW(g_hInstance, MAKEINTRESOURCEW(1));
    g_hIconSmall = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        S(20), S(20), LR_DEFAULTCOLOR);
    if (!g_hIconSmall) g_hIconSmall = g_hIconLarge;

    if (hWnd) {
        SendMessageW(g_hEdtPath, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
        SendMessageW(g_hBtnBrowse, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        SendMessageW(g_hBtnCancel, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
    }
}

static void FillRectColor(HDC dc, const RECT& rect, HBRUSH brush) {
    FillRect(dc, &rect, brush ? brush : g_hBrushPaper);
}

static void DrawRule(HDC dc, int left, int top, int right, int bottom, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, left, top, NULL);
    LineTo(dc, right, bottom);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void DrawUiText(HDC dc, const std::wstring& text, RECT rect, HFONT font,
                       COLORREF color, UINT flags, int characterExtra = 0) {
    if (!font || !g_hBrushPaper) return;
    HGDIOBJ oldFont = SelectObject(dc, font);
    int oldExtra = SetTextCharacterExtra(dc, characterExtra);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), (int)text.size(), &rect,
        flags | DT_NOPREFIX);
    SetTextCharacterExtra(dc, oldExtra);
    SelectObject(dc, oldFont);
}

static void DrawEyebrow(HDC dc, const std::wstring& text, int left, int top, int width,
                        COLORREF color = CLR_MUTED) {
    RECT rect = { left, top, left + width, top + S(18) };
    DrawUiText(dc, text, rect, g_hFontEyebrow, color,
        DT_LEFT | DT_TOP | DT_SINGLELINE, S(1));
}

static void DrawCutRect(HDC dc, const RECT& rect, int cut, HBRUSH fill, COLORREF border) {
    int width = (int)(rect.right - rect.left);
    int height = (int)(rect.bottom - rect.top);
    int c = std::max(0, std::min(cut, std::min(width, height) / 2));
    POINT points[5] = {
        { rect.left, rect.top },
        { rect.right, rect.top },
        { rect.right, rect.bottom - c },
        { rect.right - c, rect.bottom },
        { rect.left, rect.bottom }
    };
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, fill ? fill : (HBRUSH)GetStockObject(HOLLOW_BRUSH));
    Polygon(dc, points, 5);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void DrawGridArea(HDC dc, const RECT& area) {
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_GRID);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int step = S(32);
    for (int x = area.left; x < area.right; x += step) {
        MoveToEx(dc, x, area.top, NULL);
        LineTo(dc, x, area.bottom);
    }
    for (int y = area.top; y < area.bottom; y += step) {
        MoveToEx(dc, area.left, y, NULL);
        LineTo(dc, area.right, y);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void DrawHatchBand(HDC dc, const RECT& rect) {
    SaveDC(dc);
    SetBkColor(dc, RGB(239, 242, 244));
    SetTextColor(dc, CLR_RULE);
    HBRUSH brush = CreateHatchBrush(HS_BDIAGONAL, RGB(205, 213, 218));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    RestoreDC(dc, -1);
}

static void DrawStatusDot(HDC dc, int x, int y, COLORREF color) {
    RECT dot = { x, y, x + S(7), y + S(7) };
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &dot, brush);
    DeleteObject(brush);
}

static void DrawWindowButtons(HDC dc, const RECT& client) {
    (void)client;
    RECT minRect = g_rcMinimize;
    RECT closeRect = g_rcClose;
    bool minHover = g_hHoverControl == (HWND)(UINT_PTR)0xF001;
    bool closeHover = g_hHoverControl == (HWND)(UINT_PTR)0xF002;
    if (minHover) FillRectColor(dc, minRect, g_hBrushSignal);
    if (closeHover) FillRectColor(dc, closeRect, g_hBrushSignal);

    HPEN pen = CreatePen(PS_SOLID, std::max(1, S(1)), (minHover || closeHover) ? CLR_INK : CLR_COMMAND_TEXT);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int minY = (minRect.top + minRect.bottom) / 2;
    MoveToEx(dc, minRect.left + S(17), minY, NULL);
    LineTo(dc, minRect.right - S(17), minY);
    int closeX = (closeRect.left + closeRect.right) / 2;
    int closeY = (closeRect.top + closeRect.bottom) / 2;
    MoveToEx(dc, closeX - S(6), closeY - S(6), NULL);
    LineTo(dc, closeX + S(6), closeY + S(6));
    MoveToEx(dc, closeX + S(6), closeY - S(6), NULL);
    LineTo(dc, closeX - S(6), closeY + S(6));
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    (void)closeHover;
}

static void LayoutInstallerUi(HWND hWnd) {
    if (!hWnd) return;
    RECT client = {0};
    GetClientRect(hWnd, &client);
    int width = client.right;
    int height = client.bottom;
    int footerTop = height - S(46);
    int sidebarWidth = S(210);
    int cardLeft = S(238);
    int cardRight = width - S(28);
    int cardTop = S(168);
    int cardBottom = footerTop - S(16);

    g_rcCard = { cardLeft, cardTop, cardRight, cardBottom };
    g_rcClose = { width - S(46), 0, width, S(40) };
    g_rcMinimize = { width - S(86), 0, width - S(46), S(40) };

    int pathTop = cardTop + S(64);
    int pathHeight = S(42);
    int browseWidth = S(74);
    int contentLeft = cardLeft + S(20);
    int contentRight = cardRight - S(20);
    g_rcPathFrame = { contentLeft, pathTop, contentRight - browseWidth - S(8), pathTop + pathHeight };
    g_rcBrowse = { g_rcPathFrame.right + S(8), pathTop, contentRight, pathTop + pathHeight };

    int optionStart = cardTop + S(184);
    int rowHeight = S(44);
    int checkX = contentRight - S(40);
    g_rcCheckAutoRun = { checkX, optionStart + S(12), checkX + S(40), optionStart + S(32) };
    g_rcCheckDesktop = { checkX, optionStart + rowHeight + S(12), checkX + S(40), optionStart + rowHeight + S(32) };
    g_rcCheckLaunch = { checkX, optionStart + rowHeight * 2 + S(12), checkX + S(40), optionStart + rowHeight * 2 + S(32) };

    int buttonBottom = cardBottom - S(12);
    int buttonTop = buttonBottom - S(38);
    g_rcInstall = { contentRight - S(110), buttonTop, contentRight, buttonBottom };
    g_rcCancel = { g_rcInstall.left - S(8) - S(92), buttonTop, g_rcInstall.left - S(8), buttonBottom };

    if (g_hEdtPath) {
        RECT editRect = { g_rcPathFrame.left + S(12), g_rcPathFrame.top + S(8),
                          g_rcPathFrame.right - S(12), g_rcPathFrame.bottom - S(8) };
        if (g_isUpdate) editRect.right = contentRight - S(12);
        SetWindowPos(g_hEdtPath, NULL, editRect.left, editRect.top,
            editRect.right - editRect.left, editRect.bottom - editRect.top,
            SWP_NOZORDER | SWP_NOACTIVATE | (g_installComplete ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    }
    if (g_hBtnBrowse) {
        SetWindowPos(g_hBtnBrowse, NULL, g_rcBrowse.left, g_rcBrowse.top,
            g_rcBrowse.right - g_rcBrowse.left, g_rcBrowse.bottom - g_rcBrowse.top,
            SWP_NOZORDER | SWP_NOACTIVATE | ((g_installComplete || g_isUpdate) ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    }
    HWND checks[] = { g_hChkAutoRun, g_hChkDesktop, g_hChkLaunch };
    RECT checkRects[] = { g_rcCheckAutoRun, g_rcCheckDesktop, g_rcCheckLaunch };
    for (int i = 0; i < 3; ++i) {
        if (!checks[i]) continue;
        SetWindowPos(checks[i], NULL, checkRects[i].left, checkRects[i].top,
            checkRects[i].right - checkRects[i].left, checkRects[i].bottom - checkRects[i].top,
            SWP_NOZORDER | SWP_NOACTIVATE | (g_installComplete ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    }
    if (g_hBtnInstall) {
        SetWindowPos(g_hBtnInstall, NULL, g_rcInstall.left, g_rcInstall.top,
            g_rcInstall.right - g_rcInstall.left, g_rcInstall.bottom - g_rcInstall.top,
            SWP_NOZORDER | SWP_NOACTIVATE | (g_installComplete ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    }
    if (g_hBtnCancel) {
        SetWindowTextW(g_hBtnCancel, g_installComplete ? L"完成" : L"取消");
        SetWindowPos(g_hBtnCancel, NULL, g_rcCancel.left, g_rcCancel.top,
            g_rcCancel.right - g_rcCancel.left, g_rcCancel.bottom - g_rcCancel.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    (void)sidebarWidth;
}

static void SetInstallProgress(int percent, const std::wstring& status) {
    g_progressPercent = percent;
    g_statusText = status;
    if (g_hMainWnd) {
        InvalidateRect(g_hMainWnd, NULL, FALSE);
        UpdateWindow(g_hMainWnd);
    }
}

static bool IsOptionChecked(int id) {
    if (id == IDC_CHK_AUTORUN) return g_optAutoRun;
    if (id == IDC_CHK_DESKTOP) return g_optDesktop;
    if (id == IDC_CHK_LAUNCH) return g_optLaunch;
    return false;
}

static void ToggleOption(int id) {
    if (id == IDC_CHK_AUTORUN) g_optAutoRun = !g_optAutoRun;
    else if (id == IDC_CHK_DESKTOP) g_optDesktop = !g_optDesktop;
    else if (id == IDC_CHK_LAUNCH) g_optLaunch = !g_optLaunch;
}

static void DrawOwnerItem(const DRAWITEMSTRUCT* item) {
    if (!item || !item->hDC) return;
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    int id = (int)item->CtlID;
    bool hover = g_hHoverControl == item->hwndItem;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool focused = (item->itemState & ODS_FOCUS) != 0;

    if (id >= IDC_CHK_AUTORUN && id <= IDC_CHK_LAUNCH) {
        bool checked = IsOptionChecked(id);
        FillRectColor(dc, rect, g_hBrushPanel);
        int trackW = S(40), trackH = S(20);
        int trackX = rect.left + (rect.right - rect.left - trackW) / 2;
        int trackY = rect.top + (rect.bottom - rect.top - trackH) / 2;
        RECT track = { trackX, trackY, trackX + trackW, trackY + trackH };
        FillRectColor(dc, track, checked ? g_hBrushSignal : g_hBrushChip);
        HPEN border = CreatePen(PS_SOLID, 1, checked ? CLR_INK : CLR_RULE);
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, track.left, track.top, track.right, track.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(border);
        int knobX = checked ? track.right - S(17) : track.left + S(3);
        RECT knob = { knobX, track.top + S(3), knobX + S(14), track.top + S(17) };
        FillRectColor(dc, knob, checked ? g_hBrushInk : g_hBrushPanel);
        if (focused) {
            RECT focus = { track.left + S(2), track.top + S(2), track.right - S(2), track.bottom - S(2) };
            HPEN fp = CreatePen(PS_SOLID, 1, CLR_INK);
            HGDIOBJ op = SelectObject(dc, fp);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, focus.left, focus.top, focus.right, focus.bottom);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(fp);
        }
        return;
    }

    bool primary = id == IDC_BTN_INSTALL;
    HBRUSH fill = primary ? g_hBrushInk : g_hBrushChip;
    COLORREF textColor = primary ? CLR_PAPER : CLR_INK;
    if (disabled) {
        fill = g_hBrushChip;
        textColor = CLR_MUTED;
    } else if (hover || (item->itemState & ODS_SELECTED)) {
        fill = g_hBrushSignal;
        textColor = CLR_INK;
    }
    FillRectColor(dc, rect, g_hBrushPanel);
    DrawCutRect(dc, rect, S(5), fill, fill == g_hBrushChip ? CLR_RULE : CLR_INK);

    wchar_t label[128] = {0};
    GetWindowTextW(item->hwndItem, label, ARRAYSIZE(label));
    RECT textRect = rect;
    textRect.left += S(8);
    textRect.right -= S(13);
    DrawUiText(dc, label, textRect, g_hFontButton, textColor,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (focused && !disabled) {
        RECT focus = { rect.left + S(2), rect.top + S(2), rect.right - S(2), rect.bottom - S(2) };
        HPEN fp = CreatePen(PS_SOLID, 1, primary && !hover ? CLR_PAPER : CLR_INK);
        HGDIOBJ op = SelectObject(dc, fp);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, focus.left, focus.top, focus.right, focus.bottom);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(fp);
    }
}

static LRESULT CALLBACK InstallerChildSubclass(HWND hWnd, UINT msg, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (g_hHoverControl != hWnd) {
                g_hHoverControl = hWnd;
                InvalidateRect(hWnd, NULL, FALSE);
                if (g_hMainWnd) InvalidateRect(g_hMainWnd, &g_rcCard, FALSE);
            }
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&track);
            break;
        }
        case WM_MOUSELEAVE:
            if (g_hHoverControl == hWnd) {
                g_hHoverControl = NULL;
                InvalidateRect(hWnd, NULL, FALSE);
                if (g_hMainWnd) InvalidateRect(g_hMainWnd, &g_rcCard, FALSE);
            }
            break;
        case BM_CLICK: {
            int id = GetDlgCtrlID(hWnd);
            if (id >= IDC_CHK_AUTORUN && id <= IDC_CHK_LAUNCH) {
                ToggleOption(id);
                SendMessageW(hWnd, BM_SETCHECK, IsOptionChecked(id) ? BST_CHECKED : BST_UNCHECKED, 0);
                InvalidateRect(hWnd, NULL, FALSE);
                if (g_hMainWnd) InvalidateRect(g_hMainWnd, &g_rcCard, FALSE);
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            int id = GetDlgCtrlID(hWnd);
            if (id >= IDC_CHK_AUTORUN && id <= IDC_CHK_LAUNCH) {
                SetFocus(hWnd);
                SetCapture(hWnd);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            int id = GetDlgCtrlID(hWnd);
            if (id >= IDC_CHK_AUTORUN && id <= IDC_CHK_LAUNCH) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT client = {0};
                GetClientRect(hWnd, &client);
                ReleaseCapture();
                if (PtInRect(&client, pt)) {
                    ToggleOption(id);
                    SendMessageW(hWnd, BM_SETCHECK, IsOptionChecked(id) ? BST_CHECKED : BST_UNCHECKED, 0);
                    InvalidateRect(hWnd, NULL, FALSE);
                    if (g_hMainWnd) InvalidateRect(g_hMainWnd, &g_rcCard, FALSE);
                }
                return 0;
            }
            break;
        }
        case WM_KEYUP: {
            int id = GetDlgCtrlID(hWnd);
            if (id >= IDC_CHK_AUTORUN && id <= IDC_CHK_LAUNCH && wParam == VK_SPACE) {
                ToggleOption(id);
                SendMessageW(hWnd, BM_SETCHECK, IsOptionChecked(id) ? BST_CHECKED : BST_UNCHECKED, 0);
                InvalidateRect(hWnd, NULL, FALSE);
                if (g_hMainWnd) InvalidateRect(g_hMainWnd, &g_rcCard, FALSE);
                return 0;
            }
            break;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hWnd, NULL, FALSE);
            if (g_hMainWnd) InvalidateRect(g_hMainWnd, &g_rcPathFrame, FALSE);
            break;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                int id = GetDlgCtrlID(hWnd);
                SetCursor(LoadCursor(NULL, id == IDC_EDT_PATH ? IDC_IBEAM : IDC_HAND));
                return TRUE;
            }
            break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void PaintInstallerUi(HWND hWnd) {
    PAINTSTRUCT ps = {0};
    HDC target = BeginPaint(hWnd, &ps);
    RECT client = {0};
    GetClientRect(hWnd, &client);

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    FillRectColor(dc, client, g_hBrushPaper);

    int width = client.right;
    int height = client.bottom;
    int titleHeight = S(40);
    int accentHeight = S(3);
    int contentTop = titleHeight + accentHeight;
    int footerTop = height - S(46);
    int sidebarWidth = S(210);
    int cardLeft = S(238);
    int cardRight = width - S(28);

    RECT grid = { sidebarWidth, contentTop, width, footerTop };
    DrawGridArea(dc, grid);

    RECT titlebar = { 0, 0, width, titleHeight };
    FillRectColor(dc, titlebar, g_hBrushInk);
    if (g_hIconSmall) DrawIconEx(dc, S(17), S(9), g_hIconSmall, S(22), S(22), 0, NULL, DI_NORMAL);
    RECT brand = { S(48), S(5), width - S(100), titleHeight - S(3) };
    DrawUiText(dc, L"rapoo-tray", brand, g_hFontBrand, CLR_COMMAND_TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE, S(1));
    RECT setupTag = { S(165), S(6), S(255), titleHeight - S(4) };
    DrawUiText(dc, g_isUpdate ? L"UPDATER" : L"SETUP", setupTag, g_hFontEyebrow,
        RGB(176, 183, 188), DT_LEFT | DT_VCENTER | DT_SINGLELINE, S(1));

    RECT triLeft = { 0, titleHeight, width / 3, contentTop };
    RECT triMid = { width / 3, titleHeight, width * 2 / 3, contentTop };
    RECT triRight = { width * 2 / 3, titleHeight, width, contentTop };
    FillRectColor(dc, triLeft, g_hBrushInk);
    FillRectColor(dc, triMid, g_hBrushSignal);
    FillRectColor(dc, triRight, g_hBrushInk);
    DrawWindowButtons(dc, client);

    DrawRule(dc, sidebarWidth, contentTop, sidebarWidth, footerTop, CLR_RULE);

    RECT logoPlate = { S(18), S(58), S(62), S(102) };
    FillRectColor(dc, logoPlate, g_hBrushPanel);
    HPEN borderPen = CreatePen(PS_SOLID, 1, CLR_INK);
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, logoPlate.left, logoPlate.top, logoPlate.right, logoPlate.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(borderPen);
    if (g_hIconLarge) DrawIconEx(dc, S(26), S(66), g_hIconLarge, S(28), S(28), 0, NULL, DI_NORMAL);

    RECT brandName = { S(74), S(60), S(193), S(80) };
    DrawUiText(dc, L"RAPOO", brandName, g_hFontBrand, CLR_INK,
        DT_LEFT | DT_TOP | DT_SINGLELINE, S(1));
    RECT brandSub = { S(74), S(82), S(193), S(101) };
    DrawUiText(dc, L"安装与部署", brandSub, g_hFontSmall, CLR_MUTED,
        DT_LEFT | DT_TOP | DT_SINGLELINE);
    DrawRule(dc, S(18), S(116), S(192), S(116), CLR_RULE);

    int activeStep = 0;
    if (g_installComplete) activeStep = 2;
    else if (g_progressPercent >= 0) activeStep = g_progressPercent < 38 ? 0 : (g_progressPercent < 82 ? 1 : 2);
    const wchar_t* stepCodes[] = { L"01 / TARGET", L"02 / OPTIONS", L"03 / APPLY" };
    const wchar_t* stepNames[] = { L"安装位置", L"安装选项", L"写入文件" };
    const wchar_t* stepNotes[] = { L"程序文件落点", L"快捷方式与启动", g_isUpdate ? L"停止旧版并部署" : L"写入程序与组件" };
    for (int i = 0; i < 3; ++i) {
        int top = S(132) + i * S(62);
        RECT row = { S(10), top, S(200), top + S(54) };
        bool active = i == activeStep;
        if (active) FillRectColor(dc, row, g_hBrushSignal);
        if (active) {
            RECT trace = { row.left, row.top + S(5), row.left + S(3), row.bottom - S(5) };
            FillRectColor(dc, trace, g_hBrushInk);
        }
        DrawEyebrow(dc, stepCodes[i], S(21), top + S(8), S(170), active ? CLR_INK : CLR_MUTED);
        RECT name = { S(21), top + S(24), S(186), top + S(42) };
        DrawUiText(dc, stepNames[i], name, g_hFontSection, active ? CLR_INK : CLR_MUTED,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT note = { S(21), top + S(41), S(186), top + S(53) };
        DrawUiText(dc, stepNotes[i], note, g_hFontSmall, active ? CLR_MUTED : RGB(125, 130, 134),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    int modeTop = footerTop - S(104);
    DrawRule(dc, S(18), modeTop, S(192), modeTop, CLR_RULE);
    DrawEyebrow(dc, L"// MODE", S(18), modeTop + S(12), S(170));
    DrawStatusDot(dc, S(18), modeTop + S(40), CLR_SIGNAL);
    RECT modeLabel = { S(32), modeTop + S(34), S(192), modeTop + S(52) };
    DrawUiText(dc, g_isUpdate ? L"原地更新" : L"全新安装", modeLabel, g_hFontSection, CLR_INK,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT modeVersion = { S(18), modeTop + S(58), S(192), modeTop + S(76) };
    DrawUiText(dc, std::wstring(L"v") + APP_VERSION + L"  /  PER-USER", modeVersion,
        g_hFontEyebrow, CLR_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE, S(1));

    int headerLeft = S(238);
    DrawEyebrow(dc, g_installComplete ? L"// COMPLETE" : (g_isUpdate ? L"// UPDATER" : L"// INSTALLER"),
        headerLeft, S(58), S(330));
    RECT headerTitle = { headerLeft, S(72), cardRight - S(315), S(112) };
    DrawUiText(dc, g_installComplete ? (g_isUpdate ? L"更新完成" : L"安装完成") :
        (g_isUpdate ? L"更新 rapoo-tray" : L"安装 rapoo-tray"), headerTitle, g_hFontTitle, CLR_INK,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT headerSub = { headerLeft, S(113), cardRight - S(315), S(138) };
    DrawUiText(dc, g_installComplete ? (g_isUpdate ? L"文件已部署，现有配置与本地数据保持完整" : L"托盘程序、设置中心与依赖组件已部署完成") :
        (g_isUpdate ? L"保留配置，替换托盘程序、设置中心与设备组件" : L"部署托盘程序、设置中心与设备控制组件"),
        headerSub, g_hFontBody, CLR_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT command = { cardRight - S(292), S(68), cardRight, S(114) };
    FillRectColor(dc, command, g_hBrushInk);
    DrawStatusDot(dc, command.left + S(14), command.top + S(19), CLR_SIGNAL);
    RECT commandText = { command.left + S(30), command.top + S(7), command.left + S(164), command.bottom - S(4) };
    DrawUiText(dc, std::wstring(L"本机部署  │  v") + APP_VERSION, commandText, g_hFontReadout,
        CLR_COMMAND_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT commandRule = { command.left + S(176), command.top + S(13), command.left + S(177), command.bottom - S(13) };
    HBRUSH commandDivider = CreateSolidBrush(RGB(70, 70, 70));
    FillRectColor(dc, commandRule, commandDivider);
    DeleteObject(commandDivider);
    RECT commandTech = { command.left + S(190), command.top + S(7), command.right - S(12), command.bottom - S(4) };
    DrawUiText(dc, L"WPF + WIN32", commandTech, g_hFontEyebrow,
        RGB(190, 197, 202), DT_LEFT | DT_VCENTER | DT_SINGLELINE, S(1));

    FillRectColor(dc, g_rcCard, g_hBrushPanel);
    HPEN cardPen = CreatePen(PS_SOLID, 1, CLR_RULE);
    oldPen = SelectObject(dc, cardPen);
    oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, g_rcCard.left, g_rcCard.top, g_rcCard.right, g_rcCard.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(cardPen);

    RECT modeChip = { g_rcCard.right - S(100), g_rcCard.top + S(18), g_rcCard.right - S(20), g_rcCard.top + S(40) };
    FillRectColor(dc, modeChip, g_installComplete ? g_hBrushInk : g_hBrushSignal);
    RECT modeChipText = modeChip;
    DrawUiText(dc, g_installComplete ? L"已完成" : (g_isUpdate ? L"原地更新" : L"全新安装"), modeChipText,
        g_hFontSmall, g_installComplete ? CLR_PANEL : CLR_INK, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (!g_installComplete) {
        DrawEyebrow(dc, L"// TARGET", g_rcCard.left + S(20), g_rcCard.top + S(18), S(260));
        RECT targetTitle = { g_rcCard.left + S(20), g_rcCard.top + S(34), g_rcCard.right - S(120), g_rcCard.top + S(57) };
        DrawUiText(dc, L"安装位置", targetTitle, g_hFontSection, CLR_INK,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        FillRectColor(dc, g_rcPathFrame, g_hBrushPanel);
        HPEN pathPen = CreatePen(PS_SOLID, 1, CLR_RULE);
        oldPen = SelectObject(dc, pathPen);
        oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, g_rcPathFrame.left, g_rcPathFrame.top, g_rcPathFrame.right, g_rcPathFrame.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pathPen);
        if (GetFocus() == g_hEdtPath) {
            RECT focusLine = { g_rcPathFrame.left, g_rcPathFrame.bottom - S(2), g_rcPathFrame.right, g_rcPathFrame.bottom };
            FillRectColor(dc, focusLine, g_hBrushSignal);
        }
        RECT pathHint = { g_rcPathFrame.left, g_rcPathFrame.bottom + S(7), g_rcCard.right - S(20), g_rcPathFrame.bottom + S(25) };
        DrawUiText(dc, L"程序、设置中心与本地配置将写入此目录。", pathHint, g_hFontSmall, CLR_MUTED,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        DrawRule(dc, g_rcCard.left + S(20), g_rcCard.top + S(128), g_rcCard.right - S(20), g_rcCard.top + S(128), CLR_RULE);
        DrawEyebrow(dc, L"// OPTIONS", g_rcCard.left + S(20), g_rcCard.top + S(144), S(260));
        RECT optionTitle = { g_rcCard.left + S(20), g_rcCard.top + S(160), g_rcCard.right - S(20), g_rcCard.top + S(182) };
        DrawUiText(dc, L"安装选项", optionTitle, g_hFontSection, CLR_INK,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const wchar_t* optionNames[] = { L"开机自动启动", L"创建桌面快捷方式", L"完成后立即运行" };
        const wchar_t* optionNotes[] = { L"登录 Windows 后自动启动托盘程序。", L"在桌面放置 rapoo-tray 快捷方式。", L"部署完成后直接启动托盘程序。" };
        int optionStart = g_rcCard.top + S(184);
        for (int i = 0; i < 3; ++i) {
            int top = optionStart + i * S(44);
            if (i > 0) DrawRule(dc, g_rcCard.left + S(20), top, g_rcCard.right - S(20), top, CLR_RULE);
            RECT name = { g_rcCard.left + S(22), top + S(5), g_rcCard.right - S(78), top + S(23) };
            DrawUiText(dc, optionNames[i], name, g_hFontBody, CLR_INK,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT note = { g_rcCard.left + S(22), top + S(22), g_rcCard.right - S(78), top + S(40) };
            DrawUiText(dc, optionNotes[i], note, g_hFontSmall, CLR_MUTED,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        RECT note = { g_rcCard.left + S(20), g_rcInstall.top, g_rcCancel.left - S(12), g_rcInstall.bottom };
        DrawUiText(dc, g_isUpdate ? L"升级将保留现有配置、快捷方式与开机启动状态。" :
            L"配置档案保存在当前用户账户；升级与卸载默认保留。", note, g_hFontSmall, CLR_MUTED,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        RECT doneEyebrow = { g_rcCard.left + S(20), g_rcCard.top + S(24), g_rcCard.right - S(120), g_rcCard.top + S(44) };
        DrawUiText(dc, L"// RESULT", doneEyebrow, g_hFontEyebrow, CLR_MUTED,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE, S(1));
        RECT doneTitle = { g_rcCard.left + S(20), g_rcCard.top + S(44), g_rcCard.right - S(120), g_rcCard.top + S(74) };
        DrawUiText(dc, g_isUpdate ? L"更新已写入" : L"安装已写入", doneTitle, g_hFontTitle, CLR_INK,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT badge = { g_rcCard.left + S(20), g_rcCard.top + S(94), g_rcCard.left + S(88), g_rcCard.top + S(162) };
        FillRectColor(dc, badge, g_hBrushSignal);
        HPEN tickPen = CreatePen(PS_SOLID, std::max(2, S(3)), CLR_INK);
        oldPen = SelectObject(dc, tickPen);
        MoveToEx(dc, badge.left + S(19), badge.top + S(34), NULL);
        LineTo(dc, badge.left + S(31), badge.top + S(46));
        LineTo(dc, badge.left + S(51), badge.top + S(23));
        SelectObject(dc, oldPen);
        DeleteObject(tickPen);

        RECT doneLead = { g_rcCard.left + S(108), g_rcCard.top + S(96), g_rcCard.right - S(20), g_rcCard.top + S(122) };
        DrawUiText(dc, L"rapoo-tray 已部署到本机", doneLead, g_hFontSection, CLR_INK,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT donePath = { g_rcCard.left + S(108), g_rcCard.top + S(124), g_rcCard.right - S(20), g_rcCard.top + S(164) };
        DrawUiText(dc, g_completedPath, donePath, g_hFontSmall, CLR_MUTED,
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        DrawRule(dc, g_rcCard.left + S(20), g_rcCard.top + S(184), g_rcCard.right - S(20), g_rcCard.top + S(184), CLR_RULE);
        RECT metric1 = { g_rcCard.left + S(20), g_rcCard.top + S(198), g_rcCard.left + S(180), g_rcCard.top + S(220) };
        RECT metric2 = { g_rcCard.left + S(200), g_rcCard.top + S(198), g_rcCard.left + S(360), g_rcCard.top + S(220) };
        RECT metric3 = { g_rcCard.left + S(380), g_rcCard.top + S(198), g_rcCard.right - S(20), g_rcCard.top + S(220) };
        DrawUiText(dc, std::wstring(L"版本  v") + APP_VERSION, metric1, g_hFontReadout, CLR_INK,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawUiText(dc, g_isUpdate ? L"模式  原地更新" : L"模式  全新安装", metric2, g_hFontBody, CLR_INK,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawUiText(dc, g_isUpdate ? L"配置  已保留" : L"配置  已初始化", metric3, g_hFontBody, CLR_INK,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT doneNote = { g_rcCard.left + S(20), g_rcInstall.top, g_rcCancel.left - S(12), g_rcInstall.bottom };
        DrawUiText(dc, L"配置与设备档案保存在当前用户账户。", doneNote, g_hFontSmall, CLR_MUTED,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    RECT footer = { 0, footerTop, width, height };
    FillRectColor(dc, footer, g_hBrushPaper);
    DrawRule(dc, 0, footerTop, width, footerTop, CLR_RULE);
    if (g_progressPercent >= 0) {
        int progressRight = width * std::max(0, std::min(100, g_progressPercent)) / 100;
        RECT progress = { 0, footerTop, progressRight, footerTop + S(2) };
        FillRectColor(dc, progress, g_hBrushSignal);
    }
    DrawStatusDot(dc, S(22), footerTop + S(18), g_installComplete ? CLR_SIGNAL : CLR_INK);
    DrawEyebrow(dc, L"// STATUS", S(37), footerTop + S(12), S(90));
    RECT status = { S(118), footerTop + S(8), width - S(220), footerTop + S(36) };
    DrawUiText(dc, g_statusText, status, g_hFontBody, CLR_MUTED,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT footerRight = { width - S(205), footerTop + S(8), width - S(22), footerTop + S(36) };
    std::wstring rightText = g_progressPercent >= 0 ? std::to_wstring(g_progressPercent) + L"%" : L"本地部署  │  无需联网";
    DrawUiText(dc, rightText, footerRight, g_hFontReadout, g_progressPercent >= 0 ? CLR_INK : CLR_MUTED,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE, S(1));

    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(hWnd, &ps);
}

// Perform Installation
static void DoInstall() {
    SetInstallControlsEnabled(false);

    // 1. Mandatory stop-before-install strategy. Never continue if a tray
    // instance is still alive, because replacing a locked executable can leave
    // a partially updated installation.
    SetWindowTextW(g_hBtnInstall, L"正在停止旧程序…");
    SetInstallProgress(4, L"正在停止旧程序…");
    DWORD stopError = 0;
    if (!TerminateAppProcess(&stopError)) {
        WCHAR message[256] = {0};
        StringCchPrintfW(message, ARRAYSIZE(message),
            L"无法停止正在运行的 rapoo-tray.exe（错误 %lu）。\n请手动退出托盘程序后重试。",
            (unsigned long)stopError);
        MessageBoxW(g_hMainWnd, message, L"无法安装", MB_ICONERROR | MB_OK);
        SetWindowTextW(g_hBtnInstall, g_isUpdate ? L"更新" : L"安装");
        SetInstallControlsEnabled(true);
        SetInstallProgress(-1, L"停止旧程序失败，请重试");
        return;
    }
    SetWindowTextW(g_hBtnInstall, g_isUpdate ? L"正在更新…" : L"正在安装…");
    SetInstallProgress(16, L"旧程序已停止，正在准备目录…");

    // 2. Prepare directories (user-editable path)
    WCHAR installDir[MAX_PATH];
    GetWindowTextW(g_hEdtPath, installDir, MAX_PATH);
    // Trim trailing backslash (but keep "X:\")
    size_t dlen = wcslen(installDir);
    while (dlen > 3 && installDir[dlen - 1] == L'\\') { installDir[dlen - 1] = 0; --dlen; }
    if (installDir[0] == 0 || installDir[1] != L':') {
        MessageBoxW(g_hMainWnd, L"请输入有效的安装路径。", L"错误", MB_ICONERROR);
        SetWindowTextW(g_hBtnInstall, g_isUpdate ? L"更新" : L"安装");
        SetInstallControlsEnabled(true);
        SetInstallProgress(-1, L"安装路径无效");
        return;
    }
    CreateDirectoryW(installDir, NULL);
    SetInstallProgress(24, L"正在准备安装目录…");

    WCHAR appExePath[MAX_PATH];
    StringCchPrintfW(appExePath, MAX_PATH, L"%s\\rapoo-tray.exe", installDir);
    WCHAR uninstallerPath[MAX_PATH];
    StringCchPrintfW(uninstallerPath, MAX_PATH, L"%s\\Uninstall.exe", installDir);

    // 3. Extract the native tray and the WPF Fluent settings payload.
    struct PayloadFile { WORD id; const WCHAR* name; };
    const PayloadFile settingsPayload[] = {
        { IDR_SETTINGS_PAYLOAD, L"rapoo-tray-settings.exe" },
        { IDR_SETTINGS_CONFIG, L"rapoo-tray-settings.exe.config" },
        { IDR_WPF_UI, L"Wpf.Ui.dll" },
        { IDR_WPF_UI_ABSTRACTIONS, L"Wpf.Ui.Abstractions.dll" },
        { IDR_SYSTEM_MEMORY, L"System.Memory.dll" },
        { IDR_SYSTEM_BUFFERS, L"System.Buffers.dll" },
        { IDR_SYSTEM_NUMERICS, L"System.Numerics.Vectors.dll" },
        { IDR_SYSTEM_UNSAFE, L"System.Runtime.CompilerServices.Unsafe.dll" },
    };
    SetInstallProgress(38, L"正在写入托盘程序与设置中心…");
    bool extracted = ExtractPayloadToFile(IDR_APP_PAYLOAD, appExePath);
    for (const auto& payload : settingsPayload) {
        WCHAR path[MAX_PATH];
        StringCchPrintfW(path, MAX_PATH, L"%s\\%s", installDir, payload.name);
        extracted = extracted && ExtractPayloadToFile(payload.id, path);
    }
    if (!extracted) {
        MessageBoxW(g_hMainWnd, L"旧程序已停止，但写入文件仍失败。请检查安装目录权限后重试。", L"错误", MB_ICONERROR);
        SetWindowTextW(g_hBtnInstall, g_isUpdate ? L"更新" : L"安装");
        SetInstallControlsEnabled(true);
        SetInstallProgress(-1, L"写入文件失败，请检查安装目录权限");
        return;
    }

    SetInstallProgress(64, L"文件写入完成，正在创建卸载入口…");
    // 4. Copy current installer as Uninstall.exe
    WCHAR selfExe[MAX_PATH];
    GetModuleFileNameW(NULL, selfExe, MAX_PATH);
    CopyFileW(selfExe, uninstallerPath, FALSE);

    SetInstallProgress(70, L"正在创建开始菜单入口…");
    // 5. Create Start Menu shortcut
    WCHAR startMenuPrograms[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPrograms))) {
        WCHAR startLnk[MAX_PATH];
        StringCchPrintfW(startLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", startMenuPrograms);
        CreateShellShortcut(appExePath, startLnk, L"rapoo-tray", appExePath);
    }

    // 6. Create Desktop shortcut
    SetInstallProgress(76, L"正在创建桌面快捷方式…");
    if (g_optDesktop) {
        WCHAR desktopDir[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopDir))) {
            WCHAR deskLnk[MAX_PATH];
            StringCchPrintfW(deskLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", desktopDir);
            CreateShellShortcut(appExePath, deskLnk, L"rapoo-tray", appExePath);
        }
    }

    // 7. Configure Run on Startup
    SetInstallProgress(84, L"正在写入启动与卸载信息…");
    HKEY hKeyRun;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKeyRun, NULL) == ERROR_SUCCESS) {
        if (g_optAutoRun) {
            RegSetValueExW(hKeyRun, APP_NAME, 0, REG_SZ, (const BYTE*)appExePath, (DWORD)((wcslen(appExePath) + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKeyRun, APP_NAME);
        }
        RegCloseKey(hKeyRun);
    }

    // 8. Register in Windows Add/Remove Programs
    HKEY hKeyUn;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_UNINSTALL, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKeyUn, NULL) == ERROR_SUCCESS) {
        const WCHAR* name = L"rapoo-tray";
        const WCHAR* ver = APP_VERSION;
        const WCHAR* pub = L"Iris";
        WCHAR unCmd[MAX_PATH + 32];
        StringCchPrintfW(unCmd, sizeof(unCmd) / sizeof(WCHAR), L"\"%s\" /uninstall", uninstallerPath);

        RegSetValueExW(hKeyUn, L"DisplayName", 0, REG_SZ, (const BYTE*)name, (DWORD)((wcslen(name) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"DisplayVersion", 0, REG_SZ, (const BYTE*)ver, (DWORD)((wcslen(ver) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"Publisher", 0, REG_SZ, (const BYTE*)pub, (DWORD)((wcslen(pub) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"DisplayIcon", 0, REG_SZ, (const BYTE*)appExePath, (DWORD)((wcslen(appExePath) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"UninstallString", 0, REG_SZ, (const BYTE*)unCmd, (DWORD)((wcslen(unCmd) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKeyUn, L"InstallLocation", 0, REG_SZ, (const BYTE*)installDir, (DWORD)((wcslen(installDir) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKeyUn);
    }

    // 9. Launch application
    SetInstallProgress(94, g_isUpdate ? L"更新完成，正在启动 rapoo-tray…" : L"安装完成，正在启动 rapoo-tray…");
    if (g_optLaunch) {
        ShellExecuteW(NULL, L"open", appExePath, NULL, installDir, SW_SHOWNORMAL);
    }

    g_installComplete = true;
    g_completedPath = installDir;
    g_statusText = g_isUpdate ? L"更新完成 │ 配置已保留" : L"安装完成 │ 可安全关闭窗口";
    g_progressPercent = 100;
    EnableWindow(g_hBtnCancel, TRUE);
    LayoutInstallerUi(g_hMainWnd);
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateWindow(g_hMainWnd);
}

// Perform Uninstallation
static void DoUninstall() {
    int res = MessageBoxW(
        NULL,
        L"确定要卸载 rapoo-tray 吗？",
        L"卸载确认",
        MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2
    );
    if (res != IDYES) return;

    // 1. Terminate app
    TerminateAppProcess();

    // 2. Remove Shortcuts
    WCHAR startMenuPrograms[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPrograms))) {
        WCHAR startLnk[MAX_PATH];
        StringCchPrintfW(startLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", startMenuPrograms);
        DeleteFileW(startLnk);
    }

    WCHAR desktopDir[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopDir))) {
        WCHAR deskLnk[MAX_PATH];
        StringCchPrintfW(deskLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", desktopDir);
        DeleteFileW(deskLnk);
    }

    // 3. Remove Run Key
    HKEY hKeyRun;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_SET_VALUE, &hKeyRun) == ERROR_SUCCESS) {
        RegDeleteValueW(hKeyRun, APP_NAME);
        RegCloseKey(hKeyRun);
    }

    // 4. Remove Uninstall Key
    RegDeleteKeyW(HKEY_CURRENT_USER, REG_UNINSTALL);

    // 5. Delete files and remove directory
    // Prefer the install dir saved in the registry (supports custom paths);
    // fall back to the default location for older installs.
    WCHAR installDir[MAX_PATH];
    if (!GetSavedInstallDir(installDir, MAX_PATH)) {
        GetInstallDir(installDir, MAX_PATH);
    }
    {
        WCHAR appExe[MAX_PATH];
        StringCchPrintfW(appExe, MAX_PATH, L"%s\\rapoo-tray.exe", installDir);
        DeleteFileW(appExe);

        WCHAR cmd[MAX_PATH * 3];
        StringCchPrintfW(cmd, sizeof(cmd) / sizeof(WCHAR),
            L"/c timeout /t 1 /nobreak > nul & del /f /q \"%s\\Uninstall.exe\" & rmdir /s /q \"%s\"",
            installDir, installDir
        );
        ShellExecuteW(NULL, L"open", L"cmd.exe", cmd, NULL, SW_HIDE);
    }

    MessageBoxW(NULL, L"卸载完成。", L"提示", MB_ICONINFORMATION | MB_OK);
}

// Window Procedure for Installer Dialog
static void SetInstallControlsEnabled(bool enabled) {
    EnableWindow(g_hEdtPath, enabled);
    EnableWindow(g_hBtnBrowse, enabled && !g_isUpdate);
    EnableWindow(g_hChkAutoRun, enabled);
    EnableWindow(g_hChkDesktop, enabled);
    EnableWindow(g_hChkLaunch, enabled);
    EnableWindow(g_hBtnInstall, enabled);
    EnableWindow(g_hBtnCancel, enabled);
}

static LRESULT CALLBACK InstallerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWnd = hWnd;
            typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            pfnGetDpiForWindow getDpiForWindow = user32 ? (pfnGetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow") : NULL;
            g_dpi = getDpiForWindow ? getDpiForWindow(hWnd) : QuerySystemDpi();
            g_scale = g_dpi / 96.0;
            CreateUiResources(hWnd);

            WCHAR installDir[MAX_PATH] = {0};
            StringCchCopyW(installDir, ARRAYSIZE(installDir), g_detectedInstallDir);
            if (!installDir[0]) GetInstallDir(installDir, ARRAYSIZE(installDir));

            DWORD pathStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
            if (g_isUpdate) pathStyle |= ES_READONLY;
            g_hEdtPath = CreateWindowExW(0, L"EDIT", installDir, pathStyle,
                0, 0, 10, 10, hWnd, (HMENU)IDC_EDT_PATH, g_hInstance, NULL);
            SendMessageW(g_hEdtPath, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            g_hBtnBrowse = CreateWindowExW(0, L"BUTTON", L"浏览...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, hWnd, (HMENU)IDC_BTN_BROWSE, g_hInstance, NULL);
            SendMessageW(g_hBtnBrowse, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

            g_hChkAutoRun = CreateWindowExW(0, L"BUTTON", L"开机自动启动",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_CHECKBOX | BS_NOTIFY,
                0, 0, 10, 10, hWnd, (HMENU)IDC_CHK_AUTORUN, g_hInstance, NULL);
            g_hChkDesktop = CreateWindowExW(0, L"BUTTON", L"创建桌面快捷方式",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_CHECKBOX | BS_NOTIFY,
                0, 0, 10, 10, hWnd, (HMENU)IDC_CHK_DESKTOP, g_hInstance, NULL);
            g_hChkLaunch = CreateWindowExW(0, L"BUTTON", L"完成后立即运行",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_CHECKBOX | BS_NOTIFY,
                0, 0, 10, 10, hWnd, (HMENU)IDC_CHK_LAUNCH, g_hInstance, NULL);
            SendMessageW(g_hChkAutoRun, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkDesktop, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkLaunch, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            g_optAutoRun = g_isUpdate ? RunEntryExists() : true;
            g_optDesktop = true;
            g_optLaunch = true;
            SendMessageW(g_hChkAutoRun, BM_SETCHECK, g_optAutoRun ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(g_hChkLaunch, BM_SETCHECK, BST_CHECKED, 0);

            g_hBtnInstall = CreateWindowExW(0, L"BUTTON", g_isUpdate ? L"更新" : L"安装",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, hWnd, (HMENU)IDC_BTN_INSTALL, g_hInstance, NULL);
            g_hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, hWnd, (HMENU)IDC_BTN_CANCEL, g_hInstance, NULL);
            SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
            SendMessageW(g_hBtnCancel, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

            HWND children[] = { g_hEdtPath, g_hBtnBrowse, g_hChkAutoRun, g_hChkDesktop,
                                g_hChkLaunch, g_hBtnInstall, g_hBtnCancel };
            for (HWND child : children) {
                if (child) SetWindowSubclass(child, InstallerChildSubclass, 1, 0);
            }

            LayoutInstallerUi(hWnd);
            SetFocus(g_isUpdate ? g_hBtnInstall : g_hEdtPath);
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            SetBkMode(dc, TRANSPARENT);
            SetBkColor(dc, CLR_PANEL);
            SetTextColor(dc, CLR_INK);
            return (LRESULT)g_hBrushPanel;
        }

        case WM_DRAWITEM:
            DrawOwnerItem((const DRAWITEMSTRUCT*)lParam);
            return TRUE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintInstallerUi(hWnd);
            return 0;

        case WM_SIZE:
            LayoutInstallerUi(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;

        case WM_DPICHANGED: {
            g_dpi = HIWORD(wParam);
            g_scale = g_dpi / 96.0;
            RECT* suggested = (RECT*)lParam;
            SetWindowPos(hWnd, NULL, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            CreateUiResources(hWnd);
            LayoutInstallerUi(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            if (PtInRect(&g_rcClose, pt) || PtInRect(&g_rcMinimize, pt)) return HTCLIENT;
            if (pt.y >= 0 && pt.y < S(40)) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_SETCURSOR: {
            POINT pt = {0};
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            if (LOWORD(lParam) == HTCLIENT && (PtInRect(&g_rcClose, pt) || PtInRect(&g_rcMinimize, pt))) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HWND hover = NULL;
            if (PtInRect(&g_rcClose, pt)) hover = (HWND)(UINT_PTR)0xF002;
            else if (PtInRect(&g_rcMinimize, pt)) hover = (HWND)(UINT_PTR)0xF001;
            if (g_hHoverControl != hover) {
                g_hHoverControl = hover;
                RECT title = { 0, 0, g_rcClose.right, S(40) };
                InvalidateRect(hWnd, &title, FALSE);
            }
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&track);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_hHoverControl == (HWND)(UINT_PTR)0xF001 || g_hHoverControl == (HWND)(UINT_PTR)0xF002) {
                g_hHoverControl = NULL;
                RECT title = { 0, 0, g_rcClose.right, S(40) };
                InvalidateRect(hWnd, &title, FALSE);
            }
            return 0;

        case WM_LBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&g_rcClose, pt)) {
                DestroyWindow(hWnd);
                return 0;
            }
            if (PtInRect(&g_rcMinimize, pt)) {
                ShowWindow(hWnd, SW_MINIMIZE);
                return 0;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);
            if (id == IDC_BTN_INSTALL) {
                if (!g_installComplete) DoInstall();
            } else if (id == IDC_BTN_CANCEL) {
                DestroyWindow(hWnd);
            } else if (id == IDC_BTN_BROWSE) {
                WCHAR dir[MAX_PATH];
                if (BrowseForFolder(hWnd, dir, MAX_PATH)) {
                    SetWindowTextW(g_hEdtPath, dir);
                    InvalidateRect(hWnd, &g_rcPathFrame, FALSE);
                }
            } else if (id >= IDC_CHK_AUTORUN && id <= IDC_CHK_LAUNCH && notification == BN_CLICKED) {
                InvalidateRect(hWnd, &g_rcCard, FALSE);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && !g_installComplete) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            DestroyUiResources();
            g_hMainWnd = NULL;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    CoInitialize(NULL);

    // Check if invoked as uninstaller
    WCHAR selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);

    bool isUninstall = false;
    if (wcsstr(pCmdLine, L"/uninstall") || wcsstr(pCmdLine, L"/UNINSTALL") || wcsstr(selfPath, L"Uninstall.exe") || wcsstr(selfPath, L"uninstall.exe")) {
        isUninstall = true;
    }

    if (isUninstall) {
        DoUninstall();
        CoUninitialize();
        return 0;
    }

    // Resolve the existing installation before creating the window so both the
    // title bar and controls can switch to update mode immediately.
    g_isUpdate = DetectInstalledDir(g_detectedInstallDir, ARRAYSIZE(g_detectedInstallDir));
    if (!g_isUpdate) GetInstallDir(g_detectedInstallDir, ARRAYSIZE(g_detectedInstallDir));

    g_isPreview = wcsstr(pCmdLine, L"/preview") != NULL || wcsstr(pCmdLine, L"--preview") != NULL;

    // Modern DPI awareness must be set before any UI window or font is created.
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL (WINAPI *pfnSetDpiAwareV2)(DPI_AWARENESS_CONTEXT);
        pfnSetDpiAwareV2 fnSetDpiAwareV2 = (pfnSetDpiAwareV2)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (fnSetDpiAwareV2) {
            fnSetDpiAwareV2(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
    g_dpi = QuerySystemDpi();
    g_scale = g_dpi / 96.0;

    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = InstallerWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"rapoo-traySetupWndClass";
    if (!RegisterClassExW(&wc)) {
        CoUninitialize();
        return 1;
    }

    int winWidth = S(860);
    int winHeight = S(610);
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int screenX = work.left + ((work.right - work.left) - winWidth) / 2;
    int screenY = work.top + ((work.bottom - work.top) - winHeight) / 2;
    if (g_isPreview) {
        screenX = work.left - winWidth - S(80);
        screenY = work.top;
    }

    g_hMainWnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_CONTROLPARENT,
        wc.lpszClassName,
        g_isUpdate ? L"rapoo-tray 更新" : L"rapoo-tray Setup",
        WS_POPUP | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
        screenX, screenY, winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMainWnd, g_isPreview ? SW_SHOWNOACTIVATE : nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_hMainWnd && IsWindowVisible(g_hMainWnd) && !IsIconic(g_hMainWnd)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                DestroyWindow(g_hMainWnd);
                continue;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && !g_installComplete) {
                SendMessageW(g_hMainWnd, WM_COMMAND, IDC_BTN_INSTALL, 0);
                continue;
            }
        }
        if (!g_hMainWnd || !IsDialogMessageW(g_hMainWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return (int)msg.wParam;
}

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <stdbool.h>
#include <stdio.h>
#include <tlhelp32.h>

#define IDR_APP_PAYLOAD      1001

#define IDC_BTN_INSTALL      2001
#define IDC_BTN_CANCEL       2002
#define IDC_CHK_AUTORUN      2003
#define IDC_CHK_DESKTOP      2004
#define IDC_CHK_LAUNCH       2005
#define IDC_EDT_PATH         2006
#define IDC_BTN_BROWSE       2007

static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hChkAutoRun = NULL;
static HWND g_hChkDesktop = NULL;
static HWND g_hChkLaunch = NULL;
static HWND g_hEdtPath = NULL;
static HWND g_hBtnInstall = NULL;
static HWND g_hBtnCancel = NULL;
static bool g_isUpdate = false;
static WCHAR g_detectedInstallDir[MAX_PATH] = {0};
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontBody = NULL;

static const WCHAR* REG_RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* REG_UNINSTALL = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\rapoo-tray";
static const WCHAR* APP_NAME = L"rapoo-tray";

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
static bool ExtractPayloadToFile(LPCWSTR destPath) {
    HRSRC hRes = FindResourceW(g_hInstance, MAKEINTRESOURCEW(IDR_APP_PAYLOAD), RT_RCDATA);
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

// Perform Installation
static void DoInstall() {
    EnableWindow(g_hBtnInstall, FALSE);
    EnableWindow(g_hBtnCancel, FALSE);

    // 1. Mandatory stop-before-install strategy. Never continue if a tray
    // instance is still alive, because replacing a locked executable can leave
    // a partially updated installation.
    SetWindowTextW(g_hBtnInstall, L"正在停止旧程序…");
    UpdateWindow(g_hBtnInstall);
    DWORD stopError = 0;
    if (!TerminateAppProcess(&stopError)) {
        WCHAR message[256] = {0};
        StringCchPrintfW(message, ARRAYSIZE(message),
            L"无法停止正在运行的 rapoo-tray.exe（错误 %lu）。\n请手动退出托盘程序后重试。",
            (unsigned long)stopError);
        MessageBoxW(g_hMainWnd, message, L"无法安装", MB_ICONERROR | MB_OK);
        SetWindowTextW(g_hBtnInstall, g_isUpdate ? L"更新" : L"安装");
        EnableWindow(g_hBtnInstall, TRUE);
        EnableWindow(g_hBtnCancel, TRUE);
        return;
    }
    SetWindowTextW(g_hBtnInstall, L"正在安装…");
    UpdateWindow(g_hBtnInstall);

    // 2. Prepare directories (user-editable path)
    WCHAR installDir[MAX_PATH];
    GetWindowTextW(g_hEdtPath, installDir, MAX_PATH);
    // Trim trailing backslash (but keep "X:\")
    size_t dlen = wcslen(installDir);
    while (dlen > 3 && installDir[dlen - 1] == L'\\') { installDir[dlen - 1] = 0; --dlen; }
    if (installDir[0] == 0 || installDir[1] != L':') {
        MessageBoxW(g_hMainWnd, L"请输入有效的安装路径。", L"错误", MB_ICONERROR);
        SetWindowTextW(g_hBtnInstall, g_isUpdate ? L"更新" : L"安装");
        EnableWindow(g_hBtnInstall, TRUE);
        EnableWindow(g_hBtnCancel, TRUE);
        return;
    }
    CreateDirectoryW(installDir, NULL);

    WCHAR appExePath[MAX_PATH];
    StringCchPrintfW(appExePath, MAX_PATH, L"%s\\rapoo-tray.exe", installDir);

    WCHAR uninstallerPath[MAX_PATH];
    StringCchPrintfW(uninstallerPath, MAX_PATH, L"%s\\Uninstall.exe", installDir);

    // 3. Extract rapoo-tray.exe
    if (!ExtractPayloadToFile(appExePath)) {
        MessageBoxW(g_hMainWnd, L"旧程序已停止，但写入文件仍失败。请检查安装目录权限后重试。", L"错误", MB_ICONERROR);
        SetWindowTextW(g_hBtnInstall, g_isUpdate ? L"更新" : L"安装");
        EnableWindow(g_hBtnInstall, TRUE);
        EnableWindow(g_hBtnCancel, TRUE);
        return;
    }

    // 4. Copy current installer as Uninstall.exe
    WCHAR selfExe[MAX_PATH];
    GetModuleFileNameW(NULL, selfExe, MAX_PATH);
    CopyFileW(selfExe, uninstallerPath, FALSE);

    // 5. Create Start Menu shortcut
    WCHAR startMenuPrograms[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPrograms))) {
        WCHAR startLnk[MAX_PATH];
        StringCchPrintfW(startLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", startMenuPrograms);
        CreateShellShortcut(appExePath, startLnk, L"rapoo-tray", appExePath);
    }

    // 6. Create Desktop shortcut
    if (SendMessageW(g_hChkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        WCHAR desktopDir[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopDir))) {
            WCHAR deskLnk[MAX_PATH];
            StringCchPrintfW(deskLnk, MAX_PATH, L"%s\\rapoo-tray.lnk", desktopDir);
            CreateShellShortcut(appExePath, deskLnk, L"rapoo-tray", appExePath);
        }
    }

    // 7. Configure Run on Startup
    HKEY hKeyRun;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKeyRun, NULL) == ERROR_SUCCESS) {
        if (SendMessageW(g_hChkAutoRun, BM_GETCHECK, 0, 0) == BST_CHECKED) {
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
        const WCHAR* ver = L"1.6.8";
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
    if (SendMessageW(g_hChkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        ShellExecuteW(NULL, L"open", appExePath, NULL, installDir, SW_SHOWNORMAL);
    }

    MessageBoxW(g_hMainWnd, g_isUpdate ? L"更新完成。" : L"安装完成。", L"提示", MB_ICONINFORMATION | MB_OK);
    DestroyWindow(g_hMainWnd);
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
static LRESULT CALLBACK InstallerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hFontTitle = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
            g_hFontBody = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

            // An existing installation is updated in place. Custom install paths
            // are recovered from the uninstall or Run entry and are not requested again.
            WCHAR installDir[MAX_PATH] = {0};
            StringCchCopyW(installDir, ARRAYSIZE(installDir), g_detectedInstallDir);
            if (!installDir[0]) GetInstallDir(installDir, ARRAYSIZE(installDir));

            HWND hTitle = CreateWindowExW(0, L"STATIC", g_isUpdate ? L"rapoo-tray 更新" : L"rapoo-tray Setup", WS_CHILD | WS_VISIBLE, 24, 20, 360, 24, hWnd, NULL, g_hInstance, NULL);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

            HWND hPathLabel = CreateWindowExW(0, L"STATIC", g_isUpdate ? L"更新到:" : L"安装路径:", WS_CHILD | WS_VISIBLE, 24, 52, 70, 20, hWnd, NULL, g_hInstance, NULL);
            SendMessageW(hPathLabel, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            DWORD pathStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
            if (g_isUpdate) pathStyle |= ES_READONLY;
            g_hEdtPath = CreateWindowExW(0, L"EDIT", installDir, pathStyle, 94, 50, 240, 24, hWnd, (HMENU)IDC_EDT_PATH, g_hInstance, NULL);
            SendMessageW(g_hEdtPath, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            HWND hBtnBrowse = CreateWindowExW(0, L"BUTTON", L"浏览...", WS_CHILD | (g_isUpdate ? 0 : WS_VISIBLE) | (g_isUpdate ? WS_DISABLED : 0) | BS_PUSHBUTTON, 340, 49, 72, 26, hWnd, (HMENU)IDC_BTN_BROWSE, g_hInstance, NULL);
            SendMessageW(hBtnBrowse, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            // Options
            g_hChkAutoRun = CreateWindowExW(0, L"BUTTON", L"开机自动启动", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 92, 200, 22, hWnd, (HMENU)IDC_CHK_AUTORUN, g_hInstance, NULL);
            SendMessageW(g_hChkAutoRun, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkAutoRun, BM_SETCHECK,
                (g_isUpdate ? RunEntryExists() : true) ? BST_CHECKED : BST_UNCHECKED, 0);

            g_hChkDesktop = CreateWindowExW(0, L"BUTTON", L"创建桌面快捷方式", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 120, 200, 22, hWnd, (HMENU)IDC_CHK_DESKTOP, g_hInstance, NULL);
            SendMessageW(g_hChkDesktop, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkLaunch = CreateWindowExW(0, L"BUTTON", L"安装完成后立即运行", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 24, 148, 200, 22, hWnd, (HMENU)IDC_CHK_LAUNCH, g_hInstance, NULL);
            SendMessageW(g_hChkLaunch, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);
            SendMessageW(g_hChkLaunch, BM_SETCHECK, BST_CHECKED, 0);

            // Buttons
            g_hBtnInstall = CreateWindowExW(0, L"BUTTON", g_isUpdate ? L"更新" : L"安装", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 216, 188, 88, 30, hWnd, (HMENU)IDC_BTN_INSTALL, g_hInstance, NULL);
            SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            g_hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 314, 188, 88, 30, hWnd, (HMENU)IDC_BTN_CANCEL, g_hInstance, NULL);
            SendMessageW(g_hBtnCancel, WM_SETFONT, (WPARAM)g_hFontBody, TRUE);

            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDC_BTN_INSTALL) {
                DoInstall();
            } else if (wmId == IDC_BTN_CANCEL) {
                DestroyWindow(hWnd);
            } else if (wmId == IDC_BTN_BROWSE) {
                WCHAR dir[MAX_PATH];
                if (BrowseForFolder(hWnd, dir, MAX_PATH)) {
                    SetWindowTextW(g_hEdtPath, dir);
                }
            }
            return 0;
        }

        case WM_DESTROY: {
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontBody) DeleteObject(g_hFontBody);
            PostQuitMessage(0);
            return 0;
        }
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

    // Modern DPI awareness
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL (WINAPI *pfnSetDpiAwareV2)(DPI_AWARENESS_CONTEXT);
        pfnSetDpiAwareV2 fnSetDpiAwareV2 = (pfnSetDpiAwareV2)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (fnSetDpiAwareV2) {
            fnSetDpiAwareV2(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    // Register Window Class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = InstallerWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"rapoo-traySetupWndClass";
    RegisterClassExW(&wc);

    int winWidth = 436;
    int winHeight = 265;
    int screenX = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
    int screenY = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;

    g_hMainWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        g_isUpdate ? L"rapoo-tray 更新" : L"rapoo-tray Setup",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        screenX, screenY, winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}

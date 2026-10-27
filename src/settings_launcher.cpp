#include "settings_window.h"

#include <shellapi.h>
#include <string>

namespace Settings {
namespace {

void LaunchSettings(HWND owner, int page) {
    page = page < 0 ? 0 : (page > 3 ? 3 : page);

    WCHAR modulePath[MAX_PATH] = {0};
    if (!GetModuleFileNameW(NULL, modulePath, MAX_PATH)) {
        MessageBoxW(owner, L"无法定位 rapoo-tray 安装目录。", L"rapoo-tray", MB_OK | MB_ICONERROR);
        return;
    }
    WCHAR* slash = wcsrchr(modulePath, L'\\');
    if (!slash) return;
    *(slash + 1) = L'\0';

    std::wstring directory = modulePath;
    std::wstring exe = directory + L"rapoo-tray-settings.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(owner,
            L"未找到 rapoo-tray-settings.exe。\n请重新安装完整版本，或把设置中心文件放在托盘程序同一目录。",
            L"rapoo-tray", MB_OK | MB_ICONERROR);
        return;
    }

    WCHAR arguments[64] = {0};
    swprintf_s(arguments, L"--page %d", page);
    HINSTANCE result = ShellExecuteW(owner, L"open", exe.c_str(), arguments, directory.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        MessageBoxW(owner, L"无法启动设置中心。", L"rapoo-tray", MB_OK | MB_ICONERROR);
    }
}

} // namespace

void Show(HWND owner) { LaunchSettings(owner, 0); }
void ShowPage(HWND owner, int page) { LaunchSettings(owner, page); }
void Cleanup() { }
bool PreTranslateMessage(MSG*) { return false; }

} // namespace Settings

#pragma once

#include <windows.h>

namespace QuickPanel {

constexpr UINT HOTKEY_ID = 0x7101;
constexpr UINT HOTKEY_PROFILE_ID = 0x7102;
constexpr UINT HOTKEY_DPI_ID = 0x7103;
constexpr UINT HOTKEY_PERFORMANCE_ID = 0x7104;
constexpr UINT HOTKEY_TOGGLE_OSD_ID = 0x7105;

bool Init(HINSTANCE hInstance, HWND mainWindow);
void Cleanup();
bool ReloadConfig();

void Toggle();
void Show(bool preview = false);
void Hide();
bool IsVisible();
bool HotkeyRegistered();
DWORD HotkeyRegistrationMask();
bool HandleHotkey(UINT hotkeyId);
HWND GetWindowHandle();

} // namespace QuickPanel

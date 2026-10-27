#pragma once

#include <windows.h>

namespace QuickPanel {

constexpr UINT HOTKEY_ID = 0x7101;

bool Init(HINSTANCE hInstance, HWND mainWindow);
void Cleanup();
bool ReloadConfig();

void Toggle();
void Show(bool preview = false);
void Hide();
bool IsVisible();
bool HotkeyRegistered();
HWND GetWindowHandle();

} // namespace QuickPanel

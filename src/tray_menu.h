#pragma once
#include <windows.h>
#include <shellapi.h>
#include "device_manager.h"

namespace Tray {

void InitTheme();
bool IsDarkMode();
void ApplyModernWindowStyle(HWND hWnd, bool isDark, int width = 0, int height = 0);
void ShowMenu(HWND hWndOwner);
void DismissAllMenus();

HICON CreateBatteryIcon(int battery, bool isCharging, bool isConnected, int size, bool isDark);
void UpdateTooltip(NOTIFYICONDATAW& nid, const Device::State& state);

int CycleBatteryStyle();
int GetBatteryStyle();

bool IsAutoRunEnabled();
void ToggleAutoRun();

} // namespace Tray

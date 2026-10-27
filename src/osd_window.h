#pragma once
#include <windows.h>
#include "device_manager.h"
#include "osd_config.h"

namespace Osd {

bool Init(HINSTANCE hInstance);
void Cleanup();

// Shows custom 2-line or 3-line notification with auto-sizing geometry
void Show(const WCHAR* line1, const WCHAR* line2, const WCHAR* line3 = nullptr);

// Applies the user's configurable display fields to a live device snapshot.
void ShowDeviceUpdate(const Device::State& state);

// Formatted notification when Battery Icon Style changes
void ShowStyleOsd(int style);

// User-facing OSD master switch, persisted in HKCU.
bool IsEnabled();
void SetEnabled(bool enabled);

OsdConfig::Config GetConfig();
bool SetConfig(const OsdConfig::Config& config);

HWND GetWindowHandle();

} // namespace Osd

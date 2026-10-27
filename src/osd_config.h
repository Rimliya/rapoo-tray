#pragma once

#include <windows.h>
#include <string>

namespace OsdConfig {

constexpr DWORD FIELD_MODEL      = 1u << 0;
constexpr DWORD FIELD_MODE       = 1u << 1;
constexpr DWORD FIELD_DPI_LEVEL  = 1u << 2;
constexpr DWORD FIELD_DPI_VALUE  = 1u << 3;
constexpr DWORD FIELD_BATTERY    = 1u << 4;
constexpr DWORD FIELD_POLLING    = 1u << 5;
constexpr DWORD FIELD_PERFORMANCE = 1u << 6;
constexpr DWORD FIELD_ALL        = (1u << 7) - 1;

constexpr DWORD PANEL_PROFILES     = 1u << 0;
constexpr DWORD PANEL_DPI          = 1u << 1;
constexpr DWORD PANEL_PERFORMANCE  = 1u << 2;
constexpr DWORD PANEL_BATTERY      = 1u << 3;
constexpr DWORD PANEL_POLLING      = 1u << 4;
constexpr DWORD PANEL_DETAILS      = 1u << 5;
constexpr DWORD PANEL_ALL          = (1u << 6) - 1;

struct Config {
    bool enabled = true;
    bool quickPanelEnabled = true;
    UINT hotkeyModifiers = MOD_CONTROL | MOD_ALT;
    UINT hotkeyVirtualKey = 'M';
    DWORD displayFields = FIELD_ALL;
    DWORD panelItems = PANEL_ALL;
};

bool NormalizeHotkey(UINT modifiers, UINT virtualKey, UINT& outModifiers, UINT& outVirtualKey);
bool IsHotkeyValid(UINT modifiers, UINT virtualKey);
std::wstring FormatHotkey(UINT modifiers, UINT virtualKey);
DWORD NormalizeDisplayMask(DWORD mask);
DWORD NormalizePanelMask(DWORD mask);

Config Defaults();
Config Load();
bool Save(const Config& config);

} // namespace OsdConfig

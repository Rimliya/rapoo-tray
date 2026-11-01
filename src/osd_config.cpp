#include "osd_config.h"

#include <algorithm>
#include <cwctype>
#include <strsafe.h>

namespace OsdConfig {
namespace {

constexpr const WCHAR* APP_KEY = L"Software\\rapoo-tray";
constexpr UINT SUPPORTED_MODIFIERS = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

DWORD ReadDword(HKEY key, const WCHAR* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD) {
        return fallback;
    }
    return value;
}

bool IsFunctionKey(UINT virtualKey) {
    return virtualKey >= VK_F1 && virtualKey <= VK_F24;
}

bool IsModifierKey(UINT virtualKey) {
    return virtualKey == VK_SHIFT || virtualKey == VK_CONTROL || virtualKey == VK_MENU ||
           virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
           virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
           virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
           virtualKey == VK_LWIN || virtualKey == VK_RWIN;
}

void LoadOptionalHotkey(HKEY key, const WCHAR* modifierName, const WCHAR* keyName,
                        UINT& modifiers, UINT& virtualKey) {
    const DWORD storedModifiers = ReadDword(key, modifierName, modifiers);
    const DWORD storedVirtualKey = ReadDword(key, keyName, virtualKey);
    if (storedModifiers == 0 && storedVirtualKey == 0) {
        modifiers = 0;
        virtualKey = 0;
        return;
    }

    UINT normalizedModifiers = 0;
    UINT normalizedVirtualKey = 0;
    if (NormalizeHotkey(storedModifiers, storedVirtualKey,
                        normalizedModifiers, normalizedVirtualKey)) {
        modifiers = normalizedModifiers;
        virtualKey = normalizedVirtualKey;
    } else {
        modifiers = 0;
        virtualKey = 0;
    }
}

void SaveOptionalHotkey(HKEY key, const WCHAR* modifierName, const WCHAR* keyName,
                        UINT modifiers, UINT virtualKey) {
    UINT normalizedModifiers = 0;
    UINT normalizedVirtualKey = 0;
    if (!NormalizeHotkey(modifiers, virtualKey, normalizedModifiers, normalizedVirtualKey)) {
        normalizedModifiers = 0;
        normalizedVirtualKey = 0;
    }
    RegSetValueExW(key, modifierName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&normalizedModifiers), sizeof(normalizedModifiers));
    RegSetValueExW(key, keyName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&normalizedVirtualKey), sizeof(normalizedVirtualKey));
}

int ClampInt(int value, int minimum, int maximum) {
    return std::clamp(value, minimum, maximum);
}

} // namespace

bool NormalizeHotkey(UINT modifiers, UINT virtualKey, UINT& outModifiers, UINT& outVirtualKey) {
    modifiers &= SUPPORTED_MODIFIERS;
    virtualKey &= 0xFF;
    if (virtualKey == 0 || IsModifierKey(virtualKey) || virtualKey == VK_ESCAPE || virtualKey == VK_F4) {
        return false;
    }
    if (modifiers == 0 && !IsFunctionKey(virtualKey)) return false;
    outModifiers = modifiers;
    outVirtualKey = virtualKey;
    return true;
}

bool IsHotkeyValid(UINT modifiers, UINT virtualKey) {
    UINT normalizedModifiers = 0;
    UINT normalizedVirtualKey = 0;
    return NormalizeHotkey(modifiers, virtualKey, normalizedModifiers, normalizedVirtualKey);
}

bool IsHotkeyAssigned(UINT modifiers, UINT virtualKey) {
    return modifiers != 0 || virtualKey != 0;
}

bool AreHotkeysUnique(const Config& config, std::wstring* error) {
    struct Binding {
        const WCHAR* label;
        UINT modifiers;
        UINT virtualKey;
        bool enabled;
    };
    const Binding bindings[] = {
        {L"快速面板", config.hotkeyModifiers, config.hotkeyVirtualKey, config.quickPanelEnabled},
        {L"切换配置", config.profileCycleHotkeyModifiers, config.profileCycleHotkeyVirtualKey, true},
        {L"切换 DPI", config.dpiCycleHotkeyModifiers, config.dpiCycleHotkeyVirtualKey, true},
        {L"循环性能模式", config.performanceCycleHotkeyModifiers, config.performanceCycleHotkeyVirtualKey, true},
        {L"开关状态 OSD", config.toggleOsdHotkeyModifiers, config.toggleOsdHotkeyVirtualKey, true},
    };

    for (size_t i = 0; i < ARRAYSIZE(bindings); ++i) {
        if (!bindings[i].enabled || !IsHotkeyAssigned(bindings[i].modifiers, bindings[i].virtualKey)) continue;
        for (size_t j = i + 1; j < ARRAYSIZE(bindings); ++j) {
            if (!bindings[j].enabled || !IsHotkeyAssigned(bindings[j].modifiers, bindings[j].virtualKey)) continue;
            if (bindings[i].modifiers == bindings[j].modifiers &&
                bindings[i].virtualKey == bindings[j].virtualKey) {
                if (error) {
                    *error = std::wstring(L"快捷键冲突：") + bindings[i].label + L" 与 " + bindings[j].label + L" 使用了同一组合键";
                }
                return false;
            }
        }
    }
    return true;
}

std::wstring FormatHotkey(UINT modifiers, UINT virtualKey) {
    UINT normalizedModifiers = 0;
    UINT normalizedVirtualKey = 0;
    if (!NormalizeHotkey(modifiers, virtualKey, normalizedModifiers, normalizedVirtualKey)) {
        return L"未设置";
    }

    std::wstring text;
    auto append = [&](const WCHAR* part) {
        if (!text.empty()) text += L" + ";
        text += part;
    };
    if (normalizedModifiers & MOD_CONTROL) append(L"Ctrl");
    if (normalizedModifiers & MOD_ALT) append(L"Alt");
    if (normalizedModifiers & MOD_SHIFT) append(L"Shift");
    if (normalizedModifiers & MOD_WIN) append(L"Win");

    WCHAR keyName[32] = {0};
    if (normalizedVirtualKey >= 'A' && normalizedVirtualKey <= 'Z') {
        keyName[0] = (WCHAR)normalizedVirtualKey;
    } else if (normalizedVirtualKey >= '0' && normalizedVirtualKey <= '9') {
        keyName[0] = (WCHAR)normalizedVirtualKey;
    } else if (normalizedVirtualKey >= VK_F1 && normalizedVirtualKey <= VK_F24) {
        StringCchPrintfW(keyName, ARRAYSIZE(keyName), L"F%u", normalizedVirtualKey - VK_F1 + 1);
    } else {
        switch (normalizedVirtualKey) {
            case VK_SPACE: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Space"); break;
            case VK_TAB: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Tab"); break;
            case VK_RETURN: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Enter"); break;
            case VK_BACK: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Backspace"); break;
            case VK_INSERT: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Insert"); break;
            case VK_DELETE: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Delete"); break;
            case VK_HOME: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Home"); break;
            case VK_END: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"End"); break;
            case VK_PRIOR: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Page Up"); break;
            case VK_NEXT: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Page Down"); break;
            case VK_LEFT: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Left"); break;
            case VK_RIGHT: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Right"); break;
            case VK_UP: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Up"); break;
            case VK_DOWN: StringCchCopyW(keyName, ARRAYSIZE(keyName), L"Down"); break;
            default: StringCchPrintfW(keyName, ARRAYSIZE(keyName), L"VK %u", normalizedVirtualKey); break;
        }
    }
    append(keyName);
    return text;
}

DWORD NormalizeDisplayMask(DWORD mask) {
    return mask & FIELD_ALL;
}

DWORD NormalizePanelMask(DWORD mask) {
    return mask & PANEL_ALL;
}

Config Defaults() {
    return Config();
}

Config Load() {
    Config config;
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return config;
    }

    config.enabled = ReadDword(key, L"OsdEnabled", config.enabled ? 1 : 0) != 0;
    config.quickPanelEnabled = ReadDword(key, L"QuickPanelEnabled", config.quickPanelEnabled ? 1 : 0) != 0;
    UINT modifiers = ReadDword(key, L"QuickPanelHotkeyModifiers", config.hotkeyModifiers);
    UINT virtualKey = ReadDword(key, L"QuickPanelHotkeyVirtualKey", config.hotkeyVirtualKey);
    UINT normalizedModifiers = 0;
    UINT normalizedVirtualKey = 0;
    if (NormalizeHotkey(modifiers, virtualKey, normalizedModifiers, normalizedVirtualKey)) {
        config.hotkeyModifiers = normalizedModifiers;
        config.hotkeyVirtualKey = normalizedVirtualKey;
    }
    config.displayFields = NormalizeDisplayMask(ReadDword(key, L"OsdDisplayFields", config.displayFields));
    config.panelItems = NormalizePanelMask(ReadDword(key, L"QuickPanelItems", config.panelItems));

    config.osdPosition = std::min<DWORD>(OSD_POSITION_TOP_LEFT,
        ReadDword(key, L"OsdPosition", config.osdPosition));
    config.osdMonitor = std::min<DWORD>(OSD_MONITOR_PRIMARY,
        ReadDword(key, L"OsdMonitor", config.osdMonitor));
    config.osdOffsetX = ClampInt(static_cast<int>(ReadDword(key, L"OsdOffsetX",
        static_cast<DWORD>(config.osdOffsetX))), -1000, 1000);
    config.osdOffsetY = ClampInt(static_cast<int>(ReadDword(key, L"OsdOffsetY",
        static_cast<DWORD>(config.osdOffsetY))), -1000, 1000);
    config.osdScalePercent = ClampInt(static_cast<int>(ReadDword(key, L"OsdScalePercent",
        static_cast<DWORD>(config.osdScalePercent))), 75, 150);
    config.osdOpacityPercent = ClampInt(static_cast<int>(ReadDword(key, L"OsdOpacityPercent",
        static_cast<DWORD>(config.osdOpacityPercent))), 40, 100);
    config.osdDurationMs = ClampInt(static_cast<int>(ReadDword(key, L"OsdDurationMs",
        static_cast<DWORD>(config.osdDurationMs))), 800, 8000);

    LoadOptionalHotkey(key, L"ProfileCycleHotkeyModifiers", L"ProfileCycleHotkeyVirtualKey",
        config.profileCycleHotkeyModifiers, config.profileCycleHotkeyVirtualKey);
    LoadOptionalHotkey(key, L"DpiCycleHotkeyModifiers", L"DpiCycleHotkeyVirtualKey",
        config.dpiCycleHotkeyModifiers, config.dpiCycleHotkeyVirtualKey);
    LoadOptionalHotkey(key, L"PerformanceCycleHotkeyModifiers", L"PerformanceCycleHotkeyVirtualKey",
        config.performanceCycleHotkeyModifiers, config.performanceCycleHotkeyVirtualKey);
    LoadOptionalHotkey(key, L"ToggleOsdHotkeyModifiers", L"ToggleOsdHotkeyVirtualKey",
        config.toggleOsdHotkeyModifiers, config.toggleOsdHotkeyVirtualKey);

    RegCloseKey(key);
    return config;
}

bool Save(const Config& input) {
    Config config = input;
    UINT normalizedModifiers = 0;
    UINT normalizedVirtualKey = 0;
    if (!NormalizeHotkey(config.hotkeyModifiers, config.hotkeyVirtualKey,
                         normalizedModifiers, normalizedVirtualKey)) {
        config.hotkeyModifiers = MOD_CONTROL | MOD_ALT;
        config.hotkeyVirtualKey = 'M';
    } else {
        config.hotkeyModifiers = normalizedModifiers;
        config.hotkeyVirtualKey = normalizedVirtualKey;
    }

    config.displayFields = NormalizeDisplayMask(config.displayFields);
    config.panelItems = NormalizePanelMask(config.panelItems);
    config.osdPosition = std::min<DWORD>(OSD_POSITION_TOP_LEFT, config.osdPosition);
    config.osdMonitor = std::min<DWORD>(OSD_MONITOR_PRIMARY, config.osdMonitor);
    config.osdOffsetX = ClampInt(config.osdOffsetX, -1000, 1000);
    config.osdOffsetY = ClampInt(config.osdOffsetY, -1000, 1000);
    config.osdScalePercent = ClampInt(config.osdScalePercent, 75, 150);
    config.osdOpacityPercent = ClampInt(config.osdOpacityPercent, 40, 100);
    config.osdDurationMs = ClampInt(config.osdDurationMs, 800, 8000);

    if (!AreHotkeysUnique(config)) {
        return false;
    }

    HKEY key = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return false;
    }
    auto setDword = [&](const WCHAR* name, DWORD value) {
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    };
    setDword(L"OsdEnabled", config.enabled ? 1u : 0u);
    setDword(L"QuickPanelEnabled", config.quickPanelEnabled ? 1u : 0u);
    setDword(L"QuickPanelHotkeyModifiers", config.hotkeyModifiers);
    setDword(L"QuickPanelHotkeyVirtualKey", config.hotkeyVirtualKey);
    setDword(L"OsdDisplayFields", config.displayFields);
    setDword(L"QuickPanelItems", config.panelItems);
    setDword(L"OsdPosition", config.osdPosition);
    setDword(L"OsdMonitor", config.osdMonitor);
    setDword(L"OsdOffsetX", static_cast<DWORD>(config.osdOffsetX));
    setDword(L"OsdOffsetY", static_cast<DWORD>(config.osdOffsetY));
    setDword(L"OsdScalePercent", static_cast<DWORD>(config.osdScalePercent));
    setDword(L"OsdOpacityPercent", static_cast<DWORD>(config.osdOpacityPercent));
    setDword(L"OsdDurationMs", static_cast<DWORD>(config.osdDurationMs));
    SaveOptionalHotkey(key, L"ProfileCycleHotkeyModifiers", L"ProfileCycleHotkeyVirtualKey",
        config.profileCycleHotkeyModifiers, config.profileCycleHotkeyVirtualKey);
    SaveOptionalHotkey(key, L"DpiCycleHotkeyModifiers", L"DpiCycleHotkeyVirtualKey",
        config.dpiCycleHotkeyModifiers, config.dpiCycleHotkeyVirtualKey);
    SaveOptionalHotkey(key, L"PerformanceCycleHotkeyModifiers", L"PerformanceCycleHotkeyVirtualKey",
        config.performanceCycleHotkeyModifiers, config.performanceCycleHotkeyVirtualKey);
    SaveOptionalHotkey(key, L"ToggleOsdHotkeyModifiers", L"ToggleOsdHotkeyVirtualKey",
        config.toggleOsdHotkeyModifiers, config.toggleOsdHotkeyVirtualKey);
    RegCloseKey(key);
    return true;
}

} // namespace OsdConfig
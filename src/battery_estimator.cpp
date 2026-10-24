#include "battery_estimator.h"
#include "device_manager.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace BatteryEstimate {
namespace {

constexpr const WCHAR* REG_SUBKEY = L"Software\\rapoo-tray";
constexpr const WCHAR* REG_MODEL_VALUE = L"BatteryEstimateModel";

struct PowerProfile {
    const WCHAR* key;
    const WCHAR* label;
    double baseHoursAt1000;
};

// Rough full-charge runtimes at 1000 Hz, smart RF and the balanced profile.
// The calculator is intentionally conservative and is labelled as an estimate.
const PowerProfile PROFILES[] = {
    { L"vt7", L"雷柏 VT7 / VT7 V2", 750.0 },
    { L"vt3v2", L"雷柏 VT3 V2", 760.0 },
    { L"vt3s", L"雷柏 VT3S", 300.0 },
    { L"vt3max", L"雷柏 VT3 MAX", 600.0 },
    { L"vt3", L"雷柏 VT3", 700.0 },
    { L"generic", L"通用（粗略）", 500.0 },
};

std::wstring g_override;
bool g_overrideLoaded = false;

const PowerProfile* FindProfile(const std::wstring& key) {
    for (const auto& profile : PROFILES) {
        if (_wcsicmp(profile.key, key.c_str()) == 0) return &profile;
    }
    return nullptr;
}

bool Contains(const WCHAR* text, const WCHAR* needle) {
    return text && needle && wcsstr(text, needle) != nullptr;
}

double PollingFactor(int hz) {
    switch (hz) {
        case 125: return 4.00;
        case 250: return 3.00;
        case 500: return 1.75;
        case 1000: return 1.00;
        case 2000: return 0.68;
        case 4000: return 0.43;
        case 8000: return 0.29;
        default: return 1.00;
    }
}

std::wstring ReadOverrideFromRegistry() {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return std::wstring();

    DWORD type = 0;
    DWORD size = 0;
    std::wstring value;
    if (RegQueryValueExW(hKey, REG_MODEL_VALUE, NULL, &type, NULL, &size) == ERROR_SUCCESS &&
        type == REG_SZ && size >= sizeof(WCHAR)) {
        value.resize(size / sizeof(WCHAR), L'\0');
        if (RegQueryValueExW(hKey, REG_MODEL_VALUE, NULL, &type, (LPBYTE)value.data(), &size) != ERROR_SUCCESS) {
            value.clear();
        } else {
            while (!value.empty() && value.back() == L'\0') value.pop_back();
        }
    }
    RegCloseKey(hKey);
    return value;
}

} // namespace

const std::vector<ModelOption>& Models() {
    static const std::vector<ModelOption> options = {
        { L"", L"自动识别" },
        { L"vt7", L"雷柏 VT7 / VT7 V2" },
        { L"vt3v2", L"雷柏 VT3 V2" },
        { L"vt3s", L"雷柏 VT3S" },
        { L"vt3max", L"雷柏 VT3 MAX" },
        { L"vt3", L"雷柏 VT3" },
        { L"generic", L"通用（粗略）" },
    };
    return options;
}

std::wstring LoadOverride() {
    if (!g_overrideLoaded) {
        g_override = ReadOverrideFromRegistry();
        if (!g_override.empty() && !FindProfile(g_override)) g_override.clear();
        g_overrideLoaded = true;
    }
    return g_override;
}

void SaveOverride(const std::wstring& key) {
    std::wstring normalized;
    if (FindProfile(key)) normalized = key;
    g_override = normalized;
    g_overrideLoaded = true;

    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) return;
    if (normalized.empty()) {
        RegDeleteValueW(hKey, REG_MODEL_VALUE);
    } else {
        RegSetValueExW(
            hKey,
            REG_MODEL_VALUE,
            0,
            REG_SZ,
            (const BYTE*)normalized.c_str(),
            (DWORD)((normalized.size() + 1) * sizeof(WCHAR))
        );
    }
    RegCloseKey(hKey);
}

std::wstring DetectModelKey(const Device::State& state) {
    if (Contains(state.modelName, L"VT3 MAX")) return L"vt3max";
    if (Contains(state.modelName, L"VT3S")) return L"vt3s";
    if (Contains(state.modelName, L"VT7")) return L"vt7";
    if (Contains(state.modelName, L"VT3 V2")) return L"vt3v2";
    if (Contains(state.modelName, L"VT3")) return L"vt3";
    return L"generic";
}

Result Calculate(const Device::State& state) {
    Result result;
    std::wstring overrideKey = LoadOverride();
    std::wstring modelKey = overrideKey.empty() ? DetectModelKey(state) : overrideKey;
    const PowerProfile* profile = FindProfile(modelKey);
    if (!profile) profile = FindProfile(L"generic");
    if (!profile) return result;

    result.available = true;
    result.autoDetected = overrideKey.empty();
    result.modelLabel = profile->label;

    double hours = profile->baseHoursAt1000;
    hours *= PollingFactor(state.pollingHz);
    if (state.rfFull) hours *= 0.82;

    // A small uncertainty band keeps the UI honest: the mouse battery gauge and
    // real-world radio conditions are not precise enough for a single number.
    double low = hours * 0.96;
    double high = hours * 1.04;
    result.fullMinHours = std::max(1, (int)std::lround(low));
    result.fullMaxHours = std::max(result.fullMinHours, (int)std::lround(high));

    int battery = std::clamp(state.battery, 1, 100);
    result.remainingMinHours = std::max(1, (int)std::lround(low * battery / 100.0));
    result.remainingMaxHours = std::max(result.remainingMinHours, (int)std::lround(high * battery / 100.0));
    return result;
}

} // namespace BatteryEstimate
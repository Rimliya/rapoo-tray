#include "profile_manager.h"
#include "battery_estimator.h"
#include "device_manager.h"
#include "osd_window.h"

#include <strsafe.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <mutex>

namespace Profiles {
namespace {

constexpr const WCHAR* REG_PROFILES = L"Software\\rapoo-tray\\Profiles";
constexpr int MAX_PROFILES = 16;

struct Store {
    bool initialized = false;
    std::vector<Definition> profiles;
    int nextId = 1;
    int defaultId = 1;
    int activeId = 1;
    bool autoSwitch = true;
};

Store g_store;
std::recursive_mutex g_apiMutex;

// Runtime-only state for application switching.
int g_autoContextProfileId = 0;
int g_manualHoldMatchedProfileId = 0;

const int SUPPORTED_RATES[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };

std::wstring Trim(const std::wstring& value) {
    size_t first = 0;
    while (first < value.size() && iswspace(value[first])) ++first;
    size_t last = value.size();
    while (last > first && iswspace(value[last - 1])) --last;
    return value.substr(first, last - first);
}

std::wstring NormalizeAppPath(const std::wstring& value) {
    std::wstring path = Trim(value);
    while (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
        path = Trim(path.substr(1, path.size() - 2));
    }
    return path;
}

std::wstring FileNameFromPath(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

bool EqualsNoCase(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) return false;
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

DWORD ReadDwordFromKey(HKEY hKey, const WCHAR* valueName, DWORD defaultValue) {
    DWORD value = defaultValue;
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD) {
        return defaultValue;
    }
    return value;
}

std::wstring ReadStringFromKey(HKEY hKey, const WCHAR* valueName) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(hKey, valueName, NULL, &type, NULL, &size) != ERROR_SUCCESS || type != REG_SZ || size < sizeof(WCHAR)) {
        return std::wstring();
    }

    std::wstring value(size / sizeof(WCHAR), L'\0');
    if (RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)value.data(), &size) != ERROR_SUCCESS) {
        return std::wstring();
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

bool WriteDword(HKEY hKey, const WCHAR* valueName, DWORD value) {
    return RegSetValueExW(hKey, valueName, 0, REG_DWORD, (const BYTE*)&value, sizeof(value)) == ERROR_SUCCESS;
}

bool WriteString(HKEY hKey, const WCHAR* valueName, const std::wstring& value) {
    return RegSetValueExW(
        hKey,
        valueName,
        0,
        REG_SZ,
        (const BYTE*)value.c_str(),
        (DWORD)((value.size() + 1) * sizeof(WCHAR))
    ) == ERROR_SUCCESS;
}

bool ValidPollingRate(int hz) {
    for (int supported : SUPPORTED_RATES) {
        if (hz == supported) return true;
    }
    return false;
}

void NormalizeDpiPair(int& x, int& y) {
    if (x <= 0 || y <= 0) {
        x = 0;
        y = 0;
        return;
    }

    BatteryEstimate::DpiSpec spec;
    Device::State state;
    if (Device::IsStarted()) state = Device::GetCurrentState();
    BatteryEstimate::GetDpiSpecForState(state, spec);
    const int step = std::max(1, spec.step);
    auto normalize = [&](int value) {
        value = std::clamp(value, spec.minDpi, spec.maxDpi);
        if (step > 1) {
            value = ((value + step / 2) / step) * step;
        }
        return std::clamp(value, spec.minDpi, spec.maxDpi);
    };
    x = normalize(x);
    y = normalize(y);
}

bool ProfileNameExists(const std::wstring& name, int exceptId) {
    for (const auto& profile : g_store.profiles) {
        if (profile.id != exceptId && EqualsNoCase(profile.name, name)) return true;
    }
    return false;
}

Definition* FindInternal(int id) {
    for (auto& profile : g_store.profiles) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

const Definition* FindInternalConst(int id) {
    for (const auto& profile : g_store.profiles) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

int FindMatchingProfile(const std::wstring& processPath) {
    if (processPath.empty()) return 0;
    const std::wstring processName = FileNameFromPath(processPath);

    for (const auto& profile : g_store.profiles) {
        if (profile.appPath.empty()) continue;
        if (EqualsNoCase(processPath, profile.appPath)) return profile.id;
        if (EqualsNoCase(processName, FileNameFromPath(profile.appPath))) return profile.id;
    }
    return 0;
}

bool TryGetForegroundProcessPath(std::wstring& outPath, DWORD* outPid = nullptr) {
    outPath.clear();
    if (outPid) *outPid = 0;

    HWND hForeground = GetForegroundWindow();
    if (!hForeground) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hForeground, &pid);
    if (pid == 0) return false;
    if (outPid) *outPid = pid;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    WCHAR path[32768] = {0};
    DWORD chars = (DWORD)(sizeof(path) / sizeof(path[0]));
    bool ok = QueryFullProcessImageNameW(hProcess, 0, path, &chars) && chars > 0;
    CloseHandle(hProcess);
    if (!ok) return false;
    outPath.assign(path, chars);
    return true;
}

void SaveState() {
    HKEY hRoot = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PROFILES, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hRoot, NULL) != ERROR_SUCCESS) {
        return;
    }

    WriteDword(hRoot, L"NextId", (DWORD)g_store.nextId);
    WriteDword(hRoot, L"DefaultId", (DWORD)g_store.defaultId);
    WriteDword(hRoot, L"ActiveId", (DWORD)g_store.activeId);
    WriteDword(hRoot, L"AutoSwitch", g_store.autoSwitch ? 1u : 0u);

    std::vector<DWORD> ids;
    ids.reserve(g_store.profiles.size());
    for (const auto& profile : g_store.profiles) {
        ids.push_back((DWORD)profile.id);

        std::wstring subKeyName = L"P" + std::to_wstring(profile.id);
        HKEY hProfile = NULL;
        if (RegCreateKeyExW(hRoot, subKeyName.c_str(), 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hProfile, NULL) == ERROR_SUCCESS) {
            WriteString(hProfile, L"Name", profile.name);
            WriteDword(hProfile, L"PollingHz", (DWORD)profile.pollingHz);
            WriteDword(hProfile, L"SleepMinutes", (DWORD)profile.sleepMinutes);
            WriteDword(hProfile, L"DpiX", (DWORD)profile.dpiX);
            WriteDword(hProfile, L"DpiY", (DWORD)profile.dpiY);
            WriteString(hProfile, L"AppPath", profile.appPath);
            WriteString(hProfile, L"AppName", profile.appName);
            RegCloseKey(hProfile);
        }
    }

    if (!ids.empty()) {
        RegSetValueExW(
            hRoot,
            L"Ids",
            0,
            REG_BINARY,
            (const BYTE*)ids.data(),
            (DWORD)(ids.size() * sizeof(DWORD))
        );
    } else {
        RegDeleteValueW(hRoot, L"Ids");
    }
    RegCloseKey(hRoot);
}

void AddFactoryProfiles() {
    g_store.profiles.clear();
    g_store.profiles.push_back({ 1, L"日常", 1000, 10, L"", L"" });
    g_store.profiles.push_back({ 2, L"高性能", 4000, 10, L"", L"" });
    g_store.nextId = 3;
    g_store.defaultId = 1;
    g_store.activeId = 1;
}

void LoadState() {
    g_store = Store();
    AddFactoryProfiles();

    HKEY hRoot = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PROFILES, 0, KEY_READ, &hRoot) != ERROR_SUCCESS) {
        SaveState();
        g_store.initialized = true;
        return;
    }

    std::vector<DWORD> ids;
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(hRoot, L"Ids", NULL, &type, NULL, &bytes) == ERROR_SUCCESS &&
        type == REG_BINARY && bytes >= sizeof(DWORD) && (bytes % sizeof(DWORD)) == 0) {
        ids.resize(bytes / sizeof(DWORD));
        if (RegQueryValueExW(hRoot, L"Ids", NULL, &type, (LPBYTE)ids.data(), &bytes) != ERROR_SUCCESS) {
            ids.clear();
        }
    }

    if (ids.empty()) {
        DWORD subKeyCount = 0;
        DWORD longest = 0;
        if (RegQueryInfoKeyW(hRoot, NULL, NULL, NULL, &subKeyCount, &longest, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < subKeyCount; ++i) {
                WCHAR name[128] = {0};
                DWORD nameLen = (DWORD)(sizeof(name) / sizeof(name[0]));
                if (RegEnumKeyExW(hRoot, i, name, &nameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS && name[0] == L'P') {
                    WCHAR* end = NULL;
                    unsigned long id = wcstoul(name + 1, &end, 10);
                    if (end && *end == L'\0' && id > 0) ids.push_back((DWORD)id);
                }
            }
        }
    }

    std::vector<Definition> loaded;
    for (DWORD rawId : ids) {
        std::wstring subKeyName = L"P" + std::to_wstring(rawId);
        HKEY hProfile = NULL;
        if (RegOpenKeyExW(hRoot, subKeyName.c_str(), 0, KEY_READ, &hProfile) != ERROR_SUCCESS) continue;

        Definition profile;
        profile.id = (int)rawId;
        profile.name = Trim(ReadStringFromKey(hProfile, L"Name"));
        profile.pollingHz = (int)ReadDwordFromKey(hProfile, L"PollingHz", 1000);
        profile.sleepMinutes = (int)ReadDwordFromKey(hProfile, L"SleepMinutes", 10);
        profile.dpiX = (int)ReadDwordFromKey(hProfile, L"DpiX", 0);
        profile.dpiY = (int)ReadDwordFromKey(hProfile, L"DpiY", 0);
        profile.appPath = NormalizeAppPath(ReadStringFromKey(hProfile, L"AppPath"));
        profile.appName = ReadStringFromKey(hProfile, L"AppName");
        RegCloseKey(hProfile);

        if (profile.id <= 0 || profile.name.empty()) continue;
        if (!ValidPollingRate(profile.pollingHz)) profile.pollingHz = 1000;
        profile.sleepMinutes = Rapoo::ClampSleepMinutes(profile.sleepMinutes);
        NormalizeDpiPair(profile.dpiX, profile.dpiY);
        if (profile.appPath.empty()) {
            profile.appName.clear();
        } else if (profile.appName.empty()) {
            profile.appName = FileNameFromPath(profile.appPath);
        }
        loaded.push_back(profile);
    }

    DWORD savedDefault = ReadDwordFromKey(hRoot, L"DefaultId", 1);
    DWORD savedActive = ReadDwordFromKey(hRoot, L"ActiveId", savedDefault);
    DWORD savedAuto = ReadDwordFromKey(hRoot, L"AutoSwitch", 1);
    DWORD savedNext = ReadDwordFromKey(hRoot, L"NextId", 1);
    RegCloseKey(hRoot);

    if (!loaded.empty()) {
        g_store.profiles.swap(loaded);
        g_store.autoSwitch = (savedAuto != 0);
    }

    int maxId = 0;
    bool hasDefault = false;
    bool hasActive = false;
    for (const auto& profile : g_store.profiles) {
        maxId = std::max(maxId, profile.id);
        if (profile.id == (int)savedDefault) hasDefault = true;
        if (profile.id == (int)savedActive) hasActive = true;
    }
    g_store.nextId = std::max<int>((int)savedNext, maxId + 1);
    g_store.defaultId = hasDefault ? (int)savedDefault : g_store.profiles.front().id;
    g_store.activeId = hasActive ? (int)savedActive : g_store.defaultId;
    g_store.initialized = true;
    SaveState();
}

void EnsureInitialized() {
    if (!g_store.initialized) LoadState();
}

void ShowSwitchOsd(const Definition& profile, bool automatic, bool fallback, bool connected) {
    WCHAR line1[64] = {0};
    WCHAR line2[128] = {0};
    WCHAR line3[160] = {0};
    WCHAR dpiText[64] = {0};

    if (profile.dpiX > 0 && profile.dpiY > 0) {
        if (profile.dpiX == profile.dpiY) {
            StringCchPrintfW(dpiText, ARRAYSIZE(dpiText), L"%d DPI", profile.dpiX);
        } else {
            StringCchPrintfW(dpiText, ARRAYSIZE(dpiText), L"X %d / Y %d DPI", profile.dpiX, profile.dpiY);
        }
    } else {
        StringCchCopyW(dpiText, ARRAYSIZE(dpiText), L"DPI 跟随当前");
    }

    if (fallback) {
        StringCchCopyW(line1, ARRAYSIZE(line1), L"已恢复默认配置");
        StringCchCopyW(line2, ARRAYSIZE(line2), profile.name.c_str());
    } else if (automatic) {
        StringCchCopyW(line1, ARRAYSIZE(line1), L"已按前台程序切换配置");
        StringCchCopyW(line2, ARRAYSIZE(line2), profile.name.c_str());
    } else {
        StringCchCopyW(line1, ARRAYSIZE(line1), L"配置已切换");
        StringCchCopyW(line2, ARRAYSIZE(line2), profile.name.c_str());
    }

    if (!connected) {
        StringCchPrintfW(line3, ARRAYSIZE(line3), L"%d Hz · %s · 设备未连接，连接后自动应用", profile.pollingHz, dpiText);
    } else if (automatic && !profile.appName.empty()) {
        StringCchPrintfW(line3, ARRAYSIZE(line3), L"%d Hz · %s · %s 前台时启用", profile.pollingHz, dpiText, profile.appName.c_str());
    } else if (fallback) {
        StringCchPrintfW(line3, ARRAYSIZE(line3), L"%d Hz · %s · 已离开关联程序", profile.pollingHz, dpiText);
    } else {
        StringCchPrintfW(line3, ARRAYSIZE(line3), L"%d Hz · %s · 休眠 %d 分钟", profile.pollingHz, dpiText, profile.sleepMinutes);
    }

    Osd::Show(line1, line2, line3);
}

bool ApplyInternal(const Definition& profile, bool showOsd, bool automatic, bool fallback) {
    const Device::State before = Device::GetCurrentState();
    g_store.activeId = profile.id;
    SaveState();

    Definition effective = profile;
    NormalizeDpiPair(effective.dpiX, effective.dpiY);
    bool pollingOk = Device::SetPollingRate(profile.pollingHz);
    bool sleepOk = Device::SetSleepTimeout(profile.sleepMinutes);
    bool dpiOk = true;
    if (effective.dpiX > 0 && effective.dpiY > 0) {
        dpiOk = Device::SetCurrentDpi(effective.dpiX, effective.dpiY);
    }
    if (showOsd) {
        ShowSwitchOsd(profile, automatic, fallback, before.isConnected && pollingOk && dpiOk);
    }
    return pollingOk && sleepOk && dpiOk;
}

void RecordManualOverride(int matchedProfileId) {
    g_manualHoldMatchedProfileId = matchedProfileId;
    if (matchedProfileId != 0) g_autoContextProfileId = matchedProfileId;
}

} // namespace

bool IsSupportedPollingRate(int hz) {
    return ValidPollingRate(hz);
}

int NormalizePollingRate(int hz) {
    int best = SUPPORTED_RATES[0];
    int bestDistance = std::abs(hz - best);
    for (int supported : SUPPORTED_RATES) {
        int distance = std::abs(hz - supported);
        if (distance < bestDistance) {
            best = supported;
            bestDistance = distance;
        }
    }
    return best;
}

bool Initialize() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    if (!g_store.initialized) LoadState();
    return !g_store.profiles.empty();
}

void Reload() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    g_autoContextProfileId = 0;
    g_manualHoldMatchedProfileId = 0;
    LoadState();
}

std::vector<Definition> GetAll() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    return g_store.profiles;
}

bool Find(int id, Definition& outProfile) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    const Definition* profile = FindInternalConst(id);
    if (!profile) return false;
    outProfile = *profile;
    return true;
}

int DefaultId() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    return g_store.defaultId;
}

int ActiveId() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    return g_store.activeId;
}

bool HasAppRule(const Definition& profile) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    return !profile.appPath.empty();
}

int Create(const std::wstring& requestedName, int pollingHz, int sleepMinutes, int dpiX, int dpiY) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    if ((int)g_store.profiles.size() >= MAX_PROFILES) return 0;

    std::wstring name = Trim(requestedName);
    if (name.empty()) name = MakeUniqueName();
    if (ProfileNameExists(name, 0)) return 0;

    Definition profile;
    profile.id = g_store.nextId++;
    profile.name = name;
    profile.pollingHz = ValidPollingRate(pollingHz) ? pollingHz : 1000;
    profile.sleepMinutes = Rapoo::ClampSleepMinutes(sleepMinutes);
    profile.dpiX = dpiX;
    profile.dpiY = dpiY;
    NormalizeDpiPair(profile.dpiX, profile.dpiY);
    g_store.profiles.push_back(profile);
    SaveState();
    return profile.id;
}

bool Update(const Definition& requested) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    Definition* profile = FindInternal(requested.id);
    if (!profile) return false;

    std::wstring name = Trim(requested.name);
    if (name.empty() || ProfileNameExists(name, requested.id)) return false;

    Definition updated = requested;
    updated.name = name;
    updated.pollingHz = ValidPollingRate(requested.pollingHz) ? requested.pollingHz : 1000;
    updated.sleepMinutes = Rapoo::ClampSleepMinutes(requested.sleepMinutes);
    updated.dpiX = requested.dpiX;
    updated.dpiY = requested.dpiY;
    NormalizeDpiPair(updated.dpiX, updated.dpiY);
    updated.appPath = NormalizeAppPath(requested.appPath);
    updated.appName = updated.appPath.empty() ? std::wstring() : FileNameFromPath(updated.appPath);
    *profile = updated;
    SaveState();

    if (g_store.activeId == updated.id) {
        ApplyInternal(updated, false, false, false);
    }
    return true;
}

bool Delete(int id) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    if (g_store.profiles.size() <= 1) return false;

    auto it = std::find_if(g_store.profiles.begin(), g_store.profiles.end(), [id](const Definition& profile) {
        return profile.id == id;
    });
    if (it == g_store.profiles.end()) return false;

    bool wasDefault = (g_store.defaultId == id);
    bool wasActive = (g_store.activeId == id);
    g_store.profiles.erase(it);

    if (wasDefault) g_store.defaultId = g_store.profiles.front().id;
    if (wasActive) g_store.activeId = g_store.defaultId;
    if (!FindInternalConst(g_store.defaultId)) g_store.defaultId = g_store.profiles.front().id;
    if (!FindInternalConst(g_store.activeId)) g_store.activeId = g_store.defaultId;

    SaveState();
    if (wasActive) {
        const Definition* active = FindInternalConst(g_store.activeId);
        if (active) ApplyInternal(*active, false, false, false);
    }
    return true;
}

void SetDefault(int id) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    if (!FindInternalConst(id)) return;
    g_store.defaultId = id;
    SaveState();
}

bool Select(int id) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    const Definition* profile = FindInternalConst(id);
    if (!profile) return false;

    std::wstring processPath;
    int matched = 0;
    if (g_store.autoSwitch && TryGetForegroundProcessPath(processPath)) {
        matched = FindMatchingProfile(processPath);
    }
    RecordManualOverride(matched);

    Definition selected = *profile;
    return ApplyInternal(selected, true, false, false);
}

bool SetCurrentPolling(int hz) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    hz = ValidPollingRate(hz) ? hz : NormalizePollingRate(hz);
    Definition* active = FindInternal(g_store.activeId);
    if (active) {
        active->pollingHz = hz;
        SaveState();
    }

    std::wstring processPath;
    int matched = 0;
    if (g_store.autoSwitch && TryGetForegroundProcessPath(processPath)) {
        matched = FindMatchingProfile(processPath);
    }
    RecordManualOverride(matched);
    return Device::SetPollingRate(hz);
}

bool SetCurrentSleep(int minutes) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    minutes = Rapoo::ClampSleepMinutes(minutes);
    Definition* active = FindInternal(g_store.activeId);
    if (active) {
        active->sleepMinutes = minutes;
        SaveState();
    }

    std::wstring processPath;
    int matched = 0;
    if (g_store.autoSwitch && TryGetForegroundProcessPath(processPath)) {
        matched = FindMatchingProfile(processPath);
    }
    RecordManualOverride(matched);
    return Device::SetSleepTimeout(minutes);
}

bool SetCurrentDpi(int x, int y) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    NormalizeDpiPair(x, y);
    if (x <= 0 || y <= 0) return false;

    Definition* active = FindInternal(g_store.activeId);
    if (active) {
        active->dpiX = x;
        active->dpiY = y;
        SaveState();
    }

    std::wstring processPath;
    int matched = 0;
    if (g_store.autoSwitch && TryGetForegroundProcessPath(processPath)) {
        matched = FindMatchingProfile(processPath);
    }
    RecordManualOverride(matched);
    return Device::SetCurrentDpi(x, y);
}

bool AutoSwitchEnabled() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    return g_store.autoSwitch;
}

void SetAutoSwitchEnabled(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    g_store.autoSwitch = enabled;
    if (!enabled) {
        g_autoContextProfileId = 0;
        g_manualHoldMatchedProfileId = 0;
    }
    SaveState();
}

void PollForeground(bool force) {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    if (!g_store.autoSwitch) return;

    const Device::State state = Device::GetCurrentState();
    if (!state.isConnected) return;

    std::wstring processPath;
    if (!TryGetForegroundProcessPath(processPath)) return;
    const int matched = FindMatchingProfile(processPath);

    if (matched != 0) {
        if (g_manualHoldMatchedProfileId == matched) {
            g_autoContextProfileId = matched;
            return;
        }
        g_manualHoldMatchedProfileId = 0;
        g_autoContextProfileId = matched;

        const Definition* profile = FindInternalConst(matched);
        if (profile && (force || g_store.activeId != matched)) {
            ApplyInternal(*profile, true, true, false);
        }
        return;
    }

    g_manualHoldMatchedProfileId = 0;
    if (g_autoContextProfileId != 0 || force) {
        const Definition* fallback = FindInternalConst(g_store.defaultId);
        const bool leavingMatchedApp = (g_autoContextProfileId != 0);
        if (fallback && (force || g_store.activeId != fallback->id)) {
            // Initial/reconnect synchronization should stay quiet; only an actual
            // app-to-desktop transition gets a visible fallback notification.
            ApplyInternal(*fallback, leavingMatchedApp, false, true);
        }
        g_autoContextProfileId = 0;
    }
}

void OnDeviceConnected() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    g_autoContextProfileId = 0;
    g_manualHoldMatchedProfileId = 0;
    // The device retains its hardware settings across restarts. Reapplying the
    // active profile here causes unnecessary polling/DPI writes and a visible
    // cursor freeze at login, so profiles are applied only on an actual switch.
}

std::wstring MakeUniqueName() {
    std::lock_guard<std::recursive_mutex> lock(g_apiMutex);
    EnsureInitialized();
    const std::wstring base = L"新配置";
    if (!ProfileNameExists(base, 0)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::wstring candidate = base + L" " + std::to_wstring(i);
        if (!ProfileNameExists(candidate, 0)) return candidate;
    }
    return base;
}

} // namespace Profiles


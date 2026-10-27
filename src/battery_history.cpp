#include "battery_history.h"

#include "device_manager.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

namespace BatteryHistory {
namespace {

constexpr int64_t RETENTION_SECONDS = 30LL * 24 * 60 * 60;
constexpr int64_t SAMPLE_INTERVAL_SECONDS = 5 * 60;
constexpr size_t MAX_STORED_SAMPLES = 12000;
constexpr const WCHAR* APP_KEY = L"Software\\rapoo-tray";

std::mutex g_mutex;
bool g_initialized = false;
bool g_dirty = false;
std::wstring g_path;
std::vector<Sample> g_samples;
BatteryAlert::Settings g_alertSettings;

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

DWORD ReadDword(HKEY key, const WCHAR* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD) {
        return fallback;
    }
    return value;
}

BatteryAlert::Settings LoadAlertSettings() {
    BatteryAlert::Settings settings;
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return BatteryAlert::NormalizeSettings(settings);
    }
    settings.enabled = ReadDword(key, L"BatteryAlertsEnabled", settings.enabled ? 1 : 0) != 0;
    settings.warningThreshold = (int)ReadDword(key, L"BatteryWarningThreshold", settings.warningThreshold);
    settings.criticalThreshold = (int)ReadDword(key, L"BatteryCriticalThreshold", settings.criticalThreshold);
    settings.chargingReminderEnabled = ReadDword(key, L"ChargingReminderEnabled", settings.chargingReminderEnabled ? 1 : 0) != 0;
    settings.reminderMinutes = (int)ReadDword(key, L"ChargingReminderMinutes", settings.reminderMinutes);
    RegCloseKey(key);
    return BatteryAlert::NormalizeSettings(settings);
}

std::wstring DefaultDataDirectory() {
    WCHAR path[MAX_PATH] = {0};
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, path) != S_OK) {
        WCHAR temp[MAX_PATH] = {0};
        DWORD length = GetTempPathW(ARRAYSIZE(temp), temp);
        if (length == 0 || length >= ARRAYSIZE(temp)) return L".";
        StringCchCatW(temp, ARRAYSIZE(temp), L"rapoo-tray");
        return temp;
    }
    std::wstring directory = path;
    if (!directory.empty() && directory.back() != L'\\') directory += L'\\';
    directory += L"rapoo-tray";
    return directory;
}

bool EnsureDirectory(const std::wstring& directory) {
    if (directory.empty()) return false;
    int result = SHCreateDirectoryExW(NULL, directory.c_str(), NULL);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           GetFileAttributesW(directory.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ReadTextFile(const std::wstring& path, std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {0};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    text.resize((size_t)size.QuadPart);
    DWORD total = 0;
    while (total < text.size()) {
        DWORD read = 0;
        if (!ReadFile(file, text.data() + total, (DWORD)(text.size() - total), &read, NULL) || read == 0) {
            break;
        }
        total += read;
    }
    CloseHandle(file);
    text.resize(total);
    return total > 0;
}

bool WriteTextFileAtomic(const std::wstring& path, const std::string& text) {
    std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD total = 0;
    bool ok = true;
    while (total < text.size()) {
        DWORD written = 0;
        if (!WriteFile(file, text.data() + total, (DWORD)(text.size() - total), &written, NULL) || written == 0) {
            ok = false;
            break;
        }
        total += written;
    }
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

void SaveLocked() {
    if (!g_initialized || !g_dirty) return;
    std::ostringstream out;
    out << "# rapoo-tray battery history v1\n";
    for (const Sample& sample : g_samples) {
        out << sample.timestamp << ',' << sample.battery << ','
            << (sample.charging ? 1 : 0) << ',' << (sample.wired ? 1 : 0) << '\n';
    }
    if (WriteTextFileAtomic(g_path, out.str())) g_dirty = false;
}

void LoadLocked() {
    g_samples.clear();
    std::string text;
    if (!ReadTextFile(g_path, text)) return;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t first = line.find(',');
        size_t second = first == std::string::npos ? first : line.find(',', first + 1);
        size_t third = second == std::string::npos ? second : line.find(',', second + 1);
        if (first == std::string::npos || second == std::string::npos || third == std::string::npos) continue;
        try {
            Sample sample;
            sample.timestamp = std::stoll(line.substr(0, first));
            sample.battery = std::stoi(line.substr(first + 1, second - first - 1));
            sample.charging = line.substr(second + 1, third - second - 1) == "1";
            sample.wired = line.substr(third + 1) == "1";
            if (sample.battery < 0 || sample.battery > 100) continue;
            g_samples.push_back(sample);
        } catch (...) {
        }
    }
    std::sort(g_samples.begin(), g_samples.end(), [](const Sample& a, const Sample& b) {
        return a.timestamp < b.timestamp;
    });
    const int64_t cutoff = NowSeconds() - RETENTION_SECONDS;
    g_samples.erase(std::remove_if(g_samples.begin(), g_samples.end(), [&](const Sample& sample) {
        return sample.timestamp < cutoff;
    }), g_samples.end());
    if (g_samples.size() > MAX_STORED_SAMPLES) {
        g_samples.erase(g_samples.begin(), g_samples.end() - (std::ptrdiff_t)MAX_STORED_SAMPLES);
    }
}

void EnsureInitializedLocked() {
    if (g_initialized) return;
    g_alertSettings = LoadAlertSettings();
    std::wstring directory = DefaultDataDirectory();
    EnsureDirectory(directory);
    g_path = directory + L"\\battery-history.tsv";
    g_initialized = true;
    LoadLocked();
}

} // namespace

bool Initialize(const std::wstring& dataDirectory) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        SaveLocked();
        g_samples.clear();
    }
    g_alertSettings = LoadAlertSettings();
    std::wstring directory = dataDirectory.empty() ? DefaultDataDirectory() : dataDirectory;
    if (!EnsureDirectory(directory)) return false;
    g_path = directory;
    if (!g_path.empty() && g_path.back() != L'\\') g_path += L'\\';
    g_path += L"battery-history.tsv";
    g_initialized = true;
    g_dirty = false;
    LoadLocked();
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    SaveLocked();
    g_initialized = false;
    g_samples.clear();
    g_path.clear();
    g_dirty = false;
}

void Observe(const Device::State& state) {
    ObserveAt(state, NowSeconds());
}

void ObserveAt(const Device::State& state, int64_t timestamp) {
    if (!state.isConnected || state.battery < 0 || state.battery > 100) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureInitializedLocked();
    const bool changed = g_samples.empty() ||
        g_samples.back().battery != state.battery ||
        g_samples.back().charging != state.isCharging ||
        g_samples.back().wired != state.isWired ||
        timestamp >= g_samples.back().timestamp + SAMPLE_INTERVAL_SECONDS ||
        timestamp < g_samples.back().timestamp;
    if (!changed) return;

    Sample sample;
    sample.timestamp = timestamp;
    sample.battery = state.battery;
    sample.charging = state.isCharging;
    sample.wired = state.isWired;
    g_samples.push_back(sample);
    if (g_samples.size() > MAX_STORED_SAMPLES) {
        g_samples.erase(g_samples.begin(), g_samples.end() - (std::ptrdiff_t)MAX_STORED_SAMPLES);
    }
    g_dirty = true;
    SaveLocked();
}

std::vector<Sample> Query(int days, int maxPoints) {
    return QueryAt(days, NowSeconds(), maxPoints);
}

std::vector<Sample> QueryAt(int days, int64_t now, int maxPoints) {
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureInitializedLocked();
    days = std::clamp(days, 1, 30);
    maxPoints = std::clamp(maxPoints, 24, 4000);
    const int64_t cutoff = now - (int64_t)days * 24 * 60 * 60;
    std::vector<Sample> result;
    for (const Sample& sample : g_samples) {
        if (sample.timestamp >= cutoff && sample.timestamp <= now) result.push_back(sample);
    }
    if ((int)result.size() <= maxPoints) return result;

    const size_t stride = (result.size() + (size_t)maxPoints - 1) / (size_t)maxPoints;
    std::vector<Sample> downsampled;
    downsampled.reserve((size_t)maxPoints + 32);
    for (size_t i = 0; i < result.size(); ++i) {
        const bool transition = i == 0 || result[i].charging != result[i - 1].charging ||
                                result[i].wired != result[i - 1].wired;
        if (i % stride == 0 || transition || i + 1 == result.size()) downsampled.push_back(result[i]);
    }
    if (!result.empty() && (downsampled.empty() || downsampled.back().timestamp != result.back().timestamp)) {
        downsampled.push_back(result.back());
    }
    if ((int)downsampled.size() > maxPoints) downsampled.resize((size_t)maxPoints);
    return downsampled;
}

std::wstring HistoryPath() {
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureInitializedLocked();
    return g_path;
}

BatteryAlert::Settings GetAlertSettings() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) g_alertSettings = LoadAlertSettings();
    return BatteryAlert::NormalizeSettings(g_alertSettings);
}

bool SetAlertSettings(const BatteryAlert::Settings& input) {
    const BatteryAlert::Settings settings = BatteryAlert::NormalizeSettings(input);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_alertSettings = settings;
    }
    HKEY key = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return false;
    }
    auto setDword = [&](const WCHAR* name, DWORD value) {
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    };
    setDword(L"BatteryAlertsEnabled", settings.enabled ? 1u : 0u);
    setDword(L"BatteryWarningThreshold", (DWORD)settings.warningThreshold);
    setDword(L"BatteryCriticalThreshold", (DWORD)settings.criticalThreshold);
    setDword(L"ChargingReminderEnabled", settings.chargingReminderEnabled ? 1u : 0u);
    setDword(L"ChargingReminderMinutes", (DWORD)settings.reminderMinutes);
    RegCloseKey(key);
    return true;
}

} // namespace BatteryHistory

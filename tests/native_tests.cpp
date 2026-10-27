#include "battery_alert_policy.h"
#include "battery_history.h"
#include "device_manager.h"
#include "osd_config.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "[FAIL] " << message << '\n';
}

void TestHotkeyConfiguration() {
    UINT modifiers = 0;
    UINT virtualKey = 0;
    Check(OsdConfig::NormalizeHotkey(MOD_CONTROL | MOD_ALT, 'M', modifiers, virtualKey),
          "Ctrl+Alt+M should be a valid hotkey");
    Check(modifiers == (MOD_CONTROL | MOD_ALT) && virtualKey == 'M', "hotkey should preserve supported keys");
    Check(OsdConfig::FormatHotkey(modifiers, virtualKey) == L"Ctrl + Alt + M", "hotkey formatting should be stable");
    Check(!OsdConfig::NormalizeHotkey(0, 'M', modifiers, virtualKey), "plain letters require a modifier");
    Check(OsdConfig::NormalizeHotkey(0, VK_F8, modifiers, virtualKey), "function keys may be unmodified");
    Check(!OsdConfig::NormalizeHotkey(MOD_CONTROL, VK_ESCAPE, modifiers, virtualKey), "Escape is reserved");
    Check(!OsdConfig::NormalizeHotkey(MOD_CONTROL, VK_CONTROL, modifiers, virtualKey), "modifier-only hotkeys are invalid");
    Check(OsdConfig::NormalizeDisplayMask(0xFFFFFFFFu) == OsdConfig::FIELD_ALL, "display mask should strip unknown bits");
    Check(OsdConfig::NormalizeDisplayMask(0) == 0, "empty display mask is configurable");
    Check(OsdConfig::NormalizePanelMask(0xFFFFFFFFu) == OsdConfig::PANEL_ALL, "panel mask should strip unknown bits");
}

void TestBatteryAlertPolicy() {
    BatteryAlert::Policy policy;
    BatteryAlert::Settings settings;
    settings.warningThreshold = 20;
    settings.criticalThreshold = 10;
    settings.reminderMinutes = 30;

    Check(policy.Update(50, false, 1000, settings) == BatteryAlert::Event::None, "healthy battery should not alert");
    Check(policy.Update(18, false, 1100, settings) == BatteryAlert::Event::Warning, "warning threshold should alert once");
    Check(policy.Update(18, false, 1200, settings) == BatteryAlert::Event::None, "warning should not repeat immediately");
    Check(policy.Update(18, false, 1100 + 30 * 60, settings) == BatteryAlert::Event::ChargeReminder,
          "charging reminder should fire after the configured interval");
    Check(policy.Update(18, false, 1100 + 31 * 60, settings) == BatteryAlert::Event::None,
          "charging reminder should not repeat before the next interval");
    Check(policy.Update(8, false, 1100 + 32 * 60, settings) == BatteryAlert::Event::Critical,
          "critical threshold should escalate once");
    Check(policy.Update(8, true, 1100 + 33 * 60, settings) == BatteryAlert::Event::None,
          "charging should suppress low battery alerts");
    Check(policy.Update(35, false, 1100 + 34 * 60, settings) == BatteryAlert::Event::None,
          "charged battery should reset alert state");
    Check(policy.Update(18, false, 1100 + 35 * 60, settings) == BatteryAlert::Event::Warning,
          "warning should be available again after recovery");

    BatteryAlert::Settings invalid;
    invalid.warningThreshold = 2;
    invalid.criticalThreshold = 99;
    invalid.reminderMinutes = 1;
    const BatteryAlert::Settings normalized = BatteryAlert::NormalizeSettings(invalid);
    Check(normalized.warningThreshold == 5 && normalized.criticalThreshold == 4 && normalized.reminderMinutes == 5,
          "invalid alert settings must be clamped");

    settings.enabled = false;
    Check(policy.Update(5, false, 5000, settings) == BatteryAlert::Event::None, "disabled alerts should not fire");
}

void TestBatteryHistoryPersistence() {
    WCHAR temp[MAX_PATH] = {0};
    DWORD length = GetTempPathW(ARRAYSIZE(temp), temp);
    Check(length > 0 && length < ARRAYSIZE(temp), "temp path should be available");
    if (length == 0 || length >= ARRAYSIZE(temp)) return;
    std::wstring directory = temp;
    directory += L"rapoo-tray-native-tests-";
    directory += std::to_wstring(GetTickCount64());

    Check(BatteryHistory::Initialize(directory), "battery history should initialize in a test directory");
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Device::State state;
    state.isConnected = true;
    state.battery = 82;
    state.isCharging = false;
    state.isWired = false;
    BatteryHistory::ObserveAt(state, now - 1200);
    BatteryHistory::ObserveAt(state, now - 1100);
    state.battery = 81;
    BatteryHistory::ObserveAt(state, now - 1000);
    state.isCharging = true;
    state.isWired = true;
    state.battery = 82;
    BatteryHistory::ObserveAt(state, now - 900);

    auto samples = BatteryHistory::QueryAt(1, now, 100);
    Check(samples.size() == 3, "history should coalesce duplicate observations and retain transitions");
    Check(samples.size() >= 2 && samples.back().charging && samples.back().wired, "charging transition should persist");
    const std::wstring path = BatteryHistory::HistoryPath();
    Check(GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES, "history file should be written");
    BatteryHistory::Shutdown();
    Check(BatteryHistory::Initialize(directory), "battery history should reload after shutdown");
    samples = BatteryHistory::QueryAt(1, now, 100);
    Check(samples.size() == 3, "history should survive a process-style reload");
    Check(samples.back().charging, "charging state should survive reload");
    BatteryHistory::Shutdown();

    DeleteFileW(path.c_str());
    RemoveDirectoryW(directory.c_str());
}

} // namespace

int main() {
    TestHotkeyConfiguration();
    TestBatteryAlertPolicy();
    TestBatteryHistoryPersistence();
    if (g_failures == 0) {
        std::cout << "[PASS] native logic tests\n";
        return 0;
    }
    std::cerr << g_failures << " native logic test(s) failed\n";
    return 1;
}

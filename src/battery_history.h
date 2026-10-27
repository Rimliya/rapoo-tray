#pragma once

#include "battery_alert_policy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Device { struct State; }

namespace BatteryHistory {

struct Sample {
    int64_t timestamp = 0;
    int battery = 0;
    bool charging = false;
    bool wired = false;
};

bool Initialize(const std::wstring& dataDirectory = std::wstring());
void Shutdown();

void Observe(const Device::State& state);
void ObserveAt(const Device::State& state, int64_t timestamp);

std::vector<Sample> Query(int days, int maxPoints = 1200);
std::vector<Sample> QueryAt(int days, int64_t now, int maxPoints = 1200);
std::wstring HistoryPath();

BatteryAlert::Settings GetAlertSettings();
bool SetAlertSettings(const BatteryAlert::Settings& settings);

} // namespace BatteryHistory

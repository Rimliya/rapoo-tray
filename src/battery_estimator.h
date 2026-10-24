#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace Device { struct State; }

namespace BatteryEstimate {

struct ModelOption {
    std::wstring key;
    std::wstring label;
};

struct Result {
    bool available = false;
    bool autoDetected = true;
    std::wstring modelLabel;
    int fullMinHours = 0;
    int fullMaxHours = 0;
    int remainingMinHours = 0;
    int remainingMaxHours = 0;
};

const std::vector<ModelOption>& Models();
std::wstring LoadOverride();
void SaveOverride(const std::wstring& key);
std::wstring DetectModelKey(const Device::State& state);
Result Calculate(const Device::State& state);

} // namespace BatteryEstimate
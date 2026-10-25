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

struct DpiSpec {
    int minDpi = 50;
    int maxDpi = 26000;
    int step = 10;
    int maxSlots = 7;
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
std::wstring ResolveModelKey(const Device::State& state);
bool GetDpiSpecForState(const Device::State& state, DpiSpec& outSpec,
                        std::wstring* outKey = nullptr,
                        std::wstring* outLabel = nullptr);
Result Calculate(const Device::State& state);

} // namespace BatteryEstimate
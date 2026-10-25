#pragma once
#include <windows.h>
#include <vector>
#include "rapoo_protocol.h"

namespace Device {

constexpr int MAX_DPI_SLOTS = 7;

struct DpiConfig {
    int currentIndex = 0;
    std::vector<int> dpiX;
    std::vector<int> dpiY;
};

struct State {
    bool isConnected = false;
    bool isWired = false;
    bool isCharging = false;
    int battery = 100;
    int dpiLevel = 1;
    int dpiX = 800;
    int dpiY = 800;
    int pollingHz = 1000;
    int sleepMinutes = 10;
    bool rfFull = false;
    bool rfAvailable = false;
    WCHAR modelName[64] = {0};
};

typedef void (*StateCallback)(const State& state, DWORD changeMask);

constexpr DWORD CHANGE_CONNECTED = 0x01;
constexpr DWORD CHANGE_BATTERY   = 0x02;
constexpr DWORD CHANGE_DPI       = 0x04;
constexpr DWORD CHANGE_POLLING   = 0x08;
constexpr DWORD CHANGE_SLEEP     = 0x10;
constexpr DWORD CHANGE_RF        = 0x20;

bool Start(HWND hNotifyWnd, StateCallback callback);
void Stop();

void NotifyDeviceChange();

bool SetPollingRate(int hz);
bool SetSleepTimeout(int minutes);
bool RefreshPollingRate();
bool RefreshRfState();
bool SetRfFull(bool full);
bool ReadDpiConfig(DpiConfig& outConfig);
bool WriteDpiConfig(const DpiConfig& config);
bool SetCurrentDpi(int x, int y);
bool SetDpiIndex(int index);
bool IsDpiConfigValid();
bool IsStarted();
void ForceRefresh();

State GetCurrentState();

} // namespace Device

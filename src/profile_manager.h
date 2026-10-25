#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace Profiles {

struct Definition {
    int id = 0;
    std::wstring name;
    int pollingHz = 1000;
    int sleepMinutes = 10;
    std::wstring appPath;
    std::wstring appName;
    // Zero means the profile predates DPI capture. The settings editor fills
    // it from the connected mouse the next time the profile is saved.
    int dpiX = 0;
    int dpiY = 0;
};

// Loads profiles from HKCU and creates a small default set on first run.
bool Initialize();
void Reload();

std::vector<Definition> GetAll();
bool Find(int id, Definition& outProfile);

int DefaultId();
int ActiveId();
bool HasAppRule(const Definition& profile);

// Profile editing. Names are trimmed and must be unique (case-insensitive).
int Create(const std::wstring& name, int pollingHz = 1000, int sleepMinutes = 10,
           int dpiX = 0, int dpiY = 0);
bool Update(const Definition& profile);
bool Delete(int id);
void SetDefault(int id);

// Quick/manual profile switch. The profile remains active until an app rule takes
// control or the user selects another profile.
bool Select(int id);

// Keeps a manually selected current profile in sync when the existing tray
// polling/sleep controls are used directly.
bool SetCurrentPolling(int hz);
bool SetCurrentSleep(int minutes);
bool SetCurrentDpi(int x, int y);

bool AutoSwitchEnabled();
void SetAutoSwitchEnabled(bool enabled);

// Foreground application rule engine. PollForeground is intended to be called
// on the main UI thread (about twice a second).
void PollForeground(bool force = false);
void OnDeviceConnected();

std::wstring MakeUniqueName();

bool IsSupportedPollingRate(int hz);
int NormalizePollingRate(int hz);

} // namespace Profiles

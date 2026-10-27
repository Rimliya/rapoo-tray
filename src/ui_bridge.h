#pragma once

#include <windows.h>

namespace UiBridge {

constexpr UINT WM_APP_UI_COMMAND = WM_APP + 0x51;

// Starts the local-only request pipe used by rapoo-tray-settings.exe.
bool Start(HWND mainWindow);

// Stops the pipe server and wakes any blocked connection.
void Stop();

// Executes a command synchronously on the tray UI thread. The pipe server
// calls this through WM_APP_UI_COMMAND so profile and device state keep their
// existing single-threaded ownership.
LRESULT HandleMainMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

} // namespace UiBridge

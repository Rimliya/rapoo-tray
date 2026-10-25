#pragma once

#include <windows.h>

namespace DpiWindow {
void Show(HWND owner);
void Cleanup();
bool PreTranslateMessage(MSG* msg);
} // namespace DpiWindow

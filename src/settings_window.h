#pragma once

#include <windows.h>

namespace Settings {

void Show(HWND owner);
void ShowPage(HWND owner, int page);
void Cleanup();
bool PreTranslateMessage(MSG* msg);

} // namespace Settings

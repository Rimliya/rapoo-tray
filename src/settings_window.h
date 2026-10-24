#pragma once

#include <windows.h>

namespace Settings {

void Show(HWND owner);
void Cleanup();
bool PreTranslateMessage(MSG* msg);

} // namespace Settings
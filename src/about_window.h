#pragma once

#include <windows.h>

namespace About {

void Show(HWND owner);
void Cleanup();
bool PreTranslateMessage(MSG* msg);

} // namespace About

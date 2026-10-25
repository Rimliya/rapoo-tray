#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace DpiWindow {

struct QuickSlot {
    int x = 800;
    int y = 800;
};

struct QuickSlotList {
    std::vector<QuickSlot> slots;
    int activeIndex = 0;
    bool fromDevice = false;
    std::wstring modelKey;
    std::wstring modelLabel;
};

void Show(HWND owner);
void Cleanup();
bool PreTranslateMessage(MSG* msg);

// Shared by the DPI editor and the tray quick-switch submenu. The list comes
// from the device first and falls back to the per-model local DPI cache.
bool GetQuickSlots(QuickSlotList& out);
bool SelectQuickSlot(int index);

} // namespace DpiWindow

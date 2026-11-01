#include "about_window.h"
#include "settings_window.h"

namespace About {

void Show(HWND owner) {
    Settings::ShowPage(owner, 6);
}

void Cleanup() {
    Settings::Cleanup();
}

bool PreTranslateMessage(MSG*) {
    return false;
}

} // namespace About

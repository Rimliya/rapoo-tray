#include <windows.h>
#include <cstdio>
#include "device_manager.h"

static void OnState(const Device::State&, DWORD) {}

int wmain() {
    FILE* log = _wfopen(L"hid-probe-result.txt", L"w, ccs=UTF-8");
    if (!Device::Start(NULL, OnState)) {
        if (log) fwprintf(log, L"Device::Start failed\n");
        if (log) fclose(log);
        return 1;
    }

    Device::State state;
    for (int i = 0; i < 75; ++i) {
        Sleep(200);
        state = Device::GetCurrentState();
        if (state.isConnected) break;
    }

    Device::DpiConfig dpi;
    bool dpiOk = state.isConnected && Device::ReadDpiConfig(dpi);
    if (log) {
        fwprintf(log, L"Connected: %d\n", state.isConnected ? 1 : 0);
        fwprintf(log, L"Wired: %d\n", state.isWired ? 1 : 0);
        fwprintf(log, L"Model: %s\n", state.modelName);
        fwprintf(log, L"Battery: %d\n", state.battery);
        fwprintf(log, L"PollingHz: %d\n", state.pollingHz);
        fwprintf(log, L"RF available: %d\n", state.rfAvailable ? 1 : 0);
        fwprintf(log, L"RF full: %d\n", state.rfFull ? 1 : 0);
        fwprintf(log, L"DPI read: %d\n", dpiOk ? 1 : 0);
        if (dpiOk) {
            fwprintf(log, L"DPI count: %d\n", (int)dpi.dpiX.size());
            fwprintf(log, L"DPI index: %d\n", dpi.currentIndex);
            for (size_t i = 0; i < dpi.dpiX.size(); ++i) {
                fwprintf(log, L"DPI[%d]: X=%d Y=%d\n", (int)i, dpi.dpiX[i], dpi.dpiY[i]);
            }
        }
        fclose(log);
    }
    Device::Stop();
    return 0;
}
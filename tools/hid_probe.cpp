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
    Device::IoDebugInfo io = Device::GetIoDebugInfo();
    if (log) {
        fwprintf(log, L"Control open: %d\n", io.controlOpen ? 1 : 0);
        fwprintf(log, L"Feature open: %d\n", io.featureOpen ? 1 : 0);
        fwprintf(log, L"Feature error: %lu\n", (unsigned long)io.featureError);
        fwprintf(log, L"Feature bytes: %lu\n", (unsigned long)io.featureBytes);
        fwprintf(log, L"Feature data:");
        for (DWORD i = 0; i < io.featureBytes; ++i) fwprintf(log, L" %02X", io.featureData[i]);
        fwprintf(log, L"\n");
        fwprintf(log, L"nRF54L: %d\n", io.nrf54l ? 1 : 0);
        fwprintf(log, L"Control error: %lu\n", (unsigned long)io.controlError);
        fwprintf(log, L"Control bytes: %lu\n", (unsigned long)io.controlBytes);
        fwprintf(log, L"Control data:");
        for (DWORD i = 0; i < io.controlBytes; ++i) fwprintf(log, L" %02X", io.controlData[i]);
        fwprintf(log, L"\n");
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
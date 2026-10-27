#include "device_manager.h"
extern "C" {
#include <hidsdi.h>
}
#include <setupapi.h>
#include <strsafe.h>
#include <algorithm>
#include <atomic>
#include <cstring>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace Device {

static const WCHAR* REG_SUBKEY = L"Software\\rapoo-tray";

static State g_currentState;
static StateCallback g_callback = nullptr;
static HWND g_hNotifyWnd = NULL;

static HANDLE g_hWorkerThread = NULL;
static HANDLE g_hStopEvent = NULL;
static HANDLE g_hDevChangeEvent = NULL;

static CRITICAL_SECTION g_csDevIO;
static CRITICAL_SECTION g_csState;
static HANDLE g_hControlDev = INVALID_HANDLE_VALUE;
static HANDLE g_hFeatureDev = INVALID_HANDLE_VALUE;
static bool g_controlUnlocked = false;

static int g_cachedBattery = 100;
static std::atomic<bool> g_started{false};
static std::atomic<bool> g_pollingKnown{false};
static std::atomic<bool> g_perfKnown{false};
static bool g_dpiConfigValid = false;
static DpiConfig g_cachedDpiConfig;
static BYTE g_perfRegion[16] = {};
static bool g_perfRegionValid = false;

// Registry helper for persistent settings
static DWORD LoadRegistryDword(const WCHAR* valName, DWORD defVal) {
    HKEY hKey;
    DWORD val = defVal;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0, size = sizeof(DWORD);
        RegQueryValueExW(hKey, valName, NULL, &type, (LPBYTE)&val, &size);
        RegCloseKey(hKey);
    }
    return val;
}

static void SaveRegistryDword(const WCHAR* valName, DWORD val) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, valName, 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

// Caller must hold g_csDevIO. Vendor control traffic is unlocked lazily so a
// normal startup with no pending setting changes does not touch the mouse.
static bool EnsureDeviceUnlockedLocked() {
    if (g_controlUnlocked) return true;
    if (g_hControlDev == INVALID_HANDLE_VALUE) return false;

    BYTE unlockBuf[33] = {0};
    DWORD unlockSize = Rapoo::BuildUnlockPacket(unlockBuf, sizeof(unlockBuf));
    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return false;

    BOOL ok = WriteFile(g_hControlDev, unlockBuf, unlockSize, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        ok = WaitForSingleObject(ov.hEvent, 800) == WAIT_OBJECT_0 &&
             GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
    }
    CloseHandle(ov.hEvent);

    if (!ok || written != unlockSize) return false;
    Sleep(50);
    g_controlUnlocked = true;
    return true;
}

static bool SendRawCommand(BYTE bank, BYTE addr, const BYTE* pData, BYTE len) {
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }
    if (!EnsureDeviceUnlockedLocked()) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    BYTE buf[33] = {0};
    DWORD pktSize = Rapoo::BuildWriteCommand(bank, addr, pData, len, buf, sizeof(buf));

    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL ok = WriteFile(g_hControlDev, buf, pktSize, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 800) == WAIT_OBJECT_0) {
            GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
            ok = (written == pktSize);
        } else {
            CancelIo(g_hControlDev);
            ok = FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    LeaveCriticalSection(&g_csDevIO);
    return (ok && written == pktSize);
}


static bool ReadRegisterBytesAttempts(BYTE bank, BYTE addr, BYTE len, BYTE* outData, int maxAttempts) {
    if (!outData || len == 0 || len > 31) return false;

    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE || g_hFeatureDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }
    if (!EnsureDeviceUnlockedLocked()) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    bool ok = false;
    for (int attempt = 0; attempt < maxAttempts && !ok; ++attempt) {
        BYTE outBuf[33] = {0};
        DWORD pktSize = Rapoo::BuildReadCommand(bank, addr, len, outBuf, sizeof(outBuf));

        DWORD written = 0;
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        BOOL wOk = WriteFile(g_hControlDev, outBuf, pktSize, &written, &ov);
        if (!wOk && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 800) == WAIT_OBJECT_0) {
                GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
                wOk = (written == pktSize);
            } else {
                CancelIo(g_hControlDev);
                wOk = FALSE;
            }
        }
        CloseHandle(ov.hEvent);
        if (!wOk) {
            Sleep(15);
            continue;
        }

        Sleep(50);
        BYTE featBuf[33] = {0};
        featBuf[0] = Rapoo::REPORT_ID_FEATURE;
        BOOL fOk = HidD_GetFeature(g_hFeatureDev, featBuf, sizeof(featBuf));
        ok = fOk && Rapoo::ParseFeatureRead(featBuf, sizeof(featBuf), len, outData);
        if (!ok) Sleep(50 + attempt * 50);
    }

    LeaveCriticalSection(&g_csDevIO);
    return ok;
}

static bool ReadRegisterBytes(BYTE bank, BYTE addr, BYTE len, BYTE* outData) {
    return ReadRegisterBytesAttempts(bank, addr, len, outData, 4);
}

static bool ReadRegisterByte(BYTE bank, BYTE addr, BYTE& outValue) {
    return ReadRegisterBytes(bank, addr, 1, &outValue);
}

static bool ReadPerfModeRegion(BYTE outRegion[16]) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt != 0) Sleep(100);
        if (ReadRegisterBytes(Rapoo::BANK_SYSTEM, Rapoo::ADDR_PERF_MODE_BASE,
                              16, outRegion)) {
            return true;
        }
    }
    return false;
}

static bool PingDevice(int& outHz) {
    BYTE value = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_POLLING_HZ, value)) return false;
    outHz = Rapoo::CodeToPollingHz(value);
    return true;
}


struct RapooModelEntry {
    const WCHAR* pidKeyword;
    const WCHAR* modelName;
};

// 仅收录经由实体硬件测试验证的数据，严禁未经证实的推测
static const RapooModelEntry VERIFIED_MODELS[] = {
    // 雷柏 VT7 系列 (实测已验证)
    { L"1460", L"雷柏 VT7" },
    { L"4660", L"雷柏 VT7" },

    // 雷柏 VT3S 系列 (实测已验证)
    { L"1406", L"雷柏 VT3S" },
    { L"1410", L"雷柏 VT3S" },
    { L"4606", L"雷柏 VT3S" },
    { L"1411", L"雷柏 VT3S" },
    { L"4611", L"雷柏 VT3S" },

    // 雷柏 VT3 V2 系列 (实测已验证；1462/4662 为本机接收器/有线变体)
    { L"1412", L"雷柏 VT3 V2" },
    { L"4612", L"雷柏 VT3 V2" },
    { L"1462", L"雷柏 VT3 V2" },
    { L"4662", L"雷柏 VT3 V2" },

    // 雷柏 VT3 MAX 系列 (实测已验证 - PR #3 by @sAchNMN)
    { L"1417", L"雷柏 VT3 MAX" },
    { L"4617", L"雷柏 VT3 MAX" },
};

static void ResolveRapooModelName(const WCHAR* targetPid, const WCHAR* productName, WCHAR* outModel, DWORD maxModelLen) {
    if (!outModel || maxModelLen == 0) return;

    if (productName && productName[0]) {
        if (wcsstr(productName, L"VT3 MAX")) {
            StringCchCopyW(outModel, maxModelLen, L"雷柏 VT3 MAX");
            return;
        }
        if (wcsstr(productName, L"VT3S")) {
            StringCchCopyW(outModel, maxModelLen, L"雷柏 VT3S");
            return;
        }
        if (wcsstr(productName, L"VT7")) {
            StringCchCopyW(outModel, maxModelLen, L"雷柏 VT7");
            return;
        }
        if (wcsstr(productName, L"VT3 V2") || wcsstr(productName, L"VT3V2")) {
            StringCchCopyW(outModel, maxModelLen, L"雷柏 VT3 V2");
            return;
        }
    }

    for (const auto& entry : VERIFIED_MODELS) {
        if (wcsstr(targetPid, entry.pidKeyword)) {
            StringCchCopyW(outModel, maxModelLen, entry.modelName);
            return;
        }
    }

    // 通用类兜底：兼容所有雷柏二代 Nordic 架构 (54L15/3950) 未打标机型
    StringCchCopyW(outModel, maxModelLen, L"通用");
}

static bool FindRapooEndpoints(WCHAR* pathStatus, WCHAR* pathControl, WCHAR* pathFeature, WCHAR* outModel, DWORD maxModelLen, bool* outIsWired) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA devData = {0};
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    pathStatus[0] = 0;
    pathControl[0] = 0;
    pathFeature[0] = 0;

    WCHAR targetPid[32] = {0};
    WCHAR detectedProduct[128] = {0};
    bool isWired = false;

    // Phase 1: Determine the target device.
    // Scan 1: look specifically for wired Rapoo mice (pid_46xx).
    // If a cable was just plugged in, give Windows PnP up to 500ms to register HID child interfaces.
    for (int retry = 0; retry < 3 && targetPid[0] == 0; ++retry) {
        if (retry > 0) {
            Sleep(250);
            SetupDiDestroyDeviceInfoList(hDevInfo);
            hDevInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (hDevInfo == INVALID_HANDLE_VALUE) return false;
        }

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, NULL, 0, &reqSize, NULL);
            if (reqSize == 0) continue;

            PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(reqSize);
            if (!pDetail) continue;

            pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, pDetail, reqSize, NULL, NULL)) {
                WCHAR lower[MAX_PATH];
                StringCchCopyW(lower, MAX_PATH, pDetail->DevicePath);
                _wcslwr_s(lower, MAX_PATH);

                if (wcsstr(lower, L"vid_24ae")) {
                    const WCHAR* pPid = wcsstr(lower, L"pid_");
                    if (pPid && wcsstr(pPid, L"pid_46")) {
                        StringCchCopyNW(targetPid, 32, pPid, 8);
                        isWired = true;
                        free(pDetail);
                        break;
                    }
                }
            }
            free(pDetail);
        }
    }

    // Second scan: no wired device — take any Rapoo VID (dongle etc.)
    if (targetPid[0] == 0) {
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, NULL, 0, &reqSize, NULL);
            if (reqSize == 0) continue;

            PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(reqSize);
            if (!pDetail) continue;

            pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, pDetail, reqSize, NULL, NULL)) {
                WCHAR lower[MAX_PATH];
                StringCchCopyW(lower, MAX_PATH, pDetail->DevicePath);
                _wcslwr_s(lower, MAX_PATH);

                if (wcsstr(lower, L"vid_24ae")) {
                    const WCHAR* pPid = wcsstr(lower, L"pid_");
                    if (pPid) {
                        StringCchCopyNW(targetPid, 32, pPid, 8);
                        isWired = false;
                        free(pDetail);
                        break;
                    }
                }
            }
            free(pDetail);
        }
    }

    if (targetPid[0] == 0) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    // Phase 2: Find the 3 required endpoints for the target PID
    bool foundStatus = false;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, NULL, 0, &reqSize, NULL);
        if (reqSize == 0) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(reqSize);
        if (!pDetail) continue;

        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, pDetail, reqSize, NULL, NULL)) {
            WCHAR lowerPath[MAX_PATH];
            StringCchCopyW(lowerPath, MAX_PATH, pDetail->DevicePath);
            _wcslwr_s(lowerPath, MAX_PATH);

            if (wcsstr(lowerPath, L"vid_24ae") && wcsstr(lowerPath, targetPid)) {
                HANDLE hProbe = CreateFileW(
                    pDetail->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                if (hProbe == INVALID_HANDLE_VALUE) {
                    hProbe = CreateFileW(
                        pDetail->DevicePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                    );
                }
                if (hProbe == INVALID_HANDLE_VALUE) {
                    hProbe = CreateFileW(
                        pDetail->DevicePath,
                        0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                    );
                }

                if (hProbe != INVALID_HANDLE_VALUE) {
                    WCHAR probeProduct[128] = {0};
                    if (HidD_GetProductString(hProbe, probeProduct, sizeof(probeProduct)) && probeProduct[0]) {
                        bool knownProduct = wcsstr(probeProduct, L"VT3") || wcsstr(probeProduct, L"VT7");
                        if (!detectedProduct[0] || knownProduct) {
                            StringCchCopyW(detectedProduct, ARRAYSIZE(detectedProduct), probeProduct);
                        }
                    }

                    PHIDP_PREPARSED_DATA pData = NULL;
                    if (HidD_GetPreparsedData(hProbe, &pData)) {
                        HIDP_CAPS caps;
                        if (HidP_GetCaps(pData, &caps) == HIDP_STATUS_SUCCESS) {
                            if (caps.UsagePage == 0xFF00) {
                                if (caps.Usage == 0x000E || caps.OutputReportByteLength == 33) {
                                    StringCchCopyW(pathControl, MAX_PATH, pDetail->DevicePath);
                                } else if (caps.Usage == 0x000F || (caps.FeatureReportByteLength == 33 && caps.Usage != 0x0010)) {
                                    StringCchCopyW(pathFeature, MAX_PATH, pDetail->DevicePath);
                                } else if (caps.Usage == 0x0002 || (caps.InputReportByteLength >= 19 && wcsstr(lowerPath, L"col09")) || (caps.InputReportByteLength == 19 && caps.Usage != 0x000E)) {
                                    StringCchCopyW(pathStatus, MAX_PATH, pDetail->DevicePath);
                                    foundStatus = true;
                                }
                            }
                        }
                        HidD_FreePreparsedData(pData);
                    }
                    CloseHandle(hProbe);
                }
            }
        }
        free(pDetail);
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);

    bool ok = (foundStatus && pathControl[0] != 0);
    if (ok) {
        if (outModel && maxModelLen > 0) {
            ResolveRapooModelName(targetPid, detectedProduct, outModel, maxModelLen);
        }
        if (outIsWired) {
            *outIsWired = isWired;
        }
    }

    return ok;
}

static DWORD WINAPI HidWorkerThread(LPVOID lpParam) {
    WCHAR pathStatus[MAX_PATH] = {0};
    WCHAR pathControl[MAX_PATH] = {0};
    WCHAR pathFeature[MAX_PATH] = {0};
    WCHAR modelBuf[64] = {0};
    bool isWired = false;

    // Load user's saved sleep timeout from registry (default 10 min)
    EnterCriticalSection(&g_csState);
    g_currentState.sleepMinutes = (int)LoadRegistryDword(L"SleepTimeout", 10);
    g_currentState.sleepMinutes = Rapoo::ClampSleepMinutes(g_currentState.sleepMinutes);
    LeaveCriticalSection(&g_csState);

    while (WaitForSingleObject(g_hStopEvent, 200) == WAIT_TIMEOUT) {
        if (!FindRapooEndpoints(pathStatus, pathControl, pathFeature, modelBuf, 64, &isWired)) {
            EnterCriticalSection(&g_csState);
            bool wasConn = g_currentState.isConnected;
            g_currentState.isConnected = false;
            g_currentState.isCharging = false;
            g_currentState.perfModeKnown = false;
            g_currentState.rfAvailable = false;
            g_dpiConfigValid = false;
            State copySt = g_currentState;
            LeaveCriticalSection(&g_csState);
            if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);
            Sleep(1000);
            continue;
        }

        HANDLE hStatus = CreateFileW(
            pathStatus,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );

        if (hStatus == INVALID_HANDLE_VALUE) {
            EnterCriticalSection(&g_csState);
            bool wasConn = g_currentState.isConnected;
            g_currentState.isConnected = false;
            g_currentState.isCharging = false;
            g_currentState.perfModeKnown = false;
            g_currentState.rfAvailable = false;
            g_dpiConfigValid = false;
            State copySt = g_currentState;
            LeaveCriticalSection(&g_csState);
            if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);
            Sleep(1000);
            continue;
        }

        EnterCriticalSection(&g_csDevIO);
        if (pathControl[0]) {
            g_hControlDev = CreateFileW(
                pathControl,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                NULL
            );
            if (g_hControlDev == INVALID_HANDLE_VALUE) {
                g_hControlDev = CreateFileW(
                    pathControl,
                    GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED,
                    NULL
                );
            }
        }
        if (pathFeature[0]) {
            g_hFeatureDev = CreateFileW(
                pathFeature,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (g_hFeatureDev == INVALID_HANDLE_VALUE) {
                g_hFeatureDev = CreateFileW(
                    pathFeature,
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
            }
            if (g_hFeatureDev == INVALID_HANDLE_VALUE) {
                g_hFeatureDev = CreateFileW(
                    pathFeature,
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
            }
        }
        g_controlUnlocked = false;
        g_pollingKnown.store(false, std::memory_order_release);
        g_perfKnown.store(false, std::memory_order_release);
        LeaveCriticalSection(&g_csDevIO);

        EnterCriticalSection(&g_csState);
        StringCchCopyW(g_currentState.modelName, ARRAYSIZE(g_currentState.modelName), modelBuf);
        g_currentState.isWired = isWired;
        if (isWired) {
            g_currentState.isCharging = true;
        }
        g_currentState.isConnected = true;
        g_currentState.perfModeKnown = false;
        g_perfRegionValid = false;
        g_dpiConfigValid = false;
        State connSt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (g_callback) g_callback(connSt, CHANGE_CONNECTED | (isWired ? CHANGE_BATTERY : 0));

        // Enter the passive status stream first. Hardware reads and writes are
        // performed lazily when the UI or a profile change actually needs them.

        HANDLE hReadEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        OVERLAPPED ov = {0};
        ov.hEvent = hReadEvent;

        BYTE buf[65] = {0};
        DWORD bytesRead = 0;

        while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
            ResetEvent(hReadEvent);
            BOOL ok = ReadFile(hStatus, buf, 19, &bytesRead, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waitHandles[3] = { g_hStopEvent, hReadEvent, g_hDevChangeEvent };

                    // Keep one passive report read pending. Some Rapoo dongles
                    // visibly stall mouse input for any vendor register access,
                    // and cancelling/reissuing the HID read every few seconds
                    // causes the same kind of intermittent cursor hitch.
                    DWORD waitRes = WaitForMultipleObjects(3, waitHandles, FALSE, INFINITE);

                    if (waitRes == WAIT_OBJECT_0) {
                        CancelIo(hStatus);
                        break;
                    } else if (waitRes == WAIT_OBJECT_0 + 2) {
                        // PnP Debounce: Windows composite device sends multiple arrival messages.
                        // Drain any consecutive events within 600ms before handling reconnect.
                        Sleep(600);
                        while (WaitForSingleObject(g_hDevChangeEvent, 0) == WAIT_OBJECT_0) {
                            ResetEvent(g_hDevChangeEvent);
                            Sleep(100);
                        }
                        CancelIo(hStatus);
                        break;
                    } else if (waitRes == WAIT_OBJECT_0 + 1) {
                        if (!GetOverlappedResult(hStatus, &ov, &bytesRead, FALSE)) {
                            break;
                        }
                    } else {
                        CancelIo(hStatus);
                        break;
                    }
                } else {
                    break;
                }
            }

            // Parse status broadcast using ClickSync standard parser
            Rapoo::DeviceStatus devStatus;
            if (Rapoo::ParseStatusReport(buf, bytesRead, devStatus, g_cachedBattery)) {
                // NOTE: wired USB direct connect does NOT imply charging.
                // Trust the protocol-level charging flag for all modes.

                DWORD mask = 0;
                State copySt;

                EnterCriticalSection(&g_csState);
                if (!g_currentState.isConnected) {
                    g_currentState.isConnected = true;
                    mask |= CHANGE_CONNECTED;
                }

                // Effective connection mode from endpoint PID or packet device marker
                bool effWired = isWired || devStatus.isWired;
                if (effWired != g_currentState.isWired) {
                    g_currentState.isWired = effWired;
                    mask |= CHANGE_CONNECTED;
                }

                // On the 54L15 generation the passive status packet can keep
                // reporting a stale 800 DPI value. Once the real register table
                // has been read, it is authoritative and broadcasts are ignored.
                if (!g_dpiConfigValid &&
                    (devStatus.dpiLevel != g_currentState.dpiLevel || devStatus.dpiX != g_currentState.dpiX)) {
                    g_currentState.dpiLevel = devStatus.dpiLevel;
                    g_currentState.dpiX = devStatus.dpiX;
                    g_currentState.dpiY = devStatus.dpiY;
                    mask |= CHANGE_DPI;
                }

                bool effCharging = devStatus.isCharging || effWired;
                if (devStatus.battery != g_currentState.battery || effCharging != g_currentState.isCharging) {
                    g_currentState.battery = devStatus.battery;
                    g_currentState.isCharging = effCharging;
                    g_cachedBattery = devStatus.battery;
                    SaveRegistryDword(L"LastBattery", (DWORD)devStatus.battery);
                    mask |= CHANGE_BATTERY;
                }
                copySt = g_currentState;
                LeaveCriticalSection(&g_csState);

                if (mask != 0 && g_callback) {
                    g_callback(copySt, mask);
                }
            }
        }

        EnterCriticalSection(&g_csState);
        bool wasConn = g_currentState.isConnected;
        g_currentState.isConnected = false;
        g_currentState.isCharging = false;
        g_currentState.perfModeKnown = false;
        g_currentState.rfAvailable = false;
        g_dpiConfigValid = false;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);

        CloseHandle(hReadEvent);
        CloseHandle(hStatus);

        EnterCriticalSection(&g_csDevIO);
        if (g_hControlDev != INVALID_HANDLE_VALUE) {
            CloseHandle(g_hControlDev);
            g_hControlDev = INVALID_HANDLE_VALUE;
        }
        if (g_hFeatureDev != INVALID_HANDLE_VALUE) {
            CloseHandle(g_hFeatureDev);
            g_hFeatureDev = INVALID_HANDLE_VALUE;
        }
        g_controlUnlocked = false;
        g_pollingKnown.store(false, std::memory_order_release);
        LeaveCriticalSection(&g_csDevIO);

        Sleep(300);
    }

    return 0;
}

bool Start(HWND hNotifyWnd, StateCallback callback) {
    g_hNotifyWnd = hNotifyWnd;
    g_callback = callback;

    InitializeCriticalSection(&g_csDevIO);
    InitializeCriticalSection(&g_csState);

    DWORD savedBat = LoadRegistryDword(L"LastBattery", 100);
    if (savedBat >= 2 && savedBat <= 100) {
        g_cachedBattery = (int)savedBat;
    } else {
        g_cachedBattery = 100;
    }
    EnterCriticalSection(&g_csState);
    g_currentState.battery = g_cachedBattery;
    LeaveCriticalSection(&g_csState);

    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hDevChangeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

    g_hWorkerThread = CreateThread(NULL, 0, HidWorkerThread, NULL, CREATE_SUSPENDED, NULL);
    g_started.store(g_hWorkerThread != NULL, std::memory_order_release);
    if (g_hWorkerThread) ResumeThread(g_hWorkerThread);
    return g_started.load(std::memory_order_acquire);
}

void Stop() {
    g_started.store(false, std::memory_order_release);
    if (g_hStopEvent) SetEvent(g_hStopEvent);
    if (g_hWorkerThread) {
        WaitForSingleObject(g_hWorkerThread, 2000);
        CloseHandle(g_hWorkerThread);
        g_hWorkerThread = NULL;
    }
    if (g_hStopEvent) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
    }
    if (g_hDevChangeEvent) {
        CloseHandle(g_hDevChangeEvent);
        g_hDevChangeEvent = NULL;
    }
    DeleteCriticalSection(&g_csDevIO);
    DeleteCriticalSection(&g_csState);
}

void NotifyDeviceChange() {
    if (g_hDevChangeEvent) {
        SetEvent(g_hDevChangeEvent);
    }
}

bool SetPollingRate(int hz) {
    if (!g_started.load(std::memory_order_acquire)) return false;
    if (g_pollingKnown.load(std::memory_order_acquire)) {
        const State current = GetCurrentState();
        if (current.isConnected && current.pollingHz == hz) return true;
    }

    BYTE code = Rapoo::PollingHzToCode(hz);
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_POLLING_HZ, &code, 1)) return false;

    g_pollingKnown.store(true, std::memory_order_release);
    g_perfKnown.store(false, std::memory_order_release);
    EnterCriticalSection(&g_csState);
    g_currentState.pollingHz = hz;
    g_currentState.perfModeKnown = false;
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) g_callback(copySt, CHANGE_POLLING);
    return true;
}

bool SetPerformanceMode(int mode) {
    if (!g_started.load(std::memory_order_acquire)) return false;
    if (!g_pollingKnown.load(std::memory_order_acquire)) RefreshPollingRate();

    State current = GetCurrentState();
    if (!Rapoo::IsPerfModeAllowed(mode, current.pollingHz)) return false;
    if (g_perfKnown.load(std::memory_order_acquire) &&
        current.perfModeKnown && current.perfMode == mode) {
        return true;
    }

    const BYTE address = Rapoo::PerfModeAddress(current.pollingHz);
    const int rateIndex = std::clamp((int)(address - Rapoo::ADDR_PERF_MODE_BASE), 0, 6);
    BYTE region[16] = {0};
    bool cached = false;
    EnterCriticalSection(&g_csState);
    if (g_perfRegionValid) {
        std::memcpy(region, g_perfRegion, sizeof(region));
        cached = true;
    }
    LeaveCriticalSection(&g_csState);

    if (!cached && !ReadPerfModeRegion(region)) return false;

    BYTE word[4] = {region[rateIndex], region[rateIndex + 1],
                    region[rateIndex + 2], region[rateIndex + 3]};
    word[0] = (BYTE)mode;
    bool sent = false;
    for (int attempt = 0; attempt < 3 && !sent; ++attempt) {
        sent = SendRawCommand(Rapoo::BANK_SYSTEM, address, word, 4);
        if (!sent) Sleep(60);
    }
    if (!sent) return false;

    // The endpoint accepting the 4-byte block is the success condition. Keep
    // the region cache coherent so adjacent rate switches preserve all slots.
    g_perfKnown.store(true, std::memory_order_release);
    EnterCriticalSection(&g_csState);
    region[rateIndex] = (BYTE)mode;
    std::memcpy(g_perfRegion, region, sizeof(region));
    g_perfRegionValid = true;
    g_currentState.perfMode = mode;
    g_currentState.perfModeKnown = true;
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) g_callback(copySt, CHANGE_PERF);
    return true;
}

bool SetSleepTimeout(int minutes) {
    if (!g_started.load(std::memory_order_acquire)) return false;
    minutes = Rapoo::ClampSleepMinutes(minutes);
    const State current = GetCurrentState();
    if (current.isConnected && current.sleepMinutes == minutes) return true;

    BYTE code = (BYTE)minutes;
    if (SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_SLEEP_TIMEOUT, &code, 1)) {
        EnterCriticalSection(&g_csState);
        g_currentState.sleepMinutes = minutes;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        SaveRegistryDword(L"SleepTimeout", (DWORD)minutes);
        if (g_callback) g_callback(copySt, CHANGE_SLEEP);
        return true;
    }
    return false;
}

bool RefreshPollingRate() {
    int curHz = 1000;
    if (PingDevice(curHz)) {
        g_pollingKnown.store(true, std::memory_order_release);
        EnterCriticalSection(&g_csState);
        bool changed = (g_currentState.pollingHz != curHz);
        if (changed) {
            g_currentState.pollingHz = curHz;
            g_currentState.perfModeKnown = false;
        }
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (changed) {
            g_perfKnown.store(false, std::memory_order_release);
            if (g_callback) g_callback(copySt, CHANGE_POLLING);
        }
        return true;
    }
    return false;
}

bool RefreshPerformanceMode() {
    const State current = GetCurrentState();
    const int rateIndex = std::clamp(
        (int)(Rapoo::PerfModeAddress(current.pollingHz) - Rapoo::ADDR_PERF_MODE_BASE), 0, 6);
    BYTE region[16] = {0};
    if (!ReadPerfModeRegion(region)) return false;
    const BYTE raw = region[rateIndex];
    if (!Rapoo::IsValidPerfMode(raw)) return false;

    g_perfKnown.store(true, std::memory_order_release);
    EnterCriticalSection(&g_csState);
    std::memcpy(g_perfRegion, region, sizeof(region));
    g_perfRegionValid = true;
    bool changed = (!g_currentState.perfModeKnown || g_currentState.perfMode != raw);
    g_currentState.perfMode = raw;
    g_currentState.perfModeKnown = true;
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (changed && g_callback) g_callback(copySt, CHANGE_PERF);
    return true;
}

bool RefreshRfState() {
    BYTE raw = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_RF_STRENGTHEN, raw)) {
        // A failed read is not evidence that RF control disappeared. Keeping the
        // last known value avoids a false "device disconnected" state during the
        // short endpoint reset caused by some wireless-strategy changes.
        return false;
    }

    bool full = (raw & 0x01) != 0;
    EnterCriticalSection(&g_csState);
    bool changed = (!g_currentState.rfAvailable || g_currentState.rfFull != full);
    g_currentState.rfAvailable = true;
    g_currentState.rfFull = full;
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (changed && g_callback) g_callback(copySt, CHANGE_RF);
    return true;
}

bool SetRfFull(bool full) {
    // The wireless strategy is a one-byte enum on the supported generation
    // (0 = smart, 1 = full). Writing the complete value is safer than a
    // read-modify-write transaction because switching can reset the endpoint.
    BYTE next = full ? 0x01 : 0x00;
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_RF_STRENGTHEN, &next, 1)) return false;

    EnterCriticalSection(&g_csState);
    g_currentState.rfAvailable = true;
    g_currentState.rfFull = full;
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) g_callback(copySt, CHANGE_RF);
    return true;
}

static bool DecodeDpiTable(const BYTE* raw, int count, std::vector<int>& out) {
    if (!raw || count <= 0) return false;
    out.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        out[(size_t)i] = (int)raw[i * 2] | ((int)raw[i * 2 + 1] << 8);
    }
    return true;
}

bool ReadDpiConfig(DpiConfig& outConfig) {
    BYTE countRaw = 0;
    BYTE indexRaw = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_SLOT_COUNT, countRaw)) return false;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, indexRaw)) return false;

    // 0x96 stores count - 1. Firmware tolerates values above the supported
    // maximum, so clamp defensively for unknown or partially configured devices.
    int count = std::clamp((int)countRaw + 1, 1, MAX_DPI_SLOTS);
    int index = std::clamp((int)indexRaw, 0, count - 1);

    BYTE xRaw[MAX_DPI_SLOTS * 2] = {0};
    BYTE yRaw[MAX_DPI_SLOTS * 2] = {0};
    if (!ReadRegisterBytes(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_X, (BYTE)(count * 2), xRaw)) return false;
    if (!ReadRegisterBytes(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_Y, (BYTE)(count * 2), yRaw)) return false;

    DpiConfig config;
    config.currentIndex = index;
    if (!DecodeDpiTable(xRaw, count, config.dpiX) ||
        !DecodeDpiTable(yRaw, count, config.dpiY)) {
        return false;
    }
    outConfig = config;

    EnterCriticalSection(&g_csState);
    g_dpiConfigValid = true;
    g_cachedDpiConfig = config;
    bool changed = g_currentState.dpiLevel != (index + 1) ||
                   g_currentState.dpiX != config.dpiX[(size_t)index] ||
                   g_currentState.dpiY != config.dpiY[(size_t)index];
    g_currentState.dpiLevel = index + 1;
    g_currentState.dpiX = config.dpiX[(size_t)index];
    g_currentState.dpiY = config.dpiY[(size_t)index];
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (changed && g_callback) g_callback(copySt, CHANGE_DPI);
    return true;
}

bool WriteDpiConfig(const DpiConfig& config) {
    int count = (int)config.dpiX.size();
    if (count < 1 || count > MAX_DPI_SLOTS || (int)config.dpiY.size() != count) return false;
    int index = std::clamp(config.currentIndex, 0, count - 1);

    BYTE xBytes[MAX_DPI_SLOTS * 2] = {0};
    BYTE yBytes[MAX_DPI_SLOTS * 2] = {0};
    for (int i = 0; i < count; ++i) {
        int x = std::clamp(config.dpiX[(size_t)i], 1, 0xFFFF);
        int y = std::clamp(config.dpiY[(size_t)i], 1, 0xFFFF);
        xBytes[i * 2] = (BYTE)(x & 0xFF);
        xBytes[i * 2 + 1] = (BYTE)((x >> 8) & 0xFF);
        yBytes[i * 2] = (BYTE)(y & 0xFF);
        yBytes[i * 2 + 1] = (BYTE)((y >> 8) & 0xFF);
    }

    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_X,
                        xBytes, (BYTE)(count * 2))) return false;
    Sleep(15);
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_Y,
                        yBytes, (BYTE)(count * 2))) return false;
    Sleep(15);

    BYTE countCode = (BYTE)(count - 1);
    BYTE indexCode = (BYTE)index;
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_SLOT_COUNT, &countCode, 1)) return false;
    Sleep(15);
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, &indexCode, 1)) return false;
    Sleep(30);

    DpiConfig verified;
    if (ReadDpiConfig(verified)) {
        if ((int)verified.dpiX.size() != count || (int)verified.dpiY.size() != count ||
            verified.currentIndex != index) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            int expectedX = std::clamp(config.dpiX[(size_t)i], 1, 0xFFFF);
            int expectedY = std::clamp(config.dpiY[(size_t)i], 1, 0xFFFF);
            if (verified.dpiX[(size_t)i] != expectedX || verified.dpiY[(size_t)i] != expectedY) {
                return false;
            }
        }
        return true;
    }

    // The writes were accepted by the endpoint but the optional verification read
    // timed out (common during endpoint settling). Keep the device usable and
    // update the local state optimistically instead of reporting a false failure.
    EnterCriticalSection(&g_csState);
    g_dpiConfigValid = true;
    g_cachedDpiConfig = config;
    g_currentState.dpiLevel = index + 1;
    g_currentState.dpiX = std::clamp(config.dpiX[(size_t)index], 1, 0xFFFF);
    g_currentState.dpiY = std::clamp(config.dpiY[(size_t)index], 1, 0xFFFF);
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) g_callback(copySt, CHANGE_DPI);
    return true;
}

bool SetCurrentDpi(int x, int y) {
    if (!g_started.load(std::memory_order_acquire)) return false;
    if (IsDpiConfigValid()) {
        const State current = GetCurrentState();
        if (current.isConnected && current.dpiX == x && current.dpiY == y) return true;
    }

    DpiConfig config;
    EnterCriticalSection(&g_csState);
    if (g_dpiConfigValid) config = g_cachedDpiConfig;
    LeaveCriticalSection(&g_csState);
    if ((config.dpiX.empty() || config.dpiY.empty()) &&
        (!ReadDpiConfig(config) || config.dpiX.empty() || config.dpiY.empty())) {
        return false;
    }

    x = std::clamp(x, 1, 0xFFFF);
    y = std::clamp(y, 1, 0xFFFF);
    int current = std::clamp(config.currentIndex, 0, (int)config.dpiX.size() - 1);
    if (config.dpiX[(size_t)current] == x && config.dpiY[(size_t)current] == y) return true;

    // Reuse an existing gear when possible so profile switching does not
    // silently rewrite the user's configured DPI slot table.
    for (int i = 0; i < (int)config.dpiX.size() && i < (int)config.dpiY.size(); ++i) {
        if (config.dpiX[(size_t)i] == x && config.dpiY[(size_t)i] == y) {
            return SetDpiIndex(i);
        }
    }

    config.dpiX[(size_t)current] = x;
    config.dpiY[(size_t)current] = y;
    return WriteDpiConfig(config);
}

bool SetDpiIndex(int index) {
    if (index < 0 || index >= MAX_DPI_SLOTS) return false;
    const State before = GetCurrentState();
    if (before.isConnected && before.dpiLevel == index + 1) return true;

    BYTE indexCode = (BYTE)index;
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, &indexCode, 1)) return false;
    Sleep(20);

    BYTE readBack = 0;
    const bool verified = ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, readBack);
    if (verified && readBack != indexCode) return false;

    EnterCriticalSection(&g_csState);
    g_dpiConfigValid = true;
    if (g_cachedDpiConfig.currentIndex >= 0 &&
        g_cachedDpiConfig.currentIndex < (int)g_cachedDpiConfig.dpiX.size()) {
        g_cachedDpiConfig.currentIndex = index;
    }
    g_currentState.dpiLevel = index + 1;
    if (g_cachedDpiConfig.currentIndex == index &&
        index < (int)g_cachedDpiConfig.dpiX.size() &&
        index < (int)g_cachedDpiConfig.dpiY.size()) {
        g_currentState.dpiX = g_cachedDpiConfig.dpiX[(size_t)index];
        g_currentState.dpiY = g_cachedDpiConfig.dpiY[(size_t)index];
    }
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) g_callback(copySt, CHANGE_DPI);
    return true;
}

bool IsDpiConfigValid() {
    if (!g_started.load(std::memory_order_acquire)) return false;
    EnterCriticalSection(&g_csState);
    bool valid = g_dpiConfigValid;
    LeaveCriticalSection(&g_csState);
    return valid;
}

bool IsStarted() {
    return g_started.load(std::memory_order_acquire);
}

void ForceRefresh() {
    // 1. Actively query hardware polling rate and RF strategy
    RefreshPollingRate();
    RefreshRfState();

    // 2. Send active wake command to trigger hardware status report
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev != INVALID_HANDLE_VALUE) {
        BYTE unlockBuf[33] = {0};
        DWORD uSize = Rapoo::BuildUnlockPacket(unlockBuf, sizeof(unlockBuf));
        DWORD written = 0;
        OVERLAPPED uOv = {0};
        uOv.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        WriteFile(g_hControlDev, unlockBuf, uSize, &written, &uOv);
        WaitForSingleObject(uOv.hEvent, 100);
        CloseHandle(uOv.hEvent);
    }
    LeaveCriticalSection(&g_csDevIO);

    // 3. Callback with current state
    EnterCriticalSection(&g_csState);
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) {
        g_callback(copySt, CHANGE_BATTERY | CHANGE_POLLING);
    }
}

State GetCurrentState() {
    if (!g_started.load(std::memory_order_acquire)) return State();
    EnterCriticalSection(&g_csState);
    State st = g_currentState;
    LeaveCriticalSection(&g_csState);
    return st;
}

} // namespace Device

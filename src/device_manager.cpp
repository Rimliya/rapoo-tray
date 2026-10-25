#include "device_manager.h"
extern "C" {
#include <hidsdi.h>
}
#include <setupapi.h>
#include <strsafe.h>
#include <algorithm>

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

static int g_cachedBattery = 100;
static bool g_started = false;
static DWORD g_lastFeatureError = 0;
static DWORD g_lastFeatureBytes = 0;
static BYTE g_lastFeatureData[33] = {0};
static bool g_useNrf54lProtocol = false;
static BYTE g_commandPrefix = 0xA5;

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

static bool WriteControlPacketLocked(const BYTE* packet, DWORD size, DWORD timeoutMs = 800) {
    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL ok = WriteFile(g_hControlDev, packet, size, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, timeoutMs) == WAIT_OBJECT_0) {
            GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
            ok = (written == size);
        } else {
            CancelIo(g_hControlDev);
            ok = FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    return ok && written == size;
}

static bool ReadControlReportLocked(BYTE* outBuf, DWORD outSize, DWORD timeoutMs, DWORD& bytesRead) {
    bytesRead = 0;
    if (!outBuf || outSize == 0) return false;
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline) {
        DWORD remaining = (DWORD)(deadline - GetTickCount64());
        DWORD waitMs = std::min<DWORD>(remaining, 250);
        ZeroMemory(outBuf, outSize);
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        DWORD read = 0;
        BOOL ok = ReadFile(g_hControlDev, outBuf, outSize, &read, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, waitMs);
            if (wait == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(g_hControlDev, &ov, &read, FALSE);
            } else {
                CancelIo(g_hControlDev);
                CloseHandle(ov.hEvent);
                continue;
            }
        }
        CloseHandle(ov.hEvent);
        if (ok && read >= 2) {
            if (outBuf[0] == Rapoo::REPORT_ID_CONTROL ||
                outBuf[0] == Rapoo::RESP_ACK ||
                outBuf[0] == 0x02 || outBuf[0] == 0x03) {
                bytesRead = read;
                return true;
            }
        }
    }
    return false;
}

static bool ReadNrf54lRegisterLocked(BYTE bank, BYTE addr, BYTE len, BYTE* outData) {
    BYTE command[33] = {0};
    command[0] = Rapoo::REPORT_ID_CONTROL;
    command[1] = g_commandPrefix;
    command[2] = 0xA4;
    command[3] = len;
    command[4] = addr;
    command[5] = bank;
    if (!WriteControlPacketLocked(command, sizeof(command))) return false;
    Sleep(20);

    BYTE response[65] = {0};
    DWORD bytesRead = 0;
    if (!ReadControlReportLocked(response, sizeof(response), 1200, bytesRead)) return false;
    g_lastFeatureError = 0;
    g_lastFeatureBytes = std::min<DWORD>(sizeof(g_lastFeatureData), bytesRead);
    memcpy(g_lastFeatureData, response, g_lastFeatureBytes);

    DWORD dataOffset = 0;
    if (bytesRead >= 5 && response[0] == Rapoo::REPORT_ID_CONTROL &&
        (response[1] == 0x01 || response[1] == 0x02 || response[1] == 0x03)) {
        dataOffset = 5;
    } else if (bytesRead >= 4 && (response[0] == 0x01 || response[0] == 0x02 || response[0] == 0x03)) {
        dataOffset = 4;
    } else {
        return false;
    }
    if (bytesRead < dataOffset + len) return false;
    memcpy(outData, response + dataOffset, len);
    return true;
}

static bool SendRawCommand(BYTE bank, BYTE addr, const BYTE* pData, BYTE len) {
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    if (g_useNrf54lProtocol) {
        if (!pData || len == 0 || len > 24) {
            LeaveCriticalSection(&g_csDevIO);
            return false;
        }
        BYTE command[33] = {0};
        command[0] = Rapoo::REPORT_ID_CONTROL;
        command[1] = g_commandPrefix;
        command[2] = 0xA5;
        command[3] = len;
        command[4] = addr;
        command[5] = bank;
        memcpy(&command[8], pData, len);
        bool wrote = WriteControlPacketLocked(command, sizeof(command));
        BYTE response[65] = {0};
        DWORD bytesRead = 0;
        bool acked = wrote && ReadControlReportLocked(response, sizeof(response), 1200, bytesRead);
        if (acked) {
            bool okAck = (bytesRead >= 2 && response[0] == Rapoo::REPORT_ID_CONTROL &&
                          (response[1] == 0x01 || response[1] == 0x02 || response[1] == 0x03)) ||
                         (bytesRead >= 1 && (response[0] == 0x01 || response[0] == 0x02 || response[0] == 0x03));
            if (!okAck) acked = false;
        }
        LeaveCriticalSection(&g_csDevIO);
        return acked;
    }

    BYTE buf[33] = {0};
    DWORD pktSize = Rapoo::BuildWriteCommand(bank, addr, pData, len, buf, sizeof(buf));
    bool ok = WriteControlPacketLocked(buf, pktSize);
    LeaveCriticalSection(&g_csDevIO);
    return ok;
}


static bool ReadRegisterBytes(BYTE bank, BYTE addr, BYTE len, BYTE* outData) {
    if (!outData || len == 0 || len > 31) return false;
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }
    if (g_useNrf54lProtocol) {
        bool ok = ReadNrf54lRegisterLocked(bank, addr, len, outData);
        LeaveCriticalSection(&g_csDevIO);
        return ok;
    }
    if (g_hFeatureDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    BYTE outBuf[33] = {0};
    DWORD pktSize = Rapoo::BuildReadCommand(bank, addr, len, outBuf, sizeof(outBuf));
    bool wOk = WriteControlPacketLocked(outBuf, pktSize);
    if (!wOk) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    Sleep(20);
    BYTE featBuf[33] = {0};
    featBuf[0] = Rapoo::REPORT_ID_FEATURE;
    BOOL fOk = HidD_GetFeature(g_hFeatureDev, featBuf, sizeof(featBuf));
    g_lastFeatureError = fOk ? 0 : GetLastError();
    g_lastFeatureBytes = sizeof(featBuf);
    memcpy(g_lastFeatureData, featBuf, sizeof(featBuf));
    bool ok = fOk && Rapoo::ParseFeatureRead(featBuf, sizeof(featBuf), len, outData);
    LeaveCriticalSection(&g_csDevIO);
    return ok;
}

static bool ReadRegisterByte(BYTE bank, BYTE addr, BYTE& outValue) {
    return ReadRegisterBytes(bank, addr, 1, &outValue);
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

static bool FindRapooEndpoints(WCHAR* pathStatus, WCHAR* pathControl, WCHAR* pathFeature, WCHAR* outModel, DWORD maxModelLen, bool* outIsWired, bool* outUseNrf54l) {
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
        if (outUseNrf54l) {
            *outUseNrf54l = wcsstr(targetPid, L"1462") != nullptr || wcsstr(targetPid, L"4662") != nullptr;
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
    bool useNrf54l = false;

    // Load user's saved sleep timeout from registry (default 10 min)
    EnterCriticalSection(&g_csState);
    g_currentState.sleepMinutes = (int)LoadRegistryDword(L"SleepTimeout", 10);
    g_currentState.sleepMinutes = Rapoo::ClampSleepMinutes(g_currentState.sleepMinutes);
    LeaveCriticalSection(&g_csState);

    while (WaitForSingleObject(g_hStopEvent, 200) == WAIT_TIMEOUT) {
        if (!FindRapooEndpoints(pathStatus, pathControl, pathFeature, modelBuf, 64, &isWired, &useNrf54l)) {
            EnterCriticalSection(&g_csState);
            bool wasConn = g_currentState.isConnected;
            g_currentState.isConnected = false;
            g_currentState.isCharging = false;
            g_currentState.rfAvailable = false;
            State copySt = g_currentState;
            LeaveCriticalSection(&g_csState);
            if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);
            Sleep(1000);
            continue;
        }

        g_useNrf54lProtocol = useNrf54l;
        g_commandPrefix = isWired ? 0xFF : 0xA5;

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
            g_currentState.rfAvailable = false;
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
        LeaveCriticalSection(&g_csDevIO);

        EnterCriticalSection(&g_csState);
        StringCchCopyW(g_currentState.modelName, ARRAYSIZE(g_currentState.modelName), modelBuf);
        g_currentState.isWired = isWired;
        if (isWired) {
            g_currentState.isCharging = true;
        }
        g_currentState.isConnected = true;
        State connSt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (g_callback) g_callback(connSt, CHANGE_CONNECTED | (isWired ? CHANGE_BATTERY : 0));

        // ClickSync A5 A3 unlock handshake
        if (g_hControlDev != INVALID_HANDLE_VALUE) {
            BYTE unlockBuf[33] = {0};
            DWORD uSize = Rapoo::BuildUnlockPacket(unlockBuf, sizeof(unlockBuf));
            DWORD written = 0;
            OVERLAPPED uOv = {0};
            uOv.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            WriteFile(g_hControlDev, unlockBuf, uSize, &written, &uOv);
            WaitForSingleObject(uOv.hEvent, 200);
            CloseHandle(uOv.hEvent);
        }

        // Query current hardware polling rate
        int curHz = 1000;
        if (PingDevice(curHz)) {
            EnterCriticalSection(&g_csState);
            g_currentState.pollingHz = curHz;
            State pollSt = g_currentState;
            LeaveCriticalSection(&g_csState);
            if (g_callback) g_callback(pollSt, CHANGE_POLLING);
        }

        RefreshRfState();
        DpiConfig connectedDpi;
        ReadDpiConfig(connectedDpi);

        // Push saved sleep timeout to device hardware
        int curSleepMin = 10;
        EnterCriticalSection(&g_csState);
        curSleepMin = g_currentState.sleepMinutes;
        LeaveCriticalSection(&g_csState);
        BYTE sleepCode = (BYTE)curSleepMin;
        SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_SLEEP_TIMEOUT, &sleepCode, 1);

        HANDLE hReadEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        OVERLAPPED ov = {0};
        ov.hEvent = hReadEvent;

        BYTE buf[65] = {0};
        DWORD bytesRead = 0;
        bool gotFirstPacket = false; // no ping-alive judgement until mode is known
        int pingFailureCount = 0;

        while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
            ResetEvent(hReadEvent);
            BOOL ok = ReadFile(hStatus, buf, 19, &bytesRead, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waitHandles[3] = { g_hStopEvent, hReadEvent, g_hDevChangeEvent };
                    DWORD waitMs = 3000;
                    DWORD waitRes = WaitForMultipleObjects(3, waitHandles, FALSE, waitMs);

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
                    } else if (waitRes == WAIT_TIMEOUT) {
                        CancelIo(hStatus);
                        GetOverlappedResult(hStatus, &ov, &bytesRead, FALSE);

                        // Ping-alive probe is only meaningful in dongle mode,
                        // and only after the packet marker told us the mode.
                        // (Wired devices may not answer the feature ping, and
                        // before the first packet isWired is still the default.)
                        if (gotFirstPacket) {
                            bool wiredNow;
                            EnterCriticalSection(&g_csState);
                            wiredNow = g_currentState.isWired;
                            LeaveCriticalSection(&g_csState);
                            if (!wiredNow) {
                                int hz = 0;
                                bool alive = PingDevice(hz);
                                if (!alive) {
                                    // RF mode changes can briefly make the control
                                    // endpoint unresponsive. Require three failed
                                    // probes before showing the device as offline.
                                    ++pingFailureCount;
                                    if (pingFailureCount >= 3) {
                                        EnterCriticalSection(&g_csState);
                                        bool wasConn = g_currentState.isConnected;
                                        g_currentState.isConnected = false;
                                        g_currentState.isCharging = false;
                                        g_currentState.rfAvailable = false;
                                        State copySt = g_currentState;
                                        LeaveCriticalSection(&g_csState);
                                        if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);
                                    }
                                } else {
                                    pingFailureCount = 0;
                                    EnterCriticalSection(&g_csState);
                                    bool wasConn = g_currentState.isConnected;
                                    g_currentState.isConnected = true;
                                    State copySt = g_currentState;
                                    LeaveCriticalSection(&g_csState);
                                    if (!wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED);
                                }
                            }
                        }
                        continue;
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
                gotFirstPacket = true;
                pingFailureCount = 0;
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

                if (devStatus.dpiLevel != g_currentState.dpiLevel || devStatus.dpiX != g_currentState.dpiX) {
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
        g_currentState.rfAvailable = false;
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

    g_hWorkerThread = CreateThread(NULL, 0, HidWorkerThread, NULL, 0, NULL);
    g_started = (g_hWorkerThread != NULL);
    return g_started;
}

void Stop() {
    g_started = false;
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
    BYTE code = Rapoo::PollingHzToCode(hz);
    if (SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_POLLING_HZ, &code, 1)) {
        EnterCriticalSection(&g_csState);
        g_currentState.pollingHz = hz;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (g_callback) g_callback(copySt, CHANGE_POLLING);
        return true;
    }
    return false;
}

bool SetSleepTimeout(int minutes) {
    minutes = Rapoo::ClampSleepMinutes(minutes);
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
        EnterCriticalSection(&g_csState);
        bool changed = (g_currentState.pollingHz != curHz);
        if (changed) g_currentState.pollingHz = curHz;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (changed && g_callback) g_callback(copySt, CHANGE_POLLING);
        return true;
    }
    return false;
}

bool RefreshRfState() {
    BYTE raw = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_RF_STRENGTHEN, raw)) {
        EnterCriticalSection(&g_csState);
        g_currentState.rfAvailable = false;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (g_callback) g_callback(copySt, CHANGE_RF);
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
    BYTE current = 0;
    bool readCurrent = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_RF_STRENGTHEN, current)) {
            readCurrent = true;
            break;
        }
        Sleep(60);
    }
    if (!readCurrent) return false;

    BYTE next = full ? (BYTE)(current | 0x01) : (BYTE)(current & 0xFE);
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_RF_STRENGTHEN, &next, 1)) return false;

    // The RF switch can briefly interrupt the configuration endpoint. Accept
    // the acknowledged write immediately and verify best-effort in the
    // background of this call, so the settings UI does not falsely report that
    // the device disappeared.
    EnterCriticalSection(&g_csState);
    g_currentState.rfAvailable = true;
    g_currentState.rfFull = full;
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) g_callback(copySt, CHANGE_RF);

    for (int attempt = 0; attempt < 3; ++attempt) {
        Sleep(80);
        BYTE verify = 0;
        if (ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_RF_STRENGTHEN, verify)) {
            bool actual = (verify & 0x01) != 0;
            if (actual == full) return true;
            if (attempt == 2) {
                EnterCriticalSection(&g_csState);
                g_currentState.rfAvailable = true;
                g_currentState.rfFull = actual;
                State failedSt = g_currentState;
                LeaveCriticalSection(&g_csState);
                if (g_callback) g_callback(failedSt, CHANGE_RF);
                return false;
            }
            if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_RF_STRENGTHEN, &next, 1)) return false;
        }
    }
    // Write was acknowledged; a transient read timeout is not a failed switch.
    return true;
}

static bool DecodeDpiTable(const BYTE* raw, int count, std::vector<int>& out) {
    if (!raw || count <= 0) return false;
    out.resize(count);
    for (int i = 0; i < count; ++i) {
        out[i] = (int)raw[i * 2] | ((int)raw[i * 2 + 1] << 8);
    }
    return true;
}

bool ReadDpiConfig(DpiConfig& outConfig) {
    BYTE countRaw = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_SLOT_COUNT, countRaw)) return false;
    int count = std::clamp((int)countRaw, 1, MAX_DPI_SLOTS);

    BYTE indexRaw = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, indexRaw)) return false;
    int index = std::clamp((int)indexRaw, 0, count - 1);

    BYTE xRaw[MAX_DPI_SLOTS * 2] = {0};
    BYTE yRaw[MAX_DPI_SLOTS * 2] = {0};
    if (!ReadRegisterBytes(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_X, (BYTE)(count * 2), xRaw)) return false;
    if (!ReadRegisterBytes(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_Y, (BYTE)(count * 2), yRaw)) return false;

    DpiConfig config;
    config.currentIndex = index;
    if (!DecodeDpiTable(xRaw, count, config.dpiX) || !DecodeDpiTable(yRaw, count, config.dpiY)) return false;
    outConfig = config;

    EnterCriticalSection(&g_csState);
    bool changed = g_currentState.dpiLevel != (index + 1) ||
                   g_currentState.dpiX != config.dpiX[index] ||
                   g_currentState.dpiY != config.dpiY[index];
    g_currentState.dpiLevel = index + 1;
    g_currentState.dpiX = config.dpiX[index];
    g_currentState.dpiY = config.dpiY[index];
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
        int x = std::clamp(config.dpiX[i], 1, 0xFFFF);
        int y = std::clamp(config.dpiY[i], 1, 0xFFFF);
        xBytes[i * 2] = (BYTE)(x & 0xFF);
        xBytes[i * 2 + 1] = (BYTE)((x >> 8) & 0xFF);
        yBytes[i * 2] = (BYTE)(y & 0xFF);
        yBytes[i * 2 + 1] = (BYTE)((y >> 8) & 0xFF);
    }

    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_X, xBytes, (BYTE)(count * 2))) return false;
    Sleep(20);
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_TABLE_Y, yBytes, (BYTE)(count * 2))) return false;
    BYTE countByte = (BYTE)count;
    BYTE indexByte = (BYTE)index;
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_SLOT_COUNT, &countByte, 1)) return false;
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, &indexByte, 1)) return false;
    Sleep(30);

    DpiConfig verify;
    if (!ReadDpiConfig(verify)) return false;
    if ((int)verify.dpiX.size() != count || (int)verify.dpiY.size() != count || verify.currentIndex != index) return false;
    for (int i = 0; i < count; ++i) {
        if (verify.dpiX[i] != config.dpiX[i] || verify.dpiY[i] != config.dpiY[i]) return false;
    }
    return true;
}

bool SetDpiIndex(int index) {
    BYTE countRaw = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_SLOT_COUNT, countRaw)) return false;
    int count = std::clamp((int)countRaw, 1, MAX_DPI_SLOTS);
    if (index < 0 || index >= count) return false;

    BYTE indexByte = (BYTE)index;
    if (!SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, &indexByte, 1)) return false;

    BYTE verify = 0;
    if (!ReadRegisterByte(Rapoo::BANK_SYSTEM, Rapoo::ADDR_DPI_INDEX, verify)) return false;
    if ((int)verify != index) return false;

    DpiConfig current;
    if (ReadDpiConfig(current)) return true;

    EnterCriticalSection(&g_csState);
    g_currentState.dpiLevel = index + 1;
    State copySt = g_currentState;
    LeaveCriticalSection(&g_csState);
    if (g_callback) g_callback(copySt, CHANGE_DPI);
    return true;
}

bool IsStarted() {
    return g_started;
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
    EnterCriticalSection(&g_csState);
    State st = g_currentState;
    LeaveCriticalSection(&g_csState);
    return st;
}

IoDebugInfo GetIoDebugInfo() {
    IoDebugInfo info;
    EnterCriticalSection(&g_csDevIO);
    info.controlOpen = g_hControlDev != INVALID_HANDLE_VALUE;
    info.featureOpen = g_hFeatureDev != INVALID_HANDLE_VALUE;
    info.featureError = g_lastFeatureError;
    info.featureBytes = g_lastFeatureBytes;
    memcpy(info.featureData, g_lastFeatureData, sizeof(info.featureData));
    LeaveCriticalSection(&g_csDevIO);
    return info;
}

} // namespace Device

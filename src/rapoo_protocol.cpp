#include "rapoo_protocol.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Rapoo {

DWORD BuildUnlockPacket(BYTE* outBuf, DWORD bufSize) {
    if (!outBuf || bufSize < 33) return 0;
    std::memset(outBuf, 0, bufSize);
    outBuf[0] = REPORT_ID_CONTROL; // 0x06
    outBuf[1] = 0xA5;
    outBuf[2] = 0xA3;
    outBuf[3] = 0x00;
    outBuf[4] = 0x00;
    outBuf[5] = 0x00;
    outBuf[6] = 0x00;
    outBuf[7] = 0x00;
    outBuf[8] = 0x00;
    return 33;
}

DWORD BuildWriteCommand(BYTE bank, BYTE addr, const BYTE* pData, BYTE dataLen, BYTE* outBuf, DWORD bufSize) {
    if (!outBuf || bufSize < 33) return 0;
    std::memset(outBuf, 0, bufSize);
    outBuf[0] = REPORT_ID_CONTROL; // 0x06
    outBuf[1] = 0xA5;
    outBuf[2] = 0xA5;
    outBuf[3] = (BYTE)(dataLen & 0xFF);
    outBuf[4] = addr;
    outBuf[5] = bank;
    outBuf[6] = 0x00;
    outBuf[7] = 0x00;
    if (pData && dataLen > 0) {
        BYTE copyLen = (dataLen > 25) ? 25 : dataLen;
        std::memcpy(&outBuf[8], pData, copyLen);
    }
    return 33;
}

DWORD BuildReadCommand(BYTE bank, BYTE addr, BYTE readLen, BYTE* outBuf, DWORD bufSize) {
    if (!outBuf || bufSize < 33) return 0;
    std::memset(outBuf, 0, bufSize);
    outBuf[0] = REPORT_ID_CONTROL; // 0x06
    outBuf[1] = 0xA5;
    outBuf[2] = 0xA4;
    outBuf[3] = readLen;
    outBuf[4] = addr;
    outBuf[5] = bank;
    outBuf[6] = 0x00;
    outBuf[7] = 0x00;
    outBuf[8] = 0x00;
    return 33;
}

bool ParseStatusReport(const BYTE* buf, DWORD bytesRead, DeviceStatus& outStatus, int cachedBattery) {
    if (!buf || bytesRead < 9) return false;
    if (buf[0] != REPORT_ID_STATUS) return false;
    // Device marker: 0x20 (2.4G dongle), 0x10 (wired USB direct).
    // High nibble contains device mode; lower nibble carries dynamic status bits.
    BYTE modeNibble = buf[1] & 0xF0;
    if (modeNibble != 0x20 && modeNibble != 0x10) return false;
    outStatus.isWired = (modeNibble == 0x10);

    outStatus.dpiLevel = (int)buf[2] + 1;
    outStatus.dpiX = (int)buf[3] | ((int)buf[4] << 8);
    outStatus.dpiY = (int)buf[5] | ((int)buf[6] << 8);

    BYTE statusByte = buf[7];
    BYTE rawBat = buf[8];
    BYTE extraByte = (bytesRead > 9) ? buf[9] : 0;

    // Charging flag detection based on Rapoo status flags
    bool charging = ((statusByte & 0x02) != 0 || statusByte == 0x02 || statusByte == 0x03 ||
                     extraByte == 0x01 || extraByte == 0x02);
    outStatus.isCharging = charging;

    // Battery percentage filtering:
    // In Rapoo hardware, rawBat is an integer 0..100.
    // When the mouse is first plugged into USB/charging, rawBat may momentarily report 0 or 1
    // (transient handshake / ADC settle time). We protect the battery gauge by retaining
    // the previous valid reading until a real measurement (>= 2) arrives.
    if (rawBat >= 2 && rawBat <= 100) {
        outStatus.battery = rawBat;
    } else if (rawBat <= 1) {
        if (cachedBattery >= 2 && cachedBattery <= 100) {
            outStatus.battery = cachedBattery;
        } else {
            outStatus.battery = (rawBat == 0) ? 100 : rawBat;
        }
    } else {
        outStatus.battery = 100;
    }

    return true;
}

bool ParseFeatureRead(const BYTE* buf, DWORD bytesRead, BYTE expectedLen, BYTE* outData) {
    if (!buf || bytesRead < 5) return false;

    // Feature reads use 0x01 for a successful reply. Other small status codes
    // can be emitted while the configuration endpoint is settling, but they do
    // not carry register data and must be retried rather than decoded as zero.
    auto isAck = [](BYTE value) { return value == 0x01; };

    // Case 1: Windows HID driver stripped Report ID -> buf[0] is ACK.
    if (isAck(buf[0]) && bytesRead >= (DWORD)(4 + expectedLen)) {
        if (outData) {
            std::memcpy(outData, &buf[4], expectedLen);
        }
        return true;
    }

    // Case 2: buf[0] is Report ID (0x08) and buf[1] is ACK.
    if (buf[0] == REPORT_ID_FEATURE && isAck(buf[1]) && bytesRead >= (DWORD)(5 + expectedLen)) {
        if (outData) {
            std::memcpy(outData, &buf[5], expectedLen);
        }
        return true;
    }

    return false;
}

namespace {

constexpr int POLLING_RATES[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
constexpr BYTE PERF_MODE_MASKS[] = { 0x03, 0x03, 0x07, 0x3E, 0x3C, 0x38, 0x38 };

int PollingRateIndex(int hz) {
    int best = 3;
    int distance = 0x7fffffff;
    for (int i = 0; i < (int)(sizeof(POLLING_RATES) / sizeof(POLLING_RATES[0])); ++i) {
        int candidate = std::abs(POLLING_RATES[i] - hz);
        if (candidate < distance) {
            best = i;
            distance = candidate;
        }
    }
    return best;
}

} // namespace

int CodeToPollingHz(BYTE code) {
    switch (code) {
        case 0x08: return 125;
        case 0x04: return 250;
        case 0x02: return 500;
        case 0x01: return 1000;
        case 0x84: return 2000;
        case 0x82: return 4000;
        case 0x81: return 8000;
        default:   return 1000;
    }
}

BYTE PollingHzToCode(int hz) {
    switch (hz) {
        case 125:  return 0x08;
        case 250:  return 0x04;
        case 500:  return 0x02;
        case 1000: return 0x01;
        case 2000: return 0x84;
        case 4000: return 0x82;
        case 8000: return 0x81;
        default:   return 0x01;
    }
}

bool IsValidPerfMode(int mode) {
    return mode >= PERF_OFFICE && mode <= PERF_FURY_GAMING;
}

bool IsPerfModeAllowed(int mode, int pollingHz) {
    if (!IsValidPerfMode(mode)) return false;
    const int index = PollingRateIndex(pollingHz);
    return (PERF_MODE_MASKS[index] & (BYTE)(1u << mode)) != 0;
}

int NearestPerfMode(int mode, int pollingHz) {
    const int index = PollingRateIndex(pollingHz);
    const BYTE mask = PERF_MODE_MASKS[index];
    const int desired = IsValidPerfMode(mode) ? mode : PERF_OFFICE;
    int best = PERF_OFFICE;
    int bestDistance = 0x7fffffff;
    for (int candidate = PERF_OFFICE; candidate <= PERF_FURY_GAMING; ++candidate) {
        if ((mask & (BYTE)(1u << candidate)) == 0) continue;
        int distance = std::abs(candidate - desired);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

BYTE PerfModeAddress(int pollingHz) {
    return (BYTE)(ADDR_PERF_MODE_BASE + PollingRateIndex(pollingHz));
}

const WCHAR* PerfModeName(int mode) {
    switch (mode) {
        case PERF_OFFICE:            return L"办公模式";
        case PERF_BALANCED:          return L"均衡模式";
        case PERF_FIRE:              return L"火力模式";
        case PERF_HYPER_CORE:        return L"超核模式";
        case PERF_GAMING_HYPER_CORE: return L"竞技超核模式";
        case PERF_FURY_GAMING:       return L"狂暴竞技模式";
        default:                     return L"未知模式";
    }
}

} // namespace Rapoo

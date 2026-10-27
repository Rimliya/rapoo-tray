#include "ui_bridge.h"

#include "battery_estimator.h"
#include "device_manager.h"
#include "dpi_window.h"
#include "osd_window.h"
#include "profile_manager.h"
#include "rapoo_protocol.h"
#include "tray_menu.h"

#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace UiBridge {
namespace {

constexpr const WCHAR* PIPE_NAME = L"\\\\.\\pipe\\rapoo-tray-ui-v1";

struct CommandContext {
    std::string command;
    std::string response;
};

HWND g_mainWindow = NULL;
HANDLE g_stopEvent = NULL;
std::thread g_pipeThread;
std::mutex g_statusMutex;
std::wstring g_statusText = L"选择配置进行编辑，或新增一个配置";
bool g_statusGood = true;
int g_createdProfileId = 0;

std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) return std::string();
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
    if (count <= 0) return std::string();
    std::string result((size_t)count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), count, NULL, NULL);
    return result;
}

std::wstring WideFromUtf8(const std::string& text) {
    if (text.empty()) return std::wstring();
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), NULL, 0);
    if (count <= 0) return std::wstring();
    std::wstring result((size_t)count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), count);
    return result;
}

std::string Base64Encode(const std::string& input) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    int value = 0;
    int bits = -6;
    for (unsigned char c : input) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            output.push_back(alphabet[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) output.push_back(alphabet[((value << 8) >> (bits + 8)) & 0x3F]);
    while (output.size() % 4) output.push_back('=');
    return output;
}

std::string Base64Decode(const std::string& input) {
    int table[256];
    std::fill(std::begin(table), std::end(table), -1);
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) table[(unsigned char)alphabet[i]] = i;

    std::string output;
    output.reserve(input.size() * 3 / 4);
    int value = 0;
    int bits = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        int decoded = table[c];
        if (decoded < 0) continue;
        value = (value << 6) + decoded;
        bits += 6;
        if (bits >= 0) {
            output.push_back((char)((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return output;
}

std::string Field(const std::wstring& text) {
    return Base64Encode(Utf8FromWide(text));
}

std::vector<std::string> Split(const std::string& text, char delimiter = '|') {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(delimiter, start);
        if (end == std::string::npos) end = text.size();
        result.push_back(text.substr(start, end - start));
        if (end == text.size()) break;
        start = end + 1;
    }
    return result;
}

std::string At(const std::vector<std::string>& fields, size_t index) {
    return index < fields.size() ? fields[index] : std::string();
}

int ToInt(const std::string& text, int fallback = 0) {
    try {
        size_t used = 0;
        int value = std::stoi(text, &used);
        return used == text.size() ? value : fallback;
    } catch (...) {
        return fallback;
    }
}

std::wstring DecodeField(const std::vector<std::string>& fields, size_t index) {
    return WideFromUtf8(Base64Decode(At(fields, index)));
}

std::wstring Trim(std::wstring value) {
    size_t first = 0;
    while (first < value.size() && iswspace(value[first])) ++first;
    size_t last = value.size();
    while (last > first && iswspace(value[last - 1])) --last;
    return value.substr(first, last - first);
}

void SetStatus(const std::wstring& text, bool good = true) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_statusText = text;
    g_statusGood = good;
}

std::wstring GetExecutableVersion() {
    WCHAR path[MAX_PATH] = {0};
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return L"未知";
    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size) return L"未知";
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return L"未知";
    VS_FIXEDFILEINFO* info = NULL;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", (void**)&info, &infoSize) || !info) return L"未知";
    WCHAR version[64] = {0};
    swprintf_s(version, L"%d.%d.%d",
        HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS));
    return version;
}

std::wstring FormatDpi(const Profiles::Definition& profile) {
    if (profile.dpiX <= 0 || profile.dpiY <= 0) return L"跟随当前";
    if (profile.dpiX == profile.dpiY) return std::to_wstring(profile.dpiX);
    return std::to_wstring(profile.dpiX) + L"×" + std::to_wstring(profile.dpiY);
}

void GetCurrentDpiDefaults(int& x, int& y) {
    const Device::State state = Device::GetCurrentState();
    BatteryEstimate::DpiSpec spec;
    BatteryEstimate::GetDpiSpecForState(state, spec);
    if (state.isConnected && Device::IsDpiConfigValid() && state.dpiX > 0) {
        x = std::clamp(state.dpiX, spec.minDpi, spec.maxDpi);
        y = std::clamp(state.dpiY > 0 ? state.dpiY : state.dpiX, spec.minDpi, spec.maxDpi);
    } else {
        x = std::clamp(800, spec.minDpi, spec.maxDpi);
        y = x;
    }
}

std::string BuildStateResponse() {
    const Device::State state = Device::GetCurrentState();
    const std::wstring name = state.modelName[0] ? std::wstring(state.modelName) : std::wstring(L"雷柏游戏鼠标");
    const std::wstring mode = !state.isConnected ? std::wstring(L"未连接")
        : state.isWired ? std::wstring(L"USB 有线") : std::wstring(L"2.4G 无线");

    const auto& models = BatteryEstimate::Models();
    const std::wstring selectedKey = BatteryEstimate::ResolveModelKey(state);
    int modelIndex = 0;
    for (size_t i = 0; i < models.size(); ++i) {
        if (_wcsicmp(models[i].key.c_str(), selectedKey.c_str()) == 0) modelIndex = (int)i;
    }

    std::wstring statusText;
    bool statusGood = true;
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        statusText = g_statusText;
        statusGood = g_statusGood;
    }

    std::ostringstream out;
    out << "OK\n";
    out << "STATUS|" << (statusGood ? 1 : 0) << "|" << Field(statusText) << "\n";
    out << "APP|" << Field(GetExecutableVersion())
        << "|" << Field(name)
        << "|" << Field(mode)
        << "|" << (state.isConnected ? 1 : 0)
        << "|" << state.battery
        << "|" << state.dpiX
        << "|" << state.dpiY
        << "|" << modelIndex
        << "|" << (Profiles::AutoSwitchEnabled() ? 1 : 0)
        << "|" << (Osd::IsEnabled() ? 1 : 0)
        << "|" << (Tray::IsAutoRunEnabled() ? 1 : 0)
        << "|" << (state.rfAvailable ? 1 : 0)
        << "|" << (state.rfFull ? 1 : 0)
        << "|" << Profiles::ActiveId() << "\n";

    const int activeId = Profiles::ActiveId();
    const int defaultId = Profiles::DefaultId();
    for (const auto& profile : Profiles::GetAll()) {
        std::wstring badge;
        if (profile.id == activeId) badge = L"当前";
        else if (profile.id == defaultId) badge = L"默认";
        out << "PROFILE|" << profile.id
            << "|" << Field(profile.name)
            << "|" << profile.pollingHz
            << "|" << profile.performanceMode
            << "|" << Field(Rapoo::PerfModeName(profile.performanceMode))
            << "|" << Field(FormatDpi(profile))
            << "|" << Field(profile.appName)
            << "|" << Field(badge)
            << "|" << (profile.id == defaultId ? 1 : 0)
            << "|" << profile.sleepMinutes
            << "|" << profile.dpiX
            << "|" << profile.dpiY
            << "|" << Field(profile.appPath) << "\n";
    }

    for (size_t i = 0; i < models.size(); ++i) {
        out << "MODEL|" << i << "|" << Field(models[i].key) << "|" << Field(models[i].label) << "\n";
    }
    if (g_createdProfileId > 0) out << "CREATED|" << g_createdProfileId << "\n";
    out << "END\n";
    return out.str();
}

std::string BuildDpiResponse() {
    const Device::State state = Device::GetCurrentState();
    BatteryEstimate::DpiSpec spec;
    std::wstring key;
    std::wstring label;
    BatteryEstimate::GetDpiSpecForState(state, spec, &key, &label);

    DpiWindow::QuickSlotList list;
    if (!DpiWindow::GetQuickSlots(list) || list.slots.empty()) {
        DpiWindow::QuickSlot slot;
        GetCurrentDpiDefaults(slot.x, slot.y);
        list.slots.push_back(slot);
        list.activeIndex = 0;
        list.fromDevice = false;
        list.modelKey = key;
        list.modelLabel = label;
    }

    int limit = std::min(std::max(1, spec.maxSlots), Device::MAX_DPI_SLOTS);
    if ((int)list.slots.size() > limit) list.slots.resize((size_t)limit);
    const int step = std::max(1, spec.step);
    auto snap = [&](int value) {
        value = std::clamp(value, spec.minDpi, spec.maxDpi);
        int offset = std::max(0, value - spec.minDpi);
        int snapped = spec.minDpi + ((offset + step / 2) / step) * step;
        return std::clamp(snapped, spec.minDpi, spec.maxDpi);
    };
    for (auto& slot : list.slots) {
        slot.x = snap(slot.x);
        slot.y = snap(slot.y);
    }
    list.activeIndex = std::clamp(list.activeIndex, 0, (int)list.slots.size() - 1);

    std::wstring statusText;
    bool statusGood = true;
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        statusText = g_statusText;
        statusGood = g_statusGood;
    }

    std::ostringstream out;
    out << "OK\n";
    out << "STATUS|" << (statusGood ? 1 : 0) << "|" << Field(statusText) << "\n";
    out << "DPI|" << list.activeIndex
        << "|" << list.activeIndex
        << "|" << spec.minDpi
        << "|" << spec.maxDpi
        << "|" << step
        << "|" << limit
        << "|" << (list.fromDevice ? 1 : 0)
        << "|" << Field(list.modelLabel.empty() ? label : list.modelLabel) << "\n";
    for (size_t i = 0; i < list.slots.size(); ++i) {
        out << "GEAR|" << i << "|" << list.slots[i].x << "|" << list.slots[i].y
            << "|" << ((int)i == list.activeIndex ? 1 : 0) << "\n";
    }
    out << "END\n";
    return out.str();
}

std::string HandleMainCommand(const std::string& command) {
    g_createdProfileId = 0;
    const auto fields = Split(command);
    const std::string verb = At(fields, 0);

    if (verb == "GET_STATE") return BuildStateResponse();

    if (verb == "ADD_PROFILE") {
        int dpiX = 800;
        int dpiY = 800;
        GetCurrentDpiDefaults(dpiX, dpiY);
        const Device::State state = Device::GetCurrentState();
        const int perfMode = state.isConnected && state.perfModeKnown ? state.perfMode : Rapoo::PERF_OFFICE;
        const int id = Profiles::Create(Profiles::MakeUniqueName(), 1000, 10, dpiX, dpiY, perfMode);
        if (id == 0) {
            SetStatus(L"无法新增配置，最多可创建 16 个配置", false);
        } else {
            g_createdProfileId = id;
            SetStatus(L"已新增配置，请修改名称和参数");
        }
        return BuildStateResponse();
    }

    if (verb == "DELETE_PROFILE") {
        const int id = ToInt(At(fields, 1));
        Profiles::Definition profile;
        if (id == 0 || !Profiles::Find(id, profile)) {
            SetStatus(L"未找到要删除的配置", false);
        } else if (!Profiles::Delete(id)) {
            SetStatus(L"至少需要保留一个配置", false);
        } else {
            SetStatus(L"配置已删除");
        }
        return BuildStateResponse();
    }

    if (verb == "SAVE_PROFILE") {
        const int id = ToInt(At(fields, 1));
        Profiles::Definition profile;
        if (id == 0 || !Profiles::Find(id, profile)) {
            SetStatus(L"请选择一个配置后再保存", false);
            return BuildStateResponse();
        }

        const std::wstring name = Trim(DecodeField(fields, 2));
        if (name.empty()) {
            SetStatus(L"配置名称不能为空", false);
            return BuildStateResponse();
        }

        const Device::State state = Device::GetCurrentState();
        BatteryEstimate::DpiSpec spec;
        BatteryEstimate::GetDpiSpecForState(state, spec);
        profile.name = name;
        profile.pollingHz = Profiles::NormalizePollingRate(ToInt(At(fields, 3), profile.pollingHz));
        profile.performanceMode = Rapoo::NearestPerfMode(ToInt(At(fields, 4), profile.performanceMode), profile.pollingHz);
        profile.sleepMinutes = Rapoo::ClampSleepMinutes(ToInt(At(fields, 5), profile.sleepMinutes));
        profile.dpiX = std::clamp(ToInt(At(fields, 6), profile.dpiX), spec.minDpi, spec.maxDpi);
        profile.dpiY = std::clamp(ToInt(At(fields, 7), profile.dpiY), spec.minDpi, spec.maxDpi);
        profile.appPath = Trim(DecodeField(fields, 8));
        const bool setDefault = At(fields, 9) == "1";

        if (!Profiles::Update(profile)) {
            SetStatus(L"保存失败：名称不能为空，也不能与其他配置重名", false);
            return BuildStateResponse();
        }

        if (setDefault) {
            Profiles::SetDefault(profile.id);
            SetStatus(L"配置已保存；点击“应用”后写入鼠标");
        } else if (profile.id == Profiles::DefaultId()) {
            SetStatus(L"已保存；请在另一个配置中勾选“设为默认”来更改回退项");
        } else {
            SetStatus(L"配置已保存；点击“应用”后写入鼠标");
        }
        return BuildStateResponse();
    }

    if (verb == "SET_AUTO_SWITCH") {
        const bool enabled = At(fields, 1) == "1";
        Profiles::SetAutoSwitchEnabled(enabled);
        SetStatus(enabled ? L"前台程序自动切换已开启" : L"前台程序自动切换已关闭");
        return BuildStateResponse();
    }

    if (verb == "SET_OSD") {
        const bool enabled = At(fields, 1) == "1";
        Osd::SetEnabled(enabled);
        SetStatus(enabled ? L"悬浮窗提示已开启" : L"悬浮窗提示已关闭");
        return BuildStateResponse();
    }

    if (verb == "SET_AUTORUN") {
        const bool enabled = At(fields, 1) == "1";
        if (Tray::IsAutoRunEnabled() != enabled) Tray::ToggleAutoRun();
        const bool actual = Tray::IsAutoRunEnabled();
        SetStatus(actual ? L"已启用开机自启动" : L"已关闭开机自启动");
        return BuildStateResponse();
    }

    if (verb == "SET_RF_FULL") {
        const bool full = At(fields, 1) == "1";
        if (!Device::SetRfFull(full)) {
            SetStatus(L"射频策略写入失败，请唤醒鼠标后重试", false);
        } else {
            SetStatus(full ? L"已切换为满格射频" : L"已切换为智能调节");
        }
        return BuildStateResponse();
    }

    if (verb == "SET_MODEL") {
        const auto& models = BatteryEstimate::Models();
        const int index = ToInt(At(fields, 1), -1);
        if (index >= 0 && index < (int)models.size()) {
            BatteryEstimate::SaveOverride(models[(size_t)index].key);
            SetStatus(L"续航估算型号已更新");
        } else {
            SetStatus(L"无效的鼠标型号", false);
        }
        return BuildStateResponse();
    }

    SetStatus(L"未知的设置请求", false);
    return BuildStateResponse();
}

std::string RequestMainState() {
    if (!g_mainWindow) return "ERROR\n";
    CommandContext context;
    context.command = "GET_STATE";
    SendMessageW(g_mainWindow, WM_APP_UI_COMMAND, 0, (LPARAM)&context);
    return context.response;
}

std::string HandleHeavyCommand(const std::string& command) {
    const auto fields = Split(command);
    const std::string verb = At(fields, 0);

    if (verb == "APPLY_PROFILE") {
        const int id = ToInt(At(fields, 1));
        const bool ok = id != 0 && Profiles::Select(id, false);
        SetStatus(ok ? L"配置已应用到鼠标" : L"应用失败，请唤醒鼠标后重试", ok);
        return RequestMainState();
    }

    if (verb == "DPI_APPLY") {
        const int selected = ToInt(At(fields, 1));
        const int count = ToInt(At(fields, 2));
        if (count <= 0 || count > Device::MAX_DPI_SLOTS || (int)fields.size() < 3 + count * 2) {
            SetStatus(L"DPI 档位数据无效", false);
            return BuildDpiResponse();
        }

        DpiWindow::QuickSlotList list;
        for (int i = 0; i < count; ++i) {
            DpiWindow::QuickSlot slot;
            slot.x = ToInt(At(fields, 3 + i * 2), 800);
            slot.y = ToInt(At(fields, 4 + i * 2), slot.x);
            list.slots.push_back(slot);
        }
        list.activeIndex = std::clamp(selected, 0, count - 1);
        const Device::State state = Device::GetCurrentState();
        BatteryEstimate::DpiSpec spec;
        BatteryEstimate::GetDpiSpecForState(state, spec, &list.modelKey, &list.modelLabel);

        const bool ok = DpiWindow::ApplyQuickSlots(list, list.activeIndex);
        if (ok) {
            SetStatus(state.isConnected ? L"已写入鼠标，并切换到所选 DPI 档位"
                                        : L"已保存到本机缓存；连接鼠标后再次点击“应用并切换”", true);
        } else {
            SetStatus(L"DPI 写入失败，请唤醒鼠标后重试；本机配置未丢失", false);
        }
        return BuildDpiResponse();
    }

    if (verb == "GET_DPI") return BuildDpiResponse();
    return RequestMainState();
}

std::string ExecuteCommand(const std::string& command) {
    const std::string verb = At(Split(command), 0);
    if (verb == "APPLY_PROFILE" || verb == "DPI_APPLY" || verb == "GET_DPI") {
        return HandleHeavyCommand(command);
    }
    if (!g_mainWindow) return "ERROR\n";
    CommandContext context;
    context.command = command;
    SendMessageW(g_mainWindow, WM_APP_UI_COMMAND, 0, (LPARAM)&context);
    return context.response;
}

void PipeServerThread() {
    const DWORD bufferSize = 256 * 1024;
    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1,
            bufferSize,
            bufferSize,
            0,
            NULL);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(g_stopEvent, 120) == WAIT_OBJECT_0) break;
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, NULL);
        DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && connectError != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            continue;
        }
        if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            break;
        }

        std::vector<char> request(bufferSize);
        DWORD read = 0;
        if (ReadFile(pipe, request.data(), bufferSize, &read, NULL) && read > 0) {
            std::string response = ExecuteCommand(std::string(request.data(), request.data() + read));
            DWORD total = 0;
            bool ok = true;
            while (total < response.size()) {
                DWORD written = 0;
                if (!WriteFile(pipe, response.data() + total, (DWORD)response.size() - total, &written, NULL) || written == 0) {
                    ok = false;
                    break;
                }
                total += written;
            }
            if (ok) FlushFileBuffers(pipe);
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

LRESULT MainThreadCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)hwnd;
    (void)msg;
    (void)wParam;
    CommandContext* context = reinterpret_cast<CommandContext*>(lParam);
    if (!context) return 0;
    context->response = HandleMainCommand(context->command);
    return 0;
}

} // namespace

bool Start(HWND mainWindow) {
    if (g_pipeThread.joinable()) return true;
    g_mainWindow = mainWindow;
    g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_stopEvent) {
        g_mainWindow = NULL;
        return false;
    }
    try {
        g_pipeThread = std::thread(PipeServerThread);
    } catch (...) {
        CloseHandle(g_stopEvent);
        g_stopEvent = NULL;
        g_mainWindow = NULL;
        return false;
    }
    return true;
}

void Stop() {
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_pipeThread.joinable()) {
        HANDLE wake = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
        CancelSynchronousIo((HANDLE)g_pipeThread.native_handle());
        g_pipeThread.join();
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = NULL;
    }
    g_mainWindow = NULL;
}

LRESULT HandleMainMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != WM_APP_UI_COMMAND) return 0;
    return MainThreadCommand(hwnd, msg, wParam, lParam);
}

} // namespace UiBridge

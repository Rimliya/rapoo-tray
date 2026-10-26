#include "settings_window.h"
#include "profile_manager.h"
#include "device_manager.h"
#include "dpi_window.h"
#include "battery_estimator.h"
#include "osd_window.h"
#include "tray_menu.h"
#include "settings.h"

#include <slint.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winver.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Settings {
namespace {

using RapooUi::DpiGearItem;
using RapooUi::MainWindow;
using RapooUi::ProfileItem;

constexpr const WCHAR* SOURCE_URL = L"https://github.com/Rimliya/rapoo-tray";
constexpr const WCHAR* RELEASES_URL = L"https://github.com/Rimliya/rapoo-tray/releases/latest";
constexpr int POLLING_RATES[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };

HWND g_owner = NULL;
std::atomic<bool> g_uiOpen{false};
std::thread g_uiThread;
bool g_updateAvailable = false;
std::wstring g_releaseUrl = RELEASES_URL;
slint::ComponentWeakHandle<MainWindow> g_uiWeak;

struct DpiState {
    std::vector<DpiWindow::QuickSlot> slots;
    std::wstring modelKey;
    std::wstring modelLabel;
    int selectedIndex = 0;
    int activeIndex = 0;
    int minimum = 10;
    int maximum = 30000;
    int step = 10;
    bool independent = false;
    bool loaded = false;
};
DpiState g_dpi;

struct VersionNumber {
    int part[4] = { 0, 0, 0, 0 };
};

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), NULL, 0);
    if (count <= 0) return std::wstring();
    std::wstring result((size_t)count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), count);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
    if (count <= 0) return std::string();
    std::string result((size_t)count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), count, NULL, NULL);
    return result;
}

slint::SharedString S(const std::wstring& text) {
    std::string utf8 = WideToUtf8(text);
    return slint::SharedString(utf8);
}

slint::SharedString S(const WCHAR* text) {
    return S(text ? std::wstring(text) : std::wstring());
}

std::wstring W(const slint::SharedString& text) {
    return Utf8ToWide(std::string(static_cast<std::string_view>(text)));
}

std::wstring Trim(std::wstring value) {
    size_t first = 0;
    while (first < value.size() && iswspace(value[first])) ++first;
    size_t last = value.size();
    while (last > first && iswspace(value[last - 1])) --last;
    return value.substr(first, last - first);
}

int PollingIndex(int hz) {
    int best = 3;
    int distance = 0x7fffffff;
    for (int i = 0; i < (int)(sizeof(POLLING_RATES) / sizeof(POLLING_RATES[0])); ++i) {
        int d = std::abs(POLLING_RATES[i] - hz);
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}

int PollingRate(int index) {
    index = std::clamp(index, 0, (int)(sizeof(POLLING_RATES) / sizeof(POLLING_RATES[0])) - 1);
    return POLLING_RATES[index];
}

std::wstring FormatDpi(const Profiles::Definition& profile) {
    if (profile.dpiX <= 0 || profile.dpiY <= 0) return L"跟随当前";
    if (profile.dpiX == profile.dpiY) return std::to_wstring(profile.dpiX);
    return std::to_wstring(profile.dpiX) + L"×" + std::to_wstring(profile.dpiY);
}

void SetStatus(MainWindow& ui, const std::wstring& text, bool good = true) {
    ui.set_status_text(S(text));
    ui.set_status_good(good);
}

int SnapDpi(int value) {
    int step = std::max(1, g_dpi.step);
    value = std::clamp(value, g_dpi.minimum, g_dpi.maximum);
    int offset = std::max(0, value - g_dpi.minimum);
    int snapped = g_dpi.minimum + ((offset + step / 2) / step) * step;
    return std::clamp(snapped, g_dpi.minimum, g_dpi.maximum);
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

bool ParseVersion(const std::wstring& text, VersionNumber& out) {
    VersionNumber parsed;
    std::wstring normalized = text;
    if (!normalized.empty() && (normalized[0] == L'v' || normalized[0] == L'V')) normalized.erase(normalized.begin());
    int count = swscanf_s(normalized.c_str(), L"%d.%d.%d.%d", &parsed.part[0], &parsed.part[1], &parsed.part[2], &parsed.part[3]);
    if (count < 2) return false;
    out = parsed;
    return true;
}

int CompareVersions(const VersionNumber& a, const VersionNumber& b) {
    for (int i = 0; i < 4; ++i) {
        if (a.part[i] < b.part[i]) return -1;
        if (a.part[i] > b.part[i]) return 1;
    }
    return 0;
}

std::wstring ExtractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return std::wstring();
    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::wstring();
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return std::wstring();
    std::string value;
    bool escaped = false;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        char ch = json[i];
        if (escaped) {
            if (ch == 'n') value.push_back('\n');
            else if (ch == 'r') value.push_back('\r');
            else if (ch == 't') value.push_back('\t');
            else value.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return Utf8ToWide(value);
        } else {
            value.push_back(ch);
        }
    }
    return std::wstring();
}

bool QueryLatestRelease(std::wstring& latestTag, std::wstring& releaseUrl, std::wstring& error) {
    latestTag.clear();
    releaseUrl = RELEASES_URL;
    error.clear();

    HINTERNET session = WinHttpOpen(L"rapoo-tray/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"rapoo-tray/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = L"无法初始化网络连接";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 8000);

    HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET",
        L"/repos/Rimliya/rapoo-tray/releases/latest", NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    const WCHAR* headers = L"Accept: application/vnd.github+json\r\n"
                           L"User-Agent: rapoo-tray\r\n"
                           L"X-GitHub-Api-Version: 2022-11-28\r\n";
    bool ok = request && WinHttpSendRequest(request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, NULL);
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (ok) {
        ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    }
    if (!ok || status != 200) {
        error = L"无法连接 GitHub Releases";
    } else {
        std::string body;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
            chunk.resize(read);
            body += chunk;
            if (body.size() > 256 * 1024) break;
        }
        latestTag = ExtractJsonString(body, "tag_name");
        std::wstring url = ExtractJsonString(body, "html_url");
        if (!url.empty()) releaseUrl = url;
        if (latestTag.empty()) error = L"未找到最新版本信息";
        ok = !latestTag.empty();
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}


void RefreshProfileList(MainWindow& ui) {
    std::vector<ProfileItem> rows;
    const int activeId = Profiles::ActiveId();
    const int defaultId = Profiles::DefaultId();
    for (const auto& profile : Profiles::GetAll()) {
        ProfileItem row;
        row.id = profile.id;
        row.name = S(profile.name);
        row.polling = S(std::to_wstring(profile.pollingHz) + L" Hz");
        row.dpi = S(FormatDpi(profile));
        row.app = S(profile.appName.empty() ? std::wstring(L"—") : profile.appName);
        row.badge = S(profile.id == activeId ? std::wstring(L"当前") : profile.id == defaultId ? std::wstring(L"默认") : std::wstring());
        row.is_default = profile.id == defaultId;
        rows.push_back(std::move(row));
    }
    ui.set_profiles(std::make_shared<slint::VectorModel<ProfileItem>>(rows));
}

void RefreshDeviceUi(MainWindow& ui) {
    Device::State state = Device::GetCurrentState();
    std::wstring name = state.modelName[0] ? state.modelName : std::wstring(L"雷柏游戏鼠标");
    std::wstring mode = !state.isConnected ? std::wstring(L"未连接")
                       : state.isWired ? std::wstring(L"USB 有线")
                                       : std::wstring(L"2.4G 无线");
    BatteryEstimate::Result estimate = BatteryEstimate::Calculate(state);
    std::wstring model = estimate.modelLabel.empty() ? name : estimate.modelLabel;

    ui.set_device_name(S(name));
    ui.set_device_mode(S(mode));
    ui.set_connected(state.isConnected);
    ui.set_battery(state.battery);
    ui.set_device_dpi_x(state.dpiX);
    ui.set_device_dpi_y(state.dpiY);
    ui.set_device_dpi_index(std::max(0, state.dpiLevel - 1));
    ui.set_model_label(S(model));
}

void InitializeModelOptions(MainWindow& ui) {
    const Device::State state = Device::GetCurrentState();
    const std::wstring selectedKey = BatteryEstimate::ResolveModelKey(state);
    std::vector<slint::SharedString> labels;
    int selectedIndex = 0;
    const auto& models = BatteryEstimate::Models();
    for (size_t i = 0; i < models.size(); ++i) {
        labels.push_back(S(models[i].label));
        if (_wcsicmp(models[i].key.c_str(), selectedKey.c_str()) == 0) selectedIndex = (int)i;
    }
    ui.set_model_loading(true);
    ui.set_model_options(std::make_shared<slint::VectorModel<slint::SharedString>>(labels));
    ui.set_model_index(selectedIndex);
    ui.set_model_loading(false);
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

void LoadProfileEditor(MainWindow& ui, int profileId) {
    Profiles::Definition profile;
    if (!Profiles::Find(profileId, profile)) return;

    int dpiX = profile.dpiX;
    int dpiY = profile.dpiY;
    if (dpiX <= 0 || dpiY <= 0) GetCurrentDpiDefaults(dpiX, dpiY);

    ui.set_profile_loading(true);
    ui.set_selected_profile_id(profile.id);
    ui.set_profile_name(S(profile.name));
    ui.set_profile_poll_index(PollingIndex(profile.pollingHz));
    ui.set_profile_sleep(profile.sleepMinutes);
    ui.set_profile_dpi_x(dpiX);
    ui.set_profile_dpi_y(dpiY);
    ui.set_profile_app_path(S(profile.appPath));
    ui.set_profile_is_default(profile.id == Profiles::DefaultId());
    ui.set_profile_dirty(false);
    ui.set_profile_loading(false);
}

bool SaveCurrentProfile(MainWindow& ui) {
    const int id = ui.get_selected_profile_id();
    Profiles::Definition profile;
    if (id == 0 || !Profiles::Find(id, profile)) {
        SetStatus(ui, L"请选择一个配置后再保存", false);
        return false;
    }

    std::wstring name = Trim(W(ui.get_profile_name()));
    if (name.empty()) {
        SetStatus(ui, L"配置名称不能为空", false);
        return false;
    }

    const Device::State state = Device::GetCurrentState();
    BatteryEstimate::DpiSpec spec;
    BatteryEstimate::GetDpiSpecForState(state, spec);

    profile.name = name;
    profile.pollingHz = Profiles::NormalizePollingRate(PollingRate(ui.get_profile_poll_index()));
    profile.sleepMinutes = Rapoo::ClampSleepMinutes(ui.get_profile_sleep());
    profile.dpiX = std::clamp(ui.get_profile_dpi_x(), spec.minDpi, spec.maxDpi);
    profile.dpiY = std::clamp(ui.get_profile_dpi_y(), spec.minDpi, spec.maxDpi);
    profile.appPath = Trim(W(ui.get_profile_app_path()));

    if (!Profiles::Update(profile)) {
        SetStatus(ui, L"保存失败：名称不能为空，也不能与其他配置重名", false);
        return false;
    }

    bool keptDefault = false;
    if (ui.get_profile_is_default()) {
        Profiles::SetDefault(profile.id);
    } else if (profile.id == Profiles::DefaultId()) {
        Profiles::SetDefault(profile.id);
        ui.set_profile_is_default(true);
        keptDefault = true;
    }

    ui.set_profile_dirty(false);
    RefreshProfileList(ui);
    LoadProfileEditor(ui, profile.id);
    if (keptDefault) {
        SetStatus(ui, L"已保存；请在另一个配置中勾选“设为默认”来更改回退项");
    } else {
        SetStatus(ui, L"配置已保存并应用");
    }
    return true;
}

void AddProfile(MainWindow& ui) {
    if (ui.get_profile_dirty() && !SaveCurrentProfile(ui)) return;
    int dpiX = 800;
    int dpiY = 800;
    GetCurrentDpiDefaults(dpiX, dpiY);
    int id = Profiles::Create(Profiles::MakeUniqueName(), 1000, 10, dpiX, dpiY);
    if (id == 0) {
        SetStatus(ui, L"无法新增配置，最多可创建 16 个配置", false);
        return;
    }
    RefreshProfileList(ui);
    LoadProfileEditor(ui, id);
    SetStatus(ui, L"已新增配置，请修改名称和参数");
}

void DeleteProfile(MainWindow& ui) {
    if (ui.get_profile_dirty() && !SaveCurrentProfile(ui)) return;
    const int id = ui.get_selected_profile_id();
    Profiles::Definition profile;
    if (id == 0 || !Profiles::Find(id, profile)) return;

    std::wstring prompt = L"确定删除配置“" + profile.name + L"”吗？";
    if (MessageBoxW(g_owner, prompt.c_str(), L"删除配置", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!Profiles::Delete(id)) {
        SetStatus(ui, L"至少需要保留一个配置", false);
        return;
    }

    int nextId = Profiles::ActiveId();
    Profiles::Definition next;
    if (!Profiles::Find(nextId, next)) nextId = Profiles::DefaultId();
    RefreshProfileList(ui);
    LoadProfileEditor(ui, nextId);
    SetStatus(ui, L"配置已删除");
}

void BrowseProfileApp(MainWindow& ui) {
    WCHAR path[32768] = {0};
    std::wstring current = W(ui.get_profile_app_path());
    if (!current.empty()) wcsncpy_s(path, current.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_owner;
    dialog.lpstrFilter = L"可执行程序 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    dialog.lpstrTitle = L"选择需要自动切换配置的程序";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        ui.set_profile_app_path(S(std::wstring(path)));
        ui.set_profile_dirty(true);
        SetStatus(ui, L"已选择关联程序，保存后生效");
    }
}

void ClearProfileApp(MainWindow& ui) {
    ui.set_profile_app_path(slint::SharedString());
    ui.set_profile_dirty(true);
    SetStatus(ui, L"已清除关联程序，保存后生效");
}

void CaptureProfileDpi(MainWindow& ui) {
    Device::State state = Device::GetCurrentState();
    if (!state.isConnected || !Device::IsDpiConfigValid() || state.dpiX <= 0) {
        SetStatus(ui, L"暂未读取到鼠标 DPI，请唤醒设备后重试", false);
        return;
    }
    BatteryEstimate::DpiSpec spec;
    BatteryEstimate::GetDpiSpecForState(state, spec);
    int x = std::clamp(state.dpiX, spec.minDpi, spec.maxDpi);
    int y = std::clamp(state.dpiY > 0 ? state.dpiY : state.dpiX, spec.minDpi, spec.maxDpi);
    ui.set_profile_dpi_x(x);
    ui.set_profile_dpi_y(y);
    ui.set_profile_dirty(true);
    SetStatus(ui, L"已读取鼠标当前 DPI，保存后写入此档案");
}

void RefreshDpiModel(MainWindow& ui) {
    std::vector<DpiGearItem> rows;
    for (size_t i = 0; i < g_dpi.slots.size(); ++i) {
        DpiGearItem row;
        row.index = (int)i;
        row.value = S(std::to_wstring(g_dpi.slots[i].x));
        row.active = (int)i == g_dpi.activeIndex;
        rows.push_back(std::move(row));
    }
    ui.set_dpi_gears(std::make_shared<slint::VectorModel<DpiGearItem>>(rows));
}

void SyncDpiEditor(MainWindow& ui) {
    if (g_dpi.slots.empty()) return;
    g_dpi.selectedIndex = std::clamp(g_dpi.selectedIndex, 0, (int)g_dpi.slots.size() - 1);
    g_dpi.activeIndex = std::clamp(g_dpi.activeIndex, 0, (int)g_dpi.slots.size() - 1);
    const DpiWindow::QuickSlot& slot = g_dpi.slots[(size_t)g_dpi.selectedIndex];
    ui.set_dpi_loading(true);
    ui.set_selected_dpi_index(g_dpi.selectedIndex);
    ui.set_dpi_active_index(g_dpi.activeIndex);
    ui.set_dpi_x(slot.x);
    ui.set_dpi_y(slot.y);
    ui.set_dpi_independent(g_dpi.independent);
    ui.set_dpi_min(g_dpi.minimum);
    ui.set_dpi_max(g_dpi.maximum);
    ui.set_dpi_step(g_dpi.step);
    ui.set_dpi_loading(false);
    RefreshDpiModel(ui);
}

void LoadDpiState(MainWindow& ui, bool force) {
    if (g_dpi.loaded && !force) return;
    Device::State state = Device::GetCurrentState();
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
    g_dpi.slots = list.slots;
    g_dpi.modelKey = list.modelKey.empty() ? key : list.modelKey;
    g_dpi.modelLabel = list.modelLabel.empty() ? label : list.modelLabel;
    g_dpi.minimum = spec.minDpi;
    g_dpi.maximum = spec.maxDpi;
    g_dpi.step = std::max(1, spec.step);
    g_dpi.activeIndex = std::clamp(list.activeIndex, 0, (int)g_dpi.slots.size() - 1);
    g_dpi.selectedIndex = g_dpi.activeIndex;
    g_dpi.independent = false;
    for (const auto& slot : g_dpi.slots) {
        if (slot.x != slot.y) {
            g_dpi.independent = true;
            break;
        }
    }
    for (auto& slot : g_dpi.slots) {
        slot.x = SnapDpi(slot.x);
        slot.y = SnapDpi(slot.y);
    }
    g_dpi.loaded = true;
    ui.set_dpi_dirty(false);
    SyncDpiEditor(ui);
    SetStatus(ui, list.fromDevice ? L"已读取鼠标当前 DPI 档位；修改后点击“应用并切换”写入鼠标"
                                  : L"当前显示本机缓存或当前 DPI；应用后会写入鼠标");
}

bool CommitDpiEditor(MainWindow& ui) {
    if (g_dpi.slots.empty()) return false;
    g_dpi.selectedIndex = std::clamp(ui.get_selected_dpi_index(), 0, (int)g_dpi.slots.size() - 1);
    int x = SnapDpi(ui.get_dpi_x());
    int y = ui.get_dpi_independent() ? SnapDpi(ui.get_dpi_y()) : x;
    DpiWindow::QuickSlot& slot = g_dpi.slots[(size_t)g_dpi.selectedIndex];
    if (slot.x != x || slot.y != y || g_dpi.independent != ui.get_dpi_independent()) {
        slot.x = x;
        slot.y = y;
        g_dpi.independent = ui.get_dpi_independent();
        ui.set_dpi_dirty(true);
        RefreshDpiModel(ui);
    }
    return true;
}

void SelectDpiGear(MainWindow& ui, int index) {
    if (index < 0 || index >= (int)g_dpi.slots.size()) return;
    CommitDpiEditor(ui);
    g_dpi.selectedIndex = index;
    SyncDpiEditor(ui);
}

void AddDpiGear(MainWindow& ui) {
    if (!CommitDpiEditor(ui)) return;
    if ((int)g_dpi.slots.size() >= 7) {
        SetStatus(ui, L"当前型号已达到最大 DPI 档位数量", false);
        return;
    }
    const int candidates[] = {400, 800, 1200, 1600, 2400, 3200, 6400, 12800, 30000, 60000};
    int value = SnapDpi(g_dpi.maximum);
    for (int candidate : candidates) {
        int snapped = SnapDpi(candidate);
        bool used = false;
        for (const auto& slot : g_dpi.slots) used = used || slot.x == snapped;
        if (!used) {
            value = snapped;
            break;
        }
    }
    DpiWindow::QuickSlot slot;
    slot.x = value;
    slot.y = value;
    g_dpi.slots.push_back(slot);
    g_dpi.selectedIndex = (int)g_dpi.slots.size() - 1;
    g_dpi.independent = false;
    ui.set_dpi_dirty(true);
    SyncDpiEditor(ui);
    SetStatus(ui, L"已添加档位，修改数值后点击“应用并切换”");
}

void DeleteDpiGear(MainWindow& ui) {
    if (g_dpi.slots.size() <= 1) {
        SetStatus(ui, L"至少需要保留一个 DPI 档位", false);
        return;
    }
    CommitDpiEditor(ui);
    int index = std::clamp(g_dpi.selectedIndex, 0, (int)g_dpi.slots.size() - 1);
    bool deletingActive = index == g_dpi.activeIndex;
    g_dpi.slots.erase(g_dpi.slots.begin() + index);
    g_dpi.selectedIndex = std::min(index, (int)g_dpi.slots.size() - 1);
    if (deletingActive) g_dpi.activeIndex = g_dpi.selectedIndex;
    else if (g_dpi.activeIndex > index) --g_dpi.activeIndex;
    g_dpi.activeIndex = std::clamp(g_dpi.activeIndex, 0, (int)g_dpi.slots.size() - 1);
    ui.set_dpi_dirty(true);
    SyncDpiEditor(ui);
    SetStatus(ui, L"已删除档位，应用后写入鼠标");
}

void ApplyDpiGear(MainWindow& ui) {
    if (!CommitDpiEditor(ui)) return;
    DpiWindow::QuickSlotList list;
    list.slots = g_dpi.slots;
    list.activeIndex = g_dpi.selectedIndex;
    list.modelKey = g_dpi.modelKey;
    list.modelLabel = g_dpi.modelLabel;
    bool connected = Device::GetCurrentState().isConnected;
    if (!DpiWindow::ApplyQuickSlots(list, g_dpi.selectedIndex)) {
        SetStatus(ui, L"DPI 写入失败，请唤醒鼠标后重试；本机配置未丢失", false);
        return;
    }
    g_dpi.activeIndex = g_dpi.selectedIndex;
    g_dpi.independent = false;
    for (const auto& slot : g_dpi.slots) {
        if (slot.x != slot.y) {
            g_dpi.independent = true;
            break;
        }
    }
    ui.set_dpi_dirty(false);
    SyncDpiEditor(ui);
    RefreshDeviceUi(ui);
    SetStatus(ui, connected ? L"已写入鼠标，并切换到所选 DPI 档位"
                            : L"已保存到本机缓存；连接鼠标后再次点击“应用并切换”",
              connected);
}

void RefreshUi(MainWindow& ui) {
    RefreshDeviceUi(ui);
    RefreshProfileList(ui);
    ui.set_auto_switch_enabled(Profiles::AutoSwitchEnabled());
    ui.set_osd_enabled(Osd::IsEnabled());
    ui.set_auto_run_enabled(Tray::IsAutoRunEnabled());

    Device::State state = Device::GetCurrentState();
    if (state.rfAvailable) ui.set_rf_full(state.rfFull);
}

void OpenUrl(const std::wstring& url) {
    HINSTANCE result = ShellExecuteW(g_owner, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    (void)result;
}

void StartUpdateCheck() {
    auto locked = g_uiWeak.lock();
    if (!locked) return;
    MainWindow& ui = *(*locked);
    if (ui.get_update_busy()) return;
    ui.set_update_busy(true);
    ui.set_update_status(S(L"正在检查 GitHub 最新发行版…"));
    ui.set_update_button_text(S(L"检查中…"));
    g_updateAvailable = false;

    std::wstring currentVersion = W(ui.get_about_version());
    auto weak = g_uiWeak;
    std::thread([weak, currentVersion] {
        std::wstring latestTag;
        std::wstring releaseUrl = RELEASES_URL;
        std::wstring error;
        bool success = QueryLatestRelease(latestTag, releaseUrl, error);
        bool newer = false;
        if (success) {
            VersionNumber current;
            VersionNumber latest;
            if (!ParseVersion(currentVersion, current) || !ParseVersion(latestTag, latest)) {
                success = false;
                error = L"版本信息格式无效";
            } else {
                newer = CompareVersions(latest, current) > 0;
            }
        }

        slint::invoke_from_event_loop([weak, success, newer, latestTag, releaseUrl, error] {
            auto locked = weak.lock();
            if (!locked) return;
            MainWindow& view = *(*locked);
            view.set_update_busy(false);
            if (!success) {
                view.set_update_status(S(L"检查失败：" + error));
                view.set_update_button_text(S(L"重新检查"));
                return;
            }
            if (newer) {
                g_updateAvailable = true;
                g_releaseUrl = releaseUrl;
                view.set_update_status(S(L"发现新版本 " + latestTag + L"，可前往 GitHub 下载"));
                view.set_update_button_text(S(L"前往下载"));
            } else {
                g_updateAvailable = false;
                g_releaseUrl = releaseUrl;
                view.set_update_status(S(L"已是最新版本 " + latestTag));
                view.set_update_button_text(S(L"再次检查"));
            }
        });
    }).detach();
}

void RegisterCallbacks(MainWindow& ui) {
    MainWindow* view = &ui;

    view->on_page_changed([view](int page) {
        if (page == 2) LoadDpiState(*view, false);
        if (page == 3) view->set_update_status(S(L"检查 GitHub Releases 上是否有新版本"));
    });

    view->on_profile_selected([view](int id) {
        if (id == view->get_selected_profile_id()) return;
        if (view->get_profile_dirty() && !SaveCurrentProfile(*view)) return;
        LoadProfileEditor(*view, id);
        SetStatus(*view, L"选择配置后可直接修改；点击“保存并应用”生效");
    });
    view->on_profile_save([view] { SaveCurrentProfile(*view); });
    view->on_profile_add([view] { AddProfile(*view); });
    view->on_profile_delete([view] { DeleteProfile(*view); });
    view->on_profile_browse([view] { BrowseProfileApp(*view); });
    view->on_profile_clear_app([view] { ClearProfileApp(*view); });
    view->on_profile_capture_dpi([view] { CaptureProfileDpi(*view); });

    view->on_auto_switch_changed([view](bool enabled) {
        Profiles::SetAutoSwitchEnabled(enabled);
        SetStatus(*view, enabled ? L"前台程序自动切换已开启" : L"前台程序自动切换已关闭");
    });
    view->on_osd_changed([view](bool enabled) {
        Osd::SetEnabled(enabled);
        SetStatus(*view, enabled ? L"悬浮窗提示已开启" : L"悬浮窗提示已关闭");
    });
    view->on_startup_changed([view](bool enabled) {
        if (Tray::IsAutoRunEnabled() != enabled) Tray::ToggleAutoRun();
        bool actual = Tray::IsAutoRunEnabled();
        view->set_auto_run_enabled(actual);
        SetStatus(*view, actual ? L"已启用开机自启动" : L"已关闭开机自启动");
    });
    view->on_rf_changed([view](bool full) {
        if (!Device::SetRfFull(full)) {
            view->set_rf_full(!full);
            SetStatus(*view, L"射频策略写入失败，请唤醒鼠标后重试", false);
            return;
        }
        RefreshDeviceUi(*view);
        SetStatus(*view, full ? L"已切换为满格射频" : L"已切换为智能调节");
    });
    view->on_model_changed([view](int index) {
        const auto& models = BatteryEstimate::Models();
        if (index < 0 || index >= (int)models.size()) return;
        BatteryEstimate::SaveOverride(models[(size_t)index].key);
        InitializeModelOptions(*view);
        RefreshDeviceUi(*view);
        SetStatus(*view, L"续航估算型号已更新");
    });

    view->on_dpi_gear_selected([view](int index) { SelectDpiGear(*view, index); });
    view->on_dpi_gear_add([view] { AddDpiGear(*view); });
    view->on_dpi_gear_delete([view] { DeleteDpiGear(*view); });
    view->on_dpi_values_edited([view] {
        SetStatus(*view, L"DPI 修改尚未写入鼠标");
    });
    view->on_dpi_apply([view] { ApplyDpiGear(*view); });

    view->on_check_update([view] {
        if (g_updateAvailable) {
            OpenUrl(g_releaseUrl);
        } else {
            StartUpdateCheck();
        }
    });
    view->on_open_source([view] {
        (void)view;
        OpenUrl(SOURCE_URL);
    });
    view->on_close_window([view] {
        view->hide();
        slint::quit_event_loop();
    });
    view->on_refresh_tick([view] { RefreshUi(*view); });

    view->window().on_close_requested([] {
        slint::quit_event_loop();
        return slint::CloseRequestResponse::HideWindow;
    });
}

void RunUiThread(HWND owner, int page) {
    g_owner = owner;
    g_dpi = DpiState();
    g_updateAvailable = false;
    g_releaseUrl = RELEASES_URL;

    try {
        Profiles::Initialize();
        auto handle = MainWindow::create();
        g_uiWeak = slint::ComponentWeakHandle(handle);

        RefreshDeviceUi(*handle);
        RefreshProfileList(*handle);
        InitializeModelOptions(*handle);
        handle->set_auto_switch_enabled(Profiles::AutoSwitchEnabled());
        handle->set_osd_enabled(Osd::IsEnabled());
        handle->set_auto_run_enabled(Tray::IsAutoRunEnabled());
        Device::State state = Device::GetCurrentState();
        if (state.rfAvailable) handle->set_rf_full(state.rfFull);
        handle->set_about_version(S(GetExecutableVersion()));
        handle->set_page(std::clamp(page, 0, 3));

        int startProfile = Profiles::ActiveId();
        Profiles::Definition profile;
        if (!Profiles::Find(startProfile, profile)) startProfile = Profiles::DefaultId();
        LoadProfileEditor(*handle, startProfile);
        SetStatus(*handle, L"选择配置进行编辑，或新增一个配置");

        RegisterCallbacks(*handle);
        if (page == 2) LoadDpiState(*handle, true);

        handle->show();
        slint::run_event_loop();
    } catch (const std::exception& error) {
        std::wstring message = L"Slint 界面启动失败：";
        message += Utf8ToWide(error.what());
        MessageBoxW(owner, message.c_str(), L"rapoo-tray", MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(owner, L"Slint 界面发生未知错误。", L"rapoo-tray", MB_OK | MB_ICONERROR);
    }

    g_owner = NULL;
    g_dpi = DpiState();
    g_updateAvailable = false;
    g_uiOpen.store(false);
}

void OpenUiPage(HWND owner, int page) {
    page = std::clamp(page, 0, 3);
    if (g_uiOpen.load(std::memory_order_acquire)) {
        try {
            slint::invoke_from_event_loop([page] {
                auto locked = g_uiWeak.lock();
                if (!locked) return;
                MainWindow& view = *(*locked);
                view.set_page(page);
                if (page == 2) LoadDpiState(view, false);
                if (page == 3) view.set_update_status(S(L"检查 GitHub Releases 上是否有新版本"));
                view.show();
            });
        } catch (...) {
        }
        return;
    }

    if (g_uiThread.joinable()) g_uiThread.join();
    g_uiOpen.store(true, std::memory_order_release);
    try {
        g_uiThread = std::thread([owner, page] { RunUiThread(owner, page); });
    } catch (const std::exception& error) {
        g_uiOpen.store(false, std::memory_order_release);
        std::wstring message = L"无法启动 Slint 界面线程：";
        message += Utf8ToWide(error.what());
        MessageBoxW(owner, message.c_str(), L"rapoo-tray", MB_OK | MB_ICONERROR);
    }
}

} // namespace

void Show(HWND owner) {
    OpenUiPage(owner, 0);
}

void ShowPage(HWND owner, int page) {
    OpenUiPage(owner, page);
}

void Cleanup() {
    if (g_uiOpen.load(std::memory_order_acquire)) {
        try {
            slint::invoke_from_event_loop([] { slint::quit_event_loop(); });
        } catch (...) {
        }
    }
    if (g_uiThread.joinable() && g_uiThread.get_id() != std::this_thread::get_id()) {
        g_uiThread.join();
    }
}

bool PreTranslateMessage(MSG*) {
    return false;
}

} // namespace Settings




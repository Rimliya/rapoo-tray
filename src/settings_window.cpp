#include "settings_window.h"
#include "profile_manager.h"
#include "osd_window.h"
#include "rapoo_protocol.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>
#include <algorithm>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace Settings {
namespace {

constexpr const WCHAR* WINDOW_CLASS = L"RapooProfilesSettingsWnd";
constexpr int WINDOW_WIDTH = 780;
constexpr int WINDOW_HEIGHT = 620;

enum ControlId {
    IDC_PROFILE_LIST = 3001,
    IDC_ADD_PROFILE,
    IDC_DELETE_PROFILE,
    IDC_PROFILE_NAME,
    IDC_POLLING_RATE,
    IDC_SLEEP_TIME,
    IDC_DEFAULT_PROFILE,
    IDC_APP_PATH,
    IDC_BROWSE_APP,
    IDC_CLEAR_APP,
    IDC_AUTO_SWITCH,
    IDC_OSD_ENABLED,
    IDC_SAVE,
    IDC_CLOSE,
    IDC_STATUS
};

HWND g_hwnd = NULL;
HWND g_hOwner = NULL;
HWND g_hProfileList = NULL;
HWND g_hTitle = NULL;
HWND g_hSubtitle = NULL;
HWND g_hStatus = NULL;
HFONT g_fontNormal = NULL;
HFONT g_fontBold = NULL;
HFONT g_fontTitle = NULL;
HFONT g_fontSmall = NULL;
int g_dpi = 96;
int g_selectedId = 0;
bool g_loading = false;
bool g_dirty = false;
bool g_refreshingList = false;
bool g_statusGood = true;
bool g_classRegistered = false;

int S(int value) {
    return MulDiv(value, g_dpi, 96);
}

UINT GetSystemDpiSafe() {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef UINT (WINAPI *pfnGetDpiForSystem)();
        pfnGetDpiForSystem fn = (pfnGetDpiForSystem)GetProcAddress(hUser, "GetDpiForSystem");
        if (fn) {
            UINT dpi = fn();
            if (dpi > 0) return dpi;
        }
    }
    HDC dc = GetDC(NULL);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return dpi > 0 ? dpi : 96;
}

UINT GetDpiForHwnd(HWND hWnd) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
        pfnGetDpiForWindow fn = (pfnGetDpiForWindow)GetProcAddress(hUser, "GetDpiForWindow");
        if (fn && hWnd) {
            UINT dpi = fn(hWnd);
            if (dpi > 0) return dpi;
        }
    }
    return 96;
}

HFONT MakeFont(int px, int weight) {
    return CreateFontW(
        -S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI"
    );
}

void SetText(HWND control, const WCHAR* text) {
    if (control) SetWindowTextW(control, text ? text : L"");
}

std::wstring GetText(HWND control) {
    WCHAR buffer[2048] = {0};
    if (control) GetWindowTextW(control, buffer, ARRAYSIZE(buffer));
    return buffer;
}

void SetStatus(const WCHAR* text, bool good = true, bool clearDirty = false) {
    g_statusGood = good;
    if (clearDirty) g_dirty = false;
    SetText(g_hStatus, text);
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
}

HFONT GetNormalFont() {
    return g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

HWND CreateControl(const WCHAR* className, const WCHAR* text, DWORD style, DWORD exStyle, int x, int y, int w, int h, int id) {
    HWND control = CreateWindowExW(
        exStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        S(x), S(y), S(w), S(h),
        g_hwnd,
        (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE),
        NULL
    );
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)GetNormalFont(), TRUE);
    return control;
}

HWND CreateText(const WCHAR* text, int x, int y, int w, int h, int id = 0, DWORD style = SS_LEFT) {
    return CreateControl(L"STATIC", text, style, 0, x, y, w, h, id);
}

HWND CreateButton(const WCHAR* text, int x, int y, int w, int h, int id) {
    return CreateControl(L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, 0, x, y, w, h, id);
}

void ApplyFonts() {
    EnumChildWindows(g_hwnd, [](HWND child, LPARAM) -> BOOL {
        if (child == g_hTitle) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
        } else if (child == g_hSubtitle || child == g_hStatus) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
        } else {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
        }
        return TRUE;
    }, 0);
}

void MarkDirty() {
    if (g_loading) return;
    g_dirty = true;
    SetStatus(L"有未保存的更改", true);
}

void EnableClearButton(bool enabled);

int SelectedProfileId() {
    if (!g_hProfileList) return 0;
    int item = ListView_GetNextItem(g_hProfileList, -1, LVNI_SELECTED);
    if (item < 0) return 0;
    LVITEMW lv = {0};
    lv.mask = LVIF_PARAM;
    lv.iItem = item;
    if (!ListView_GetItem(g_hProfileList, &lv)) return 0;
    return (int)lv.lParam;
}

void FindPollingSelection(int hz) {
    HWND combo = GetDlgItem(g_hwnd, IDC_POLLING_RATE);
    int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        if ((int)SendMessageW(combo, CB_GETITEMDATA, i, 0) == hz) {
            SendMessageW(combo, CB_SETCURSEL, i, 0);
            return;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void LoadEditor(int profileId) {
    Profiles::Definition profile;
    if (!Profiles::Find(profileId, profile)) return;

    g_loading = true;
    g_selectedId = profileId;
    SetText(GetDlgItem(g_hwnd, IDC_PROFILE_NAME), profile.name.c_str());
    FindPollingSelection(profile.pollingHz);

    WCHAR sleepText[32] = {0};
    StringCchPrintfW(sleepText, ARRAYSIZE(sleepText), L"%d", profile.sleepMinutes);
    SetText(GetDlgItem(g_hwnd, IDC_SLEEP_TIME), sleepText);
    SetText(GetDlgItem(g_hwnd, IDC_APP_PATH), profile.appPath.c_str());
    EnableClearButton(!profile.appPath.empty());
    CheckDlgButton(g_hwnd, IDC_DEFAULT_PROFILE, profileId == Profiles::DefaultId() ? BST_CHECKED : BST_UNCHECKED);
    g_dirty = false;
    g_loading = false;
}

void RefreshList(int selectId) {
    if (!g_hProfileList) return;
    g_refreshingList = true;
    SendMessageW(g_hProfileList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hProfileList);

    const auto profiles = Profiles::GetAll();
    int selectedRow = -1;
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& profile = profiles[i];
        std::wstring status;
        if (profile.id == Profiles::ActiveId()) status = L"当前";
        if (profile.id == Profiles::DefaultId()) {
            if (!status.empty()) status += L" · ";
            status += L"默认";
        }

        LVITEMW lv = {0};
        lv.mask = LVIF_TEXT | LVIF_PARAM;
        lv.iItem = (int)i;
        lv.pszText = (LPWSTR)profile.name.c_str();
        lv.lParam = (LPARAM)profile.id;
        int row = ListView_InsertItem(g_hProfileList, &lv);

        WCHAR rate[32] = {0};
        StringCchPrintfW(rate, ARRAYSIZE(rate), L"%d Hz", profile.pollingHz);
        ListView_SetItemText(g_hProfileList, row, 1, rate);

        WCHAR sleep[32] = {0};
        StringCchPrintfW(sleep, ARRAYSIZE(sleep), L"%d 分钟", profile.sleepMinutes);
        ListView_SetItemText(g_hProfileList, row, 2, sleep);

        std::wstring app = profile.appName.empty() ? L"—" : profile.appName;
        ListView_SetItemText(g_hProfileList, row, 3, (LPWSTR)app.c_str());
        ListView_SetItemText(g_hProfileList, row, 4, (LPWSTR)status.c_str());

        if (profile.id == selectId) selectedRow = row;
    }

    if (selectedRow >= 0) {
        ListView_SetItemState(g_hProfileList, selectedRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_hProfileList, selectedRow, FALSE);
    }
    SendMessageW(g_hProfileList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hProfileList, NULL, TRUE);
    g_refreshingList = false;
}

bool CommitEditor(bool showErrors) {
    if (!g_dirty) return true;
    Profiles::Definition profile;
    if (g_selectedId == 0 || !Profiles::Find(g_selectedId, profile)) return false;

    std::wstring name = GetText(GetDlgItem(g_hwnd, IDC_PROFILE_NAME));
    if (name.empty()) {
        if (showErrors) MessageBoxW(g_hwnd, L"配置名称不能为空。", L"无法保存", MB_OK | MB_ICONWARNING);
        return false;
    }

    HWND pollCombo = GetDlgItem(g_hwnd, IDC_POLLING_RATE);
    int pollIndex = (int)SendMessageW(pollCombo, CB_GETCURSEL, 0, 0);
    if (pollIndex == CB_ERR) pollIndex = 0;
    int polling = (int)SendMessageW(pollCombo, CB_GETITEMDATA, pollIndex, 0);
    profile.pollingHz = Profiles::NormalizePollingRate(polling);

    BOOL translated = FALSE;
    UINT sleepMinutes = GetDlgItemInt(g_hwnd, IDC_SLEEP_TIME, &translated, FALSE);
    if (!translated) sleepMinutes = 10;
    sleepMinutes = (UINT)Rapoo::ClampSleepMinutes((int)sleepMinutes);
    profile.sleepMinutes = (int)sleepMinutes;

    profile.name = name;
    profile.appPath = GetText(GetDlgItem(g_hwnd, IDC_APP_PATH));
    if (!Profiles::Update(profile)) {
        if (showErrors) {
            MessageBoxW(
                g_hwnd,
                L"配置保存失败。名称不能为空，也不能与其他配置重名。",
                L"无法保存",
                MB_OK | MB_ICONWARNING
            );
        }
        return false;
    }

    bool keptDefault = false;
    if (IsDlgButtonChecked(g_hwnd, IDC_DEFAULT_PROFILE) == BST_CHECKED) {
        Profiles::SetDefault(profile.id);
    } else if (profile.id == Profiles::DefaultId()) {
        // Exactly one profile must always remain the fallback profile.
        CheckDlgButton(g_hwnd, IDC_DEFAULT_PROFILE, BST_CHECKED);
        keptDefault = true;
    }
    Profiles::SetAutoSwitchEnabled(IsDlgButtonChecked(g_hwnd, IDC_AUTO_SWITCH) == BST_CHECKED);
    Osd::SetEnabled(IsDlgButtonChecked(g_hwnd, IDC_OSD_ENABLED) == BST_CHECKED);

    g_dirty = false;
    RefreshList(profile.id);
    if (keptDefault) {
        SetStatus(L"已保存；请在另一个配置中勾选“设为默认”来更改回退项", true);
    } else {
        SetStatus(L"配置已保存并应用", true);
    }
    return true;
}

void EnableClearButton(bool enabled) {
    HWND clear = GetDlgItem(g_hwnd, IDC_CLEAR_APP);
    if (clear) EnableWindow(clear, enabled ? TRUE : FALSE);
}

void AddProfile() {
    if (!CommitEditor(true)) return;
    std::wstring name = Profiles::MakeUniqueName();
    int id = Profiles::Create(name, 1000, 10);
    if (id == 0) {
        MessageBoxW(g_hwnd, L"无法新增配置，最多可创建 16 个配置。", L"新增失败", MB_OK | MB_ICONWARNING);
        return;
    }
    RefreshList(id);
    LoadEditor(id);
    SetStatus(L"已新增配置，请修改名称和参数", true);
    HWND nameEdit = GetDlgItem(g_hwnd, IDC_PROFILE_NAME);
    SetFocus(nameEdit);
    SendMessageW(nameEdit, EM_SETSEL, 0, -1);
}

void DeleteProfile() {
    if (!CommitEditor(true)) return;
    Profiles::Definition profile;
    if (g_selectedId == 0 || !Profiles::Find(g_selectedId, profile)) return;

    std::wstring prompt = L"确定删除配置“" + profile.name + L"”吗？";
    if (MessageBoxW(g_hwnd, prompt.c_str(), L"删除配置", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!Profiles::Delete(g_selectedId)) {
        MessageBoxW(g_hwnd, L"至少需要保留一个配置。", L"无法删除", MB_OK | MB_ICONWARNING);
        return;
    }

    int nextId = Profiles::ActiveId();
    Profiles::Definition next;
    if (!Profiles::Find(nextId, next)) nextId = Profiles::DefaultId();
    RefreshList(nextId);
    LoadEditor(nextId);
    SetStatus(L"配置已删除", true);
}

void BrowseApp() {
    WCHAR path[32768] = {0};
    std::wstring current = GetText(GetDlgItem(g_hwnd, IDC_APP_PATH));
    if (!current.empty()) {
        StringCchCopyW(path, ARRAYSIZE(path), current.c_str());
    }

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"可执行程序 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = ARRAYSIZE(path);
    ofn.lpstrTitle = L"选择需要自动切换配置的程序";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        SetText(GetDlgItem(g_hwnd, IDC_APP_PATH), path);
        EnableClearButton(true);
        MarkDirty();
    }
}

void ClearApp() {
    SetText(GetDlgItem(g_hwnd, IDC_APP_PATH), L"");
    EnableClearButton(false);
    MarkDirty();
}

void CreateControls() {
    g_hTitle = CreateText(L"配置与自动化", 24, 18, 400, 32);
    g_hSubtitle = CreateText(
        L"快速切换回报率配置；指定前台程序后，进入程序时自动应用，离开后恢复默认配置。",
        24, 50, 730, 22
    );
    CreateText(L"配置档案", 24, 84, 200, 22);

    g_hProfileList = CreateControl(
        WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        WS_EX_CLIENTEDGE, 24, 108, 330, 214, IDC_PROFILE_LIST
    );
    ListView_SetExtendedListViewStyle(g_hProfileList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = (LPWSTR)L"名称";
    col.cx = S(82);
    ListView_InsertColumn(g_hProfileList, 0, &col);
    col.pszText = (LPWSTR)L"回报率";
    col.cx = S(58);
    ListView_InsertColumn(g_hProfileList, 1, &col);
    col.pszText = (LPWSTR)L"休眠";
    col.cx = S(52);
    ListView_InsertColumn(g_hProfileList, 2, &col);
    col.pszText = (LPWSTR)L"关联程序";
    col.cx = S(78);
    ListView_InsertColumn(g_hProfileList, 3, &col);
    col.pszText = (LPWSTR)L"状态";
    col.cx = S(60);
    ListView_InsertColumn(g_hProfileList, 4, &col);

    CreateButton(L"新增配置", 24, 330, 100, 30, IDC_ADD_PROFILE);
    CreateButton(L"删除配置", 132, 330, 100, 30, IDC_DELETE_PROFILE);

    CreateControl(L"BUTTON", L"配置详情", BS_GROUPBOX, 0, 370, 84, 386, 276, 0);
    CreateText(L"名称", 388, 119, 70, 22);
    CreateControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 462, 114, 276, 28, IDC_PROFILE_NAME);
    CreateText(L"回报率", 388, 157, 70, 22);
    HWND pollCombo = CreateControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 462, 152, 276, 160, IDC_POLLING_RATE);
    const int rates[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
    for (int rate : rates) {
        WCHAR label[32] = {0};
        StringCchPrintfW(label, ARRAYSIZE(label), L"%d Hz", rate);
        int index = (int)SendMessageW(pollCombo, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(pollCombo, CB_SETITEMDATA, index, rate);
    }

    CreateText(L"休眠时间", 388, 195, 70, 22);
    HWND sleepCombo = CreateControl(
        L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL | ES_NUMBER,
        WS_EX_CLIENTEDGE, 462, 190, 276, 160, IDC_SLEEP_TIME
    );
    const int sleeps[] = { 2, 5, 10, 15, 30, 60, 90, 120 };
    for (int minutes : sleeps) {
        WCHAR label[32] = {0};
        StringCchPrintfW(label, ARRAYSIZE(label), L"%d", minutes);
        SendMessageW(sleepCombo, CB_ADDSTRING, 0, (LPARAM)label);
    }

    CreateControl(
        L"BUTTON", L"设为默认配置（离开关联程序后恢复）",
        WS_TABSTOP | BS_AUTOCHECKBOX, 0, 388, 229, 350, 24, IDC_DEFAULT_PROFILE
    );
    CreateText(L"默认配置是自动切换结束后的回退配置。", 388, 258, 350, 20);

    CreateControl(L"BUTTON", L"前台程序自动切换", BS_GROUPBOX, 0, 24, 356, 732, 130, 0);
    CreateText(L"关联程序", 40, 382, 100, 22);
    CreateControl(
        L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY,
        WS_EX_CLIENTEDGE, 40, 406, 552, 28, IDC_APP_PATH
    );
    CreateButton(L"浏览…", 600, 405, 84, 30, IDC_BROWSE_APP);
    CreateButton(L"清除", 692, 405, 48, 30, IDC_CLEAR_APP);
    CreateText(
        L"当该程序处于前台（含无边框 / 独占全屏）时应用此配置；切到其他程序后恢复默认配置。",
        40, 447, 700, 22
    );

    CreateControl(L"BUTTON", L"全局选项", BS_GROUPBOX, 0, 24, 494, 732, 72, 0);
    CreateControl(
        L"BUTTON", L"启用前台程序自动切换",
        WS_TABSTOP | BS_AUTOCHECKBOX, 0, 40, 518, 220, 24, IDC_AUTO_SWITCH
    );
    CreateControl(
        L"BUTTON", L"显示悬浮窗提示 (OSD)",
        WS_TABSTOP | BS_AUTOCHECKBOX, 0, 278, 518, 220, 24, IDC_OSD_ENABLED
    );
    CreateText(L"建议为游戏创建独立配置并设置 2000 / 4000 Hz。", 40, 540, 650, 20);

    g_hStatus = CreateText(L"", 24, 580, 500, 24, IDC_STATUS);
    HWND saveButton = CreateButton(L"保存并应用", 550, 574, 110, 32, IDC_SAVE);
    SendMessageW(saveButton, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
    CreateButton(L"关闭", 670, 574, 86, 32, IDC_CLOSE);

    ApplyFonts();
    SendMessageW(g_hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    SendMessageW(g_hSubtitle, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    CheckDlgButton(g_hwnd, IDC_AUTO_SWITCH, Profiles::AutoSwitchEnabled() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hwnd, IDC_OSD_ENABLED, Osd::IsEnabled() ? BST_CHECKED : BST_UNCHECKED);
    EnableClearButton(false);
}

void OnProfileSelectionChanged() {
    if (g_refreshingList) return;
    int id = SelectedProfileId();
    if (id == 0 || id == g_selectedId) return;

    int previousId = g_selectedId;
    if (!CommitEditor(true)) {
        RefreshList(previousId);
        LoadEditor(previousId);
        return;
    }
    LoadEditor(id);
    SetStatus(L"选择配置后可直接修改；点击“保存并应用”生效", true);
}

LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hWnd;
            g_dpi = (int)GetDpiForHwnd(hWnd);
            g_fontNormal = MakeFont(13, FW_NORMAL);
            g_fontBold = MakeFont(13, FW_SEMIBOLD);
            g_fontTitle = MakeFont(20, FW_BOLD);
            g_fontSmall = MakeFont(11, FW_NORMAL);
            CreateControls();

            DragAcceptFiles(hWnd, TRUE);

            RefreshList(Profiles::ActiveId());
            LoadEditor(Profiles::ActiveId());
            SetStatus(L"选择配置进行编辑，或新增一个配置", true);
            return 0;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            if (id == IDC_ADD_PROFILE && notification == BN_CLICKED) {
                AddProfile();
                return 0;
            }
            if (id == IDC_DELETE_PROFILE && notification == BN_CLICKED) {
                DeleteProfile();
                return 0;
            }
            if (id == IDC_BROWSE_APP && notification == BN_CLICKED) {
                BrowseApp();
                return 0;
            }
            if (id == IDC_CLEAR_APP && notification == BN_CLICKED) {
                ClearApp();
                return 0;
            }
            if (id == IDC_SAVE && notification == BN_CLICKED) {
                g_dirty = true;
                CommitEditor(true);
                return 0;
            }
            if ((id == IDC_CLOSE && notification == BN_CLICKED) || id == IDCANCEL) {
                SendMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (id == IDC_AUTO_SWITCH && notification == BN_CLICKED) {
                Profiles::SetAutoSwitchEnabled(IsDlgButtonChecked(hWnd, IDC_AUTO_SWITCH) == BST_CHECKED);
                SetStatus(L"自动切换设置已更新", true);
                return 0;
            }
            if (id == IDC_OSD_ENABLED && notification == BN_CLICKED) {
                Osd::SetEnabled(IsDlgButtonChecked(hWnd, IDC_OSD_ENABLED) == BST_CHECKED);
                SetStatus(L"悬浮窗设置已更新", true);
                return 0;
            }
            if ((id == IDC_PROFILE_NAME && notification == EN_CHANGE) ||
                (id == IDC_POLLING_RATE && notification == CBN_SELCHANGE) ||
                (id == IDC_SLEEP_TIME && notification == CBN_EDITCHANGE) ||
                (id == IDC_SLEEP_TIME && notification == CBN_SELCHANGE) ||
                (id == IDC_DEFAULT_PROFILE && notification == BN_CLICKED)) {
                MarkDirty();
                return 0;
            }
            break;
        }

        case WM_NOTIFY: {
            LPNMHDR header = (LPNMHDR)lParam;
            if (header && header->idFrom == IDC_PROFILE_LIST && header->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW change = (LPNMLISTVIEW)lParam;
                if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED)) {
                    OnProfileSelectionChanged();
                }
            }
            break;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wParam;
            WCHAR path[32768] = {0};
            if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) > 0) {
                SetText(GetDlgItem(hWnd, IDC_APP_PATH), path);
                EnableClearButton(true);
                MarkDirty();
            }
            DragFinish(drop);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            HWND control = (HWND)lParam;
            SetBkMode(dc, TRANSPARENT);
            if (control == g_hSubtitle) {
                SetTextColor(dc, RGB(96, 105, 120));
            } else if (control == g_hStatus && !g_statusGood) {
                SetTextColor(dc, RGB(190, 48, 48));
            } else {
                SetTextColor(dc, RGB(28, 32, 40));
            }
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_CLOSE: {
            if (!CommitEditor(true)) return 0;
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_DESTROY: {
            if (g_fontNormal) DeleteObject(g_fontNormal);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            g_fontNormal = g_fontBold = g_fontTitle = g_fontSmall = NULL;
            g_hwnd = NULL;
            g_hProfileList = NULL;
            g_hTitle = g_hSubtitle = g_hStatus = NULL;
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

void Show(HWND owner) {
    Profiles::Initialize();
    if (g_hwnd && IsWindow(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwnd);
        return;
    }

    g_hOwner = owner;
    g_dpi = (int)GetSystemDpiSafe();
    INITCOMMONCONTROLSEX common = { sizeof(common), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&common);

    if (!g_classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = (HINSTANCE)GetModuleHandleW(NULL);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = WINDOW_CLASS;
        if (!RegisterClassExW(&wc)) return;
        g_classRegistered = true;
    }

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_APPWINDOW;
    RECT rc = { 0, 0, S(WINDOW_WIDTH), S(WINDOW_HEIGHT) };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    RECT work = {0};
    if (owner && IsWindow(owner)) {
        HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(monitor, &mi)) work = mi.rcWork;
    }
    if (IsRectEmpty(&work)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;

    HWND hwnd = CreateWindowExW(
        exStyle,
        WINDOW_CLASS,
        L"配置与自动化 - rapoo-tray",
        style,
        x, y, width, height,
        owner, NULL, (HINSTANCE)GetModuleHandleW(NULL), NULL
    );
    if (hwnd) {
        g_hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(GetDlgItem(hwnd, IDC_PROFILE_NAME));
    }
}

bool PreTranslateMessage(MSG* msg) {
    if (!g_hwnd || !IsWindow(g_hwnd) || !msg) return false;
    if (msg->hwnd == g_hwnd || IsChild(g_hwnd, msg->hwnd)) {
        return IsDialogMessageW(g_hwnd, msg) != FALSE;
    }
    return false;
}

void Cleanup() {
    if (g_hwnd && IsWindow(g_hwnd)) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
}

} // namespace Settings
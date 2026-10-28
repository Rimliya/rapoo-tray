using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Input;
using System.Windows.Resources;
using System.Windows.Media;
using System.Windows.Threading;
using Wpf.Ui.Appearance;
using RapooTray.Settings.Controls;
using NumberBoxValueChangedEventArgs = Wpf.Ui.Controls.NumberBoxValueChangedEventArgs;

namespace RapooTray.Settings
{
    public partial class MainWindow
    {
        private readonly TrayBridge _bridge;
        private readonly int _initialPage;
        private readonly DispatcherTimer _liveTimer;
        private HwndSource _source;
        private AppState _state;
        private DpiState _dpi;
        private bool _initialized;
        private bool _loading;
        private bool _switchingPage;
        private bool _busy;
        private bool _profileDirty;
        private bool _dpiDirty;
        private bool _dpiLoaded;
        private bool _rfRefreshInFlight;
        private bool _capturingHotkey;
        private bool _batteryLoading;
        private bool _osdDirty;
        private bool _batteryDirty;
        private bool _systemThemeWatching;
        private int _selectedProfileId;
        private int _profilePerfMode = 1;
        private int _selectedDpiIndex;
        private int _batteryHistoryDays = 1;
        private int _hotkeyModifiers = 3;
        private int _hotkeyVirtualKey = 77;
        private DateTime _batteryLoadedAt = DateTime.MinValue;
        private bool _updateAvailable;
        private bool? _dirtyVisualState;
        private string _modelSignature = "";
        private string _releaseUrl = "https://github.com/Rimliya/rapoo-tray/releases/latest";
        private static readonly int[] PollRates = { 125, 250, 500, 1000, 2000, 4000, 8000 };
        private static readonly HttpClient Http = CreateHttpClient();

        public MainWindow(int page, bool demo)
        {
            InitializeComponent();
            int savedTheme = ReadThemePreference();
            if (savedTheme == 1 || savedTheme == 2)
            {
                ApplicationThemeManager.Apply(savedTheme == 2 ? ApplicationTheme.Dark : ApplicationTheme.Light);
            }
            else
            {
                SystemThemeWatcher.Watch(this);
                _systemThemeWatching = true;
            }
            ApplicationThemeManager.Changed += OnApplicationThemeChanged;
            UpdateThemeToggle();
            _initialPage = Math.Max(0, Math.Min(5, page));
            _bridge = new TrayBridge(demo);
            _liveTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
            _liveTimer.Tick += LiveTimer_Tick;
            Loaded += MainWindow_Loaded;
            SourceInitialized += MainWindow_SourceInitialized;
            PreviewKeyDown += MainWindow_PreviewKeyDown;
            Closed += (_, __) =>
            {
                _liveTimer.Stop();
                ApplicationThemeManager.Changed -= OnApplicationThemeChanged;
                if (_source != null) _source.RemoveHook(WndProc);
            };
            UpdateDirtyVisuals();
        }

        private void OnApplicationThemeChanged(ApplicationTheme theme, Color accent)
        {
            UpdateThemeToggle();
            BatteryHistoryChart?.InvalidateVisual();
        }

        private void UpdateThemeToggle()
        {
            bool dark = ApplicationThemeManager.GetAppTheme() == ApplicationTheme.Dark;
            string label = dark ? "浅色" : "深色";
            if (!string.Equals(Convert.ToString(ThemeToggleButton.Content), label, StringComparison.Ordinal))
            {
                ThemeToggleButton.Content = label;
                EfMotion.Nudge(ThemeToggleButton, 1.5);
            }
            ThemeToggleButton.ToolTip = dark ? "切换到浅色模式" : "切换到深色模式";
        }

        private void ThemeToggle_Click(object sender, RoutedEventArgs e)
        {
            if (_systemThemeWatching && IsLoaded)
            {
                SystemThemeWatcher.UnWatch(this);
                _systemThemeWatching = false;
            }

            bool dark = ApplicationThemeManager.GetAppTheme() == ApplicationTheme.Dark;
            ApplicationTheme target = dark ? ApplicationTheme.Light : ApplicationTheme.Dark;
            ApplicationThemeManager.Apply(target);
            SaveThemePreference(target == ApplicationTheme.Dark ? 2 : 1);
            UpdateThemeToggle();
        }

        private static int ReadThemePreference()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(@"Software\rapoo-tray\Settings"))
                {
                    object value = key == null ? null : key.GetValue("UiTheme");
                    return value == null ? 0 : Convert.ToInt32(value, CultureInfo.InvariantCulture);
                }
            }
            catch
            {
                return 0;
            }
        }

        private static void SaveThemePreference(int value)
        {
            try
            {
                using (var key = Registry.CurrentUser.CreateSubKey(@"Software\rapoo-tray\Settings"))
                {
                    if (key != null) key.SetValue("UiTheme", value, RegistryValueKind.DWord);
                }
            }
            catch
            {
            }
        }

        private static HttpClient CreateHttpClient()
        {
            var client = new HttpClient { Timeout = TimeSpan.FromSeconds(12) };
            client.DefaultRequestHeaders.UserAgent.ParseAdd("rapoo-tray-settings/1.14.1");
            return client;
        }

        private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            _initialized = true;
            var ignored = Dispatcher.BeginInvoke(new Action(AnimateShellEntrance), DispatcherPriority.Loaded);
            await ReloadStateAsync(true);
            ActivatePage(_initialPage, true);
            _liveTimer.Start();
        }

        private void AnimateShellEntrance()
        {
            EfMotion.Reveal(NavRail, -10, 0, 0);
            EfMotion.Reveal(HeaderPanel, 0, 9, 45);
            EfMotion.Reveal(HeaderCommandCluster, 8, 0, 90);
            EfMotion.Reveal(FooterBar, 0, 3, 120);
            EfMotion.ScanOnce(HeaderScan, HeaderAccentTrack.ActualWidth, false);
        }

        private void MainWindow_SourceInitialized(object sender, EventArgs e)
        {
            _source = HwndSource.FromHwnd(new WindowInteropHelper(this).Handle);
            if (_source != null) _source.AddHook(WndProc);
        }

        private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (msg == App.ActivateMessageValue)
            {
                ActivatePage(wParam.ToInt32(), true);
                if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
                Show();
                Activate();
                handled = true;
            }
            return IntPtr.Zero;
        }

        private async Task ReloadStateAsync(bool full)
        {
            if (_busy) return;
            SetBusy(true, "正在读取托盘状态…");
            try
            {
                var state = await _bridge.GetStateAsync();
                ApplyState(state, full);
                SetStatus(state.StatusGood ? "已读取设备状态" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex)
            {
                SetStatus(ex.Message, false);
            }
            finally
            {
                SetBusy(false);
            }
        }

        private async void LiveTimer_Tick(object sender, EventArgs e)
        {
            if (_busy || _loading || !_initialized) return;
            try
            {
                var state = await _bridge.GetStateAsync();
                ApplyLiveState(state);
                if (BatteryPage.Visibility == Visibility.Visible &&
                    (DateTime.UtcNow - _batteryLoadedAt).TotalSeconds >= 10)
                {
                    await LoadBatteryHistoryAsync(false);
                }
            }
            catch
            {
                // Keep the editor usable while the tray process is restarting.
            }
        }

        private void ApplyState(AppState state, bool refreshProfiles)
        {
            _loading = true;
            try
            {
                _state = state;
                ApplyLiveState(state);
                if (refreshProfiles)
                {
                    ProfilesList.ItemsSource = state.Profiles;
                    ProfileCountText.Text = state.Profiles.Count.ToString(CultureInfo.InvariantCulture);
                    int target = state.CreatedProfileId > 0 ? state.CreatedProfileId : _selectedProfileId;
                    if (target == 0 || state.Profiles.All(x => x.Id != target)) target = state.ActiveProfileId;
                    var selected = state.Profiles.FirstOrDefault(x => x.Id == target) ?? state.Profiles.FirstOrDefault();
                    ProfilesList.SelectedItem = selected;
                    _selectedProfileId = selected == null ? 0 : selected.Id;
                    if (selected != null) LoadProfileEditor(selected);
                }
                else
                {
                    ModelCombo.ItemsSource = state.Models;
                    ModelCombo.SelectedItem = state.Models.FirstOrDefault(x => x.Index == state.ModelIndex);
                }
                SetStatus(state.StatusText, state.StatusGood);
            }
            finally
            {
                _loading = false;
            }
        }

        private void ApplyLiveState(AppState state)
        {
            _state = state;
            SetReadout(HeaderConnectionText, state.Connected ? "已连接 │ " + state.Battery + "%" : "等待设备");
            SetReadout(HeaderModeText, state.DeviceMode);
            SetReadout(HeaderDpiText, state.DeviceDpiX + " DPI");
            HeaderStatusDot.Fill = (Brush)FindResource(state.Connected ? "AppSignalBrush" : "AppSecondaryTextBrush");
            RailDeviceDot.Fill = HeaderStatusDot.Fill;
            SetReadout(CurrentModelText, state.DeviceName);
            SetReadout(CurrentModeText, state.DeviceMode);
            SetReadout(CurrentBatteryText, state.Battery + "%");
            SetReadout(CurrentDpiText, state.DeviceDpiX == state.DeviceDpiY ? state.DeviceDpiX.ToString() : state.DeviceDpiX + " × " + state.DeviceDpiY);

            bool oldLoading = _loading;
            _loading = true;
            AutoSwitchToggle.IsChecked = state.AutoSwitch;
            AutoRunToggle.IsChecked = state.AutoRun;
            RfFullToggle.IsChecked = state.RfFull;
            if (!_osdDirty)
            {
                OsdToggle.IsChecked = state.OsdEnabled;
                QuickPanelToggle.IsChecked = state.QuickPanelEnabled;
                if (!_capturingHotkey)
                {
                    _hotkeyModifiers = state.HotkeyModifiers;
                    _hotkeyVirtualKey = state.HotkeyVirtualKey;
                    HotkeyValueText.Text = FormatHotkey(_hotkeyModifiers, _hotkeyVirtualKey);
                    HotkeyStatusText.Text = state.HotkeyRegistered
                        ? "快捷键已注册，可在任意应用上方呼出快速面板"
                        : "快捷键当前不可用，请更换组合键";
                }
                SetDisplayFieldChecks(state.OsdDisplayFields);
                SetQuickPanelItemChecks(state.QuickPanelItems);
            }
            if (!_batteryDirty)
            {
                BatteryAlertToggle.IsChecked = state.BatteryAlertsEnabled;
                ChargeReminderToggle.IsChecked = state.ChargingReminderEnabled;
                WarningThresholdBox.Value = state.BatteryWarningThreshold;
                CriticalThresholdBox.Value = state.BatteryCriticalThreshold;
                ReminderMinutesBox.Value = state.ChargingReminderMinutes;
            }
            BatteryPagePercentText.Text = state.Battery + "%";
            BatteryPageModeText.Text = !state.Connected ? "设备未连接"
                : state.BatteryAlertsEnabled && state.Battery <= state.BatteryWarningThreshold
                    ? "低电量 · 建议充电"
                    : state.DeviceMode;
            RfFullToggle.IsEnabled = state.RfAvailable && !_busy;
            RfStateText.Text = state.RfAvailable
                ? (state.RfFull ? "满格射频" : "智能调节（推荐）")
                : (state.Connected ? "等待读取" : "设备未连接");
            RefreshRfButton.Visibility = state.RfAvailable ? Visibility.Collapsed : Visibility.Visible;
            RefreshRfButton.IsEnabled = state.Connected && !_rfRefreshInFlight;
            string modelSignature = string.Join("|", state.Models.Select(x => x.Key));
            if (modelSignature != _modelSignature)
            {
                _modelSignature = modelSignature;
                ModelCombo.ItemsSource = state.Models;
            }
            var modelItems = ModelCombo.ItemsSource as ObservableCollection<ModelOption>;
            var model = modelItems == null ? null : modelItems.FirstOrDefault(x => x.Index == state.ModelIndex);
            if (model != null) ModelCombo.SelectedItem = model;
            _loading = oldLoading;

            AboutVersionText.Text = "版本 " + state.Version;
        }

        private void SetStatus(string text, bool good)
        {
            string value = string.IsNullOrWhiteSpace(text) ? "就绪" : text;
            if (!string.Equals(StatusText.Text, value, StringComparison.Ordinal))
            {
                StatusText.Text = value;
                EfMotion.ReadoutSettle(StatusText);
            }
            StatusDot.Fill = (Brush)FindResource(good ? "AppTraceBrush" : "AppSignalBrush");
            EfMotion.Pulse(StatusDot);
        }

        private void SetBusy(bool busy, string text = null)
        {
            _busy = busy;
            AddProfileButton.IsEnabled = !busy;
            DeleteProfileButton.IsEnabled = !busy && ProfilesList.Items.Count > 1;
            ImportProfilesButton.IsEnabled = !busy;
            ExportProfilesButton.IsEnabled = !busy && _state != null && _state.Profiles.Count > 0;
            SaveProfileButton.IsEnabled = !busy && _profileDirty;
            ApplyProfileButton.IsEnabled = !busy && !_profileDirty && _selectedProfileId != 0;
            AddDpiButton.IsEnabled = !busy && (_dpi == null || _dpi.Slots.Count < Math.Min(7, _dpi.MaxSlots));
            DeleteDpiButton.IsEnabled = !busy && _dpi != null && _dpi.Slots.Count > 1;
            ApplyDpiButton.IsEnabled = !busy && _dpi != null && _dpiDirty;
            OsdSaveButton.IsEnabled = !busy;
            BatterySaveButton.IsEnabled = !busy;
            QuickPanelPreviewButton.IsEnabled = !busy;
            HotkeyCaptureButton.IsEnabled = !busy;
            HotkeyResetButton.IsEnabled = !busy;
            if (!string.IsNullOrEmpty(text)) SetStatus(text, true);
            Cursor = busy ? System.Windows.Input.Cursors.Wait : System.Windows.Input.Cursors.Arrow;
            EfMotion.SetBusyScan(BusyScan, BusyTrack.ActualWidth, busy);
        }

        private void ActivatePage(int page, bool force)
        {
            if (!force && !_initialized) return;
            page = Math.Max(0, Math.Min(5, page));
            _switchingPage = true;
            NavProfile.IsChecked = page == 0;
            NavAutomation.IsChecked = page == 1;
            NavOsd.IsChecked = page == 2;
            NavBattery.IsChecked = page == 3;
            NavDpi.IsChecked = page == 4;
            NavAbout.IsChecked = page == 5;
            _switchingPage = false;

            ProfilePage.Visibility = page == 0 ? Visibility.Visible : Visibility.Collapsed;
            AutomationPage.Visibility = page == 1 ? Visibility.Visible : Visibility.Collapsed;
            OsdPage.Visibility = page == 2 ? Visibility.Visible : Visibility.Collapsed;
            BatteryPage.Visibility = page == 3 ? Visibility.Visible : Visibility.Collapsed;
            DpiPage.Visibility = page == 4 ? Visibility.Visible : Visibility.Collapsed;
            AboutPage.Visibility = page == 5 ? Visibility.Visible : Visibility.Collapsed;

            if (page == 0)
            {
                PageEyebrow.Text = "// PROFILES";
                PageTitle.Text = "配置档案";
                PageSubtitle.Text = "为不同应用保存回报率、性能模式、DPI 与启动规则";
            }
            else if (page == 1)
            {
                PageEyebrow.Text = "// AUTOMATION";
                PageTitle.Text = "自动化";
                PageSubtitle.Text = "管理前台联动、射频策略与设备续航估算";
                if (_state != null && _state.Connected) { var ignored = RefreshRfStateAsync(); }
            }
            else if (page == 2)
            {
                PageEyebrow.Text = "// OSD";
                PageTitle.Text = "OSD 快速面板";
                PageSubtitle.Text = "配置全局快捷键、状态提示字段与快速切换项目";
            }
            else if (page == 3)
            {
                PageEyebrow.Text = "// BATTERY";
                PageTitle.Text = "电量历史与提醒";
                PageSubtitle.Text = "查看电量曲线，配置低电量通知与循环充电提醒";
                var ignored = LoadBatteryHistoryAsync(true);
            }
            else if (page == 4)
            {
                PageEyebrow.Text = "// DPI";
                PageTitle.Text = "DPI 档位";
                PageSubtitle.Text = "编辑鼠标内建 DPI 档位并安全写入设备";
                if (!_dpiLoaded) { var ignored = LoadDpiAsync(); }
            }
            else
            {
                PageEyebrow.Text = "// ABOUT";
                PageTitle.Text = "关于";
                PageSubtitle.Text = "版本信息、项目源码与更新检查";
            }

            FrameworkElement pageElement = page == 0 ? ProfilePage
                : page == 1 ? (FrameworkElement)AutomationPage
                : page == 2 ? OsdPage
                : page == 3 ? BatteryPage
                : page == 4 ? DpiPage
                : AboutPage;
            EfMotion.RevealPage(pageElement, PageEyebrow, PageTitle, PageSubtitle);
            EfMotion.Nudge(HeaderCommandCluster, 2);
        }

        private void Nav_Checked(object sender, RoutedEventArgs e)
        {
            if (!_initialized || _switchingPage) return;
            var button = sender as RadioButton;
            int page;
            if (button != null && int.TryParse(Convert.ToString(button.Tag), out page)) ActivatePage(page, false);
        }

        private void ProfilesList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_loading || _busy) return;
            var row = ProfilesList.SelectedItem as ProfileRow;
            if (row == null || row.Id == _selectedProfileId) return;

            if (_profileDirty)
            {
                var result = MessageBox.Show("当前配置有未保存修改。放弃修改并切换配置？", "rapoo-tray", MessageBoxButton.YesNo, MessageBoxImage.Question);
                if (result != MessageBoxResult.Yes)
                {
                    _loading = true;
                    ProfilesList.SelectedItem = _state.Profiles.FirstOrDefault(x => x.Id == _selectedProfileId);
                    _loading = false;
                    return;
                }
            }

            _selectedProfileId = row.Id;
            LoadProfileEditor(row);
            SetStatus("已选择配置；保存只写入本机配置，点击“应用”才会下发到鼠标", true);
        }

        private void LoadProfileEditor(ProfileRow row)
        {
            bool oldLoading = _loading;
            _loading = true;
            try
            {
                _selectedProfileId = row.Id;
                ProfileNameBox.Text = row.Name;
                SelectPolling(row.PollingHz);
                SleepBox.Value = row.SleepMinutes;
                ProfileDpiXBox.Minimum = 10;
                ProfileDpiXBox.Maximum = 60000;
                ProfileDpiYBox.Minimum = 10;
                ProfileDpiYBox.Maximum = 60000;
                ProfileDpiXBox.Value = row.DpiX > 0 ? row.DpiX : Math.Max(10, _state.DeviceDpiX);
                ProfileDpiYBox.Value = row.DpiY > 0 ? row.DpiY : Math.Max(10, _state.DeviceDpiY);
                AppPathBox.Text = row.AppPath ?? "";
                DefaultProfileCheck.IsChecked = row.IsDefault;
                _profilePerfMode = row.PerformanceMode;
                UpdatePerfAvailability();
                SetPerfMode(_profilePerfMode);
                _profileDirty = false;
                UpdateDirtyVisuals();
            }
            finally
            {
                _loading = oldLoading;
            }
        }

        private void SelectPolling(int hz)
        {
            foreach (ComboBoxItem item in PollingCombo.Items)
            {
                int value;
                if (item.Tag != null && int.TryParse(item.Tag.ToString(), out value) && value == hz)
                {
                    PollingCombo.SelectedItem = item;
                    return;
                }
            }
        }

        private int SelectedPolling()
        {
            var item = PollingCombo.SelectedItem as ComboBoxItem;
            int value;
            return item != null && item.Tag != null && int.TryParse(item.Tag.ToString(), out value) ? value : 1000;
        }

        private void MarkProfileDirty()
        {
            if (_loading || _busy) return;
            _profileDirty = true;
            UpdateDirtyVisuals();
            SetStatus("有未保存的更改", true);
        }

        private void UpdateDirtyVisuals()
        {
            ProfileDirtyText.Text = _profileDirty ? "未保存" : "已保存";
            if (_dirtyVisualState != _profileDirty)
            {
                _dirtyVisualState = _profileDirty;
                EfMotion.Nudge(ProfileDirtyChip, _profileDirty ? 3 : 1.5);
                EfMotion.Pulse(ProfileDirtyChip);
            }
            ProfileDirtyChip.Background = _profileDirty ? (Brush)FindResource("AppSignalBrush") : Brushes.Transparent;
            ProfileDirtyChip.BorderBrush = (Brush)FindResource(_profileDirty ? "AppSignalBrush" : "AppRuleBrush");
            ProfileDirtyText.Foreground = (Brush)FindResource(_profileDirty ? "AppOnSignalBrush" : "AppSecondaryTextBrush");
            SaveProfileButton.IsEnabled = !_busy && _profileDirty;
            ApplyProfileButton.IsEnabled = !_busy && !_profileDirty && _selectedProfileId != 0;
        }

        private void ProfileField_Changed(object sender, RoutedEventArgs e) { MarkProfileDirty(); }
        private void SleepBox_ValueChanged(object sender, NumberBoxValueChangedEventArgs e) { MarkProfileDirty(); }
        private void ProfileDpi_ValueChanged(object sender, NumberBoxValueChangedEventArgs e) { MarkProfileDirty(); }

        private void PollingCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_loading) return;
            UpdatePerfAvailability();
            if (!IsPerfModeAvailable(_profilePerfMode)) SetPerfMode(NearestAvailableMode(_profilePerfMode));
            MarkProfileDirty();
        }

        private static bool ModeAllowedForRate(int mode, int hz)
        {
            if (hz <= 250) return mode <= 1;
            if (hz == 500) return mode <= 2;
            if (hz == 1000) return mode >= 1;
            if (hz == 2000) return mode >= 2;
            return mode >= 3;
        }

        private bool IsPerfModeAvailable(int mode) => ModeAllowedForRate(mode, SelectedPolling());

        private int NearestAvailableMode(int desired)
        {
            int best = 0;
            int distance = int.MaxValue;
            for (int mode = 0; mode <= 5; mode++)
            {
                if (!IsPerfModeAvailable(mode)) continue;
                int d = Math.Abs(mode - desired);
                if (d < distance) { distance = d; best = mode; }
            }
            return best;
        }

        private void UpdatePerfAvailability()
        {
            int count = 0;
            foreach (var item in new[] { PerfFury, PerfGamingHyper, PerfHyper, PerfFire, PerfBalanced, PerfOffice })
            {
                int mode;
                int.TryParse(Convert.ToString(item.Tag), out mode);
                bool available = IsPerfModeAvailable(mode);
                item.IsEnabled = available;
                if (available) count++;
            }
            PerfAvailableText.Text = "当前回报率可用 " + count + " 档";
        }

        private void SetPerfMode(int mode)
        {
            RadioButton target = mode == 5 ? PerfFury : mode == 4 ? PerfGamingHyper : mode == 3 ? PerfHyper : mode == 2 ? PerfFire : mode == 1 ? PerfBalanced : PerfOffice;
            target.IsChecked = true;
            _profilePerfMode = mode;
        }

        private void PerfMode_Checked(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            int mode;
            if (int.TryParse(Convert.ToString(((RadioButton)sender).Tag), out mode))
            {
                _profilePerfMode = mode;
                MarkProfileDirty();
            }
        }

        private async void SaveProfile_Click(object sender, RoutedEventArgs e)
        {
            if (_busy || _selectedProfileId == 0) return;
            var row = new ProfileRow
            {
                Id = _selectedProfileId,
                Name = (ProfileNameBox.Text ?? "").Trim(),
                PollingHz = SelectedPolling(),
                PerformanceMode = _profilePerfMode,
                SleepMinutes = (int)Math.Round(SleepBox.Value ?? 10),
                DpiX = (int)Math.Round(ProfileDpiXBox.Value ?? 800),
                DpiY = (int)Math.Round(ProfileDpiYBox.Value ?? 800),
                AppPath = (AppPathBox.Text ?? "").Trim(),
                IsDefault = DefaultProfileCheck.IsChecked == true
            };
            if (row.Name.Length == 0)
            {
                SetStatus("配置名称不能为空", false);
                return;
            }

            SetBusy(true, "正在保存配置…");
            try
            {
                var state = await _bridge.SaveProfileAsync(row, row.IsDefault);
                ApplyState(state, true);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "配置已保存；点击“应用”后写入鼠标" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private async void AddProfile_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            if (_profileDirty && System.Windows.MessageBox.Show("当前配置有未保存修改。放弃修改并新建配置？", "rapoo-tray", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
            SetBusy(true, "正在新增配置…");
            try
            {
                var state = await _bridge.AddProfileAsync();
                ApplyState(state, true);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "已新增配置，请修改名称和参数" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private async void DeleteProfile_Click(object sender, RoutedEventArgs e)
        {
            if (_busy || _selectedProfileId == 0) return;
            var row = _state == null ? null : _state.Profiles.FirstOrDefault(x => x.Id == _selectedProfileId);
            if (row == null) return;
            if (System.Windows.MessageBox.Show("确定删除配置“" + row.Name + "”吗？", "删除配置", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
            SetBusy(true, "正在删除配置…");
            try
            {
                var state = await _bridge.DeleteProfileAsync(_selectedProfileId);
                ApplyState(state, true);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "配置已删除" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private void ExportProfiles_Click(object sender, RoutedEventArgs e)
        {
            if (_busy || _state == null) return;
            if (_profileDirty)
            {
                SetStatus("请先保存当前配置，再导出配置包", false);
                MessageBox.Show(this, "当前配置有未保存修改，请先保存后再导出。", "导出配置", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var dialog = new Microsoft.Win32.SaveFileDialog
            {
                Title = "导出 rapoo-tray 配置包",
                Filter = "rapoo-tray 配置包 (*.rapoo)|*.rapoo|JSON 文件 (*.json)|*.json",
                DefaultExt = ".rapoo",
                AddExtension = true,
                FileName = "rapoo-tray-profiles-" + DateTime.Now.ToString("yyyyMMdd-HHmm") + ".rapoo"
            };
            if (dialog.ShowDialog(this) != true) return;

            try
            {
                ProfilePackage package = ProfilePackageFile.FromState(_state);
                string error;
                if (!package.NormalizeAndValidate(out error)) throw new InvalidOperationException(error);
                ProfilePackageFile.SaveAtomic(dialog.FileName, package);
                SetStatus("已导出 " + package.Profiles.Count + " 个配置到 " + Path.GetFileName(dialog.FileName), true);
            }
            catch (Exception ex)
            {
                SetStatus("配置导出失败：" + ex.Message, false);
                MessageBox.Show(this, "配置导出失败：" + Environment.NewLine + ex.Message, "导出配置", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void ImportProfiles_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            if (_profileDirty && MessageBox.Show(this, "当前配置有未保存修改。导入会替换当前配置，是否继续？", "导入配置", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            {
                return;
            }

            var dialog = new Microsoft.Win32.OpenFileDialog
            {
                Title = "导入 rapoo-tray 配置包",
                Filter = "rapoo-tray 配置包 (*.rapoo;*.json)|*.rapoo;*.json|所有文件 (*.*)|*.*",
                CheckFileExists = true,
                Multiselect = false
            };
            if (dialog.ShowDialog(this) != true) return;

            ProfilePackage package;
            try
            {
                package = ProfilePackageFile.Load(dialog.FileName);
            }
            catch (Exception ex)
            {
                SetStatus("配置包无效：" + ex.Message, false);
                MessageBox.Show(this, "无法读取配置包：" + Environment.NewLine + ex.Message, "导入配置", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            int currentCount = _state == null ? 0 : _state.Profiles.Count;
            string confirm = "将使用“" + Path.GetFileName(dialog.FileName) + "”中的 " + package.Profiles.Count +
                             " 个配置替换当前 " + currentCount + " 个配置。" + Environment.NewLine + Environment.NewLine +
                             "导入前会自动备份当前配置，自动切换开关也会随包恢复。是否继续？";
            if (MessageBox.Show(this, confirm, "导入配置", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;

            string backupPath;
            try
            {
                backupPath = CreateImportBackup();
            }
            catch (Exception ex)
            {
                SetStatus("无法创建导入前备份：" + ex.Message, false);
                MessageBox.Show(this, "为避免丢失现有配置，导入已取消。" + Environment.NewLine + Environment.NewLine +
                    "无法创建备份：" + ex.Message, "导入配置", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            SetBusy(true, "正在导入 " + package.Profiles.Count + " 个配置…");
            try
            {
                AppState state = await _bridge.ImportProfilesAsync(package);
                _profileDirty = false;
                _selectedProfileId = 0;
                ApplyState(state, true);
                if (!state.StatusGood)
                {
                    SetStatus(state.StatusText, false);
                    MessageBox.Show(this, state.StatusText, "导入配置", MessageBoxButton.OK, MessageBoxImage.Error);
                }
                else
                {
                    SetStatus("已导入 " + package.Profiles.Count + " 个配置；备份：" + Path.GetFileName(backupPath), true);
                    MessageBox.Show(this, "已导入 " + package.Profiles.Count + " 个配置。" + Environment.NewLine + Environment.NewLine +
                        "导入前备份：" + Environment.NewLine + backupPath + Environment.NewLine + Environment.NewLine +
                        "配置尚未写入鼠标，请选择配置后点击“应用”。", "导入完成", MessageBoxButton.OK, MessageBoxImage.Information);
                }
            }
            catch (Exception ex)
            {
                SetStatus("配置导入失败：" + ex.Message, false);
                MessageBox.Show(this, "配置导入失败：" + Environment.NewLine + ex.Message, "导入配置", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                SetBusy(false);
            }
        }

        private string CreateImportBackup()
        {
            if (_state == null) throw new InvalidOperationException("当前没有可备份的配置状态");
            ProfilePackage package = ProfilePackageFile.FromState(_state);
            string error;
            if (!package.NormalizeAndValidate(out error)) throw new InvalidOperationException(error);
            string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rapoo-tray", "backups");
            string path = Path.Combine(folder, "profiles-before-import-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff") + ".rapoo");
            ProfilePackageFile.SaveAtomic(path, package);
            return path;
        }

        private async void ApplyProfile_Click(object sender, RoutedEventArgs e)
        {
            if (_busy || _selectedProfileId == 0) return;
            if (_profileDirty)
            {
                SetStatus("请先保存修改，再应用配置", false);
                return;
            }
            SetBusy(true, "正在应用配置到鼠标…");
            try
            {
                var state = await _bridge.ApplyProfileAsync(_selectedProfileId);
                ApplyState(state, true);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "配置已应用到鼠标" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private void BrowseApp_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new Microsoft.Win32.OpenFileDialog
            {
                Title = "选择需要自动切换配置的程序",
                Filter = "可执行程序 (*.exe)|*.exe|所有文件 (*.*)|*.*",
                CheckFileExists = true
            };
            if (dialog.ShowDialog(this) == true)
            {
                AppPathBox.Text = dialog.FileName;
                MarkProfileDirty();
            }
        }

        private void ClearApp_Click(object sender, RoutedEventArgs e)
        {
            AppPathBox.Text = "";
            MarkProfileDirty();
        }

        private void CaptureProfileDpi_Click(object sender, RoutedEventArgs e)
        {
            if (_state == null) return;
            ProfileDpiXBox.Value = Math.Max(10, _state.DeviceDpiX);
            ProfileDpiYBox.Value = Math.Max(10, _state.DeviceDpiY > 0 ? _state.DeviceDpiY : _state.DeviceDpiX);
            MarkProfileDirty();
        }

        private async Task RunToggleAsync(Func<bool, Task<AppState>> action, bool value, string status)
        {
            if (_loading || _busy) return;
            SetBusy(true, status);
            try
            {
                var state = await action(value);
                ApplyState(state, false);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? status : state.StatusText, state.StatusGood);
            }
            catch (Exception ex)
            {
                ApplyLiveState(_state);
                SetStatus(ex.Message, false);
            }
            finally { SetBusy(false); }
        }

        private static int ReadCheckMask(params CheckBox[] boxes)
        {
            int mask = 0;
            foreach (CheckBox box in boxes)
            {
                if (box.IsChecked == true)
                {
                    int bit;
                    if (int.TryParse(Convert.ToString(box.Tag, CultureInfo.InvariantCulture), out bit)) mask |= bit;
                }
            }
            return mask;
        }

        private void SetDisplayFieldChecks(int mask)
        {
            DisplayModelCheck.IsChecked = (mask & 1) != 0;
            DisplayModeCheck.IsChecked = (mask & 2) != 0;
            DisplayDpiLevelCheck.IsChecked = (mask & 4) != 0;
            DisplayDpiValueCheck.IsChecked = (mask & 8) != 0;
            DisplayBatteryCheck.IsChecked = (mask & 16) != 0;
            DisplayPollingCheck.IsChecked = (mask & 32) != 0;
            DisplayPerformanceCheck.IsChecked = (mask & 64) != 0;
        }

        private void SetQuickPanelItemChecks(int mask)
        {
            PanelProfilesCheck.IsChecked = (mask & 1) != 0;
            PanelDpiCheck.IsChecked = (mask & 2) != 0;
            PanelPerformanceCheck.IsChecked = (mask & 4) != 0;
            PanelBatteryCheck.IsChecked = (mask & 8) != 0;
            PanelPollingCheck.IsChecked = (mask & 16) != 0;
            PanelDetailsCheck.IsChecked = (mask & 32) != 0;
        }

        private static bool IsFunctionKey(Key key)
        {
            return key >= Key.F1 && key <= Key.F24;
        }

        private static string FormatHotkey(int modifiers, int virtualKey)
        {
            var parts = new List<string>();
            if ((modifiers & 2) != 0) parts.Add("Ctrl");
            if ((modifiers & 1) != 0) parts.Add("Alt");
            if ((modifiers & 4) != 0) parts.Add("Shift");
            if ((modifiers & 8) != 0) parts.Add("Win");
            Key key = KeyInterop.KeyFromVirtualKey(virtualKey);
            if (key >= Key.A && key <= Key.Z) parts.Add(key.ToString());
            else if (key >= Key.D0 && key <= Key.D9) parts.Add(((char)('0' + (key - Key.D0))).ToString());
            else if (key >= Key.NumPad0 && key <= Key.NumPad9) parts.Add("Num " + (key - Key.NumPad0));
            else if (key >= Key.F1 && key <= Key.F24) parts.Add("F" + (key - Key.F1 + 1));
            else
            {
                switch (key)
                {
                    case Key.Space: parts.Add("Space"); break;
                    case Key.Tab: parts.Add("Tab"); break;
                    case Key.Enter: parts.Add("Enter"); break;
                    case Key.Back: parts.Add("Backspace"); break;
                    case Key.Insert: parts.Add("Insert"); break;
                    case Key.Delete: parts.Add("Delete"); break;
                    case Key.Home: parts.Add("Home"); break;
                    case Key.End: parts.Add("End"); break;
                    case Key.PageUp: parts.Add("Page Up"); break;
                    case Key.PageDown: parts.Add("Page Down"); break;
                    case Key.Left: parts.Add("Left"); break;
                    case Key.Right: parts.Add("Right"); break;
                    case Key.Up: parts.Add("Up"); break;
                    case Key.Down: parts.Add("Down"); break;
                    default: parts.Add("VK " + virtualKey.ToString(CultureInfo.InvariantCulture)); break;
                }
            }
            return string.Join(" + ", parts);
        }

        private void CaptureHotkey_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            _capturingHotkey = true;
            HotkeyValueText.Text = "请按下组合键…";
            HotkeyStatusText.Text = "按 Esc 取消；字母/数字/方向键需搭配修饰键";
            HotkeyCaptureButton.Focus();
        }

        private void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (!_capturingHotkey) return;
            e.Handled = true;
            Key key = e.Key == Key.System ? e.SystemKey : e.Key;
            if (key == Key.Escape)
            {
                _capturingHotkey = false;
                HotkeyValueText.Text = FormatHotkey(_hotkeyModifiers, _hotkeyVirtualKey);
                HotkeyStatusText.Text = "已取消快捷键录制";
                return;
            }
            if (key == Key.LeftCtrl || key == Key.RightCtrl || key == Key.LeftAlt || key == Key.RightAlt ||
                key == Key.LeftShift || key == Key.RightShift || key == Key.LWin || key == Key.RWin) return;

            int modifiers = (int)Keyboard.Modifiers;
            int virtualKey = KeyInterop.VirtualKeyFromKey(key);
            if ((modifiers & 15) == 0 && !IsFunctionKey(key))
            {
                HotkeyStatusText.Text = "该组合键无修饰键；请使用 Ctrl / Alt / Shift / Win，或单独使用 F1–F24";
                return;
            }
            if (virtualKey <= 0 || key == Key.None)
            {
                HotkeyStatusText.Text = "该按键不可用作全局快捷键";
                return;
            }
            _capturingHotkey = false;
            _hotkeyModifiers = modifiers & 15;
            _hotkeyVirtualKey = virtualKey;
            _osdDirty = true;
            HotkeyValueText.Text = FormatHotkey(_hotkeyModifiers, _hotkeyVirtualKey);
            HotkeyStatusText.Text = "快捷键已录制；保存 OSD 设置后生效";
            OsdConfigStatusText.Text = "有未保存的 OSD 设置";
        }

        private void ResetHotkey_Click(object sender, RoutedEventArgs e)
        {
            _capturingHotkey = false;
            _hotkeyModifiers = 3;
            _hotkeyVirtualKey = 'M';
            _osdDirty = true;
            HotkeyValueText.Text = "Ctrl + Alt + M";
            HotkeyStatusText.Text = "已恢复默认组合键；保存后重新注册";
            OsdConfigStatusText.Text = "有未保存的 OSD 设置";
        }

        private void OsdOption_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading || !_initialized) return;
            _osdDirty = true;
            OsdConfigStatusText.Text = "有未保存的 OSD 设置";
        }

        private async void SaveOsdSettings_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            int displayFields = ReadCheckMask(DisplayModelCheck, DisplayModeCheck, DisplayDpiLevelCheck,
                DisplayDpiValueCheck, DisplayBatteryCheck, DisplayPollingCheck, DisplayPerformanceCheck);
            int panelItems = ReadCheckMask(PanelProfilesCheck, PanelDpiCheck, PanelPerformanceCheck,
                PanelBatteryCheck, PanelPollingCheck, PanelDetailsCheck);
            SetBusy(true, "正在保存 OSD 设置并重新注册快捷键…");
            try
            {
                AppState state = await _bridge.SetOsdConfigAsync(OsdToggle.IsChecked == true,
                    QuickPanelToggle.IsChecked == true, _hotkeyModifiers, _hotkeyVirtualKey, displayFields, panelItems);
                _osdDirty = false;
                ApplyState(state, false);
                OsdConfigStatusText.Text = state.HotkeyRegistered || !state.QuickPanelEnabled
                    ? "OSD 设置已保存"
                    : "快捷键注册失败：可能已被其他程序占用";
                SetStatus(state.StatusText, state.StatusGood);
            }
            catch (Exception ex)
            {
                SetStatus(ex.Message, false);
            }
            finally { SetBusy(false); }
        }

        private async void PreviewQuickPanel_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            try
            {
                AppState state = await _bridge.ShowQuickPanelAsync();
                SetStatus("已打开快速面板；点击项目即可切换，Esc 关闭", state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
        }

        private async void BatteryRange_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading || !_initialized) return;
            var button = sender as RadioButton;
            int days;
            if (button == null || !int.TryParse(Convert.ToString(button.Tag, CultureInfo.InvariantCulture), out days)) return;
            _batteryHistoryDays = Math.Max(1, Math.Min(30, days));
            await LoadBatteryHistoryAsync(true);
        }

        private void BatteryOption_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading || !_initialized) return;
            _batteryDirty = true;
            BatteryConfigStatusText.Text = "有未保存的提醒设置";
        }

        private void BatteryNumber_Changed(object sender, NumberBoxValueChangedEventArgs e)
        {
            if (_loading || !_initialized) return;
            _batteryDirty = true;
            BatteryConfigStatusText.Text = "有未保存的提醒设置";
        }

        private async Task LoadBatteryHistoryAsync(bool force)
        {
            if (_batteryLoading || _bridge == null) return;
            if (!force && (DateTime.UtcNow - _batteryLoadedAt).TotalSeconds < 10) return;
            _batteryLoading = true;
            try
            {
                BatteryHistoryState history = await _bridge.GetBatteryHistoryAsync(_batteryHistoryDays);
                BatteryHistoryChart.SetSamples(history.Samples, history.Days);
                _batteryLoadedAt = DateTime.UtcNow;
                if (history.Samples.Count == 0)
                {
                    BatteryMinText.Text = "--";
                    BatteryMaxText.Text = "--";
                    BatteryAverageText.Text = "--";
                    BatterySamplesText.Text = "0";
                }
                else
                {
                    BatteryMinText.Text = history.Samples.Min(x => x.Battery) + "%";
                    BatteryMaxText.Text = history.Samples.Max(x => x.Battery) + "%";
                    BatteryAverageText.Text = Math.Round(history.Samples.Average(x => x.Battery)) + "%";
                    BatterySamplesText.Text = history.Samples.Count.ToString(CultureInfo.InvariantCulture);
                }
            }
            catch (Exception ex)
            {
                BatteryConfigStatusText.Text = "电量历史读取失败：" + ex.Message;
            }
            finally { _batteryLoading = false; }
        }

        private async void SaveBatterySettings_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            int warning = (int)Math.Round(WarningThresholdBox.Value ?? 20);
            int critical = (int)Math.Round(CriticalThresholdBox.Value ?? 10);
            int minutes = (int)Math.Round(ReminderMinutesBox.Value ?? 30);
            warning = Math.Max(5, Math.Min(50, warning));
            critical = Math.Max(1, Math.Min(warning - 1, critical));
            minutes = Math.Max(5, Math.Min(180, minutes));
            bool oldLoading = _loading;
            _loading = true;
            WarningThresholdBox.Value = warning;
            CriticalThresholdBox.Value = critical;
            ReminderMinutesBox.Value = minutes;
            _loading = oldLoading;

            SetBusy(true, "正在保存电量提醒设置…");
            try
            {
                AppState state = await _bridge.SetBatteryConfigAsync(BatteryAlertToggle.IsChecked == true,
                    warning, critical, ChargeReminderToggle.IsChecked == true, minutes);
                _batteryDirty = false;
                ApplyState(state, false);
                BatteryConfigStatusText.Text = "提醒设置已保存";
                SetStatus(state.StatusText, state.StatusGood);
            }
            catch (Exception ex)
            {
                BatteryConfigStatusText.Text = "保存失败：" + ex.Message;
                SetStatus(ex.Message, false);
            }
            finally { SetBusy(false); }
        }

        private async void AutoSwitch_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            await RunToggleAsync(_bridge.SetAutoSwitchAsync, AutoSwitchToggle.IsChecked == true, "正在更新前台联动…");
        }

        private async void AutoRun_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            await RunToggleAsync(_bridge.SetAutoRunAsync, AutoRunToggle.IsChecked == true, "正在更新开机自启动…");
        }

        private async void RfFull_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            await RunToggleAsync(_bridge.SetRfFullAsync, RfFullToggle.IsChecked == true, "正在写入射频策略…");
        }

        private async Task RefreshRfStateAsync()
        {
            if (_rfRefreshInFlight || _state == null || !_state.Connected) return;
            _rfRefreshInFlight = true;
            RefreshRfButton.IsEnabled = false;
            RfStateText.Text = "正在读取…";
            try
            {
                var state = await _bridge.RefreshRfAsync();
                ApplyState(state, false);
                SetStatus(state.StatusText, state.StatusGood);
            }
            catch (Exception ex)
            {
                RfStateText.Text = "读取失败，请重试";
                SetStatus(ex.Message, false);
            }
            finally
            {
                _rfRefreshInFlight = false;
                if (_state != null)
                {
                    RefreshRfButton.IsEnabled = _state.Connected;
                    if (!_state.RfAvailable && _state.Connected) RfStateText.Text = "等待读取";
                }
            }
        }

        private async void RefreshRf_Click(object sender, RoutedEventArgs e)
        {
            await RefreshRfStateAsync();
        }

        private async void ModelCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_loading || _busy) return;
            var model = ModelCombo.SelectedItem as ModelOption;
            if (model == null) return;
            SetBusy(true, "正在更新续航估算型号…");
            try
            {
                var state = await _bridge.SetModelAsync(model.Index);
                ApplyState(state, false);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "续航估算型号已更新" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private async Task LoadDpiAsync()
        {
            if (_busy || _dpiLoaded) return;
            SetBusy(true, "正在读取鼠标 DPI 档位…");
            try
            {
                _dpi = await _bridge.GetDpiStateAsync();
                _dpiLoaded = true;
                _selectedDpiIndex = Math.Max(0, Math.Min(_dpi.ActiveIndex, Math.Max(0, _dpi.Slots.Count - 1)));
                DpiSlotsList.ItemsSource = _dpi.Slots;
                SelectDpiSlot(_selectedDpiIndex);
                UpdateDpiEditor();
                _dpiDirty = false;
                DpiStatusText.Text = _dpi.FromDevice ? "已读取鼠标当前 DPI 档位" : "当前显示本机缓存或当前 DPI";
                SetStatus(DpiStatusText.Text, true);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private void SelectDpiSlot(int index)
        {
            if (_dpi == null || _dpi.Slots.Count == 0) return;
            index = Math.Max(0, Math.Min(index, _dpi.Slots.Count - 1));
            bool oldLoading = _loading;
            _loading = true;
            _selectedDpiIndex = index;
            DpiSlotsList.SelectedItem = _dpi.Slots[index];
            _loading = oldLoading;
        }

        private void DpiSlotsList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_loading || _busy || _dpi == null) return;
            var row = DpiSlotsList.SelectedItem as DpiSlotRow;
            if (row == null) return;
            _selectedDpiIndex = row.Index;
            UpdateDpiEditor();
        }

        private void UpdateDpiEditor()
        {
            if (_dpi == null || _dpi.Slots.Count == 0) return;
            var slot = _dpi.Slots[Math.Max(0, Math.Min(_selectedDpiIndex, _dpi.Slots.Count - 1))];
            bool oldLoading = _loading;
            _loading = true;
            try
            {
                DpiXBox.Minimum = _dpi.Min;
                DpiXBox.Maximum = _dpi.Max;
                DpiYBox.Minimum = _dpi.Min;
                DpiYBox.Maximum = _dpi.Max;
                DpiSlider.Minimum = _dpi.Min;
                DpiSlider.Maximum = _dpi.Max;
                DpiSlider.TickFrequency = Math.Max(1, _dpi.Step);
                DpiXBox.Value = slot.X;
                DpiYBox.Value = slot.Y;
                DpiSlider.Value = slot.X;
                DpiIndependentCheck.IsChecked = slot.X != slot.Y;
                DpiYBox.IsEnabled = slot.X != slot.Y;
                DpiPageTitle.Text = "第 " + (_selectedDpiIndex + 1) + " 档";
                DpiPageSubtitle.Text = (_dpi.FromDevice ? "鼠标当前第 " : "当前视图第 ") + (_selectedDpiIndex + 1) + " 档使用 " + (slot.X == slot.Y ? slot.X.ToString() : slot.X + " × " + slot.Y) + " DPI";
                DpiRangeText.Text = _dpi.Min + "–" + _dpi.Max + " DPI │ 步进 " + _dpi.Step + " │ 最多 " + Math.Min(7, _dpi.MaxSlots) + " 档";
                DpiCountText.Text = _dpi.Slots.Count + " / " + Math.Min(7, _dpi.MaxSlots);
            }
            finally { _loading = oldLoading; }
            UpdateDpiButtons();
        }

        private void UpdateDpiButtons()
        {
            if (_dpi == null) return;
            AddDpiButton.IsEnabled = !_busy && _dpi.Slots.Count < Math.Min(7, _dpi.MaxSlots);
            DeleteDpiButton.IsEnabled = !_busy && _dpi.Slots.Count > 1;
            ApplyDpiButton.IsEnabled = !_busy && _dpiDirty;
            DpiStatusText.Text = _dpiDirty ? "DPI 修改尚未写入鼠标" : "当前档位与设备同步";
        }

        private int SnapDpi(double raw)
        {
            if (_dpi == null) return (int)Math.Round(raw);
            int step = Math.Max(1, _dpi.Step);
            int value = (int)Math.Round(raw);
            value = Math.Max(_dpi.Min, Math.Min(_dpi.Max, value));
            int snapped = _dpi.Min + (int)Math.Round((value - _dpi.Min) / (double)step) * step;
            return Math.Max(_dpi.Min, Math.Min(_dpi.Max, snapped));
        }

        private void ApplyEditorsToDpiSlot()
        {
            if (_loading || _busy || _dpi == null || _dpi.Slots.Count == 0) return;
            var slot = _dpi.Slots[Math.Max(0, Math.Min(_selectedDpiIndex, _dpi.Slots.Count - 1))];
            int x = SnapDpi(DpiXBox.Value ?? slot.X);
            int y = DpiIndependentCheck.IsChecked == true ? SnapDpi(DpiYBox.Value ?? slot.Y) : x;
            if (slot.X != x || slot.Y != y)
            {
                slot.X = x;
                slot.Y = y;
                _dpiDirty = true;
                UpdateDpiButtons();
                SetStatus("DPI 修改尚未写入鼠标", true);
            }
        }

        private void DpiValue_Changed(object sender, NumberBoxValueChangedEventArgs e) { ApplyEditorsToDpiSlot(); }

        private void DpiSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e) { ApplyEditorsToDpiSlot(); }

        private void DpiIndependent_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading || _busy) return;
            DpiYBox.IsEnabled = DpiIndependentCheck.IsChecked == true;
            if (DpiIndependentCheck.IsChecked != true) DpiYBox.Value = DpiXBox.Value;
            ApplyEditorsToDpiSlot();
        }

        private void AddDpi_Click(object sender, RoutedEventArgs e)
        {
            if (_busy || _dpi == null || _dpi.Slots.Count >= Math.Min(7, _dpi.MaxSlots)) return;
            int[] candidates = { 400, 800, 1200, 1600, 2400, 3200, 6400, 12800, 30000 };
            int value = SnapDpi(_dpi.Max);
            foreach (int candidate in candidates)
            {
                int snapped = SnapDpi(candidate);
                if (_dpi.Slots.All(x => x.X != snapped)) { value = snapped; break; }
            }
            var slot = new DpiSlotRow { Index = _dpi.Slots.Count, X = value, Y = value, IsActive = false };
            _dpi.Slots.Add(slot);
            _selectedDpiIndex = _dpi.Slots.Count - 1;
            SelectDpiSlot(_selectedDpiIndex);
            UpdateDpiEditor();
            _dpiDirty = true;
            UpdateDpiButtons();
            SetStatus("已添加档位，修改数值后点击“应用并切换”", true);
        }

        private void DeleteDpi_Click(object sender, RoutedEventArgs e)
        {
            if (_busy || _dpi == null || _dpi.Slots.Count <= 1) return;
            bool deletingActive = _dpi.Slots[_selectedDpiIndex].IsActive;
            _dpi.Slots.RemoveAt(_selectedDpiIndex);
            for (int i = 0; i < _dpi.Slots.Count; i++) _dpi.Slots[i].Index = i;
            _selectedDpiIndex = Math.Min(_selectedDpiIndex, _dpi.Slots.Count - 1);
            if (deletingActive)
            {
                for (int i = 0; i < _dpi.Slots.Count; i++) _dpi.Slots[i].IsActive = i == _selectedDpiIndex;
            }
            SelectDpiSlot(_selectedDpiIndex);
            DpiSlotsList.Items.Refresh();
            UpdateDpiEditor();
            _dpiDirty = true;
            UpdateDpiButtons();
            SetStatus("已删除档位，应用后写入鼠标", true);
        }

        private async void ApplyDpi_Click(object sender, RoutedEventArgs e)
        {
            if (_busy || _dpi == null) return;
            ApplyEditorsToDpiSlot();
            SetBusy(true, "正在写入 DPI 档位…");
            try
            {
                var state = await _bridge.ApplyDpiAsync(_dpi, _selectedDpiIndex);
                _dpi = state;
                _dpiLoaded = true;
                _dpiDirty = false;
                DpiSlotsList.ItemsSource = state.Slots;
                _selectedDpiIndex = Math.Max(0, Math.Min(state.ActiveIndex, state.Slots.Count - 1));
                SelectDpiSlot(_selectedDpiIndex);
                UpdateDpiEditor();
                UpdateDpiButtons();
                SetStatus("已写入鼠标，并切换到所选 DPI 档位", true);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private void OpenSource_Click(object sender, RoutedEventArgs e) => OpenUrl("https://github.com/Rimliya/rapoo-tray");

        private void ShowFontLicense_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var uri = new Uri("pack://application:,,,/rapoo-tray-settings;component/Assets/Fonts/HarmonyOS-Sans-LICENSE.txt", UriKind.Absolute);
                StreamResourceInfo resource = Application.GetResourceStream(uri);
                if (resource == null) throw new InvalidOperationException("未找到字体许可资源");
                using (var reader = new StreamReader(resource.Stream))
                {
                    string license = reader.ReadToEnd();
                    int terminator = license.IndexOf('\0');
                    if (terminator >= 0) license = license.Substring(0, terminator);
                    MessageBox.Show(this, license.Trim(), "HarmonyOS Sans Fonts License Agreement", MessageBoxButton.OK, MessageBoxImage.Information);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "无法读取字体许可：" + ex.Message, "rapoo-tray", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        private async void CheckUpdate_Click(object sender, RoutedEventArgs e)
        {
            if (_updateAvailable)
            {
                OpenUrl(_releaseUrl);
                return;
            }
            CheckUpdateButton.IsEnabled = false;
            CheckUpdateButton.Content = "检查中…";
            UpdateStatusText.Text = "正在检查 GitHub 最新发行版…";
            try
            {
                using (var response = await Http.GetAsync("https://github.com/Rimliya/rapoo-tray/releases/latest"))
                {
                    response.EnsureSuccessStatusCode();
                    _releaseUrl = response.RequestMessage.RequestUri.ToString();
                    var tagMatch = Regex.Match(_releaseUrl, "/tag/(v[^/?#]+)");
                    string latest = tagMatch.Success ? tagMatch.Groups[1].Value : "";
                    if (latest.Length == 0) throw new InvalidOperationException("未找到最新版本信息");
                    int versionComparison = CompareVersions(latest, _state == null ? "0" : _state.Version);
                    if (versionComparison > 0)
                    {
                        _updateAvailable = true;
                        UpdateStatusText.Text = "发现新版本 " + latest + "，可前往 GitHub 下载";
                        CheckUpdateButton.Content = "前往下载";
                    }
                    else if (versionComparison < 0)
                    {
                        UpdateStatusText.Text = "当前测试版 " + _state.Version + "；GitHub 最新发行版为 " + latest;
                        CheckUpdateButton.Content = "再次检查";
                    }
                    else
                    {
                        UpdateStatusText.Text = "已是最新版本 " + latest;
                        CheckUpdateButton.Content = "再次检查";
                    }
                }
            }
            catch (Exception ex)
            {
                _updateAvailable = false;
                UpdateStatusText.Text = "检查失败：" + ex.Message;
                CheckUpdateButton.Content = "重新检查";
            }
            finally { CheckUpdateButton.IsEnabled = true; }
        }

        private static int CompareVersions(string left, string right)
        {
            int[] a = VersionParts(left), b = VersionParts(right);
            for (int i = 0; i < 4; i++) { int c = a[i].CompareTo(b[i]); if (c != 0) return c; }
            return 0;
        }

        private static int[] VersionParts(string value)
        {
            var m = Regex.Match(value ?? "", "(\\d+)\\.(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?");
            if (!m.Success) return new[] { 0, 0, 0, 0 };
            return new[] { int.Parse(m.Groups[1].Value), int.Parse(m.Groups[2].Value), m.Groups[3].Success ? int.Parse(m.Groups[3].Value) : 0, m.Groups[4].Success ? int.Parse(m.Groups[4].Value) : 0 };
        }

        private static void SetReadout(TextBlock target, string value)
        {
            if (target == null || string.Equals(target.Text, value, StringComparison.Ordinal)) return;
            target.Text = value;
            EfMotion.ReadoutSettle(target);
        }

        private static void OpenUrl(string url)
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }

        private void Close_Click(object sender, RoutedEventArgs e) => Close();
    }
}

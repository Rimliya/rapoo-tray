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
        private enum HotkeyKind { Panel, Profile, Dpi, Performance, ToggleOsd }

        private sealed class HotkeyBinding
        {
            public int Modifiers;
            public int VirtualKey;
            public int DefaultModifiers;
            public int DefaultVirtualKey;
            public int RegistrationBit;
            public TextBlock ValueText;
            public TextBlock StatusText;

            public HotkeyBinding(int modifiers, int virtualKey, int registrationBit,
                TextBlock valueText, TextBlock statusText)
            {
                Modifiers = DefaultModifiers = modifiers;
                VirtualKey = DefaultVirtualKey = virtualKey;
                RegistrationBit = registrationBit;
                ValueText = valueText;
                StatusText = statusText;
            }
        }
        private readonly TrayBridge _bridge;
        private readonly bool _demo;
        private readonly int _initialPage;
        private readonly DispatcherTimer _liveTimer;
        private readonly PollingProbe _pollingProbe;
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
        private HotkeyKind? _capturingHotkeyKind;
        private readonly Dictionary<HotkeyKind, HotkeyBinding> _hotkeyBindings;
        private bool _batteryLoading;
        private bool _osdDirty;
        private bool _batteryDirty;
        private bool _systemThemeWatching;
        private int _selectedProfileId;
        private int _profilePerfMode = 1;
        private int _selectedDpiIndex;
        private int _batteryHistoryDays = 1;
        private DateTime _batteryLoadedAt = DateTime.MinValue;
        private bool _updateAvailable;
        private bool _updateDownloading;
        private GitHubReleaseInfo _latestRelease;
        private VerifiedUpdate _verifiedUpdate;
        private bool? _dirtyVisualState;
        private string _modelSignature = "";
        private static readonly int[] PollRates = { 125, 250, 500, 1000, 2000, 4000, 8000 };
        private static readonly HttpClient Http = CreateHttpClient();

        public MainWindow(int page, bool demo)
        {
            InitializeComponent();
            _hotkeyBindings = new Dictionary<HotkeyKind, HotkeyBinding>
            {
                { HotkeyKind.Panel, new HotkeyBinding(3, 'M', 0, HotkeyValueText, HotkeyStatusText) },
                { HotkeyKind.Profile, new HotkeyBinding(3, 'P', 1, ProfileHotkeyValueText, ProfileHotkeyStatusText) },
                { HotkeyKind.Dpi, new HotkeyBinding(3, 'G', 2, DpiHotkeyValueText, DpiHotkeyStatusText) },
                { HotkeyKind.Performance, new HotkeyBinding(3, 'F', 3, PerformanceHotkeyValueText, PerformanceHotkeyStatusText) },
                { HotkeyKind.ToggleOsd, new HotkeyBinding(3, 'O', 4, ToggleOsdHotkeyValueText, ToggleOsdHotkeyStatusText) },
            };
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
            _initialPage = Math.Max(0, Math.Min(6, page));
            _demo = demo;
            _bridge = new TrayBridge(demo);
            _pollingProbe = new PollingProbe();
            _pollingProbe.Updated += OnPollingProbeUpdated;
            _liveTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
            _liveTimer.Tick += LiveTimer_Tick;
            Loaded += MainWindow_Loaded;
            SourceInitialized += MainWindow_SourceInitialized;
            PreviewKeyDown += MainWindow_PreviewKeyDown;
            Closed += (_, __) =>
            {
                _liveTimer.Stop();
                _pollingProbe.Dispose();
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
            client.DefaultRequestHeaders.UserAgent.ParseAdd("rapoo-tray-settings/1.16.1");
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
            SetReadout(HeaderConnectionText, state.Connected ? "已连接 │ " + state.Battery + "%" : "未连接");
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
                    LoadHotkeyBindings(state);
                    UpdateHotkeyVisuals(state.HotkeyRegistrationMask);
                }
                SetDisplayFieldChecks(state.OsdDisplayFields);
                SetQuickPanelItemChecks(state.QuickPanelItems);
                LoadOsdAppearance(state);
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
                    ? "电量偏低，建议充电"
                    : state.DeviceMode;
            RfFullToggle.IsEnabled = state.RfAvailable && !_busy;
            RfStateText.Text = state.RfAvailable
                ? (state.RfFull ? "满格射频" : "智能调节（推荐）")
                : (state.Connected ? "待读取" : "未连接");
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
            UpdateDiagnostics();
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
            page = Math.Max(0, Math.Min(6, page));
            if (page != 5 && _pollingProbe.IsRunning) _pollingProbe.Stop();
            _switchingPage = true;
            NavProfile.IsChecked = page == 0;
            NavAutomation.IsChecked = page == 1;
            NavOsd.IsChecked = page == 2;
            NavBattery.IsChecked = page == 3;
            NavDpi.IsChecked = page == 4;
            NavDiagnostics.IsChecked = page == 5;
            NavAbout.IsChecked = page == 6;
            _switchingPage = false;

            ProfilePage.Visibility = page == 0 ? Visibility.Visible : Visibility.Collapsed;
            AutomationPage.Visibility = page == 1 ? Visibility.Visible : Visibility.Collapsed;
            OsdPage.Visibility = page == 2 ? Visibility.Visible : Visibility.Collapsed;
            BatteryPage.Visibility = page == 3 ? Visibility.Visible : Visibility.Collapsed;
            DpiPage.Visibility = page == 4 ? Visibility.Visible : Visibility.Collapsed;
            DiagnosticsPage.Visibility = page == 5 ? Visibility.Visible : Visibility.Collapsed;
            AboutPage.Visibility = page == 6 ? Visibility.Visible : Visibility.Collapsed;

            if (page == 0)
            {
                PageEyebrow.Text = "// PROFILES";
                PageTitle.Text = "配置";
                PageSubtitle.Text = "为不同程序保存回报率、性能模式与 DPI 设置";
            }
            else if (page == 1)
            {
                PageEyebrow.Text = "// AUTOMATION";
                PageTitle.Text = "自动化";
                PageSubtitle.Text = "管理前台自动切换、射频策略与续航估算";
                if (_state != null && _state.Connected) { var ignored = RefreshRfStateAsync(); }
            }
            else if (page == 2)
            {
                PageEyebrow.Text = "// OSD";
                PageTitle.Text = "OSD 与快捷键";
                PageSubtitle.Text = "设置状态 OSD、快速面板与全局快捷键";
            }
            else if (page == 3)
            {
                PageEyebrow.Text = "// BATTERY";
                PageTitle.Text = "电量";
                PageSubtitle.Text = "查看电量曲线，设置低电量通知与循环充电提醒";
                var ignored = LoadBatteryHistoryAsync(true);
            }
            else if (page == 4)
            {
                PageEyebrow.Text = "// DPI";
                PageTitle.Text = "DPI 档位";
                PageSubtitle.Text = "编辑鼠标板载 DPI 档位，应用后写入鼠标";
                if (!_dpiLoaded) { var ignored = LoadDpiAsync(); }
            }
            else if (page == 5)
            {
                PageEyebrow.Text = "// DIAGNOSTICS";
                PageTitle.Text = "诊断";
                PageSubtitle.Text = "实测回报率、查看连接状态，导出诊断报告";
                UpdateDiagnostics();
                if (!_dpiLoaded) { var ignored = LoadDpiAsync(); }
            }
            else
            {
                PageEyebrow.Text = "// ABOUT";
                PageTitle.Text = "关于";
                PageSubtitle.Text = "版本信息、项目源码、配置快照与更新检查";
                RefreshSnapshots();
            }

            FrameworkElement pageElement = page == 0 ? ProfilePage
                : page == 1 ? (FrameworkElement)AutomationPage
                : page == 2 ? OsdPage
                : page == 3 ? BatteryPage
                : page == 4 ? DpiPage
                : page == 5 ? (FrameworkElement)DiagnosticsPage
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
                var result = MessageBox.Show("当前配置还没保存。放弃修改并切换吗？", "rapoo-tray", MessageBoxButton.YesNo, MessageBoxImage.Question);
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
            SetStatus("已选中配置。保存仅修改本机；应用后写入鼠标。", true);
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
            SetStatus("更改尚未保存", true);
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
            PerfAvailableText.Text = "当前回报率支持 " + count + " 档";
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

            CaptureSnapshot("保存前");
            SetBusy(true, "正在保存配置…");
            try
            {
                var state = await _bridge.SaveProfileAsync(row, row.IsDefault);
                ApplyState(state, true);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "配置已保存。点“应用”才会写入鼠标。" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private async void AddProfile_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            if (_profileDirty && System.Windows.MessageBox.Show("当前配置还没保存。放弃修改并新建配置吗？", "rapoo-tray", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
            CaptureSnapshot("新增前");
            SetBusy(true, "正在新增配置…");
            try
            {
                var state = await _bridge.AddProfileAsync();
                ApplyState(state, true);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "配置已创建，接下来修改名称和参数。" : state.StatusText, state.StatusGood);
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
            CaptureSnapshot("删除前");
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
                SetStatus("先保存当前配置，再导出配置包", false);
                MessageBox.Show(this, "当前配置还没保存，先保存再导出。", "导出配置", MessageBoxButton.OK, MessageBoxImage.Information);
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
            if (_profileDirty && MessageBox.Show(this, "当前配置还没保存。导入会替换当前配置，继续吗？", "导入配置", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
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

            CaptureSnapshot("导入前");
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
                        "配置尚未写入鼠标。选择配置后点“应用”。", "导入完成", MessageBoxButton.OK, MessageBoxImage.Information);
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
                SetStatus("先保存修改，再应用配置", false);
                return;
            }
            CaptureSnapshot("应用前");
            SetBusy(true, "正在写入鼠标到鼠标…");
            try
            {
                var state = await _bridge.ApplyProfileAsync(_selectedProfileId);
                ApplyState(state, true);
                SetStatus(string.IsNullOrWhiteSpace(state.StatusText) ? "配置已写入鼠标" : state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
            finally { SetBusy(false); }
        }

        private void BrowseApp_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new Microsoft.Win32.OpenFileDialog
            {
                Title = "选择要关联的程序",
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

        private void LoadHotkeyBindings(AppState state)
        {
            _hotkeyBindings[HotkeyKind.Panel].Modifiers = state.HotkeyModifiers;
            _hotkeyBindings[HotkeyKind.Panel].VirtualKey = state.HotkeyVirtualKey;
            _hotkeyBindings[HotkeyKind.Profile].Modifiers = state.ProfileHotkeyModifiers;
            _hotkeyBindings[HotkeyKind.Profile].VirtualKey = state.ProfileHotkeyVirtualKey;
            _hotkeyBindings[HotkeyKind.Dpi].Modifiers = state.DpiHotkeyModifiers;
            _hotkeyBindings[HotkeyKind.Dpi].VirtualKey = state.DpiHotkeyVirtualKey;
            _hotkeyBindings[HotkeyKind.Performance].Modifiers = state.PerformanceHotkeyModifiers;
            _hotkeyBindings[HotkeyKind.Performance].VirtualKey = state.PerformanceHotkeyVirtualKey;
            _hotkeyBindings[HotkeyKind.ToggleOsd].Modifiers = state.ToggleOsdHotkeyModifiers;
            _hotkeyBindings[HotkeyKind.ToggleOsd].VirtualKey = state.ToggleOsdHotkeyVirtualKey;
        }

        private void UpdateHotkeyVisuals(int registrationMask)
        {
            foreach (KeyValuePair<HotkeyKind, HotkeyBinding> pair in _hotkeyBindings)
            {
                HotkeyBinding binding = pair.Value;
                bool assigned = binding.Modifiers != 0 || binding.VirtualKey != 0;
                binding.ValueText.Text = FormatHotkey(binding.Modifiers, binding.VirtualKey);
                if (!assigned)
                {
                    binding.StatusText.Text = "已禁用";
                    binding.StatusText.Foreground = (Brush)FindResource("AppSecondaryTextBrush");
                    continue;
                }
                if (pair.Key == HotkeyKind.Panel && QuickPanelToggle.IsChecked != true)
                {
                    binding.StatusText.Text = "快速面板已关闭";
                    binding.StatusText.Foreground = (Brush)FindResource("AppSecondaryTextBrush");
                    continue;
                }
                bool registered = (registrationMask & (1 << binding.RegistrationBit)) != 0;
                binding.StatusText.Text = registered ? "已注册" : "注册失败或组合键被占用";
                binding.StatusText.Foreground = (Brush)FindResource(registered ? "AppSignalBrush" : "AppDangerBrush");
            }
        }

        private static int ComboTag(ComboBox combo, int fallback)
        {
            ComboBoxItem item = combo == null ? null : combo.SelectedItem as ComboBoxItem;
            int value;
            return item != null && item.Tag != null && int.TryParse(Convert.ToString(item.Tag, CultureInfo.InvariantCulture), out value)
                ? value : fallback;
        }

        private static void SelectComboTag(ComboBox combo, int value)
        {
            if (combo == null) return;
            foreach (ComboBoxItem item in combo.Items)
            {
                int tag;
                if (item.Tag != null && int.TryParse(Convert.ToString(item.Tag, CultureInfo.InvariantCulture), out tag) && tag == value)
                {
                    combo.SelectedItem = item;
                    return;
                }
            }
        }

        private void LoadOsdAppearance(AppState state)
        {
            SelectComboTag(OsdPositionCombo, state.OsdPosition);
            SelectComboTag(OsdMonitorCombo, state.OsdMonitor);
            SelectComboTag(OsdScaleCombo, state.OsdScalePercent <= 0 ? 100 : state.OsdScalePercent);
            SelectComboTag(OsdOpacityCombo, state.OsdOpacityPercent <= 0 ? 94 : state.OsdOpacityPercent);
            OsdDurationBox.Value = state.OsdDurationMs <= 0 ? 2200 : state.OsdDurationMs;
            OsdOffsetXBox.Value = state.OsdOffsetX;
            OsdOffsetYBox.Value = state.OsdOffsetY;
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

        private static HotkeyKind? ParseHotkeyKind(object tag)
        {
            switch (Convert.ToString(tag))
            {
                case "panel": return HotkeyKind.Panel;
                case "profile": return HotkeyKind.Profile;
                case "dpi": return HotkeyKind.Dpi;
                case "performance": return HotkeyKind.Performance;
                case "toggleOsd": return HotkeyKind.ToggleOsd;
                default: return null;
            }
        }

        private void CaptureHotkey_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            HotkeyKind? kind = ParseHotkeyKind((sender as Button)?.Tag);
            if (!kind.HasValue) return;
            _capturingHotkey = true;
            _capturingHotkeyKind = kind;
            HotkeyBinding binding = _hotkeyBindings[kind.Value];
            binding.ValueText.Text = "请按下组合键…";
            binding.StatusText.Text = "按 Esc 取消；字母/数字/方向键需搭配修饰键";
            binding.StatusText.Foreground = (Brush)FindResource("AppSecondaryTextBrush");
            (sender as Button)?.Focus();
        }

        private void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (!_capturingHotkey || !_capturingHotkeyKind.HasValue) return;
            e.Handled = true;
            Key key = e.Key == Key.System ? e.SystemKey : e.Key;
            HotkeyBinding binding = _hotkeyBindings[_capturingHotkeyKind.Value];
            if (key == Key.Escape)
            {
                _capturingHotkey = false;
                _capturingHotkeyKind = null;
                binding.ValueText.Text = FormatHotkey(binding.Modifiers, binding.VirtualKey);
                binding.StatusText.Text = "已取消快捷键录制";
                binding.StatusText.Foreground = (Brush)FindResource("AppSecondaryTextBrush");
                return;
            }
            if (key == Key.LeftCtrl || key == Key.RightCtrl || key == Key.LeftAlt || key == Key.RightAlt ||
                key == Key.LeftShift || key == Key.RightShift || key == Key.LWin || key == Key.RWin) return;

            int modifiers = (int)Keyboard.Modifiers;
            int virtualKey = KeyInterop.VirtualKeyFromKey(key);
            if ((modifiers & 15) == 0 && !IsFunctionKey(key))
            {
                binding.StatusText.Text = "无修饰键的组合不可用；请使用 Ctrl / Alt / Shift / Win，或单独使用 F1–F24";
                return;
            }
            if (virtualKey <= 0 || key == Key.None)
            {
                binding.StatusText.Text = "该按键不可用作全局快捷键";
                return;
            }
            _capturingHotkey = false;
            _capturingHotkeyKind = null;
            binding.Modifiers = modifiers & 15;
            binding.VirtualKey = virtualKey;
            binding.ValueText.Text = FormatHotkey(binding.Modifiers, binding.VirtualKey);
            binding.StatusText.Text = "已录制，保存后生效";
            binding.StatusText.Foreground = (Brush)FindResource("AppSignalBrush");
            _osdDirty = true;
            OsdConfigStatusText.Text = "OSD 设置尚未保存";
        }

        private void ClearActionHotkey_Click(object sender, RoutedEventArgs e)
        {
            HotkeyKind? kind = ParseHotkeyKind((sender as Button)?.Tag);
            if (!kind.HasValue || kind.Value == HotkeyKind.Panel) return;
            HotkeyBinding binding = _hotkeyBindings[kind.Value];
            binding.Modifiers = 0;
            binding.VirtualKey = 0;
            binding.ValueText.Text = "未设置";
            binding.StatusText.Text = "已禁用，保存后生效";
            binding.StatusText.Foreground = (Brush)FindResource("AppSecondaryTextBrush");
            _capturingHotkey = false;
            _capturingHotkeyKind = null;
            _osdDirty = true;
            OsdConfigStatusText.Text = "OSD 设置尚未保存";
        }

        private void ResetAllHotkeys_Click(object sender, RoutedEventArgs e)
        {
            foreach (HotkeyBinding binding in _hotkeyBindings.Values)
            {
                binding.Modifiers = binding.DefaultModifiers;
                binding.VirtualKey = binding.DefaultVirtualKey;
                binding.ValueText.Text = FormatHotkey(binding.Modifiers, binding.VirtualKey);
                binding.StatusText.Text = "已恢复默认，保存后生效";
                binding.StatusText.Foreground = (Brush)FindResource("AppSecondaryTextBrush");
            }
            _capturingHotkey = false;
            _capturingHotkeyKind = null;
            _osdDirty = true;
            OsdConfigStatusText.Text = "OSD 设置尚未保存";
        }

        private void OsdOption_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading || !_initialized) return;
            _osdDirty = true;
            OsdConfigStatusText.Text = "OSD 设置尚未保存";
        }

        private OsdSettings ReadOsdSettings()
        {
            HotkeyBinding panel = _hotkeyBindings[HotkeyKind.Panel];
            HotkeyBinding profile = _hotkeyBindings[HotkeyKind.Profile];
            HotkeyBinding dpi = _hotkeyBindings[HotkeyKind.Dpi];
            HotkeyBinding performance = _hotkeyBindings[HotkeyKind.Performance];
            HotkeyBinding toggleOsd = _hotkeyBindings[HotkeyKind.ToggleOsd];
            return new OsdSettings
            {
                Enabled = OsdToggle.IsChecked == true,
                QuickPanelEnabled = QuickPanelToggle.IsChecked == true,
                HotkeyModifiers = panel.Modifiers,
                HotkeyVirtualKey = panel.VirtualKey,
                DisplayFields = ReadCheckMask(DisplayModelCheck, DisplayModeCheck, DisplayDpiLevelCheck,
                    DisplayDpiValueCheck, DisplayBatteryCheck, DisplayPollingCheck, DisplayPerformanceCheck),
                PanelItems = ReadCheckMask(PanelProfilesCheck, PanelDpiCheck, PanelPerformanceCheck,
                    PanelBatteryCheck, PanelPollingCheck, PanelDetailsCheck),
                Position = ComboTag(OsdPositionCombo, 0),
                Monitor = ComboTag(OsdMonitorCombo, 0),
                OffsetX = (int)Math.Round(OsdOffsetXBox.Value ?? 0),
                OffsetY = (int)Math.Round(OsdOffsetYBox.Value ?? 0),
                ScalePercent = ComboTag(OsdScaleCombo, 100),
                OpacityPercent = ComboTag(OsdOpacityCombo, 94),
                DurationMs = (int)Math.Round(OsdDurationBox.Value ?? 2200),
                ProfileHotkeyModifiers = profile.Modifiers,
                ProfileHotkeyVirtualKey = profile.VirtualKey,
                DpiHotkeyModifiers = dpi.Modifiers,
                DpiHotkeyVirtualKey = dpi.VirtualKey,
                PerformanceHotkeyModifiers = performance.Modifiers,
                PerformanceHotkeyVirtualKey = performance.VirtualKey,
                ToggleOsdHotkeyModifiers = toggleOsd.Modifiers,
                ToggleOsdHotkeyVirtualKey = toggleOsd.VirtualKey
            };
        }

        private void OsdNumber_Changed(object sender, NumberBoxValueChangedEventArgs e)
        {
            if (_loading || !_initialized) return;
            _osdDirty = true;
            OsdConfigStatusText.Text = "OSD 设置尚未保存";
        }
        private async Task<bool> SaveOsdSettingsAsync()
        {
            if (_busy) return false;
            SetBusy(true, "正在保存 OSD 与快捷键…");
            try
            {
                AppState state = await _bridge.SetOsdConfigAsync(ReadOsdSettings());
                _osdDirty = !state.StatusGood;
                ApplyState(state, false);
                OsdConfigStatusText.Text = state.StatusGood ? "OSD 与快捷键已保存" : state.StatusText;
                SetStatus(state.StatusText, state.StatusGood);
                return state.StatusGood;
            }
            catch (Exception ex)
            {
                SetStatus(ex.Message, false);
                return false;
            }
            finally { SetBusy(false); }
        }

        private async void SaveOsdSettings_Click(object sender, RoutedEventArgs e)
        {
            await SaveOsdSettingsAsync();
        }

        private async void PreviewOsd_Click(object sender, RoutedEventArgs e)
        {
            if (OsdToggle.IsChecked != true)
            {
                SetStatus("请先开启“状态 OSD”再预览 OSD", false);
                return;
            }
            if (_osdDirty && !await SaveOsdSettingsAsync()) return;
            try
            {
                AppState state = await _bridge.PreviewOsdAsync();
                SetStatus(state.StatusText, state.StatusGood);
            }
            catch (Exception ex) { SetStatus(ex.Message, false); }
        }

        private void ResetOsdAppearance_Click(object sender, RoutedEventArgs e)
        {
            SelectComboTag(OsdPositionCombo, 0);
            SelectComboTag(OsdMonitorCombo, 0);
            SelectComboTag(OsdScaleCombo, 100);
            SelectComboTag(OsdOpacityCombo, 94);
            OsdDurationBox.Value = 2200;
            OsdOffsetXBox.Value = 0;
            OsdOffsetYBox.Value = 0;
            _osdDirty = true;
            OsdConfigStatusText.Text = "已恢复默认外观，保存后生效";
        }

        private async void PreviewQuickPanel_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            try
            {
                AppState state = await _bridge.ShowQuickPanelAsync();
                SetStatus("快速面板已打开。点击项目切换，Esc 关闭。", state.StatusGood);
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
            BatteryConfigStatusText.Text = "提醒设置尚未保存";
        }

        private void BatteryNumber_Changed(object sender, NumberBoxValueChangedEventArgs e)
        {
            if (_loading || !_initialized) return;
            _batteryDirty = true;
            BatteryConfigStatusText.Text = "提醒设置尚未保存";
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

            SetBusy(true, "正在保存提醒设置…");
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
                    if (!_state.RfAvailable && _state.Connected) RfStateText.Text = "待读取";
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
                UpdateDiagnostics();
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
                DpiPageSubtitle.Text = (slot.X == slot.Y ? slot.X.ToString() : slot.X + " × " + slot.Y) + " DPI" + (_dpi.FromDevice ? " · 来自鼠标" : " · 本机缓存");
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
            DpiStatusText.Text = _dpiDirty ? "DPI 修改尚未写入鼠标" : "已与鼠标同步";
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

        private void CaptureSnapshot(string reason)
        {
            try
            {
                if (SnapshotService.Create(_state, reason) != null)
                {
                    RefreshSnapshots();
                }
            }
            catch (Exception ex)
            {
                SetStatus("自动快照失败：" + ex.Message, false);
            }
        }

        private void RefreshSnapshots()
        {
            try
            {
                List<ProfileSnapshot> snapshots = SnapshotService.List();
                SnapshotCombo.ItemsSource = snapshots;
                SnapshotCombo.SelectedIndex = snapshots.Count > 0 ? 0 : -1;
                SnapshotStatusText.Text = snapshots.Count == 0
                    ? "暂无快照；完整设置和关键配置操作会自动备份"
                    : "共 " + snapshots.Count + " 份快照，最多保留 20 份";
            }
            catch (Exception ex)
            {
                SnapshotStatusText.Text = "读取快照失败：" + ex.Message;
            }
        }

        private void RefreshSnapshots_Click(object sender, RoutedEventArgs e)
        {
            RefreshSnapshots();
        }

        private void CreateSnapshot_Click(object sender, RoutedEventArgs e)
        {
            if (_state == null || _state.Profiles.Count == 0)
            {
                SetStatus("当前没有可备份的配置", false);
                return;
            }
            try
            {
                ProfileSnapshot snapshot = SnapshotService.Create(_state, "手动");
                RefreshSnapshots();
                SnapshotCombo.SelectedItem = snapshot;
                SnapshotStatusText.Text = "已创建完整设置快照：" + Path.GetFileName(snapshot.Path);
                SetStatus("已创建完整设置快照", true);
            }
            catch (Exception ex)
            {
                SetStatus("创建快照失败：" + ex.Message, false);
            }
        }

        private async void RestoreSnapshot_Click(object sender, RoutedEventArgs e)
        {
            if (_busy) return;
            ProfileSnapshot snapshot = SnapshotCombo.SelectedItem as ProfileSnapshot;
            if (snapshot == null)
            {
                SetStatus("请选择要恢复的快照", false);
                return;
            }
            if (MessageBox.Show(this, "恢复会替换当前配置，并尽量恢复 OSD、快捷键、电量提醒、自启动与型号设置。当前状态会先自动备份，是否继续？",
                "恢复配置快照", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
            CaptureSnapshot("恢复前");
            SetBusy(true, "正在恢复完整设置快照…");
            try
            {
                SettingsSnapshotPackage package = SnapshotService.Load(snapshot);
                var restoreErrors = new List<string>();
                void RememberRestore(AppState candidate)
                {
                    if (candidate != null && !candidate.StatusGood && !restoreErrors.Contains(candidate.StatusText))
                        restoreErrors.Add(candidate.StatusText);
                }

                AppState state = await _bridge.ImportProfilesAsync(package.Profiles);
                RememberRestore(state);
                bool fullSettings = package.Osd != null || package.Battery != null;
                if (package.Osd != null)
                {
                    state = await _bridge.SetOsdConfigAsync(package.Osd.ToOsdSettings());
                    RememberRestore(state);
                }
                if (package.Battery != null)
                {
                    state = await _bridge.SetBatteryConfigAsync(package.Battery.Enabled,
                        package.Battery.WarningThreshold, package.Battery.CriticalThreshold,
                        package.Battery.ChargingReminderEnabled, package.Battery.ReminderMinutes);
                    RememberRestore(state);
                }
                if (fullSettings)
                {
                    state = await _bridge.SetModelAsync(package.ModelIndex);
                    RememberRestore(state);
                    state = await _bridge.SetAutoRunAsync(package.AutoRun);
                    RememberRestore(state);
                }
                state = await _bridge.GetStateAsync();
                _profileDirty = false;
                _selectedProfileId = 0;
                ApplyState(state, true);
                RefreshSnapshots();
                if (restoreErrors.Count > 0)
                    SetStatus("快照部分恢复失败：" + string.Join("；", restoreErrors), false);
                else
                    SetStatus(fullSettings ? "完整设置快照已恢复；应用后写入鼠标" : "旧版配置快照已恢复；应用后写入鼠标", true);
            }
            catch (Exception ex)
            {
                SetStatus("恢复快照失败：" + ex.Message, false);
            }
            finally { SetBusy(false); }
        }
        private void OnPollingProbeUpdated(object sender, PollingProbeSnapshot snapshot)
        {
            if (snapshot == null) return;
            if (!Dispatcher.CheckAccess())
            {
                Dispatcher.BeginInvoke(new Action(() => OnPollingProbeUpdated(sender, snapshot)));
                return;
            }
            if (!string.IsNullOrWhiteSpace(snapshot.FilterLabel)) PollingTargetText.Text = "目标设备：" + snapshot.FilterLabel;
            PollingMeasuredText.Text = Math.Round(snapshot.MeasuredHz).ToString(CultureInfo.InvariantCulture) + " Hz";
            PollingJitterText.Text = snapshot.JitterMs.ToString("0.00", CultureInfo.InvariantCulture) + " ms";
            PollingMaxGapText.Text = snapshot.MaxGapMs.ToString("0.00", CultureInfo.InvariantCulture) + " ms";
            PollingSamplesText.Text = snapshot.Samples.ToString(CultureInfo.InvariantCulture);
            PollingStatusText.Text = snapshot.Running
                ? (snapshot.Samples == 0 ? "请持续移动鼠标以采集原始输入间隔" : "采样中：保持鼠标持续移动，结果会每秒更新")
                : "采样已停止";
            PollingStartButton.Content = snapshot.Running ? "停止采样" : "开始采样";
        }

        private void TogglePollingProbe_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                if (_pollingProbe.IsRunning)
                {
                    _pollingProbe.Stop();
                    return;
                }
                if (_state == null || !_state.Connected) throw new InvalidOperationException("设备未连接，无法开始目标设备采样。");
                _pollingProbe.Start(this, _demo ? "" : _state.DeviceProductId);
                PollingStartButton.Content = "停止采样";
                PollingStatusText.Text = "请持续移动鼠标以采集原始输入间隔";
            }
            catch (Exception ex)
            {
                SetStatus("无法启动回报率验证：" + ex.Message, false);
            }
        }

        private void UpdateDiagnostics()
        {
            if (_state == null) return;
            DiagModelText.Text = _state.DeviceName;
            PollingTargetText.Text = string.IsNullOrWhiteSpace(_state.DeviceProductId)
                ? "目标设备：VID_24AE / 等待 PID"
                : "目标设备：VID_24AE / " + _state.DeviceProductId.ToUpperInvariant();
            DiagModeText.Text = _state.DeviceMode;
            DiagBatteryText.Text = _state.Battery + "%";
            ProfileRow active = _state.Profiles.FirstOrDefault(x => x.Id == _state.ActiveProfileId);
            DiagPollingText.Text = active == null ? "--" : active.PollingHz + " Hz";
            DiagPerfText.Text = active == null ? "--" : active.PerformanceLabel;
            DiagDpiText.Text = _state.DeviceDpiX == _state.DeviceDpiY
                ? _state.DeviceDpiX + " DPI"
                : _state.DeviceDpiX + " × " + _state.DeviceDpiY + " DPI";
            DiagDpiRangeText.Text = _dpi == null
                ? "打开 DPI 页后读取"
                : _dpi.Min + "–" + _dpi.Max + " / " + _dpi.Slots.Count + " 档";
        }

        private void ExportDiagnostics_Click(object sender, RoutedEventArgs e)
        {
            if (_state == null) return;
            var dialog = new Microsoft.Win32.SaveFileDialog
            {
                Title = "导出 rapoo-tray 诊断报告",
                Filter = "文本文件 (*.txt)|*.txt",
                DefaultExt = ".txt",
                AddExtension = true,
                FileName = "rapoo-tray-diagnostics-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".txt"
            };
            if (dialog.ShowDialog(this) != true) return;
            PollingProbeSnapshot polling = _pollingProbe.GetSnapshot();
            ProfileRow active = _state.Profiles.FirstOrDefault(x => x.Id == _state.ActiveProfileId);
            var lines = new List<string>
            {
                "rapoo-tray diagnostics",
                "timestamp=" + DateTime.Now.ToString("O"),
                "app_version=" + _state.Version,
                "os=" + Environment.OSVersion.VersionString,
                "device=" + _state.DeviceName,
                "connection=" + _state.DeviceMode,
                "connected=" + _state.Connected,
                "battery=" + _state.Battery + "%",
                "dpi=" + _state.DeviceDpiX + "x" + _state.DeviceDpiY,
                "configured_polling=" + (active == null ? "--" : active.PollingHz.ToString(CultureInfo.InvariantCulture)),
                "performance_mode=" + (active == null ? "--" : active.PerformanceLabel),
                "dpi_range=" + (_dpi == null ? "--" : _dpi.Min + "-" + _dpi.Max),
                "dpi_slots=" + (_dpi == null ? "--" : _dpi.Slots.Count.ToString(CultureInfo.InvariantCulture)),
                "hotkey_mask=" + _state.HotkeyRegistrationMask,
                "measured_polling_hz=" + polling.MeasuredHz.ToString("0", CultureInfo.InvariantCulture),
                "jitter_ms=" + polling.JitterMs.ToString("0.000", CultureInfo.InvariantCulture),
                "max_gap_ms=" + polling.MaxGapMs.ToString("0.000", CultureInfo.InvariantCulture),
                "probe_samples=" + polling.Samples.ToString(CultureInfo.InvariantCulture),
                "note=measured_polling_uses_raw_input_arrival_times"
            };
            try
            {
                File.WriteAllLines(dialog.FileName, lines);
                SetStatus("诊断报告已导出：" + Path.GetFileName(dialog.FileName), true);
            }
            catch (Exception ex)
            {
                SetStatus("导出诊断报告失败：" + ex.Message, false);
            }
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

        private void SetUpdateRecoveryButtons(bool showOpen, bool showClear)
        {
            OpenReleasePageButton.Visibility = showOpen ? Visibility.Visible : Visibility.Collapsed;
            ClearUpdateCacheButton.Visibility = showClear ? Visibility.Visible : Visibility.Collapsed;
        }

        private void OpenReleasePage_Click(object sender, RoutedEventArgs e)
        {
            OpenUrl(_latestRelease != null && !string.IsNullOrWhiteSpace(_latestRelease.HtmlUrl)
                ? _latestRelease.HtmlUrl
                : "https://github.com/Rimliya/rapoo-tray/releases/latest");
        }

        private void ClearUpdateCache_Click(object sender, RoutedEventArgs e)
        {
            if (_updateDownloading) return;
            try
            {
                int removed = UpdateService.ClearCache();
                _verifiedUpdate = null;
                ClearUpdateCacheButton.Visibility = Visibility.Collapsed;
                CheckUpdateButton.Content = _updateAvailable && _latestRelease != null ? "重新下载" : "检查更新";
                UpdateStatusText.Text = removed > 0
                    ? "已清理 " + removed + " 个更新缓存文件，可重新下载"
                    : "更新缓存已经为空";
            }
            catch (Exception ex)
            {
                UpdateStatusText.Text = "清理更新缓存失败：" + ex.Message;
                SetStatus(ex.Message, false);
            }
        }

        private async void CheckUpdate_Click(object sender, RoutedEventArgs e)
        {
            if (_updateDownloading) return;
            if (_verifiedUpdate != null)
            {
                try
                {
                    Process.Start(new ProcessStartInfo(_verifiedUpdate.InstallerPath)
                    {
                        UseShellExecute = true,
                        WorkingDirectory = Path.GetDirectoryName(_verifiedUpdate.InstallerPath)
                    });
                    Close();
                }
                catch (Exception ex)
                {
                    UpdateStatusText.Text = "无法启动安装器：" + ex.Message + "；可清理缓存后重新下载。";
                    SetUpdateRecoveryButtons(true, true);
                    SetStatus(ex.Message, false);
                }
                return;
            }

            if (_updateAvailable && _latestRelease != null)
            {
                CheckUpdateButton.IsEnabled = false;
                CheckUpdateButton.Content = "下载中…";
                _updateDownloading = true;
                SetUpdateRecoveryButtons(true, true);
                UpdateStatusText.Text = "正在下载更新…";
                try
                {
                    var progress = new Progress<string>(message => UpdateStatusText.Text = message);
                    _verifiedUpdate = await UpdateService.DownloadAndVerifyAsync(Http, _latestRelease, progress);
                    UpdateStatusText.Text = _verifiedUpdate.Signed
                        ? "更新包已通过 SHA256 和数字签名验证：" + _verifiedUpdate.Publisher
                        : "更新包已通过 SHA256 校验（未签名）；SmartScreen 可能提示未知发布者";
                    CheckUpdateButton.Content = "安装更新";
                    SetUpdateRecoveryButtons(true, true);
                }
                catch (Exception ex)
                {
                    UpdateStatusText.Text = "自动更新失败：" + ex.Message + "；可查看 Releases 或清理缓存后重试。";
                    CheckUpdateButton.Content = "重试下载";
                    _verifiedUpdate = null;
                    SetUpdateRecoveryButtons(true, true);
                    SetStatus(ex.Message, false);
                }
                finally
                {
                    _updateDownloading = false;
                    CheckUpdateButton.IsEnabled = true;
                }
                return;
            }

            CheckUpdateButton.IsEnabled = false;
            CheckUpdateButton.Content = "检查中…";
            UpdateStatusText.Text = "正在检查 GitHub 最新版本…";
            _verifiedUpdate = null;
            _latestRelease = null;
            SetUpdateRecoveryButtons(false, false);
            try
            {
                _latestRelease = await UpdateService.GetLatestReleaseAsync(Http);
                string latest = _latestRelease.TagName;
                int versionComparison = CompareVersions(latest, _state == null ? "0" : _state.Version);
                if (versionComparison > 0)
                {
                    _updateAvailable = true;
                    UpdateStatusText.Text = "发现新版本 " + latest + "，可直接下载、校验并安装";
                    CheckUpdateButton.Content = "下载更新";
                    SetUpdateRecoveryButtons(true, false);
                }
                else if (versionComparison < 0)
                {
                    _updateAvailable = false;
                    UpdateStatusText.Text = "当前版本 " + _state.Version + "；GitHub 最新正式版为 " + latest;
                    CheckUpdateButton.Content = "再次检查";
                    SetUpdateRecoveryButtons(false, false);
                }
                else
                {
                    _updateAvailable = false;
                    UpdateStatusText.Text = "已是最新版本 " + latest;
                    CheckUpdateButton.Content = "再次检查";
                    SetUpdateRecoveryButtons(false, false);
                }
            }
            catch (Exception ex)
            {
                _updateAvailable = false;
                UpdateStatusText.Text = "检查失败：" + ex.Message + "；可查看 Releases 页面或稍后重试。";
                CheckUpdateButton.Content = "重新检查";
                SetUpdateRecoveryButtons(true, true);
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

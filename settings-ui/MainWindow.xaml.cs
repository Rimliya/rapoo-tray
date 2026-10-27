using Microsoft.Win32;
using System;
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
using System.Windows.Media;
using System.Windows.Threading;
using Wpf.Ui.Appearance;
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
        private int _selectedProfileId;
        private int _profilePerfMode = 1;
        private int _selectedDpiIndex;
        private bool _updateAvailable;
        private string _modelSignature = "";
        private string _releaseUrl = "https://github.com/Rimliya/rapoo-tray/releases/latest";
        private static readonly int[] PollRates = { 125, 250, 500, 1000, 2000, 4000, 8000 };
        private static readonly HttpClient Http = CreateHttpClient();

        public MainWindow(int page, bool demo)
        {
            InitializeComponent();
            SystemThemeWatcher.Watch(this);
            _initialPage = Math.Max(0, Math.Min(3, page));
            _bridge = new TrayBridge(demo);
            _liveTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
            _liveTimer.Tick += LiveTimer_Tick;
            Loaded += MainWindow_Loaded;
            SourceInitialized += MainWindow_SourceInitialized;
            Closed += (_, __) => { _liveTimer.Stop(); if (_source != null) _source.RemoveHook(WndProc); };
            UpdateDirtyVisuals();
        }

        private static HttpClient CreateHttpClient()
        {
            var client = new HttpClient { Timeout = TimeSpan.FromSeconds(12) };
            client.DefaultRequestHeaders.UserAgent.ParseAdd("rapoo-tray-settings/1.9");
            return client;
        }

        private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            _initialized = true;
            await ReloadStateAsync(true);
            ActivatePage(_initialPage, true);
            _liveTimer.Start();
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
            HeaderConnectionText.Text = state.Connected ? "已连接 · " + state.Battery + "%" : "等待设备";
            HeaderModeText.Text = state.DeviceMode;
            HeaderDpiText.Text = state.DeviceDpiX + " DPI";
            HeaderStatusDot.Fill = new SolidColorBrush(state.Connected ? Color.FromRgb(24, 166, 106) : Color.FromRgb(196, 58, 58));
            RailDeviceDot.Fill = HeaderStatusDot.Fill;
            CurrentModelText.Text = state.DeviceName;
            CurrentModeText.Text = state.DeviceMode;
            CurrentBatteryText.Text = state.Battery + "%";
            CurrentDpiText.Text = state.DeviceDpiX == state.DeviceDpiY ? state.DeviceDpiX.ToString() : state.DeviceDpiX + " × " + state.DeviceDpiY;

            bool oldLoading = _loading;
            _loading = true;
            AutoSwitchToggle.IsChecked = state.AutoSwitch;
            OsdToggle.IsChecked = state.OsdEnabled;
            AutoRunToggle.IsChecked = state.AutoRun;
            RfFullToggle.IsChecked = state.RfFull;
            RfFullToggle.IsEnabled = state.RfAvailable;
            RfStateText.Text = state.RfAvailable ? (state.RfFull ? "满格射频" : "智能调节（推荐）") : "当前设备不支持";
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
            StatusText.Text = string.IsNullOrWhiteSpace(text) ? "就绪" : text;
            StatusDot.Fill = new SolidColorBrush(good ? Color.FromRgb(24, 166, 106) : Color.FromRgb(196, 58, 58));
        }

        private void SetBusy(bool busy, string text = null)
        {
            _busy = busy;
            AddProfileButton.IsEnabled = !busy;
            DeleteProfileButton.IsEnabled = !busy && ProfilesList.Items.Count > 1;
            SaveProfileButton.IsEnabled = !busy && _profileDirty;
            ApplyProfileButton.IsEnabled = !busy && !_profileDirty && _selectedProfileId != 0;
            AddDpiButton.IsEnabled = !busy && (_dpi == null || _dpi.Slots.Count < Math.Min(7, _dpi.MaxSlots));
            DeleteDpiButton.IsEnabled = !busy && _dpi != null && _dpi.Slots.Count > 1;
            ApplyDpiButton.IsEnabled = !busy && _dpi != null && _dpiDirty;
            if (!string.IsNullOrEmpty(text)) SetStatus(text, true);
            Cursor = busy ? System.Windows.Input.Cursors.Wait : System.Windows.Input.Cursors.Arrow;
        }

        private void ActivatePage(int page, bool force)
        {
            if (!force && !_initialized) return;
            page = Math.Max(0, Math.Min(3, page));
            _switchingPage = true;
            NavProfile.IsChecked = page == 0;
            NavAutomation.IsChecked = page == 1;
            NavDpi.IsChecked = page == 2;
            NavAbout.IsChecked = page == 3;
            _switchingPage = false;

            ProfilePage.Visibility = page == 0 ? Visibility.Visible : Visibility.Collapsed;
            AutomationPage.Visibility = page == 1 ? Visibility.Visible : Visibility.Collapsed;
            DpiPage.Visibility = page == 2 ? Visibility.Visible : Visibility.Collapsed;
            AboutPage.Visibility = page == 3 ? Visibility.Visible : Visibility.Collapsed;

            if (page == 0)
            {
                PageTitle.Text = "配置档案";
                PageSubtitle.Text = "为不同应用保存回报率、性能模式、DPI 与启动规则";
            }
            else if (page == 1)
            {
                PageTitle.Text = "自动化";
                PageSubtitle.Text = "管理前台联动、悬浮提示与设备策略";
            }
            else if (page == 2)
            {
                PageTitle.Text = "DPI 档位";
                PageSubtitle.Text = "编辑鼠标内建 DPI 档位并安全写入设备";
                if (!_dpiLoaded) { var ignored = LoadDpiAsync(); }
            }
            else
            {
                PageTitle.Text = "关于";
                PageSubtitle.Text = "版本信息、项目源码与更新检查";
            }
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
            ProfileDirtyText.Foreground = _profileDirty
                ? new SolidColorBrush(Color.FromRgb(194, 122, 0))
                : (Brush)FindResource("TextFillColorSecondaryBrush");
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

        private async void AutoSwitch_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            await RunToggleAsync(_bridge.SetAutoSwitchAsync, AutoSwitchToggle.IsChecked == true, "正在更新前台联动…");
        }

        private async void Osd_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            await RunToggleAsync(_bridge.SetOsdAsync, OsdToggle.IsChecked == true, "正在更新悬浮窗…");
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
                DpiRangeText.Text = _dpi.Min + "–" + _dpi.Max + " DPI · 步进 " + _dpi.Step + " · 最多 " + Math.Min(7, _dpi.MaxSlots) + " 档";
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

        private static void OpenUrl(string url)
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }

        private void Close_Click(object sender, RoutedEventArgs e) => Close();
    }
}

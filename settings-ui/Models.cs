using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Windows;

namespace RapooTray.Settings
{
    public sealed class ProfileRow : INotifyPropertyChanged
    {
        public int Id { get; set; }
        public string Name { get; set; }
        public int PollingHz { get; set; }
        public int PerformanceMode { get; set; }
        public string PerformanceLabel { get; set; }
        public string DpiText { get; set; }
        public string AppName { get; set; }
        public string Badge { get; set; }
        public bool IsDefault { get; set; }
        public int SleepMinutes { get; set; }
        public int DpiX { get; set; }
        public int DpiY { get; set; }
        public string AppPath { get; set; }

        public string Summary => PollingHz + " Hz  /  " + PerformanceLabel + "  /  " + DpiText + (string.IsNullOrEmpty(AppName) ? "" : "  /  " + AppName);
        public string Initial
        {
            get
            {
                string name = (Name ?? "").Trim();
                return name.Length == 0 ? "?" : StringInfo.GetNextTextElement(name);
            }
        }
        public Visibility BadgeVisibility => string.IsNullOrEmpty(Badge) ? Visibility.Collapsed : Visibility.Visible;
        public Visibility ChevronVisibility => Visibility.Visible;

        public event PropertyChangedEventHandler PropertyChanged;
        private void Notify([CallerMemberName] string name = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }

    public sealed class ModelOption
    {
        public int Index { get; set; }
        public string Key { get; set; }
        public string Label { get; set; }
        public override string ToString() => Label;
    }

    public sealed class DpiSlotRow : INotifyPropertyChanged
    {
        private int _x;
        private int _y;
        private bool _isActive;

        public int Index { get; set; }
        public int X { get => _x; set { _x = value; Notify(); Notify(nameof(Display)); } }
        public int Y { get => _y; set { _y = value; Notify(); Notify(nameof(Display)); } }
        public bool IsActive { get => _isActive; set { _isActive = value; Notify(); Notify(nameof(ActiveLabel)); } }
        public string Display => X == Y ? X + " DPI" : X + " × " + Y + " DPI";
        public string ActiveLabel => IsActive ? "当前" : "";
        public Visibility ActiveVisibility => IsActive ? Visibility.Visible : Visibility.Collapsed;

        public event PropertyChangedEventHandler PropertyChanged;
        private void Notify([CallerMemberName] string name = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }

    public sealed class BatterySample
    {
        public long Timestamp { get; set; }
        public int Battery { get; set; }
        public bool Charging { get; set; }
        public bool Wired { get; set; }
        public DateTime LocalTime => DateTimeOffset.FromUnixTimeSeconds(Timestamp).LocalDateTime;
    }

    public sealed class BatteryHistoryState
    {
        public int Days { get; set; }
        public readonly ObservableCollection<BatterySample> Samples = new ObservableCollection<BatterySample>();
    }

    public sealed class AppState
    {
        public string Version = "1.16.0";
        public string DeviceName = "雷柏游戏鼠标";
        public string DeviceMode = "未连接";
        public bool Connected;
        public int Battery;
        public int DeviceDpiX;
        public int DeviceDpiY;
        public int ModelIndex;
        public bool AutoSwitch;
        public bool OsdEnabled;
        public bool AutoRun;
        public bool RfAvailable;
        public bool RfFull;
        public bool QuickPanelEnabled = true;
        public int HotkeyModifiers = 3;
        public int HotkeyVirtualKey = 77;
        public bool HotkeyRegistered = true;
        public int OsdDisplayFields = 127;
        public int QuickPanelItems = 63;
        public int OsdPosition;
        public int OsdMonitor;
        public int OsdOffsetX;
        public int OsdOffsetY;
        public int OsdScalePercent = 100;
        public int OsdOpacityPercent = 94;
        public int OsdDurationMs = 2200;
        public int ProfileHotkeyModifiers = 3;
        public int ProfileHotkeyVirtualKey = 80;
        public int DpiHotkeyModifiers = 3;
        public int DpiHotkeyVirtualKey = 71;
        public int PerformanceHotkeyModifiers = 3;
        public int PerformanceHotkeyVirtualKey = 70;
        public int ToggleOsdHotkeyModifiers = 3;
        public int ToggleOsdHotkeyVirtualKey = 79;
        public int HotkeyRegistrationMask = 31;
        public bool BatteryAlertsEnabled = true;
        public int BatteryWarningThreshold = 20;
        public int BatteryCriticalThreshold = 10;
        public bool ChargingReminderEnabled = true;
        public int ChargingReminderMinutes = 30;
        public int ActiveProfileId;
        public bool StatusGood = true;
        public string StatusText = "选择配置进行编辑，或新增一个配置";
        public int CreatedProfileId;
        public readonly ObservableCollection<ProfileRow> Profiles = new ObservableCollection<ProfileRow>();
        public readonly ObservableCollection<ModelOption> Models = new ObservableCollection<ModelOption>();
    }

    public sealed class OsdSettings
    {
        public bool Enabled;
        public bool QuickPanelEnabled;
        public int HotkeyModifiers;
        public int HotkeyVirtualKey;
        public int DisplayFields;
        public int PanelItems;
        public int Position;
        public int Monitor;
        public int OffsetX;
        public int OffsetY;
        public int ScalePercent = 100;
        public int OpacityPercent = 94;
        public int DurationMs = 2200;
        public int ProfileHotkeyModifiers;
        public int ProfileHotkeyVirtualKey;
        public int DpiHotkeyModifiers;
        public int DpiHotkeyVirtualKey;
        public int PerformanceHotkeyModifiers;
        public int PerformanceHotkeyVirtualKey;
        public int ToggleOsdHotkeyModifiers;
        public int ToggleOsdHotkeyVirtualKey;
    }

    public sealed class DpiState
    {
        public int SelectedIndex;
        public int ActiveIndex;
        public int Min = 10;
        public int Max = 30000;
        public int Step = 10;
        public int MaxSlots = 7;
        public bool FromDevice;
        public string ModelLabel = "雷柏鼠标";
        public readonly ObservableCollection<DpiSlotRow> Slots = new ObservableCollection<DpiSlotRow>();
    }
}

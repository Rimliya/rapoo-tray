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

    public sealed class AppState
    {
        public string Version = "1.11.0";
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
        public int ActiveProfileId;
        public bool StatusGood = true;
        public string StatusText = "选择配置进行编辑，或新增一个配置";
        public int CreatedProfileId;
        public readonly ObservableCollection<ProfileRow> Profiles = new ObservableCollection<ProfileRow>();
        public readonly ObservableCollection<ModelOption> Models = new ObservableCollection<ModelOption>();
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

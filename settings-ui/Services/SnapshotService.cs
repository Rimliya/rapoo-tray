using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text.RegularExpressions;

namespace RapooTray.Settings
{
    internal sealed class ProfileSnapshot
    {
        public string Path;
        public DateTime CreatedAt;
        public string Reason;
        public bool FullSettings;
        public string DisplayName => CreatedAt.ToString("yyyy-MM-dd HH:mm:ss") + "  //  " + Reason + (FullSettings ? "  /  SETTINGS" : "  /  PROFILES");
    }

    [DataContract]
    internal sealed class SnapshotOsdSettings
    {
        [DataMember(Order = 0)] public bool Enabled { get; set; }
        [DataMember(Order = 1)] public bool QuickPanelEnabled { get; set; }
        [DataMember(Order = 2)] public int HotkeyModifiers { get; set; }
        [DataMember(Order = 3)] public int HotkeyVirtualKey { get; set; }
        [DataMember(Order = 4)] public int DisplayFields { get; set; }
        [DataMember(Order = 5)] public int PanelItems { get; set; }
        [DataMember(Order = 6)] public int Position { get; set; }
        [DataMember(Order = 7)] public int Monitor { get; set; }
        [DataMember(Order = 8)] public int OffsetX { get; set; }
        [DataMember(Order = 9)] public int OffsetY { get; set; }
        [DataMember(Order = 10)] public int ScalePercent { get; set; }
        [DataMember(Order = 11)] public int OpacityPercent { get; set; }
        [DataMember(Order = 12)] public int DurationMs { get; set; }
        [DataMember(Order = 13)] public int ProfileHotkeyModifiers { get; set; }
        [DataMember(Order = 14)] public int ProfileHotkeyVirtualKey { get; set; }
        [DataMember(Order = 15)] public int DpiHotkeyModifiers { get; set; }
        [DataMember(Order = 16)] public int DpiHotkeyVirtualKey { get; set; }
        [DataMember(Order = 17)] public int PerformanceHotkeyModifiers { get; set; }
        [DataMember(Order = 18)] public int PerformanceHotkeyVirtualKey { get; set; }
        [DataMember(Order = 19)] public int ToggleOsdHotkeyModifiers { get; set; }
        [DataMember(Order = 20)] public int ToggleOsdHotkeyVirtualKey { get; set; }

        public OsdSettings ToOsdSettings()
        {
            return new OsdSettings
            {
                Enabled = Enabled,
                QuickPanelEnabled = QuickPanelEnabled,
                HotkeyModifiers = HotkeyModifiers,
                HotkeyVirtualKey = HotkeyVirtualKey,
                DisplayFields = DisplayFields,
                PanelItems = PanelItems,
                Position = Position,
                Monitor = Monitor,
                OffsetX = OffsetX,
                OffsetY = OffsetY,
                ScalePercent = ScalePercent,
                OpacityPercent = OpacityPercent,
                DurationMs = DurationMs,
                ProfileHotkeyModifiers = ProfileHotkeyModifiers,
                ProfileHotkeyVirtualKey = ProfileHotkeyVirtualKey,
                DpiHotkeyModifiers = DpiHotkeyModifiers,
                DpiHotkeyVirtualKey = DpiHotkeyVirtualKey,
                PerformanceHotkeyModifiers = PerformanceHotkeyModifiers,
                PerformanceHotkeyVirtualKey = PerformanceHotkeyVirtualKey,
                ToggleOsdHotkeyModifiers = ToggleOsdHotkeyModifiers,
                ToggleOsdHotkeyVirtualKey = ToggleOsdHotkeyVirtualKey
            };
        }
    }

    [DataContract]
    internal sealed class SnapshotBatterySettings
    {
        [DataMember(Order = 0)] public bool Enabled { get; set; }
        [DataMember(Order = 1)] public int WarningThreshold { get; set; }
        [DataMember(Order = 2)] public int CriticalThreshold { get; set; }
        [DataMember(Order = 3)] public bool ChargingReminderEnabled { get; set; }
        [DataMember(Order = 4)] public int ReminderMinutes { get; set; }
    }

    [DataContract]
    internal sealed class SettingsSnapshotPackage
    {
        public const string CurrentFormat = "rapoo-tray-settings-snapshot";
        public const int CurrentSchemaVersion = 1;

        [DataMember(Name = "format", Order = 0)] public string Format { get; set; }
        [DataMember(Name = "schemaVersion", Order = 1)] public int SchemaVersion { get; set; }
        [DataMember(Name = "appVersion", Order = 2)] public string AppVersion { get; set; }
        [DataMember(Name = "createdAt", Order = 3)] public string CreatedAt { get; set; }
        [DataMember(Name = "profiles", Order = 4)] public ProfilePackage Profiles { get; set; }
        [DataMember(Name = "osd", Order = 5)] public SnapshotOsdSettings Osd { get; set; }
        [DataMember(Name = "battery", Order = 6)] public SnapshotBatterySettings Battery { get; set; }
        [DataMember(Name = "autoRun", Order = 7)] public bool AutoRun { get; set; }
        [DataMember(Name = "modelIndex", Order = 8)] public int ModelIndex { get; set; }

        public bool NormalizeAndValidate(out string error)
        {
            error = "";
            if (!string.Equals(Format, CurrentFormat, StringComparison.Ordinal))
            {
                error = "不是完整设置快照";
                return false;
            }
            if (SchemaVersion != CurrentSchemaVersion)
            {
                error = "不支持的完整设置快照版本：" + SchemaVersion;
                return false;
            }
            if (Profiles == null)
            {
                error = "快照缺少配置";
                return false;
            }
            if (!Profiles.NormalizeAndValidate(out error)) return false;
            if (Osd == null)
            {
                error = "快照缺少 OSD 设置";
                return false;
            }
            if (Battery == null)
            {
                error = "快照缺少电量提醒设置";
                return false;
            }
            return true;
        }
    }

    internal static class SnapshotService
    {
        private const int MaxSnapshots = 20;
        private const long MaxSnapshotBytes = 4 * 1024 * 1024;

        public static string SnapshotDirectory => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rapoo-tray", "snapshots");

        public static ProfileSnapshot Create(AppState state, string reason)
        {
            if (state == null || state.Profiles == null || state.Profiles.Count == 0) return null;
            Directory.CreateDirectory(SnapshotDirectory);
            DateTime now = DateTime.Now;
            string safeReason = Regex.Replace(string.IsNullOrWhiteSpace(reason) ? "manual" : reason, @"[^0-9A-Za-z\u4e00-\u9fff_-]+", "-");
            if (safeReason.Length > 36) safeReason = safeReason.Substring(0, 36);
            string fileName = now.ToString("yyyyMMdd-HHmmss-fff") + "_" + safeReason + ".rapoo-snapshot";
            string path = Path.Combine(SnapshotDirectory, fileName);
            SettingsSnapshotPackage package = FromState(state, now);
            string validationError;
            if (!package.NormalizeAndValidate(out validationError)) throw new InvalidOperationException(validationError);
            SaveAtomic(path, package);
            Prune();
            return new ProfileSnapshot { Path = path, CreatedAt = now, Reason = reason, FullSettings = true };
        }

        public static List<ProfileSnapshot> List()
        {
            if (!Directory.Exists(SnapshotDirectory)) return new List<ProfileSnapshot>();
            var result = new List<ProfileSnapshot>();
            foreach (string path in Directory.GetFiles(SnapshotDirectory))
            {
                string extension = Path.GetExtension(path);
                if (!string.Equals(extension, ".rapoo-snapshot", StringComparison.OrdinalIgnoreCase) &&
                    !string.Equals(extension, ".rapoo", StringComparison.OrdinalIgnoreCase)) continue;
                var info = new FileInfo(path);
                string name = Path.GetFileNameWithoutExtension(path);
                int separator = name.IndexOf('_');
                string reason = separator >= 0 && separator + 1 < name.Length ? name.Substring(separator + 1) : "snapshot";
                result.Add(new ProfileSnapshot
                {
                    Path = path,
                    CreatedAt = info.LastWriteTime,
                    Reason = reason,
                    FullSettings = string.Equals(extension, ".rapoo-snapshot", StringComparison.OrdinalIgnoreCase)
                });
            }
            return result.OrderByDescending(item => item.CreatedAt).Take(MaxSnapshots).ToList();
        }

        public static SettingsSnapshotPackage Load(ProfileSnapshot snapshot)
        {
            if (snapshot == null) throw new ArgumentNullException(nameof(snapshot));
            var info = new FileInfo(snapshot.Path);
            if (!info.Exists) throw new FileNotFoundException("快照文件不存在", snapshot.Path);
            if (info.Length > MaxSnapshotBytes) throw new InvalidOperationException("快照文件超过 4 MB，拒绝恢复");
            byte[] data = File.ReadAllBytes(snapshot.Path);

            if (string.Equals(info.Extension, ".rapoo-snapshot", StringComparison.OrdinalIgnoreCase))
            {
                using (var stream = new MemoryStream(data))
                {
                    var serializer = new DataContractJsonSerializer(typeof(SettingsSnapshotPackage));
                    var package = serializer.ReadObject(stream) as SettingsSnapshotPackage;
                    if (package == null) throw new InvalidOperationException("快照内容为空");
                    string error;
                    if (!package.NormalizeAndValidate(out error)) throw new InvalidOperationException(error);
                    return package;
                }
            }

            // v1.16.0 and older profile-only snapshots remain restorable.
            ProfilePackage legacy = ProfilePackageFile.Load(snapshot.Path);
            return new SettingsSnapshotPackage
            {
                Format = SettingsSnapshotPackage.CurrentFormat,
                SchemaVersion = SettingsSnapshotPackage.CurrentSchemaVersion,
                AppVersion = legacy.AppVersion,
                CreatedAt = legacy.CreatedAt,
                Profiles = legacy,
                Osd = null,
                Battery = null,
                AutoRun = false,
                ModelIndex = 0
            };
        }

        private static SettingsSnapshotPackage FromState(AppState state, DateTime now)
        {
            return new SettingsSnapshotPackage
            {
                Format = SettingsSnapshotPackage.CurrentFormat,
                SchemaVersion = SettingsSnapshotPackage.CurrentSchemaVersion,
                AppVersion = state.Version,
                CreatedAt = now.ToString("O"),
                Profiles = ProfilePackageFile.FromState(state),
                Osd = new SnapshotOsdSettings
                {
                    Enabled = state.OsdEnabled,
                    QuickPanelEnabled = state.QuickPanelEnabled,
                    HotkeyModifiers = state.HotkeyModifiers,
                    HotkeyVirtualKey = state.HotkeyVirtualKey,
                    DisplayFields = state.OsdDisplayFields,
                    PanelItems = state.QuickPanelItems,
                    Position = state.OsdPosition,
                    Monitor = state.OsdMonitor,
                    OffsetX = state.OsdOffsetX,
                    OffsetY = state.OsdOffsetY,
                    ScalePercent = state.OsdScalePercent,
                    OpacityPercent = state.OsdOpacityPercent,
                    DurationMs = state.OsdDurationMs,
                    ProfileHotkeyModifiers = state.ProfileHotkeyModifiers,
                    ProfileHotkeyVirtualKey = state.ProfileHotkeyVirtualKey,
                    DpiHotkeyModifiers = state.DpiHotkeyModifiers,
                    DpiHotkeyVirtualKey = state.DpiHotkeyVirtualKey,
                    PerformanceHotkeyModifiers = state.PerformanceHotkeyModifiers,
                    PerformanceHotkeyVirtualKey = state.PerformanceHotkeyVirtualKey,
                    ToggleOsdHotkeyModifiers = state.ToggleOsdHotkeyModifiers,
                    ToggleOsdHotkeyVirtualKey = state.ToggleOsdHotkeyVirtualKey
                },
                Battery = new SnapshotBatterySettings
                {
                    Enabled = state.BatteryAlertsEnabled,
                    WarningThreshold = state.BatteryWarningThreshold,
                    CriticalThreshold = state.BatteryCriticalThreshold,
                    ChargingReminderEnabled = state.ChargingReminderEnabled,
                    ReminderMinutes = state.ChargingReminderMinutes
                },
                AutoRun = state.AutoRun,
                ModelIndex = state.ModelIndex
            };
        }

        private static void SaveAtomic(string path, SettingsSnapshotPackage package)
        {
            byte[] data;
            using (var stream = new MemoryStream())
            {
                var serializer = new DataContractJsonSerializer(typeof(SettingsSnapshotPackage));
                serializer.WriteObject(stream, package);
                data = stream.ToArray();
            }

            string temp = path + ".writing-" + Guid.NewGuid().ToString("N");
            File.WriteAllBytes(temp, data);
            try
            {
                if (File.Exists(path)) File.Delete(path);
                File.Move(temp, path);
            }
            finally
            {
                if (File.Exists(temp)) File.Delete(temp);
            }
        }

        private static void Prune()
        {
            List<string> files = Directory.GetFiles(SnapshotDirectory)
                .Where(path => string.Equals(Path.GetExtension(path), ".rapoo-snapshot", StringComparison.OrdinalIgnoreCase) ||
                               string.Equals(Path.GetExtension(path), ".rapoo", StringComparison.OrdinalIgnoreCase))
                .OrderByDescending(path => File.GetLastWriteTimeUtc(path))
                .ToList();
            foreach (string path in files.Skip(MaxSnapshots))
            {
                try { File.Delete(path); } catch { }
            }
        }
    }
}
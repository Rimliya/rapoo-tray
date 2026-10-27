using System;
using System.Collections.Generic;
using System.Runtime.Serialization;

namespace RapooTray.Settings
{
    [DataContract]
    public sealed class ProfilePackage
    {
        public const string CurrentFormat = "rapoo-tray-profiles";
        public const int CurrentSchemaVersion = 1;

        [DataMember(Name = "format", Order = 0)]
        public string Format { get; set; }

        [DataMember(Name = "schemaVersion", Order = 1)]
        public int SchemaVersion { get; set; }

        [DataMember(Name = "appVersion", Order = 2)]
        public string AppVersion { get; set; }

        [DataMember(Name = "createdAt", Order = 3)]
        public string CreatedAt { get; set; }

        [DataMember(Name = "autoSwitch", Order = 4)]
        public bool AutoSwitch { get; set; }

        [DataMember(Name = "defaultProfileIndex", Order = 5)]
        public int DefaultProfileIndex { get; set; }

        [DataMember(Name = "profiles", Order = 6)]
        public List<ProfilePackageEntry> Profiles { get; set; }

        public bool NormalizeAndValidate(out string error)
        {
            error = "";
            if (!string.Equals(Format, CurrentFormat, StringComparison.Ordinal))
            {
                error = "不是 rapoo-tray 配置包";
                return false;
            }
            if (SchemaVersion != CurrentSchemaVersion)
            {
                error = "配置包版本不受支持：" + SchemaVersion;
                return false;
            }
            if (Profiles == null || Profiles.Count < 1 || Profiles.Count > 16)
            {
                error = "配置包必须包含 1–16 个配置";
                return false;
            }
            if (DefaultProfileIndex < 0 || DefaultProfileIndex >= Profiles.Count)
            {
                error = "默认配置索引无效";
                return false;
            }

            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var profile in Profiles)
            {
                if (profile == null)
                {
                    error = "配置包包含空配置";
                    return false;
                }
                profile.Name = (profile.Name ?? "").Trim();
                profile.AppPath = (profile.AppPath ?? "").Trim();
                if (profile.Name.Length == 0 || profile.Name.Length > 64)
                {
                    error = "配置名称必须为 1–64 个字符";
                    return false;
                }
                if (!names.Add(profile.Name))
                {
                    error = "配置名称重复：" + profile.Name;
                    return false;
                }
                if (profile.PollingHz != 125 && profile.PollingHz != 250 && profile.PollingHz != 500 &&
                    profile.PollingHz != 1000 && profile.PollingHz != 2000 && profile.PollingHz != 4000 &&
                    profile.PollingHz != 8000)
                {
                    error = "配置“" + profile.Name + "”包含无效回报率";
                    return false;
                }
                if (profile.PerformanceMode < 0 || profile.PerformanceMode > 5)
                {
                    error = "配置“" + profile.Name + "”包含无效性能模式";
                    return false;
                }
                if (profile.AppPath.Length > 4096)
                {
                    error = "配置“" + profile.Name + "”的程序路径过长（上限 4096 字符）";
                    return false;
                }
                profile.SleepMinutes = Math.Max(5, Math.Min(60, profile.SleepMinutes));
                profile.DpiX = Math.Max(0, profile.DpiX);
                profile.DpiY = Math.Max(0, profile.DpiY);
            }
            return true;
        }
    }

    [DataContract]
    public sealed class ProfilePackageEntry
    {
        [DataMember(Name = "name", Order = 0)] public string Name { get; set; }
        [DataMember(Name = "pollingHz", Order = 1)] public int PollingHz { get; set; }
        [DataMember(Name = "performanceMode", Order = 2)] public int PerformanceMode { get; set; }
        [DataMember(Name = "sleepMinutes", Order = 3)] public int SleepMinutes { get; set; }
        [DataMember(Name = "dpiX", Order = 4)] public int DpiX { get; set; }
        [DataMember(Name = "dpiY", Order = 5)] public int DpiY { get; set; }
        [DataMember(Name = "appPath", Order = 6)] public string AppPath { get; set; }
    }
}

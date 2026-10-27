using System;
using System.IO;
using System.Runtime.Serialization.Json;

namespace RapooTray.Settings
{
    internal static class ProfilePackageFile
    {
        private const long MaxFileBytes = 2 * 1024 * 1024;

        public static ProfilePackage FromState(AppState state)
        {
            var package = new ProfilePackage
            {
                Format = ProfilePackage.CurrentFormat,
                SchemaVersion = ProfilePackage.CurrentSchemaVersion,
                AppVersion = state == null ? "" : state.Version,
                CreatedAt = DateTime.Now.ToString("O"),
                AutoSwitch = state != null && state.AutoSwitch,
                DefaultProfileIndex = 0,
                Profiles = new System.Collections.Generic.List<ProfilePackageEntry>()
            };
            if (state == null) return package;

            for (int i = 0; i < state.Profiles.Count; i++)
            {
                ProfileRow row = state.Profiles[i];
                if (row.IsDefault) package.DefaultProfileIndex = i;
                package.Profiles.Add(new ProfilePackageEntry
                {
                    Name = row.Name,
                    PollingHz = row.PollingHz,
                    PerformanceMode = row.PerformanceMode,
                    SleepMinutes = row.SleepMinutes,
                    DpiX = row.DpiX,
                    DpiY = row.DpiY,
                    AppPath = row.AppPath
                });
            }
            return package;
        }

        public static void SaveAtomic(string path, ProfilePackage package)
        {
            byte[] data = Serialize(package);
            string directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

            string temp = path + ".writing-" + Guid.NewGuid().ToString("N");
            File.WriteAllBytes(temp, data);
            try
            {
                if (File.Exists(path))
                {
                    string backup = path + ".previous";
                    try
                    {
                        File.Replace(temp, path, backup, true);
                        if (File.Exists(backup)) File.Delete(backup);
                    }
                    catch
                    {
                        File.Copy(temp, path, true);
                    }
                }
                else
                {
                    File.Move(temp, path);
                }
            }
            finally
            {
                if (File.Exists(temp)) File.Delete(temp);
            }
        }

        public static ProfilePackage Load(string path)
        {
            var info = new FileInfo(path);
            if (!info.Exists) throw new FileNotFoundException("配置文件不存在", path);
            if (info.Length > MaxFileBytes) throw new InvalidOperationException("配置文件超过 2 MB，拒绝导入");
            byte[] data = File.ReadAllBytes(path);
            using (var stream = new MemoryStream(data))
            {
                var serializer = new DataContractJsonSerializer(typeof(ProfilePackage));
                var package = serializer.ReadObject(stream) as ProfilePackage;
                if (package == null) throw new InvalidOperationException("配置文件内容为空");
                string error;
                if (!package.NormalizeAndValidate(out error)) throw new InvalidOperationException(error);
                return package;
            }
        }

        private static byte[] Serialize(ProfilePackage package)
        {
            using (var stream = new MemoryStream())
            {
                var serializer = new DataContractJsonSerializer(typeof(ProfilePackage));
                serializer.WriteObject(stream, package);
                return stream.ToArray();
            }
        }
    }
}

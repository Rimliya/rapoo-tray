using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace RapooTray.Settings
{
    internal sealed class ProfileSnapshot
    {
        public string Path;
        public DateTime CreatedAt;
        public string Reason;
        public string DisplayName => CreatedAt.ToString("yyyy-MM-dd HH:mm:ss") + "  //  " + Reason;
    }

    internal static class SnapshotService
    {
        private const int MaxSnapshots = 20;

        public static string SnapshotDirectory => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rapoo-tray", "snapshots");

        public static ProfileSnapshot Create(AppState state, string reason)
        {
            if (state == null || state.Profiles == null || state.Profiles.Count == 0) return null;
            Directory.CreateDirectory(SnapshotDirectory);
            DateTime now = DateTime.Now;
            string safeReason = Regex.Replace(string.IsNullOrWhiteSpace(reason) ? "manual" : reason, @"[^0-9A-Za-z\u4e00-\u9fff_-]+", "-");
            if (safeReason.Length > 36) safeReason = safeReason.Substring(0, 36);
            string fileName = now.ToString("yyyyMMdd-HHmmss-fff") + "_" + safeReason + ".rapoo";
            string path = Path.Combine(SnapshotDirectory, fileName);
            ProfilePackageFile.SaveAtomic(path, ProfilePackageFile.FromState(state));
            Prune();
            return new ProfileSnapshot { Path = path, CreatedAt = now, Reason = reason };
        }

        public static List<ProfileSnapshot> List()
        {
            if (!Directory.Exists(SnapshotDirectory)) return new List<ProfileSnapshot>();
            return Directory.GetFiles(SnapshotDirectory, "*.rapoo")
                .Select(path =>
                {
                    var info = new FileInfo(path);
                    string name = Path.GetFileNameWithoutExtension(path);
                    int separator = name.IndexOf('_');
                    string reason = separator >= 0 && separator + 1 < name.Length ? name.Substring(separator + 1) : "snapshot";
                    return new ProfileSnapshot { Path = path, CreatedAt = info.LastWriteTime, Reason = reason };
                })
                .OrderByDescending(item => item.CreatedAt)
                .Take(MaxSnapshots)
                .ToList();
        }

        public static ProfilePackage Load(ProfileSnapshot snapshot)
        {
            if (snapshot == null) throw new ArgumentNullException(nameof(snapshot));
            return ProfilePackageFile.Load(snapshot.Path);
        }

        private static void Prune()
        {
            List<string> files = Directory.GetFiles(SnapshotDirectory, "*.rapoo")
                .OrderByDescending(path => File.GetLastWriteTimeUtc(path))
                .ToList();
            foreach (string path in files.Skip(MaxSnapshots))
            {
                try { File.Delete(path); } catch { }
            }
        }
    }
}
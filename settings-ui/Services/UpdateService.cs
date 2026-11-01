using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace RapooTray.Settings
{
    [DataContract]
    internal sealed class GitHubReleaseInfo
    {
        [DataMember(Name = "tag_name")] public string TagName { get; set; }
        [DataMember(Name = "html_url")] public string HtmlUrl { get; set; }
        [DataMember(Name = "assets")] public List<GitHubReleaseAsset> Assets { get; set; } = new List<GitHubReleaseAsset>();
    }

    [DataContract]
    internal sealed class GitHubReleaseAsset
    {
        [DataMember(Name = "name")] public string Name { get; set; }
        [DataMember(Name = "browser_download_url")] public string BrowserDownloadUrl { get; set; }
        [DataMember(Name = "size")] public long Size { get; set; }
    }

    internal sealed class VerifiedUpdate
    {
        public string InstallerPath;
        public string Publisher;
        public string Sha256;
        public bool Signed;
    }

    internal static class UpdateService
    {
        private const string LatestReleaseApi = "https://api.github.com/repos/Rimliya/rapoo-tray/releases/latest";
        private const string SetupAssetName = "rapoo-tray-setup.exe";
        private const string SetupHashAssetName = "rapoo-tray-setup.exe.sha256";

        public static async Task<GitHubReleaseInfo> GetLatestReleaseAsync(HttpClient client)
        {
            using (var request = new HttpRequestMessage(HttpMethod.Get, LatestReleaseApi))
            {
                request.Headers.Accept.ParseAdd("application/vnd.github+json");
                request.Headers.UserAgent.ParseAdd("rapoo-tray-settings/1.16.0");
                using (HttpResponseMessage response = await client.SendAsync(request).ConfigureAwait(false))
                {
                    response.EnsureSuccessStatusCode();
                    using (Stream stream = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                    {
                        var serializer = new DataContractJsonSerializer(typeof(GitHubReleaseInfo));
                        var release = serializer.ReadObject(stream) as GitHubReleaseInfo;
                        if (release == null || string.IsNullOrWhiteSpace(release.TagName))
                            throw new InvalidOperationException("GitHub 返回的 Release 信息无效。");
                        if (release.Assets == null) release.Assets = new List<GitHubReleaseAsset>();
                        return release;
                    }
                }
            }
        }

        public static async Task<VerifiedUpdate> DownloadAndVerifyAsync(HttpClient client,
            GitHubReleaseInfo release, IProgress<string> progress)
        {
            GitHubReleaseAsset setup = FindAsset(release, SetupAssetName);
            GitHubReleaseAsset hashAsset = FindAsset(release, SetupHashAssetName);
            if (setup == null || hashAsset == null)
                throw new InvalidOperationException("Release 缺少安装包或 .sha256 校验文件。");
            if (setup.Size <= 0 || setup.Size > 200L * 1024 * 1024)
                throw new InvalidOperationException("安装包大小异常，已拒绝下载。");

            string updateDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "rapoo-tray", "updates");
            Directory.CreateDirectory(updateDir);
            string installerPath = Path.Combine(updateDir, "rapoo-tray-setup-" + SafeTag(release.TagName) + ".exe");
            string hashPath = installerPath + ".sha256";
            string tempPath = installerPath + ".downloading";
            var downloadClient = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
            downloadClient.DefaultRequestHeaders.UserAgent.ParseAdd("rapoo-tray-settings/1.16.0");

            try
            {
                progress?.Report("正在下载校验文件…");
                string hashText = await downloadClient.GetStringAsync(hashAsset.BrowserDownloadUrl).ConfigureAwait(false);
                File.WriteAllText(hashPath, hashText);

                progress?.Report("正在下载安装包…");
                using (HttpResponseMessage response = await downloadClient.GetAsync(
                    setup.BrowserDownloadUrl, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false))
                {
                    response.EnsureSuccessStatusCode();
                    long total = response.Content.Headers.ContentLength ?? setup.Size;
                    using (Stream input = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                    using (FileStream output = new FileStream(tempPath, FileMode.Create, FileAccess.Write, FileShare.None,
                        128 * 1024, true))
                    {
                        var buffer = new byte[128 * 1024];
                        long received = 0;
                        int read;
                        while ((read = await input.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false)) > 0)
                        {
                            await output.WriteAsync(buffer, 0, read).ConfigureAwait(false);
                            received += read;
                            if (total > 0)
                                progress?.Report("正在下载安装包… " + (received * 100 / total) + "%");
                        }
                    }
                }

                progress?.Report("正在校验 SHA256…");
                string expectedHash = ReadFirstHash(hashText);
                string actualHash;
                using (var sha = SHA256.Create())
                using (FileStream stream = File.OpenRead(tempPath))
                    actualHash = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "");
                if (!string.Equals(expectedHash, actualHash, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("安装包 SHA256 校验失败，已删除下载文件。");

                progress?.Report("正在验证文件签名…");
                bool signed = WinTrust.VerifyFile(tempPath, out string publisher);
                if (!signed) publisher = "未签名（仅 SHA256 校验）";

                if (File.Exists(installerPath)) File.Delete(installerPath);
                File.Move(tempPath, installerPath);
                return new VerifiedUpdate
                {
                    InstallerPath = installerPath,
                    Publisher = publisher,
                    Sha256 = actualHash,
                    Signed = signed
                };
            }
            finally
            {
                downloadClient.Dispose();
                if (File.Exists(tempPath)) File.Delete(tempPath);
            }
        }

        private static GitHubReleaseAsset FindAsset(GitHubReleaseInfo release, string name)
        {
            if (release == null || release.Assets == null) return null;
            foreach (GitHubReleaseAsset asset in release.Assets)
                if (string.Equals(asset.Name, name, StringComparison.OrdinalIgnoreCase)) return asset;
            return null;
        }

        private static string SafeTag(string tag)
        {
            return Regex.Replace(tag ?? "update", "[^0-9A-Za-z._-]", "_");
        }

        private static string ReadFirstHash(string text)
        {
            Match match = Regex.Match(text ?? "", "[0-9A-Fa-f]{64}");
            if (!match.Success) throw new InvalidOperationException("SHA256 校验文件格式无效。");
            return match.Value;
        }
    }

    internal static class WinTrust
    {
        private static readonly Guid GenericVerifyV2 = new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private sealed class WINTRUST_FILE_INFO
        {
            public uint cbStruct = (uint)Marshal.SizeOf(typeof(WINTRUST_FILE_INFO));
            public string pcwszFilePath;
            public IntPtr hFile = IntPtr.Zero;
            public IntPtr pgKnownSubject = IntPtr.Zero;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private sealed class WINTRUST_DATA
        {
            public uint cbStruct = (uint)Marshal.SizeOf(typeof(WINTRUST_DATA));
            public IntPtr pPolicyCallbackData = IntPtr.Zero;
            public IntPtr pSIPClientData = IntPtr.Zero;
            public uint dwUIChoice = 2;
            public uint fdwRevocationChecks = 0;
            public uint dwUnionChoice = 1;
            public IntPtr pFile;
            public uint dwStateAction = 0;
            public IntPtr hWVTStateData = IntPtr.Zero;
            public IntPtr pwszURLReference = IntPtr.Zero;
            public uint dwProvFlags = 0x00000010;
            public uint dwUIContext = 0;
            public IntPtr pSignatureSettings = IntPtr.Zero;
        }

        [DllImport("wintrust.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern uint WinVerifyTrust(IntPtr hwnd, [MarshalAs(UnmanagedType.LPStruct)] Guid actionId, IntPtr data);

        public static bool VerifyFile(string path, out string publisher)
        {
            publisher = "";
            var file = new WINTRUST_FILE_INFO { pcwszFilePath = path };
            IntPtr filePtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WINTRUST_FILE_INFO)));
            try
            {
                Marshal.StructureToPtr(file, filePtr, false);
                var data = new WINTRUST_DATA { pFile = filePtr };
                IntPtr dataPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WINTRUST_DATA)));
                try
                {
                    Marshal.StructureToPtr(data, dataPtr, false);
                    uint result = WinVerifyTrust(IntPtr.Zero, GenericVerifyV2, dataPtr);
                    if (result != 0) return false;
                    try
                    {
                        var cert = new X509Certificate2(X509Certificate.CreateFromSignedFile(path));
                        publisher = cert.GetNameInfo(X509NameType.SimpleName, false);
                    }
                    catch { publisher = "已验证发布者"; }
                    return true;
                }
                finally { Marshal.FreeHGlobal(dataPtr); }
            }
            finally { Marshal.FreeHGlobal(filePtr); }
        }
    }
}

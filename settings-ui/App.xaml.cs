using System;
using System.Threading;
using System.Net;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using Wpf.Ui.Appearance;
using System.Runtime.InteropServices;

namespace RapooTray.Settings
{
    public partial class App : Application
    {
        internal const int ActivateMessageId = 0x8001;
        private static readonly int ActivateMessage = NativeMethods.RegisterWindowMessage("RapooTraySettingsActivate");
        private Mutex _instanceMutex;

        protected override void OnStartup(StartupEventArgs e)
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            base.OnStartup(e);
            ApplicationThemeManager.Changed += OnApplicationThemeChanged;
            ApplyReadabilityPalette(ApplicationThemeManager.GetAppTheme());

            int page = 0;
            bool demo = false;
            for (int i = 0; i < e.Args.Length; i++)
            {
                if (string.Equals(e.Args[i], "--demo", StringComparison.OrdinalIgnoreCase)) demo = true;
                if (string.Equals(e.Args[i], "--page", StringComparison.OrdinalIgnoreCase) && i + 1 < e.Args.Length)
                {
                    int.TryParse(e.Args[++i], out page);
                }
            }

            bool createdNew;
            _instanceMutex = new Mutex(true, @"Local\rapoo-tray-settings-ui-v1", out createdNew);
            if (!createdNew)
            {
                IntPtr existing = NativeMethods.FindWindow(null, "rapoo-tray 设置");
                if (existing != IntPtr.Zero)
                {
                    NativeMethods.ShowWindow(existing, 9);
                    NativeMethods.SetForegroundWindow(existing);
                    NativeMethods.PostMessage(existing, ActivateMessage, new IntPtr(page), IntPtr.Zero);
                }
                Shutdown();
                return;
            }

            var window = new MainWindow(page, demo);
            MainWindow = window;
            window.Show();
        }

        private void OnApplicationThemeChanged(ApplicationTheme theme, Color accent)
        {
            ApplyReadabilityPalette(theme);
        }

        private static void ApplyReadabilityPalette(ApplicationTheme theme)
        {
            bool dark = theme == ApplicationTheme.Dark;
            var resources = Current.Resources;
            resources["AppSecondaryTextBrush"] = new SolidColorBrush(dark ? Color.FromRgb(215, 218, 223) : Color.FromRgb(77, 82, 90));
            resources["AppTertiaryTextBrush"] = new SolidColorBrush(dark ? Color.FromRgb(178, 184, 193) : Color.FromRgb(103, 108, 117));
            resources["AppCardSurfaceBrush"] = new SolidColorBrush(dark ? Color.FromRgb(37, 39, 44) : Color.FromRgb(252, 252, 253));
            resources["AppSoftAccentSurfaceBrush"] = new SolidColorBrush(dark ? Color.FromRgb(44, 52, 67) : Color.FromRgb(243, 247, 255));
            resources["AppCardStrokeBrush"] = new SolidColorBrush(dark ? Color.FromArgb(58, 255, 255, 255) : Color.FromArgb(38, 0, 0, 0));
            resources["AppSubtleSurfaceBrush"] = new SolidColorBrush(dark ? Color.FromArgb(28, 255, 255, 255) : Color.FromArgb(16, 0, 0, 0));
        }

        protected override void OnExit(ExitEventArgs e)
        {
            ApplicationThemeManager.Changed -= OnApplicationThemeChanged;
            if (_instanceMutex != null)
            {
                try { _instanceMutex.ReleaseMutex(); } catch { }
                _instanceMutex.Dispose();
            }
            base.OnExit(e);
        }

        internal static int ActivateMessageValue => ActivateMessage;
    }

    internal static class NativeMethods
    {
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern IntPtr FindWindow(string className, string windowName);

        [DllImport("user32.dll")]
        internal static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [DllImport("user32.dll")]
        internal static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        internal static extern bool PostMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        internal static extern int RegisterWindowMessage(string message);
    }
}

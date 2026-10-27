using System;
using System.Threading;
using System.Net;
using System.Windows;
using System.Windows.Interop;
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

        protected override void OnExit(ExitEventArgs e)
        {
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

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

            Color paper = dark ? Color.FromRgb(23, 26, 28) : Color.FromRgb(240, 243, 245);
            Color panel = dark ? Color.FromRgb(31, 35, 38) : Color.FromRgb(255, 255, 255);
            Color ink = dark ? Color.FromRgb(237, 237, 237) : Color.FromRgb(22, 22, 22);
            Color muted = dark ? Color.FromRgb(163, 163, 163) : Color.FromRgb(92, 92, 92);
            Color tertiary = dark ? Color.FromRgb(140, 140, 140) : Color.FromRgb(112, 112, 112);
            Color rule = dark ? Color.FromRgb(50, 58, 63) : Color.FromRgb(211, 218, 223);
            Color chip = dark ? Color.FromRgb(42, 48, 52) : Color.FromRgb(224, 230, 234);
            Color signal = Color.FromRgb(125, 230, 255);
            Color onSignal = Color.FromRgb(22, 22, 22);
            Color trace = dark ? signal : ink;
            Color danger = dark ? Color.FromRgb(255, 138, 122) : Color.FromRgb(179, 38, 30);
            Color commandBar = Color.FromRgb(22, 22, 22);
            Color commandText = Color.FromRgb(237, 237, 237);

            resources["AppPaperBrush"] = new SolidColorBrush(paper);
            resources["AppPanelBrush"] = new SolidColorBrush(panel);
            resources["AppInkBrush"] = new SolidColorBrush(ink);
            resources["AppSecondaryTextBrush"] = new SolidColorBrush(muted);
            resources["AppTertiaryTextBrush"] = new SolidColorBrush(tertiary);
            resources["AppRuleBrush"] = new SolidColorBrush(rule);
            resources["AppChipBrush"] = new SolidColorBrush(chip);
            resources["AppSignalBrush"] = new SolidColorBrush(signal);
            resources["AppOnSignalBrush"] = new SolidColorBrush(onSignal);
            resources["AppTraceBrush"] = new SolidColorBrush(trace);
            resources["AppDangerBrush"] = new SolidColorBrush(danger);
            resources["AppCommandBarBrush"] = new SolidColorBrush(commandBar);
            resources["AppCommandBarTextBrush"] = new SolidColorBrush(commandText);

            resources["AppCardSurfaceBrush"] = new SolidColorBrush(panel);
            resources["AppCardStrokeBrush"] = new SolidColorBrush(rule);
            resources["AppSoftAccentSurfaceBrush"] = new SolidColorBrush(signal);
            resources["AppSubtleSurfaceBrush"] = new SolidColorBrush(chip);
            resources["AppHatchBrush"] = CreateHatchBrush(ink);
            resources["AppGridBrush"] = CreateGridBrush(ink);

            resources["TextControlBackground"] = new SolidColorBrush(panel);
            resources["TextControlBackgroundFocused"] = new SolidColorBrush(panel);
            resources["TextControlForeground"] = new SolidColorBrush(ink);
            resources["TextControlPlaceholderForeground"] = new SolidColorBrush(muted);
            resources["TextControlElevationBorderBrush"] = new SolidColorBrush(rule);
            resources["TextControlFocusedBorderBrush"] = new SolidColorBrush(trace);
            resources["ComboBoxBorderBrushFocused"] = new SolidColorBrush(trace);
            resources["ControlStrokeColorDefaultBrush"] = new SolidColorBrush(rule);

            resources["SystemAccentColor"] = trace;
            resources["SystemAccentColorPrimary"] = trace;
            resources["SystemAccentColorSecondary"] = trace;
            resources["SystemAccentColorTertiary"] = trace;
            resources["SystemAccentColorBrush"] = new SolidColorBrush(trace);
            resources["SystemAccentColorPrimaryBrush"] = new SolidColorBrush(trace);
            resources["SystemAccentColorSecondaryBrush"] = new SolidColorBrush(trace);
            resources["SystemAccentColorTertiaryBrush"] = new SolidColorBrush(trace);
            resources["SystemFillColorAttentionBrush"] = new SolidColorBrush(signal);

            resources["AccentFillColorDefault"] = signal;
            resources["AccentFillColorSecondary"] = signal;
            resources["AccentFillColorTertiary"] = signal;
            resources["AccentFillColorDefaultBrush"] = new SolidColorBrush(signal);
            resources["AccentFillColorSecondaryBrush"] = new SolidColorBrush(signal);
            resources["AccentFillColorTertiaryBrush"] = new SolidColorBrush(signal);
            resources["AccentFillColorSelectedTextBackgroundBrush"] = new SolidColorBrush(signal);
            resources["AccentTextFillColorPrimaryBrush"] = new SolidColorBrush(trace);
            resources["AccentTextFillColorSecondaryBrush"] = new SolidColorBrush(trace);
            resources["AccentTextFillColorTertiaryBrush"] = new SolidColorBrush(trace);
            resources["TextOnAccentFillColorPrimary"] = onSignal;
            resources["TextOnAccentFillColorSecondary"] = onSignal;
            resources["TextOnAccentFillColorSelectedText"] = onSignal;
            resources["TextOnAccentFillColorPrimaryBrush"] = new SolidColorBrush(onSignal);
            resources["TextOnAccentFillColorSecondaryBrush"] = new SolidColorBrush(onSignal);
            resources["TextOnAccentFillColorSelectedTextBrush"] = new SolidColorBrush(onSignal);
        }

        private static DrawingBrush CreateHatchBrush(Color ink)
        {
            Color hatchInk = Color.FromArgb(56, ink.R, ink.G, ink.B);
            var group = new GeometryGroup();
            group.Children.Add(new LineGeometry(new Point(0, 9.2), new Point(9.2, 0)));
            group.Children.Add(new LineGeometry(new Point(-2.3, 2.3), new Point(2.3, -2.3)));
            group.Children.Add(new LineGeometry(new Point(6.9, 11.5), new Point(11.5, 6.9)));
            var drawing = new GeometryDrawing(null, new Pen(new SolidColorBrush(hatchInk), 1.5), group);
            var brush = new DrawingBrush(drawing)
            {
                TileMode = TileMode.Tile,
                Viewport = new Rect(0, 0, 9.2, 9.2),
                ViewportUnits = BrushMappingMode.Absolute,
                Stretch = Stretch.None
            };
            brush.Freeze();
            return brush;
        }

        private static DrawingBrush CreateGridBrush(Color ink)
        {
            Color gridInk = Color.FromArgb(18, ink.R, ink.G, ink.B);
            var group = new GeometryGroup();
            group.Children.Add(new LineGeometry(new Point(0, 0), new Point(32, 0)));
            group.Children.Add(new LineGeometry(new Point(0, 0), new Point(0, 32)));
            var drawing = new GeometryDrawing(null, new Pen(new SolidColorBrush(gridInk), 1), group);
            var brush = new DrawingBrush(drawing)
            {
                TileMode = TileMode.Tile,
                Viewport = new Rect(0, 0, 32, 32),
                ViewportUnits = BrushMappingMode.Absolute,
                Stretch = Stretch.None
            };
            brush.Freeze();
            return brush;
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

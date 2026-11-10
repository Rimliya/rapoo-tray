using System;
using System.Reflection;
using RapooTray.Settings;

internal static class SettingsLogicTests
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void CheckSettings(AppState state, OsdSettings expected)
    {
        Check(state.OsdEnabled == expected.Enabled, "OSD enabled");
        Check(state.QuickPanelEnabled == expected.QuickPanelEnabled, "quick panel enabled");
        string[] settingsNames = { "HotkeyModifiers", "HotkeyVirtualKey", "DisplayFields", "PanelItems",
            "Position", "Monitor", "OffsetX", "OffsetY", "ScalePercent", "OpacityPercent", "DurationMs",
            "ProfileHotkeyModifiers", "ProfileHotkeyVirtualKey", "DpiHotkeyModifiers", "DpiHotkeyVirtualKey",
            "PerformanceHotkeyModifiers", "PerformanceHotkeyVirtualKey", "ToggleOsdHotkeyModifiers", "ToggleOsdHotkeyVirtualKey" };
        string[] stateNames = { "HotkeyModifiers", "HotkeyVirtualKey", "OsdDisplayFields", "QuickPanelItems",
            "OsdPosition", "OsdMonitor", "OsdOffsetX", "OsdOffsetY", "OsdScalePercent", "OsdOpacityPercent", "OsdDurationMs",
            "ProfileHotkeyModifiers", "ProfileHotkeyVirtualKey", "DpiHotkeyModifiers", "DpiHotkeyVirtualKey",
            "PerformanceHotkeyModifiers", "PerformanceHotkeyVirtualKey", "ToggleOsdHotkeyModifiers", "ToggleOsdHotkeyVirtualKey" };
        for (int i = 0; i < settingsNames.Length; i++)
            Check(Equals(typeof(AppState).GetField(stateNames[i]).GetValue(state),
                typeof(OsdSettings).GetField(settingsNames[i]).GetValue(expected)), stateNames[i] + " did not round-trip");
    }

    private static void Main()
    {
        var expected = new OsdSettings {
            Enabled = true, QuickPanelEnabled = false, HotkeyModifiers = 2, HotkeyVirtualKey = 120,
            DisplayFields = 37, PanelItems = 19, Position = 3, Monitor = 1, OffsetX = -123, OffsetY = 87,
            ScalePercent = 125, OpacityPercent = 73, DurationMs = 4700,
            ProfileHotkeyModifiers = 4, ProfileHotkeyVirtualKey = 121,
            DpiHotkeyModifiers = 6, DpiHotkeyVirtualKey = 122,
            PerformanceHotkeyModifiers = 8, PerformanceHotkeyVirtualKey = 123,
            ToggleOsdHotkeyModifiers = 0, ToggleOsdHotkeyVirtualKey = 0
        };
        // Real backend field order, including a failed hotkey registration bit.
        const string wire = "OK\nOSD|1|0|2|120|1|37|19|3|1|-123|87|125|73|4700|4|121|6|122|8|123|0|0|13\nEND\n";
        var state = (AppState)typeof(TrayBridge).GetMethod("ParseState", BindingFlags.NonPublic | BindingFlags.Static)
            .Invoke(null, new object[] { wire });
        CheckSettings(state, expected);
        Check(state.HotkeyRegistrationMask == 13, "hotkey failures must reach the UI");

        var snapshot = (SettingsSnapshotPackage)typeof(SnapshotService)
            .GetMethod("FromState", BindingFlags.NonPublic | BindingFlags.Static)
            .Invoke(null, new object[] { state, DateTime.Now });
        var restored = new TrayBridge(true).SetOsdConfigAsync(snapshot.Osd.ToOsdSettings()).GetAwaiter().GetResult();
        CheckSettings(restored, expected);

        var demo = new TrayBridge(true);
        CheckSettings(demo.SetOsdConfigAsync(expected).GetAwaiter().GetResult(), expected);
        CheckSettings(demo.GetStateAsync().GetAwaiter().GetResult(), expected);
        Console.WriteLine("[PASS] settings protocol, snapshot and demo round-trip");
    }
}

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading.Tasks;

namespace RapooTray.Settings
{
    public sealed class TrayBridge
    {
        private const string PipeName = "rapoo-tray-ui-v1";
        private readonly bool _demo;

        public TrayBridge(bool demo) { _demo = demo; }

        public Task<AppState> GetStateAsync() => SendStateCommandAsync("GET_STATE");
        public Task<AppState> AddProfileAsync() => SendStateCommandAsync("ADD_PROFILE");
        public Task<AppState> DeleteProfileAsync(int id) => SendStateCommandAsync("DELETE_PROFILE|" + id);
        public Task<AppState> ApplyProfileAsync(int id) => SendStateCommandAsync("APPLY_PROFILE|" + id);

        public Task<AppState> SaveProfileAsync(ProfileRow row, bool setDefault)
        {
            string command = string.Join("|", new[]
            {
                "SAVE_PROFILE", row.Id.ToString(CultureInfo.InvariantCulture), Encode(row.Name),
                row.PollingHz.ToString(CultureInfo.InvariantCulture), row.PerformanceMode.ToString(CultureInfo.InvariantCulture),
                row.SleepMinutes.ToString(CultureInfo.InvariantCulture), row.DpiX.ToString(CultureInfo.InvariantCulture),
                row.DpiY.ToString(CultureInfo.InvariantCulture), Encode(row.AppPath), setDefault ? "1" : "0"
            });
            return SendStateCommandAsync(command);
        }

        public Task<AppState> SetAutoSwitchAsync(bool value) => SendStateCommandAsync("SET_AUTO_SWITCH|" + (value ? "1" : "0"));
        public Task<AppState> SetOsdAsync(bool value) => SendStateCommandAsync("SET_OSD|" + (value ? "1" : "0"));
        public Task<AppState> SetAutoRunAsync(bool value) => SendStateCommandAsync("SET_AUTORUN|" + (value ? "1" : "0"));
        public Task<AppState> SetRfFullAsync(bool value) => SendStateCommandAsync("SET_RF_FULL|" + (value ? "1" : "0"));
        public Task<AppState> SetModelAsync(int index) => SendStateCommandAsync("SET_MODEL|" + index);

        public async Task<DpiState> GetDpiStateAsync()
        {
            if (_demo) { await Task.Delay(90).ConfigureAwait(false); return DemoDpi(); }
            string response = await SendRawAsync("GET_DPI").ConfigureAwait(false);
            return ParseDpi(response);
        }

        public async Task<DpiState> ApplyDpiAsync(DpiState state, int selectedIndex)
        {
            if (_demo)
            {
                await Task.Delay(180).ConfigureAwait(false);
                var copy = DemoDpi();
                copy.SelectedIndex = selectedIndex;
                copy.ActiveIndex = selectedIndex;
                foreach (var slot in copy.Slots) slot.IsActive = slot.Index == selectedIndex;
                return copy;
            }

            var parts = new List<string> { "DPI_APPLY", selectedIndex.ToString(CultureInfo.InvariantCulture), state.Slots.Count.ToString(CultureInfo.InvariantCulture) };
            foreach (var slot in state.Slots)
            {
                parts.Add(slot.X.ToString(CultureInfo.InvariantCulture));
                parts.Add(slot.Y.ToString(CultureInfo.InvariantCulture));
            }
            string response = await SendRawAsync(string.Join("|", parts)).ConfigureAwait(false);
            return ParseDpi(response);
        }

        private async Task<AppState> SendStateCommandAsync(string command)
        {
            if (_demo) { await Task.Delay(90).ConfigureAwait(false); return DemoState(); }
            string response = await SendRawAsync(command).ConfigureAwait(false);
            return ParseState(response);
        }

        private async Task<string> SendRawAsync(string command)
        {
            Exception lastError = null;
            for (int attempt = 0; attempt < 2; attempt++)
            {
                try
                {
                    return await Task.Run(() =>
                    {
                        using (var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.None))
                        {
                            pipe.Connect(2200);
                            byte[] request = Encoding.UTF8.GetBytes(command);
                            pipe.Write(request, 0, request.Length);
                            pipe.Flush();
                            using (var output = new MemoryStream())
                            {
                                byte[] buffer = new byte[65536];
                                int read;
                                while ((read = pipe.Read(buffer, 0, buffer.Length)) > 0) output.Write(buffer, 0, read);
                                return Encoding.UTF8.GetString(output.ToArray());
                            }
                        }
                    }).ConfigureAwait(false);
                }
                catch (Exception ex)
                {
                    lastError = ex;
                    await Task.Delay(180).ConfigureAwait(false);
                }
            }
            throw new InvalidOperationException("无法连接 rapoo-tray 托盘程序。请确认托盘程序正在运行。", lastError);
        }

        private static AppState ParseState(string text)
        {
            var state = new AppState();
            string[] lines = (text ?? "").Replace("\r", "").Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
            if (lines.Length == 0 || lines[0] != "OK") throw new InvalidOperationException("托盘程序返回了无效状态。");
            state.Profiles.Clear();
            state.Models.Clear();
            foreach (string line in lines)
            {
                string[] f = line.Split('|');
                switch (f[0])
                {
                    case "STATUS": state.StatusGood = Field(f, 1) == "1"; state.StatusText = Decode(Field(f, 2)); break;
                    case "APP":
                        state.Version = Decode(Field(f, 1)); state.DeviceName = Decode(Field(f, 2)); state.DeviceMode = Decode(Field(f, 3));
                        state.Connected = Field(f, 4) == "1"; state.Battery = Int(Field(f, 5)); state.DeviceDpiX = Int(Field(f, 6)); state.DeviceDpiY = Int(Field(f, 7));
                        state.ModelIndex = Int(Field(f, 8)); state.AutoSwitch = Field(f, 9) == "1"; state.OsdEnabled = Field(f, 10) == "1";
                        state.AutoRun = Field(f, 11) == "1"; state.RfAvailable = Field(f, 12) == "1"; state.RfFull = Field(f, 13) == "1";
                        state.ActiveProfileId = Int(Field(f, 14)); break;
                    case "PROFILE":
                        state.Profiles.Add(new ProfileRow {
                            Id = Int(Field(f, 1)), Name = Decode(Field(f, 2)), PollingHz = Int(Field(f, 3)), PerformanceMode = Int(Field(f, 4)),
                            PerformanceLabel = Decode(Field(f, 5)), DpiText = Decode(Field(f, 6)), AppName = Decode(Field(f, 7)), Badge = Decode(Field(f, 8)),
                            IsDefault = Field(f, 9) == "1", SleepMinutes = Int(Field(f, 10)), DpiX = Int(Field(f, 11)), DpiY = Int(Field(f, 12)), AppPath = Decode(Field(f, 13))
                        }); break;
                    case "MODEL": state.Models.Add(new ModelOption { Index = Int(Field(f, 1)), Key = Decode(Field(f, 2)), Label = Decode(Field(f, 3)) }); break;
                    case "CREATED": state.CreatedProfileId = Int(Field(f, 1)); break;
                }
            }
            return state;
        }

        private static DpiState ParseDpi(string text)
        {
            var state = new DpiState(); state.Slots.Clear();
            string[] lines = (text ?? "").Replace("\r", "").Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
            if (lines.Length == 0 || lines[0] != "OK") throw new InvalidOperationException("托盘程序返回了无效 DPI 状态。");
            int index = 0;
            foreach (string line in lines)
            {
                string[] f = line.Split('|');
                if (f[0] == "DPI")
                {
                    state.SelectedIndex = Int(Field(f, 1)); state.ActiveIndex = Int(Field(f, 2)); state.Min = Int(Field(f, 3)); state.Max = Int(Field(f, 4));
                    state.Step = Int(Field(f, 5)); state.MaxSlots = Int(Field(f, 6)); state.FromDevice = Field(f, 7) == "1"; state.ModelLabel = Decode(Field(f, 8));
                }
                else if (f[0] == "GEAR")
                {
                    state.Slots.Add(new DpiSlotRow { Index = index++, X = Int(Field(f, 2)), Y = Int(Field(f, 3)), IsActive = Field(f, 4) == "1" });
                }
            }
            return state;
        }

        private static string Field(string[] fields, int index) => index < fields.Length ? fields[index] : "";
        private static int Int(string value) { int parsed; int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed); return parsed; }
        private static string Encode(string value) => Convert.ToBase64String(Encoding.UTF8.GetBytes(value ?? ""));
        private static string Decode(string value) { try { return Encoding.UTF8.GetString(Convert.FromBase64String(value ?? "")); } catch { return ""; } }

        private static AppState DemoState()
        {
            var state = new AppState { Version = "1.9.0", DeviceName = "雷柏 VT3 V2", DeviceMode = "2.4G 无线", Connected = true,
                Battery = 87, DeviceDpiX = 1600, DeviceDpiY = 1600, AutoSwitch = true, OsdEnabled = true, AutoRun = true,
                RfAvailable = true, RfFull = true, ActiveProfileId = 1, StatusText = "已读取设备状态，修改后点击“应用”写入鼠标", StatusGood = true };
            state.Models.Add(new ModelOption { Index = 0, Key = "vt3v2", Label = "雷柏 VT3 V2" });
            state.Models.Add(new ModelOption { Index = 1, Key = "vt7", Label = "雷柏 VT7" });
            state.Models.Add(new ModelOption { Index = 2, Key = "vt3max", Label = "雷柏 VT3 MAX" });
            state.Models.Add(new ModelOption { Index = 3, Key = "generic", Label = "通用鼠标" });
            state.Profiles.Add(new ProfileRow { Id = 1, Name = "日常", PollingHz = 1000, PerformanceMode = 1, PerformanceLabel = "均衡模式", DpiText = "1600", AppName = "", Badge = "当前", IsDefault = true, SleepMinutes = 10, DpiX = 1600, DpiY = 1600, AppPath = "" });
            state.Profiles.Add(new ProfileRow { Id = 2, Name = "竞技", PollingHz = 4000, PerformanceMode = 3, PerformanceLabel = "超核模式", DpiText = "800 × 1600", AppName = "cs2.exe", Badge = "默认", IsDefault = false, SleepMinutes = 5, DpiX = 800, DpiY = 1600, AppPath = @"C:\Games\cs2.exe" });
            return state;
        }

        private static DpiState DemoDpi()
        {
            var state = new DpiState { SelectedIndex = 3, ActiveIndex = 3, Min = 10, Max = 30000, Step = 10, MaxSlots = 7, FromDevice = true, ModelLabel = "雷柏 VT3 V2" };
            int[] values = { 400, 800, 1200, 1600, 3200, 6400, 12800 };
            for (int i = 0; i < values.Length; i++) state.Slots.Add(new DpiSlotRow { Index = i, X = values[i], Y = values[i], IsActive = i == 3 });
            return state;
        }
    }
}

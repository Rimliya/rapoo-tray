using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace RapooTray.Settings
{
    internal sealed class PollingProbeSnapshot
    {
        public double MeasuredHz;
        public double JitterMs;
        public double MaxGapMs;
        public int Samples;
        public bool Running;
        public string FilterLabel;
    }

    internal sealed class PollingProbe : IDisposable
    {
        private const int WM_INPUT = 0x00FF;
        private const uint RID_INPUT = 0x10000003;
        private const uint RIDI_DEVICENAME = 0x20000007;
        private const uint RIDEV_INPUTSINK = 0x00000100;
        private const uint RIDEV_REMOVE = 0x00000001;
        private const uint RIM_TYPEMOUSE = 0;
        private const uint RIDEV_DEVNOTIFY = 0x00002000;

        private HwndSource _source;
        private DispatcherTimer _timer;
        private readonly Queue<long> _samples = new Queue<long>();
        private readonly Queue<double> _intervals = new Queue<double>();
        private readonly HashSet<IntPtr> _targetDevices = new HashSet<IntPtr>();
        private long _lastTimestamp;
        private bool _running;
        private string _filterLabel = "";

        public event EventHandler<PollingProbeSnapshot> Updated;

        public bool IsRunning => _running;

        public void Start(Window owner, string productId)
        {
            if (_running || owner == null) return;
            IntPtr handle = new WindowInteropHelper(owner).Handle;
            if (handle == IntPtr.Zero) throw new InvalidOperationException("设置窗口句柄尚未创建。");

            ResolveTargetDevices(productId);

            RAWINPUTDEVICE[] devices =
            {
                new RAWINPUTDEVICE
                {
                    UsagePage = 0x01,
                    Usage = 0x02,
                    Flags = RIDEV_INPUTSINK,
                    Target = handle
                }
            };
            if (!RegisterRawInputDevices(devices, 1, (uint)Marshal.SizeOf(typeof(RAWINPUTDEVICE))))
                throw new InvalidOperationException("无法注册鼠标原始输入，错误码 " + Marshal.GetLastWin32Error());

            _source = HwndSource.FromHwnd(handle);
            if (_source == null) throw new InvalidOperationException("无法连接设置窗口消息源。");
            _source.AddHook(WndProc);
            _samples.Clear();
            _intervals.Clear();
            _lastTimestamp = 0;
            _running = true;
            _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
            _timer.Tick += (_, __) => Updated?.Invoke(this, GetSnapshot());
            _timer.Start();
        }

        public void Stop()
        {
            if (_timer != null)
            {
                _timer.Stop();
                _timer = null;
            }
            if (_source != null)
            {
                _source.RemoveHook(WndProc);
                _source = null;
            }
            try
            {
                RAWINPUTDEVICE[] remove =
                {
                    new RAWINPUTDEVICE { UsagePage = 0x01, Usage = 0x02, Flags = RIDEV_REMOVE, Target = IntPtr.Zero }
                };
                RegisterRawInputDevices(remove, 1, (uint)Marshal.SizeOf(typeof(RAWINPUTDEVICE)));
            }
            catch { }
            _running = false;
            _targetDevices.Clear();
            Updated?.Invoke(this, GetSnapshot());
        }

        public PollingProbeSnapshot GetSnapshot()
        {
            long now = Stopwatch.GetTimestamp();
            long oneSecondAgo = now - Stopwatch.Frequency;
            while (_samples.Count > 0 && _samples.Peek() < oneSecondAgo) _samples.Dequeue();

            double jitter = 0;
            double maxGap = 0;
            if (_intervals.Count > 1)
            {
                double sum = 0;
                double max = 0;
                foreach (double interval in _intervals)
                {
                    sum += interval;
                    if (interval > max) max = interval;
                }
                double average = sum / _intervals.Count;
                double variance = 0;
                foreach (double interval in _intervals) variance += (interval - average) * (interval - average);
                jitter = Math.Sqrt(variance / _intervals.Count);
                maxGap = max;
            }
            return new PollingProbeSnapshot
            {
                MeasuredHz = _samples.Count,
                JitterMs = jitter,
                MaxGapMs = maxGap,
                Samples = _intervals.Count,
                Running = _running,
                FilterLabel = _filterLabel
            };
        }

        private void ResolveTargetDevices(string productId)
        {
            _targetDevices.Clear();
            string normalizedProduct = (productId ?? "").Trim().ToLowerInvariant();
            if (normalizedProduct.Length > 0 && !normalizedProduct.StartsWith("pid_", StringComparison.Ordinal))
                normalizedProduct = "pid_" + normalizedProduct;
            _filterLabel = string.IsNullOrEmpty(normalizedProduct)
                ? "VID_24AE / 任意雷柏鼠标"
                : "VID_24AE / " + normalizedProduct.ToUpperInvariant();

            uint count = 0;
            uint structSize = (uint)Marshal.SizeOf(typeof(RAWINPUTDEVICELIST));
            GetRawInputDeviceList(null, ref count, structSize);
            if (count == 0) throw new InvalidOperationException("系统没有返回原始输入设备。");

            var list = new RAWINPUTDEVICELIST[count];
            uint actual = GetRawInputDeviceList(list, ref count, structSize);
            if (actual == uint.MaxValue) throw new InvalidOperationException("读取原始输入设备列表失败，错误码 " + Marshal.GetLastWin32Error());
            foreach (RAWINPUTDEVICELIST device in list)
            {
                if (device.Type != RIM_TYPEMOUSE) continue;
                string path = GetDeviceName(device.Device);
                if (string.IsNullOrEmpty(path)) continue;
                string lower = path.ToLowerInvariant();
                if (!lower.Contains("vid_24ae") && !lower.Contains("000024ae")) continue;
                if (!string.IsNullOrEmpty(normalizedProduct) && !lower.Contains(normalizedProduct)) continue;
                _targetDevices.Add(device.Device);
                break;
            }

            if (_targetDevices.Count == 0 && string.IsNullOrEmpty(normalizedProduct))
            {
                foreach (RAWINPUTDEVICELIST device in list)
                {
                    if (device.Type == RIM_TYPEMOUSE) _targetDevices.Add(device.Device);
                }
                if (_targetDevices.Count > 0) _filterLabel = "SYSTEM MOUSE / TEST MODE";
            }
            if (_targetDevices.Count == 0)
                throw new InvalidOperationException("未找到匹配当前设备的雷柏鼠标 Raw Input 接口：" + _filterLabel);
        }

        private static string GetDeviceName(IntPtr device)
        {
            uint size = 0;
            GetRawInputDeviceInfo(device, RIDI_DEVICENAME, IntPtr.Zero, ref size);
            if (size == 0) return null;
            var builder = new StringBuilder((int)size);
            uint result = GetRawInputDeviceInfo(device, RIDI_DEVICENAME, builder, ref size);
            return result == uint.MaxValue ? null : builder.ToString();
        }

        private IntPtr WndProc(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (message != WM_INPUT) return IntPtr.Zero;
            uint size = 0;
            uint headerSize = (uint)Marshal.SizeOf(typeof(RAWINPUTHEADER));
            GetRawInputData(lParam, RID_INPUT, IntPtr.Zero, ref size, headerSize);
            if (size == 0) return IntPtr.Zero;
            IntPtr buffer = Marshal.AllocHGlobal((int)size);
            try
            {
                if (GetRawInputData(lParam, RID_INPUT, buffer, ref size, headerSize) == size)
                {
                    RAWINPUTHEADER header = Marshal.PtrToStructure<RAWINPUTHEADER>(buffer);
                    if (header.Type == RIM_TYPEMOUSE && _targetDevices.Contains(header.Device))
                    {
                        long timestamp = Stopwatch.GetTimestamp();
                        if (_lastTimestamp > 0)
                        {
                            double intervalMs = (timestamp - _lastTimestamp) * 1000.0 / Stopwatch.Frequency;
                            if (intervalMs <= 250)
                            {
                                _intervals.Enqueue(intervalMs);
                                while (_intervals.Count > 2048) _intervals.Dequeue();
                            }
                        }
                        _lastTimestamp = timestamp;
                        _samples.Enqueue(timestamp);
                    }
                }
            }
            finally { Marshal.FreeHGlobal(buffer); }
            return IntPtr.Zero;
        }

        public void Dispose() => Stop();

        [StructLayout(LayoutKind.Sequential)]
        private struct RAWINPUTDEVICE
        {
            public ushort UsagePage;
            public ushort Usage;
            public uint Flags;
            public IntPtr Target;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RAWINPUTDEVICELIST
        {
            public IntPtr Device;
            public uint Type;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RAWINPUTHEADER
        {
            public uint Type;
            public uint Size;
            public IntPtr Device;
            public IntPtr WParam;
        }

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool RegisterRawInputDevices([In] RAWINPUTDEVICE[] devices, uint count, uint size);

        [DllImport("user32.dll")]
        private static extern uint GetRawInputData(IntPtr rawInput, uint command, IntPtr data, ref uint size, uint headerSize);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint GetRawInputDeviceList([Out] RAWINPUTDEVICELIST[] devices, ref uint count, uint size);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint GetRawInputDeviceList(IntPtr devices, ref uint count, uint size);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetRawInputDeviceInfo(IntPtr device, uint command, IntPtr data, ref uint size);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetRawInputDeviceInfo(IntPtr device, uint command, StringBuilder data, ref uint size);
    }
}
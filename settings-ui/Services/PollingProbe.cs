using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
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
    }

    internal sealed class PollingProbe : IDisposable
    {
        private const int WM_INPUT = 0x00FF;
        private const uint RID_INPUT = 0x10000003;
        private const uint RIDEV_INPUTSINK = 0x00000100;
        private const uint RIDEV_REMOVE = 0x00000001;
        private const int RIM_TYPEMOUSE = 0;

        private HwndSource _source;
        private DispatcherTimer _timer;
        private readonly Queue<long> _samples = new Queue<long>();
        private readonly Queue<double> _intervals = new Queue<double>();
        private long _lastTimestamp;
        private bool _running;

        public event EventHandler<PollingProbeSnapshot> Updated;

        public bool IsRunning => _running;

        public void Start(Window owner)
        {
            if (_running || owner == null) return;
            IntPtr handle = new WindowInteropHelper(owner).Handle;
            if (handle == IntPtr.Zero) throw new InvalidOperationException("设置窗口句柄尚未创建。");

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
                Running = _running
            };
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
                    if (header.Type == RIM_TYPEMOUSE)
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
    }
}
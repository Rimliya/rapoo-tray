using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Media;

namespace RapooTray.Settings.Controls
{
    public sealed class BatteryChart : FrameworkElement
    {
        private IReadOnlyList<BatterySample> _samples = new List<BatterySample>();
        private int _days = 7;

        public void SetSamples(IEnumerable<BatterySample> samples, int days)
        {
            _samples = (samples ?? Enumerable.Empty<BatterySample>()).OrderBy(x => x.Timestamp).ToList();
            _days = days;
            InvalidateVisual();
        }

        protected override void OnRender(DrawingContext dc)
        {
            base.OnRender(dc);
            double width = ActualWidth;
            double height = ActualHeight;
            if (width < 80 || height < 80) return;

            Brush ink = ResourceBrush("AppInkBrush", Color.FromRgb(22, 22, 22));
            Brush muted = ResourceBrush("AppSecondaryTextBrush", Color.FromRgb(92, 92, 92));
            Brush rule = ResourceBrush("AppRuleBrush", Color.FromRgb(211, 218, 223));
            Brush signal = ResourceBrush("AppSignalBrush", Color.FromRgb(125, 230, 255));
            Brush trace = ResourceBrush("AppTraceBrush", Color.FromRgb(22, 22, 22));
            Brush success = new SolidColorBrush(Color.FromRgb(47, 167, 103));
            success.Freeze();

            var gridPen = new Pen(rule, 1);
            gridPen.Freeze();
            dc.DrawRectangle(Brushes.Transparent, null, new Rect(0, 0, width, height));

            const double left = 40;
            const double right = 14;
            const double top = 12;
            const double bottom = 28;
            double plotWidth = Math.Max(1, width - left - right);
            double plotHeight = Math.Max(1, height - top - bottom);
            var typeface = new Typeface("Segoe UI");
            double pixelsPerDip = VisualTreeHelper.GetDpi(this).PixelsPerDip;

            for (int level = 0; level <= 100; level += 25)
            {
                double y = top + plotHeight * (1 - level / 100.0);
                dc.DrawLine(gridPen, new Point(left, y), new Point(left + plotWidth, y));
                var label = new FormattedText(level + "%", CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                    typeface, 10, level == 0 ? muted : muted, pixelsPerDip);
                dc.DrawText(label, new Point(left - label.Width - 8, y - label.Height / 2));
            }

            if (_samples.Count == 0)
            {
                var empty = new FormattedText("等待电量采样…", CultureInfo.CurrentUICulture,
                    FlowDirection.LeftToRight, typeface, 13, muted, pixelsPerDip);
                dc.DrawText(empty, new Point(left + (plotWidth - empty.Width) / 2, top + (plotHeight - empty.Height) / 2));
                return;
            }

            long start = _samples[0].Timestamp;
            long end = _samples[_samples.Count - 1].Timestamp;
            if (end <= start) end = start + 3600;

            Func<BatterySample, Point> pointFor = sample =>
            {
                double x = left + (sample.Timestamp - start) * plotWidth / (end - start);
                double y = top + (100 - Math.Max(0, Math.Min(100, sample.Battery))) * plotHeight / 100.0;
                return new Point(x, y);
            };

            var line = new StreamGeometry();
            using (StreamGeometryContext context = line.Open())
            {
                Point first = pointFor(_samples[0]);
                context.BeginFigure(first, false, false);
                for (int i = 1; i < _samples.Count; i++) context.LineTo(pointFor(_samples[i]), true, false);
            }
            line.Freeze();

            var area = new StreamGeometry();
            using (StreamGeometryContext context = area.Open())
            {
                Point first = pointFor(_samples[0]);
                context.BeginFigure(new Point(first.X, top + plotHeight), true, true);
                context.LineTo(first, true, false);
                for (int i = 1; i < _samples.Count; i++) context.LineTo(pointFor(_samples[i]), true, false);
                Point last = pointFor(_samples[_samples.Count - 1]);
                context.LineTo(new Point(last.X, top + plotHeight), true, false);
            }
            area.Freeze();

            var fill = new LinearGradientBrush(
                Color.FromArgb(82, ((SolidColorBrush)signal).Color.R, ((SolidColorBrush)signal).Color.G, ((SolidColorBrush)signal).Color.B),
                Color.FromArgb(0, ((SolidColorBrush)signal).Color.R, ((SolidColorBrush)signal).Color.G, ((SolidColorBrush)signal).Color.B),
                new Point(0, 0), new Point(0, 1));
            fill.Freeze();
            dc.DrawGeometry(fill, null, area);

            var linePen = new Pen(signal, 2.2);
            linePen.Freeze();
            dc.DrawGeometry(null, linePen, line);

            for (int i = 0; i < _samples.Count; i++)
            {
                BatterySample sample = _samples[i];
                if (!sample.Charging && sample.Battery > 20) continue;
                Point point = pointFor(sample);
                Brush marker = sample.Charging ? success : ResourceBrush("AppDangerBrush", Color.FromRgb(179, 38, 30));
                dc.DrawEllipse(marker, null, point, 2.8, 2.8);
            }

            DateTime localStart = DateTimeOffset.FromUnixTimeSeconds(start).LocalDateTime;
            DateTime localEnd = DateTimeOffset.FromUnixTimeSeconds(end).LocalDateTime;
            DateTime localMid = DateTimeOffset.FromUnixTimeSeconds(start + (end - start) / 2).LocalDateTime;
            string format = _days >= 2 ? "M/d" : "HH:mm";
            DrawTimeLabel(dc, typeface, muted, pixelsPerDip, localStart.ToString(format), left, top + plotHeight + 8, TextAlignment.Left, left, plotWidth);
            DrawTimeLabel(dc, typeface, muted, pixelsPerDip, localMid.ToString(format), left + plotWidth / 2, top + plotHeight + 8, TextAlignment.Center, left, plotWidth);
            DrawTimeLabel(dc, typeface, muted, pixelsPerDip, localEnd.ToString(format), left + plotWidth, top + plotHeight + 8, TextAlignment.Right, left, plotWidth);

            var originPen = new Pen(trace, 1);
            originPen.Freeze();
            dc.DrawLine(originPen, new Point(left, top + plotHeight), new Point(left + plotWidth, top + plotHeight));
        }

        private static void DrawTimeLabel(DrawingContext dc, Typeface typeface, Brush brush, double pixelsPerDip,
            string text, double x, double y, TextAlignment alignment, double plotLeft, double plotWidth)
        {
            var formatted = new FormattedText(text, CultureInfo.CurrentUICulture, FlowDirection.LeftToRight,
                typeface, 10, brush, pixelsPerDip);
            double left = alignment == TextAlignment.Left ? x : alignment == TextAlignment.Center ? x - formatted.Width / 2 : x - formatted.Width;
            left = Math.Max(plotLeft, Math.Min(plotLeft + plotWidth - formatted.Width, left));
            dc.DrawText(formatted, new Point(left, y));
        }

        private Brush ResourceBrush(string key, Color fallback)
        {
            var brush = TryFindResource(key) as Brush;
            if (brush != null) return brush;
            var solid = new SolidColorBrush(fallback);
            solid.Freeze();
            return solid;
        }
    }
}

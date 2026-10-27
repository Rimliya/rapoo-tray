using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace RapooTray.Settings.Controls
{
    internal static class EfMotion
    {
        private static readonly Duration Fast = new Duration(TimeSpan.FromMilliseconds(140));
        private static readonly Duration Base = new Duration(TimeSpan.FromMilliseconds(190));
        private static readonly Duration Slow = new Duration(TimeSpan.FromMilliseconds(280));

        public static bool Enabled
        {
            get { return SystemParameters.ClientAreaAnimation && !SystemParameters.HighContrast; }
        }

        public static void Reveal(FrameworkElement element, double offsetX = 0, double offsetY = 8, int delayMs = 0, bool slow = false)
        {
            if (element == null) return;
            element.BeginAnimation(UIElement.OpacityProperty, null);
            TranslateTransform transform = EnsureTranslate(element);

            if (!Enabled)
            {
                element.Opacity = 1;
                transform.X = 0;
                transform.Y = 0;
                return;
            }

            Duration duration = slow ? Slow : Base;
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            var fade = new DoubleAnimation(0, 1, duration)
            {
                BeginTime = TimeSpan.FromMilliseconds(delayMs),
                EasingFunction = ease,
                FillBehavior = FillBehavior.HoldEnd
            };
            var slideX = new DoubleAnimation(offsetX, 0, duration)
            {
                BeginTime = TimeSpan.FromMilliseconds(delayMs),
                EasingFunction = ease,
                FillBehavior = FillBehavior.HoldEnd
            };
            var slideY = new DoubleAnimation(offsetY, 0, duration)
            {
                BeginTime = TimeSpan.FromMilliseconds(delayMs),
                EasingFunction = ease,
                FillBehavior = FillBehavior.HoldEnd
            };

            element.Opacity = 0;
            element.BeginAnimation(UIElement.OpacityProperty, fade);
            transform.BeginAnimation(TranslateTransform.XProperty, slideX);
            transform.BeginAnimation(TranslateTransform.YProperty, slideY);
        }

        public static void RevealPage(FrameworkElement page, FrameworkElement eyebrow, FrameworkElement title, FrameworkElement subtitle)
        {
            Reveal(page, 10, 4, 70);
            Reveal(eyebrow, 0, 5, 0);
            Reveal(title, 0, 7, 28);
            Reveal(subtitle, 0, 5, 54);
        }

        public static void ReadoutSettle(TextBlock textBlock)
        {
            if (textBlock == null) return;
            if (!Enabled)
            {
                textBlock.BeginAnimation(UIElement.OpacityProperty, null);
                textBlock.Opacity = 1;
                return;
            }

            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            var fade = new DoubleAnimation(0.45, 1, Fast) { EasingFunction = ease, FillBehavior = FillBehavior.Stop };
            textBlock.BeginAnimation(UIElement.OpacityProperty, fade);
            TranslateTransform transform = EnsureTranslate(textBlock);
            transform.BeginAnimation(
                TranslateTransform.YProperty,
                new DoubleAnimation(2, 0, Fast) { EasingFunction = ease, FillBehavior = FillBehavior.Stop });
        }

        public static void Pulse(UIElement element)
        {
            if (element == null || !Enabled) return;
            var fade = new DoubleAnimation(0.35, 1, new Duration(TimeSpan.FromMilliseconds(260)))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
                FillBehavior = FillBehavior.Stop
            };
            element.BeginAnimation(UIElement.OpacityProperty, fade);
        }

        public static void Nudge(FrameworkElement element, double offsetX = 2)
        {
            if (element == null) return;
            TranslateTransform transform = EnsureTranslate(element);
            if (!Enabled)
            {
                transform.X = 0;
                return;
            }
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            transform.BeginAnimation(
                TranslateTransform.XProperty,
                new DoubleAnimation(0, offsetX, new Duration(TimeSpan.FromMilliseconds(90)))
                {
                    AutoReverse = true,
                    EasingFunction = ease,
                    FillBehavior = FillBehavior.Stop
                });
        }

        public static void ScanOnce(FrameworkElement scan, double trackWidth, bool slow = false)
        {
            if (scan == null || !Enabled) return;
            TranslateTransform transform = EnsureTranslate(scan);
            double width = scan.ActualWidth > 0 ? scan.ActualWidth : 120;
            double target = Math.Max(0, trackWidth + width);
            scan.Opacity = 0.85;
            transform.BeginAnimation(
                TranslateTransform.XProperty,
                new DoubleAnimation(-width, target, slow ? Slow : new Duration(TimeSpan.FromMilliseconds(620)))
                {
                    EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseInOut },
                    FillBehavior = FillBehavior.HoldEnd
                });
        }

        public static void SetBusyScan(FrameworkElement scan, double trackWidth, bool busy)
        {
            if (scan == null) return;
            TranslateTransform transform = EnsureTranslate(scan);
            transform.BeginAnimation(TranslateTransform.XProperty, null);
            if (!busy || !Enabled)
            {
                scan.Opacity = 0;
                transform.X = 0;
                return;
            }

            double width = scan.ActualWidth > 0 ? scan.ActualWidth : 96;
            scan.Opacity = 1;
            transform.X = -width;
            var slide = new DoubleAnimation(-width, Math.Max(width, trackWidth + width), new Duration(TimeSpan.FromMilliseconds(920)))
            {
                RepeatBehavior = RepeatBehavior.Forever,
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseInOut }
            };
            transform.BeginAnimation(TranslateTransform.XProperty, slide);
        }

        public static void SettleSelection(FrameworkElement element)
        {
            if (element == null) return;
            if (!Enabled)
            {
                element.BeginAnimation(UIElement.OpacityProperty, null);
                element.Opacity = 1;
                return;
            }
            element.BeginAnimation(
                UIElement.OpacityProperty,
                new DoubleAnimation(0.55, 1, Fast)
                {
                    EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
                    FillBehavior = FillBehavior.Stop
                });
        }

        private static TranslateTransform EnsureTranslate(FrameworkElement element)
        {
            if (element.RenderTransform is TranslateTransform)
            {
                return (TranslateTransform)element.RenderTransform;
            }

            var transform = new TranslateTransform();
            element.RenderTransform = transform;
            return transform;
        }
    }
}

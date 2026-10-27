using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace RapooTray.Settings.Controls
{
    internal static class CutGeometry
    {
        public static Geometry Create(double width, double height, double cut)
        {
            double c = Math.Max(0, Math.Min(cut, Math.Min(width, height) / 2));
            var geometry = new StreamGeometry();
            using (StreamGeometryContext context = geometry.Open())
            {
                context.BeginFigure(new Point(0, 0), true, true);
                context.LineTo(new Point(width, 0), true, false);
                context.LineTo(new Point(width, height - c), true, false);
                context.LineTo(new Point(width - c, height), true, false);
                context.LineTo(new Point(0, height), true, false);
            }
            geometry.Freeze();
            return geometry;
        }

        public static Geometry CreateInset(double width, double height, double cut, double inset)
        {
            double c = Math.Max(inset, Math.Min(cut, Math.Min(width, height) / 2 - inset));
            var geometry = new StreamGeometry();
            using (StreamGeometryContext context = geometry.Open())
            {
                context.BeginFigure(new Point(inset, inset), false, true);
                context.LineTo(new Point(width - inset, inset), true, false);
                context.LineTo(new Point(width - inset, height - c), true, false);
                context.LineTo(new Point(width - c, height - inset), true, false);
                context.LineTo(new Point(inset, height - inset), true, false);
            }
            geometry.Freeze();
            return geometry;
        }
    }

    public class CutButton : Button
    {
        public static readonly DependencyProperty CutSizeProperty = DependencyProperty.Register(
            nameof(CutSize), typeof(double), typeof(CutButton),
            new FrameworkPropertyMetadata(5.0, FrameworkPropertyMetadataOptions.AffectsRender));

        public double CutSize
        {
            get { return (double)GetValue(CutSizeProperty); }
            set { SetValue(CutSizeProperty, value); }
        }

        protected override void OnRender(DrawingContext drawingContext)
        {
            double width = ActualWidth;
            double height = ActualHeight;
            if (width <= 0 || height <= 0) return;

            Geometry geometry = CutGeometry.Create(width, height, CutSize);
            Pen borderPen = BorderBrush == null || BorderThickness.Left <= 0
                ? null
                : new Pen(BorderBrush, BorderThickness.Left);
            drawingContext.DrawGeometry(Background, borderPen, geometry);

            if (IsKeyboardFocused && Foreground != null)
            {
                drawingContext.DrawGeometry(null, new Pen(Foreground, 1),
                    CutGeometry.CreateInset(width, height, CutSize, 2));
            }
        }
    }

    public class CutRadioButton : RadioButton
    {
        public static readonly DependencyProperty CutSizeProperty = DependencyProperty.Register(
            nameof(CutSize), typeof(double), typeof(CutRadioButton),
            new FrameworkPropertyMetadata(5.0, FrameworkPropertyMetadataOptions.AffectsRender));

        public double CutSize
        {
            get { return (double)GetValue(CutSizeProperty); }
            set { SetValue(CutSizeProperty, value); }
        }

        protected override void OnRender(DrawingContext drawingContext)
        {
            double width = ActualWidth;
            double height = ActualHeight;
            if (width <= 0 || height <= 0) return;

            Geometry geometry = CutGeometry.Create(width, height, CutSize);
            Pen borderPen = BorderBrush == null || BorderThickness.Left <= 0
                ? null
                : new Pen(BorderBrush, BorderThickness.Left);
            drawingContext.DrawGeometry(Background, borderPen, geometry);

            if (IsKeyboardFocused && Foreground != null)
            {
                drawingContext.DrawGeometry(null, new Pen(Foreground, 1),
                    CutGeometry.CreateInset(width, height, CutSize, 2));
            }
        }
    }
}

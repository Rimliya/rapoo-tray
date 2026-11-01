#include "osd_window.h"

#include <objidl.h>
#include <gdiplus.h>
#include <strsafe.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

namespace Osd {

namespace {

constexpr UINT_PTR TIMER_OSD_HIDE = 101;
constexpr UINT_PTR TIMER_OSD_FADE = 102;
constexpr int IDR_SAIRA_WIDE_SEMI_BOLD = 201;
constexpr int IDR_SAIRA_WIDE_EXTRA_BOLD = 202;
constexpr int IDR_SAIRA_FIELD = 203;
constexpr int HIDE_DELAY_MS = 2200;
constexpr int MIN_WIDTH_DIP = 410;
constexpr int MAX_WIDTH_DIP = 720;

HWND g_window = NULL;
WCHAR g_textLine1[160] = {0};
WCHAR g_textLine2[160] = {0};
WCHAR g_textLine3[160] = {0};
BYTE g_osdAlpha = 0;
OsdConfig::Config g_config;

ULONG_PTR g_gdiplusToken = 0;
Gdiplus::PrivateFontCollection* g_privateFonts = NULL;
Gdiplus::FontFamily* g_sairaFamily = NULL;
bool g_highContrast = false;

struct Palette {
    Gdiplus::Color paper;
    Gdiplus::Color panel;
    Gdiplus::Color ink;
    Gdiplus::Color ink2;
    Gdiplus::Color rule;
    Gdiplus::Color chip;
    Gdiplus::Color signal;
    Gdiplus::Color onSignal;
    Gdiplus::Color commandBar;
    Gdiplus::Color commandText;
    Gdiplus::Color commandMuted;
};

int ScaleDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

UINT GetCurrentWindowDpi(HWND hWnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
        auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindow && hWnd) {
            UINT dpi = getDpiForWindow(hWnd);
            if (dpi > 0) return dpi;
        }
    }
    return 96;
}
UINT GetMonitorDpi(HMONITOR monitor, HWND fallbackWindow) {
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI* GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
        auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
            GetProcAddress(shcore, "GetDpiForMonitor"));
        if (getDpiForMonitor && monitor) {
            UINT dpiX = 0;
            UINT dpiY = 0;
            if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY)) && dpiX > 0) {
                FreeLibrary(shcore);
                return dpiX;
            }
        }
        FreeLibrary(shcore);
    }
    return GetCurrentWindowDpi(fallbackWindow);
}

bool IsSystemDark() {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) != ERROR_SUCCESS) return true;

    DWORD value = 1;
    DWORD type = 0;
    DWORD size = sizeof(value);
    bool dark = true;
    if (RegQueryValueExW(key, L"SystemUsesLightTheme", NULL, &type,
            reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS) {
        dark = value == 0;
    }
    RegCloseKey(key);
    return dark;
}

bool IsHighContrast() {
    HIGHCONTRASTW settings = {sizeof(settings)};
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0)) {
        return false;
    }
    return (settings.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

Palette MakePalette(bool dark) {
    if (g_highContrast) {
        return Palette{
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_WINDOW)),
                GetGValue(GetSysColor(COLOR_WINDOW)), GetBValue(GetSysColor(COLOR_WINDOW))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_WINDOW)),
                GetGValue(GetSysColor(COLOR_WINDOW)), GetBValue(GetSysColor(COLOR_WINDOW))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_WINDOWTEXT)),
                GetGValue(GetSysColor(COLOR_WINDOWTEXT)), GetBValue(GetSysColor(COLOR_WINDOWTEXT))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_GRAYTEXT)),
                GetGValue(GetSysColor(COLOR_GRAYTEXT)), GetBValue(GetSysColor(COLOR_GRAYTEXT))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_WINDOWTEXT)),
                GetGValue(GetSysColor(COLOR_WINDOWTEXT)), GetBValue(GetSysColor(COLOR_WINDOWTEXT))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_BTNFACE)),
                GetGValue(GetSysColor(COLOR_BTNFACE)), GetBValue(GetSysColor(COLOR_BTNFACE))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_HIGHLIGHT)),
                GetGValue(GetSysColor(COLOR_HIGHLIGHT)), GetBValue(GetSysColor(COLOR_HIGHLIGHT))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_HIGHLIGHTTEXT)),
                GetGValue(GetSysColor(COLOR_HIGHLIGHTTEXT)), GetBValue(GetSysColor(COLOR_HIGHLIGHTTEXT))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_WINDOWTEXT)),
                GetGValue(GetSysColor(COLOR_WINDOWTEXT)), GetBValue(GetSysColor(COLOR_WINDOWTEXT))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_WINDOW)),
                GetGValue(GetSysColor(COLOR_WINDOW)), GetBValue(GetSysColor(COLOR_WINDOW))),
            Gdiplus::Color(255, GetRValue(GetSysColor(COLOR_GRAYTEXT)),
                GetGValue(GetSysColor(COLOR_GRAYTEXT)), GetBValue(GetSysColor(COLOR_GRAYTEXT))),
        };
    }

    if (dark) {
        return Palette{
            Gdiplus::Color(255, 0x12, 0x15, 0x16),
            Gdiplus::Color(255, 0x1C, 0x20, 0x22),
            Gdiplus::Color(255, 0xED, 0xF0, 0xF0),
            Gdiplus::Color(255, 0xA6, 0xAE, 0xB1),
            Gdiplus::Color(255, 0x39, 0x42, 0x47),
            Gdiplus::Color(255, 0x25, 0x2B, 0x2E),
            Gdiplus::Color(255, 0x7D, 0xE6, 0xFF),
            Gdiplus::Color(255, 0x14, 0x17, 0x18),
            Gdiplus::Color(255, 0x0F, 0x11, 0x12),
            Gdiplus::Color(255, 0xF1, 0xF3, 0xF3),
            Gdiplus::Color(255, 0x9E, 0xA8, 0xAD),
        };
    }

    return Palette{
        Gdiplus::Color(255, 0xEF, 0xF2, 0xF3),
        Gdiplus::Color(255, 0xFC, 0xFD, 0xFD),
        Gdiplus::Color(255, 0x16, 0x19, 0x1A),
        Gdiplus::Color(255, 0x5E, 0x66, 0x6A),
        Gdiplus::Color(255, 0xD5, 0xDC, 0xDE),
        Gdiplus::Color(255, 0xE5, 0xEA, 0xEC),
        Gdiplus::Color(255, 0x7D, 0xE6, 0xFF),
        Gdiplus::Color(255, 0x14, 0x17, 0x18),
        Gdiplus::Color(255, 0x14, 0x17, 0x18),
        Gdiplus::Color(255, 0xF1, 0xF3, 0xF3),
        Gdiplus::Color(255, 0xAA, 0xB2, 0xB5),
    };
}

Gdiplus::Color Mix(const Gdiplus::Color& a, const Gdiplus::Color& b, int aWeight) {
    aWeight = std::clamp(aWeight, 0, 100);
    const int bWeight = 100 - aWeight;
    return Gdiplus::Color(255,
        (a.GetR() * aWeight + b.GetR() * bWeight) / 100,
        (a.GetG() * aWeight + b.GetG() * bWeight) / 100,
        (a.GetB() * aWeight + b.GetB() * bWeight) / 100);
}

bool HasNonAscii(const WCHAR* text) {
    if (!text) return false;
    for (const WCHAR* p = text; *p; ++p) {
        if (*p > 0x7F) return true;
    }
    return false;
}

std::vector<std::wstring> SplitDetailFields(const WCHAR* text) {
    std::vector<std::wstring> fields;
    if (!text || !text[0]) return fields;

    std::wstring raw(text);
    size_t start = 0;
    while (start <= raw.size()) {
        const size_t end = raw.find(L'\t', start);
        std::wstring field = raw.substr(start, end == std::wstring::npos ? raw.size() - start : end - start);
        size_t first = 0;
        while (first < field.size() && iswspace(field[first])) ++first;
        size_t last = field.size();
        while (last > first && iswspace(field[last - 1])) --last;
        if (last > first) fields.push_back(field.substr(first, last - first));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return fields;
}

void LoadEmbeddedFonts(HINSTANCE instance) {
    g_privateFonts = new Gdiplus::PrivateFontCollection();
    const int ids[] = {
        IDR_SAIRA_WIDE_SEMI_BOLD,
        IDR_SAIRA_WIDE_EXTRA_BOLD,
        IDR_SAIRA_FIELD,
    };
    for (int id : ids) {
        HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!resource) continue;
        HGLOBAL loaded = LoadResource(instance, resource);
        if (!loaded) continue;
        const void* data = LockResource(loaded);
        const DWORD size = SizeofResource(instance, resource);
        if (!data || !size) continue;
        g_privateFonts->AddMemoryFont(data, static_cast<INT>(size));
    }

    g_sairaFamily = new Gdiplus::FontFamily(L"Rapoo Saira", g_privateFonts);
    if (!g_sairaFamily->IsAvailable()) {
        delete g_sairaFamily;
        g_sairaFamily = NULL;
    }
}

std::unique_ptr<Gdiplus::Font> MakeFont(int pixels, bool bold, bool latinOnly) {
    if (latinOnly && g_sairaFamily) {
        return std::make_unique<Gdiplus::Font>(
            g_sairaFamily, static_cast<Gdiplus::REAL>(pixels),
            bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
            Gdiplus::UnitPixel);
    }

    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    return std::make_unique<Gdiplus::Font>(
        &family, static_cast<Gdiplus::REAL>(pixels),
        bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
        Gdiplus::UnitPixel);
}

void FillRect(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
              const Gdiplus::Color& color) {
    Gdiplus::SolidBrush brush(color);
    graphics.FillRectangle(&brush, rect);
}

void DrawHairline(Gdiplus::Graphics& graphics, float x1, float y1, float x2, float y2,
                  const Gdiplus::Color& color) {
    Gdiplus::Pen pen(color, 1.0f);
    graphics.DrawLine(&pen, x1, y1, x2, y2);
}

void DrawHatch(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
               const Gdiplus::Color& color, float step) {
    Gdiplus::Pen pen(color, 1.0f);
    const float height = rect.Height;
    for (float x = rect.X - height; x < rect.GetRight(); x += step) {
        graphics.DrawLine(&pen, x, rect.GetBottom(), x + height, rect.Y);
    }
}

float MeasureTextWidth(const std::wstring& text, Gdiplus::Font* font);

void DrawTextLine(Gdiplus::Graphics& graphics, const std::wstring& text,
                  Gdiplus::Font* font, const Gdiplus::Color& color,
                  const Gdiplus::RectF& rect,
                  Gdiplus::StringAlignment alignment = Gdiplus::StringAlignmentNear) {
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                          Gdiplus::StringFormatFlagsNoClip);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetAlignment(alignment);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.DrawString(text.c_str(), -1, font, rect, &format, &brush);
}

void Paint(HWND hWnd) {
    PAINTSTRUCT paint = {0};
    HDC target = BeginPaint(hWnd, &paint);
    RECT client = {0};
    GetClientRect(hWnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) {
        EndPaint(hWnd, &paint);
        return;
    }

    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);

    const UINT dpi = GetMonitorDpi(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), hWnd);
    const int scale = std::clamp(g_config.osdScalePercent, 75, 150);
    auto S = [dpi, scale](int value) { return MulDiv(ScaleDpi(value, dpi), scale, 100); };
    g_highContrast = IsHighContrast();
    const Palette palette = MakePalette(IsSystemDark());
    const std::vector<std::wstring> detailFields = SplitDetailFields(g_textLine3);

    {
        Gdiplus::Graphics graphics(memory);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        // Grayscale grid-fit avoids the colour fringing ClearType produces on
        // layered HUD windows while keeping small Chinese text properly hinted.
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.SetTextContrast(5);

        FillRect(graphics, Gdiplus::RectF(0, 0, float(width), float(height)), palette.paper);
        Gdiplus::Pen framePen(palette.rule, 1.0f);
        graphics.DrawRectangle(&framePen, 0, 0, width - 1, height - 1);

        const int stripeHeight = std::max(2, S(3));
        const int commandHeight = S(34);
        const int commandBottom = stripeHeight + commandHeight;
        FillRect(graphics,
            Gdiplus::RectF(0, float(stripeHeight), float(width), float(commandHeight)),
            palette.commandBar);

        if (!g_highContrast) {
            const int third = width / 3;
            FillRect(graphics, Gdiplus::RectF(0, 0, float(third), float(stripeHeight)),
                     Gdiplus::Color(255, 0xFF, 0x00, 0xF0));
            FillRect(graphics,
                Gdiplus::RectF(float(third), 0, float(third), float(stripeHeight)),
                palette.signal);
            FillRect(graphics,
                Gdiplus::RectF(float(third * 2), 0, float(width - third * 2), float(stripeHeight)),
                Gdiplus::Color(255, 0x00, 0xFF, 0xA2));
        } else {
            FillRect(graphics, Gdiplus::RectF(0, 0, float(width), float(stripeHeight)), palette.ink);
        }

        auto headerFont = MakeFont(S(11), true, true);
        auto liveFont = MakeFont(S(10), true, true);
        const int liveWidth = S(52);
        const int liveHeight = S(20);
        const float liveX = float(width - S(16) - liveWidth);
        const float liveY = float(stripeHeight + (commandHeight - liveHeight) / 2);

        const Gdiplus::RectF hatchRect(float(std::max(S(196), width - S(158))),
                                       float(stripeHeight + S(5)),
                                       float(S(86)), float(S(22)));
        graphics.SetClip(hatchRect);
        DrawHatch(graphics, hatchRect, Mix(palette.commandMuted, palette.commandBar, 26), float(S(9)));
        graphics.ResetClip();

        DrawTextLine(graphics, L"RAPOO // DEVICE STATUS", headerFont.get(),
            palette.commandText,
            Gdiplus::RectF(float(S(16)), float(stripeHeight),
                           float(width - S(190)), float(commandHeight)));

        FillRect(graphics, Gdiplus::RectF(liveX, liveY, float(liveWidth), float(liveHeight)),
                 palette.signal);
        DrawTextLine(graphics, L"LIVE", liveFont.get(), palette.onSignal,
            Gdiplus::RectF(liveX, liveY, float(liveWidth), float(liveHeight)),
            Gdiplus::StringAlignmentCenter);

        const int footerHeight = detailFields.empty() ? 0 : S(36);
        const int bodyTop = commandBottom;
        const int bodyBottom = height - footerHeight;
        FillRect(graphics,
            Gdiplus::RectF(0, float(bodyTop), float(width), float(bodyBottom - bodyTop)),
            palette.panel);
        FillRect(graphics,
            Gdiplus::RectF(0, float(bodyTop), float(S(5)), float(bodyBottom - bodyTop)),
            palette.signal);

        const float contentLeft = float(S(20));
        const float contentRight = float(width - S(18));
        const float contentWidth = contentRight - contentLeft;
        auto deviceLabelFont = MakeFont(S(10), true, true);
        auto modelFont = MakeFont(S(14), true, false);

        const int primaryDip = std::wstring(g_textLine2).size() <= 14 ? 30 :
                               (std::wstring(g_textLine2).size() <= 24 ? 24 : 18);
        auto primaryFont = MakeFont(S(primaryDip), true, !HasNonAscii(g_textLine2));

        DrawTextLine(graphics, L"// DEVICE", deviceLabelFont.get(), palette.ink2,
            Gdiplus::RectF(contentLeft, float(bodyTop + S(6)), contentWidth, float(S(12))));
        DrawTextLine(graphics, g_textLine1, modelFont.get(), palette.ink,
            Gdiplus::RectF(contentLeft, float(bodyTop + S(18)), contentWidth, float(S(18))));
        DrawTextLine(graphics, g_textLine2, primaryFont.get(), palette.ink,
            Gdiplus::RectF(contentLeft, float(bodyTop + S(34)), contentWidth,
                           float(S(primaryDip + 4))));

        if (!detailFields.empty()) {
            const float footerTop = float(bodyBottom);
            FillRect(graphics, Gdiplus::RectF(0, footerTop, float(width), float(footerHeight)),
                     palette.commandBar);
            DrawHairline(graphics, contentLeft, footerTop, contentRight, footerTop, palette.rule);

            auto detailFont = MakeFont(S(12), false, false);
            std::vector<float> widths;
            float natural = 0;
            for (const auto& field : detailFields) {
                float w = MeasureTextWidth(field, detailFont.get()) + S(30);
                widths.push_back(w);
                natural += w;
            }
            const float gap = S(8);
            natural += gap * float(detailFields.size() - 1);
            const float available = contentRight - contentLeft;
            if (natural < available && !detailFields.empty()) {
                const float extra = (available - natural) / float(detailFields.size());
                for (float& w : widths) w += extra;
            } else if (natural > available && natural > 0) {
                const float totalGaps = gap * float(detailFields.size() - 1);
                const float widthAvailable = std::max(0.0f, available - totalGaps);
                const float widthNatural = std::max(1.0f, natural - totalGaps);
                const float factor = widthAvailable / widthNatural;
                for (float& w : widths) w *= factor;
            }

            float x = contentLeft;
            for (size_t i = 0; i < detailFields.size(); ++i) {
                if (i > 0) {
                    DrawHairline(graphics, x - gap * 0.5f, footerTop + S(9),
                                 x - gap * 0.5f, footerTop + footerHeight - S(9),
                                 Mix(palette.commandMuted, palette.commandBar, 36));
                }
                FillRect(graphics,
                    Gdiplus::RectF(x, footerTop + (footerHeight - S(5)) * 0.5f,
                                   float(S(5)), float(S(5))),
                    palette.signal);
                DrawTextLine(graphics, detailFields[i], detailFont.get(), palette.commandText,
                    Gdiplus::RectF(x + S(12), footerTop,
                                   std::max(float(S(24)), widths[i] - S(14)),
                                   float(footerHeight)));
                x += widths[i] + (i + 1 < detailFields.size() ? gap : 0.0f);
            }
        }
    }

    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(hWnd, &paint);
}
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            Paint(hWnd);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_OSD_HIDE) {
                KillTimer(hWnd, TIMER_OSD_HIDE);
                SetTimer(hWnd, TIMER_OSD_FADE, 16, NULL);
            } else if (wParam == TIMER_OSD_FADE) {
                if (g_osdAlpha > 14) {
                    g_osdAlpha = static_cast<BYTE>(std::max(0, g_osdAlpha - 14));
                    SetLayeredWindowAttributes(hWnd, 0, g_osdAlpha, LWA_ALPHA);
                } else {
                    KillTimer(hWnd, TIMER_OSD_FADE);
                    ShowWindow(hWnd, SW_HIDE);
                    g_osdAlpha = 0;
                }
            }
            return 0;

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}

void LoadSettings() {
    g_config = OsdConfig::Load();
}

float MeasureTextWidth(const std::wstring& text, Gdiplus::Font* font) {
    Gdiplus::Bitmap probe(1, 1, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(&probe);
    Gdiplus::RectF layout(0, 0, 4096, 512);
    Gdiplus::RectF bounds;
    Gdiplus::StringFormat format;
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    graphics.MeasureString(text.c_str(), -1, font, layout, &format, &bounds);
    return bounds.Width;
}

} // namespace

bool Init(HINSTANCE hInstance) {
    LoadSettings();

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, NULL) != Gdiplus::Ok) {
        return false;
    }
    LoadEmbeddedFonts(hInstance);

    WNDCLASSEXW windowClass = {0};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = L"RapooOsdPopupWnd";
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&windowClass);

    g_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        windowClass.lpszClassName,
        L"RapooOSD",
        WS_POPUP,
        0, 0, 420, 142,
        NULL, NULL, hInstance, NULL);

    return g_window != NULL;
}

void Cleanup() {
    if (g_window && IsWindow(g_window)) {
        DestroyWindow(g_window);
        g_window = NULL;
    }

    delete g_sairaFamily;
    g_sairaFamily = NULL;
    delete g_privateFonts;
    g_privateFonts = NULL;

    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

HWND GetWindowHandle() {
    return g_window;
}

bool IsEnabled() {
    return g_config.enabled;
}

void SetEnabled(bool enabled) {
    g_config.enabled = enabled;
    OsdConfig::Save(g_config);
    if (!enabled && g_window) {
        KillTimer(g_window, TIMER_OSD_HIDE);
        KillTimer(g_window, TIMER_OSD_FADE);
        ShowWindow(g_window, SW_HIDE);
    }
}

void Show(const WCHAR* line1, const WCHAR* line2, const WCHAR* line3) {
    if (!g_config.enabled || !g_window) return;

    StringCchCopyW(g_textLine1, ARRAYSIZE(g_textLine1), line1 ? line1 : L"");
    StringCchCopyW(g_textLine2, ARRAYSIZE(g_textLine2), line2 ? line2 : L"");
    if (line3 && line3[0]) {
        StringCchCopyW(g_textLine3, ARRAYSIZE(g_textLine3), line3);
    } else {
        g_textLine3[0] = 0;
    }

    POINT cursor = {0};
    GetCursorPos(&cursor);
    HMONITOR monitor = g_config.osdMonitor == OsdConfig::OSD_MONITOR_PRIMARY
        ? MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY)
        : MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    RECT work = info.rcWork;

    const UINT dpi = GetMonitorDpi(monitor, g_window);
    const int scale = std::clamp(g_config.osdScalePercent, 75, 150);
    auto S = [dpi, scale](int value) { return MulDiv(ScaleDpi(value, dpi), scale, 100); };

    auto modelProbe = MakeFont(S(14), true, false);
    const int primaryDip = std::wstring(g_textLine2).size() <= 14 ? 30 :
                           (std::wstring(g_textLine2).size() <= 24 ? 24 : 18);
    auto primaryProbe = MakeFont(S(primaryDip), true, !HasNonAscii(g_textLine2));
    auto detailProbe = MakeFont(S(12), false, false);
    const std::vector<std::wstring> detailFields = SplitDetailFields(g_textLine3);

    float footerWidth = 0;
    for (const auto& field : detailFields) {
        footerWidth += MeasureTextWidth(field, detailProbe.get()) + S(30);
    }
    if (detailFields.size() > 1) {
        footerWidth += S(8) * float(detailFields.size() - 1);
    }

    const float contentWidth = std::max({
        MeasureTextWidth(g_textLine1, modelProbe.get()),
        MeasureTextWidth(g_textLine2, primaryProbe.get()),
        footerWidth,
    });
    int targetWidth = static_cast<int>(contentWidth) + S(38);
    targetWidth = std::max(targetWidth, S(MIN_WIDTH_DIP));
    targetWidth = std::min(targetWidth, S(MAX_WIDTH_DIP));
    const int targetHeight = detailFields.empty() ? S(104) : S(142);

    int x = work.left + ((work.right - work.left) - targetWidth) / 2;
    int y = work.bottom - targetHeight - S(48);
    switch (g_config.osdPosition) {
        case OsdConfig::OSD_POSITION_BOTTOM_RIGHT:
            x = work.right - targetWidth - S(32);
            y = work.bottom - targetHeight - S(48);
            break;
        case OsdConfig::OSD_POSITION_TOP_RIGHT:
            x = work.right - targetWidth - S(32);
            y = work.top + S(32);
            break;
        case OsdConfig::OSD_POSITION_TOP_LEFT:
            x = work.left + S(32);
            y = work.top + S(32);
            break;
        case OsdConfig::OSD_POSITION_BOTTOM_CENTER:
        default:
            break;
    }
    x += S(g_config.osdOffsetX);
    y += S(g_config.osdOffsetY);
    x = std::clamp(x, (int)work.left, std::max((int)work.left, (int)work.right - targetWidth));
    y = std::clamp(y, (int)work.top, std::max((int)work.top, (int)work.bottom - targetHeight));

    KillTimer(g_window, TIMER_OSD_HIDE);
    KillTimer(g_window, TIMER_OSD_FADE);

    const int opacity = std::clamp(g_config.osdOpacityPercent, 40, 100);
    g_osdAlpha = static_cast<BYTE>(std::clamp((255 * opacity) / 100, 1, 255));
    SetLayeredWindowAttributes(g_window, 0, g_osdAlpha, LWA_ALPHA);
    SetWindowPos(g_window, HWND_TOPMOST, x, y, targetWidth, targetHeight,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    InvalidateRect(g_window, NULL, FALSE);
    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    UpdateWindow(g_window);
    SetTimer(g_window, TIMER_OSD_HIDE, (UINT)std::clamp(g_config.osdDurationMs, 800, 8000), NULL);
}

void ShowDeviceUpdate(const Device::State& state) {
    const WCHAR* model = state.modelName[0] ? state.modelName : L"通用";
    WCHAR line1[160] = {0};
    WCHAR line2[160] = {0};
    WCHAR line3[160] = {0};

    if (g_config.displayFields & OsdConfig::FIELD_MODEL) {
        StringCchCopyW(line1, ARRAYSIZE(line1), model);
    } else {
        StringCchCopyW(line1, ARRAYSIZE(line1), L"RAPOO // DEVICE");
    }

    if (g_config.displayFields & OsdConfig::FIELD_DPI_VALUE) {
        if (state.dpiX == state.dpiY) {
            StringCchPrintfW(line2, ARRAYSIZE(line2), L"%d DPI", state.dpiX);
        } else {
            StringCchPrintfW(line2, ARRAYSIZE(line2), L"X %d  ·  Y %d DPI", state.dpiX, state.dpiY);
        }
    } else if (g_config.displayFields & OsdConfig::FIELD_PERFORMANCE) {
        StringCchCopyW(line2, ARRAYSIZE(line2), Rapoo::PerfModeName(state.perfMode));
    } else if (g_config.displayFields & OsdConfig::FIELD_BATTERY) {
        StringCchPrintfW(line2, ARRAYSIZE(line2), L"电量 %d%%", state.battery);
    } else if (g_config.displayFields & OsdConfig::FIELD_POLLING) {
        StringCchPrintfW(line2, ARRAYSIZE(line2), L"回报率 %d Hz", state.pollingHz);
    } else {
        StringCchCopyW(line2, ARRAYSIZE(line2), L"设备状态已更新");
    }

    std::wstring detail;
    auto append = [&](const std::wstring& part) {
        if (part.empty()) return;
        if (!detail.empty()) detail += L"\t";
        detail += part;
    };
    if (g_config.displayFields & OsdConfig::FIELD_MODE) {
        append(state.isWired ? L"USB 有线" : L"2.4G 无线");
    }
    if (g_config.displayFields & OsdConfig::FIELD_DPI_LEVEL) {
        append(L"DPI 第 " + std::to_wstring(state.dpiLevel) + L" 档");
    }
    if ((g_config.displayFields & OsdConfig::FIELD_PERFORMANCE) &&
        (g_config.displayFields & OsdConfig::FIELD_DPI_VALUE)) {
        append(Rapoo::PerfModeName(state.perfMode));
    }
    if ((g_config.displayFields & OsdConfig::FIELD_BATTERY) &&
        (g_config.displayFields & OsdConfig::FIELD_DPI_VALUE ||
         g_config.displayFields & OsdConfig::FIELD_PERFORMANCE ||
         g_config.displayFields & OsdConfig::FIELD_POLLING)) {
        std::wstring battery = L"电量 " + std::to_wstring(state.battery) + L"%";
        if (state.isCharging) battery += L" 充电中";
        append(battery);
    }
    if ((g_config.displayFields & OsdConfig::FIELD_POLLING) &&
        (g_config.displayFields & OsdConfig::FIELD_DPI_VALUE ||
         g_config.displayFields & OsdConfig::FIELD_PERFORMANCE)) {
        append(std::to_wstring(state.pollingHz) + L" Hz");
    }
    StringCchCopyW(line3, ARRAYSIZE(line3), detail.c_str());
    Show(line1, line2, line3);
}

OsdConfig::Config GetConfig() {
    return g_config;
}

bool SetConfig(const OsdConfig::Config& config) {
    g_config = config;
    const bool saved = OsdConfig::Save(g_config);
    g_config = OsdConfig::Load();
    if (!g_config.enabled && g_window) {
        KillTimer(g_window, TIMER_OSD_HIDE);
        KillTimer(g_window, TIMER_OSD_FADE);
        ShowWindow(g_window, SW_HIDE);
    }
    return saved;
}

void ShowStyleOsd(int style) {
    const WCHAR* names[] = {L"样式一：经典电池", L"样式二：环形进度", L"样式三：大号数字"};
    const int index = std::clamp(style, 0, 2);
    Show(L"电池图标样式", names[index], L"双击托盘图标可循环切换");
}

} // namespace Osd
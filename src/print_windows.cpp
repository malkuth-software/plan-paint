#include "platform.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <stdexcept>
// Win32 base types must precede the common-dialog declarations.
// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on
namespace paint {
namespace {
struct PrintState {
    HGLOBAL mode = nullptr;
    HGLOBAL names = nullptr;
    ~PrintState() {
        if (mode) {
            GlobalFree(mode);
        }
        if (names) {
            GlobalFree(names);
        }
    }
    static void replace(HGLOBAL& current, HGLOBAL replacement) {
        if (current != replacement) {
            if (current) {
                GlobalFree(current);
            }
            current = replacement;
        }
    }
    void update(HGLOBAL replacement_mode, HGLOBAL replacement_names) {
        replace(mode, replacement_mode);
        replace(names, replacement_names);
    }
};
PrintState print_state;
} // namespace
static RECT print_margins{};
static double margin_units = 1000.0;
void page_setup() {
    PAGESETUPDLGW setup{};
    setup.lStructSize = sizeof(setup);
    setup.hDevMode = print_state.mode;
    setup.hDevNames = print_state.names;
    setup.Flags = PSD_DEFAULTMINMARGINS;
    if (PageSetupDlgW(&setup)) {
        print_margins = setup.rtMargin;
        margin_units = (setup.Flags & PSD_INHUNDREDTHSOFMILLIMETERS) ? 2540.0 : 1000.0;
    }
    print_state.update(setup.hDevMode, setup.hDevNames);
}
bool print_image(const Image& image) {
    Image flat;
    flat.reset(image.width, image.height);
    composite(flat, image, 0, 0);
    for (Color& pixel : flat.pixels) {
        std::swap(pixel.r, pixel.b);
    }
    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hDevMode = print_state.mode;
    dialog.hDevNames = print_state.names;
    dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    const bool accepted = PrintDlgW(&dialog) != 0;
    print_state.update(dialog.hDevMode, dialog.hDevNames);
    if (!accepted) {
        return false;
    }
    HDC context = dialog.hDC;
    DOCINFOW document{};
    document.cbSize = sizeof(document);
    document.lpszDocName = L"Plan Paint picture";
    bool success = StartDocW(context, &document) > 0;
    if (success) {
        success = StartPage(context) > 0;
    }
    if (success) {
        int page_width = GetDeviceCaps(context, HORZRES);
        int page_height = GetDeviceCaps(context, VERTRES);
        int dpi_x = GetDeviceCaps(context, LOGPIXELSX), dpi_y = GetDeviceCaps(context, LOGPIXELSY);
        int offset_x = GetDeviceCaps(context, PHYSICALOFFSETX),
            offset_y = GetDeviceCaps(context, PHYSICALOFFSETY);
        int left = std::max(0, static_cast<int>(print_margins.left * dpi_x / margin_units) - offset_x);
        int top = std::max(0, static_cast<int>(print_margins.top * dpi_y / margin_units) - offset_y);
        int right = std::min(page_width, GetDeviceCaps(context, PHYSICALWIDTH) - offset_x -
                                             static_cast<int>(print_margins.right * dpi_x / margin_units));
        int bottom = std::min(page_height, GetDeviceCaps(context, PHYSICALHEIGHT) - offset_y -
                                               static_cast<int>(print_margins.bottom * dpi_y / margin_units));
        page_width = std::max(1, right - left);
        page_height = std::max(1, bottom - top);
        double scale = std::min(static_cast<double>(page_width) / image.width,
                                static_cast<double>(page_height) / image.height);
        int width = static_cast<int>(image.width * scale);
        int height = static_cast<int>(image.height * scale);
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = image.width;
        bitmap.bmiHeader.biHeight = -image.height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(context, HALFTONE);
        int result = StretchDIBits(context, left + (page_width - width) / 2, top + (page_height - height) / 2,
                                   width, height, 0, 0, image.width, image.height, flat.pixels.data(),
                                   &bitmap, DIB_RGB_COLORS, SRCCOPY);
        success = result != static_cast<int>(GDI_ERROR) && result > 0 && EndPage(context) > 0;
    }
    if (success) {
        success = EndDoc(context) > 0;
    } else {
        AbortDoc(context);
    }
    DeleteDC(context);
    return success;
}
} // namespace paint

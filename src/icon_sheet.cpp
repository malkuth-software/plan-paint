#include "icon_sheet.hpp"
#include "text.hpp"
#include <algorithm>
#include <stdexcept>
namespace paint {
namespace {
bool cancelled(const std::atomic<bool>* cancel) {
    return cancel && (*cancel).load(std::memory_order_relaxed);
}
std::string identity(const IconFrame& frame, int index) {
    std::string text = "#" + std::to_string(index + 1) + "  " + std::to_string(frame.image.width) + " x " +
                       std::to_string(frame.image.height);
    if (frame.source) {
        const IconFormat format = (*frame.source).format;
        text += "\n" + std::to_string(format.bits_per_pixel) + " bpp " +
                (format.storage == IconStorage::Png ? "PNG" : "DIB");
    } else {
        text += "\nRGBA";
    }
    return text;
}
Color background_pixel(const IconSheetOptions& options, int x, int y) {
    switch (options.background) {
    case IconSheetBackground::Dark:
        return {35, 42, 53, 255};
    case IconSheetBackground::Checker:
        return ((x / 8 + y / 8) % 2) ? Color{205, 211, 219, 255} : Color{250, 250, 250, 255};
    case IconSheetBackground::Transparent:
        return {0, 0, 0, 0};
    case IconSheetBackground::Custom:
        return {options.custom.r, options.custom.g, options.custom.b, 255};
    default:
        return {255, 255, 255, 255};
    }
}
} // namespace
IconSheetLayout plan_icon_sheet(const ImageContainer& source, const std::vector<int>& selection,
                                const IconSheetOptions& options) {
    if (selection.empty() || selection.size() > 256) {
        throw std::runtime_error("Choose 1 to 256 representations for the contact sheet.");
    }
    if (options.columns < 1 || options.columns > 16 || options.padding < 0 || options.padding > 64 ||
        options.gap < 0 || options.gap > 64 || options.label_size < 8 || options.label_size > 24) {
        throw std::runtime_error("Contact sheet layout settings are outside their supported ranges.");
    }
    IconSheetLayout result;
    TextStyle style;
    style.size = options.label_size;
    int cell_width = 1, image_height = 1, label_height = 0;
    for (int index : selection) {
        if (index < 0 || static_cast<std::size_t>(index) >= source.frames.size()) {
            throw std::runtime_error("A selected representation is unavailable.");
        }
        const IconFrame& frame = source.frames[index];
        if (frame.image.width < 1 || frame.image.height < 1 || frame.image.width > 256 ||
            frame.image.height > 256 ||
            frame.image.pixels.size() != static_cast<std::size_t>(frame.image.width) * frame.image.height) {
            throw std::runtime_error("Contact sheets require valid icon images no larger than 256 by 256.");
        }
        if (options.background == IconSheetBackground::Transparent) {
            for (std::size_t pixel = 0; pixel < frame.image.pixels.size(); ++pixel) {
                if (legacy_xor_pixel(frame, pixel)) {
                    throw std::runtime_error("Legacy XOR images need an opaque proof background.");
                }
            }
        }
        IconSheetCell cell;
        cell.source_index = index;
        cell.image = {0, 0, frame.image.width, frame.image.height};
        cell.text = identity(frame, index);
        cell_width = std::max(cell_width, frame.image.width);
        image_height = std::max(image_height, frame.image.height);
        if (options.labels) {
            const TextLayout text = layout_text(cell.text, style);
            cell_width = std::max(cell_width, text.width);
            label_height = std::max(label_height, text.height);
        }
        result.cells.push_back(std::move(cell));
    }
    const int columns = std::min(options.columns, static_cast<int>(selection.size()));
    const int rows = (static_cast<int>(selection.size()) + columns - 1) / columns;
    const int cell_height = image_height + (options.labels ? 6 + label_height : 0);
    const std::uint64_t width =
        2 * options.padding + std::uint64_t(columns) * cell_width + std::uint64_t(columns - 1) * options.gap;
    const std::uint64_t height =
        2 * options.padding + std::uint64_t(rows) * cell_height + std::uint64_t(rows - 1) * options.gap;
    if (width > 32768 || height > 32768 || width * height > 16000000) {
        throw std::runtime_error(
            "Contact sheet exceeds the 16 megapixel output limit. Use fewer images or columns.");
    }
    result.width = static_cast<int>(width);
    result.height = static_cast<int>(height);
    for (std::size_t index = 0; index < result.cells.size(); ++index) {
        IconSheetCell& cell = result.cells[index];
        const int left = options.padding + static_cast<int>(index % columns) * (cell_width + options.gap);
        const int top = options.padding + static_cast<int>(index / columns) * (cell_height + options.gap);
        cell.image.x = left + (cell_width - cell.image.w) / 2;
        cell.image.y = top + (image_height - cell.image.h) / 2;
        cell.label = {static_cast<double>(left), static_cast<double>(top + image_height + 6)};
    }
    return result;
}
Image render_icon_sheet(const ImageContainer& source, const std::vector<int>& selection,
                        const IconSheetOptions& options, const std::atomic<bool>* cancel) {
    if (cancelled(cancel)) {
        return {};
    }
    const IconSheetLayout layout = plan_icon_sheet(source, selection, options);
    Image output;
    output.reset(layout.width, layout.height, {0, 0, 0, 0});
    for (int y = 0; y < output.height; ++y) {
        if (cancelled(cancel)) {
            return {};
        }
        for (int x = 0; x < output.width; ++x) {
            output.set(x, y, background_pixel(options, x, y));
        }
    }
    TextStyle style;
    style.size = options.label_size;
    const Color base = background_pixel(options, 0, 0);
    const Color ink =
        base.a && (int(base.r) + base.g + base.b < 384) ? Color{255, 255, 255, 255} : Color{25, 35, 50, 255};
    for (const IconSheetCell& cell : layout.cells) {
        const IconFrame& frame = source.frames[cell.source_index];
        for (int y = 0; y < cell.image.h; ++y) {
            if (cancelled(cancel)) {
                return {};
            }
            for (int x = 0; x < cell.image.w; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * cell.image.w + x;
                const int target_x = cell.image.x + x, target_y = cell.image.y + y;
                if (legacy_xor_pixel(frame, index)) {
                    const Color back = output.get(target_x, target_y), mask = frame.xor_pixels[index];
                    output.set(target_x, target_y,
                               {static_cast<std::uint8_t>(back.r ^ mask.r),
                                static_cast<std::uint8_t>(back.g ^ mask.g),
                                static_cast<std::uint8_t>(back.b ^ mask.b), 255});
                } else {
                    output.blend(target_x, target_y, frame.image.pixels[index]);
                }
            }
        }
        if (options.labels) {
            draw_text(output, cell.label, cell.text, style, ink, {}, "");
        }
    }
    return cancelled(cancel) ? Image{} : output;
}
} // namespace paint

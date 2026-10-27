#include "toolbar_strip.hpp"
#include "conv.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
namespace paint {
namespace {
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && (*cancel).load()) {
        throw std::runtime_error("Toolbar strip cancelled.");
    }
}
} // namespace
std::vector<int> parse_strip_order(const std::string& text, int count) {
    std::vector<int> result;
    std::size_t start = 0;
    do {
        const std::size_t comma = text.find(',', start);
        std::string token = text.substr(start, comma == std::string::npos ? comma : comma - start);
        const std::size_t first = token.find_first_not_of(" \t\r\n"),
                          last = token.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) {
            throw std::runtime_error("Enter comma-separated image numbers, starting at 1.");
        }
        token = token.substr(first, last - first + 1);
        int ordinal = 0;
        const std::from_chars_result parsed =
            std::from_chars(token.data(), token.data() + token.size(), ordinal);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || ordinal < 1 ||
            ordinal > count || result.size() >= 256) {
            throw std::runtime_error("Choose 1 to 256 valid image numbers in output order.");
        }
        result.push_back(ordinal - 1);
        if (comma == std::string::npos) {
            return result;
        }
        start = comma + 1;
    } while (start <= text.size());
    return result;
}
ToolbarStripPlan plan_toolbar_strip(const ImageContainer& source, const std::vector<int>& order,
                                    const ToolbarStripOptions& options) {
    if (order.empty() || order.size() > 256 || options.width < 1 || options.width > 4096 ||
        options.height < 1 || options.height > 4096 || options.columns < 1 || options.columns > 256 ||
        options.gap_x < 0 || options.gap_x > 256 || options.gap_y < 0 || options.gap_y > 256) {
        throw std::runtime_error("Toolbar strip settings exceed supported ranges.");
    }
    const int columns = std::min(options.columns, static_cast<int>(order.size()));
    const int rows = (static_cast<int>(order.size()) + columns - 1) / columns;
    ToolbarStripPlan plan;
    plan.width = columns * options.width + (columns - 1) * options.gap_x;
    plan.height = rows * options.height + (rows - 1) * options.gap_y;
    if (plan.width > 32768 || plan.height > 32768 || std::uint64_t(plan.width) * plan.height > 100000000) {
        throw std::runtime_error("Toolbar strip exceeds the image working limits.");
    }
    for (std::size_t position = 0; position < order.size(); ++position) {
        const int index = order[position];
        if (index < 0 || static_cast<std::size_t>(index) >= source.frames.size()) {
            throw std::runtime_error("An image is unavailable.");
        }
        const IconFrame& frame = source.frames[index];
        const Image& image = frame.image;
        if (image.width < 1 || image.height < 1 || image.width > 32768 || image.height > 32768 ||
            image.pixels.size() != std::size_t(image.width) * image.height) {
            throw std::runtime_error("Invalid toolbar image.");
        }
        for (std::size_t pixel = 0; pixel < image.pixels.size(); ++pixel) {
            if (legacy_xor_pixel(frame, pixel)) {
                throw std::runtime_error("Legacy XOR cannot be exported as toolbar RGBA pixels.");
            }
        }
        int width = image.width, height = image.height;
        if (options.fit) {
            const double scale = std::min(double(options.width) / width, double(options.height) / height);
            width = std::clamp(static_cast<int>(std::round(width * scale)), 1, options.width);
            height = std::clamp(static_cast<int>(std::round(height * scale)), 1, options.height);
        } else if (width > options.width || height > options.height) {
            throw std::runtime_error(
                "An image exceeds its cell. Increase cell dimensions or enable Fit image.");
        }
        const int x = int(position % columns) * (options.width + options.gap_x) + (options.width - width) / 2;
        const int y =
            int(position / columns) * (options.height + options.gap_y) + (options.height - height) / 2;
        plan.cells.push_back({index, {x, y, width, height}, width > image.width || height > image.height});
    }
    return plan;
}
Image render_toolbar_strip(const ImageContainer& source, const std::vector<int>& order,
                           const ToolbarStripOptions& options, const std::atomic<bool>* cancel) {
    check_cancel(cancel);
    const ToolbarStripPlan plan = plan_toolbar_strip(source, order, options);
    Image output;
    const Color matte{options.matte.r, options.matte.g, options.matte.b, 255};
    output.reset(plan.width, plan.height, options.opaque ? matte : Color{0, 0, 0, 0});
    for (const ToolbarStripCell& cell : plan.cells) {
        check_cancel(cancel);
        const Image& original = source.frames[cell.index].image;
        Image resized;
        const Image* image = &original;
        if (cell.image.w != original.width || cell.image.h != original.height) {
            conv_resize_area(original, cell.image.w, cell.image.h, resized);
            image = &resized;
        }
        for (int y = 0; y < cell.image.h; ++y) {
            check_cancel(cancel);
            for (int x = 0; x < cell.image.w; ++x) {
                const Color pixel = (*image).get(x, y);
                if (options.opaque) {
                    output.blend(cell.image.x + x, cell.image.y + y, pixel);
                } else {
                    output.set(cell.image.x + x, cell.image.y + y, pixel);
                }
            }
        }
    }
    check_cancel(cancel);
    return output;
}
} // namespace paint

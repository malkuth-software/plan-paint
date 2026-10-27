#include "sticky_note.hpp"
#include "paper.hpp"
#include <stdexcept>
namespace paint {
namespace {
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && (*cancel).load()) {
        throw std::runtime_error("Sticky note preview cancelled.");
    }
}
} // namespace
Image render_sticky_paper(const StickyNoteSettings& settings, const std::atomic<bool>* cancel) {
    check_cancel(cancel);
    if (settings.width < 16 || settings.height < 16 || settings.width > paper::max_side ||
        settings.height > paper::max_side || settings.paper < 0 || settings.paper >= paper::preset_count ||
        settings.paper_color.a != 255) {
        throw std::invalid_argument(
            "Use 16–4096 pixel notes, an opaque paper color, and padding smaller than half the note.");
    }
    paper::Parameters parameters = paper::preset(static_cast<paper::Preset>(settings.paper));
    parameters.colour = {settings.paper_color.r / 255.0, settings.paper_color.g / 255.0,
                         settings.paper_color.b / 255.0};
    parameters.seed = settings.seed;
    paper::Image sheet;
    if (!paper::generate_sheet(parameters, settings.width, settings.height, paper::Shading{}, sheet)) {
        throw std::runtime_error("The paper generator could not create this note.");
    }
    check_cancel(cancel);
    Image result;
    result.reset(sheet.width, sheet.height);
    for (std::size_t index = 0; index < result.pixels.size(); ++index) {
        result.pixels[index] = {sheet.rgba[index * 4], sheet.rgba[index * 4 + 1], sheet.rgba[index * 4 + 2],
                                sheet.rgba[index * 4 + 3]};
    }
    return result;
}
StickyNoteResult render_sticky_note(const StickyNoteSettings& settings, const std::atomic<bool>* cancel) {
    if (settings.padding < 0 || settings.padding > 2047 || settings.padding * 2 >= settings.width ||
        settings.padding * 2 >= settings.height || settings.font.size < 8 || settings.font.size > 256 ||
        settings.text.size() > 8191) {
        throw std::invalid_argument("Invalid note text, font size or padding.");
    }
    StickyNoteResult result;
    result.image = render_sticky_paper(settings, cancel);
    TextStyle font = settings.font;
    font.opaque = false;
    font.word_wrap = true;
    const int width = settings.width - 2 * settings.padding, height = settings.height - 2 * settings.padding;
    const TextLayout layout = layout_text(settings.text, font, width);
    result.text_clipped = !settings.text.empty() && (layout.width > width || layout.height > height);
    Image ink;
    ink.reset(width, height, {0, 0, 0, 0});
    render_text(ink, {0, 0}, layout, font, settings.ink_color, {0, 0, 0, 0});
    check_cancel(cancel);
    composite(result.image, ink, settings.padding, settings.padding);
    return result;
}
} // namespace paint

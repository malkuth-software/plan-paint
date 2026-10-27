#include "text_session.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace paint {
namespace {
std::string scalar_key(double value) {
    // This is an internal cache identity, not display text. Keep every bit and
    // avoid locale-sensitive printf conversions in fortified musl/LTO builds.
    return std::to_string(std::bit_cast<std::uint64_t>(value));
}
} // namespace
void TextSession::begin(Point origin) {
    clear();
    active = true;
    bounds = {static_cast<int>(std::round(origin.x)), static_cast<int>(std::round(origin.y)), 440, 160};
}
void TextSession::clear() {
    active = false;
    edit = {};
    undo_.clear();
    redo_.clear();
    signature_.clear();
    paper_signature_.clear();
    paper_ = {};
    note.reset();
    preview = {};
    layout = {};
}
void TextSession::refresh(Color foreground, Color background) {
    if (!active) {
        return;
    }
    std::string signature =
        edit.content + "\n" + style.face_path + "@" + std::to_string(style.size) + ":" +
        std::to_string(bounds.w) + ":" + std::to_string(bounds.h) + (style.bold ? "b" : "-") +
        (style.italic ? "i" : "-") + (style.underline ? "u" : "-") + (style.strikeout ? "s" : "-") +
        (style.mono ? "m" : "-") + (style.opaque ? "o" : "-") + (style.word_wrap ? "w" : "-") +
        to_hex(foreground) + std::to_string(foreground.a) + ":" + to_hex(background) +
        std::to_string(background.a) + ":" + std::to_string(style.contour) + ":" +
        scalar_key(style.outline_width) + ":" + scalar_key(style.skew) + ":" + scalar_key(style.perspective) +
        ":" + scalar_key(style.warp) + ":" + std::to_string(static_cast<int>(style.word_art));
    const int padding = inset();
    if (note) {
        signature += ":paper:" + std::to_string((*note).paper) + ":" + std::to_string((*note).seed) +
                     ":" + to_hex((*note).paper_color) + ":" + std::to_string(padding);
    }
    if (signature == signature_) {
        return;
    }
    TextStyle lettering = style;
    if (note) { lettering.opaque = false; }
    layout = layout_text(edit.content, lettering, bounds.w - 2 * padding);
    if (note) {
        const std::string paper_key = std::to_string(bounds.w) + ":" + std::to_string(bounds.h) + ":" +
            std::to_string((*note).paper) + ":" + std::to_string((*note).seed) + ":" + to_hex((*note).paper_color);
        if (paper_key != paper_signature_) {
            StickyNoteSettings settings = *note;
            settings.width = bounds.w; settings.height = bounds.h;
            paper_ = render_sticky_paper(settings);
            paper_signature_ = paper_key;
        }
        preview = paper_;
        Image letters;
        letters.reset(bounds.w - 2 * padding, bounds.h - 2 * padding, {0, 0, 0, 0});
        render_text(letters, {0, 0}, layout, lettering, foreground, background);
        composite(preview, letters, padding, padding);
    } else {
        preview.reset(bounds.w, bounds.h, style.opaque ? background : Color{0, 0, 0, 0});
        render_text(preview, {0, 0}, layout, style, foreground, background);
    }
    if (!note && (style.skew != 0 || style.perspective != 0 || style.warp != 0)) {
        Image source = std::move(preview);
        preview.reset(bounds.w, bounds.h, {0, 0, 0, 0});
        for (int y = 0; y < bounds.h; ++y) {
            for (int x = 0; x < bounds.w; ++x) {
                Point p = source_point({x + 0.5, y + 0.5});
                double sx = p.x - 0.5, sy = p.y - 0.5;
                int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
                double alpha = 0, red = 0, green = 0, blue = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        Color c = source.contains(ix + dx, iy + dy) ? source.get(ix + dx, iy + dy)
                                                                    : Color{0, 0, 0, 0};
                        double weight = (dx ? sx - ix : 1 - sx + ix) * (dy ? sy - iy : 1 - sy + iy) * c.a;
                        alpha += weight;
                        red += weight * c.r;
                        green += weight * c.g;
                        blue += weight * c.b;
                    }
                }
                if (alpha > 0) {
                    preview.set(x, y,
                                {static_cast<std::uint8_t>(std::round(red / alpha)),
                                 static_cast<std::uint8_t>(std::round(green / alpha)),
                                 static_cast<std::uint8_t>(std::round(blue / alpha)),
                                 static_cast<std::uint8_t>(std::clamp(std::round(alpha), 0.0, 255.0))});
                }
            }
        }
    }
    signature_ = std::move(signature);
    edit.caret = std::min(edit.caret, edit.content.size());
    edit.anchor = std::min(edit.anchor, edit.content.size());
}
bool TextSession::replace(const std::string& inserted) {
    std::size_t first = std::min(edit.caret, edit.anchor), last = std::max(edit.caret, edit.anchor);
    std::string value = inserted;
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\0'), value.end());
    std::size_t available = 8191 - (edit.content.size() - (last - first));
    if (value.size() > available) {
        std::size_t length = available;
        while (length > 0 && (static_cast<unsigned char>(value[length]) & 192) == 128) {
            --length;
        }
        value.resize(length);
    }
    if (first == last && value.empty()) {
        return false;
    }
    undo_.push_back(edit);
    if (undo_.size() > 64) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    edit.content.replace(first, last - first, value);
    edit.caret = edit.anchor = first + value.size();
    return true;
}
void TextSession::history(bool redo) {
    std::vector<TextEditState>& source = redo ? redo_ : undo_;
    std::vector<TextEditState>& destination = redo ? undo_ : redo_;
    if (source.empty()) {
        return;
    }
    destination.push_back(edit);
    edit = std::move(source.back());
    source.pop_back();
}
std::size_t TextSession::caret_at(Point point) const {
    if (layout.carets.empty()) {
        return 0;
    }
    double best = 1e100;
    std::size_t result = 0;
    for (std::size_t i = 0; i <= layout.glyphs.size(); ++i) {
        std::size_t offset = i == layout.glyphs.size() ? layout.carets.size() - 1 : layout.glyphs[i].begin;
        Point caret = layout.carets[offset];
        double row = std::abs(std::floor(point.y / layout.line_height) - caret.y / layout.line_height);
        double distance = row * 100000 + std::abs(point.x - caret.x);
        if (distance < best) {
            best = distance;
            result = offset;
        }
    }
    return result;
}
// A bounded trapezoid/shear followed by a sine bend. The analytic inverse is
// shared by raster sampling and pointer selection so text remains editable.
Point TextSession::display_point(Point point) const {
    if (note) { return {point.x + inset(), point.y + inset()}; }
    double u = point.x / bounds.w, v = point.y / bounds.h;
    double skew = std::clamp(style.skew * bounds.h / bounds.w, -0.7, 0.7);
    double perspective = std::clamp(style.perspective, -0.8, 0.8);
    double bend = std::clamp(style.warp, -0.4, 0.4);
    double width = (1 - std::abs(skew)) * (1 - std::abs(perspective) * (perspective >= 0 ? 1 - v : v));
    double x = 0.5 + (u - 0.5) * width + skew * (v - 0.5);
    double y = v * (1 - std::abs(bend)) + std::max(0.0, -bend) + bend * std::sin(3.14159265358979323846 * x);
    return {x * bounds.w, y * bounds.h};
}
Point TextSession::source_point(Point point) const {
    if (note) { return {point.x - inset(), point.y - inset()}; }
    double x = point.x / bounds.w, y = point.y / bounds.h;
    double skew = std::clamp(style.skew * bounds.h / bounds.w, -0.7, 0.7);
    double perspective = std::clamp(style.perspective, -0.8, 0.8);
    double bend = std::clamp(style.warp, -0.4, 0.4);
    double v =
        (y - std::max(0.0, -bend) - bend * std::sin(3.14159265358979323846 * x)) / (1 - std::abs(bend));
    double width = (1 - std::abs(skew)) * (1 - std::abs(perspective) * (perspective >= 0 ? 1 - v : v));
    double u = 0.5 + (x - 0.5 - skew * (v - 0.5)) / std::max(0.02, width);
    return {u * bounds.w, v * bounds.h};
}
void TextSession::resize(Rect rectangle) {
    bounds = rectangle;
    const int maximum = note ? 4096 : 8192;
    bounds.w = std::clamp(bounds.w, 24, maximum);
    bounds.h = std::clamp(bounds.h, 24, std::min(maximum, 16000000 / bounds.w));
}
int TextSession::inset() const {
    return note ? std::clamp((*note).padding, 0, (std::min(bounds.w, bounds.h) - 1) / 2) : 0;
}
} // namespace paint

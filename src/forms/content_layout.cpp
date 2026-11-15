#include "forms/content_layout.hpp"
#include <gui_forms/text/text_store/text_store.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
// A row of the layout before its clusters are known: its byte range and the
// y of its top, in text-box pixels.
struct LayoutRow {
    std::size_t start = 0;
    std::size_t end = 0;
    double top = 0;
};
// How boxes reach the editor: either kept in text-box pixels under the
// layout's transform (`affine`), or mapped here through the display mapping
// and the view as bounding boxes.
struct BoxMapping {
    const TextSession& session;
    const gf::ContentLayoutTransform& view;
    bool affine = false;
};
// The text-box rectangle (left, top, width, height) as a layout box.
gf::Rect mapped_box(const BoxMapping& mapping, double left, double top, double width, double height) {
    if (mapping.affine) {
        const gf::Rect raw{left, top, width, height};
        return raw;
    }
    const Point display[4] = {mapping.session.display_point({left, top}),
                              mapping.session.display_point({left + width, top}),
                              mapping.session.display_point({left, top + height}),
                              mapping.session.display_point({left + width, top + height})};
    gf::Point corners[4];
    for (int index = 0; index < 4; ++index) {
        corners[index] = mapping.view.map({display[index].x, display[index].y});
    }
    double minimum_x = corners[0].x;
    double maximum_x = corners[0].x;
    double minimum_y = corners[0].y;
    double maximum_y = corners[0].y;
    for (const gf::Point& corner : corners) {
        minimum_x = std::min(minimum_x, corner.x);
        maximum_x = std::max(maximum_x, corner.x);
        minimum_y = std::min(minimum_y, corner.y);
        maximum_y = std::max(maximum_y, corner.y);
    }
    const gf::Rect result{minimum_x, minimum_y, maximum_x - minimum_x, maximum_y - minimum_y};
    return result;
}
// `outer` after `inner`: local = outer(inner(point)).
gf::ContentLayoutTransform compose(const gf::ContentLayoutTransform& outer, const gf::ContentLayoutTransform& inner) {
    gf::ContentLayoutTransform result;
    result.xx = outer.xx * inner.xx + outer.xy * inner.yx;
    result.xy = outer.xx * inner.xy + outer.xy * inner.yy;
    result.yx = outer.yx * inner.xx + outer.yy * inner.yx;
    result.yy = outer.yx * inner.xy + outer.yy * inner.yy;
    result.dx = outer.xx * inner.dx + outer.xy * inner.dy + outer.dx;
    result.dy = outer.yx * inner.dx + outer.yy * inner.dy + outer.dy;
    return result;
}
// The rows of `layout` over `text`, in text order. A wrap that would split a
// grapheme (a zero-width mark pushed past the wrap width) stays on its row.
std::vector<LayoutRow> layout_rows(const TextLayout& layout, const std::string& text, const gf::TextStore& store) {
    std::vector<LayoutRow> rows;
    LayoutRow row;
    for (const TextGlyph& glyph : layout.glyphs) {
        if (glyph.codepoint == '\n') {
            row.end = glyph.begin;
            rows.push_back(row);
            row.start = glyph.end;
            row.top = glyph.y + layout.line_height;
            continue;
        }
        const bool wrapped = glyph.y != row.top && glyph.begin > row.start;
        if (wrapped && store.is_grapheme_boundary(gf::Utf8Offset(glyph.begin))) {
            row.end = glyph.begin;
            rows.push_back(row);
            row.start = glyph.begin;
            row.top = glyph.y;
        }
    }
    row.end = text.size();
    rows.push_back(row);
    return rows;
}
} // namespace

std::optional<gf::ContentLayoutTransform> affine_display(const TextSession& session) {
    if (!session.note && (session.style.perspective != 0 || session.style.warp != 0)) {
        return std::nullopt;
    }
    // Affine: three points fix the map (display_point is a shear and a
    // scale about the box's centre, or the note's inset).
    const Point origin = session.display_point({0.0, 0.0});
    const Point unit_x = session.display_point({1.0, 0.0});
    const Point unit_y = session.display_point({0.0, 1.0});
    gf::ContentLayoutTransform result;
    result.xx = unit_x.x - origin.x;
    result.yx = unit_x.y - origin.y;
    result.xy = unit_y.x - origin.x;
    result.yy = unit_y.y - origin.y;
    result.dx = origin.x;
    result.dy = origin.y;
    return result;
}

gf::ContentTextLayout content_layout(const TextSession& session, const std::string& text, std::uint64_t revision,
                                     const gf::ContentLayoutTransform& view) {
    gf::ContentTextLayout result;
    result.text_revision = revision;
    const TextLayout& layout = session.layout;
    if (layout.carets.size() != text.size() + 1) {
        return result;
    }
    const std::optional<gf::ContentLayoutTransform> display = affine_display(session);
    BoxMapping mapping{session, view, display.has_value()};
    // The caret bar is one client point wide: in text-box pixels under the
    // transform, the inverse of its mean scale.
    double caret_width = 1.0;
    if (display) {
        result.transform = compose(view, *display);
        const double scale = std::sqrt(std::abs((*result.transform).determinant()));
        caret_width = scale > 0.0 ? 1.0 / scale : 1.0;
    }
    const gf::TextStore store(text);
    const double line_height = layout.line_height;
    const std::vector<LayoutRow> rows = layout_rows(layout, text, store);
    result.lines.reserve(rows.size());
    result.clusters.reserve(layout.glyphs.size());
    result.carets.reserve(layout.glyphs.size() + 1);
    // The glyphs are in text order, one per code point: one cursor walks them
    // beside the graphemes.
    std::size_t glyph_index = 0;
    for (std::size_t line_index = 0; line_index < rows.size(); ++line_index) {
        const LayoutRow& row = rows[line_index];
        // The row's right edge is its widest cluster's (the caret table
        // cannot say: a wrapped row's end byte holds the next row's caret).
        double right = 0.0;
        std::size_t position = row.start;
        while (position < row.end) {
            const std::size_t next = std::min(store.next_grapheme_boundary(gf::Utf8Offset(position)).value(), row.end);
            while (glyph_index < layout.glyphs.size() && layout.glyphs[glyph_index].begin < position) {
                ++glyph_index;
            }
            double left = layout.carets[position].x;
            double extent = left;
            double top = row.top;
            for (std::size_t index = glyph_index;
                 index < layout.glyphs.size() && layout.glyphs[index].begin < next; ++index) {
                const TextGlyph& glyph = layout.glyphs[index];
                left = std::min(left, static_cast<double>(glyph.x));
                extent = std::max(extent, static_cast<double>(glyph.x + std::max(glyph.advance, 0)));
                top = static_cast<double>(glyph.y);
            }
            gf::ContentTextCluster cluster;
            cluster.range = {gf::Utf8Offset(position), gf::Utf8Offset(next)};
            cluster.line = static_cast<std::uint32_t>(line_index);
            cluster.bounds = mapped_box(mapping, left, top, extent - left, line_height);
            cluster.right_to_left = false;
            result.clusters.push_back(cluster);
            right = std::max(right, extent);
            position = next;
        }
        gf::ContentTextLine line;
        line.range = {gf::Utf8Offset(row.start), gf::Utf8Offset(row.end)};
        line.bounds = mapped_box(mapping, 0.0, row.top, right, line_height);
        result.lines.push_back(line);
    }
    // One caret stop per grapheme boundary, ascending. Each is the bar at
    // the layout's caret for that byte, one client point wide.
    std::size_t boundary = 0;
    while (true) {
        const Point caret = layout.carets[boundary];
        gf::Rect bar = mapped_box(mapping, caret.x, caret.y, 0.0, line_height);
        bar.width = std::max(bar.width, caret_width);
        gf::ContentCaretStop stop;
        stop.offset = gf::Utf8Offset(boundary);
        stop.caret = bar;
        result.carets.push_back(stop);
        if (boundary >= text.size()) {
            break;
        }
        boundary = store.next_grapheme_boundary(gf::Utf8Offset(boundary)).value();
    }
    return result;
}
} // namespace paint::forms

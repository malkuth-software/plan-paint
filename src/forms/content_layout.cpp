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
// The bounding box, in editor coordinates, of the text-box rectangle
// (left, top, width, height) mapped through the session's display mapping.
gf::Rect mapped_box(const TextSession& session, double left, double top, double width, double height, double zoom) {
    const Point corners[4] = {session.display_point({left, top}), session.display_point({left + width, top}),
                              session.display_point({left, top + height}),
                              session.display_point({left + width, top + height})};
    double minimum_x = corners[0].x;
    double maximum_x = corners[0].x;
    double minimum_y = corners[0].y;
    double maximum_y = corners[0].y;
    for (const Point& corner : corners) {
        minimum_x = std::min(minimum_x, corner.x);
        maximum_x = std::max(maximum_x, corner.x);
        minimum_y = std::min(minimum_y, corner.y);
        maximum_y = std::max(maximum_y, corner.y);
    }
    const gf::Rect result{minimum_x * zoom, minimum_y * zoom, (maximum_x - minimum_x) * zoom,
                          (maximum_y - minimum_y) * zoom};
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

gf::ContentTextLayout content_layout(const TextSession& session, const std::string& text, std::uint64_t revision,
                                     double zoom) {
    gf::ContentTextLayout result;
    result.text_revision = revision;
    const TextLayout& layout = session.layout;
    if (layout.carets.size() != text.size() + 1) {
        return result;
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
            cluster.bounds = mapped_box(session, left, top, extent - left, line_height, zoom);
            cluster.right_to_left = false;
            result.clusters.push_back(cluster);
            right = std::max(right, extent);
            position = next;
        }
        gf::ContentTextLine line;
        line.range = {gf::Utf8Offset(row.start), gf::Utf8Offset(row.end)};
        line.bounds = mapped_box(session, 0.0, row.top, right, line_height, zoom);
        result.lines.push_back(line);
    }
    // One caret stop per grapheme boundary, ascending. Each is the bar at
    // the layout's caret for that byte, one bitmap pixel wide before
    // mapping, and at least one client point wide after it.
    std::size_t boundary = 0;
    while (true) {
        const Point caret = layout.carets[boundary];
        gf::Rect bar = mapped_box(session, caret.x, caret.y, 0.0, line_height, zoom);
        bar.width = std::max(bar.width, 1.0);
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

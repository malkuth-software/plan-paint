#include "document.hpp"
#include <algorithm>
#include <cmath>
namespace paint {
void Document::begin_curve(CurveKind kind, Point start, bool secondary) {
    settle_selection();
    commit_path();
    commit_curve();
    curve.geometry.kind = kind;
    curve.geometry.set_line(start, start);
    curve.base = std::make_shared<const Image>(image);
    curve.session = next_curve_session++;
    curve.secondary = secondary;
    curve.ink = ink;
}
bool Document::establish_curve(Point end) {
    if (!curve.base || curve.line_set ||
        std::hypot(end.x - curve.geometry.start.x, end.y - curve.geometry.start.y) < 1e-9) {
        return false;
    }
    checkpoint();
    curve.geometry.set_line(curve.geometry.start, end);
    curve.line_set = true;
    sync_curve();
    return true;
}
Image Document::curve_image(const Point* pending_end) const {
    if (!curve.base || (!curve.line_set && !pending_end)) {
        return image;
    }
    Image result = *curve.base;
    CurveGeometry geometry = curve.geometry;
    if (!curve.line_set && pending_end) {
        geometry.set_line(geometry.start, *pending_end);
    }
    Ink stroke_ink = curve.secondary ? alternate_ink() : primary_ink();
    const double tolerance = stroke_ink.smooth && stroke_ink.supersample ? 0.03125 : 0.125;
    polygon(result, geometry.samples(tolerance), stroke_ink, true, false, false);
    constrain_selection(result, *curve.base);
    return result;
}
void Document::sync_curve() {
    if (!curve.base || !curve.line_set) {
        return;
    }
    curve.ink = ink;
    Image rendered = curve_image();
    if (!std::equal(image.pixels.begin(), image.pixels.end(), rendered.pixels.begin(), rendered.pixels.end(),
                    equal)) {
        image = std::move(rendered);
        if (revision == saved_revision) {
            revision = next_revision++;
        }
    }
}
void Document::commit_curve() {
    sync_curve();
    curve = {};
}
void Document::restore_curve(const EditableCurve& previous) {
    if (curve.session == 0 || curve.session != previous.session) {
        std::uint64_t session = curve.session;
        curve = {};
        curve.session = session;
        return;
    }
    curve = previous;
    if (curve.base) {
        ink = curve.ink;
    }
}
} // namespace paint

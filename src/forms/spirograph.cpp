#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "forms/spirograph_view.hpp"
#include "spiro_quad.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
constexpr double tau = 2 * std::numbers::pi;
void disc(gf::Painter& painter, gf::Point c, double r, gf::Color color) {
    painter.fill_rounded_rect({c.x - r, c.y - r, 2 * r, 2 * r}, r, color);
}
void circle(gf::Painter& painter, gf::Point c, double r, gf::Color color, double width) {
    painter.stroke_rounded_rect({c.x - r, c.y - r, 2 * r, 2 * r}, r, color, width);
}
gf::Point polar(gf::Point center, double radius, double angle) {
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}
gf::Point profile_point(gf::Point center, double radius, const SpiroProfile& profile, double a,
                        double rotation) {
    const Point p = profile.point(a);
    const double c = std::cos(rotation), sn = std::sin(rotation);
    return {center.x + radius * (p.x * c - p.y * sn), center.y + radius * (p.x * sn + p.y * c)};
}
void contour_shape(gf::Painter& painter, const std::vector<gf::Point>& points,
                   gf::Color fill, gf::Color edge, double width) {
    double top = points.front().y, bottom = top;
    for (const gf::Point point : points) {
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    if (fill.alpha > 0) {
        // Even-odd spans preserve the Quad's concave cutouts and avoid overlapping coats.
        const double step = std::max(1.0, (bottom - top) / 600);
        for (double y = top; y < bottom; y += step) {
            std::vector<double> crossings;
            for (int i = 0; i < static_cast<int>(points.size()); ++i) {
                const gf::Point a = points[i], b = points[(i + 1) % points.size()];
                if ((a.y <= y + step * .5 && b.y > y + step * .5) ||
                    (b.y <= y + step * .5 && a.y > y + step * .5)) {
                    const double x = a.x + (b.x - a.x) * (y + step * .5 - a.y) / (b.y - a.y);
                    crossings.push_back(x);
                }
            }
            std::sort(crossings.begin(), crossings.end());
            for (std::size_t i = 1; i < crossings.size(); i += 2) {
                painter.fill_rect({crossings[i - 1], y, crossings[i] - crossings[i - 1], step}, fill);
            }
        }
    }
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        painter.draw_line(points[i], points[(i + 1) % points.size()], edge, width);
    }
}
void profile_shape(gf::Painter& painter, gf::Point center, double r, const SpiroProfile& profile,
                   double rotation, gf::Color fill, gf::Color edge, double width) {
    if (profile.circular()) {
        if (fill.alpha > 0) {
            disc(painter, center, r, fill);
        }
        circle(painter, center, r, edge, width);
        return;
    }
    std::vector<gf::Point> points;
    for (int i = 0; i < 96; ++i) {
        const gf::Point p = profile_point(center, r, profile, tau * i / 96, rotation);
        points.push_back(p);
    }
    contour_shape(painter, points, fill, edge, width);
}

double profile_clearance(Point point, Point center, double radius, const SpiroProfile& profile,
                         double rotation) {
    const double x = (point.x - center.x) * std::cos(rotation) + (point.y - center.y) * std::sin(rotation);
    const double y = -(point.x - center.x) * std::sin(rotation) + (point.y - center.y) * std::cos(rotation);
    if (profile.kind == SpiroProfileKind::Quad) {
        bool inside = false;
        double nearest = 1e20;
        for (int i = 0; i < 192; ++i) {
            const Point pa = QuadTrack::boundary(tau * i / 192), pb = QuadTrack::boundary(tau * (i + 1) / 192);
            const Point a{pa.x * radius, pa.y * radius}, b{pb.x * radius, pb.y * radius};
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
            nearest = std::min(nearest, std::hypot(x - a.x - t * dx, y - a.y - t * dy));
            if ((a.y > y) != (b.y > y) && x < a.x + dx * (y - a.y) / dy) {
                inside = !inside;
            }
        }
        return inside ? -nearest : nearest;
    }
    double result = -1e20;
    for (int i = 0; i < 96; ++i) {
        const double a = tau * i / 96;
        result = std::max(result, x * std::cos(a) + y * std::sin(a) - radius * profile.support(a));
    }
    return result;
}
void peg_cap(gf::Painter& painter, gf::Point center, double r, const SpiroPeg& peg) {
    disc(painter, {center.x + 1, center.y + 2}, r + 1, gf::Color::rgba(10, 25, 50, 80));
    const Color ink = peg.loaded ? peg.ink : Color{222, 235, 245, 255};
    disc(painter, center, r, gf::Color::rgba(ink.r, ink.g, ink.b, 190));
    if (!peg.preview.pixels.empty()) {
        const double pixel = r * 1.6 / peg.preview.width;
        for (int y = 0; y < peg.preview.height; ++y) {
            for (int x = 0; x < peg.preview.width; ++x) {
                const Color c = peg.preview.get(x, y);
                if (c.a == 0) {
                    continue;
                }
                painter.fill_rect({center.x + (x - peg.preview.width * .5) * pixel,
                                   center.y + (y - peg.preview.height * .5) * pixel, pixel, pixel},
                                  gf::Color::rgba(c.r, c.g, c.b, c.a));
            }
        }
    }
    circle(painter, center, r, gf::Color::rgba(245, 255, 255, 210), 1.3);
    circle(painter, center, r + 1, gf::Color::rgba(20, 60, 90, 210), 1);
    disc(painter, {center.x - r * .24, center.y - r * .32}, r * .28, gf::Color::rgba(255, 255, 255, 160));
    disc(painter, center, 1.3, gf::Color::rgba(20, 45, 65, 210));
}
} // namespace
void paint_spiro_part(gf::Painter& painter, gf::Rect bounds, int kind, int index) {
    const gf::Point c{bounds.x + bounds.width * .5, bounds.y + bounds.height * .5};
    const double r = std::min(bounds.width, bounds.height) * .34;
    if (kind == 2) {
        SpiroPeg peg;
        peg.seated = true;
        peg_cap(painter, c, r * (.6 + .15 * index), peg);
        return;
    }
    const SpiroProfile& profile = kind == 0 ? spiro_guides()[index].profile : spiro_inserts()[index].profile;
    if (kind == 0 && spiro_guides()[index].cap_radius > 0) {
        const SpiroGuide& frame = spiro_guides()[index];
        const double extent = std::numbers::pi * (frame.teeth - frame.cap_radius) / 2 + frame.cap_radius;
        const double cap = 1.4 * r * frame.cap_radius / extent;
        painter.fill_rounded_rect({c.x - 1.4 * r, c.y - cap, 2.8 * r, 2 * cap}, cap,
                                  gf::Color::rgba(60, 215, 70, 150));
        painter.stroke_rounded_rect({c.x - 1.4 * r, c.y - cap, 2.8 * r, 2 * cap}, cap,
                                    gf::Color::rgba(20, 130, 40), 1);
        return;
    }
    if (kind == 0 && spiro_guides()[index].outside_teeth) {
        const double inner = r * spiro_guides()[index].teeth / spiro_guides()[index].outside_teeth;
        circle(painter, c, r, gf::Color::rgba(60, 215, 70, 120), r - inner);
        circle(painter, c, inner, gf::Color::rgba(20, 130, 40), 1);
        circle(painter, c, r, gf::Color::rgba(20, 130, 40), 1);
        return;
    }
    if (kind == 0 && spiro_guides()[index].rack) {
        painter.draw_line({c.x - r * 1.4, c.y}, {c.x + r * 1.4, c.y}, gf::Color::rgba(60, 215, 70, 200), 7);
        for (int i = -6; i <= 6; ++i) {
            painter.draw_line({c.x + i * r / 5, c.y - 5}, {c.x + i * r / 5, c.y},
                              gf::Color::rgba(20, 130, 40), 1);
        }
        return;
    }
    profile_shape(painter, c, r, profile, 0,
                  kind == 0 ? gf::Color::rgba(0, 0, 0, 0) : gf::Color::rgba(0, 170, 255, 100),
                  kind == 0 ? gf::Color::rgba(60, 215, 70, 200) : gf::Color::rgba(10, 120, 225, 220),
                  kind == 0 ? 5 : 1.5);
    if (kind == 1) {
        for (const Point hole : spiro_inserts()[index].holes) {
            circle(painter, {c.x + hole.x * r, c.y + hole.y * r}, 1.4, gf::Color::rgba(250, 255, 255), 1);
        }
    }
}
void Editor::paint_spiro_overlay(gf::Painter& painter) {
    if (!spiro.active) {
        return;
    }
    const double zoom = canvas().zoom(), radius = spiro.guide_radius() * zoom;
    const gf::Point center = screen(spiro.center);
    const SpiroGuide& frame = spiro_guides()[spiro.guide];
    const double rim = std::max(8.0, 10 * spiro.scale * zoom);
    if (spiro.rack()) {
        const gf::Point a = screen(spiro.guide_point(-1.5)), b = screen(spiro.guide_point(1.5));
        painter.draw_line(a, b, gf::Color::rgba(55, 235, 65, 150), rim);
        const int marks = std::max(1, static_cast<int>(3 * frame.teeth / tau));
        for (int i = 0; i <= marks; ++i) {
            const Point p = spiro.guide_point(-1.5 + 3.0 * i / marks);
            painter.draw_line(screen({p.x, p.y - 3 * spiro.scale}), screen({p.x, p.y + 3 * spiro.scale}),
                              gf::Color::rgba(25, 140, 40, 230), std::max(1.0, spiro.scale * zoom));
        }
    } else if (spiro.capsule()) {
        std::vector<gf::Point> contour;
        for (int i = 0; i < 600; ++i) {
            contour.push_back(screen(spiro.guide_point(tau * i / 600)));
        }
        contour_shape(painter, contour, gf::Color::rgba(55, 235, 65, 80),
                      gf::Color::rgba(25, 145, 40, 210), 1.3);
        for (int i = 0; i < frame.teeth; ++i) {
            const double phase = tau * i / frame.teeth;
            const gf::Point p = screen(spiro.guide_point(phase));
            const double normal = spiro.guide_normal(phase) + canvas().view_angle;
            painter.draw_line(polar(p, -1.5 * spiro.scale * zoom, normal),
                              polar(p, 1.5 * spiro.scale * zoom, normal),
                              gf::Color::rgba(25, 140, 40, 230), std::max(1.0, spiro.scale * zoom));
        }
    } else if (frame.outside_teeth) {
        const double inner = frame.teeth * spiro.scale * zoom, outer = spiro.guide_body_radius() * zoom;
        circle(painter, center, outer, gf::Color::rgba(55, 235, 65, 80), outer - inner);
        for (int side = 0; side < 2; ++side) {
            const int teeth = side ? frame.outside_teeth : frame.teeth;
            const double track = teeth * spiro.scale * zoom;
            const bool active = (side != 0) == spiro.outside;
            const gf::Color color = active ? gf::Color::rgba(20, 135, 40, 235)
                                           : gf::Color::rgba(40, 175, 55, 145);
            circle(painter, center, track, color, active ? 1.8 : 1);
            for (int i = 0; i < teeth; ++i) {
                const double angle = tau * i / teeth + canvas().view_angle;
                painter.draw_line(polar(center, track - 1.5 * spiro.scale * zoom, angle),
                                  polar(center, track + 1.5 * spiro.scale * zoom, angle), color,
                                  std::max(1.0, spiro.scale * zoom));
            }
        }
    } else {
        profile_shape(painter, center, radius + rim * .65, frame.profile, canvas().view_angle,
                      gf::Color::rgba(0, 0, 0, 0), gf::Color::rgba(55, 235, 65, 105), rim);
        profile_shape(painter, center, radius, frame.profile, canvas().view_angle,
                      gf::Color::rgba(0, 0, 0, 0), gf::Color::rgba(25, 145, 40, 210), 1.3);
        for (int i = 0; i < frame.teeth; ++i) {
            const double a = frame.profile.normal_at_arc(tau * i / frame.teeth);
            const gf::Point p = screen(spiro.guide_point(a));
            painter.draw_line(polar(p, -1.5 * spiro.scale * zoom, a + canvas().view_angle),
                              polar(p, 2 * spiro.scale * zoom, a + canvas().view_angle),
                              gf::Color::rgba(38, 200, 45, 185), std::max(1.0, 2 * spiro.scale * zoom));
        }
    }
    const gf::Point close = screen(spiro.close_position());
    disc(painter, {close.x + 1, close.y + 2}, 10, gf::Color::rgba(70, 20, 20, 100));
    disc(painter, close, 9, gf::Color::rgba(219, 55, 55, 250));
    circle(painter, close, 9, gf::Color::rgba(255, 184, 162, 240), 1.4);
    painter.draw_line({close.x - 3, close.y - 3}, {close.x + 3, close.y + 3}, gf::Color::rgba(255, 255, 255),
                      1.8);
    painter.draw_line({close.x - 3, close.y + 3}, {close.x + 3, close.y - 3}, gf::Color::rgba(255, 255, 255),
                      1.8);
    const gf::Point resize = screen(spiro.resize_position());
    painter.fill_rect({resize.x - 7, resize.y - 7, 14, 14}, gf::Color::rgba(255, 212, 80, 250));
    painter.stroke_rect({resize.x - 7, resize.y - 7, 14, 14}, gf::Color::rgba(80, 65, 20), 1.3);
    painter.draw_line({resize.x - 4, resize.y - 4}, {resize.x + 4, resize.y + 4},
                      gf::Color::rgba(80, 65, 20), 1.5);
    if (spiro.inserted) {
        const gf::Point wheel = screen(spiro.wheel_center(spiro.angle));
        const double r = spiro.wheel_radius() * zoom;
        const SpiroInsert& part = spiro_inserts()[spiro.insert];
        const double rotation = spiro.wheel_rotation(spiro.angle) + canvas().view_angle;
        profile_shape(painter, {wheel.x + 1, wheel.y + 2}, r, part.profile, rotation,
                      gf::Color::rgba(15, 45, 90, 25), gf::Color::rgba(15, 45, 90, 50), 1);
        profile_shape(painter, wheel, r, part.profile, rotation, gf::Color::rgba(0, 170, 255, 70),
                      spiro.detached ? gf::Color::rgba(220, 140, 20, 245) :
                                       gf::Color::rgba(15, 110, 215, 220), spiro.detached ? 2.5 : 1.3);
        for (int i = 0; i < part.teeth; ++i) {
            const double a = spiro.quad() ? (i / 15) * std::numbers::pi / 2 + .4 * (i % 15) / 14
                                          : part.profile.normal_at_arc(tau * i / part.teeth);
            const double normal = spiro.quad() ? a - .2 : a;
            const gf::Point p = profile_point(wheel, r, part.profile, a, rotation);
            painter.draw_line(polar(p, -1.3 * spiro.scale * zoom, normal + rotation),
                              polar(p, 1.3 * spiro.scale * zoom, normal + rotation),
                              gf::Color::rgba(5, 151, 245, 215), std::max(1.0, 2 * spiro.scale * zoom));
        }
        const double grip = spiro_grip_radius();
        disc(painter, wheel, grip, gf::Color::rgba(145, 228, 255, 180));
        circle(painter, wheel, grip, gf::Color::rgba(10, 100, 170, 230), 1);
        for (int axis = 0; axis < 2; ++axis) {
            painter.draw_line({wheel.x - (axis ? 0 : grip * .5), wheel.y - (axis ? grip * .5 : 0)},
                              {wheel.x + (axis ? 0 : grip * .5), wheel.y + (axis ? grip * .5 : 0)},
                              gf::Color::rgba(25, 100, 145), 1);
        }
        for (int i = 0; i < spiro.hole_count(); ++i) {
            const gf::Point hole = screen(spiro.hole(i, spiro.angle));
            if (spiro.pegs[i].seated) {
                if (spiro.selected_peg == i) {
                    circle(painter, hole, 11, gf::Color::rgba(255, 200, 30, 65), 5);
                    circle(painter, hole, 10, gf::Color::rgba(255, 184, 20, 255), 2);
                }
                peg_cap(painter, hole, 7, spiro.pegs[i]);
            } else {
                disc(painter, hole, 4, gf::Color::rgba(240, 250, 255, 160));
                circle(painter, hole, 4, gf::Color::rgba(10, 100, 155, 200), 1);
                if (spiro_drag_ == SpiroDrag::Peg && spiro_target_hole_ == i) {
                    circle(painter, hole, 9, gf::Color::rgba(255, 195, 30, 250), 2);
                }
            }
        }
    }
    if (spiro_drag_ == SpiroDrag::Peg) {
        peg_cap(painter, screen(spiro_pointer_), 9, spiro_carried_);
    }
}
void Editor::resize_spirograph(double scale) {
    if (!spiro.active) {
        return;
    }
    release_gesture();
    spiro.set_scale(scale);
    (*ribbon_).synchronize();
    canvas().invalidate(gf::Dirty::paint);
}
void Editor::start_spirograph() {
    finish_controls(false);
    if (!spiro.active) {
        spiro.open(document.image.width, document.image.height);
        spiro_lift = false;
    }
    document.tool = Tool::Spirograph;
    (*ribbon_).show_tool_context();
    refresh();
}
void Editor::spiro_choice(const std::string& id) {
    if (id == "spirograph") {
        start_spirograph();
        return;
    }
    if (!spiro.active) {
        return;
    }
    release_gesture();
    if (id.starts_with("spiro-guide-")) {
        spiro.set_guide(std::stoi(id.substr(12)));
    } else if (id.starts_with("spiro-insert-")) {
        spiro.set_insert(std::stoi(id.substr(13)));
    } else if (id.starts_with("spiro-hole-")) {
        const int hole = std::stoi(id.substr(11));
        if (hole >= 0 && hole < spiro.hole_count()) {
            if (!spiro.pegs[hole].seated) {
                spiro.seat(hole, {true, false, {}, 1});
            }
            spiro.select_peg(hole);
        }
    } else if (id == "spiro-load-primary" || id == "spiro-load-alt") {
        const Ink ink = id == "spiro-load-primary" ? document.primary_ink() : document.alternate_ink();
        spiro.fill(spiro.selected_peg, ink.pattern == Pattern::None ? Color{0, 0, 0, 0} : ink.primary);
    } else if (id == "spiro-remove") {
        spiro.remove_insert();
    } else if (id == "spiro-outside") {
        const double rotation = spiro.wheel_rotation(spiro.angle);
        spiro.outside = !spiro.outside;
        if (!spiro.compatible(spiro.guide, spiro.insert)) {
            spiro.outside = !spiro.outside;
            return;
        }
        spiro.angle = 0;
        spiro.rolling_offset = 0;
        if (spiro.detached) {
            const double normal = spiro.guide_normal(0) - rotation;
            const double sign = spiro.exterior() ? -1 : 1;
            spiro.rolling_offset = spiro_inserts()[spiro.insert].profile.arc(normal) -
                sign * spiro.guide_radius() / spiro.wheel_radius() * spiro.guide_arc(0);
        }
    } else if (id == "spiro-deselect") {
        spiro.selected_peg = -1;
    } else if (id == "spiro-center") {
        spiro.center = {document.image.width * .5, document.image.height * .5};
    } else if (id == "spiro-clear-pegs") {
        spiro.pegs = {};
        spiro.selected_peg = -1;
    } else if (id == "spiro-fill") {
        spiro_lift = false;
        document.tool = Tool::Fill;
    } else if (id == "spiro-operate") {
        if (spiro.detached) {
            return;
        }
        spiro_lift = false;
        document.tool = Tool::Spirograph;
    } else if (id == "spiro-lift") {
        spiro_lift = true;
        document.tool = Tool::Spirograph;
    } else if (id == "spiro-close") {
        spiro = {};
        spiro_lift = false;
        document.tool = Tool::Pencil;
    }
    (*ribbon_).show_tool_context();
    refresh();
}
double Editor::spiro_grip_radius() const {
    double radius = 8;
    const Point center = spiro.wheel_center(spiro.angle);
    for (int i = 0; i < spiro.hole_count(); ++i) {
        const Point hole = spiro.hole(i, spiro.angle);
        radius = std::min(radius, std::hypot(hole.x - center.x, hole.y - center.y) * (*canvas_).zoom() * .5);
    }
    return radius;
}
int Editor::spiro_hole_at(Point point, bool empty_only) const {
    const Point center = spiro.wheel_center(spiro.angle);
    if (std::hypot(point.x - center.x, point.y - center.y) * (*canvas_).zoom() < spiro_grip_radius()) {
        return -1;
    }
    int best = -1;
    double distance = 11 / (*canvas_).zoom();
    for (int i = 0; i < spiro.hole_count(); ++i) {
        if (empty_only && spiro.pegs[i].seated) {
            continue;
        }
        const Point hole = spiro.hole(i, spiro.angle);
        const double d = std::hypot(point.x - hole.x, point.y - hole.y);
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}
void Editor::cancel_spiro_drag() {
    if (spiro_drag_ == SpiroDrag::Guide && spiro.active) {
        spiro.center = spiro_drag_center_;
    }
    if (spiro_drag_ == SpiroDrag::Resize && spiro.active) {
        spiro.set_scale(spiro_resize_scale_);
        if (ribbon_) {
            (*ribbon_).synchronize();
        }
    }
    if (spiro_drag_ == SpiroDrag::Lift && spiro.active) {
        spiro.angle = spiro_lift_angle_;
        spiro.rolling_offset = spiro_lift_offset_;
        spiro.detached = spiro_lift_detached_;
        spiro.detached_center = spiro_lift_center_;
        if (ribbon_) {
            (*ribbon_).synchronize();
        }
    }
    if (spiro_drag_ == SpiroDrag::Peg && spiro_origin_hole_ >= 0 && spiro.active) {
        spiro.seat(spiro_origin_hole_, spiro_carried_);
        spiro.select_peg(spiro_origin_hole_);
    }
    spiro_drag_ = SpiroDrag::None;
    spiro_origin_hole_ = spiro_target_hole_ = -1;
    spiro_checkpoint_ = false;
    spiro_stroke_.clear();
    if (ribbon_) {
        (*ribbon_).cancel_spiro_drag();
    }
}
void Editor::drop_spiro_peg() {
    if (spiro_target_hole_ >= 0) {
        spiro.seat(spiro_target_hole_, spiro_carried_);
        spiro.select_peg(spiro_target_hole_);
    } else if (spiro_origin_hole_ >= 0) {
        const Point center = spiro.wheel_center(spiro.angle);
        if (profile_clearance(spiro_pointer_, center, spiro.wheel_radius(),
                              spiro_inserts()[spiro.insert].profile,
                              spiro.wheel_rotation(spiro.angle)) <= 0) {
            spiro.seat(spiro_origin_hole_, spiro_carried_);
            spiro.select_peg(spiro_origin_hole_);
        }
    }
    // Releasing away from a socket removes the peg and its ink.
    spiro_origin_hole_ = -1;
    spiro_drag_ = SpiroDrag::None;
    spiro_target_hole_ = -1;
    spiro.select_peg(spiro.selected_peg);
    (*ribbon_).synchronize();
    canvas().invalidate(gf::Dirty::paint);
}
void Editor::spiro_tray_pointer(int width, const gf::PointerEvent& event) {
    if (!spiro.active || !spiro.inserted) {
        return;
    }
    const gui_drawing::PointF mapped = canvas().client_to_bitmap(canvas().point_from_window(event.position));
    spiro_pointer_ = {mapped.x, mapped.y};
    if (event.action == gf::PointerAction::down) {
        release_gesture();
        spiro_carried_ = {true, false, {}, width};
        spiro_origin_hole_ = -1;
        spiro_drag_ = SpiroDrag::Peg;
        if (window()) {
            static_cast<void>((*window()).request_focus(canvas_));
        }
    }
    spiro_target_hole_ = spiro_hole_at(spiro_pointer_, true);
    if (event.action == gf::PointerAction::up) {
        drop_spiro_peg();
    }
    canvas().invalidate(gf::Dirty::paint);
}
bool Editor::spiro_pointer(const gf::PointerEvent& event, Point point) {
    if (!spiro.active || event.button == gf::PointerButton::middle ||
        event.action == gf::PointerAction::wheel) {
        return false;
    }
    if (spiro_drag_ != SpiroDrag::None) {
        if (event.action == gf::PointerAction::move || event.action == gf::PointerAction::up) {
            if (spiro_drag_ == SpiroDrag::PegPending) {
                if (std::hypot(point.x - spiro_pointer_.x, point.y - spiro_pointer_.y) * canvas().zoom() >
                    4) {
                    spiro_carried_ = spiro.pegs[spiro_origin_hole_];
                    spiro.pegs[spiro_origin_hole_] = {};
                    spiro.selected_peg = -1;
                    spiro_drag_ = SpiroDrag::Peg;
                } else if (event.action == gf::PointerAction::up) {
                    spiro_drag_ = SpiroDrag::None;
                    spiro_origin_hole_ = -1;
                }
            }
            if (spiro_drag_ == SpiroDrag::None || spiro_drag_ == SpiroDrag::PegPending) {
                if (event.action == gf::PointerAction::up) {
                    canvas().set_pointer_capture(false);
                }
                return true;
            }
            if (spiro_drag_ == SpiroDrag::Guide) {
                spiro.center = {point.x + spiro_grab_.x, point.y + spiro_grab_.y};
            } else if (spiro_drag_ == SpiroDrag::Resize) {
                const double radius = std::hypot(point.x - spiro.center.x, point.y - spiro.center.y);
                spiro.set_scale(std::max(.05, spiro_resize_scale_ * radius / spiro_resize_radius_));
                (*ribbon_).synchronize();
            } else if (spiro_drag_ == SpiroDrag::Peg) {
                spiro_pointer_ = point;
                spiro_target_hole_ = spiro_hole_at(point, true);
                if (event.action == gf::PointerAction::up) {
                    drop_spiro_peg();
                }
            } else if (spiro_drag_ == SpiroDrag::Lift) {
                const Point target_point{point.x + spiro_grab_.x, point.y + spiro_grab_.y};
                spiro.lift_to(target_point, 12 / canvas().zoom());
                (*ribbon_).synchronize();
            } else {
                const Point target_point{point.x + spiro_grab_.x, point.y + spiro_grab_.y};
                if (spiro.rack() ||
                    std::hypot(target_point.x - spiro.center.x, target_point.y - spiro.center.y) > 2) {
                    double target = spiro.project(target_point, spiro.angle);
                    if (std::abs(target - spiro.angle) <= 1e-9) {
                        target = spiro.angle;
                    }
                    if (spiro.loaded() && std::abs(target - spiro.angle) > 1e-9 && !spiro_checkpoint_) {
                        document.settle_selection();
                        paint_base_ = document.image;
                        document.checkpoint();
                        spiro_checkpoint_ = true;
                        spiro_stroke_.clear();
                    }
                    const std::vector<SpiroTrace> traces = spiro.advance(target);
                    const Rect damage = spiro_stroke_.render(document.image, paint_base_, spiro, traces);
                    if (!traces.empty()) {
                        constrain_paint(document.image, paint_base_, guide,
                                        atlas_painting() && atlas_preserve_alpha);
                        document.constrain_selection(document.image, paint_base_);
                        ++canvas_revision;
                        publish_image(document.selection.active && !document.selection.on_canvas
                                          ? document.visible_image()
                                          : document.image,
                                      *canvas_, damage);
                        update_status();
                    }
                }
            }
            if (event.action == gf::PointerAction::up) {
                spiro_drag_ = SpiroDrag::None;
                spiro_checkpoint_ = false;
                spiro_stroke_.clear();
                canvas().set_pointer_capture(false);
            }
            canvas().invalidate(gf::Dirty::paint);
        }
        return true;
    }
    const Point close = spiro.close_position();
    const Point wheel = spiro.wheel_center(spiro.angle);
    const bool on_wheel =
        spiro.inserted &&
        profile_clearance(point, wheel, spiro.wheel_radius(), spiro_inserts()[spiro.insert].profile,
                          spiro.wheel_rotation(spiro.angle)) < 3 * spiro.scale;
    const double margin = 10 * spiro.scale + 4 / canvas().zoom();
    const SpiroGuide& frame = spiro_guides()[spiro.guide];
    const double center_distance = std::hypot(point.x - spiro.center.x, point.y - spiro.center.y);
    const double cap = frame.cap_radius * spiro.scale;
    const double half = std::numbers::pi * (spiro.guide_radius() - cap) / 2;
    const bool on_ring = spiro.capsule()
                             ? std::hypot(std::max(0.0, std::abs(point.x - spiro.center.x) - half),
                                          point.y - spiro.center.y) < cap + margin
                         : frame.outside_teeth
                             ? center_distance >= frame.teeth * spiro.scale - margin &&
                                   center_distance <= spiro.guide_body_radius() + margin
                         : spiro.rack()
                             ? std::abs(point.y - spiro.center.y) < margin &&
                                   std::abs(point.x - spiro.center.x) < 1.5 * spiro.guide_radius() + margin
                             : std::abs(profile_clearance(point, spiro.center, spiro.guide_radius(),
                                                          spiro_guides()[spiro.guide].profile, 0) -
                                        6 * spiro.scale) < margin;
    const bool on_close = std::hypot(point.x - close.x, point.y - close.y) * canvas().zoom() < 11;
    const Point resize = spiro.resize_position();
    const bool on_resize = std::hypot(point.x - resize.x, point.y - resize.y) * canvas().zoom() < 11;
    const int hole = spiro_hole_at(point, false);
    if (event.action == gf::PointerAction::down && (on_wheel || on_ring || on_close || on_resize || hole >= 0)) {
        if (on_close) {
            spiro_choice("spiro-close");
            return true;
        }
        if (on_resize && event.button == gf::PointerButton::primary) {
            release_gesture();
            spiro_resize_scale_ = spiro.scale;
            spiro_resize_radius_ = std::max(1e-9, std::hypot(point.x - spiro.center.x, point.y - spiro.center.y));
            spiro_drag_ = SpiroDrag::Resize;
            canvas().set_pointer_capture(true);
            canvas().invalidate(gf::Dirty::paint);
            return true;
        }
        if (document.tool == Tool::Fill && hole >= 0) {
            const Ink ink = event.button == gf::PointerButton::secondary ? document.alternate_ink()
                                                                         : document.primary_ink();
            if (spiro.fill(hole, ink.pattern == Pattern::None ? Color{0, 0, 0, 0} : ink.primary)) {
                spiro.select_peg(hole);
                (*ribbon_).synchronize();
            }
            canvas().invalidate(gf::Dirty::paint);
            return true;
        }
        if (document.tool == Tool::Fill && on_wheel &&
            std::hypot(point.x - wheel.x, point.y - wheel.y) * canvas().zoom() > 10) {
            return true;
        }
        if (event.button != gf::PointerButton::primary) {
            return true;
        }
        release_gesture();
        if (hole >= 0 && spiro.pegs[hole].seated) {
            spiro.select_peg(hole);
            (*ribbon_).synchronize();
            spiro_origin_hole_ = hole;
            spiro_pointer_ = point;
            spiro_drag_ = SpiroDrag::PegPending;
        } else if (on_wheel && (spiro_lift || std::hypot(point.x - wheel.x, point.y - wheel.y) * canvas().zoom() <= 10)) {
            spiro_drag_ = spiro_lift ? SpiroDrag::Lift : SpiroDrag::Wheel;
            spiro_lift_angle_ = spiro.angle;
            spiro_lift_offset_ = spiro.rolling_offset;
            spiro_lift_detached_ = spiro.detached;
            spiro_lift_center_ = spiro.detached_center;
            spiro_grab_ = {wheel.x - point.x, wheel.y - point.y};
        } else {
            spiro_drag_ = SpiroDrag::Guide;
            spiro_drag_center_ = spiro.center;
            spiro_grab_ = {spiro.center.x - point.x, spiro.center.y - point.y};
        }
        if (window()) {
            static_cast<void>((*window()).request_focus(canvas_));
        }
        canvas().set_pointer_capture(true);
        canvas().invalidate(gf::Dirty::paint);
        return true;
    }
    if (event.action == gf::PointerAction::move && (on_wheel || on_ring || on_close || on_resize || hole >= 0)) {
        canvas().set_cursor(on_resize ? gf::CursorKind::resize_diagonal_down : gf::CursorKind::hand);
    }
    if (event.action == gf::PointerAction::down && spiro.selected_peg >= 0) {
        spiro.selected_peg = -1;
        (*ribbon_).synchronize();
        canvas().invalidate(gf::Dirty::paint);
    }
    return document.tool == Tool::Spirograph;
}
} // namespace paint::forms

#include "application.hpp"
#include <cmath>
#include <numbers>
namespace paint {
namespace {
Point line_end(Point start, Point end) {
    if (!ImGui::GetIO().KeyShift) {
        return end;
    }
    double dx = end.x - start.x, dy = end.y - start.y, length = std::hypot(dx, dy);
    double angle = std::round(std::atan2(dy, dx) / (std::numbers::pi / 4)) * (std::numbers::pi / 4);
    return {start.x + length * std::cos(angle), start.y + length * std::sin(angle)};
}
ImVec2 on_screen(Point point, ImVec2 origin, float zoom) {
    return {origin.x + static_cast<float>(point.x) * zoom, origin.y + static_cast<float>(point.y) * zoom};
}
void append_point(std::string& signature, Point point) {
    signature += ":" + std::to_string(point.x) + "," + std::to_string(point.y);
}
} // namespace
void Application::clear_curve_controls() {
    curve_handle = -1;
    curve_handle_checkpoint = false;
    curve_preview_signature.clear();
    preview_active = false;
    dragging = false;
    texture_dirty = true;
}
void Application::finish_curve() {
    if (document.curve.session == 0) {
        return;
    }
    bool established = document.curve.line_set;
    document.commit_curve();
    clear_curve_controls();
    status = established ? "Curve released. The drawing stays on the canvas."
                         : "Curve cancelled. The starting line was not set.";
}
void Application::begin_curve_gesture(Point point, bool right) {
    if (document.curve.base && document.curve.line_set) {
        dragging = false;
        status = "Drag a curve handle to adjust it. Escape releases the curve.";
        return;
    }
    if (!document.curve.base) {
        document.begin_curve(document.shape == Shape::Arc ? CurveKind::Arc : CurveKind::Bezier, point, right);
    }
    curve_pending_end = line_end(document.curve.geometry.start, point);
    dragging = true;
    preview_active = true;
    texture_dirty = true;
    status = "Drag the starting line, or click its other endpoint. Escape cancels an unset line.";
}
void Application::update_curve_gesture(Point point) {
    curve_pending_end = line_end(document.curve.geometry.start, point);
    texture_dirty = true;
}
void Application::end_curve_gesture(Point point) {
    update_curve_gesture(point);
    dragging = false;
    Point start = document.curve.geometry.start;
    if (std::hypot(curve_pending_end.x - start.x, curve_pending_end.y - start.y) * zoom >= 3 &&
        document.establish_curve(curve_pending_end)) {
        preview_active = false;
        status = document.curve.geometry.kind == CurveKind::Arc
                     ? "Drag the middle handle to bend the arc. Escape releases it."
                     : "Drag either endpoint's control handle to bend the Bézier. Escape releases it.";
    }
    texture_dirty = true;
}
bool Application::curve_control(Point point, bool window_hovered) {
    if (!document.curve.base || !document.curve.line_set) {
        curve_handle = -1;
        return false;
    }
    CurveGeometry& geometry = document.curve.geometry;
    int hit = -1;
    double nearest = 11;
    if (window_hovered) {
        for (int i = 0; i < geometry.handle_count(); ++i) {
            Point handle = geometry.handle(i);
            double distance = std::hypot(handle.x - point.x, handle.y - point.y) * zoom;
            if (distance < nearest) {
                hit = i;
                nearest = distance;
            }
        }
    }
    if (hit >= 0 && ImGui::IsMouseClicked(0)) {
        curve_handle = hit;
        curve_handle_checkpoint = false;
        Point handle = geometry.handle(hit);
        curve_handle_offset = {point.x - handle.x, point.y - handle.y};
    }
    if (curve_handle >= 0) {
        Point target{point.x - curve_handle_offset.x, point.y - curve_handle_offset.y};
        CurveGeometry changed = geometry;
        changed.move_handle(curve_handle, target);
        Point old = geometry.handle(curve_handle), next = changed.handle(curve_handle);
        if (std::hypot(next.x - old.x, next.y - old.y) > 1e-9) {
            if (!curve_handle_checkpoint) {
                document.sync_curve();
                document.checkpoint();
                curve_handle_checkpoint = true;
            }
            geometry = changed;
            document.sync_curve();
            texture_dirty = true;
        }
        if (!ImGui::IsMouseDown(0)) {
            curve_handle = -1;
        }
    }
    if (hit >= 0 || curve_handle >= 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        return true;
    }
    return false;
}
void Application::refresh_curve_preview(Point point, bool over) {
    if (!document.curve.base) {
        if (!curve_preview_signature.empty()) {
            clear_curve_controls();
        }
        return;
    }
    if (!document.curve.line_set && over && !dragging) {
        curve_pending_end = line_end(document.curve.geometry.start, point);
    }
    const CurveGeometry& geometry = document.curve.geometry;
    std::string signature =
        std::to_string(document.curve.session) + ":" + std::to_string(document.curve.line_set) + ":" +
        std::to_string(document.revision) + ":" + std::to_string(over) + ":" + std::to_string(geometry.bulge);
    append_point(signature, geometry.start);
    append_point(signature, geometry.end);
    append_point(signature, geometry.first_control);
    append_point(signature, geometry.second_control);
    if (!document.curve.line_set) {
        append_point(signature, curve_pending_end);
    }
    const Ink& ink = document.ink;
    signature += ":" + std::to_string(packed(ink.primary)) + ":" + std::to_string(packed(ink.secondary)) +
                 ":" + std::to_string(ink.size) + ":" + std::to_string(int(ink.brush)) + ":" +
                 std::to_string(int(ink.pattern)) + ":" + std::to_string(ink.transparent_pattern) + ":" +
                 std::to_string(ink.noise) + ":" + std::to_string(ink.grain_scale) + ":" +
                 std::to_string(ink.paper_roughness) + ":" + std::to_string(ink.pigment_load) + ":" +
                 std::to_string(ink.material_angle);
    if (signature != curve_preview_signature) {
        if (document.curve.line_set) {
            document.sync_curve();
            preview_active = false;
        } else {
            preview = document.curve_image(over || dragging ? &curve_pending_end : nullptr);
            preview_active = true;
        }
        curve_preview_signature = signature;
        texture_dirty = true;
    }
}
void Application::draw_curve_controls(ImDrawList& draw, ImVec2 origin) {
    if (!document.curve.base) {
        return;
    }
    const CurveGeometry& geometry = document.curve.geometry;
    ImVec2 start = on_screen(geometry.start, origin, zoom), end = on_screen(geometry.end, origin, zoom);
    if (document.curve.line_set) {
        for (int i = 0; i < geometry.handle_count(); ++i) {
            ImVec2 handle = on_screen(geometry.handle(i), origin, zoom);
            if (geometry.kind == CurveKind::Bezier) {
                draw.AddLine(i == 0 ? start : end, handle, IM_COL32(55, 120, 185, 180), 1);
            }
            draw.AddCircleFilled(handle, 6, IM_COL32(255, 255, 255, 255));
            draw.AddCircleFilled(
                handle, 4.5f, i == curve_handle ? IM_COL32(235, 150, 25, 255) : IM_COL32(0, 115, 210, 255));
        }
        draw.AddRectFilled({end.x - 3, end.y - 3}, {end.x + 3, end.y + 3}, IM_COL32(30, 75, 120, 255));
    }
    draw.AddRectFilled({start.x - 3, start.y - 3}, {start.x + 3, start.y + 3}, IM_COL32(30, 75, 120, 255));
}
} // namespace paint

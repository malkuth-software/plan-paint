#include "application.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace paint {
AffineMap Application::rotation_map() const {
    const double radians = rotation_angle * std::numbers::pi / 180.0;
    const double cosine = std::cos(radians), sine = std::sin(radians);
    const double center_x = (rotation_original.image.width - 1) * 0.5;
    const double center_y = (rotation_original.image.height - 1) * 0.5;
    return {cosine, -sine,  center_x - cosine * center_x + sine * center_y,
            sine,   cosine, center_y - sine * center_x - cosine * center_y};
}
void Application::start_rotation() {
    if (!document.selection.active || warp_worker.busy() || reshape_active || transform_active) {
        throw std::runtime_error("Select an object and finish the current transform before rotating.");
    }
    rotation_original = document.selection;
    rotation_field.reset();
    rotation_angle = 0;
    rotation_active = true;
    rotation_dragging = false;
    rotation_render_pending = false;
    rotation_commit_pending = false;
    rotation_place_pending = false;
    rotation_whole_image = false;
    ++rotation_generation;
    try {
        warp_worker.compile(WarpTask::CompileRotation, rotation_original.image);
    } catch (...) {
        rotation_active = false;
        rotation_original = {};
        throw;
    }
    status = "Drag to turn the selection. Hold Shift for 15-degree steps. Release to finish.";
}
void Application::finish_rotation(bool place) {
    if (!rotation_active) {
        return;
    }
    rotation_dragging = false;
    rotation_commit_pending = true;
    rotation_render_pending = true;
    rotation_place_pending = rotation_place_pending || place;
    status = "Finishing the rotation with CONV area sampling...";
}
void Application::request_rotation(double degrees) {
    if (!std::isfinite(degrees)) {
        throw std::runtime_error("Enter a finite rotation angle.");
    }
    if (rotation_active || warp_worker.busy() || reshape_active || transform_active) {
        throw std::runtime_error("Finish the current transform before rotating.");
    }
    degrees = std::remainder(degrees, 360.0);
    if (std::abs(degrees) < 1e-10) {
        return;
    }
    finish_text();
    finish_curve();
    document.commit_path();
    const bool whole_image = !document.selection.active;
    if (whole_image) {
        document.select_all();
    }
    start_rotation();
    rotation_whole_image = whole_image;
    rotation_angle = degrees;
    ++rotation_generation;
    finish_rotation();
}
void Application::rotation_result(WarpResult& result) {
    if (!rotation_active) {
        return;
    }
    if (!result.error.empty()) {
        document.selection = std::move(rotation_original);
        rotation_active = false;
        rotation_dragging = false;
        rotation_commit_pending = false;
        rotation_render_pending = false;
        rotation_field.reset();
        texture_dirty = true;
        error = result.error;
        return;
    }
    if (result.task == WarpTask::CompileRotation) {
        rotation_field = std::move(result.field);
        return;
    }
    if (result.generation != rotation_generation ||
        (rotation_commit_pending && result.task == WarpTask::PreviewRotation)) {
        return;
    }
    if (result.task == WarpTask::CommitRotation && rotation_whole_image) {
        document.assign_canvas(std::move(result.image));
        document.selection = {};
    } else {
        document.selection.image = std::move(result.image);
        document.selection.x = rotation_original.x + result.bounds.x;
        document.selection.y = rotation_original.y + result.bounds.y;
        document.selection.coverage.clear();
        document.selection.outline.clear();
    }
    texture_dirty = true;
    if (result.task == WarpTask::CommitRotation) {
        rotation_active = false;
        rotation_dragging = false;
        rotation_commit_pending = false;
        rotation_render_pending = false;
        rotation_field.reset();
        rotation_original = {};
        if (rotation_place_pending) {
            document.commit_selection();
        }
        rotation_place_pending = false;
        status = "Rotation finished. Move the selection, or press Escape to place it.";
    }
}
static ImVec2 handle_position(ImVec2 origin, const FloatingSelection& selection, float zoom,
                              double angle = 0) {
    ImVec2 result(origin.x + (selection.x + selection.image.width) * zoom + 18,
                  origin.y + selection.y * zoom - 18);
    const ImVec2 window_position = ImGui::GetWindowPos(), window_size = ImGui::GetWindowSize();
    if (result.y < window_position.y + 10) {
        result.y += 36;
    }
    if (result.x > window_position.x + window_size.x - 10) {
        result.x -= 36;
    }
    const ImVec2 center(origin.x + (selection.x + selection.image.width * 0.5f) * zoom,
                        origin.y + (selection.y + selection.image.height * 0.5f) * zoom);
    const double radians = angle * std::numbers::pi / 180.0;
    const double dx = result.x - center.x, dy = result.y - center.y;
    result = {center.x + static_cast<float>(std::cos(radians) * dx - std::sin(radians) * dy),
              center.y + static_cast<float>(std::sin(radians) * dx + std::cos(radians) * dy)};
    return result;
}
bool Application::rotation_control(ImVec2 origin, Point point, bool window_hovered) {
    if (!document.selection.active || reshape_active || transform_active || resize_handle >= 0 ||
        canvas_handle >= 0) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const FloatingSelection& selection = rotation_active ? rotation_original : document.selection;
    ImVec2 handle = handle_position(origin, selection, zoom, rotation_active ? rotation_angle : 0);
    const bool hovered =
        window_hovered && std::hypot(io.MousePos.x - handle.x, io.MousePos.y - handle.y) <= 11;
    if (hovered || rotation_dragging) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (hovered && !rotation_active && !warp_worker.busy() && ImGui::IsMouseClicked(0)) {
        start_rotation();
        rotation_dragging = true;
        const Point center{rotation_original.x + rotation_original.image.width * 0.5,
                           rotation_original.y + rotation_original.image.height * 0.5};
        rotation_pointer_start = std::atan2(point.y - center.y, point.x - center.x);
    }
    if (rotation_dragging) {
        const Point center{rotation_original.x + rotation_original.image.width * 0.5,
                           rotation_original.y + rotation_original.image.height * 0.5};
        double angle = (std::atan2(point.y - center.y, point.x - center.x) - rotation_pointer_start) * 180.0 /
                       std::numbers::pi;
        angle = std::remainder(angle, 360.0);
        if (io.KeyShift) {
            angle = std::round(angle / 15.0) * 15.0;
        }
        if (std::abs(angle - rotation_angle) > 1e-6) {
            rotation_angle = angle;
            ++rotation_generation;
            rotation_render_pending = true;
        }
        if (ImGui::IsMouseReleased(0)) {
            finish_rotation();
        }
    }
    if (hovered && !rotation_dragging) {
        ImGui::SetTooltip("Rotate freely with CONV. Hold Shift for 15-degree steps.");
    }
    return hovered || rotation_active;
}
void Application::draw_rotation_control(ImVec2 origin) {
    if (!document.selection.active || reshape_active || transform_active) {
        return;
    }
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const FloatingSelection& selection = rotation_active ? rotation_original : document.selection;
    ImVec2 handle = handle_position(origin, selection, zoom, rotation_active ? rotation_angle : 0);
    if (rotation_active) {
        const AffineMap map = rotation_map();
        const double width = rotation_original.image.width, height = rotation_original.image.height;
        const Point corners[4] = {
            {-0.5, -0.5}, {width - 0.5, -0.5}, {width - 0.5, height - 0.5}, {-0.5, height - 0.5}};
        std::array<ImVec2, 4> outline{};
        for (int index = 0; index < 4; ++index) {
            const Point point = corners[index];
            outline[index] = {origin.x + static_cast<float>(selection.x + map.xx * point.x +
                                                            map.xy * point.y + map.tx + 0.5) *
                                             zoom,
                              origin.y + static_cast<float>(selection.y + map.yx * point.x +
                                                            map.yy * point.y + map.ty + 0.5) *
                                             zoom};
        }
        draw.AddPolyline(outline.data(), 4, IM_COL32(20, 103, 195, 255), ImDrawFlags_Closed, 1.5f);
        char label[64] = {};
        std::snprintf(label, sizeof(label), "%.1f°", rotation_angle);
        draw.AddText({handle.x + 14, handle.y - 8}, IM_COL32(20, 75, 130, 255), label);
    } else {
        draw.AddLine({origin.x + (selection.x + selection.image.width) * zoom, origin.y + selection.y * zoom},
                     handle, IM_COL32(48, 111, 174, 180));
    }
    draw.AddCircleFilled(handle, 10, IM_COL32(244, 249, 255, 255));
    draw.AddCircle(handle, 10, IM_COL32(32, 105, 180, 255));
    draw.PathArcTo(handle, 5, -2.7f, 1.8f, 14);
    draw.PathStroke(IM_COL32(32, 105, 180, 255), 0, 1.5f);
    draw.AddTriangleFilled({handle.x - 4, handle.y + 3}, {handle.x + 2, handle.y + 3},
                           {handle.x - 1, handle.y + 8}, IM_COL32(32, 105, 180, 255));
}
} // namespace paint

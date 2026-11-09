#include "localization.hpp"
#include "codecs.hpp"
#include "forms/editor.hpp"
#include <algorithm>
#include <cmath>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
class GradientStrip final : public gf::Control {
    std::weak_ptr<Editor> editor_;
    bool dragging_ = false;

  public:
    GradientStrip(gf::StableId id, std::weak_ptr<Editor> editor) : Control(std::move(id)), editor_(editor) {
        accessible_name = 
            tr("Gradient stops: click to add, drag to move; use Stop and Position for keyboard editing");
    }
    void on_paint(gf::Painter& painter, gf::Rect) override {
        const std::shared_ptr<Editor> editor = editor_.lock();
        if (!editor) {
            return;
        }
        const double width = client_rectangle().width;
        const GradientSampler sampler((*editor).fill_gradient, {0, 0, 100, 100});
        for (int y = 3; y < 41; y += 6) {
            for (int x = 8; x < static_cast<int>(width) - 8; x += 6) {
                painter.fill_rect({static_cast<double>(x), static_cast<double>(y),
                                   std::min(6.0, width - 8 - x), static_cast<double>(std::min(6, 41 - y))},
                                  ((x - 8) / 6 + (y - 3) / 6) % 2 ? gf::Color::rgba(205, 210, 217)
                                                                  : gf::Color::rgba(255, 255, 255));
            }
        }
        for (int x = 8; x < static_cast<int>(width) - 8; ++x) {
            const Color color = sampler.at((x - 8) / std::max(1.0, width - 17));
            painter.fill_rect({static_cast<double>(x), 3, 1, 38},
                              gf::Color::rgba(color.r, color.g, color.b, color.a));
        }
        painter.stroke_rect({8, 3, width - 16, 38}, gf::Color::rgba(71, 88, 109), 1);
        for (std::size_t i = 0; i < (*editor).fill_gradient.stops.size(); ++i) {
            const GradientStop& stop = (*editor).fill_gradient.stops[i];
            const double x = 8 + stop.position * (width - 17);
            const bool selected = i == (*editor).gradient_stop;
            painter.draw_line({x, 41}, {x, 46}, gf::Color::rgba(65, 73, 91), 1);
            painter.fill_rect({x - 5, 46, 10, 15},
                              selected ? gf::Color::rgba(255, 217, 112) : gf::Color::rgba(248, 250, 253));
            painter.stroke_rect({x - 5, 46, 10, 15},
                                selected ? gf::Color::rgba(171, 116, 18) : gf::Color::rgba(74, 94, 121),
                                selected ? 2 : 1);
            painter.fill_rect({x - 3, 49, 6, 8}, gf::Color::rgba(stop.color.r, stop.color.g, stop.color.b));
        }
        painter.draw_text_utf8({8, 76}, tr("Click to add a stop; drag its handle to move"),
                               {gf::FontRole::control, 11, 400, false}, gf::Color::rgba(55, 68, 85));
    }
    void on_pointer(gf::PointerEvent& event) override {
        const std::shared_ptr<Editor> editor = editor_.lock();
        if (!editor) {
            return;
        }
        if (dragging_ && !has_pointer_capture()) {
            dragging_ = false;
        }
        const gf::Point point = point_from_window(event.position);
        const double width = client_rectangle().width;
        Gradient& gradient = (*editor).fill_gradient;
        if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary &&
            point.y < 64) {
            std::size_t nearest = gradient.stops.size();
            double distance = 9;
            for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
                double d = std::abs(point.x - (8 + gradient.stops[i].position * (width - 17)));
                if (d < distance) {
                    distance = d;
                    nearest = i;
                }
            }
            const double position = std::clamp((point.x - 8) / std::max(1.0, width - 17), 0.0, 1.0);
            if (nearest == gradient.stops.size()) {
                if (gradient.stops.size() >= 32) {
                    return;
                }
                Color color = GradientSampler(gradient, {0, 0, 1, 1}).at(position);
                gradient.stops.push_back({position, color});
                nearest = move_gradient_stop(gradient, gradient.stops.size() - 1, position);
            }
            (*editor).gradient_stop = nearest;
            dragging_ = true;
            set_pointer_capture(true);
            event.handled = true;
        } else if (dragging_ &&
                   (event.action == gf::PointerAction::move || event.action == gf::PointerAction::up)) {
            (*editor).gradient_stop = move_gradient_stop(gradient, (*editor).gradient_stop,
                                                         (point.x - 8) / std::max(1.0, width - 17));
            if (event.action == gf::PointerAction::up) {
                dragging_ = false;
                set_pointer_capture(false);
            }
            event.handled = true;
        } else {
            return;
        }
        (*editor).refresh();
    }
};
} // namespace
// The stop strip, in Plan Paint's own overlay below "Edit stops".
gf::Size Ribbon::gradient_stop_panel(gf::Panel& panel) {
    const std::shared_ptr<gf::Control> strip = gf::make_control<GradientStrip>(gf::StableId("gradient-strip"), editor_);
    (*strip).set_requested_bounds({6, 4, 458, 84});
    panel.add_child(strip);
    return {470, 92};
}
void Ribbon::synchronize_gradient() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || (*editor).fill_gradient.stops.empty()) {
        return;
    }
    gradient_synchronizing_ = true;
    const Gradient& gradient = (*editor).fill_gradient;
    (*editor).gradient_stop = std::min((*editor).gradient_stop, gradient.stops.size() - 1);
    if (gradient_stops_ && (*editor).gradient_stop < (*gradient_stops_).items.read().size()) {
        (*gradient_stops_).selected_index = std::optional<std::size_t>{(*editor).gradient_stop};
    }
    if (gradient_angle_) {
        (*gradient_angle_).value = gradient.angle;
        (*gradient_angle_).enabled = gradient.kind == GradientKind::Linear;
    }
    if (gradient_position_) {
        (*gradient_position_).value = gradient.stops[(*editor).gradient_stop].position * 100;
    }
    if (popup_layer_ && popup_command_ == "gradient-stops-menu") {
        (*popup_layer_).invalidate(gf::Dirty::paint);
    }
    gradient_synchronizing_ = false;
}
void Ribbon::gradient_stop_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor && !gradient_synchronizing_ && !synchronizing_ && index &&
        *index < (*editor).fill_gradient.stops.size()) {
        (*editor).gradient_stop = *index;
        (*editor).refresh();
    }
}
void Ribbon::gradient_number_changed(double) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || gradient_synchronizing_ || synchronizing_) {
        return;
    }
    Gradient& gradient = (*editor).fill_gradient;
    if (gradient_angle_) {
        gradient.angle = (*gradient_angle_).value.read();
    }
    if (gradient_position_) {
        (*editor).gradient_stop =
            move_gradient_stop(gradient, (*editor).gradient_stop, (*gradient_position_).value.read() / 100);
    }
    (*editor).refresh();
}
// The gradient commands and menu choices; false for another ID.
bool Ribbon::gradient_clicked(const std::string& id) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return false;
    }
    if (id == "bucket-solid" || id == "bucket-gradient") {
        (*editor).gradient_fill = id == "bucket-gradient";
        (*editor).choose_tool(Tool::Fill);
        show_tool_context();
        return true;
    }
    if (!id.starts_with("gradient-") || id == "gradient-stops-menu") {
        return false;
    }
    Gradient& gradient = (*editor).fill_gradient;
    if (id == "gradient-edit-color") {
        (*editor).open_editor_dialog(EditorDialogKind::gradient_color);
        return true;
    }
    if (id == "gradient-linear") {
        gradient.kind = GradientKind::Linear;
    } else if (id == "gradient-radial") {
        gradient.kind = GradientKind::Radial;
    } else if (id == "gradient-reverse") {
        for (GradientStop& stop : gradient.stops) {
            stop.position = 1 - stop.position;
        }
        std::reverse(gradient.stops.begin(), gradient.stops.end());
        (*editor).gradient_stop = gradient.stops.size() - 1 - (*editor).gradient_stop;
    } else if (id == "gradient-add" && gradient.stops.size() < 32) {
        const double start = gradient.stops[(*editor).gradient_stop].position;
        const double position = (*editor).gradient_stop + 1 < gradient.stops.size()
                                    ? (start + gradient.stops[(*editor).gradient_stop + 1].position) * .5
                                    : start * .5;
        const Color color = GradientSampler(gradient, {0, 0, 1, 1}).at(position);
        gradient.stops.push_back({position, color});
        (*editor).gradient_stop = move_gradient_stop(gradient, gradient.stops.size() - 1, position);
    } else if (id == "gradient-remove" && gradient.stops.size() > 2) {
        gradient.stops.erase(gradient.stops.begin() + static_cast<std::ptrdiff_t>((*editor).gradient_stop));
        (*editor).gradient_stop = std::min((*editor).gradient_stop, gradient.stops.size() - 1);
    } else if (id.starts_with("gradient-preset-")) {
        const std::size_t index = static_cast<std::size_t>(std::stoi(id.substr(16)));
        if (index < gradient_presets().size()) {
            gradient = gradient_presets()[index].gradient;
            (*editor).gradient_stop = 0;
        }
    }
    (*editor).refresh();
    return true;
}
} // namespace paint::forms

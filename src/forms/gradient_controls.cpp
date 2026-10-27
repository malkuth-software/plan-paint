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
        set_accessible_name(
            tr("Gradient stops: click to add, drag to move; use Stop and Position for keyboard editing"));
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
void Ribbon::initialize_gradient() {
    building_page_ = 256;
    button("gradient-presets-menu", tr("Presets"), -1, {12, 35, 130, 79}, true, true);
    gradient_strip_ = gf::make_control<GradientStrip>(gf::StableId("gradient-strip"), editor_);
    option(gradient_strip_, {160, 34, 450, 84});
    button("gradient-linear", tr("Linear"), -1, {630, 35, 82, 28});
    button("gradient-radial", tr("Circular"), -1, {716, 35, 86, 28});
    gradient_angle_ = number("gradient-angle", tr("Angle (°)"), {824, 35, 225, 28}, 0, 360, 0, 1);
    (*gradient_angle_).set_increment(1);
    button("gradient-reverse", tr("Reverse"), -1, {1065, 35, 98, 28});
    gradient_stops_ = gf::make_control<gf::ComboBox>(gf::StableId("gradient-stop"));
    (*gradient_stops_).set_accessible_name(tr("Selected gradient stop"));
    option(gradient_stops_, {630, 80, 94, 28});
    subscriptions_.push_back(
        (*gradient_stops_)
            .selected_index_changed()
            .subscribe(*this,
                       gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::gradient_stop_changed>(
                           *this)));
    gradient_position_ = number("gradient-position", tr("Position (%)"), {738, 80, 215, 28}, 0, 100, 0, 1);
    (*gradient_position_).set_increment(1);
    gradient_color_ = gf::make_control<SwatchButton>(gf::StableId("gradient-color"), "", Color{0, 0, 0, 255});
    (*gradient_color_).set_requested_bounds({969, 80, 28, 28});
    (*gradient_color_).set_accessible_name(tr("Edit selected gradient stop color"));
    subscriptions_.push_back(
        (*gradient_color_)
            .clicked()
            .subscribe(*this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::clicked>(*this)));
    buttons_.push_back(gradient_color_);
    button_pages_.push_back(256);
    add_child(gradient_color_);
    button("gradient-edit-color", tr("Color…"), -1, {1001, 80, 73, 28});
    button("gradient-add", tr("Add"), -1, {1090, 80, 70, 28});
    button("gradient-remove", tr("Remove"), -1, {1170, 80, 92, 28});
}
void Ribbon::synchronize_gradient() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !gradient_stops_ || page_ != 256 || collapsed_) {
        return;
    }
    gradient_synchronizing_ = true;
    const Gradient& gradient = (*editor).fill_gradient;
    (*editor).gradient_stop = std::min((*editor).gradient_stop, gradient.stops.size() - 1);
    if ((*gradient_stops_).items().size() != gradient.stops.size()) {
        std::vector<std::string> names;
        for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
            names.push_back(tr("Stop ") + std::to_string(i + 1));
        }
        (*gradient_stops_).set_items(std::move(names));
    }
    (*gradient_stops_).set_selected_index((*editor).gradient_stop);
    (*gradient_angle_).set_value(gradient.angle);
    (*gradient_angle_).set_enabled(gradient.kind == GradientKind::Linear);
    (*gradient_position_).set_value(gradient.stops[(*editor).gradient_stop].position * 100);
    (*gradient_color_).set_color(gradient.stops[(*editor).gradient_stop].color);
    (*gradient_strip_).invalidate(gf::Dirty::paint);
    gradient_synchronizing_ = false;
}
void Ribbon::gradient_stop_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor && !gradient_synchronizing_ && index && *index < (*editor).fill_gradient.stops.size()) {
        (*editor).gradient_stop = *index;
        (*editor).refresh();
    }
}
void Ribbon::gradient_number_changed(double) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || gradient_synchronizing_) {
        return;
    }
    Gradient& gradient = (*editor).fill_gradient;
    gradient.angle = (*gradient_angle_).value();
    (*editor).gradient_stop =
        move_gradient_stop(gradient, (*editor).gradient_stop, (*gradient_position_).value() / 100);
    (*editor).refresh();
}
bool Ribbon::gradient_clicked(const std::string& id) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return false;
    }
    if (id == "bucket-solid" || id == "bucket-gradient") {
        (*editor).gradient_fill = id == "bucket-gradient";
        (*editor).choose_tool(Tool::Fill);
        page_ = (*editor).gradient_fill ? 256 : 8;
        collapsed_ = false;
        synchronize();
        return true;
    }
    if (!id.starts_with("gradient-") || id == "gradient-tab" || id == "gradient-presets-menu") {
        return false;
    }
    Gradient& gradient = (*editor).fill_gradient;
    if (id == "gradient-color" || id == "gradient-edit-color") {
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
        double position = (*editor).gradient_stop + 1 < gradient.stops.size()
                              ? (start + gradient.stops[(*editor).gradient_stop + 1].position) * .5
                              : start * .5;
        Color color = GradientSampler(gradient, {0, 0, 1, 1}).at(position);
        gradient.stops.push_back({position, color});
        (*editor).gradient_stop = move_gradient_stop(gradient, gradient.stops.size() - 1, position);
    } else if (id == "gradient-remove" && gradient.stops.size() > 2) {
        gradient.stops.erase(gradient.stops.begin() + (*editor).gradient_stop);
        (*editor).gradient_stop = std::min((*editor).gradient_stop, gradient.stops.size() - 1);
    } else if (id.starts_with("gradient-preset-")) {
        std::size_t index = static_cast<std::size_t>(std::stoi(id.substr(16)));
        if (index < gradient_presets().size()) {
            gradient = gradient_presets()[index].gradient;
            (*editor).gradient_stop = 0;
        }
    }
    (*editor).refresh();
    return true;
}
gf::Size Ribbon::gradient_gallery(gf::Panel& panel) {
    gradient_previews_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{120, 30});
    const std::span<const GradientPreset> presets = gradient_presets();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        Image sample;
        sample.reset(240, 60);
        const GradientSampler sampler(presets[i].gradient, {0, 0, 240, 60});
        for (int y = 0; y < 60; ++y) {
            for (int x = 0; x < 240; ++x) {
                sample.set(x, y, sampler.at(x / 239.0));
            }
        }
        std::vector<std::uint8_t> png = encode_png(sample);
        static_cast<void>((*gradient_previews_).add_png(std::to_string(i), std::as_bytes(std::span(png)), 2));
        const std::shared_ptr<gf::Button> item =
            add_popup_button(panel, "gradient-preset-" + std::to_string(i), presets[i].name, -1,
                             {5.0 + (i % 3) * 148, 4.0 + (i / 3) * 65, 144, 62});
        (*item).set_image_list(gradient_previews_);
        (*item).set_image_key(std::to_string(i));
        (*item).set_text_image_relation(gf::TextImageRelation::image_above_text);
        (*item).set_text_alignment(gf::ContentAlignment::middle_center);
        (*item).set_font({gf::FontRole::control, 12, 400, false});
    }
    return {454, 8 + 65.0 * ((presets.size() + 2) / 3)};
}
} // namespace paint::forms

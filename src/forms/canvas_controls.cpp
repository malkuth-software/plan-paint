#include "forms/editor.hpp"
#include "localization.hpp"
#include <algorithm>
namespace paint::forms {
namespace gf = gui_forms;
void Editor::initialize_canvas_controls() {
    canvas_controls_ = gf::make_control<gf::Panel>(gf::StableId("canvas-controls"));
    (*canvas_controls_).set_accessible_name(tr("Canvas controls"));
    add_child(canvas_controls_);
    const std::string names[] = {tr("Canvas X"), tr("Canvas Y"), tr("Step (pixels)")};
    for (int i = 0; i < 3; ++i) {
        std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(
            gf::StableId("canvas-position-label-" + std::to_string(i)), names[i]);
        (*label).set_requested_bounds({10 + i * 165.0, 4, 155, 22});
        (*canvas_controls_).add_child(label);
        canvas_positions_[i] = gf::make_control<gf::NumericUpDown>(
            gf::StableId("canvas-position-" + std::to_string(i)));
        gf::NumericUpDown& field = *canvas_positions_[i];
        field.set_accessible_name(names[i]);
        field.set_range(i == 2 ? 1 : 0, 100000);
        field.set_value(i == 2 ? 1 : 0);
        field.set_requested_bounds({10 + i * 165.0, 26, 150, 28});
        (*canvas_controls_).add_child(canvas_positions_[i]);
    }
    const std::string names2[] = {tr("Move to position"), tr("Primary click"), tr("Alt click"),
        tr("Start stroke"), tr("Finish stroke"), tr("Undo"), tr("Move left"), tr("Move right"),
        tr("Move up"), tr("Move down"), tr("Focus canvas"), tr("Reset view upright")};
    for (int i = 0; i < 12; ++i) {
        std::shared_ptr<gf::Button> button = gf::make_control<gf::Button>(
            gf::StableId("canvas-action-" + std::to_string(i)), names2[i]);
        (*button).set_accessible_name(names2[i]);
        (*button).set_use_mnemonic(false);
        subscriptions_.push_back((*button).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<Editor, &Editor::canvas_control_clicked>(*this)));
        canvas_actions_.push_back(button);
        (*canvas_controls_).add_child(button);
    }
    canvas_alt_ = gf::make_control<gf::CheckBox>(gf::StableId("canvas-alternate"), tr("Use Alt color"));
    canvas_shift_ = gf::make_control<gf::CheckBox>(gf::StableId("canvas-constrain"), tr("Constrain shape"));
    canvas_center_ = gf::make_control<gf::CheckBox>(gf::StableId("canvas-center"), tr("Draw from center"));
    int index = 0;
    for (const std::shared_ptr<gf::CheckBox>& field : {canvas_alt_, canvas_shift_, canvas_center_}) {
        (*field).set_requested_bounds({515, 3 + index++ * 19.0, 250, 22});
        (*canvas_controls_).add_child(field);
    }
}
void Editor::arrange_canvas_controls(gf::Rect area) {
    (*canvas_controls_).set_visible(settings.canvas_controls && !pattern_editing);
    set_child_layout(canvas_controls_, area);
    const double width = (area.width - 20) / 6;
    for (std::size_t i = 0; i < canvas_actions_.size(); ++i) {
        (*canvas_actions_[i]).set_requested_bounds(
            {10 + static_cast<double>(i % 6) * width, 60 + static_cast<double>(i / 6) * 34, width - 5, 30});
    }
    (*canvas_positions_[0]).set_range(0, std::max(0, document.image.width - 1));
    (*canvas_positions_[1]).set_range(0, std::max(0, document.image.height - 1));
}
void Editor::canvas_control_pointer(gf::PointerAction action, gf::PointerButton button) {
    gf::Control::Ptr focus = window() ? (*window()).focused_control() : gf::Control::Ptr{};
    gf::PointerEvent event{action, button, canvas().point_to_window(screen({canvas_position_.x + 0.5, canvas_position_.y + 0.5}))};
    if ((*canvas_shift_).checked()) { event.modifiers = event.modifiers | gf::Modifier::shift; }
    if ((*canvas_center_).checked()) { event.modifiers = event.modifiers | gf::Modifier::control; }
    canvas_control_dispatch_ = true;
    pointer(event);
    canvas_control_dispatch_ = false;
    // A latched gesture must leave all controls reachable by switch, voice and
    // keyboard. Only synthetic moves advance it; physical hover cannot draw.
    canvas().set_pointer_capture(false);
    if (focus && window()) { static_cast<void>((*window()).request_focus(focus)); }
    canvas().prepare_display();
    prepare_stamp_view();
    canvas().invalidate(gf::Dirty::paint);
}
void Editor::canvas_control_action(int action) {
    if (!settings.canvas_controls) { return; }
    if (action >= 6 && action <= 9) {
        const double step = (*canvas_positions_[2]).value();
        if (action == 6) { canvas_position_.x -= step; }
        if (action == 7) { canvas_position_.x += step; }
        if (action == 8) { canvas_position_.y -= step; }
        if (action == 9) { canvas_position_.y += step; }
    } else {
        canvas_position_ = {(*canvas_positions_[0]).value(), (*canvas_positions_[1]).value()};
    }
    canvas_position_.x = std::clamp(canvas_position_.x, 0.0, static_cast<double>(std::max(0, document.image.width - 1)));
    canvas_position_.y = std::clamp(canvas_position_.y, 0.0, static_cast<double>(std::max(0, document.image.height - 1)));
    (*canvas_positions_[0]).set_value(canvas_position_.x);
    (*canvas_positions_[1]).set_value(canvas_position_.y);
    const gf::PointerButton button = canvas_control_stroke_ ? canvas_control_button_ :
        (*canvas_alt_).checked() ? gf::PointerButton::secondary : gf::PointerButton::primary;
    if (action == 0 || (action >= 6 && action <= 9)) {
        canvas_control_pointer(gf::PointerAction::move, button);
    } else if ((action == 1 || action == 2) && !canvas_control_stroke_) {
        const gf::PointerButton click = action == 2 ? gf::PointerButton::secondary : button;
        canvas_control_pointer(gf::PointerAction::down, click);
        canvas_control_pointer(gf::PointerAction::up, click);
    } else if (action == 3 && !canvas_control_stroke_) {
        canvas_control_button_ = button;
        canvas_control_pointer(gf::PointerAction::down, button);
        canvas_control_stroke_ = true;
    } else if (action == 4 || action == 5) {
        if (canvas_control_stroke_) {
            if (placing_shape_) { canvas_control_pointer(gf::PointerAction::down, canvas_control_button_); }
            canvas_control_pointer(gf::PointerAction::up, canvas_control_button_);
        }
        canvas_control_stroke_ = false;
        if (action == 5) { execute("undo"); }
    } else if (action == 10 && window()) {
        static_cast<void>((*window()).request_focus(canvas_));
    } else if (action == 11 && !canvas_control_stroke_) {
        rotate_view(0);
        refresh();
    }
    (*canvas_actions_[3]).set_selected(canvas_control_stroke_);
    canvas().invalidate(gf::Dirty::paint);
}
void Editor::canvas_control_clicked(gf::ButtonBase& button) {
    canvas_control_action(std::stoi(std::string(button.stable_id().value().substr(14))));
}
} // namespace paint::forms

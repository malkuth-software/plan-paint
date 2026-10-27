#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <stdexcept>

namespace paint::forms {
namespace gf = gui_forms;
void EditorDialog::initialize_batch_stamp() {
    const std::size_t first = controls_.size();
    batch_stamp_path_ = gf::make_control<gf::TextBox>(gf::StableId("batch-stamp-path"));
    (*batch_stamp_path_).set_accessible_name(tr("Stamp image…"));
    put(batch_stamp_path_, {260, 84, 325, 28});
    const std::shared_ptr<gf::Button> browse =
        button("batch-stamp-browse", tr("Stamp image…"), {595, 84, 145, 28});
    subscriptions_.push_back((*browse).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::batch_clicked>(*this)));
    batch_stamp_anchor_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-stamp-anchor"));
    (*batch_stamp_anchor_).set_accessible_name(tr("Anchor"));
    (*batch_stamp_anchor_)
        .set_items({tr("Top left"), tr("Top center"), tr("Top right"), tr("Middle left"), tr("Center"),
                    tr("Middle right"), tr("Bottom left"), tr("Bottom center"), tr("Bottom right")});
    (*batch_stamp_anchor_).set_selected_index(4);
    put(batch_stamp_anchor_, {260, 120, 150, 28});
    batch_stamp_order_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-stamp-order"));
    (*batch_stamp_order_).set_accessible_name(tr("Stamp"));
    (*batch_stamp_order_).set_items({tr("Foreground"), tr("Background")});
    (*batch_stamp_order_).set_selected_index(0);
    put(batch_stamp_order_, {420, 120, 150, 28});
    batch_stamp_scale_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-stamp-scale"));
    (*batch_stamp_scale_).set_accessible_name(tr("Scale"));
    (*batch_stamp_scale_).set_items({tr("Original size"), tr("Fit image")});
    (*batch_stamp_scale_).set_selected_index(0);
    put(batch_stamp_scale_, {580, 120, 160, 28});
    label("batch-stamp-x-label", "X · px", {260, 152, 110, 24});
    label("batch-stamp-y-label", "Y · px", {380, 152, 110, 24});
    label("batch-stamp-percent-label", tr("Scale") + " %", {500, 152, 110, 24});
    label("batch-stamp-opacity-label", tr("Opacity %"), {620, 152, 120, 24});
    batch_stamp_x_ = number("batch-stamp-x", {260, 178, 110, 28}, -8192, 8192, 0);
    batch_stamp_y_ = number("batch-stamp-y", {380, 178, 110, 28}, -8192, 8192, 0);
    batch_stamp_percent_ = number("batch-stamp-percent", {500, 178, 110, 28}, 1, 800, 100);
    batch_stamp_opacity_ = number("batch-stamp-opacity", {620, 178, 120, 28}, 0, 100, 100);
    (*batch_stamp_x_).set_accessible_name("X · px");
    (*batch_stamp_y_).set_accessible_name("Y · px");
    (*batch_stamp_percent_).set_accessible_name(tr("Scale") + " %");
    (*batch_stamp_opacity_).set_accessible_name(tr("Opacity %"));
    batch_stamp_flatten_ = gf::make_control<gf::CheckBox>(gf::StableId("batch-stamp-flatten"), tr("Opaque"));
    put(batch_stamp_flatten_, {260, 212, 115, 28});
    label("batch-stamp-matte-label", tr("Matte RGB"), {385, 212, 130, 28});
    batch_stamp_matte_ = gf::make_control<gf::TextBox>(gf::StableId("batch-stamp-matte"));
    (*batch_stamp_matte_).set_accessible_name(tr("Matte RGB"));
    (*batch_stamp_matte_).set_text("#FFFFFF");
    put(batch_stamp_matte_, {525, 212, 215, 28});
    label("batch-stamp-before-label", tr("Original") + " #1", {20, 106, 105, 22});
    label("batch-stamp-after-label", tr("Preview") + " #1", {135, 106, 105, 22});
    batch_stamp_before_ = gf::make_control<gf::RasterCanvas>(gf::StableId("batch-stamp-before"));
    batch_stamp_after_ = gf::make_control<gf::RasterCanvas>(gf::StableId("batch-stamp-after"));
    (*batch_stamp_before_).set_accessible_name(tr("Original") + " #1");
    (*batch_stamp_after_).set_accessible_name(tr("Preview") + " #1");
    put(batch_stamp_before_, {20, 130, 105, 58});
    put(batch_stamp_after_, {135, 130, 105, 58});
    batch_stamp_controls_.assign(controls_.begin() + first, controls_.end());
    batch_settings_.insert(batch_settings_.end(), batch_stamp_controls_.begin(), batch_stamp_controls_.end());
    for (const std::shared_ptr<gf::ComboBox>& control :
         {batch_stamp_anchor_, batch_stamp_order_, batch_stamp_scale_}) {
        subscriptions_.push_back((*control).selected_index_changed().subscribe(
            *this,
            gf::Delegate<std::optional<std::size_t>>::bind<EditorDialog, &EditorDialog::batch_choice_changed>(
                *this)));
    }
    for (const std::shared_ptr<gf::NumericUpDown>& control :
         {batch_stamp_x_, batch_stamp_y_, batch_stamp_percent_, batch_stamp_opacity_}) {
        subscriptions_.push_back((*control).value_changed().subscribe(
            *this, gf::Delegate<double>::bind<EditorDialog, &EditorDialog::batch_number_changed>(*this)));
    }
    for (const std::shared_ptr<gf::TextBox>& control : {batch_stamp_path_, batch_stamp_matte_}) {
        subscriptions_.push_back((*control).text_changed().subscribe(
            *this,
            gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::batch_text_changed>(*this)));
    }
    subscriptions_.push_back(
        (*batch_stamp_flatten_)
            .checked_changed()
            .subscribe(*this, gf::Delegate<bool>::bind<EditorDialog, &EditorDialog::batch_checked>(*this)));
}
void EditorDialog::read_batch_stamp(IconBatchSettings& settings) const {
    settings.stamp_path = (*batch_stamp_path_).text();
    settings.stamp.anchor = static_cast<int>((*batch_stamp_anchor_).selected_index().value_or(4));
    settings.stamp.behind = (*batch_stamp_order_).selected_index().value_or(0) == 1;
    settings.stamp.scale = (*batch_stamp_scale_).selected_index().value_or(0) == 0 ? IconStampScale::Natural
                                                                                   : IconStampScale::Fit;
    settings.stamp.offset_x = static_cast<int>((*batch_stamp_x_).value());
    settings.stamp.offset_y = static_cast<int>((*batch_stamp_y_).value());
    settings.stamp.percent = (*batch_stamp_percent_).value();
    settings.stamp.opacity = (*batch_stamp_opacity_).value();
    settings.stamp.flatten = (*batch_stamp_flatten_).checked();
    if (settings.stamp.flatten &&
        !from_hex(std::string((*batch_stamp_matte_).text()), settings.stamp.matte)) {
        throw std::runtime_error(tr("Enter a matte color as #RRGGBB."));
    }
}
void EditorDialog::show_batch_stamp(const IconBatchPlan& plan) {
    (*batch_stamp_before_).clear_bitmap();
    (*batch_stamp_after_).clear_bitmap();
    if (plan.preview_before.width > 0 && plan.preview_after.width > 0) {
        IconFrame before, after;
        before.image = plan.preview_before;
        after.image = plan.preview_after;
        publish_image(render_asset_preview(before, 105, 58), *batch_stamp_before_);
        publish_image(render_asset_preview(after, 105, 58), *batch_stamp_after_);
    }
}
} // namespace paint::forms

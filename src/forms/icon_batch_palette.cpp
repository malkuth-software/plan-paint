#include "forms/editor.hpp"
#include "localization.hpp"
#include <stdexcept>
namespace paint::forms {
namespace gf = gui_forms;
void EditorDialog::initialize_batch_palette() {
    const std::size_t first = controls_.size();
    batch_palette_path_ = gf::make_control<gf::TextBox>(gf::StableId("batch-palette-path"));
    (*batch_palette_path_).set_accessible_name(tr("Shared palette"));
    put(batch_palette_path_, {260, 84, 305, 28});
    const std::shared_ptr<gf::Button> browse =
        button("batch-palette-browse", tr("Import palette…"), {575, 84, 165, 28});
    subscriptions_.push_back((*browse).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::batch_clicked>(*this)));
    label("batch-palette-depth-label", tr("Color depth"), {260, 116, 225, 24});
    label("batch-palette-dither-label", tr("Dithering"), {505, 116, 235, 24});
    batch_palette_depth_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-palette-depth"));
    (*batch_palette_depth_).set_accessible_name(tr("Color depth"));
    (*batch_palette_depth_).set_items({"1 bpp · 2", "4 bpp · 16", "8 bpp · 256"});
    (*batch_palette_depth_).set_selected_index(2);
    put(batch_palette_depth_, {260, 142, 225, 28});
    batch_palette_dither_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-palette-dither"));
    (*batch_palette_dither_).set_accessible_name(tr("Dithering"));
    (*batch_palette_dither_).set_items({tr("None"), "Floyd–Steinberg"});
    (*batch_palette_dither_).set_selected_index(0);
    put(batch_palette_dither_, {505, 142, 235, 28});
    label("batch-palette-threshold-label", tr("Alpha threshold"), {260, 174, 225, 24});
    label("batch-palette-matte-label", tr("Matte RGB"), {505, 174, 235, 24});
    batch_palette_threshold_ = number("batch-palette-threshold", {260, 200, 225, 28}, 1, 255, 128);
    (*batch_palette_threshold_).set_accessible_name(tr("Alpha threshold"));
    batch_palette_matte_ = gf::make_control<gf::TextBox>(gf::StableId("batch-palette-matte"));
    (*batch_palette_matte_).set_accessible_name(tr("Matte RGB"));
    (*batch_palette_matte_).set_text("#FFFFFF");
    put(batch_palette_matte_, {505, 200, 235, 28});
    batch_palette_controls_.assign(controls_.begin() + first, controls_.end());
    batch_settings_.insert(batch_settings_.end(), batch_palette_controls_.begin(),
                           batch_palette_controls_.end());
    for (const std::shared_ptr<gf::TextBox>& control : {batch_palette_path_, batch_palette_matte_}) {
        subscriptions_.push_back((*control).text_changed().subscribe(
            *this,
            gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::batch_text_changed>(*this)));
    }
    for (const std::shared_ptr<gf::ComboBox>& control : {batch_palette_depth_, batch_palette_dither_}) {
        subscriptions_.push_back((*control).selected_index_changed().subscribe(
            *this,
            gf::Delegate<std::optional<std::size_t>>::bind<EditorDialog, &EditorDialog::batch_choice_changed>(
                *this)));
    }
    subscriptions_.push_back(
        (*batch_palette_threshold_)
            .value_changed()
            .subscribe(*this,
                       gf::Delegate<double>::bind<EditorDialog, &EditorDialog::batch_number_changed>(*this)));
}
void EditorDialog::read_batch_palette(IconBatchSettings& settings) const {
    settings.palette_path = (*batch_palette_path_).text();
    if (settings.palette_path.empty()) {
        throw std::runtime_error("Choose a shared palette file.");
    }
    const unsigned depths[] = {1, 4, 8};
    settings.palette.bits_per_pixel = depths[(*batch_palette_depth_).selected_index().value_or(2)];
    settings.palette.alpha_threshold = static_cast<unsigned>((*batch_palette_threshold_).value());
    settings.palette.dither = (*batch_palette_dither_).selected_index().value_or(0) == 0
                                  ? IconPaletteDither::None
                                  : IconPaletteDither::FloydSteinberg;
    if (!from_hex(std::string((*batch_palette_matte_).text()), settings.palette.matte)) {
        throw std::runtime_error(tr("Enter a matte color as #RRGGBB."));
    }
}
} // namespace paint::forms

#include "localization.hpp"
#include "forms/display.hpp"
#include "forms/editor.hpp"
#include <iomanip>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
struct CarpetRenderJob {
    CarpetParameters parameters;
    Image image;
    std::string error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
const char* names[10] = {"Velvet → felt",     "Fiber diameter (μm)", "Pile / sheet depth (mm)",
                         "Nap disorder",      "Fiber crimp",         "Optical roughness",
                         "Dye concentration", "Light direction (°)", "View tilt (°)",
                         "Magnification (×)"};
const double low[10] = {0, 10, .25, .05, .1, .25, .5, 0, 0, .75};
const double high[10] = {1, 36, 1.25, 1.15, 1.3, .75, 2, 360, 48, 3};
struct CarpetDelivery {
    std::weak_ptr<EditorDialog> dialog;
    void operator()() const {
        const std::shared_ptr<EditorDialog> owner = dialog.lock();
        if (owner) {
            (*owner).deliver_carpet();
        }
    }
};
struct CarpetWork {
    std::shared_ptr<CarpetRenderJob> job;
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    void operator()() const {
        try {
            (*job).image = render_carpet_tile((*job).parameters, 512, &(*job).cancel);
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(CarpetDelivery{owner}));
        }
    }
};
} // namespace
EditorDialog::~EditorDialog() {
    stop_numeric_transform();
    stop_icon_batch();
    stop_icon_states();
    stop_asset_browser();
    stop_icon_export();
    stop_toolbar_strip();
    stop_icon_validation();
    stop_icon_sheet();
    stop_carpet();
    stop_dither();
}
void EditorDialog::stop_carpet() {
    if (carpet_job_) {
        (*carpet_job_).cancel.store(true);
    }
    if (carpet_worker_.joinable()) {
        carpet_worker_.join();
    }
    carpet_job_.reset();
}
void EditorDialog::on_attached_to_window() {
    Control::on_attached_to_window();
    if (kind_ == EditorDialogKind::icon_validation) { start_icon_validation(); }
    if (kind_ == EditorDialogKind::toolbar_strip) { start_toolbar_strip(); }
    if (kind_ == EditorDialogKind::numeric_transform) { start_numeric_transform(); }
    if (kind_ == EditorDialogKind::icon_states) {
        render_icon_states();
    }
    if (kind_ == EditorDialogKind::icon_palette) {
        render_icon_palette();
    }
    if (kind_ == EditorDialogKind::icon_effects) {
        render_icon_effects();
    }
    if (kind_ == EditorDialogKind::alpha_tools) {
        render_alpha_tools();
    }
    if (kind_ == EditorDialogKind::asset_browser) {
        request_assets(true);
    }
    if (kind_ == EditorDialogKind::icon_sheet) {
        render_icon_sheet_preview();
    }
    if (kind_ == EditorDialogKind::dither) render_dither();
    if (kind_ == EditorDialogKind::carpet) {
        render_carpet_preview();
    }
}
void EditorDialog::on_detaching_from_window(gf::Window& window) noexcept {
    stop_numeric_transform();
    stop_icon_batch();
    stop_icon_states();
    stop_asset_browser();
    stop_icon_export();
    stop_toolbar_strip();
    stop_icon_validation();
    stop_icon_sheet();
    stop_carpet();
    stop_dither();
    Control::on_detaching_from_window(window);
}
void EditorDialog::initialize_carpet() {
    panel_ = {0, 0, 920, 650};
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        carpet_ = (*editor).carpet_parameters;
    }
    carpet_presets_ = gf::make_control<gf::ComboBox>(gf::StableId("carpet-presets"));
    (*carpet_presets_).set_accessible_name(tr("Fabric preset"));
    (*carpet_presets_).add_item(tr("Current settings"));
    for (int i = 0; i < carpet_preset_count; ++i) {
        (*carpet_presets_).add_item(tr(carpet_preset_name(i)));
    }
    (*carpet_presets_).set_selected_index(0);
    put(carpet_presets_, {22, 47, 505, 30});
    subscriptions_.push_back(
        (*carpet_presets_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::carpet_preset_changed>(*this)));
    for (int i = 0; i < 10; ++i) {
        const double y = 92 + i * 43;
        label("carpet-label-" + std::to_string(i), tr(names[i]), {22, y, 208, 25});
        const std::shared_ptr<gf::TrackBar> slider =
            gf::make_control<gf::TrackBar>(gf::StableId("carpet-slider-" + std::to_string(i)));
        (*slider).set_accessible_name(tr(names[i]));
        (*slider).set_range(low[i], high[i]);
        (*slider).set_small_change((high[i] - low[i]) / 100);
        (*slider).set_large_change((high[i] - low[i]) / 10);
        put(slider, {236, y, 220, 26});
        carpet_sliders_.push_back(slider);
        subscriptions_.push_back((*slider).value_changed().subscribe(
            *this, gf::Delegate<double>::bind<EditorDialog, &EditorDialog::carpet_changed>(*this)));
        const std::shared_ptr<gf::Label> value =
            gf::make_control<gf::Label>(gf::StableId("carpet-value-" + std::to_string(i)));
        put(value, {469, y, 66, 26});
        carpet_values_.push_back(value);
    }
    label("carpet-dye-label", tr("Dye color (hex)"), {22, 529, 180, 28});
    carpet_color_ = gf::make_control<gf::TextBox>(gf::StableId("carpet-color"));
    (*carpet_color_).set_accessible_name(tr("Dye color, hex RGB"));
    put(carpet_color_, {236, 527, 150, 30});
    subscriptions_.push_back(
        (*carpet_color_)
            .text_changed()
            .subscribe(
                *this,
                gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::carpet_color_changed>(
                    *this)));
    carpet_preview_ = gf::make_control<gf::RasterCanvas>(gf::StableId("carpet-preview"));
    (*carpet_preview_).set_accessible_name(tr("Generated carpet texture preview"));
    put(carpet_preview_, {576, 92, 320, 320});
    (*carpet_preview_).set_zoom(.625);
    carpet_hillshade_ = gf::make_control<gf::CheckBox>(gf::StableId("carpet-hillshade"), tr("Hillshade relief"));
    put(carpet_hillshade_, {576, 435, 290, 28});
    subscriptions_.push_back(
        (*carpet_hillshade_)
            .clicked()
            .subscribe(*this,
                       gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::clicked>(*this)));
    button("carpet-seed", tr("New fibers"), {576, 483, 145, 30});
    label("carpet-hint",
          tr("The generated texture follows your brush.\nLighting and dye are part of the texture."),
          {576, 527, 315, 42});
    sync_carpet();
}
void EditorDialog::sync_carpet() {
    synchronizing_ = true;
    const double values[10] = {carpet_.web,   carpet_.radius * 2000,      carpet_.height, carpet_.disorder,
                               carpet_.crimp, carpet_.roughness,          carpet_.dye,    carpet_.light,
                               carpet_.view,  carpet_.magnification / .60};
    for (int i = 0; i < 10; ++i) {
        (*carpet_sliders_[i]).set_value(values[i]);
        std::ostringstream text;
        text << std::fixed << std::setprecision(i == 1 || i == 7 || i == 8 ? 0 : 2) << values[i];
        (*carpet_values_[i]).set_text(text.str());
    }
    (*carpet_color_).set_text(to_hex(carpet_.color));
    (*carpet_hillshade_).set_checked(carpet_.hillshade);
    synchronizing_ = false;
}
void EditorDialog::carpet_changed(double) {
    if (synchronizing_) {
        return;
    }
    const double old_roughness = carpet_.roughness;
    carpet_.web = (*carpet_sliders_[0]).value();
    carpet_.radius = (*carpet_sliders_[1]).value() / 2000;
    carpet_.height = (*carpet_sliders_[2]).value();
    carpet_.disorder = (*carpet_sliders_[3]).value();
    carpet_.crimp = (*carpet_sliders_[4]).value();
    carpet_.roughness = (*carpet_sliders_[5]).value();
    carpet_.dye = (*carpet_sliders_[6]).value();
    carpet_.light = (*carpet_sliders_[7]).value();
    carpet_.view = (*carpet_sliders_[8]).value();
    carpet_.magnification = (*carpet_sliders_[9]).value() * .60;
    if (old_roughness != carpet_.roughness) {
        carpet_.azimuth_roughness = std::clamp(carpet_.roughness + .1, .30, .85);
    }
    carpet_.hillshade = (*carpet_hillshade_).checked();
    synchronizing_ = true;
    (*carpet_presets_).set_selected_index(0);
    synchronizing_ = false;
    sync_carpet();
    render_carpet_preview();
}
void EditorDialog::carpet_color_changed(const std::string& value) {
    if (synchronizing_) {
        return;
    }
    Color color;
    if (from_hex(value, color)) {
        carpet_.color = color;
        synchronizing_ = true;
        (*carpet_presets_).set_selected_index(0);
        synchronizing_ = false;
        render_carpet_preview();
    } else {
        stop_carpet();
        carpet_image_ = {};
        (*error_).set_text(tr("Enter a hex RGB color, such as #1645C5."));
        if (attached_window()) {
            (*(*attached_window()).find("dialog-ok")).set_enabled(false);
        }
    }
}
void EditorDialog::carpet_preset_changed(std::optional<std::size_t> index) {
    if (synchronizing_ || !index || *index == 0) {
        return;
    }
    carpet_ = carpet_preset(static_cast<int>(*index) - 1);
    sync_carpet();
    render_carpet_preview();
}
void EditorDialog::render_carpet_preview() {
    if (!attached_window()) {
        return;
    }
    stop_carpet();
    carpet_image_ = {};
    (*error_).set_text(tr("Rendering fibers…"));
    (*(*attached_window()).find("dialog-ok")).set_enabled(false);
    carpet_job_ = std::make_shared<CarpetRenderJob>();
    (*carpet_job_).parameters = carpet_;
    carpet_worker_ = std::thread(
        CarpetWork{carpet_job_, this, std::static_pointer_cast<EditorDialog>(shared_from_this())});
}
void EditorDialog::deliver_carpet() {
    if (!attached_window() || !carpet_job_ || !(*carpet_job_).done.load(std::memory_order_acquire)) {
        return;
    }
    if (carpet_worker_.joinable()) {
        carpet_worker_.join();
    }
    if (!(*carpet_job_).error.empty()) {
        (*error_).set_text((*carpet_job_).error);
        return;
    }
    carpet_image_ = std::move((*carpet_job_).image);
    carpet_job_.reset();
    publish_image(carpet_image_, *carpet_preview_);
    (*error_).set_text("");
    (*(*attached_window()).find("dialog-ok")).set_enabled(true);
}
} // namespace paint::forms

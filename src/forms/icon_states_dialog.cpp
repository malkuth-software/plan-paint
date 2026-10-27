#include "codecs.hpp"
#include "forms/atlas.hpp"
#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include "paths.hpp"
#include <sstream>

namespace paint::forms {
namespace gf = gui_forms;
struct IconStateJob {
    std::shared_ptr<const ImageContainer> source;
    std::vector<int> selection;
    IconStateRecipe recipe;
    IconStateResult result;
    std::vector<std::uint8_t> bytes;
    std::string report, error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct IconStateDelivery {
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<IconStateJob> job;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_icon_states(job);
        }
    }
};
struct IconStateWork {
    std::shared_ptr<IconStateJob> job;
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    void operator()() const {
        try {
            (*job).result =
                generate_icon_state(*(*job).source, (*job).selection, (*job).recipe, &(*job).cancel);
            if (!(*job).cancel.load()) {
                (*job).report = icon_state_report((*job).result);
                if (icon_state_ready((*job).result)) {
                    (*job).bytes = encode_icon_container((*job).result.output);
                }
            }
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(IconStateDelivery{owner, job}));
        }
    }
};
} // namespace
void EditorDialog::initialize_icon_states() {
    panel_ = {0, 0, 780, 510};
    const std::shared_ptr<Editor> editor = editor_.lock();
    Document& document = (*editor).document;
    document.sync_atlas();
    state_source_ = std::make_shared<const ImageContainer>(
        document.output_container(document.atlas.kind == AtlasKind::Cursor));
    state_source_path_ = document.filename;
    state_current_ = document.atlas.active;
    state_selected_ = document.atlas.sequence;
    label("state-scope-label", tr("Images"), {20, 48, 105, 28});
    state_scope_ = gf::make_control<gf::ComboBox>(gf::StableId("state-scope"));
    (*state_scope_).set_accessible_name(tr("Images"));
    (*state_scope_).set_items({tr("All representations"), tr("Current image"), tr("Selected sequence")});
    (*state_scope_).set_selected_index(0);
    put(state_scope_, {135, 48, 240, 28});
    label("state-preset-label", tr("Preset"), {405, 48, 105, 28});
    state_preset_ = gf::make_control<gf::ComboBox>(gf::StableId("state-preset"));
    (*state_preset_).set_accessible_name(tr("Preset"));
    (*state_preset_).set_items({tr("Normal"), tr("Hot"), tr("Disabled")});
    (*state_preset_).set_selected_index(2);
    put(state_preset_, {520, 48, 240, 28});
    label("state-saturation-label", tr("Saturation %"), {20, 88, 145, 28});
    state_saturation_ = number("state-saturation", {165, 88, 80, 28}, 0, 200, 0);
    label("state-brightness-label", tr("Brightness %"), {275, 88, 145, 28});
    state_brightness_ = number("state-brightness", {420, 88, 80, 28}, -100, 100, 0);
    label("state-opacity-label", tr("Opacity %"), {530, 88, 150, 28});
    state_opacity_ = number("state-opacity", {680, 88, 80, 28}, 0, 100, 55);
    label("state-policy-label", tr("Output"), {20, 128, 105, 28});
    state_policy_ = gf::make_control<gf::ComboBox>(gf::StableId("state-policy"));
    (*state_policy_).set_accessible_name(tr("Output"));
    (*state_policy_).set_items({"RGBA", tr("Indexed sources → DIB")});
    (*state_policy_).set_selected_index(0);
    put(state_policy_, {135, 128, 240, 28});
    label("state-image-label", tr("Preview"), {405, 128, 105, 28});
    state_image_ = gf::make_control<gf::ComboBox>(gf::StableId("state-image"));
    (*state_image_).set_accessible_name(tr("Preview"));
    put(state_image_, {520, 128, 240, 28});
    label("state-policy-note", tr("Indexed: 1/4/8 bpp; 2→4. Alpha <128 transparent; otherwise white matte."),
          {20, 162, 740, 26});
    const std::shared_ptr<gf::Label> policy_note = std::static_pointer_cast<gf::Label>(controls_.back());
    (*policy_note).set_font({gf::FontRole::control, 11, 400, false, 0.05});
    (*policy_note).set_text_wrapping(gf::TextWrapping::word);
    label("state-before-label", tr("Original"), {20, 190, 355, 22});
    label("state-after-label", tr("Preview"), {405, 190, 355, 22});
    state_before_ = gf::make_control<gf::RasterCanvas>(gf::StableId("state-before"));
    state_after_ = gf::make_control<gf::RasterCanvas>(gf::StableId("state-after"));
    (*state_before_).set_accessible_name(tr("Original"));
    (*state_after_).set_accessible_name(tr("Preview"));
    put(state_before_, {20, 215, 355, 135});
    put(state_after_, {405, 215, 355, 135});
    state_report_ = gf::make_control<gf::TextBox>(gf::StableId("state-report"));
    (*state_report_).set_read_only(true);
    (*state_report_).set_multiline(true);
    (*state_report_).set_auto_scroll(true);
    (*state_report_).set_accessible_name(tr("Output manifest"));
    put(state_report_, {20, 359, 740, 65});
    label("state-contract", tr("Preview snapshot; source images stay unchanged."), {20, 428, 740, 20});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    put(error_, {20, 450, 740, 20});
    const std::shared_ptr<gf::Button> reset = button("state-reset", tr("Reset"), {20, 474, 140, 28});
    state_save_report_ = button("state-save-report", tr("Save report…"), {370, 474, 140, 28});
    state_export_ = button("state-export", tr("Export…"), {520, 474, 130, 28});
    (*state_save_report_).set_enabled(false);
    (*state_export_).set_enabled(false);
    button("dialog-cancel", tr("Close"), {660, 474, 100, 28});
    for (const std::shared_ptr<gf::Button>& control : {reset, state_save_report_, state_export_}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::state_clicked>(*this)));
    }
    for (const std::shared_ptr<gf::NumericUpDown>& control :
         {state_saturation_, state_brightness_, state_opacity_}) {
        subscriptions_.push_back((*control).value_changed().subscribe(
            *this, gf::Delegate<double>::bind<EditorDialog, &EditorDialog::state_number_changed>(*this)));
    }
    for (const std::shared_ptr<gf::ComboBox>& control : {state_scope_, state_policy_}) {
        subscriptions_.push_back((*control).selected_index_changed().subscribe(
            *this,
            gf::Delegate<std::optional<std::size_t>>::bind<EditorDialog, &EditorDialog::state_choice_changed>(
                *this)));
    }
    subscriptions_.push_back(
        (*state_preset_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::state_preset_changed>(*this)));
    subscriptions_.push_back(
        (*state_image_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::show_icon_state_image>(*this)));
}
void EditorDialog::stop_icon_states() {
    if (state_job_) {
        (*state_job_).cancel.store(true);
    }
    if (state_worker_.joinable()) {
        state_worker_.join();
    }
    state_job_.reset();
}
void EditorDialog::state_number_changed(double) {
    if (!state_updating_) {
        render_icon_states();
    }
}
void EditorDialog::state_choice_changed(std::optional<std::size_t>) {
    render_icon_states();
}
void EditorDialog::state_preset_changed(std::optional<std::size_t>) {
    const IconStateRecipe recipe =
        icon_state_preset(static_cast<IconStatePreset>((*state_preset_).selected_index().value_or(2)));
    state_updating_ = true;
    (*state_saturation_).set_value(recipe.saturation);
    (*state_brightness_).set_value(recipe.brightness);
    (*state_opacity_).set_value(recipe.opacity);
    state_updating_ = false;
    render_icon_states();
}
void EditorDialog::render_icon_states() {
    if (!attached_window()) {
        return;
    }
    stop_icon_states();
    (*state_export_).set_enabled(false);
    (*state_save_report_).set_enabled(false);
    (*state_before_).clear_bitmap();
    (*state_after_).clear_bitmap();
    (*state_image_).set_items({});
    (*state_report_).set_text(tr("Preparing preview…"));
    (*error_).set_text("");
    state_job_ = std::make_shared<IconStateJob>();
    IconStateJob& job = *state_job_;
    job.source = state_source_;
    const std::size_t scope = (*state_scope_).selected_index().value_or(0);
    if (scope == 0) {
        for (std::size_t index = 0; index < (*state_source_).frames.size(); ++index) {
            job.selection.push_back(static_cast<int>(index));
        }
    } else if (scope == 1) {
        job.selection.push_back(state_current_);
    } else {
        job.selection = state_selected_;
    }
    job.recipe.preset = static_cast<IconStatePreset>((*state_preset_).selected_index().value_or(2));
    job.recipe.saturation = (*state_saturation_).value();
    job.recipe.brightness = (*state_brightness_).value();
    job.recipe.opacity = (*state_opacity_).value();
    job.recipe.palette = (*state_policy_).selected_index().value_or(0) == 0 ? IconStatePalette::Rgba
                                                                            : IconStatePalette::IndexedDib;
    state_worker_ = std::thread(
        IconStateWork{state_job_, this, std::static_pointer_cast<EditorDialog>(shared_from_this())});
}
void EditorDialog::deliver_icon_states(const std::shared_ptr<IconStateJob>& completed) {
    if (!attached_window() || !state_job_ || state_job_ != completed ||
        !(*state_job_).done.load(std::memory_order_acquire)) {
        return;
    }
    if (state_worker_.joinable()) {
        state_worker_.join();
    }
    const IconStateJob& job = *state_job_;
    if (!job.error.empty()) {
        (*state_report_).set_text("");
        (*error_).set_text(job.error);
        return;
    }
    std::vector<std::string> images;
    std::ostringstream report;
    for (const IconStateEntry& entry : job.result.entries) {
        const Image& image = (*state_source_).frames[entry.source_index].image;
        const std::string identity = "#" + std::to_string(entry.source_index + 1) + " · " +
                                     std::to_string(image.width) + " × " + std::to_string(image.height);
        images.push_back(identity);
        report << identity << " · " << tr("Source: ") << icon_format_name(entry.source_format);
        if (entry.status == IconStateStatus::Ready) {
            report << " · " << tr("Save: ") << icon_format_name(entry.output_format) << " · "
                   << tr("Partial alpha converted") << ": " << entry.partial_alpha_converted;
        } else {
            report << " · " << entry.diagnostic;
        }
        report << '\n';
    }
    (*state_report_).set_text(report.str());
    (*state_image_).set_items(std::move(images));
    (*state_image_).set_selected_index(0);
    show_icon_state_image(std::nullopt);
    (*state_export_).set_enabled(!job.bytes.empty() && icon_state_ready(job.result));
    (*state_save_report_).set_enabled(!job.report.empty());
}
void EditorDialog::show_icon_state_image(std::optional<std::size_t>) {
    if (!state_job_ || !(*state_job_).done.load(std::memory_order_acquire)) {
        return;
    }
    const IconStateResult& result = (*state_job_).result;
    const std::size_t index = (*state_image_).selected_index().value_or(result.entries.size());
    (*state_before_).clear_bitmap();
    (*state_after_).clear_bitmap();
    if (index >= result.entries.size()) {
        return;
    }
    const IconStateEntry& entry = result.entries[index];
    publish_image(render_asset_preview((*state_source_).frames[entry.source_index], 355, 135),
                  *state_before_);
    if (entry.status == IconStateStatus::Ready) {
        publish_image(render_asset_preview(result.output.frames[entry.output_index], 355, 135),
                      *state_after_);
    }
}
void EditorDialog::state_clicked(gf::ButtonBase& control) {
    if (control.stable_id().value() == "state-reset") {
        state_preset_changed(std::nullopt);
        return;
    }
    const std::shared_ptr<Editor> editor = editor_.lock();
    const std::shared_ptr<IconStateJob> job = state_job_;
    if (!editor || !job || !(*job).done.load(std::memory_order_acquire) || !(*job).error.empty()) {
        return;
    }
    try {
        if (control.stable_id().value() == "state-save-report" && !(*job).report.empty()) {
            static_cast<void>((*editor).save_export_report((*job).report, "icon-states-report.json"));
        } else if (control.stable_id().value() == "state-export" && icon_state_ready((*job).result) &&
                   !(*job).bytes.empty()) {
            const bool cursor = (*job).result.output.kind == ContainerKind::Cursor;
            const std::string stem =
                state_source_path_.empty() ? "icon" : path_to_utf8(path_from_utf8(state_source_path_).stem());
            const std::string name =
                stem + "-" + icon_state_name((*job).recipe.preset) + (cursor ? ".cur" : ".ico");
            if ((*editor).export_icon_variant((*job).bytes, cursor, name, state_source_path_)) {
                (*error_).set_text(tr("Variant exported."));
            }
        }
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
    }
}
} // namespace paint::forms

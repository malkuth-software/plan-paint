#include "localization.hpp"
#include "forms/display.hpp"
#include "forms/editor.hpp"
namespace paint::forms {
namespace gf = gui_forms;
struct DitherRenderJob {
    Image source, result;
    std::vector<std::uint8_t> mask;
    std::vector<Color> palette;
    DitherOptions options;
    std::string error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct DitherDelivery {
    std::weak_ptr<EditorDialog> owner;
    void operator()() const {
        std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_dither();
        }
    }
};
struct DitherWork {
    std::shared_ptr<DitherRenderJob> job;
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    void operator()() const {
        try {
            (*job).result =
                dithered((*job).source, (*job).mask, (*job).options, &(*job).palette, &(*job).cancel);
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).source = {};
        (*job).mask = {};
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(DitherDelivery{owner}));
        }
    }
};
} // namespace
void EditorDialog::initialize_dither() {
    panel_ = {0, 0, 620, 580};
    std::shared_ptr<Editor> editor = editor_.lock();
    label("dither-colors-label", tr("Colors"), {24, 52, 120, 28});
    dither_count_ = number("dither-colors", {145, 52, 105, 28}, 2, 32, (*editor).dither_options.colors);
    label("dither-pattern-label", tr("Pattern"), {285, 52, 80, 28});
    dither_pattern_ = gf::make_control<gf::ComboBox>(gf::StableId("dither-pattern"));
    (*dither_pattern_).set_accessible_name(tr("Dithering pattern"));
    for (const std::string& name : {tr("Crosswind"), tr("Weave"), tr("Scrambled"), tr("Drift"), tr("Posterize (no dither)")}) {
        (*dither_pattern_).add_item(name);
    }
    (*dither_pattern_).set_selected_index(static_cast<std::size_t>((*editor).dither_options.pattern));
    put(dither_pattern_, {367, 52, 228, 28});
    dither_preview_ = gf::make_control<gf::RasterCanvas>(gf::StableId("dither-preview"));
    (*dither_preview_).set_accessible_name(tr("Dither result preview"));
    put(dither_preview_, {24, 96, 572, 320});
    dither_status_ = gf::make_control<gf::Label>(gf::StableId("dither-status"));
    put(dither_status_, {24, 426, 572, 28});
    label("dither-scope",
          (*editor).document.selection.active && (*editor).document.selection.canvas_selection
              ? tr("Applies inside the selection. Alpha is preserved.")
              : tr("Applies to the canvas. Alpha is preserved."),
          {24, 455, 572, 24});
    subscriptions_.push_back(
        (*dither_count_)
            .value_changed()
            .subscribe(*this,
                       gf::Delegate<double>::bind<EditorDialog, &EditorDialog::dither_count_changed>(*this)));
    subscriptions_.push_back(
        (*dither_pattern_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::dither_pattern_changed>(*this)));
}
void EditorDialog::dither_count_changed(double) {
    render_dither();
}
void EditorDialog::dither_pattern_changed(std::optional<std::size_t>) {
    render_dither();
}
void EditorDialog::stop_dither() {
    if (dither_job_) {
        (*dither_job_).cancel.store(true);
    }
    if (dither_worker_.joinable()) {
        dither_worker_.join();
    }
    dither_job_.reset();
}
void EditorDialog::render_dither() {
    if (!attached_window()) {
        return;
    }
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    stop_dither();
    (*(*attached_window()).find("dialog-ok")).set_enabled(false);
    (*dither_status_).set_text(tr("Preparing palette and preview…"));
    (*error_).set_text("");
    dither_job_ = std::make_shared<DitherRenderJob>();
    (*dither_job_).source = (*editor).document.image;
    (*dither_job_).mask = (*editor).canvas_selection_mask();
    (*dither_job_).options.colors = static_cast<int>((*dither_count_).value());
    (*dither_job_).options.pattern =
        static_cast<DitherPattern>((*dither_pattern_).selected_index().value_or(2));
    dither_worker_ = std::thread(
        DitherWork{dither_job_, this, std::static_pointer_cast<EditorDialog>(shared_from_this())});
}
void EditorDialog::deliver_dither() {
    if (!attached_window() || !dither_job_ || !(*dither_job_).done.load(std::memory_order_acquire)) {
        return;
    }
    if (dither_worker_.joinable()) {
        dither_worker_.join();
    }
    if (!(*dither_job_).error.empty()) {
        (*error_).set_text((*dither_job_).error);
        return;
    }
    const Image& image = (*dither_job_).result;
    if (image.pixels.empty()) {
        return;
    }
    publish_image(image, *dither_preview_);
    (*dither_preview_).set_zoom(std::min(572.0 / image.width, 320.0 / image.height));
    (*dither_status_)
        .set_text(std::to_string((*dither_job_).palette.size()) + tr(" colors — preview is the prepared result"));
    (*(*attached_window()).find("dialog-ok")).set_enabled(true);
}
void EditorDialog::accept_dither() {
    if (!dither_job_ || !(*dither_job_).done.load(std::memory_order_acquire) ||
        !(*dither_job_).error.empty() || (*dither_job_).result.pixels.empty()) {
        return;
    }
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    (*editor).document.checkpoint();
    (*editor).document.image = std::move((*dither_job_).result);
    (*editor).dither_options = (*dither_job_).options;
    (*editor).close_editor_dialog();
    (*editor).refresh();
}
} // namespace paint::forms

#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <algorithm>
namespace paint::forms {
namespace gf = gui_forms;
struct NumericTransformJob {
    std::shared_ptr<const NumericTransformSource> source;
    NumericTransformSettings settings;
    NumericTransformResult result;
    std::string error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct TransformDelivery {
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<NumericTransformJob> job;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_numeric_transform(job);
        }
    }
};
struct TransformWork {
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<NumericTransformJob> job;
    void operator()() const {
        try {
            (*job).result = render_numeric_transform(*(*job).source, (*job).settings, &(*job).cancel);
        } catch (const std::exception& exception) {
            (*job).error = exception.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(TransformDelivery{owner, job}));
        }
    }
};
Image transform_proof(const Image& canvas, const FloatingSelection& selection) {
    IconFrame frame;
    frame.image = canvas;
    if (selection.active) {
        selection.composite_onto(frame.image);
    }
    const int zoom = std::min(355 / frame.image.width, 192 / frame.image.height);
    if (zoom > 1) {
        Image enlarged;
        enlarged.reset(frame.image.width * zoom, frame.image.height * zoom, {0, 0, 0, 0});
        for (int y = 0; y < enlarged.height; ++y) {
            for (int x = 0; x < enlarged.width; ++x) {
                enlarged.set(x, y, frame.image.get(x / zoom, y / zoom));
            }
        }
        frame.image = std::move(enlarged);
    }
    return render_asset_preview(frame, 355, 192);
}
} // namespace
void EditorDialog::initialize_numeric_transform() {
    panel_ = {0, 0, 780, 510};
    const std::shared_ptr<Editor> editor = editor_.lock();
    numeric_source_ =
        std::make_shared<const NumericTransformSource>(capture_numeric_transform((*editor).document));
    const NumericTransformSource& source = *numeric_source_;
    label("transform-scope",
          tr(source.selection.active ? "Selection" : "Current image") + " · " +
              std::to_string(source.artwork.width) + " × " + std::to_string(source.artwork.height),
          {20, 48, 740, 28});
    label("transform-before-label", tr("Original"), {20, 84, 355, 24});
    label("transform-after-label", tr("Preview"), {405, 84, 355, 24});
    numeric_before_ = gf::make_control<gf::RasterCanvas>(gf::StableId("transform-before"));
    numeric_after_ = gf::make_control<gf::RasterCanvas>(gf::StableId("transform-after"));
    (*numeric_before_).set_accessible_name(tr("Original"));
    (*numeric_after_).set_accessible_name(tr("Preview"));
    put(numeric_before_, {20, 112, 355, 192});
    put(numeric_after_, {405, 112, 355, 192});
    const char* names[] = {"Offset X", "Offset Y", "Scale X %", "Scale Y %", "Angle (°)"};
    const double lows[] = {-16384, -16384, 1, 1, -360}, highs[] = {16384, 16384, 1600, 1600, 360},
                 defaults[] = {0, 0, 100, 100, 0};
    for (int index = 0; index < 5; ++index) {
        label("transform-label-" + std::to_string(index), tr(names[index]),
              {20 + index * 148.0, 312, 140, 24});
        const std::shared_ptr<gf::NumericUpDown> value =
            number("transform-value-" + std::to_string(index), {20 + index * 148.0, 340, 140, 28},
                   lows[index], highs[index], defaults[index], 2);
        (*value).set_accessible_name(tr(names[index]));
        numeric_values_.push_back(value);
        subscriptions_.push_back((*value).value_changed().subscribe(
            *this, gf::Delegate<double>::bind<EditorDialog, &EditorDialog::numeric_transform_number>(*this)));
    }
    label("transform-sampling-label", tr("Sampling"), {20, 382, 150, 28});
    numeric_sampling_ = gf::make_control<gf::ComboBox>(gf::StableId("transform-sampling"));
    (*numeric_sampling_).set_accessible_name(tr("Sampling"));
    (*numeric_sampling_).set_items({tr("CONV* point"), tr("CONV* minification"), tr("CONV* area")});
    (*numeric_sampling_).set_selected_index(1);
    put(numeric_sampling_, {175, 382, 380, 28});
    subscriptions_.push_back(
        (*numeric_sampling_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::numeric_transform_choice>(*this)));
    numeric_status_ = gf::make_control<gf::Label>(gf::StableId("transform-status"));
    put(numeric_status_, {20, 416, 740, 24});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    (*error_).set_text_wrapping(gf::TextWrapping::word);
    put(error_, {20, 442, 740, 28});
    numeric_preview_ = button("transform-preview", tr("Preview"), {20, 474, 150, 28});
    numeric_apply_ = button("transform-apply", tr("Apply"), {550, 474, 100, 28});
    button("dialog-cancel", tr("Close"), {660, 474, 100, 28});
    for (const std::shared_ptr<gf::Button>& control : {numeric_preview_, numeric_apply_}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this,
            gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::numeric_transform_clicked>(
                *this)));
    }
    (*numeric_apply_).set_enabled(false);
}
void EditorDialog::stop_numeric_transform() {
    if (numeric_job_) {
        (*numeric_job_).cancel.store(true);
    }
    if (numeric_worker_.joinable()) {
        numeric_worker_.join();
    }
    numeric_job_.reset();
}
void EditorDialog::invalidate_numeric_transform() {
    stop_numeric_transform();
    (*numeric_apply_).set_enabled(false);
    (*numeric_preview_).set_enabled(true);
    (*numeric_status_).set_text(tr("Preview"));
    (*error_).set_text("");
    (*numeric_after_).clear_bitmap();
}
void EditorDialog::numeric_transform_number(double) {
    invalidate_numeric_transform();
}
void EditorDialog::numeric_transform_choice(std::optional<std::size_t>) {
    invalidate_numeric_transform();
}
void EditorDialog::start_numeric_transform() {
    stop_numeric_transform();
    (*numeric_apply_).set_enabled(false);
    (*numeric_preview_).set_enabled(false);
    (*error_).set_text("");
    (*numeric_status_).set_text(tr("Preparing preview…"));
    publish_image(transform_proof((*numeric_source_).canvas, (*numeric_source_).selection), *numeric_before_);
    numeric_job_ = std::make_shared<NumericTransformJob>();
    NumericTransformJob& job = *numeric_job_;
    job.source = numeric_source_;
    job.settings.offset_x = (*numeric_values_[0]).value();
    job.settings.offset_y = (*numeric_values_[1]).value();
    job.settings.scale_x = (*numeric_values_[2]).value();
    job.settings.scale_y = (*numeric_values_[3]).value();
    job.settings.angle = (*numeric_values_[4]).value();
    job.settings.sampling = static_cast<WarpSampling>((*numeric_sampling_).selected_index().value_or(1));
    numeric_worker_ = std::thread(
        TransformWork{this, std::static_pointer_cast<EditorDialog>(shared_from_this()), numeric_job_});
}
void EditorDialog::deliver_numeric_transform(const std::shared_ptr<NumericTransformJob>& job) {
    if (!attached_window() || job != numeric_job_ || !(*job).done.load(std::memory_order_acquire)) {
        return;
    }
    if (numeric_worker_.joinable()) {
        numeric_worker_.join();
    }
    (*numeric_preview_).set_enabled(true);
    (*numeric_status_)
        .set_text(tr("Current image") + " · " + std::to_string((*numeric_source_).canvas.width) + " × " +
                  std::to_string((*numeric_source_).canvas.height));
    (*error_).set_text((*job).error);
    if ((*job).error.empty()) {
        publish_image(transform_proof((*job).result.canvas, (*job).result.selection), *numeric_after_);
        (*numeric_apply_).set_enabled((*job).result.changed);
    }
}
void EditorDialog::numeric_transform_clicked(gf::ButtonBase& control) {
    const std::shared_ptr<EditorDialog> keep_alive =
        std::static_pointer_cast<EditorDialog>(shared_from_this());
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    try {
        if (control.stable_id().value() == "transform-preview") {
            start_numeric_transform();
        } else if ((*numeric_apply_).enabled() && numeric_job_) {
            (*numeric_apply_).set_enabled(false);
            apply_numeric_transform((*editor).document, *numeric_source_, std::move((*numeric_job_).result));
            (*editor).close_editor_dialog();
            (*editor).refresh();
        }
    } catch (const std::exception& exception) {
        (*numeric_preview_).set_enabled(true);
        (*error_).set_text(exception.what());
    }
}
} // namespace paint::forms

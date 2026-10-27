#include "forms/editor.hpp"
#include "localization.hpp"
#include "paths.hpp"
#include <chrono>
#include <mutex>
#include <sstream>
#include <stdexcept>
namespace paint::forms {
namespace gf = gui_forms;
struct IconBatchJob {
    IconBatchPlan plan, published;
    IconBatchSettings settings;
    std::vector<std::string> sources;
    std::string directory, error;
    std::mutex mutex;
    std::atomic<bool> cancel{false}, done{false}, delivery_pending{false};
    bool execute = false;
};
namespace {
constexpr int batch_sizes[] = {16, 24, 32, 48, 64, 96, 128, 256};
struct BatchDelivery {
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<IconBatchJob> job;
    void operator()() const {
        (*job).delivery_pending.store(false);
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_icon_batch(job);
        }
    }
};
struct BatchWork {
    std::shared_ptr<IconBatchJob> job;
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    std::chrono::steady_clock::time_point last_progress{};
    static void progress(const IconBatchPlan& plan, void* context) {
        BatchWork& work = *static_cast<BatchWork*>(context);
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now - work.last_progress < std::chrono::milliseconds(100) ||
            (*work.job).delivery_pending.exchange(true)) {
            return;
        }
        work.last_progress = now;
        {
            std::lock_guard<std::mutex> lock((*work.job).mutex);
            (*work.job).published = plan;
        }
        static_cast<void>((*work.dispatcher).begin_invoke(BatchDelivery{work.owner, work.job}));
    }
    void operator()() {
        try {
            if ((*job).execute) {
                run_icon_batch((*job).plan, (*job).cancel, &BatchWork::progress, this);
            } else {
                (*job).plan =
                    plan_icon_batch((*job).sources, (*job).directory, (*job).settings, (*job).cancel);
            }
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock((*job).mutex);
            (*job).error = error.what();
        }
        {
            std::lock_guard<std::mutex> lock((*job).mutex);
            (*job).published = (*job).plan;
        }
        (*job).done.store(true, std::memory_order_release);
        static_cast<void>((*dispatcher).begin_invoke(BatchDelivery{owner, job}));
    }
};
} // namespace
std::vector<std::string> Editor::choose_batch_sources(bool multiple) {
    gf::HostOpenFileDialogRequest request;
    request.title = multiple ? tr("Add files…") : tr("Stamp image…");
    request.allow_multiple = multiple;
    const gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(dialog(request).payload);
    return result.outcome == gf::HostDialogOutcome::accepted ? result.paths : std::vector<std::string>{};
}
void EditorDialog::initialize_icon_batch() {
    panel_ = {0, 0, 760, 510};
    batch_sources_ = gf::make_control<gf::ListBox>(gf::StableId("batch-sources"));
    (*batch_sources_).set_accessible_name(tr("Source files"));
    put(batch_sources_, {20, 48, 220, 140});
    const std::shared_ptr<gf::Button> add = button("batch-add", tr("Add files…"), {20, 198, 110, 28});
    const std::shared_ptr<gf::Button> remove = button("batch-remove", tr("Remove"), {140, 198, 100, 28});

    batch_operation_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-operation"));
    (*batch_operation_)
        .set_items({tr("Convert to ICO"), tr("Normalize sizes"), tr("Icon state variants"),
                    tr("Export representations"), tr("Stamp"), tr("Shared palette")});
    (*batch_operation_).set_selected_index(0);
    put(batch_operation_, {260, 48, 480, 28});
    const std::size_t normal_begin = controls_.size();
    batch_policy_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-policy"));
    (*batch_policy_)
        .set_items({tr("Add missing sizes; keep existing images"),
                    tr("Replace chosen sizes; keep other images"),
                    tr("Exact profile; remove unchecked sizes")});
    (*batch_policy_).set_selected_index(0);
    (*batch_policy_).set_accessible_name(tr("Icon sizes"));
    put(batch_policy_, {260, 84, 480, 28});
    for (std::size_t index = 0; index < std::size(batch_sizes); ++index) {
        const std::shared_ptr<gf::CheckBox> size =
            gf::make_control<gf::CheckBox>(gf::StableId("batch-size-" + std::to_string(batch_sizes[index])),
                                           std::to_string(batch_sizes[index]));
        (*size).set_accessible_name(std::to_string(batch_sizes[index]) + " px");
        (*size).set_checked(batch_sizes[index] != 96);
        (*size).set_font({gf::FontRole::control, 12, 400, false});
        put(size, {260 + index * 60.0, 120, 58, 28});
        subscriptions_.push_back((*size).checked_changed().subscribe(
            *this, gf::Delegate<bool>::bind<EditorDialog, &EditorDialog::batch_checked>(*this)));
        batch_sizes_.push_back(size);
        batch_settings_.push_back(size);
    }
    batch_preset_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-preset"));
    (*batch_preset_).set_items({tr("Normal"), tr("Hot"), tr("Disabled")});
    (*batch_preset_).set_selected_index(2);
    (*batch_preset_).set_accessible_name(tr("Preset"));
    put(batch_preset_, {260, 156, 220, 28});
    label("batch-saturation-label", tr("Saturation %"), {260, 188, 150, 24});
    label("batch-brightness-label", tr("Brightness %"), {420, 188, 150, 24});
    label("batch-opacity-label", tr("Opacity %"), {580, 188, 150, 24});
    batch_saturation_ = gf::make_control<gf::NumericUpDown>(gf::StableId("batch-saturation"));
    batch_brightness_ = gf::make_control<gf::NumericUpDown>(gf::StableId("batch-brightness"));
    batch_opacity_ = gf::make_control<gf::NumericUpDown>(gf::StableId("batch-opacity"));
    (*batch_saturation_).set_range(0, 200);
    (*batch_brightness_).set_range(-100, 100);
    (*batch_opacity_).set_range(0, 100);
    (*batch_opacity_).set_value(55);
    (*batch_saturation_).set_accessible_name(tr("Saturation %"));
    (*batch_brightness_).set_accessible_name(tr("Brightness %"));
    (*batch_opacity_).set_accessible_name(tr("Opacity %"));
    (*batch_operation_).set_accessible_name(tr("Operation"));
    put(batch_saturation_, {260, 212, 145, 28});
    put(batch_brightness_, {420, 212, 145, 28});
    put(batch_opacity_, {580, 212, 160, 28});
    batch_normal_controls_.assign(controls_.begin() + normal_begin, controls_.end());
    label("batch-destination-label", tr("Destination folder"), {20, 246, 140, 28});
    batch_destination_ = gf::make_control<gf::TextBox>(gf::StableId("batch-destination"));
    (*batch_destination_).set_accessible_name(tr("Destination folder"));
    put(batch_destination_, {165, 246, 375, 28});
    const std::shared_ptr<gf::Button> folder =
        button("batch-folder", tr("Choose folder…"), {550, 246, 190, 28});
    label("batch-collision-label", tr("Output"), {20, 282, 140, 28});
    batch_collision_ = gf::make_control<gf::ComboBox>(gf::StableId("batch-collision"));
    (*batch_collision_).set_items({tr("New output folder"), tr("Skip existing"), tr("Rename outputs"), tr("Replace existing")});
    (*batch_collision_).set_selected_index(0);
    (*batch_collision_).set_accessible_name(tr("Output"));
    put(batch_collision_, {165, 282, 280, 28});
    const std::shared_ptr<gf::Button> review =
        button("batch-review", tr("Review outputs"), {550, 282, 190, 28});
    batch_manifest_ = gf::make_control<gf::TextBox>(gf::StableId("batch-manifest"));
    (*batch_manifest_).set_multiline(true);
    (*batch_manifest_).set_read_only(true);
    (*batch_manifest_).set_word_wrap(false);
    (*batch_manifest_).set_auto_scroll(true);
    (*batch_manifest_).set_accessible_name(tr("Output manifest"));
    put(batch_manifest_, {20, 318, 720, 80});
    batch_status_ = gf::make_control<gf::Label>(gf::StableId("batch-status"));
    put(batch_status_, {20, 402, 720, 24});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    (*error_).set_text_wrapping(gf::TextWrapping::word);
    put(error_, {20, 428, 720, 26});
    batch_report_ = button("batch-report", tr("Save report…"), {20, 466, 160, 28});
    batch_run_ = button("batch-run", tr("Run"), {350, 466, 120, 28});
    batch_cancel_ = button("batch-cancel", tr("Cancel"), {485, 466, 120, 28});
    button("dialog-cancel", tr("Close"), {620, 466, 120, 28});
    (*batch_cancel_).set_enabled(false);
    batch_settings_.insert(batch_settings_.end(),
                           {batch_sources_, add, remove, batch_operation_, batch_policy_, batch_preset_,
                            batch_saturation_, batch_brightness_, batch_opacity_, batch_destination_, folder,
                            batch_collision_, review});
    for (const std::shared_ptr<gf::Button>& control :
         {add, remove, folder, review, batch_report_, batch_run_, batch_cancel_}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::batch_clicked>(*this)));
    }
    for (const std::shared_ptr<gf::ComboBox>& control : {batch_operation_, batch_policy_, batch_collision_}) {
        subscriptions_.push_back((*control).selected_index_changed().subscribe(
            *this,
            gf::Delegate<std::optional<std::size_t>>::bind<EditorDialog, &EditorDialog::batch_choice_changed>(
                *this)));
    }
    subscriptions_.push_back(
        (*batch_preset_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::batch_preset_changed>(*this)));
    for (const std::shared_ptr<gf::NumericUpDown>& control :
         {batch_saturation_, batch_brightness_, batch_opacity_}) {
        subscriptions_.push_back((*control).value_changed().subscribe(
            *this, gf::Delegate<double>::bind<EditorDialog, &EditorDialog::batch_number_changed>(*this)));
    }
    subscriptions_.push_back(
        (*batch_destination_)
            .text_changed()
            .subscribe(
                *this,
                gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::batch_text_changed>(
                    *this)));
    initialize_batch_stamp();
    initialize_batch_palette();
    refresh_batch_sources();
    invalidate_batch();
}
void EditorDialog::refresh_batch_sources() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        std::vector<std::string> labels;
        for (std::size_t index = 0; index < (*editor).batch_sources.size(); ++index) {
            const std::string& path = (*editor).batch_sources[index];
            labels.push_back("#" + std::to_string(index + 1) + " " +
                             path_to_utf8(path_from_utf8(path).filename()) + " — " + path);
        }
        (*batch_sources_).set_items(labels);
        if ((*batch_destination_).text().empty() && !(*editor).batch_sources.empty()) {
            (*batch_destination_).set_text(path_to_utf8(
                std::filesystem::absolute(path_from_utf8((*editor).batch_sources.front())).parent_path()));
        }
    }
}
void EditorDialog::stop_icon_batch() {
    if (batch_job_) {
        (*batch_job_).cancel.store(true);
    }
    if (batch_worker_.joinable()) {
        batch_worker_.join();
    }
    batch_job_.reset();
}
void EditorDialog::invalidate_batch() {
    if (synchronizing_) {
        return;
    }
    stop_icon_batch();
    (*batch_run_).set_enabled(false);
    (*batch_report_).set_enabled(false);
    (*batch_manifest_).set_text("");
    (*error_).set_text("");
    (*batch_status_).set_text(tr("Review outputs"));
    (*batch_stamp_before_).clear_bitmap();
    (*batch_stamp_after_).clear_bitmap();
    update_batch_controls();
}
void EditorDialog::update_batch_controls() {
    const std::size_t operation = (*batch_operation_).selected_index().value_or(0);
    const bool stamping = operation == 4;
    for (const std::shared_ptr<gf::Control>& control : batch_normal_controls_) {
        (*control).set_visible(!stamping && operation != 5);
    }
    for (const std::shared_ptr<gf::Control>& control : batch_stamp_controls_) {
        (*control).set_visible(stamping);
    }
    for (const std::shared_ptr<gf::Control>& control : batch_palette_controls_) {
        (*control).set_visible(operation == 5);
    }
    (*batch_sources_).set_requested_bounds({20, 48, 220, stamping ? 56.0 : 140.0});
    (*batch_stamp_matte_).set_enabled(stamping && (*batch_stamp_flatten_).checked());
    (*batch_policy_).set_enabled(operation == 1);
    for (const std::shared_ptr<gf::CheckBox>& size : batch_sizes_) {
        (*size).set_enabled(operation < 2);
    }
    (*batch_preset_).set_enabled(operation == 2);
    (*batch_saturation_).set_enabled(operation == 2);
    (*batch_brightness_).set_enabled(operation == 2);
    (*batch_opacity_).set_enabled(operation == 2);
}
void EditorDialog::batch_choice_changed(std::optional<std::size_t>) {
    invalidate_batch();
}
void EditorDialog::batch_number_changed(double) {
    invalidate_batch();
}
void EditorDialog::batch_text_changed(const std::string&) {
    invalidate_batch();
}
void EditorDialog::batch_checked(bool) {
    invalidate_batch();
}
void EditorDialog::batch_preset_changed(std::optional<std::size_t> index) {
    const IconStateRecipe recipe = icon_state_preset(static_cast<IconStatePreset>(index.value_or(2)));
    synchronizing_ = true;
    (*batch_saturation_).set_value(recipe.saturation);
    (*batch_brightness_).set_value(recipe.brightness);
    (*batch_opacity_).set_value(recipe.opacity);
    synchronizing_ = false;
    invalidate_batch();
}
void EditorDialog::start_icon_batch(bool execute) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    IconBatchPlan approved;
    if (execute) {
        if (!batch_job_ || !(*batch_job_).done.load() || !icon_batch_ready((*batch_job_).plan)) {
            return;
        }
        approved = (*batch_job_).plan;
    }
    stop_icon_batch();
    batch_job_ = std::make_shared<IconBatchJob>();
    IconBatchJob& job = *batch_job_;
    job.plan = std::move(approved);
    job.execute = execute;
    job.sources = (*editor).batch_sources;
    job.directory = (*batch_destination_).text();
    job.settings.operation =
        static_cast<IconBatchOperation>((*batch_operation_).selected_index().value_or(0));
    job.settings.sizes_policy = static_cast<IconSizePolicy>((*batch_policy_).selected_index().value_or(0));
    const std::size_t output_mode = (*batch_collision_).selected_index().value_or(0);
    job.settings.new_directory = output_mode == 0;
    job.settings.collision = static_cast<IconExportCollision>(output_mode == 0 ? 0 : output_mode - 1);
    job.settings.sizes.clear();
    for (std::size_t index = 0; index < batch_sizes_.size(); ++index) {
        if ((*batch_sizes_[index]).checked()) {
            job.settings.sizes.push_back(batch_sizes[index]);
        }
    }
    job.settings.recipe.preset = static_cast<IconStatePreset>((*batch_preset_).selected_index().value_or(2));
    job.settings.recipe.saturation = (*batch_saturation_).value();
    job.settings.recipe.brightness = (*batch_brightness_).value();
    job.settings.recipe.opacity = (*batch_opacity_).value();
    if (job.settings.operation == IconBatchOperation::Palette) {
        read_batch_palette(job.settings);
    }
    if (job.settings.operation == IconBatchOperation::Stamp) {
        read_batch_stamp(job.settings);
    }
    for (const std::shared_ptr<gf::Control>& control : batch_settings_) {
        (*control).set_enabled(false);
    }
    (*batch_run_).set_enabled(false);
    (*batch_report_).set_enabled(false);
    (*batch_cancel_).set_enabled(true);
    (*error_).set_text("");
    (*batch_status_).set_text(execute ? tr("Running") : tr("Preparing preview…"));
    batch_worker_ =
        std::thread(BatchWork{batch_job_, this, std::static_pointer_cast<EditorDialog>(shared_from_this())});
}
void EditorDialog::deliver_icon_batch(const std::shared_ptr<IconBatchJob>& job) {
    if (!attached_window() || job != batch_job_) {
        return;
    }
    const bool done = (*job).done.load(std::memory_order_acquire);
    if (done && batch_worker_.joinable()) {
        batch_worker_.join();
    }
    IconBatchPlan published;
    std::string error;
    {
        std::lock_guard<std::mutex> lock((*job).mutex);
        published = (*job).published;
        error = (*job).error;
    }
    std::ostringstream text;
    if (!published.directory.empty()) {
        text << tr(published.create_directory ? "New output folder" : "Destination folder") << ": "
             << path_to_utf8(published.directory) << '\n';
    }
    int completed = 0, failed = 0, skipped = 0, cancelled = 0;
    for (const IconBatchEntry& entry : published.entries) {
        text << '#' << entry.source + 1 << ' '
             << path_to_utf8(path_from_utf8(published.sources[entry.source].path).filename()) << " → "
             << entry.filename << " · " << tr(icon_export_state_name(entry.state));
        if (entry.replaces_existing) {
            text << " · " << tr("Replace existing");
        }
        text << "\n  " << entry.summary;
        if (!entry.diagnostic.empty()) {
            text << " · " << entry.diagnostic;
        }
        text << '\n';
        completed += entry.state == IconExportState::Completed;
        failed += entry.state == IconExportState::Failed;
        skipped += entry.state == IconExportState::Skipped;
        cancelled += entry.state == IconExportState::Cancelled;
    }
    (*batch_manifest_).set_text(text.str());
    (*batch_manifest_).select(gf::Utf8Offset{0}, gf::Utf8Offset{0});
    (*batch_status_)
        .set_text(std::to_string(completed) + " " + tr("Completed") + " · " + std::to_string(failed) + " " +
                  tr("Failed") + " · " + std::to_string(skipped) + " " + tr("Skipped") + " · " +
                  std::to_string(cancelled) + " " + tr("Cancelled"));
    (*error_).set_text(error);
    if (done) {
        show_batch_stamp(published);
        for (const std::shared_ptr<gf::Control>& control : batch_settings_) {
            (*control).set_enabled(true);
        }
        update_batch_controls();
    }
    (*batch_cancel_).set_enabled(!done);
    (*batch_run_).set_enabled(done && !(*job).execute && error.empty() && icon_batch_ready(published));
    (*batch_report_).set_enabled(done && !published.entries.empty());
}
void EditorDialog::batch_clicked(gf::ButtonBase& control) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    try {
        const std::string id(control.stable_id().value());
        if (id == "batch-add") {
            const std::vector<std::string> files = (*editor).choose_batch_sources();
            if ((*editor).batch_sources.size() + files.size() > 256) {
                throw std::runtime_error("Choose at most 256 source files.");
            }
            (*editor).batch_sources.insert((*editor).batch_sources.end(), files.begin(), files.end());
            refresh_batch_sources();
            invalidate_batch();
        } else if (id == "batch-palette-browse") {
            const std::string path = (*editor).choose_palette_file();
            if (!path.empty()) {
                (*batch_palette_path_).set_text(path);
            }
        } else if (id == "batch-stamp-browse") {
            const std::vector<std::string> files = (*editor).choose_batch_sources(false);
            if (!files.empty()) {
                (*batch_stamp_path_).set_text(files.front());
            }
        } else if (id == "batch-remove") {
            const std::optional<std::size_t> selected = (*batch_sources_).selected_index();
            if (selected && *selected < (*editor).batch_sources.size()) {
                (*editor).batch_sources.erase((*editor).batch_sources.begin() + *selected);
                refresh_batch_sources();
                invalidate_batch();
            }
        } else if (id == "batch-folder") {
            const std::string folder =
                (*editor).choose_export_directory(std::string((*batch_destination_).text()));
            if (!folder.empty()) {
                (*batch_destination_).set_text(folder);
            }
        } else if (id == "batch-review" || id == "batch-run") {
            start_icon_batch(id == "batch-run");
        } else if (id == "batch-cancel" && batch_job_) {
            (*batch_job_).cancel.store(true);
        } else if (id == "batch-report" && batch_job_ && (*batch_job_).done.load()) {
            static_cast<void>((*editor).save_export_report(icon_batch_report((*batch_job_).plan),
                                                           "icon-batch-report.json"));
        }
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
    }
}
} // namespace paint::forms

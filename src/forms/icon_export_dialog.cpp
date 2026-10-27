#include "forms/editor.hpp"
#include "localization.hpp"
#include "paths.hpp"
#include <mutex>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
struct IconExportJob {
    std::shared_ptr<const ImageContainer> source;
    std::vector<int> selection;
    std::string directory, prefix, error;
    IconExportCollision collision = IconExportCollision::Skip;
    IconExportPlan plan, published;
    std::mutex mutex;
    std::atomic<bool> cancel{false}, done{false};
    bool execute = false;
};
namespace {
struct IconExportDelivery {
    std::weak_ptr<EditorDialog> owner;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_icon_export();
        }
    }
};
struct IconExportWork {
    std::shared_ptr<IconExportJob> job;
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    static void progress(const IconExportPlan& plan, void* context) {
        IconExportWork& work = *static_cast<IconExportWork*>(context);
        {
            std::lock_guard<std::mutex> lock((*work.job).mutex);
            (*work.job).published = plan;
        }
        static_cast<void>((*work.dispatcher).begin_invoke(IconExportDelivery{work.owner}));
    }
    void operator()() {
        try {
            if ((*job).execute) {
                run_icon_export((*job).plan, (*job).cancel, &IconExportWork::progress, this);
            } else {
                (*job).plan = plan_icon_export((*job).source, (*job).selection, (*job).directory,
                                               (*job).prefix, (*job).collision);
                if ((*job).cancel.load()) {
                    for (IconExportEntry& entry : (*job).plan.entries) {
                        if (entry.state == IconExportState::Planned) {
                            entry.state = IconExportState::Cancelled;
                        }
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock((*job).mutex);
                (*job).published = (*job).plan;
            }
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock((*job).mutex);
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        static_cast<void>((*dispatcher).begin_invoke(IconExportDelivery{owner}));
    }
};
} // namespace
void EditorDialog::initialize_icon_export() {
    panel_ = {0, 0, 780, 510};
    const std::shared_ptr<Editor> editor = editor_.lock();
    const Document& document = (*editor).document;
    export_source_ = std::make_shared<const ImageContainer>(
        document.output_container(document.atlas.kind == AtlasKind::Cursor));
    export_selected_ = document.atlas.sequence;
    export_current_ = document.atlas.active;
    label("export-scope-label", tr("Images"), {20, 52, 135, 28});
    export_scope_ = gf::make_control<gf::ComboBox>(gf::StableId("export-scope"));
    for (const std::string& name :
         {tr("All representations"), tr("Current image"), tr("Selected sequence")}) {
        (*export_scope_).add_item(name);
    }
    (*export_scope_).set_selected_index(0);
    put(export_scope_, {165, 52, 300, 28});
    label("export-prefix-label", tr("Filename prefix"), {20, 94, 135, 28});
    export_prefix_ = gf::make_control<gf::TextBox>(gf::StableId("export-prefix"));
    (*export_prefix_).set_text(icon_export_stem(document.filename));
    put(export_prefix_, {165, 94, 380, 28});
    export_folder_ = button("export-folder", tr("Choose folder…"), {560, 94, 200, 28});
    label("export-destination-label", tr("Destination folder"), {20, 136, 145, 28});
    export_destination_ = gf::make_control<gf::TextBox>(gf::StableId("export-destination"));
    put(export_destination_, {175, 136, 585, 28});
    label("export-collision-label", tr("Existing files"), {20, 180, 135, 28});
    export_collision_ = gf::make_control<gf::ComboBox>(gf::StableId("export-collision"));
    for (const std::string& name : {tr("Skip existing"), tr("Rename outputs"), tr("Replace existing")}) {
        (*export_collision_).add_item(name);
    }
    (*export_collision_).set_selected_index(0);
    put(export_collision_, {165, 178, 300, 28});
    export_review_ = button("export-review", tr("Review outputs"), {560, 178, 200, 28});
    label("export-hint", tr("RGBA PNG at original dimensions. Cursor hotspots are not included."),
          {20, 218, 740, 28});
    export_manifest_ = gf::make_control<gf::TextBox>(gf::StableId("export-manifest"));
    (*export_manifest_).set_accessible_name(tr("Output manifest"));
    (*export_manifest_).set_multiline(true);
    (*export_manifest_).set_auto_scroll(true);
    (*export_manifest_).set_read_only(true);
    (*export_manifest_).set_word_wrap(false);
    put(export_manifest_, {20, 254, 740, 126});
    export_status_ = gf::make_control<gf::Label>(gf::StableId("export-status"));
    put(export_status_, {20, 386, 740, 24});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    (*error_).set_text_wrapping(gf::TextWrapping::word);
    put(error_, {20, 414, 740, 44});
    const std::shared_ptr<gf::Button> strip = button("export-strip", tr("Toolbar strip"), {185,474,145,28});
    export_report_ = button("export-report", tr("Save report…"), {20, 474, 150, 28});
    export_run_ = button("export-run", tr("Export PNG…"), {345, 474, 150, 28});
    export_cancel_ = button("export-cancel", tr("Cancel"), {510, 474, 110, 28});
    button("dialog-cancel", tr("Close"), {635, 474, 125, 28});
    (*export_run_).set_enabled(false);
    (*export_cancel_).set_enabled(false);
    (*export_report_).set_enabled(false);
    for (const std::shared_ptr<gf::Button>& control :
         {export_folder_, export_review_, export_run_, export_cancel_, export_report_, strip}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this,
            gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::icon_export_clicked>(*this)));
    }
    for (const std::shared_ptr<gf::ComboBox>& control : {export_scope_, export_collision_}) {
        subscriptions_.push_back((*control).selected_index_changed().subscribe(
            *this, gf::Delegate<std::optional<std::size_t>>::bind<
                       EditorDialog, &EditorDialog::icon_export_choice_changed>(*this)));
    }
    subscriptions_.push_back(
        (*export_prefix_)
            .text_changed()
            .subscribe(
                *this,
                gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::icon_export_text_changed>(
                    *this)));
    subscriptions_.push_back(
        (*export_destination_)
            .text_changed()
            .subscribe(*this, gf::Delegate<const std::string&>::bind<
                                  EditorDialog, &EditorDialog::icon_export_destination_changed>(*this)));
    (*export_status_).set_text(tr("Choose a folder to review outputs."));
}
void EditorDialog::stop_icon_export() {
    if (export_job_) {
        (*export_job_).cancel.store(true);
    }
    if (export_worker_.joinable()) {
        export_worker_.join();
    }
    export_job_.reset();
}
void EditorDialog::icon_export_choice_changed(std::optional<std::size_t>) {
    review_icon_export();
}
void EditorDialog::icon_export_text_changed(const std::string&) {
    review_icon_export();
}
void EditorDialog::icon_export_destination_changed(const std::string& text) {
    export_directory_ = text;
    review_icon_export();
}
void EditorDialog::review_icon_export() {
    start_icon_export(false);
}
void EditorDialog::start_icon_export(bool execute) {
    IconExportPlan approved;
    if (execute) {
        if (!export_job_ || !(*export_job_).done.load(std::memory_order_acquire)) {
            return;
        }
        approved = (*export_job_).plan;
        if (!icon_export_ready(approved)) {
            return;
        }
    }
    stop_icon_export();
    (*export_cancel_).set_enabled(false);
    (*export_run_).set_enabled(false);
    (*export_report_).set_enabled(false);
    (*error_).set_text("");
    (*export_status_).set_text("");
    if (!execute) {
        (*export_manifest_).set_text("");
    }
    if (export_directory_.empty()) {
        return;
    }
    export_job_ = std::make_shared<IconExportJob>();
    IconExportJob& job = *export_job_;
    job.execute = execute;
    job.plan = std::move(approved);
    job.source = export_source_;
    job.directory = export_directory_;
    job.prefix = (*export_prefix_).text();
    job.collision = static_cast<IconExportCollision>((*export_collision_).selected_index().value_or(0));
    const std::size_t scope = (*export_scope_).selected_index().value_or(0);
    if (scope == 0) {
        for (std::size_t index = 0; index < (*export_source_).frames.size(); ++index) {
            job.selection.push_back(static_cast<int>(index));
        }
    } else if (scope == 1) {
        job.selection.push_back(export_current_);
    } else {
        job.selection = export_selected_;
    }
    const std::vector<std::shared_ptr<gf::Control>> settings = {export_scope_,  export_collision_,
                                                                export_prefix_, export_destination_,
                                                                export_folder_, export_review_};
    for (const std::shared_ptr<gf::Control>& control : settings) {
        (*control).set_enabled(!execute);
    }
    if (attached_window()) {
        const gf::Control::Ptr strip = (*attached_window()).find("export-strip");
        if (strip) { (*strip).set_enabled(!execute); }
    }
    (*export_cancel_).set_enabled(true);
    (*export_status_).set_text(execute ? tr("Exporting…") : tr("Preparing preview…"));
    export_worker_ = std::thread(
        IconExportWork{export_job_, this, std::static_pointer_cast<EditorDialog>(shared_from_this())});
}
void EditorDialog::deliver_icon_export() {
    if (!attached_window() || !export_job_) {
        return;
    }
    const bool done = (*export_job_).done.load(std::memory_order_acquire);
    if (done && export_worker_.joinable()) {
        export_worker_.join();
    }
    IconExportPlan published;
    std::string error;
    {
        std::lock_guard<std::mutex> lock((*export_job_).mutex);
        published = (*export_job_).published;
        error = (*export_job_).error;
    }
    std::ostringstream manifest;
    int completed = 0, skipped = 0, failed = 0, cancelled = 0;
    for (const IconExportEntry& entry : published.entries) {
        completed += entry.state == IconExportState::Completed;
        skipped += entry.state == IconExportState::Skipped;
        failed += entry.state == IconExportState::Failed;
        cancelled += entry.state == IconExportState::Cancelled;
        manifest << '#' << entry.source_index + 1 << "  " << entry.width << " × " << entry.height << "  →  "
                 << entry.filename << "\n    " << tr(icon_export_state_name(entry.state));
        if (entry.replaces_existing) {
            manifest << " · " << tr("Replace existing");
        }
        if (!entry.diagnostic.empty()) {
            manifest << " · " << entry.diagnostic;
        }
        manifest << "\n";
    }
    (*export_manifest_).set_text(manifest.str());
    (*export_manifest_).select(gf::Utf8Offset{0}, gf::Utf8Offset{0});
    (*error_).set_text(error);
    const bool running = (*export_job_).execute && !done;
    (*export_status_)
        .set_text((running ? tr("Exporting…") + " · " : "") + std::to_string(completed) + " " +
                  tr("Completed") + " · " + std::to_string(skipped) + " " + tr("Skipped") + " · " +
                  std::to_string(failed) + " " + tr("Failed") + " · " + std::to_string(cancelled) + " " +
                  tr("Cancelled"));
    (*export_run_)
        .set_enabled(done && error.empty() && icon_export_ready(published) && !(*export_job_).execute);
    (*export_report_).set_enabled(done && !published.entries.empty());
    (*export_cancel_).set_enabled(!done);
    if (done) {
        const gf::Control::Ptr strip = (*attached_window()).find("export-strip");
        if (strip) { (*strip).set_enabled(true); }
        (*export_scope_).set_enabled(true);
        (*export_collision_).set_enabled(true);
        (*export_prefix_).set_enabled(true);
        (*export_destination_).set_enabled(true);
        (*export_folder_).set_enabled(true);
        (*export_review_).set_enabled(true);
    }
}
void EditorDialog::icon_export_clicked(gf::ButtonBase& control) {
    const std::shared_ptr<EditorDialog> keep_alive = std::static_pointer_cast<EditorDialog>(shared_from_this());
    const std::string id(control.stable_id().value());
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    try {
        if (id == "export-strip") {
            if (export_job_ && (*export_job_).execute && !(*export_job_).done.load(std::memory_order_acquire)) { return; }
            stop_icon_export();
            (*editor).close_editor_dialog();
            (*editor).execute("atlas-strip");
        } else if (id == "export-folder") {
            const std::string folder = (*editor).choose_export_directory(export_directory_);
            if (!folder.empty()) {
                (*export_destination_).set_text(folder);
            }
        } else if (id == "export-review") {
            review_icon_export();
        } else if (id == "export-run") {
            start_icon_export(true);
        } else if (id == "export-cancel" && export_job_) {
            (*export_job_).cancel.store(true);
        } else if (id == "export-report" && export_job_ &&
                   (*export_job_).done.load(std::memory_order_acquire)) {
            static_cast<void>((*editor).save_export_report(icon_export_report((*export_job_).plan)));
        }
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
    }
}
} // namespace paint::forms

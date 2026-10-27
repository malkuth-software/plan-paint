#include "forms/editor.hpp"
#include "localization.hpp"
namespace paint::forms {
namespace gf = gui_forms;
struct AssetRevealJob {
    std::string path, error;
    RevealHandler handler;
    RevealOutcome result = RevealOutcome::Cancelled;
    std::uint64_t generation = 0;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct RevealDelivery {
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<AssetRevealJob> job;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_asset_reveal(job);
        }
    }
};
struct RevealWork {
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<AssetRevealJob> job;
    void operator()() const {
        try {
            if (!(*job).cancel.load()) {
                const RevealTarget target = prepare_file_reveal((*job).path);
                (*job).result = (*job).handler(target.path, &(*job).cancel);
            }
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(RevealDelivery{owner, job}));
        }
    }
};
} // namespace
void EditorDialog::stop_asset_reveal() {
    if (asset_reveal_job_) {
        (*asset_reveal_job_).cancel.store(true);
    }
    if (asset_reveal_thread_.joinable()) {
        asset_reveal_thread_.join();
    }
    asset_reveal_job_.reset();
}
void EditorDialog::start_asset_reveal(const std::string& path) {
    if (asset_reveal_job_ && !(*asset_reveal_job_).done.load(std::memory_order_acquire)) {
        return;
    }
    stop_asset_reveal();
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    asset_reveal_job_ = std::make_shared<AssetRevealJob>();
    (*asset_reveal_job_).path = path;
    (*asset_reveal_job_).handler = (*editor).reveal_asset;
    (*asset_reveal_job_).generation = asset_generation_;
    (*asset_reveal_).set_enabled(false);
    (*asset_status_).set_text(tr("Revealing in file manager…"));
    try {
        asset_reveal_thread_ = std::thread(
            RevealWork{this, std::static_pointer_cast<EditorDialog>(shared_from_this()), asset_reveal_job_});
    } catch (...) {
        asset_reveal_job_.reset();
        (*asset_reveal_).set_enabled(true);
        throw;
    }
}
void EditorDialog::deliver_asset_reveal(std::shared_ptr<AssetRevealJob> job) {
    if (job != asset_reveal_job_ || (*job).cancel.load() || !attached_window()) {
        return;
    }
    if (asset_reveal_thread_.joinable()) {
        asset_reveal_thread_.join();
    }
    asset_reveal_job_.reset();
    const std::optional<std::size_t> selected = (*asset_list_).selected_index();
    (*asset_reveal_)
        .set_enabled((*asset_list_).enabled() && selected && *selected < asset_listing_.entries.size());
    if ((*job).generation != asset_generation_) {
        return;
    }
    if (!(*job).error.empty()) {
        (*error_).set_text((*job).error);
        (*asset_status_).set_text(tr("Could not reveal the selected asset."));
    } else if ((*job).result != RevealOutcome::Cancelled) {
        (*asset_status_)
            .set_text((*job).result == RevealOutcome::FolderRequested
                          ? tr("Requested containing folder; file selection is unavailable.")
                          : tr("Requested file selection in the file manager."));
    }
}
} // namespace paint::forms

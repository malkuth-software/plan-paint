#include "localization.hpp"
#include "forms/editor.hpp"
#include "paths.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
void Editor::settle_recovery() {
    if (recovery_job.valid()) {
        recovery_job.get();
    }
}
void Editor::reset_recovery(bool discard, const std::string& opened_path) {
    try {
        settle_recovery();
    } catch (const std::exception& exception) {
        recovery_notice = exception.what();
    }
    if (discard && recovery_session) {
        (*recovery_session).discard();
    }
    recovery_session.reset();
    recovery_metadata = {};
    recovery_metadata.created_ms = recovery_time_ms();
    recovery_metadata.opened_path = opened_path;
    recovery_capture_revision = 0;
    recovery_failed = false;
    recovery_deadline = std::chrono::steady_clock::now();
}
void Editor::start_recovery(const std::string& initial_path) {
    try {
        if (recovery_root.empty()) {
            recovery_root = path_from_utf8(preference_directory()) / tr("Recovery");
        }
        reset_recovery(false, initial_path);
        // Ready can precede the native application's final launch notification.
        // Defer modal UI until a timer tick in the running event loop.
        recovery_startup_pending = initial_path.empty();
        recovery_timer = std::make_unique<gf::Timer>(*attached_window(), std::chrono::seconds(1));
        recovery_subscription =
            (*recovery_timer)
                .tick()
                .subscribe(*this, gf::Delegate<>::bind<Editor, &Editor::recovery_tick>(*this));
        (*recovery_timer).start();
    } catch (const std::exception& exception) {
        recovery_notice = exception.what();
        error(tr("Automatic recovery is unavailable: ") + recovery_notice);
    }
}
void Editor::recovery_tick() {
    if (recovery_startup_pending) {
        recovery_startup_pending = false;
        try {
            recover_document();
        } catch (const std::exception& exception) {
            recovery_notice = exception.what();
            error(recovery_notice);
        }
        return;
    }
    if (recovery_job.valid()) {
        if (recovery_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        try {
            recovery_job.get();
            recovery_failed = false;
            recovery_notice.clear();
        } catch (const std::exception& exception) {
            recovery_notice = exception.what();
            recovery_capture_revision = 0;
            if (!recovery_failed) {
                recovery_failed = true;
                error(tr("Automatic recovery could not save: ") + recovery_notice);
            }
        }
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (!settings.recovery_enabled || recovery_root.empty() || now < recovery_deadline ||
        (!document.dirty() && !(text.active && !text.edit.content.empty())) ||
        recovery_capture_revision == canvas_revision || dragging_ || background_busy() ||
        canvas().has_pointer_capture() || editor_dialog_) {
        return;
    }
    // Snapshot only at an event boundary. Never mutate/commit the artist's editing session.
    try {
        if (!recovery_session) {
            recovery_session = std::make_unique<RecoverySession>(recovery_root);
        }
        RecoveryRecord record = recovery_metadata;
        if (!document.filename.empty()) {
            record.source_path = document.filename;
        }
        record.captured_ms = recovery_time_ms();
        record.revision = document.revision;
        if (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor) {
            record.image = document.output_container(document.atlas.kind == AtlasKind::Cursor);
        } else {
            record.image.frames.push_back({document.output_image(), 0, 0, {}});
        }
        if (text.active) {
            Image visible = document.image;
            composite(visible, text.preview, text.bounds.x, text.bounds.y);
            document.constrain_selection(visible, document.image);
            if (document.selection.active) {
                document.selection.composite_onto(visible);
            }
            if (document.atlas.kind == AtlasKind::Sheet && document.atlas.active >= 0) {
                const Rect frame =
                    document.atlas.grid.frame(record.image.frames[0].image, document.atlas.active);
                // Copy the finished frame exactly, including transparent pixels.
                for (int y = 0; y < frame.h; ++y) {
                    for (int x = 0; x < frame.w; ++x) {
                        record.image.frames[0].image.set(frame.x + x, frame.y + y, visible.get(x, y));
                    }
                }
            } else {
                const int frame = record.image.kind == ContainerKind::Image ? 0 : document.atlas.active;
                record.image.frames[std::max(0, frame)].image = std::move(visible);
            }
        }
        recovery_capture_revision = canvas_revision;
        recovery_job = std::async(std::launch::async, &RecoverySession::write, recovery_session.get(),
                                  std::move(record));
    } catch (const std::exception& exception) {
        recovery_capture_revision = 0;
        recovery_notice = exception.what();
        if (!recovery_failed) {
            recovery_failed = true;
            error(tr("Automatic recovery could not save: ") + recovery_notice);
        }
    }
    recovery_deadline = now + std::chrono::seconds(settings.recovery_seconds);
}
void Editor::recover_document() {
    if (recovery_root.empty()) {
        return;
    }
    const std::vector<std::string> ids = RecoverySession::candidates(recovery_root);
    for (const std::string& id : ids) {
        std::unique_ptr<RecoverySession> candidate;
        RecoveryRecord record;
        try {
            candidate = std::make_unique<RecoverySession>(recovery_root, id);
            record = (*candidate).read();
        } catch (const std::exception& exception) {
            recovery_notice = exception.what();
            continue;
        }
        std::time_t captured = static_cast<std::time_t>(record.captured_ms / 1000);
        std::ostringstream timestamp;
        const std::tm* local = std::localtime(&captured);
        if (local) {
            timestamp << std::put_time(local, "%Y-%m-%d %H:%M:%S");
        }
        gf::HostMessageDialogRequest request;
        request.title = tr("Recover unfinished artwork");
        request.message = tr("Recover ") +
                          (record.source_path.empty() ? std::string(tr("Untitled")) : record.source_path) +
                          tr("?\nSnapshot: ") + timestamp.str() +
                          tr("\nYes opens a recovered copy. No keeps this snapshot for later. Cancel stops "
                          "browsing.\nThe original file will not be overwritten.");
        request.buttons = gf::HostMessageButtons::yes_no_cancel;
        request.default_choice = gf::HostDialogChoice::yes;
        const gf::HostMessageDialogResult result =
            std::get<gf::HostMessageDialogResult>(dialog(request).payload);
        if (result.choice == gf::HostDialogChoice::cancel) {
            return;
        }
        if (result.choice != gf::HostDialogChoice::yes) {
            continue;
        }
        if (!can_replace()) {
            return;
        }
        reset_recovery(true);
        finish_controls();
        unset_guide();
        spiro = {};
        atlas_reference = {};
        reference_frame = -1;
        document.replace_container(std::move(record.image), "");
        reset_file_watch();
        document.revision = document.next_revision++;
        document.saved_revision = 0;
        recovery_session = std::move(candidate);
        record.image = {};
        recovery_metadata = std::move(record);
        (*canvas_).set_view(1, {-16, -16});
        refresh();
        if (document.atlas.kind != AtlasKind::None) {
            (*ribbon_).show_atlas();
        }
        return;
    }
    if (!recovery_notice.empty()) {
        error(recovery_notice);
    }
}
} // namespace paint::forms

#include "codecs.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <stdexcept>

namespace paint::forms {
namespace gf = gui_forms;
void Editor::reset_file_watch() {
    watched_path_ = document.filename;
    watched_stamp_ = watched_path_.empty() ? FileStamp{} : file_stamp(watched_path_);
    disk_stamp_ = watched_stamp_;
    ++file_watch_generation_;
    file_notice_.clear();
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Editor::file_watch_tick() {
    // Defer a layout change until a pointer stroke ends.
    if (dragging_ || canvas().has_pointer_capture()) {
        return;
    }
    if (file_check_.valid()) {
        if (file_check_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        const FileStamp current = file_check_.get();
        if (file_check_generation_ == file_watch_generation_ && watched_path_ == document.filename) {
            disk_stamp_ = current;
            const std::string notice = current == watched_stamp_ ? ""
                                       : current.availability == FileAvailability::Missing
                                           ? tr("File missing from disk.")
                                       : current.availability == FileAvailability::Unavailable
                                           ? tr("File unavailable on disk.")
                                           : tr("File changed outside Plan Paint.");
            if (notice != file_notice_) {
                file_notice_ = notice;
                invalidate(gf::Dirty::layout | gf::Dirty::paint);
            }
        }
    }
    if (!watched_path_.empty() && watched_path_ == document.filename) {
        file_check_generation_ = file_watch_generation_;
        file_check_ = std::async(std::launch::async, &file_stamp, watched_path_);
    }
}
bool Editor::confirm_disk_action(const std::string& message) {
    gf::HostMessageDialogRequest request;
    request.title = "Plan Paint";
    request.message = message;
    request.buttons = gf::HostMessageButtons::yes_no;
    request.default_choice = gf::HostDialogChoice::no;
    const gf::HostMessageDialogResult result = std::get<gf::HostMessageDialogResult>(dialog(request).payload);
    return result.choice == gf::HostDialogChoice::yes;
}
void Editor::reload_file() {
    if (editor_dialog_ || dragging_ || canvas().has_pointer_capture() || background_busy()) {
        return;
    }
    const std::string path = document.filename;
    if (path.empty()) {
        return;
    }
    // Do not commit a live text/selection/curve session before the user chooses to replace it.
    if ((document.dirty() || text.active || document.selection.active || warp_active()) &&
        !confirm_disk_action(tr("Reload and discard local edits? Save a copy first to keep them."))) {
        return;
    }
    // Decode and verify before touching any live document state. A missing or corrupt file is harmless.
    open_file(path);
}
void Editor::reload_clicked(gf::ButtonBase&) {
    execute("reload");
}
} // namespace paint::forms

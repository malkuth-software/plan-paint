#include "alpha_tools.hpp"
#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <algorithm>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
void EditorDialog::initialize_alpha_tools() {
    panel_ = {0, 0, 780, 510};
    const std::shared_ptr<Editor> editor = editor_.lock();
    Document& document = (*editor).document;
    document.sync_atlas();
    alpha_source_ = document.image;
    alpha_revision_ = document.revision;
    alpha_epoch_ = document.atlas_epoch;
    alpha_current_ = document.atlas.active;
    label("alpha-scope",
          tr("Current image") + " · " + std::to_string(alpha_source_.width) + " × " +
              std::to_string(alpha_source_.height),
          {20, 48, 400, 28});
    alpha_view_ = gf::make_control<gf::ComboBox>(gf::StableId("alpha-view"));
    (*alpha_view_).set_accessible_name(tr("Preview"));
    (*alpha_view_).set_items({tr("Alpha"), tr("Color")});
    (*alpha_view_).set_selected_index(0);
    put(alpha_view_, {530, 48, 230, 28});
    label("alpha-before-label", tr("Original"), {20, 88, 355, 24});
    label("alpha-after-label", tr("Preview"), {405, 88, 355, 24});
    alpha_before_ = gf::make_control<gf::RasterCanvas>(gf::StableId("alpha-before"));
    alpha_after_ = gf::make_control<gf::RasterCanvas>(gf::StableId("alpha-after"));
    (*alpha_before_).set_accessible_name(tr("Original"));
    (*alpha_after_).set_accessible_name(tr("Preview"));
    put(alpha_before_, {20, 116, 355, 200});
    put(alpha_after_, {405, 116, 355, 200});
    alpha_status_ = gf::make_control<gf::TextBox>(gf::StableId("alpha-status"));
    (*alpha_status_).set_read_only(true);
    (*alpha_status_).set_multiline(true);
    (*alpha_status_).set_auto_scroll(true);
    (*alpha_status_).set_accessible_name(tr("Alpha statistics"));
    put(alpha_status_, {20, 326, 740, 52});
    alpha_resize_ = gf::make_control<gf::CheckBox>(gf::StableId("alpha-resize"), tr("Resize mask to image"));
    put(alpha_resize_, {20, 385, 355, 28});
    label("alpha-contract", tr("Mask RGB mean; mask alpha ignored. RGB is preserved."), {20, 417, 740, 24});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    put(error_, {20, 442, 740, 24});
    const std::shared_ptr<gf::Button> copy = button("alpha-copy", tr("Copy alpha"), {20, 474, 145, 28});
    const std::shared_ptr<gf::Button> paste = button("alpha-paste", tr("Paste alpha"), {175, 474, 145, 28});
    const std::shared_ptr<gf::Button> reset = button("alpha-reset", tr("Reset"), {330, 474, 125, 28});
    alpha_apply_ = button("alpha-apply", tr("Apply"), {550, 474, 100, 28});
    button("dialog-cancel", tr("Close"), {660, 474, 100, 28});
    for (const std::shared_ptr<gf::Button>& control : {copy, paste, reset, alpha_apply_}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::alpha_clicked>(*this)));
    }
    subscriptions_.push_back(
        (*alpha_resize_)
            .clicked()
            .subscribe(*this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::alpha_clicked>(
                                  *this)));
    subscriptions_.push_back(
        (*alpha_view_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::alpha_view_changed>(*this)));
}
void EditorDialog::alpha_view_changed(std::optional<std::size_t>) {
    render_alpha_tools();
}
void EditorDialog::render_alpha_tools() {
    if (!attached_window()) {
        return;
    }
    (*alpha_apply_).set_enabled(false);
    (*error_).set_text("");
    alpha_result_ = {};
    try {
        alpha_result_ = alpha_mask_.pixels.empty()
                            ? alpha_source_
                            : with_alpha_mask(alpha_source_, alpha_mask_, (*alpha_resize_).checked());
    } catch (const std::exception& exception) {
        (*error_).set_text(tr(exception.what()));
    }
    const Image& after = alpha_result_.pixels.empty() ? alpha_source_ : alpha_result_;
    const bool alpha = (*alpha_view_).selected_index().value_or(0) == 0;
    const Image before_view = alpha ? alpha_image(alpha_source_) : alpha_source_;
    const Image after_view = alpha ? alpha_image(after) : after;
    IconFrame before_frame, after_frame;
    before_frame.image = before_view;
    after_frame.image = after_view;
    publish_image(render_asset_preview(before_frame, 355, 200), *alpha_before_);
    publish_image(render_asset_preview(after_frame, 355, 200), *alpha_after_);
    const AlphaStatistics stats = alpha_statistics(after);
    const std::uint64_t partial = after.pixels.size() - stats.histogram[0] - stats.histogram[255];
    std::ostringstream text;
    text << tr("Alpha") << ": " << unsigned(stats.minimum) << "–" << unsigned(stats.maximum)
         << " · 0: " << stats.histogram[0] << " · 1–254: " << partial << " · 255: " << stats.histogram[255]
         << '\n'
         << tr("Current image") << ": " << after.width << " × " << after.height;
    if (!alpha_mask_.pixels.empty()) {
        text << " · " << tr("Mask") << ": " << alpha_mask_.width << " × " << alpha_mask_.height;
    }
    (*alpha_status_).set_text(text.str());
    (*alpha_apply_)
        .set_enabled(!alpha_result_.pixels.empty() &&
                     !std::equal(alpha_result_.pixels.begin(), alpha_result_.pixels.end(),
                                 alpha_source_.pixels.begin(), equal));
}
void EditorDialog::alpha_clicked(gf::ButtonBase& control) {
    const std::shared_ptr<EditorDialog> keep_alive =
        std::static_pointer_cast<EditorDialog>(shared_from_this());
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    const std::string id(control.stable_id().value());
    try {
        if (id == "alpha-copy") {
            (*editor).copy_alpha_image(alpha_source_);
            (*error_).set_text("");
        } else if (id == "alpha-paste") {
            Image mask = (*editor).read_alpha_mask();
            if (mask.pixels.empty()) {
                throw std::runtime_error(tr("The clipboard has no image."));
            }
            alpha_mask_ = std::move(mask);
            render_alpha_tools();
        } else if (id == "alpha-reset") {
            alpha_mask_ = {};
            render_alpha_tools();
        } else if (id == "alpha-resize") {
            render_alpha_tools();
        } else if (id == "alpha-apply" && (*alpha_apply_).enabled()) {
            Document& document = (*editor).document;
            if (document.revision != alpha_revision_ || document.atlas_epoch != alpha_epoch_ ||
                document.atlas.active != alpha_current_ || document.image.width != alpha_source_.width ||
                document.image.height != alpha_source_.height ||
                !std::equal(document.image.pixels.begin(), document.image.pixels.end(),
                            alpha_source_.pixels.begin(), alpha_source_.pixels.end(), equal)) {
                throw std::runtime_error(tr("Image changed. Reopen Alpha tools."));
            }
            document.require_rgba_transform();
            document.checkpoint();
            document.image = std::move(alpha_result_);
            (*editor).close_editor_dialog();
            (*editor).refresh();
        }
    } catch (const std::exception& exception) {
        (*error_).set_text(exception.what());
    }
}
} // namespace paint::forms

#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <algorithm>
#include <gui_forms/controls/scrollable_control/container_control/flow_layout_panel/flow_layout_panel.hpp>
#include <iomanip>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
struct IconSheetJob {
    std::shared_ptr<const ImageContainer> source;
    std::vector<int> selection;
    IconSheetOptions options;
    Image result;
    std::string error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct IconSheetDelivery {
    std::weak_ptr<EditorDialog> owner;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_icon_sheet();
        }
    }
};
struct IconSheetWork {
    std::shared_ptr<IconSheetJob> job;
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    void operator()() const {
        try {
            (*job).result =
                render_icon_sheet(*(*job).source, (*job).selection, (*job).options, &(*job).cancel);
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(IconSheetDelivery{owner}));
        }
    }
};
} // namespace
void EditorDialog::initialize_icon_sheet() {
    panel_ = {0, 0, 780, 510};
    const std::shared_ptr<Editor> editor = editor_.lock();
    const Document& document = (*editor).document;
    sheet_source_ = std::make_shared<const ImageContainer>(
        document.output_container(document.atlas.kind == AtlasKind::Cursor));
    sheet_selected_ = document.atlas.sequence;
    sheet_current_ = document.atlas.active;
    sheet_custom_ = document.ink.primary;
    label("sheet-scope-label", tr("Images"), {20, 52, 115, 28});
    sheet_scope_ = gf::make_control<gf::ComboBox>(gf::StableId("sheet-scope"));
    (*sheet_scope_).set_accessible_name(tr("Images"));
    for (const std::string& text :
         {tr("All representations"), tr("Current image"), tr("Selected sequence")}) {
        (*sheet_scope_).add_item(text);
    }
    (*sheet_scope_).set_selected_index(0);
    put(sheet_scope_, {145, 52, 240, 28});
    label("sheet-background-label", tr("Background"), {405, 52, 115, 28});
    sheet_background_ = gf::make_control<gf::ComboBox>(gf::StableId("sheet-background"));
    (*sheet_background_).set_accessible_name(tr("Background"));
    for (const std::string& text :
         {tr("Light"), tr("Dark"), tr("Checkerboard"), tr("Transparent"), tr("Primary color")}) {
        (*sheet_background_).add_item(text);
    }
    (*sheet_background_).set_selected_index(0);
    put(sheet_background_, {528, 52, 232, 28});
    label("sheet-columns-label", tr("Columns"), {20, 95, 76, 28});
    sheet_columns_ = number("sheet-columns", {98, 94, 64, 28}, 1, 16, 4);
    label("sheet-padding-label", tr("Padding"), {176, 95, 70, 28});
    sheet_padding_ = number("sheet-padding", {246, 94, 64, 28}, 0, 64, 12);
    label("sheet-gap-label", tr("Gap"), {324, 95, 50, 28});
    sheet_gap_ = number("sheet-gap", {378, 94, 64, 28}, 0, 64, 16);
    sheet_labels_ = gf::make_control<gf::CheckBox>(gf::StableId("sheet-labels"), tr("Labels"));
    (*sheet_labels_).set_checked(true);
    put(sheet_labels_, {460, 94, 90, 28});
    label("sheet-font-label", tr("Text size"), {568, 95, 110, 28});
    sheet_font_ = number("sheet-font", {686, 94, 74, 28}, 8, 24, 12);
    label("sheet-zoom-label", tr("Preview"), {20, 137, 115, 28});
    sheet_zoom_ = gf::make_control<gf::ComboBox>(gf::StableId("sheet-zoom"));
    (*sheet_zoom_).set_accessible_name(tr("Preview scale"));
    for (const std::string& text : {tr("Fit sheet"), tr("Actual pixels"), tr("4× pixels")}) {
        (*sheet_zoom_).add_item(text);
    }
    (*sheet_zoom_).set_selected_index(0);
    put(sheet_zoom_, {145, 136, 210, 28});
    sheet_status_ = gf::make_control<gf::Label>(gf::StableId("sheet-status"));
    (*sheet_status_).set_font({gf::FontRole::control, 11, 400, false});
    put(sheet_status_, {375, 136, 385, 32});
    const std::shared_ptr<gf::FlowLayoutPanel> viewport =
        gf::make_control<gf::FlowLayoutPanel>(gf::StableId("sheet-viewport"));
    (*viewport).set_auto_scroll(true);
    (*viewport).set_wrap_contents(false);
    (*viewport).set_padding({0, 0, 0, 0});
    sheet_viewport_ = viewport;
    put(viewport, {20, 176, 740, 210});
    sheet_preview_ = gf::make_control<gf::RasterCanvas>(gf::StableId("sheet-preview"));
    (*sheet_preview_).set_accessible_name(tr("Contact sheet preview"));
    (*sheet_preview_).set_sampling(gf::ImageSampling::nearest);
    (*sheet_preview_).set_requested_bounds({0, 0, 1, 1});
    (*viewport).add_child(sheet_preview_);
    label("sheet-hint", tr("Background and labels are exported. Source images stay unchanged."),
          {20, 392, 740, 24});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    (*error_).set_text_wrapping(gf::TextWrapping::word);
    put(error_, {20, 420, 740, 32});
    sheet_open_ = button("sheet-open", tr("Open as new image"), {20, 469, 220, 28});
    (*sheet_open_).set_enabled(false);
    subscriptions_.push_back((*sheet_open_).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::icon_sheet_clicked>(*this)));
    sheet_export_ = button("sheet-export", tr("Export PNG…"), {510, 469, 135, 28});
    (*sheet_export_).set_enabled(false);
    button("dialog-cancel", tr("Close"), {660, 469, 100, 28});
    subscriptions_.push_back(
        (*sheet_export_)
            .clicked()
            .subscribe(
                *this,
                gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::icon_sheet_clicked>(*this)));
    subscriptions_.push_back(
        (*sheet_labels_)
            .clicked()
            .subscribe(
                *this,
                gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::icon_sheet_clicked>(*this)));
    for (const std::shared_ptr<gf::NumericUpDown>& number :
         {sheet_columns_, sheet_padding_, sheet_gap_, sheet_font_}) {
        subscriptions_.push_back((*number).value_changed().subscribe(
            *this,
            gf::Delegate<double>::bind<EditorDialog, &EditorDialog::icon_sheet_number_changed>(*this)));
    }
    for (const std::shared_ptr<gf::ComboBox>& choice : {sheet_scope_, sheet_background_}) {
        subscriptions_.push_back((*choice).selected_index_changed().subscribe(
            *this,
            gf::Delegate<std::optional<std::size_t>>::bind<EditorDialog,
                                                           &EditorDialog::icon_sheet_choice_changed>(*this)));
    }
    subscriptions_.push_back(
        (*sheet_zoom_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::icon_sheet_zoom_changed>(*this)));
}
void EditorDialog::stop_icon_sheet() {
    if (sheet_job_) {
        (*sheet_job_).cancel.store(true);
    }
    if (sheet_worker_.joinable()) {
        sheet_worker_.join();
    }
    sheet_job_.reset();
}
void EditorDialog::icon_sheet_number_changed(double) {
    render_icon_sheet_preview();
}
void EditorDialog::icon_sheet_choice_changed(std::optional<std::size_t>) {
    render_icon_sheet_preview();
}
void EditorDialog::render_icon_sheet_preview() {
    if (!attached_window()) {
        return;
    }
    stop_icon_sheet();
    (*sheet_export_).set_enabled(false);
    (*sheet_open_).set_enabled(false);
    (*sheet_preview_).clear_bitmap();
    (*sheet_status_).set_text(tr("Preparing preview…"));
    (*error_).set_text("");
    sheet_job_ = std::make_shared<IconSheetJob>();
    IconSheetJob& job = *sheet_job_;
    job.source = sheet_source_;
    const std::size_t scope = (*sheet_scope_).selected_index().value_or(0);
    if (scope == 0) {
        for (std::size_t index = 0; index < (*sheet_source_).frames.size(); ++index) {
            job.selection.push_back(static_cast<int>(index));
        }
    } else if (scope == 1) {
        job.selection.push_back(sheet_current_);
    } else {
        job.selection = sheet_selected_;
    }
    job.options.columns = static_cast<int>((*sheet_columns_).value());
    job.options.padding = static_cast<int>((*sheet_padding_).value());
    job.options.gap = static_cast<int>((*sheet_gap_).value());
    job.options.label_size = static_cast<int>((*sheet_font_).value());
    job.options.labels = (*sheet_labels_).checked();
    job.options.background =
        static_cast<IconSheetBackground>((*sheet_background_).selected_index().value_or(0));
    job.options.custom = sheet_custom_;
    sheet_worker_ = std::thread(
        IconSheetWork{sheet_job_, this, std::static_pointer_cast<EditorDialog>(shared_from_this())});
}
void EditorDialog::deliver_icon_sheet() {
    if (!attached_window() || !sheet_job_ || !(*sheet_job_).done.load(std::memory_order_acquire)) {
        return;
    }
    if (sheet_worker_.joinable()) {
        sheet_worker_.join();
    }
    if (!(*sheet_job_).error.empty()) {
        (*error_).set_text((*sheet_job_).error);
        (*sheet_status_).set_text("");
        return;
    }
    if ((*sheet_job_).result.pixels.empty()) {
        return;
    }
    publish_image((*sheet_job_).result, *sheet_preview_);
    icon_sheet_zoom_changed(std::nullopt);
    (*sheet_export_).set_enabled(true);
    (*sheet_open_).set_enabled(true);
}
void EditorDialog::icon_sheet_zoom_changed(std::optional<std::size_t>) {
    if (!attached_window() || !sheet_job_ || !(*sheet_job_).done.load(std::memory_order_acquire) ||
        (*sheet_job_).result.pixels.empty()) {
        return;
    }
    const Image& image = (*sheet_job_).result;
    const double scale = std::max(0.1, (*attached_window()).scale());
    const std::size_t mode = (*sheet_zoom_).selected_index().value_or(0);
    const double zoom = mode == 1   ? 1 / scale
                        : mode == 2 ? 4 / scale
                                    : std::min({1.0, 724.0 / image.width, 194.0 / image.height});
    (*sheet_preview_).set_zoom(zoom);
    (*sheet_preview_).set_requested_bounds({0, 0, image.width * zoom, image.height * zoom});
    (*sheet_preview_).set_view_origin({0, 0});
    std::ostringstream status;
    status << image.width << " × " << image.height << " px · " << std::fixed << std::setprecision(2)
           << zoom * scale << tr(" display pixels / image pixel");
    (*sheet_status_).set_text(status.str());
}
void EditorDialog::icon_sheet_clicked(gf::ButtonBase& control) {
    if (control.stable_id().value() == "sheet-labels") {
        render_icon_sheet_preview();
        return;
    }
    if (!sheet_job_ || !(*sheet_job_).done.load(std::memory_order_acquire) ||
        (*sheet_job_).result.pixels.empty() || !(*sheet_export_).enabled()) {
        return;
    }
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    try {
        if (control.stable_id().value() == "sheet-open") {
            // Copy before closing this popup cancels and releases its render job.
            Image prepared = (*sheet_job_).result;
            (*editor).open_generated_image(std::move(prepared));
            return;
        }
        if ((*editor).export_png_image((*sheet_job_).result, "icon-contact-sheet.png")) {
            (*error_).set_text(tr("PNG exported."));
        }
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
    }
}
} // namespace paint::forms

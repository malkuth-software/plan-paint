#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <algorithm>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
struct ToolbarStripJob {
    std::shared_ptr<const ImageContainer> source;
    std::vector<int> order;
    ToolbarStripOptions options;
    ToolbarStripPlan plan;
    Image image, proof;
    bool bmp = false;
    std::string error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct StripDelivery {
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<ToolbarStripJob> job;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_toolbar_strip(job);
        }
    }
};
struct StripWork {
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<ToolbarStripJob> job;
    void operator()() const {
        try {
            (*job).plan = plan_toolbar_strip(*(*job).source, (*job).order, (*job).options);
            (*job).image = render_toolbar_strip(*(*job).source, (*job).order, (*job).options, &(*job).cancel);
            IconFrame frame;
            frame.image = (*job).image;
            (*job).proof = render_asset_preview(frame, 740, 120);
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(StripDelivery{owner, job}));
        }
    }
};
} // namespace
void EditorDialog::initialize_toolbar_strip() {
    panel_ = {0, 0, 780, 510};
    const std::shared_ptr<Editor> editor = editor_.lock();
    const Document& document = (*editor).document;
    ImageContainer source;
    const AtlasState& atlas = document.atlas;
    if (atlas.kind == AtlasKind::Icon || atlas.kind == AtlasKind::Cursor) {
        source = document.output_container(atlas.kind == AtlasKind::Cursor);
    } else {
        for (int index = 0; index < atlas.count(); ++index) {
            IconFrame frame;
            frame.image = atlas.frame_image(index);
            source.frames.push_back(std::move(frame));
        }
    }
    strip_source_ = std::make_shared<const ImageContainer>(std::move(source));
    strip_source_path_ = document.filename;
    std::vector<int> order = atlas.sequence;
    if (order.empty()) {
        for (int index = 0; index < std::min(256, atlas.count()); ++index) {
            order.push_back(index);
        }
    }
    std::string ordinals;
    int width = 1, height = 1;
    for (int index : order) {
        if (!ordinals.empty()) {
            ordinals += ", ";
        }
        ordinals += std::to_string(index + 1);
        width = std::max(width, (*strip_source_).frames[index].image.width);
        height = std::max(height, (*strip_source_).frames[index].image.height);
    }
    label("strip-order-label", tr("Image order (comma-separated numbers)"), {20, 46, 740, 24});
    strip_order_ = gf::make_control<gf::TextBox>(gf::StableId("strip-order"));
    (*strip_order_).set_accessible_name(tr("Image order (comma-separated numbers)"));
    (*strip_order_).set_text(ordinals);
    put(strip_order_, {20, 73, 740, 28});
    const char* labels[] = {"Cell width", "Cell height", "Columns", "Horizontal gap", "Vertical gap"};
    const double maximum[] = {4096, 4096, 256, 256, 256};
    const double values[] = {double(width), double(height), double(order.size()), 0, 0};
    for (int index = 0; index < 5; ++index) {
        label("strip-label-" + std::to_string(index), tr(labels[index]), {20 + index * 148.0, 109, 140, 24});
        const std::shared_ptr<gf::NumericUpDown> value =
            number("strip-value-" + std::to_string(index), {20 + index * 148.0, 136, 140, 28},
                   index < 3 ? 1 : 0, maximum[index], values[index]);
        (*value).set_accessible_name(tr(labels[index]));
        strip_values_.push_back(value);
        subscriptions_.push_back((*value).value_changed().subscribe(
            *this, gf::Delegate<double>::bind<EditorDialog, &EditorDialog::strip_number>(*this)));
    }
    strip_format_ = gf::make_control<gf::ComboBox>(gf::StableId("strip-format"));
    (*strip_format_).set_items({"PNG", "BMP"});
    (*strip_format_).set_selected_index(0);
    (*strip_format_).set_accessible_name(tr("Output format"));
    put(strip_format_, {20, 184, 140, 28});
    strip_background_ = gf::make_control<gf::ComboBox>(gf::StableId("strip-background"));
    (*strip_background_).set_items({tr("Transparent"), tr("Matte RGB")});
    (*strip_background_).set_selected_index(0);
    (*strip_background_).set_accessible_name(tr("Background"));
    put(strip_background_, {176, 184, 200, 28});
    strip_matte_ = gf::make_control<gf::TextBox>(gf::StableId("strip-matte"));
    (*strip_matte_).set_text(to_hex(document.ink.secondary));
    (*strip_matte_).set_accessible_name(tr("Matte RGB"));
    put(strip_matte_, {392, 184, 148, 28});
    strip_fit_ = gf::make_control<gf::CheckBox>(gf::StableId("strip-fit"), tr("Fit image"));
    put(strip_fit_, {556, 184, 204, 28});
    strip_canvas_ = gf::make_control<gf::RasterCanvas>(gf::StableId("strip-canvas"));
    (*strip_canvas_).set_accessible_name(tr("Preview"));
    put(strip_canvas_, {20, 228, 740, 120});
    strip_report_ = gf::make_control<gf::TextBox>(gf::StableId("strip-report"));
    (*strip_report_).set_font({gf::FontRole::control, 11, 400, false});
    (*strip_report_).set_read_only(true);
    (*strip_report_).set_multiline(true);
    (*strip_report_).set_word_wrap(true);
    (*strip_report_).set_accessible_name(tr("Output manifest"));
    put(strip_report_, {20, 358, 740, 80});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    (*error_).set_text_wrapping(gf::TextWrapping::word);
    put(error_, {20, 441, 740, 28});
    strip_preview_ = button("strip-preview", tr("Preview"), {20, 474, 150, 28});
    strip_export_ = button("strip-export", tr("Export…"), {550, 474, 100, 28});
    (*strip_export_).set_enabled(false);
    button("dialog-cancel", tr("Close"), {660, 474, 100, 28});
    for (const std::shared_ptr<gf::ButtonBase>& control :
         {std::static_pointer_cast<gf::ButtonBase>(strip_preview_),
          std::static_pointer_cast<gf::ButtonBase>(strip_export_),
          std::static_pointer_cast<gf::ButtonBase>(strip_fit_)}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::strip_clicked>(*this)));
    }
    for (const std::shared_ptr<gf::ComboBox>& control : {strip_format_, strip_background_}) {
        subscriptions_.push_back((*control).selected_index_changed().subscribe(
            *this, gf::Delegate<std::optional<std::size_t>>::bind<EditorDialog, &EditorDialog::strip_choice>(
                       *this)));
    }
    for (const std::shared_ptr<gf::TextBox>& control : {strip_order_, strip_matte_}) {
        subscriptions_.push_back((*control).text_changed().subscribe(
            *this, gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::strip_text>(*this)));
    }
}
void EditorDialog::stop_toolbar_strip() {
    if (strip_job_) {
        (*strip_job_).cancel.store(true);
    }
    if (strip_worker_.joinable()) {
        strip_worker_.join();
    }
    strip_job_.reset();
}
void EditorDialog::invalidate_toolbar_strip() {
    stop_toolbar_strip();
    (*strip_export_).set_enabled(false);
    (*strip_preview_).set_enabled(true);
    (*strip_canvas_).clear_bitmap();
    (*strip_report_).set_text("");
    (*error_).set_text("");
    const bool bmp = (*strip_format_).selected_index().value_or(0) == 1;
    if (bmp) {
        (*strip_background_).set_selected_index(1);
    }
    (*strip_background_).set_enabled(!bmp);
    (*strip_matte_).set_enabled(bmp || (*strip_background_).selected_index().value_or(0) == 1);
}
void EditorDialog::strip_number(double) {
    invalidate_toolbar_strip();
}
void EditorDialog::strip_choice(std::optional<std::size_t>) {
    invalidate_toolbar_strip();
}
void EditorDialog::strip_text(const std::string&) {
    invalidate_toolbar_strip();
}
void EditorDialog::start_toolbar_strip() {
    invalidate_toolbar_strip();
    try {
        strip_job_ = std::make_shared<ToolbarStripJob>();
        ToolbarStripJob& job = *strip_job_;
        job.source = strip_source_;
        job.order =
            parse_strip_order(std::string((*strip_order_).text()), int((*strip_source_).frames.size()));
        job.options.width = int((*strip_values_[0]).value());
        job.options.height = int((*strip_values_[1]).value());
        job.options.columns = int((*strip_values_[2]).value());
        job.options.gap_x = int((*strip_values_[3]).value());
        job.options.gap_y = int((*strip_values_[4]).value());
        job.options.fit = (*strip_fit_).checked();
        job.bmp = (*strip_format_).selected_index().value_or(0) == 1;
        job.options.opaque = job.bmp || (*strip_background_).selected_index().value_or(0) == 1;
        if (job.options.opaque && !from_hex(std::string((*strip_matte_).text()), job.options.matte)) {
            throw std::runtime_error(tr("Invalid color"));
        }
        (*strip_preview_).set_enabled(false);
        (*strip_report_).set_text(tr("Preparing preview…"));
        strip_worker_ = std::thread(
            StripWork{this, std::static_pointer_cast<EditorDialog>(shared_from_this()), strip_job_});
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
        (*strip_preview_).set_enabled(true);
    }
}
void EditorDialog::deliver_toolbar_strip(const std::shared_ptr<ToolbarStripJob>& job) {
    if (!attached_window() || strip_job_ != job || !(*job).done.load(std::memory_order_acquire)) {
        return;
    }
    if (strip_worker_.joinable()) {
        strip_worker_.join();
    }
    (*strip_preview_).set_enabled(true);
    (*error_).set_text((*job).error);
    (*strip_report_).set_text("");
    if (!(*job).error.empty()) {
        return;
    }
    publish_image((*job).proof, *strip_canvas_);
    std::ostringstream report;
    report << (*job).image.width << " × " << (*job).image.height << " · " << ((*job).bmp ? "BMP" : "PNG")
           << " · " << tr((*job).options.opaque ? "Opaque matte output" : "Transparent") << '\n';
    for (const ToolbarStripCell& cell : (*job).plan.cells) {
        report << '#' << cell.index + 1 << " → (" << cell.image.x << ", " << cell.image.y << ") "
               << cell.image.w << " × " << cell.image.h << (cell.upscaled ? " · " + tr("Upscaling") : "")
               << '\n';
    }
    (*strip_report_).set_text(report.str());
    (*strip_report_).select(gf::Utf8Offset{0}, gf::Utf8Offset{0});
    (*strip_export_).set_enabled(true);
}
void EditorDialog::strip_clicked(gf::ButtonBase& control) {
    const std::string id(control.stable_id().value());
    if (id == "strip-fit") {
        invalidate_toolbar_strip();
        return;
    }
    if (id == "strip-preview") {
        start_toolbar_strip();
        return;
    }
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !strip_job_ || !(*strip_export_).enabled()) {
        return;
    }
    try {
        if ((*editor).export_toolbar_strip((*strip_job_).image, (*strip_job_).bmp, strip_source_path_)) {
            (*error_).set_text(tr("Completed"));
        }
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
    }
}
} // namespace paint::forms

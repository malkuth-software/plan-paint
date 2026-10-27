#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "forms/atlas.hpp"
#include "localization.hpp"
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
struct IconValidationJob {
    ImageContainer source;
    IconValidationProfile profile;
    IconValidation result;
    std::string error;
    std::atomic<bool> cancel{false}, done{false};
};
namespace {
struct ValidationDelivery {
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<IconValidationJob> job;
    void operator()() const {
        const std::shared_ptr<EditorDialog> dialog = owner.lock();
        if (dialog) {
            (*dialog).deliver_icon_validation(job);
        }
    }
};
struct ValidationWork {
    EditorDialog* dispatcher;
    std::weak_ptr<EditorDialog> owner;
    std::shared_ptr<IconValidationJob> job;
    void operator()() const {
        try {
            (*job).result = validate_icon_output((*job).source, (*job).profile, &(*job).cancel);
        } catch (const std::exception& error) {
            (*job).error = error.what();
        }
        (*job).done.store(true, std::memory_order_release);
        if (!(*job).cancel.load()) {
            static_cast<void>((*dispatcher).begin_invoke(ValidationDelivery{owner, job}));
        }
    }
};
std::string alpha_text(const std::array<std::uint64_t, 3>& counts) {
    return std::to_string(counts[0]) + " / " + std::to_string(counts[1]) + " / " + std::to_string(counts[2]);
}
} // namespace
void EditorDialog::initialize_icon_validation() {
    panel_ = {0, 0, 780, 510};
    label("validation-sizes-label", tr("Required sizes"), {20, 48, 500, 24});
    validation_sizes_ = gf::make_control<gf::TextBox>(gf::StableId("validation-sizes"));
    (*validation_sizes_).set_accessible_name(tr("Required sizes"));
    (*validation_sizes_).set_text("16,24,32,48,64,128,256");
    put(validation_sizes_, {20, 78, 500, 28});
    label("validation-depth-label", tr("Color depth"), {540, 48, 220, 24});
    validation_depth_ = gf::make_control<gf::ComboBox>(gf::StableId("validation-depth"));
    (*validation_depth_).set_accessible_name(tr("Color depth"));
    (*validation_depth_).set_items({"32 bpp", "8 bpp", "4 bpp", tr("Any depth")});
    (*validation_depth_).set_selected_index(0);
    put(validation_depth_, {540, 78, 220, 28});
    validation_report_ = gf::make_control<gf::TextBox>(gf::StableId("validation-report"));
    (*validation_report_).set_read_only(true);
    (*validation_report_).set_multiline(true);
    (*validation_report_).set_word_wrap(true);
    (*validation_report_).set_font({gf::FontRole::control, 11, 400, false});
    (*validation_report_).set_accessible_name(tr("Icon validation"));
    put(validation_report_, {20, 122, 740, 314});
    error_ = gf::make_control<gf::Label>(gf::StableId("dialog-error"));
    (*error_).set_text_wrapping(gf::TextWrapping::word);
    put(error_, {20, 442, 740, 28});
    validation_check_ = button("validation-check", tr("Validate"), {20, 474, 150, 28});
    validation_save_ = button("validation-save", tr("Save report…"), {490, 474, 160, 28});
    (*validation_save_).set_enabled(false);
    button("dialog-cancel", tr("Close"), {660, 474, 100, 28});
    for (const std::shared_ptr<gf::Button>& control : {validation_check_, validation_save_}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this,
            gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::validation_clicked>(*this)));
    }
    subscriptions_.push_back(
        (*validation_sizes_)
            .text_changed()
            .subscribe(
                *this,
                gf::Delegate<const std::string&>::bind<EditorDialog, &EditorDialog::validation_text>(*this)));
    subscriptions_.push_back(
        (*validation_depth_)
            .selected_index_changed()
            .subscribe(*this, gf::Delegate<std::optional<std::size_t>>::bind<
                                  EditorDialog, &EditorDialog::validation_choice>(*this)));
}
void EditorDialog::stop_icon_validation() {
    if (validation_job_) {
        (*validation_job_).cancel.store(true);
    }
    if (validation_worker_.joinable()) {
        validation_worker_.join();
    }
    validation_job_.reset();
}
void EditorDialog::invalidate_icon_validation() {
    stop_icon_validation();
    (*validation_save_).set_enabled(false);
    (*validation_check_).set_enabled(true);
    (*validation_report_).set_text("");
    (*error_).set_text("");
}
void EditorDialog::validation_text(const std::string&) {
    invalidate_icon_validation();
}
void EditorDialog::validation_choice(std::optional<std::size_t>) {
    invalidate_icon_validation();
}
void EditorDialog::start_icon_validation() {
    invalidate_icon_validation();
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    try {
        validation_job_ = std::make_shared<IconValidationJob>();
        IconValidationJob& job = *validation_job_;
        job.source = (*editor).document.output_container((*editor).document.atlas.kind == AtlasKind::Cursor);
        job.profile.sizes = parse_validation_sizes(std::string((*validation_sizes_).text()));
        const unsigned depths[] = {32, 8, 4, 0};
        job.profile.depth = depths[(*validation_depth_).selected_index().value_or(0)];
        (*validation_report_).set_text(tr("Preparing preview…"));
        (*validation_check_).set_enabled(false);
        validation_worker_ = std::thread(ValidationWork{
            this, std::static_pointer_cast<EditorDialog>(shared_from_this()), validation_job_});
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
        (*validation_check_).set_enabled(true);
    }
}
void EditorDialog::deliver_icon_validation(const std::shared_ptr<IconValidationJob>& job) {
    if (!attached_window() || validation_job_ != job || !(*job).done.load(std::memory_order_acquire)) {
        return;
    }
    if (validation_worker_.joinable()) {
        validation_worker_.join();
    }
    (*validation_check_).set_enabled(true);
    (*error_).set_text((*job).error);
    if (!(*job).error.empty()) {
        (*validation_report_).set_text("");
        return;
    }
    const IconValidation& result = (*job).result;
    std::ostringstream text;
    text << tr("Missing profile sizes") << ":";
    if (result.missing.empty()) {
        text << " 0";
    }
    for (int size : result.missing) {
        text << ' ' << size;
    }
    text << '\n' << tr("Duplicate size/depth entries") << ": " << result.duplicates.size() << '\n';
    for (const std::vector<int>& group : result.duplicates) {
        for (int index : group) {
            text << '#' << index + 1 << ' ';
        }
        const IconValidationFrame& frame = result.frames[group[0]];
        text << "· " << frame.width << " × " << frame.height << " · " << frame.saved.bits_per_pixel
             << " bpp\n";
    }
    text << '\n' << tr("Alpha counts: zero / partial / opaque") << '\n';
    for (const IconValidationFrame& frame : result.frames) {
        text << '#' << frame.index + 1 << " · " << frame.width << " × " << frame.height << " · "
             << tr("Save: ") << icon_format_name(frame.saved) << '\n';
        if (!frame.valid) {
            text << tr("Failed") << ": " << frame.error << '\n';
            continue;
        }
        if (frame.has_imported) {
            text << tr("Source: ") << icon_format_name(frame.imported) << " · "
                 << alpha_text(frame.source_alpha) << '\n';
        }
        text << tr("Current image") << ": " << alpha_text(frame.current_alpha) << " · " << tr("Save: ")
             << alpha_text(frame.saved_alpha) << '\n';
        text << tr("Alpha changes on save") << ": " << frame.changed_alpha << " · " << tr("Legacy XOR pixels")
             << ": " << frame.xor_pixels << '\n';
    }
    text << '\n'
         << tr("Compatibility notes") << '\n'
         << tr("PNG entries require PNG-aware icon consumers.") << '\n'
         << tr("Partial alpha and legacy XOR depend on the target consumer.") << '\n'
         << tr("Test custom dimensions and cursor hotspots in the target application.") << '\n'
         << tr("Generation, effects and strip previews report upscaling and clipping; saved pixels do not "
               "retain that history.");
    (*validation_report_).set_text(text.str());
    (*validation_report_).select(gf::Utf8Offset{0}, gf::Utf8Offset{0});
    (*validation_save_).set_enabled(true);
}
void EditorDialog::validation_clicked(gf::ButtonBase& control) {
    if (control.stable_id().value() == "validation-check") {
        start_icon_validation();
        return;
    }
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !validation_job_ || !(*validation_save_).enabled()) {
        return;
    }
    try {
        static_cast<void>((*editor).save_export_report(icon_validation_json((*validation_job_).result),
                                                       "icon-validation.json"));
    } catch (const std::exception& error) {
        (*error_).set_text(error.what());
    }
}
} // namespace paint::forms

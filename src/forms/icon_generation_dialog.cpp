#include "forms/display.hpp"
#include "forms/editor.hpp"
#include "import_codecs.hpp"
#include "localization.hpp"
#include <algorithm>
#include <sstream>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
std::string target_dimensions(const IconGenerationTarget& target) {
    return std::to_string(target.width) + " × " + std::to_string(target.height);
}
std::string target_depth(unsigned depth) {
    return depth == 32 ? "RGBA" : std::to_string(depth) + " bpp DIB";
}
} // namespace
void EditorDialog::initialize_icon_targets() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const Document& document = (*editor).document;
    icon_existing_ = document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor;
    icon_sources_ = document.output_container(kind_ == EditorDialogKind::cursor_sizes);
    icon_sources_.kind = document.atlas.kind == AtlasKind::Cursor ? ContainerKind::Cursor
                         : icon_existing_                         ? ContainerKind::Icon
                                                                  : ContainerKind::Image;
    icon_source_current_ = icon_existing_ ? std::max(0, document.atlas.active) : 0;
    icon_revision_ = document.revision;
    icon_epoch_ = document.atlas_epoch;
    label("icon-target-label", tr("Target images"), {420, 44, 340, 24});
    icon_target_list_ = gf::make_control<gf::ListBox>(gf::StableId("icon-target-list"));
    (*icon_target_list_).set_accessible_name(tr("Target images"));
    (*icon_target_list_).set_item_height(26);
    put(icon_target_list_, {420, 74, 340, 116});
    label("icon-target-source-label", tr("Source image"), {420, 198, 105, 28});
    icon_target_source_ = gf::make_control<gf::ComboBox>(gf::StableId("icon-target-source"));
    (*icon_target_source_).set_accessible_name(tr("Source image"));
    std::vector<std::string> sources;
    for (std::size_t index = 0; index < icon_sources_.frames.size(); ++index) {
        const Image& image = icon_sources_.frames[index].image;
        sources.push_back("#" + std::to_string(index + 1) + " · " +
                          (svg_source_matches(icon_sources_.frames[index]) ? "SVG · " : "") +
                          std::to_string(image.width) + " × " + std::to_string(image.height));
    }
    (*icon_target_source_).set_items(sources);
    put(icon_target_source_, {530, 198, 230, 28});
    label("icon-target-depth-label", tr("Color depth"), {420, 236, 105, 28});
    icon_target_depth_ = gf::make_control<gf::ComboBox>(gf::StableId("icon-target-depth"));
    (*icon_target_depth_).set_accessible_name(tr("Color depth"));
    (*icon_target_depth_).set_items({"RGBA", "4 bpp DIB", "8 bpp DIB"});
    put(icon_target_depth_, {530, 236, 230, 28});
    icon_target_replace_ =
        gf::make_control<gf::CheckBox>(gf::StableId("icon-target-replace"), tr("Replace this image"));
    put(icon_target_replace_, {420, 274, 340, 28});
    label("icon-target-before-label", tr("Before"), {420, 306, 160, 22});
    label("icon-target-after-label", tr("After"), {600, 306, 160, 22});
    icon_target_before_ = gf::make_control<gf::RasterCanvas>(gf::StableId("icon-target-before"));
    icon_target_after_ = gf::make_control<gf::RasterCanvas>(gf::StableId("icon-target-after"));
    for (const std::shared_ptr<gf::RasterCanvas>& canvas : {icon_target_before_, icon_target_after_}) {
        (*canvas).set_zoom(1);
        (*canvas).set_view_origin({0, 0});
    }
    put(icon_target_before_, {420, 332, 160, 84});
    put(icon_target_after_, {600, 332, 160, 84});
    label("icon-custom-label", tr("Add custom dimensions or another depth variant"), {20, 330, 380, 30});
    (*std::static_pointer_cast<gf::Label>(controls_.back())).set_text_wrapping(gf::TextWrapping::word);
    label("icon-custom-width-label", tr("Width"), {20, 366, 60, 28});
    icon_target_width_ = number("icon-custom-width", {82, 366, 100, 28}, 1, 256, 32);
    label("icon-custom-height-label", tr("Height"), {202, 366, 60, 28});
    icon_target_height_ = number("icon-custom-height", {264, 366, 100, 28}, 1, 256, 32);
    const std::shared_ptr<gf::Button> add = button("icon-target-add", tr("Add image"), {20, 400, 144, 24});
    icon_target_discard_ = button("icon-target-discard", tr("Discard custom target"), {174, 400, 226, 24});
    for (const std::shared_ptr<gf::ButtonBase>& control :
         {std::static_pointer_cast<gf::ButtonBase>(add),
          std::static_pointer_cast<gf::ButtonBase>(icon_target_discard_),
          std::static_pointer_cast<gf::ButtonBase>(icon_target_replace_)}) {
        subscriptions_.push_back((*control).clicked().subscribe(
            *this,
            gf::Delegate<gf::ButtonBase&>::bind<EditorDialog, &EditorDialog::icon_target_clicked>(*this)));
    }
    for (const std::shared_ptr<gf::ComboBox>& control : {icon_target_source_, icon_target_depth_}) {
        subscriptions_.push_back((*control).selected_index_changed().subscribe(
            *this,
            gf::Delegate<std::optional<std::size_t>>::bind<EditorDialog, &EditorDialog::icon_target_changed>(
                *this)));
    }
    subscriptions_.push_back(
        (*icon_target_list_)
            .selection_changed()
            .subscribe(*this, gf::Delegate<const gf::ListSelectionChange&>::bind<
                                  EditorDialog, &EditorDialog::icon_target_selected>(*this)));
}
void EditorDialog::refresh_icon_plan() {
    if (!icon_plan_ || !error_) {
        return;
    }
    icon_plan_valid_ = false;
    try {
        const std::vector<IconTargetRow> previous = icon_targets_;
        icon_targets_.clear();
        const std::vector<IconFrame> empty;
        const std::vector<IconFrame>& existing = icon_existing_ ? icon_sources_.frames : empty;
        const IconSizePolicy policy =
            static_cast<IconSizePolicy>((*icon_policy_).selected_index().value_or(0));
        const std::vector<int> sizes = chosen_icon_sizes();
        // Build the full review even when its output count is invalid, so the
        // owner can remove custom additions or uncheck presets to repair it.
        IconSizePlan plan;
        for (std::size_t index = 0; index < existing.size(); ++index) {
            const Image& image = existing[index].image;
            const bool selected = image.width == image.height &&
                                  std::find(sizes.begin(), sizes.end(), image.width) != sizes.end();
            if (policy != IconSizePolicy::ExactProfile || selected) {
                plan.steps.push_back({static_cast<int>(index), image.width, false});
            }
        }
        for (int size : sizes) {
            bool present = false;
            for (const IconFrame& frame : existing) {
                present = present || (frame.image.width == size && frame.image.height == size);
            }
            if (!present) {
                plan.steps.push_back({-1, size, true});
            }
        }
        for (std::size_t index = 0; index < existing.size(); ++index) {
            IconTargetRow row;
            row.target = {icon_source_current_, static_cast<int>(index), existing[index].image.width,
                          existing[index].image.height, 32};
            row.removed = true;
            for (const IconSizeStep& step : plan.steps) {
                if (step.existing_index == static_cast<int>(index)) {
                    row.removed = false;
                    break;
                }
            }
            for (const IconTargetRow& old : previous) {
                if (old.target.existing_index == static_cast<int>(index)) {
                    row.target = old.target;
                    row.generate = !row.removed && old.generate;
                    break;
                }
            }
            icon_targets_.push_back(row);
        }
        for (const IconSizeStep& step : plan.steps) {
            if (step.existing_index >= 0) {
                continue;
            }
            IconTargetRow row;
            row.target = {icon_source_current_, -1, step.size, step.size, 32};
            row.generate = true;
            for (const IconTargetRow& old : previous) {
                if (!old.custom && old.target.existing_index < 0 && old.target.width == step.size) {
                    row.target = old.target;
                    break;
                }
            }
            icon_targets_.push_back(row);
        }
        for (const IconTargetRow& old : previous) {
            if (old.custom) {
                icon_targets_.push_back(old);
            }
        }
        render_icon_targets();
    } catch (const std::exception& error) {
        (*icon_plan_).set_text("");
        (*icon_manifest_).set_text("");
        (*error_).set_text(tr(error.what()));
    }
}
void EditorDialog::render_icon_targets() {
    icon_target_sync_ = true;
    icon_plan_valid_ = false;
    (*icon_target_discard_).set_enabled(false);
    (*icon_target_source_).set_enabled(false);
    (*icon_target_depth_).set_enabled(false);
    (*icon_target_replace_).set_enabled(false);
    try {
        std::size_t selected = (*icon_target_list_).selected_index().value_or(0);
        selected = std::min(selected, icon_targets_.empty() ? 0 : icon_targets_.size() - 1);
        std::vector<std::string> labels;
        std::string manifest, warnings;
        int kept = 0, added = 0, replaced = 0, removed = 0;
        for (const IconTargetRow& row : icon_targets_) {
            const IconGenerationTarget& target = row.target;
            const bool existing = target.existing_index >= 0;
            const std::string action = row.removed     ? tr("Remove")
                                       : !row.generate ? tr("Keep")
                                       : existing      ? tr("Replace")
                                                       : tr("Add");
            std::string description = action +
                                      (existing ? " #" + std::to_string(target.existing_index + 1) : "") +
                                      " · " + target_dimensions(target);
            if (row.removed) {
                ++removed;
            } else if (!row.generate) {
                ++kept;
            } else {
                if (existing) {
                    ++replaced;
                } else {
                    ++added;
                }
                description += " · " + target_depth(target.bits_per_pixel) + " ← #" +
                               std::to_string(target.source_index + 1);
                const IconFrame& source_frame = icon_sources_.frames[target.source_index];
                const Image& source = source_frame.image;
                if (svg_source_matches(source_frame)) {
                    description += " · SVG";
                } else if (target.width > source.width || target.height > source.height) {
                    description += " · " + tr("Upscaling");
                }
                if (target.bits_per_pixel != 32) {
                    warnings = tr("Indexed output: alpha threshold 128, white matte, no dithering.");
                }
            }
            labels.push_back(description);
            manifest += description + "\n";
        }
        (*icon_target_list_).set_items(labels);
        (*icon_plan_)
            .set_text(tr("Keep: ") + std::to_string(kept) + tr(" · Add: ") + std::to_string(added) +
                      tr(" · Replace: ") + std::to_string(replaced) + tr(" · Remove: ") +
                      std::to_string(removed));
        (*icon_manifest_).set_text(manifest);
        (*icon_manifest_).select(gf::Utf8Offset{0}, gf::Utf8Offset{0});
        (*error_).set_text(warnings);
        if (!icon_targets_.empty()) {
            (*icon_target_list_).select_index(selected);
            const IconTargetRow& row = icon_targets_[selected];
            (*icon_target_discard_).set_enabled(row.custom);
            const IconGenerationTarget& target = row.target;
            (*icon_target_source_).set_selected_index(target.source_index);
            (*icon_target_depth_)
                .set_selected_index(target.bits_per_pixel == 32  ? 0
                                    : target.bits_per_pixel == 4 ? 1
                                                                 : 2);
            (*icon_target_source_).set_enabled(row.generate && !row.removed);
            (*icon_target_depth_).set_enabled(row.generate && !row.removed);
            (*icon_target_replace_).set_enabled(target.existing_index >= 0 && !row.removed);
            (*icon_target_replace_).set_checked(target.existing_index >= 0 && row.generate && !row.removed);
            IconFrame before;
            before.image.reset(target.width, target.height, {0, 0, 0, 0});
            if (target.existing_index >= 0) {
                before = icon_sources_.frames[target.existing_index];
            }
            publish_image(render_asset_preview(before, 160, 84), *icon_target_before_);
            IconFrame after = before;
            if (row.removed) {
                after.image.reset(target.width, target.height, {0, 0, 0, 0});
                after.xor_pixels.clear();
            } else if (row.generate) {
                after = generate_icon_target(icon_sources_, target, kind_ == EditorDialogKind::cursor_sizes);
            }
            publish_image(render_asset_preview(after, 160, 84), *icon_target_after_);
        }
        if (kept + added + replaced < 1 || kept + added + replaced > 256) {
            throw std::runtime_error("ICO/CUR must contain between 1 and 256 images.");
        }
        icon_plan_valid_ = true;
    } catch (const std::exception& error) {
        Image blank;
        blank.reset(160, 84, {0, 0, 0, 0});
        publish_image(blank, *icon_target_after_);
        (*error_).set_text(tr(error.what()));
    }
    icon_target_sync_ = false;
}
void EditorDialog::icon_target_selected(const gf::ListSelectionChange&) {
    if (!icon_target_sync_) {
        render_icon_targets();
    }
}
void EditorDialog::icon_target_changed(std::optional<std::size_t>) {
    if (icon_target_sync_) {
        return;
    }
    const std::size_t index = (*icon_target_list_).selected_index().value_or(icon_targets_.size());
    if (index >= icon_targets_.size()) {
        return;
    }
    IconTargetRow& row = icon_targets_[index];
    row.target.source_index = static_cast<int>((*icon_target_source_).selected_index().value_or(0));
    const unsigned depths[] = {32, 4, 8};
    row.target.bits_per_pixel = depths[(*icon_target_depth_).selected_index().value_or(0)];
    render_icon_targets();
}
void EditorDialog::icon_target_clicked(gf::ButtonBase& control) {
    if (icon_target_sync_) {
        return;
    }
    const std::size_t index = (*icon_target_list_).selected_index().value_or(icon_targets_.size());
    if (control.stable_id().value() == "icon-target-add") {
        IconTargetRow row;
        row.target = {icon_source_current_, -1, static_cast<int>((*icon_target_width_).value()),
                      static_cast<int>((*icon_target_height_).value()), 32};
        row.generate = row.custom = true;
        icon_targets_.push_back(row);
        render_icon_targets();
        (*icon_target_list_).select_index(icon_targets_.size() - 1);
    } else if (index < icon_targets_.size()) {
        if (control.stable_id().value() == "icon-target-discard" && icon_targets_[index].custom) {
            icon_targets_.erase(icon_targets_.begin() + static_cast<std::ptrdiff_t>(index));
        } else if (control.stable_id().value() == "icon-target-replace") {
            icon_targets_[index].generate = (*icon_target_replace_).checked();
        }
        render_icon_targets();
    }
}
void EditorDialog::apply_icon_targets() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    Document& document = (*editor).document;
    if (!icon_plan_valid_) {
        throw std::runtime_error((*error_).text().empty() ? "Choose a valid target plan." : (*error_).text());
    }
    if (document.revision != icon_revision_ || document.atlas_epoch != icon_epoch_) {
        throw std::runtime_error("The document changed. Reopen Icon sizes before generating.");
    }
    std::vector<IconGenerationTarget> targets;
    std::vector<int> removals;
    for (const IconTargetRow& row : icon_targets_) {
        if (row.removed) {
            removals.push_back(row.target.existing_index);
        } else if (row.generate) {
            targets.push_back(row.target);
        }
    }
    document.generate_icon_targets(targets, removals, kind_ == EditorDialogKind::cursor_sizes);
}
} // namespace paint::forms

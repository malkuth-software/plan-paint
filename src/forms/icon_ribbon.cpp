#include "forms/icon_ribbon.hpp"
#include "forms/editor.hpp"
#include "localization.hpp"
#include <algorithm>

namespace paint::forms {
namespace gf = gui_forms;
namespace {
constexpr double command_width = 114;
constexpr double command_plane_width = command_width * 4;
} // namespace
IconPanel::IconPanel(gf::StableId id, std::weak_ptr<Editor> editor)
    : Control(std::move(id)), editor_(std::move(editor)) {}
void IconPanel::initialize_control_tree() {
    const char* ids[] = {"sizes",   "gallery", "hotspot",  "palette", "alpha-tools", "states",
                         "compare", "export",  "previous", "next",    "batch",       "index-paint", "effects", "transform", "validate"};
    const std::string labels[] = {tr("Icon sizes…"),  tr("Gallery…"), tr("Hotspot…"), tr("Palette…"),
                                  tr("Alpha tools…"), tr("States…"),  tr("Compare…"), tr("Export…"),
                                  tr("Previous"),     tr("Next"),     tr("Batch…"),   tr("Index paint"), tr("Effects"), tr("Transform"), tr("Validate")};
    for (std::size_t index = 0; index < std::size(ids); ++index) {
        const std::shared_ptr<gf::Button> command =
            gf::make_control<gf::Button>(gf::StableId(std::string("icon-") + ids[index]), labels[index]);
        (*command).set_theme_override(ribbon_theme());
        (*command).set_font({gf::FontRole::control, 11, 400, false});
        (*command).set_accessible_name(labels[index]);
        subscriptions_.push_back((*command).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<IconPanel, &IconPanel::clicked>(*this)));
        commands_.push_back(command);
        add_child(command);
    }
    const std::string group_names[] = {tr("Images"), tr("Color & alpha"), tr("Production")};
    for (std::size_t index = 0; index < std::size(group_names); ++index) {
        const std::shared_ptr<gf::Label> group = gf::make_control<gf::Label>(
            gf::StableId("icon-group-" + std::to_string(index)), group_names[index]);
        (*group).set_font({gf::FontRole::control, 11, 400, false});
        (*group).set_alignment(gf::HorizontalAlignment::center);
        (*group).set_use_mnemonic(false);
        groups_.push_back(group);
        add_child(group);
    }
    strip_ = gf::make_control<gf::FlowLayoutPanel>(gf::StableId("icon-frames"));
    (*strip_).set_auto_scroll(true);
    (*strip_).set_padding({0, 0, 0, 0});
    (*strip_).set_wrap_contents(false);
    (*strip_).set_item_spacing({4, 0});
    add_child(strip_);
    status_ = gf::make_control<gf::Label>(gf::StableId("icon-status"));
    (*status_).set_font({gf::FontRole::control, 11, 400, false});
    add_child(status_);
}
void IconPanel::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    // Production spans two columns; the remaining width belongs to image selection.
    const gf::Rect placements[] = {{6, 4, 102, 24},   {6, 30, 102, 24},   {6, 56, 102, 24},
                                   {120, 4, 102, 24}, {120, 30, 102, 24}, {234, 4, 102, 24},
                                   {348, 4, 102, 24}, {234, 30, 102, 24}};
    for (std::size_t index = 0; index < std::size(placements); ++index) {
        set_child_layout(commands_[index], placements[index]);
    }
    set_child_layout(commands_[8], {bounds.width - 162, 96, 76, 20});
    set_child_layout(commands_[9], {bounds.width - 82, 96, 76, 20});
    set_child_layout(commands_[10], {348, 30, 102, 24});
    set_child_layout(commands_[11], {120, 56, 102, 24});
    set_child_layout(commands_[12], {234, 56, 102, 24});
    set_child_layout(commands_[13], {348, 56, 102, 24});
    set_child_layout(commands_[14], {6,96,102,20});
    for (std::size_t index = 0; index < 3; ++index) {
        set_child_layout(groups_[index],
                         {index * command_width, 80, index == 2 ? command_width * 2 : command_width, 15});
    }
    const double strip_width = std::max(1.0, bounds.width - command_plane_width - 12);
    set_child_layout(strip_, {command_plane_width + 6, 4, strip_width, 90});
    set_child_layout(status_, {114, 96, std::max(1.0, bounds.width - 282), 20});
}
void IconPanel::on_paint(gf::Painter& painter, gf::Rect) {
    for (const int index : {1, 2, 4}) {
        const double x = index * command_width;
        painter.draw_line({x, 5}, {x, 92}, interface_color(*this, gf::Color::rgba(109, 141, 183)), 1);
        painter.draw_line({x + 1, 5}, {x + 1, 92}, interface_color(*this, gf::Color::rgba(245, 250, 255)), 1);
    }
}
void IconPanel::clicked(gf::ButtonBase& control) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    const std::string id(control.stable_id().value());
    const std::string action = id.substr(5);
    const bool cursor = (*editor).document.atlas.kind == AtlasKind::Cursor;
    if (action == "index-paint") {
        (*editor).icon_index_paint = !(*editor).icon_index_paint;
        if ((*editor).icon_index_paint) {
            (*editor).choose_icon_index((*editor).icon_primary_index, false);
            (*editor).choose_tool(Tool::Pencil);
        }
        (*editor).refresh();
    } else if (action == "transform") {
        (*editor).execute("numeric-transform");
    } else if (action == "batch") {
        (*editor).execute("icon-batch");
    } else {
        (*editor).execute(action == "sizes" ? cursor ? "atlas-cursors" : "atlas-icons" : "atlas-" + action);
    }
}
void IconPanel::synchronize() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    Document& document = (*editor).document;
    document.sync_atlas();
    const AtlasState& atlas = document.atlas;
    const bool cursor = atlas.kind == AtlasKind::Cursor;
    const bool icon = cursor || atlas.kind == AtlasKind::Icon;
    const int count = icon ? atlas.count() : 0;
    if (kind_ != atlas.kind || count_ != count) {
        for (const std::shared_ptr<AtlasThumbnail>& thumbnail : thumbnails_) {
            static_cast<void>((*strip_).remove_child((*thumbnail).runtime_id()));
        }
        thumbnails_.clear();
        kind_ = atlas.kind;
        count_ = count;
        active_ = -1;
        for (int index = 0; index < count; ++index) {
            const std::shared_ptr<AtlasThumbnail> thumbnail = gf::make_control<AtlasThumbnail>(
                gf::StableId("icon-frame-" + std::to_string(index)), editor_, index);
            thumbnails_.push_back(thumbnail);
            (*strip_).add_child(thumbnail);
        }
    }
    for (const std::shared_ptr<AtlasThumbnail>& thumbnail : thumbnails_) {
        (*thumbnail).synchronize();
    }
    for (const std::shared_ptr<gf::Button>& command : commands_) {
        (*command).set_enabled(icon && count > 0);
    }
    const std::string sizes = cursor ? tr("Cursor sizes…") : tr("Icon sizes…");
    (*commands_[0]).set_text(sizes);
    (*commands_[0]).set_accessible_name(sizes);
    (*commands_[2]).set_enabled(cursor && count > 0);
    const bool indexed = (*editor).icon_palette_available();
    (*commands_[11]).set_enabled(indexed || (*editor).icon_index_paint);
    (*commands_[11]).set_selected((*editor).icon_index_paint);
    if (indexed) {
        const unsigned capacity = static_cast<unsigned>((*atlas.icons[atlas.active].source).palette.size());
        (*editor).icon_primary_index = std::min((*editor).icon_primary_index, capacity - 1);
        (*editor).icon_alt_index = std::min((*editor).icon_alt_index, capacity - 1);
    }
    (*status_).set_text("");
    (*status_).set_accessible_name("");
    if (icon && atlas.active >= 0 && atlas.active < count) {
        const IconFrame& frame = atlas.icons[atlas.active];
        const ContainerKind kind = cursor ? ContainerKind::Cursor : ContainerKind::Icon;
        std::string status = tr("Frame ") + std::to_string(atlas.active + 1) + "/" + std::to_string(count) +
                             " · " + std::to_string(frame.image.width) + " × " +
                             std::to_string(frame.image.height) + " · " + tr("Source: ") +
                             icon_format_name(frame.source ? (*frame.source).format : IconFormat{}) + " · " +
                             tr("Save: ") + icon_format_name(icon_output_format(frame, kind));
        if ((*editor).icon_index_paint) {
            status = tr("Index paint") + " · " + tr("Primary") + " #" +
                     std::to_string((*editor).icon_primary_index) + " · " + tr("Alt") + " #" +
                     std::to_string((*editor).icon_alt_index) + " · " + status;
        }
        (*status_).set_text(status);
        (*status_).set_accessible_name(status);
        if (active_ != atlas.active) {
            active_ = atlas.active;
            if (window()) {
                (*window()).perform_layout();
                (*strip_).scroll_control_into_view(thumbnails_[static_cast<std::size_t>(active_)]);
            }
        }
    }
}
} // namespace paint::forms

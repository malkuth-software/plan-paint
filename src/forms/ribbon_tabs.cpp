// The ribbon's structure: which tabs, groups and commands it shows for the
// editor's state, built as gui_forms::Ribbon records, and the hosted controls
// those records make (ribbon.hpp explains the layout).
#include "codecs.hpp"
#include "forms/dropper_icon.hpp"
#include "forms/editor.hpp"
#include "forms/ribbon.hpp"
#include "forms/ribbon_icons.hpp"
#include "localization.hpp"
#include "material.hpp"
#include <gui_forms/delegate.hpp>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
using Kind = gf::RibbonCommandKind;
using Size = gf::RibbonCommandSize;
// Hosted widths, in logical units.
constexpr double number_width = 96;
constexpr double combo_width = 180;
// Groups of label/field pairs stack two rows per column, so a field's label
// sits above it.
gf::RibbonGroupLayout paired_rows() {
    gf::RibbonGroupLayout layout;
    layout.size = gf::RibbonGroupSize::large;
    layout.rows = 2;
    return layout;
}
// A layout of `size` with every listed command at `command_size`.
gf::RibbonGroupLayout sized_layout(gf::RibbonGroupSize size, std::size_t commands, gf::RibbonCommandSize command_size,
                                   std::size_t rows) {
    gf::RibbonGroupLayout layout;
    layout.size = size;
    layout.command_sizes.assign(commands, command_size);
    layout.rows = rows;
    return layout;
}
gf::RibbonGroupLayout collapsed_layout() {
    gf::RibbonGroupLayout layout;
    layout.size = gf::RibbonGroupSize::collapsed;
    return layout;
}
gf::RibbonGroupSpec group(const std::string& id, const std::string& caption) {
    gf::RibbonGroupSpec result;
    result.stable_id = id;
    result.caption = caption;
    return result;
}
gf::MenuItemSpec submenu(const std::string& id, const std::string& text, std::vector<gf::MenuItemSpec> children) {
    gf::MenuItemSpec result;
    result.stable_id = id;
    result.kind = gf::MenuItemKind::submenu;
    result.text = text;
    result.children = std::move(children);
    return result;
}
// A small command with an image may leave its label out (RB decision 30);
// before the images load, it keeps its label, which set_tabs accepts.
void icon_only(gf::RibbonCommandSpec& command) {
    if (command.image.value != 0U && command.size == Size::small) {
        command.show_label = false;
    }
}
// One hosted number field's range and starting value.
struct NumberSpec {
    const char* id;
    std::shared_ptr<gf::NumericUpDown> Ribbon::*member;
    double minimum, maximum, value;
    int decimals;
};
// Whether a tool draws geometry (shapes, paths) rather than strokes.
bool geometry_tool(Tool tool) {
    return tool == Tool::Shape || tool == Tool::Path || tool == Tool::Freehand;
}
} // namespace

std::shared_ptr<gf::Command> Ribbon::command(const std::string& id, const std::string& text) {
    std::map<std::string, CommandEntry>::iterator found = commands_.find(id);
    if (found != commands_.end()) {
        return (*found).second.command;
    }
    CommandEntry entry;
    entry.command = std::make_shared<gf::Command>(id, text);
    entry.invoked = (*entry.command).invoked().subscribe(
        *this, gf::Delegate<const gf::CommandInvocation&>::bind<Ribbon, &Ribbon::command_invoked>(*this));
    const std::shared_ptr<gf::Command> result = entry.command;
    commands_.emplace(id, std::move(entry));
    return result;
}
// Menu rows get their own Commands, which nothing subscribes to: the ribbon
// reports the choice once, through drop_down_item_invoked. Row IDs carry a
// "choice-" prefix so that they never meet a command ID.
gf::MenuItemSpec Ribbon::menu_item(const std::string& id, const std::string& text) {
    const std::string row = "choice-" + id;
    std::map<std::string, std::shared_ptr<gf::Command>>::iterator found = menu_commands_.find(row);
    std::shared_ptr<gf::Command> row_command;
    if (found == menu_commands_.end()) {
        row_command = std::make_shared<gf::Command>(row, text);
        menu_commands_.emplace(row, row_command);
    } else {
        row_command = (*found).second;
    }
    return {row, gf::MenuItemKind::command, row_command, text};
}
gf::RibbonCommandSpec Ribbon::button(const std::string& id, const std::string& text, int image_index, Size size,
                                     Kind kind) {
    gf::RibbonCommandSpec spec;
    spec.stable_id = id;
    spec.kind = kind;
    spec.size = size;
    spec.command = command(id, text);
    spec.text = text;
    if (image_index >= 0) {
        spec.image = icon(image_index, size == Size::large ? 32 : 16);
    }
    return spec;
}
gf::RibbonCommandSpec Ribbon::hosted(const std::string& id, const std::string& text, Kind kind, double width) {
    gf::RibbonCommandSpec spec;
    spec.stable_id = id;
    spec.kind = kind;
    spec.size = Size::small;
    spec.text = text;
    spec.width = width;
    return spec;
}
// A number field with its label above it. The label is a small command
// that moves keyboard focus to the field: hosted commands show no label of
// their own (their text is the field's accessible name).
void Ribbon::number(gf::RibbonGroupSpec& target, const std::string& id, const std::string& text, double width) {
    target.commands.push_back(button(id + "-label", text, -1, Size::small));
    target.commands.push_back(hosted(id, text, Kind::numeric_up_down, width > 0 ? width : number_width));
}
gf::RibbonCommandSpec Ribbon::gallery_command(const std::string& id, const GalleryEntry& entry) {
    gf::RibbonCommandSpec spec;
    spec.stable_id = id;
    spec.kind = Kind::gallery;
    spec.gallery = entry.gallery;
    return spec;
}
gf::ImageId Ribbon::icon(int index, int size) {
    if (!attached_window()) {
        return {};
    }
    const std::string key = std::to_string(size) + "/" + std::to_string(index);
    std::map<std::string, gf::ImageId>::iterator found = images_.find(key);
    if (found != images_.end()) {
        return (*found).second;
    }
    const gf::ImageLoadResult loaded = (*attached_window()).load_png(ribbon_icon_png(index, size));
    if (!loaded) {
        return {};
    }
    images_.emplace(key, loaded.image);
    return loaded.image;
}
gf::ImageId Ribbon::artwork(const std::string& name, const Image& image) {
    if (!attached_window()) {
        return {};
    }
    const std::vector<std::uint8_t> png = encode_png(image);
    const std::span<const std::byte> encoded = std::as_bytes(std::span(png));
    std::map<std::string, gf::ImageId>::iterator found = images_.find(name);
    // A replacement retires the old generational ImageId and returns the
    // new one, which the next tab specs carry; a refused replacement (a
    // stale id) loads the artwork afresh.
    if (found != images_.end()) {
        const gf::ImageLoadResult replaced = (*attached_window()).replace_png((*found).second, encoded);
        if (replaced) {
            (*found).second = replaced.image;
            return replaced.image;
        }
        images_.erase(found);
    }
    const gf::ImageLoadResult loaded = (*attached_window()).load_png(encoded);
    if (!loaded) {
        return {};
    }
    images_.emplace(name, loaded.image);
    return loaded.image;
}

gf::RibbonTabSpec Ribbon::home_tab() {
    gf::RibbonTabSpec tab;
    tab.stable_id = "home-tab";
    tab.text = tr("Home");
    // The original Home plane: Save over Undo and Redo, Paste on its own,
    // then one Tools group whose upper row holds the image and clipboard
    // commands and whose lower row holds the drawing tools; none of the first
    // two groups has a caption.
    gf::RibbonGroupSpec quick = group("quick", "");
    quick.layouts = {paired_rows()};
    // Stacked groups fill column by column, so Save stands over Undo with
    // Redo beside it (the original's Save spanned both; GUI.Forms gap).
    quick.commands.push_back(button("save", tr("Save"), 14, Size::small));
    gf::RibbonCommandSpec undo = button("undo", tr("Undo (Ctrl/Command+Z)"), 15, Size::small);
    gf::RibbonCommandSpec redo = button("redo", tr("Redo (Ctrl/Command+Y)"), 16, Size::small);
    icon_only(undo);
    icon_only(redo);
    quick.commands.push_back(std::move(undo));
    quick.commands.push_back(std::move(redo));
    tab.groups.push_back(std::move(quick));

    gf::RibbonGroupSpec clipboard = group("clipboard", "");
    gf::RibbonCommandSpec paste = button("paste", tr("Paste"), 0, Size::large, Kind::split);
    paste.drop_down = {menu_item("paste", tr("Paste")), menu_item("open", tr("Open image…"))};
    clipboard.commands.push_back(std::move(paste));
    tab.groups.push_back(std::move(clipboard));

    // The upper row, left to right: Select, Crop, Resize, Rotate, Cut, Copy;
    // the lower row: Pencil, Fill, Text, Eraser, Color picker, Magnifier.
    gf::RibbonCommandSpec select = button("tool-0", tr("Select"), 3, Size::small, Kind::split);
    select.drop_down = {menu_item("tool-0", tr("Rectangular selection")),
                        menu_item("lasso-free", tr("Free-form selection")),
                        menu_item("lasso-tight", tr("Tightening lasso")),
                        menu_item("lasso-void", tr("Inner-void lasso")),
                        menu_item("lasso-wand", tr("Magic wand (all similar colors)")),
                        menu_item("select-all", tr("Select all")),
                        menu_item("invert-selection", tr("Invert selection")),
                        menu_item("transparent-selection", tr("Transparent selection")),
                        menu_item("selection-dither", tr("Dither / posterize…"))};
    gf::RibbonCommandSpec rotate = button("rotate-menu", tr("Rotate"), 6, Size::small, Kind::drop_down);
    rotate.drop_down = {menu_item("rotate-right", tr("Rotate right 90°")),
                        menu_item("rotate-left", tr("Rotate left 90°")),
                        menu_item("rotate-180", tr("Rotate 180°")),
                        menu_item("flip-horizontal", tr("Flip horizontal")),
                        menu_item("flip-vertical", tr("Flip vertical")),
                        menu_item("numeric-transform", tr("Transform") + "…")};
    gf::RibbonCommandSpec fill = button("tool-3", tr("Fill with color"), 8, Size::small, Kind::split);
    fill.drop_down = {menu_item("bucket-solid", tr("Solid / material fill")),
                      menu_item("bucket-gradient", tr("Use gradient"))};
    gf::RibbonCommandSpec eraser = button("tool-4", tr("Eraser"), 10, Size::small, Kind::split);
    eraser.drop_down = {menu_item("eraser-0", tr("Hard eraser")), menu_item("eraser-1", tr("Soft eraser")),
                        menu_item("eraser-2", tr("Blur")), menu_item("eraser-3", tr("Sharpen")),
                        menu_item("eraser-4", tr("Smudge"))};
    gf::RibbonCommandSpec picker = button("tool-5", tr("Color picker"), -1, Size::small, Kind::toggle);
    if (attached_window()) {
        std::map<std::string, gf::ImageId>::iterator found = images_.find("dropper");
        if (found == images_.end()) {
            const gf::ImageLoadResult loaded =
                (*attached_window()).load_png(std::as_bytes(std::span(dropper_icon)));
            if (loaded) {
                found = images_.emplace("dropper", loaded.image).first;
            }
        }
        if (found != images_.end()) {
            picker.image = (*found).second;
        }
    }
    gf::RibbonCommandSpec upper[] = {std::move(select),
                                     button("crop", tr("Crop"), 4, Size::small),
                                     button("resize", tr("Resize"), 5, Size::small),
                                     std::move(rotate),
                                     button("cut", tr("Cut"), 1, Size::small),
                                     button("copy", tr("Copy"), 2, Size::small)};
    gf::RibbonCommandSpec lower[] = {button("tool-2", tr("Pencil"), 7, Size::small, Kind::toggle),
                                     std::move(fill),
                                     button("text", tr("Text"), 9, Size::small, Kind::toggle),
                                     std::move(eraser),
                                     std::move(picker),
                                     button("tool-6", tr("Magnifier"), 12, Size::small, Kind::toggle)};
    static_assert(std::size(upper) == std::size(lower));
    gf::RibbonGroupSpec tools = group("tools", tr("Tools"));
    // Two rows filled column by column: each upper command, then the tool
    // beneath it.
    for (std::size_t column = 0; column < std::size(upper); ++column) {
        icon_only(upper[column]);
        icon_only(lower[column]);
        tools.commands.push_back(std::move(upper[column]));
        tools.commands.push_back(std::move(lower[column]));
    }
    tools.layouts = {sized_layout(gf::RibbonGroupSize::large, 0, Size::small, 2), collapsed_layout()};
    tab.groups.push_back(std::move(tools));

    gf::RibbonGroupSpec brushes = group("brushes", tr("Brushes"));
    gf::RibbonCommandSpec brush = button("brush-menu", tr("Brushes"), 13, Size::large, Kind::split);
    brush.drop_down = {menu_item("family-additive", tr("Additive brushes")),
                       menu_item("family-glitter", tr("Fine glitter")),
                       menu_item("family-mix", tr("Mix existing pixels")),
                       menu_item("family-heal", tr("Heal / continuous clone")),
                       menu_item("family-carpet", tr("Carpet generator…")),
                       menu_item("family-dither", tr("Dithering brush"))};
    brushes.commands.push_back(std::move(brush));
    gf::RibbonCommandSpec stamp = button("tool-10", tr("Stamp"), 20, Size::large, Kind::split);
    stamp.drop_down = {menu_item("tool-10", tr("Capture / place stamp")), menu_item("sticky-note", tr("Sticky note")),
                       menu_item("stamp-add", tr("Add material")), menu_item("stamp-clear", tr("Clear captured stamp"))};
    brushes.commands.push_back(std::move(stamp));
    gf::RibbonCommandSpec path = button("tool-9", tr("Path"), 21, Size::large, Kind::split);
    path.drop_down = {menu_item("tool-9", tr("Continue path")), menu_item("freehand-shape", tr("Freehand shape")),
                      menu_item("spirograph", tr("Spirograph")),
                      submenu("choice-path-swap", tr("Swap segment"),
                              {menu_item("path-swap-bezier", tr("Bézier")), menu_item("path-swap-arc", tr("Arc"))})};
    brushes.commands.push_back(std::move(path));
    gf::RibbonCommandSpec guide = button("tool-11", tr("Guide"), -1, Size::large, Kind::split);
    guide.image = guide_image();
    guide.drop_down = {menu_item("guide-edit", tr("Edit guide")),
                       submenu("choice-guide-swap", tr("Swap segment"),
                               {menu_item("guide-swap-bezier", tr("Bézier")), menu_item("guide-swap-arc", tr("Arc"))}),
                       menu_item("guide-clear", tr("Unset guide"))};
    brushes.commands.push_back(std::move(guide));
    brushes.layouts = {sized_layout(gf::RibbonGroupSize::large, 0, Size::large, 3),
                       sized_layout(gf::RibbonGroupSize::medium, 4, Size::small, 2)};
    tab.groups.push_back(std::move(brushes));

    gf::RibbonGroupSpec shapes = group("shapes", tr("Shapes"));
    shapes.commands.push_back(gallery_command("shapes-gallery", shapes_));
    shapes.commands.push_back(button("edge-switch", tr("Edge"), -1, Size::small, Kind::toggle));
    shapes.commands.push_back(button("fill-switch", tr("Fill"), -1, Size::small, Kind::toggle));
    // Edge, Fill and Size stand in one column beside the shapes, as in the
    // original.
    shapes.commands.push_back(button("size-menu", tr("Size"), 23, Size::small));
    // Collapsed, the group shows its first shape (its first command is the
    // gallery, which has no image of its own).
    if (shapes_.gallery && !(*shapes_.gallery).items().empty()) {
        shapes.image = (*shapes_.gallery).items()[0].image;
    }
    shapes.layouts = {sized_layout(gf::RibbonGroupSize::large, 0, Size::small, 3), collapsed_layout()};
    tab.groups.push_back(std::move(shapes));


    gf::RibbonGroupSpec colors = group("colors", tr("Colors"));
    gf::RibbonCommandSpec primary = button("primary", tr("Primary"), -1, Size::large, Kind::toggle);
    gf::RibbonCommandSpec secondary = button("secondary", tr("Alt"), -1, Size::large, Kind::toggle);
    const std::map<std::string, gf::ImageId>::const_iterator primary_image = images_.find("primary-material");
    const std::map<std::string, gf::ImageId>::const_iterator secondary_image = images_.find("secondary-material");
    if (primary_image != images_.end()) {
        primary.image = (*primary_image).second;
    }
    if (secondary_image != images_.end()) {
        secondary.image = (*secondary_image).second;
    }
    colors.commands.push_back(std::move(primary));
    colors.commands.push_back(std::move(secondary));
    colors.commands.push_back(gallery_command("palette-gallery", palette_));
    colors.commands.push_back(button("edit-colors", tr("Edit colors"), 24, Size::large));
    // The colours fold last: Primary and Alt stay on the plane at the
    // minimum window, as in the original.
    colors.layouts = {sized_layout(gf::RibbonGroupSize::large, 0, Size::large, 3),
                      sized_layout(gf::RibbonGroupSize::medium, 4, Size::small, 3)};
    tab.groups.push_back(std::move(colors));
    // The minimum window keeps the tools, brushes, path, stamp, guide and
    // the colours on the plane: galleries give way first, then the shapes
    // group folds into a button, then the colours and brushes shrink to
    // small commands.
    // (A gallery step gives up one column: the shapes go from seven columns
    // to three, the palette from ten to four.)
    tab.reduction_order.reserve(18);
    for (int column = 0; column < 6; ++column) {
        if (column < 4) {
            tab.reduction_order.push_back({"shapes", gf::RibbonReductionKind::gallery_column});
        }
        tab.reduction_order.push_back({"colors", gf::RibbonReductionKind::gallery_column});
    }
    const gf::RibbonReductionStep layouts[] = {{"shapes", gf::RibbonReductionKind::layout},
                                               {"colors", gf::RibbonReductionKind::layout},
                                               {"brushes", gf::RibbonReductionKind::layout}};
    tab.reduction_order.insert(tab.reduction_order.end(), std::begin(layouts), std::end(layouts));
    return tab;
}

gf::RibbonTabSpec Ribbon::view_tab() {
    gf::RibbonTabSpec tab;
    tab.stable_id = "view-tab";
    tab.text = tr("View");
    gf::RibbonGroupSpec zoom = group("zoom", tr("Zoom"));
    zoom.commands.push_back(button("zoom-in", tr("Zoom in"), 12, Size::large));
    zoom.commands.push_back(button("zoom-out", tr("Zoom out"), 12, Size::large));
    zoom.commands.push_back(button("actual-size", "100%", 5, Size::large));
    tab.groups.push_back(std::move(zoom));
    gf::RibbonGroupSpec show = group("show", tr("Show or hide"));
    show.commands.push_back(hosted("show-rulers", tr("Rulers"), Kind::check_box));
    show.commands.push_back(hosted("show-grid", tr("Gridlines"), Kind::check_box));
    show.commands.push_back(hosted("show-status", tr("Status bar"), Kind::check_box));
    tab.groups.push_back(std::move(show));
    gf::RibbonGroupSpec display = group("display", tr("Display"));
    display.commands.push_back(button("full-screen", tr("Full screen"), 5, Size::large));
    display.commands.push_back(button("fit", tr("Fit window"), 5, Size::large));
    tab.groups.push_back(std::move(display));
    return tab;
}

gf::RibbonTabSpec Ribbon::materials_tab() {
    gf::RibbonTabSpec tab;
    tab.stable_id = "patterns-tab";
    tab.text = tr("Materials");
    gf::RibbonGroupSpec apply = group("apply-to", tr("Apply to"));
    apply.commands.push_back(button("material-edge", tr("Primary"), -1, Size::small, Kind::toggle));
    apply.commands.push_back(button("material-fill", tr("Alt"), -1, Size::small, Kind::toggle));
    apply.commands.push_back(hosted("alt-carries-body", tr("Alt carries body"), Kind::check_box));
    tab.groups.push_back(std::move(apply));
    gf::RibbonGroupSpec brushes = group("material-brushes", tr("Brushes"));
    // Rendering quality and smooth lines live on the tool tab only: one ID
    // names one command across the ribbon.
    brushes.commands.push_back(gallery_command("brushes-gallery", brushes_));
    tab.groups.push_back(std::move(brushes));
    gf::RibbonGroupSpec patterns = group("material-patterns", tr("Patterns"));
    patterns.commands.push_back(gallery_command("patterns-gallery", patterns_));
    patterns.commands.push_back(button("edit-custom-pattern", tr("Edit custom pattern…"), -1, Size::small));
    tab.groups.push_back(std::move(patterns));
    gf::RibbonGroupSpec grain = group("grain", tr("Grain"));
    grain.layouts = {paired_rows()};
    number(grain, "grain-scale", tr("Grain scale"));
    number(grain, "paper-tooth", tr("Paper tooth"));
    number(grain, "paint-load", tr("Paint load"));
    number(grain, "grain-angle", tr("Grain angle"));
    grain.commands.push_back(button("new-grain", tr("New"), -1, Size::small));
    tab.groups.push_back(std::move(grain));
    // The grain settings stay on the plane: the pattern gallery gives up
    // columns (nine to four) and then the brushes (four to two) first.
    for (int column = 0; column < 5; ++column) {
        tab.reduction_order.push_back({"material-patterns", gf::RibbonReductionKind::gallery_column});
    }
    for (int column = 0; column < 2; ++column) {
        tab.reduction_order.push_back({"material-brushes", gf::RibbonReductionKind::gallery_column});
    }
    return tab;
}

// The tool tab: the settings of the current tool, in the order and grouping
// of the earlier hand-built strip (show_page's visibility rules, written as
// which groups and commands exist).
gf::RibbonTabSpec Ribbon::tool_tab() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    gf::RibbonTabSpec tab;
    tab.stable_id = "tool-tab";
    tab.modes = ordinary_mode;
    if (!editor) {
        tab.text = tr("Tool");
        return tab;
    }
    const Document& document = (*editor).document;
    const Tool tool = document.tool;
    const std::string names[] = {tr("Selection"), tr("Selection"), tr("Pencil"),         tr("Fill"),
                                 tr("Text"),      tr("Eraser"),    tr("Color picker"),   tr("Magnifier"),
                                 tr("Brush"),     tr("Shape"),     tr("Path"),           tr("Stamp"),
                                 tr("Mesh"),      tr("Guide"),     tr("Freehand shape"), tr("Spirograph")};
    static_assert(std::size(names) == static_cast<std::size_t>(Tool::Spirograph) + 1);
    tab.text = names[static_cast<int>(tool)];
    const bool selection = tool == Tool::Select || tool == Tool::Lasso;
    const bool geometry = geometry_tool(tool);
    const bool brush = tool == Tool::Brush;
    const BrushFamily family = (*editor).brush_family;
    const bool placement = (*editor).stamp_placement_active();

    gf::RibbonGroupSpec settings = group("tool-settings", tr("Tool settings"));
    if (geometry || (*editor).spiro.selected_peg >= 0) {
        number(settings, "tool-size", tr("Size (px)"));
    }
    if (geometry) {
        settings.commands.push_back(hosted("outline", tr("Edge"), Kind::check_box));
        settings.commands.push_back(hosted("fill", tr("Fill"), Kind::check_box));
        settings.commands.push_back(hosted("render-quality", tr("Line and shape rendering"), Kind::combo_box, combo_width));
        settings.commands.back().choices = {tr("Crisp"), tr("Smooth"), tr("4× Smooth")};
    }
    if (tool == Tool::Path) {
        settings.commands.push_back(hosted("continuous-path", tr("Continuous path"), Kind::check_box));
        settings.commands.push_back(hosted("edit-path-nodes", tr("Edit nodes"), Kind::check_box));
    }
    if (brush && family == BrushFamily::Additive) {
        settings.commands.push_back(hosted("smooth-lines", tr("Smooth lines"), Kind::check_box));
        if (document.ink.brush == Brush::Airbrush) {
            settings.commands.push_back(hosted("spray-glitter", tr("Fine glitter"), Kind::check_box));
        }
    }
    if (brush || tool == Tool::Pencil) {
        settings.commands.push_back(hosted("stroke-stabilize", tr("Stabilizer"), Kind::check_box));
        number(settings, "stroke-lag", tr("Lag (px)"));
    }
    if (brush && family == BrushFamily::Dither) {
        settings.commands.push_back(hosted("dither-brush-noise", tr("Brightness / saturation noise"), Kind::check_box));
    }
    if (brush && family == BrushFamily::Carpet) {
        settings.commands.push_back(button("carpet-settings", tr("Carpet settings…"), -1, Size::small));
    }
    if (selection) {
        settings.commands.push_back(hosted("transparent-selection", tr("Transparent selection"), Kind::check_box));
        settings.commands.push_back(button("context-select-all", tr("Select all"), -1, Size::small));
        settings.commands.push_back(button("context-invert-selection", tr("Invert"), -1, Size::small));
        settings.commands.push_back(button("context-crop", tr("Crop"), 4, Size::small));
        settings.commands.push_back(button("context-delete", tr("Delete"), -1, Size::small));
    }
    if (tool == Tool::Guide) {
        settings.commands.push_back(button("guide-clear", tr("Clear guide"), -1, Size::small));
        settings.commands.push_back(button("guide-set", tr("Set guide"), -1, Size::small));
        settings.commands.push_back(hosted("guide-fill", tr("Protect body"), Kind::check_box));
    }
    if (tool == Tool::Magnifier) {
        settings.commands.push_back(button("context-zoom-in", tr("Zoom in"), 12, Size::large));
        settings.commands.push_back(button("context-zoom-out", tr("Zoom out"), 12, Size::large));
    }
    if (tool == Tool::Picker) {
        settings.commands.push_back(hosted("picker-mode", tr("Eyedropper sampling"), Kind::combo_box, 240));
        settings.commands.back().choices = {tr("Exact pixel"), tr("Small average (5 × 5)"),
                                            tr("Representative (reject flecks)")};
        settings.commands.push_back(hosted("picker-magnifier", tr("Local 8× magnifier"), Kind::check_box));
    }
    if (tool == Tool::Stamp) {
        if (placement) {
            settings.commands.push_back(button("context-stamp-apply", tr("Apply"), -1, Size::small));
            settings.commands.push_back(button("context-stamp-cancel", tr("Cancel"), -1, Size::small));
        } else {
            gf::RibbonCommandSpec shapes =
                button("stamp-shapes-menu", stamp_shape_name(document.stamp_shape),
                       100 + static_cast<int>(stamp_geometry_shape(document.stamp_shape)), Size::large);
            settings.commands.push_back(std::move(shapes));
            settings.commands.push_back(hosted("stamp-transparent", tr("Transparent stamp"), Kind::check_box));
            settings.commands.push_back(button("context-stamp-clear", tr("Clear stamp"), -1, Size::small));
            settings.commands.push_back(button("context-stamp-add", tr("Add material"), -1, Size::small));
        }
    }
    if (!settings.commands.empty()) {
        settings.layouts = {paired_rows()};
        tab.groups.push_back(std::move(settings));
    }

    if (tool == Tool::Stamp) {
        gf::RibbonGroupSpec size = group("stamp-size", tr("Stamp size"));
        size.layouts = {paired_rows()};
        if (!placement) {
            number(size, "stamp-width", tr("Width (px)"));
            number(size, "stamp-height", tr("Height (px)"));
        }
        number(size, "stamp-scale", tr("Scale"));
        number(size, "stamp-angle", tr("Angle"));
        number(size, "stamp-hardness", tr("Hardness"));
        tab.groups.push_back(std::move(size));
    }
    const bool mix = brush && family == BrushFamily::Mix;
    if (mix || (tool == Tool::Eraser && (*editor).eraser_mode >= EraserMode::Blur)) {
        gf::RibbonGroupSpec effect = group("effect", tr("Effect"));
        effect.layouts = {paired_rows()};
        if (mix) {
            gf::RibbonCommandSpec effects = hosted("mix-effect", tr("Mix effect"), Kind::combo_box, combo_width);
            for (int index = 0; index < mix_effect_count; ++index) {
                effects.choices.push_back(mix_effect_name(static_cast<MixEffect>(index)));
            }
            effect.commands.push_back(std::move(effects));
        }
        number(effect, "effect-strength", tr("Strength"));
        number(effect, "effect-scale", tr("Scale (px)"));
        number(effect, "effect-phase", tr("Phase"));
        tab.groups.push_back(std::move(effect));
    }
    if (brush && family == BrushFamily::Heal) {
        gf::RibbonGroupSpec heal = group("heal", tr("Healing"));
        heal.layouts = {paired_rows()};
        heal.commands.push_back(button("heal-source", tr("Set source"), -1, Size::small, Kind::toggle));
        number(heal, "heal-hardness", tr("Hardness"));
        number(heal, "heal-correction", tr("Color match"));
        heal.commands.push_back(hosted("heal-clone", tr("Clone exactly"), Kind::check_box));
        heal.commands.push_back(hosted("heal-aligned", tr("Aligned source"), Kind::check_box));
        heal.commands.push_back(hosted("heal-alpha", tr("Preserve transparency"), Kind::check_box));
        tab.groups.push_back(std::move(heal));
    }
    if (geometry) {
        gf::RibbonGroupSpec shape = group("geometry", tr("Geometry"));
        shape.layouts = {paired_rows()};
        number(shape, "geometry-corners", tr("Corner radius (px)"));
        number(shape, "geometry-organic", tr("Organic"));
        shape.commands.push_back(button("geometry-vary", tr("Vary"), -1, Size::small));
        shape.commands.push_back(button("geometry-stroke-label", tr("Stroke style"), -1, Size::small));
        shape.commands.push_back(hosted("geometry-stroke", tr("Stroke style"), Kind::combo_box, 140));
        shape.commands.back().choices = {tr("Solid"), tr("Dashed"), tr("Dotted")};
        const Shape current = document.shape;
        if (tool == Tool::Path ||
            (tool == Tool::Shape && (current == Shape::Line || current == Shape::Bezier || current == Shape::Arc))) {
            const std::vector<std::string> markers{tr("None"),        tr("Open arrow"),  tr("Triangle"),
                                                   tr("Diamond"),     tr("Circle"),      tr("Bar"),
                                                   tr("Exactly one"), tr("Many"),        tr("Zero or one"),
                                                   tr("One or more"), tr("Zero or more")};
            shape.commands.push_back(button("geometry-start-label", tr("Start marker"), -1, Size::small));
            shape.commands.push_back(hosted("geometry-start", tr("Start marker"), Kind::combo_box, 160));
            shape.commands.back().choices = markers;
            shape.commands.push_back(button("geometry-end-label", tr("End marker"), -1, Size::small));
            shape.commands.push_back(hosted("geometry-end", tr("End marker"), Kind::combo_box, 160));
            shape.commands.back().choices = markers;
        }
        tab.groups.push_back(std::move(shape));
    }
    if (selection && document.selection.active) {
        gf::RibbonGroupSpec rotation = group("rotation", tr("Rotation"));
        rotation.layouts = {paired_rows()};
        number(rotation, "rotation-degrees", tr("Angle (°)"));
        rotation.commands.push_back(button("rotate-custom", tr("Apply rotation"), 6, Size::large));
        tab.groups.push_back(std::move(rotation));
        gf::RibbonGroupSpec mesh = group("mesh", tr("Mesh"));
        mesh.layouts = {paired_rows()};
        number(mesh, "mesh-spacing", tr("Spacing (px)"));
        mesh.commands.push_back(button("context-reshape", tr("Start mesh"), 21, Size::large));
        tab.groups.push_back(std::move(mesh));
        gf::RibbonGroupSpec finish = group("finish", tr("Finish"));
        finish.commands.push_back(button("warp-place", tr("Place"), -1, Size::small));
        finish.commands.push_back(button("warp-cancel", tr("Cancel"), -1, Size::small));
        tab.groups.push_back(std::move(finish));
    }
    return tab;
}

gf::RibbonTabSpec Ribbon::text_tab() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const bool sticky = editor && (*editor).text.active && (*editor).text.note.has_value();
    gf::RibbonTabSpec tab;
    tab.stable_id = "text-tab";
    tab.text = sticky ? tr("Sticky note") : tr("Text");
    tab.tab_set = "text-tools";
    gf::RibbonGroupSpec font = group("text-font-group", tr("Font"));
    font.layouts = {paired_rows()};
    gf::RibbonCommandSpec faces = hosted("text-font", tr("Font family"), Kind::combo_box, 200);
    faces.choices = {"Portsmouth", "Portsmouth Mono"};
    for (const std::string& path : font_paths_) {
        faces.choices.push_back(font_label(path));
    }
    font.commands.push_back(std::move(faces));
    number(font, "text-size", tr("Font size"));
    font.commands.push_back(hosted("text-bold", tr("Bold"), Kind::check_box));
    font.commands.push_back(hosted("text-italic", tr("Italic"), Kind::check_box));
    font.commands.push_back(hosted("text-underline", tr("Underline"), Kind::check_box));
    font.commands.push_back(hosted("text-strikeout", tr("Strikeout"), Kind::check_box));
    if (!sticky) {
        font.commands.push_back(hosted("text-opaque", tr("Opaque"), Kind::check_box));
    }
    font.commands.push_back(hosted("text-wrap", tr("Wrap"), Kind::check_box));
    tab.groups.push_back(std::move(font));
    if (sticky) {
        sticky_groups(tab);
    } else {
        gf::RibbonGroupSpec letters = group("text-letters", tr("Letter treatment"));
        letters.layouts = {paired_rows()};
        letters.commands.push_back(hosted("text-contour", tr("Outline letters"), Kind::check_box));
        number(letters, "text-outline-width", tr("Outline (px)"));
        letters.commands.push_back(hosted("text-word-art", tr("WordArt style"), Kind::combo_box, 160));
        letters.commands.back().choices = {tr("Plain"), tr("3D extrusion"), tr("Embossed"), tr("Color gradient")};
        tab.groups.push_back(std::move(letters));
        gf::RibbonGroupSpec transform = group("text-transform", tr("Transform"));
        transform.layouts = {paired_rows()};
        number(transform, "text-skew", tr("Skew"));
        number(transform, "text-perspective", tr("Perspective"));
        number(transform, "text-warp", tr("Bend"));
        tab.groups.push_back(std::move(transform));
    }
    gf::RibbonGroupSpec colors = group("text-colors", tr("Colors"));
    colors.commands.push_back(button("text-primary", tr("Primary"), 24, Size::large));
    if (!sticky) {
        colors.commands.push_back(button("text-secondary", tr("Alt"), 24, Size::large));
        colors.layouts = {sized_layout(gf::RibbonGroupSize::large, 0, Size::large, 3), collapsed_layout()};
    }
    tab.groups.push_back(std::move(colors));
    gf::RibbonGroupSpec finish = group("text-finish", tr("Finish text"));
    // The original's two-by-two block: Place text and Cancel on the upper
    // row, Fit height and Reset below (stacks fill column by column).
    finish.commands.push_back(button("text-place", tr("Place text"), -1, Size::small));
    finish.commands.push_back(button("text-fit", tr("Fit height"), -1, Size::small));
    finish.commands.push_back(button("text-cancel", tr("Cancel"), -1, Size::small));
    if (!sticky) {
        finish.commands.push_back(button("text-reset-effects", tr("Reset"), -1, Size::small));
    }
    finish.layouts = {sized_layout(gf::RibbonGroupSize::large, 0, Size::small, 2)};
    tab.groups.push_back(std::move(finish));
    // Finishing and the font stay on the plane longest: the transform, the
    // letter treatment and the colours fold into buttons first.
    // (A sticky note's colours are one command, which never folds.)
    if (!sticky) {
        tab.reduction_order.push_back({"text-transform", gf::RibbonReductionKind::layout});
        tab.reduction_order.push_back({"text-letters", gf::RibbonReductionKind::layout});
        tab.reduction_order.push_back({"text-colors", gf::RibbonReductionKind::layout});
    }
    return tab;
}

gf::RibbonTabSpec Ribbon::spiro_tab() {
    gf::RibbonTabSpec tab;
    tab.stable_id = "spiro-tab";
    tab.text = tr("Spirograph");
    tab.tab_set = "spiro-tools";
    gf::RibbonGroupSpec guides = group("spiro-guides", tr("Guides"));
    for (int guide : {0, 16}) {
        guides.commands.push_back(button("spiro-guide-" + std::to_string(guide), tr(spiro_guides()[guide].name), -1,
                                         Size::large, Kind::toggle));
    }
    guides.commands.push_back(button("spiro-guides-menu", tr("More"), -1, Size::small));
    tab.groups.push_back(std::move(guides));
    gf::RibbonGroupSpec inserts = group("spiro-inserts", tr("Inserts"));
    for (int insert : {1, spiro_default_insert, 2}) {
        inserts.commands.push_back(button("spiro-insert-" + std::to_string(insert),
                                          tr(spiro_inserts()[insert].name), -1, Size::large, Kind::toggle));
    }
    inserts.commands.push_back(button("spiro-inserts-menu", tr("More"), -1, Size::small));
    tab.groups.push_back(std::move(inserts));
    gf::RibbonGroupSpec pegs = group("spiro-pegs", tr("Pegs · drag into a hole"));
    pegs.commands.push_back(button("spiro-peg-1", tr("Fine peg"), -1, Size::large));
    pegs.commands.push_back(button("spiro-peg-2", tr("Medium peg"), -1, Size::large));
    pegs.commands.push_back(button("spiro-peg-4", tr("Bold peg"), -1, Size::large));
    pegs.commands.push_back(button("spiro-brushes-menu", tr("Peg medium"), -1, Size::small));
    tab.groups.push_back(std::move(pegs));
    gf::RibbonGroupSpec operate = group("spiro-operate-group", tr("Load and operate"));
    operate.commands.push_back(button("spiro-fill", tr("Fill pegs"), 8, Size::small, Kind::toggle));
    operate.commands.push_back(button("spiro-remove", tr("Remove insert"), -1, Size::small));
    operate.commands.push_back(button("spiro-outside", tr("Roll outside"), -1, Size::small, Kind::toggle));
    operate.commands.push_back(button("spiro-operate", tr("Operate"), -1, Size::small, Kind::toggle));
    operate.commands.push_back(button("spiro-center", tr("Center guide"), -1, Size::small));
    operate.commands.push_back(button("spiro-deselect", tr("Deselect peg"), -1, Size::small));
    tab.groups.push_back(std::move(operate));
    gf::RibbonGroupSpec wheel = group("spiro-wheel", tr("Wheel"));
    wheel.layouts = {paired_rows()};
    number(wheel, "spiro-scale", tr("Scale"));
    wheel.commands.push_back(button("spiro-lift", tr("Lift wheel"), -1, Size::small, Kind::toggle));
    wheel.commands.push_back(button("spiro-holes-menu", tr("Choose hole"), -1, Size::small));
    wheel.commands.push_back(button("spiro-load-primary", tr("Load Primary"), -1, Size::small));
    wheel.commands.push_back(button("spiro-load-alt", tr("Load Alt"), -1, Size::small));
    tab.groups.push_back(std::move(wheel));
    return tab;
}

gf::RibbonTabSpec Ribbon::gradient_tab() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    gf::RibbonTabSpec tab;
    tab.stable_id = "gradient-tab";
    tab.text = tr("Gradients");
    tab.tab_set = "gradient-tools";
    gf::RibbonGroupSpec presets = group("gradient-presets", tr("Presets"));
    presets.commands.push_back(gallery_command("gradient-presets-gallery", gradient_presets_));
    tab.groups.push_back(std::move(presets));
    gf::RibbonGroupSpec stops = group("gradient-stops-group", tr("Color stops"));
    stops.layouts = {paired_rows()};
    stops.commands.push_back(button("gradient-stops-menu", tr("Edit stops"), -1, Size::large));
    gf::RibbonCommandSpec stop = hosted("gradient-stop", tr("Selected gradient stop"), Kind::combo_box, 110);
    if (editor) {
        for (std::size_t index = 0; index < (*editor).fill_gradient.stops.size(); ++index) {
            stop.choices.push_back(tr("Stop ") + std::to_string(index + 1));
        }
    }
    stops.commands.push_back(button("gradient-stop-label", tr("Stop"), -1, Size::small));
    stops.commands.push_back(std::move(stop));
    number(stops, "gradient-position", tr("Position (%)"));
    stops.commands.push_back(button("gradient-edit-color", tr("Color…"), -1, Size::small));
    stops.commands.push_back(button("gradient-add", tr("Add"), -1, Size::small));
    stops.commands.push_back(button("gradient-remove", tr("Remove"), -1, Size::small));
    tab.groups.push_back(std::move(stops));
    gf::RibbonGroupSpec direction = group("gradient-direction", tr("Direction"));
    direction.layouts = {paired_rows()};
    direction.commands.push_back(button("gradient-linear", tr("Linear"), -1, Size::small, Kind::toggle));
    direction.commands.push_back(button("gradient-radial", tr("Circular"), -1, Size::small, Kind::toggle));
    number(direction, "gradient-angle", tr("Angle (°)"));
    direction.commands.push_back(button("gradient-reverse", tr("Reverse"), -1, Size::small));
    tab.groups.push_back(std::move(direction));
    return tab;
}

std::vector<gf::RibbonTabSpec> Ribbon::build_tabs() {
    std::vector<gf::RibbonTabSpec> tabs;
    tabs.reserve(9);
    tabs.push_back(home_tab());
    tabs.push_back(view_tab());
    tabs.push_back(materials_tab());
    tabs.push_back(tool_tab());
    gf::RibbonTabSpec atlas;
    atlas.stable_id = "atlas-tab";
    atlas.text = tr("Atlas");
    tabs.push_back(std::move(atlas));
    tabs.push_back(text_tab());
    tabs.push_back(spiro_tab());
    tabs.push_back(gradient_tab());
    gf::RibbonTabSpec icon_tab;
    icon_tab.stable_id = "icon-tab";
    icon_tab.text = tr("Icon");
    icon_tab.tab_set = "icon-tools";
    tabs.push_back(std::move(icon_tab));
    // The original tab row's widths (the earlier strip's tab buttons); the
    // contextual text and spirograph tabs keep their labels' widths.
    const std::pair<const char*, double> widths[] = {{"home-tab", 61},  {"view-tab", 63},   {"patterns-tab", 100},
                                                     {"tool-tab", 142}, {"atlas-tab", 72},  {"icon-tab", 72},
                                                     {"gradient-tab", 105}};
    for (gf::RibbonTabSpec& tab : tabs) {
        for (const std::pair<const char*, double>& width : widths) {
            if (tab.stable_id == width.first) {
                tab.width = width.second;
            }
        }
    }
    return tabs;
}

// Gives set_tabs the structure for the editor's state when it differs from
// the last, then binds the hosted controls it made.
void Ribbon::apply_tabs() {
    std::vector<gf::RibbonTabSpec> tabs = build_tabs();
    std::string signature;
    for (const gf::RibbonTabSpec& tab : tabs) {
        signature += "|" + tab.stable_id + "=" + tab.text;
        for (const gf::RibbonGroupSpec& each : tab.groups) {
            // Image IDs, not their presence: a replaced artwork has a new ID.
            signature += "/" + each.stable_id + "@" + std::to_string(each.image.value);
            for (const gf::RibbonCommandSpec& spec : each.commands) {
                signature += "," + spec.stable_id + "@" + std::to_string(spec.image.value);
                for (const std::string& choice : spec.choices) {
                    signature += ";" + choice;
                }
            }
        }
    }
    if (signature == tabs_signature_) {
        return;
    }
    std::vector<gf::RibbonTabSetSpec> sets = {{"text-tools", tr("Text tools")},
                                              {"spiro-tools", tr("Spirograph tools")},
                                              {"gradient-tools", tr("Gradient tools")},
                                              {"icon-tools", tr("Icon tools")}};
    (*bar_).set_tabs(std::move(tabs), std::move(sets));
    tabs_signature_ = signature;
    bind_hosted();
}

// Finds the hosted controls of the current tabs: configures and subscribes
// each new one, and clears the members of commands no longer shown.
void Ribbon::bind_hosted() {
    static const NumberSpec numbers[] = {
        {"grain-scale", &Ribbon::grain_, 0.3, 4, 1, 2},
        {"paper-tooth", &Ribbon::tooth_, 0, 1, 0.65, 2},
        {"paint-load", &Ribbon::load_, 0, 1, 0.65, 2},
        {"grain-angle", &Ribbon::angle_, -180, 180, -20, 0},
        {"tool-size", &Ribbon::tool_size_, 1, 1024, 3, 0},
        {"geometry-corners", &Ribbon::corner_radius_, 0, 4096, 0, 1},
        {"geometry-organic", &Ribbon::geometry_organic_, 0, 3, 0, 2},
        {"stamp-width", &Ribbon::stamp_width_, 1, 1024, 80, 0},
        {"stamp-height", &Ribbon::stamp_height_, 1, 1024, 80, 0},
        {"stamp-scale", &Ribbon::stamp_scale_, 0.1, 8, 1, 2},
        {"stamp-angle", &Ribbon::stamp_angle_, -360, 360, 0, 1},
        {"stamp-hardness", &Ribbon::stamp_hardness_, 0, 1, 1, 2},
        {"effect-strength", &Ribbon::effect_strength_, 0, 1, 0.75, 2},
        {"effect-scale", &Ribbon::effect_scale_, 2, 128, 24, 0},
        {"effect-phase", &Ribbon::effect_phase_, -10, 10, 0, 2},
        {"heal-hardness", &Ribbon::heal_hardness_, 0, 1, 0.35, 2},
        {"heal-correction", &Ribbon::heal_correction_, 0, 1, 1, 2},
        {"stroke-lag", &Ribbon::stabilizer_lag_, 0.1, 50, 5, 1},
        {"rotation-degrees", &Ribbon::rotation_, -360, 360, 15, 1},
        {"mesh-spacing", &Ribbon::mesh_spacing_, 8, 256, 60, 0},
        {"text-size", &Ribbon::text_size_, 6, 300, 24, 0},
        {"text-outline-width", &Ribbon::text_outline_, 1, 8, 2, 0},
        {"text-skew", &Ribbon::text_skew_, -0.7, 0.7, 0, 2},
        {"text-perspective", &Ribbon::text_perspective_, -0.8, 0.8, 0, 2},
        {"text-warp", &Ribbon::text_warp_, -0.4, 0.4, 0, 2},
        {"spiro-scale", &Ribbon::spiro_scale_, 0.05, 64, 1.35, 2},
        {"sticky-padding", &Ribbon::sticky_padding_, 0, 128, 16, 0},
        {"sticky-width", &Ribbon::sticky_width_, 24, 4096, 256, 0},
        {"sticky-height", &Ribbon::sticky_height_, 24, 4096, 192, 0},
        {"gradient-angle", &Ribbon::gradient_angle_, 0, 360, 0, 1},
        {"gradient-position", &Ribbon::gradient_position_, 0, 100, 0, 1},
    };
    struct ComboSpec {
        const char* id;
        std::shared_ptr<gf::ComboBox> Ribbon::*member;
    };
    static const ComboSpec combos[] = {
        {"geometry-stroke", &Ribbon::geometry_stroke_}, {"geometry-start", &Ribbon::geometry_start_},
        {"geometry-end", &Ribbon::geometry_end_},       {"text-font", &Ribbon::font_},
        {"text-word-art", &Ribbon::word_art_},          {"picker-mode", &Ribbon::picker_mode_},
        {"mix-effect", &Ribbon::mix_effect_},           {"render-quality", &Ribbon::render_quality_},
        {"sticky-paper", &Ribbon::sticky_paper_},       {"gradient-stop", &Ribbon::gradient_stops_},
    };
    // Which hosted commands the current tabs hold, by ID and kind.
    std::map<std::string, Kind> present;
    for (const gf::RibbonTabSpec& tab : (*bar_).tabs()) {
        for (const gf::RibbonGroupSpec& each : tab.groups) {
            for (const gf::RibbonCommandSpec& spec : each.commands) {
                if (spec.kind == Kind::numeric_up_down || spec.kind == Kind::combo_box ||
                    spec.kind == Kind::check_box || spec.kind == Kind::text_box) {
                    present.emplace(spec.stable_id, spec.kind);
                }
            }
        }
    }
    for (std::map<std::string, HostedEntry>::iterator entry = hosted_.begin(); entry != hosted_.end();) {
        if (present.find((*entry).first) == present.end()) {
            entry = hosted_.erase(entry);
        } else {
            ++entry;
        }
    }
    for (const NumberSpec& spec : numbers) {
        (*this).*spec.member = nullptr;
        if (present.find(spec.id) == present.end()) {
            continue;
        }
        const std::shared_ptr<gf::NumericUpDown> control = (*bar_).numeric_up_down(spec.id);
        (*this).*spec.member = control;
        HostedEntry& entry = hosted_[spec.id];
        if (entry.control == control) {
            continue;
        }
        (*control).set_range(spec.minimum, spec.maximum);
        (*control).decimal_places = static_cast<std::uint8_t>(spec.decimals);
        (*control).increment = spec.decimals == 2 ? 0.05 : 1.0;
        (*control).value = spec.value;
        const std::string id = spec.id;
        entry.control = control;
        entry.changed = (*control).value_changed().subscribe(
            *this, id.starts_with("gradient-") ? gf::Delegate<double>::bind<Ribbon, &Ribbon::gradient_number_changed>(*this)
                   : id == "spiro-scale"       ? gf::Delegate<double>::bind<Ribbon, &Ribbon::spiro_scale_changed>(*this)
                                               : gf::Delegate<double>::bind<Ribbon, &Ribbon::options_changed>(*this));
    }
    for (const ComboSpec& spec : combos) {
        (*this).*spec.member = nullptr;
        if (present.find(spec.id) == present.end()) {
            continue;
        }
        const std::shared_ptr<gf::ComboBox> control = (*bar_).combo_box(spec.id);
        (*this).*spec.member = control;
        HostedEntry& entry = hosted_[spec.id];
        if (entry.control == control) {
            continue;
        }
        const std::string id = spec.id;
        if (id == "geometry-start" || id == "geometry-end") {
            (*control).drop_down_width = 240;
            (*control).maximum_drop_down_items = endpoint_marker_count;
        }
        if (id == "text-font") {
            (*control).maximum_drop_down_items = 12;
            (*control).drop_down_width = 320;
        }
        entry.control = control;
        entry.changed = (*control).selected_index_changed().subscribe(
            *this, id == "text-font"       ? gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::font_changed>(*this)
                   : id == "sticky-paper"  ? gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::sticky_paper_changed>(*this)
                   : id == "gradient-stop" ? gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::gradient_stop_changed>(*this)
                                           : gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::choice_changed>(*this));
    }
    sticky_color_ = nullptr;
    for (const std::pair<const std::string, Kind>& each : present) {
        HostedEntry& entry = hosted_[each.first];
        if (each.second == Kind::check_box) {
            const std::shared_ptr<gf::CheckBox> control = (*bar_).check_box(each.first);
            if (entry.control != control) {
                (*control).auto_check = false;
                entry.control = control;
                entry.changed = (*control).clicked().subscribe(
                    *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::check_clicked>(*this));
            }
        } else if (each.second == Kind::text_box) {
            const std::shared_ptr<gf::TextBox> control = (*bar_).text_box(each.first);
            if (each.first == "sticky-color") {
                sticky_color_ = control;
            }
            if (entry.control != control) {
                (*control).maximum_length = 7;
                entry.control = control;
                entry.changed = (*control).text_changed().subscribe(
                    *this, gf::Delegate<const std::string&>::bind<Ribbon, &Ribbon::sticky_color_changed>(*this));
            }
        }
    }
}
} // namespace paint::forms

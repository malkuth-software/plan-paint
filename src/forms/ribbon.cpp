// Plan Paint's ribbon: the themes and swatches the colour dialogs share, and
// the ribbon's behaviour (ribbon.hpp; its structure is in ribbon_tabs.cpp,
// its galleries in ribbon_galleries.cpp and its own pickers in
// ribbon_popups.cpp).
#include "forms/ribbon.hpp"
#include "codecs.hpp"
#include "forms/atlas.hpp"
#include "forms/editor.hpp"
#include "forms/icon_ribbon.hpp"
#include "forms/interface_theme.hpp"
#include "localization.hpp"
#include "material.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <gui_forms/delegate.hpp>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
const Tool ribbon_tools[] = {Tool::Select, Tool::Lasso,  Tool::Pencil,    Tool::Fill,
                             Tool::Eraser, Tool::Picker, Tool::Magnifier, Tool::Brush,
                             Tool::Shape,  Tool::Path,   Tool::Stamp,     Tool::Guide};
gf::SurfaceMaterial gallery_material(bool selected, bool hot, bool pressed) {
    gf::SurfaceMaterial material;
    material.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0, 0))};
    if (selected || hot || pressed) {
        gf::Color top = selected ? gf::Color::rgba(255, 237, 186) : gf::Color::rgba(255, 249, 228);
        gf::Color bottom = selected ? gf::Color::rgba(255, 207, 102) : gf::Color::rgba(255, 228, 157);
        if (pressed) {
            top = gf::Color::rgba(246, 193, 91);
            bottom = gf::Color::rgba(255, 226, 151);
        }
        material.fills.push_back(gf::MaterialFillLayer::linear({0, 0}, {0, 1}, {{0, top}, {1, bottom}}));
        material.border =
            gf::MaterialBorder{selected ? gf::Color::rgba(196, 147, 50) : gf::Color::rgba(219, 182, 109), 1};
        material.corner_radius = 2;
        material.keylines.push_back({gf::MaterialEdge::top, gf::Color::rgba(255, 255, 240), 1, 1});
        material.keylines.push_back({gf::MaterialEdge::left, gf::Color::rgba(255, 255, 240, 180), 1, 1});
        material.keylines.push_back({gf::MaterialEdge::bottom, gf::Color::rgba(160, 109, 28, 35), 1, 1});
    }
    return material;
}
} // namespace
Color ribbon_color(int index) {
    return office_color(std::clamp(index, 0, 29));
}
static std::shared_ptr<const gf::Theme> make_gallery_theme() {
    gf::ThemeDefinition definition = gf::windows_professional_theme_definition();
    definition.id = "rainstar-classic-ribbon";
    definition.structure.typography.control = {gf::FontRole::control, 14, 400, false, 0};
    definition.structure.typography.field = {gf::FontRole::content, 14, 400, false};
    for (gf::ControlVisualRole role :
         {gf::ControlVisualRole::button, gf::ControlVisualRole::command_button}) {
        gf::ControlRoleRecipes& recipes = definition.roles[static_cast<std::size_t>(role)];
        for (std::size_t state = 0; state < gf::control_surface_state_count; ++state) {
            bool hot = state == static_cast<std::size_t>(gf::ControlSurfaceState::hot);
            bool pressed = state == static_cast<std::size_t>(gf::ControlSurfaceState::pressed);
            recipes.ordinary[state].material = gallery_material(false, hot, pressed);
            recipes.selected[state].material = gallery_material(true, hot, pressed);
            recipes.ordinary[state].pressed_content_offset = {};
            recipes.selected[state].pressed_content_offset = {};
        }
    }
    return gf::Theme::create(std::move(definition));
}
std::shared_ptr<const gf::Theme> gallery_theme() {
    static const std::shared_ptr<const gf::Theme> theme = make_gallery_theme();
    return theme;
}
namespace {
gf::SurfaceMaterial action_material(bool selected, gf::ControlSurfaceState state) {
    const bool disabled = state == gf::ControlSurfaceState::disabled;
    const bool pressed = state == gf::ControlSurfaceState::pressed;
    const bool hot = state == gf::ControlSurfaceState::hot;
    if (selected && !disabled) {
        return gallery_material(true, hot, pressed);
    }
    gf::SurfaceMaterial material;
    material.corner_radius = 3;
    const gf::Color top = disabled   ? gf::Color::rgba(190, 207, 228)
                          : pressed  ? gf::Color::rgba(152, 181, 218)
                          : selected ? gf::Color::rgba(188, 210, 241)
                          : hot      ? gf::Color::rgba(218, 233, 252)
                                     : gf::Color::rgba(210, 228, 251);
    const gf::Color bottom = disabled   ? gf::Color::rgba(181, 199, 222)
                             : pressed  ? gf::Color::rgba(181, 204, 235)
                             : selected ? gf::Color::rgba(153, 181, 222)
                             : hot      ? gf::Color::rgba(177, 204, 242)
                                        : gf::Color::rgba(163, 193, 236);
    material.fills = {gf::MaterialFillLayer::linear({0, 0}, {0, 1}, {{0, top}, {1, bottom}})};
    material.border = gf::MaterialBorder{disabled ? gf::Color::rgba(143, 166, 196, 100)
                                         : hot    ? gf::Color::rgba(53, 94, 150)
                                                  : gf::Color::rgba(99, 132, 178),
                                         1};
    const gf::Color upper = disabled  ? gf::Color::rgba(255, 255, 255, 100)
                            : pressed ? gf::Color::rgba(44, 86, 132, 130)
                                      : gf::Color::rgba(255, 255, 255, selected ? 185 : 230);
    const gf::Color lower =
        pressed ? gf::Color::rgba(255, 255, 255, 155) : gf::Color::rgba(49, 86, 134, selected ? 110 : 135);
    material.keylines.push_back({gf::MaterialEdge::top, upper, 1, 1});
    if (!disabled) {
        material.keylines.push_back({gf::MaterialEdge::left, upper, 1, 1});
        material.keylines.push_back({gf::MaterialEdge::right, lower, 1, 1});
        material.keylines.push_back({gf::MaterialEdge::bottom, lower, 1, 1});
        if (!pressed) {
            material.shadows.push_back({{0, 1}, 0, 0, gf::Color::rgba(41, 75, 119, 85), false});
        }
    }
    return material;
}
std::shared_ptr<const gf::Theme> make_cobalt_theme() {
    gf::ThemeDefinition definition = gf::windows_professional_theme_definition();
    definition.id = "rainstar-royale-cobalt";
    definition.structure.typography.control = {gf::FontRole::control, 14, 400, false, 0};
    definition.structure.typography.field = {gf::FontRole::content, 14, 400, false};
    definition.compatibility.border = gf::Color::rgba(99, 132, 178);
    definition.compatibility.text = gf::Color::rgba(21, 55, 101);
    definition.compatibility.disabled_text = gf::Color::rgba(98, 121, 151);
    for (gf::ControlVisualRole role : {gf::ControlVisualRole::button, gf::ControlVisualRole::command_button,
                                       gf::ControlVisualRole::accent_button}) {
        gf::ControlRoleRecipes& recipes = definition.roles[static_cast<std::size_t>(role)];
        for (std::size_t state = 0; state < gf::control_surface_state_count; ++state) {
            const gf::ControlSurfaceState surface = static_cast<gf::ControlSurfaceState>(state);
            for (int selected = 0; selected < 2; ++selected) {
                gf::ControlVisualRecipe& recipe =
                    selected ? recipes.selected[state] : recipes.ordinary[state];
                recipe.material = action_material(selected != 0, surface);
                recipe.text = surface == gf::ControlSurfaceState::disabled ? gf::Color::rgba(98, 121, 151)
                                                                           : gf::Color::rgba(21, 55, 101);
                recipe.glyph = recipe.text;
                recipe.focus_ring = gf::Color::rgba(20, 60, 116);
                recipe.default_ring = gf::Color::rgba(33, 75, 129);
                recipe.default_width = 1;
                recipe.pressed_content_offset = {1, 1};
            }
        }
    }
    return gf::Theme::create(std::move(definition));
}
} // namespace
std::shared_ptr<const gf::Theme> ribbon_theme() {
    static const std::shared_ptr<const gf::Theme> theme = make_cobalt_theme();
    return theme;
}
OpacityIndicator opacity_indicator(Color color) {
    OpacityIndicator indicator;
    indicator.percent = color.a == 255 ? 100 : color.a == 0 ? 0 :
        std::clamp((static_cast<int>(color.a) * 100 + 127) / 255, 1, 99);
    if (color.a < 255) { indicator.text = std::to_string(indicator.percent) + "%"; }
    indicator.foreground = {static_cast<std::uint8_t>(255 - color.r),
                           static_cast<std::uint8_t>(255 - color.g),
                           static_cast<std::uint8_t>(255 - color.b), 255};
    const Color foreground = indicator.foreground;
    const int brightness = 299 * foreground.r + 587 * foreground.g + 114 * foreground.b;
    indicator.outline = brightness >= 128000 ? Color{0, 0, 0, 255} : Color{255, 255, 255, 255};
    return indicator;
}
SwatchButton::SwatchButton(gf::StableId id, std::string text, Color color)
    : Button(std::move(id), std::move(text)), color_(color) {
    if ((*this).text.read().empty()) {
        set_theme_override(gallery_theme());
    }
}
void SwatchButton::set_color(Color color) {
    if (equal(color_, color)) { return; }
    color_ = color;
    update_opacity_description();
    invalidate(gf::Dirty::paint);
}
void SwatchButton::set_show_opacity(bool show) {
    show_opacity_ = show;
    update_opacity_description();
    invalidate(gf::Dirty::paint);
}
void SwatchButton::update_opacity_description() {
    const OpacityIndicator indicator = opacity_indicator(color_);
    accessible_description = show_opacity_ ? tr("Opacity") + ": " +
        std::to_string(indicator.percent) + "% (" + std::to_string(color_.a) + "/255)" : "";
}
static bool same_material(const Ink& left, const Ink& right) {
    return equal(left.primary, right.primary) && equal(left.secondary, right.secondary) &&
           left.pattern == right.pattern && left.brush == right.brush &&
           (left.pattern != Pattern::Custom || (left.custom_pattern == right.custom_pattern &&
                                               left.custom_pattern_revision == right.custom_pattern_revision)) &&
           left.transparent_pattern == right.transparent_pattern && left.noise == right.noise &&
           left.grain_scale == right.grain_scale && left.paper_roughness == right.paper_roughness &&
           left.pigment_load == right.pigment_load && left.material_angle == right.material_angle &&
           left.smooth == right.smooth && left.supersample == right.supersample && left.size == right.size &&
           static_cast<bool>(left.alternate) == static_cast<bool>(right.alternate) &&
           (!left.alternate || same_material(*left.alternate, *right.alternate));
}
void SwatchButton::set_material(const Ink& ink) {
    set_color(ink.primary);
    accessible_name = text.read() + ": " +
                        (ink.brush == Brush::Round ? tr(pattern_names[static_cast<int>(ink.pattern)])
                                                   : tr(brush_names[static_cast<int>(ink.brush)]));
    if (!attached_window() || (material_ink_ && same_material(*material_ink_, ink))) {
        return;
    }
    material_ink_ = ink;
    Image sample;
    sample.reset(48, 36, {255, 255, 255, 255});
    for (int y = 0; y < sample.height; ++y) {
        for (int x = 0; x < sample.width; ++x) {
            if (((x / 6) + (y / 6)) % 2) {
                sample.set(x, y, {220, 220, 220, 255});
            }
        }
    }
    Ink fill = ink;
    draw_shape(sample, Shape::Rectangle, {-1, -1}, {49, 37}, fill, false, true, fill.brush, &fill);
    if (sample.pixels.size() == material_pixels_.size() &&
        std::equal(sample.pixels.begin(), sample.pixels.end(), material_pixels_.begin(), equal)) {
        return;
    }
    material_pixels_ = sample.pixels;
    std::vector<std::uint8_t> png = encode_png(sample);
    if (!material_preview_) {
        material_preview_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{48, 36});
        static_cast<void>((*material_preview_).add_png("material", std::as_bytes(std::span(png))));
    } else {
        static_cast<void>(
            (*material_preview_)
                .set_variant_png("material", gf::ImageVisualState::normal, 1, std::as_bytes(std::span(png))));
    }
    invalidate(gf::Dirty::paint);
}
void SwatchButton::on_paint(gf::Painter& painter, gf::Rect damage) {
    Button::on_paint(painter, damage);
    gf::Rect bounds = committed_arranged_bounds();
    const double inset =
        text.read().empty() ? std::min(3.0, bounds.width * 0.12) : std::min(9.0, bounds.width * 0.16);
    gf::Rect swatch = text.read().empty() ? gf::Rect{inset, 3, bounds.width - inset * 2, bounds.height - 6}
                                     : gf::Rect{inset, 7, bounds.width - inset * 2, 30};
    if (pressed_visual() && !text.read().empty()) {
        swatch.x += 1;
        swatch.y += 1;
    }
    if (color_.a == 0 ||
        (material_ink_ && (*material_ink_).pattern == Pattern::None && !(*material_ink_).alternate)) {
        painter.fill_rect(swatch, gf::Color::rgba(255, 255, 255));
        painter.draw_line({swatch.x + 2, swatch.y + swatch.height - 2},
                          {swatch.x + swatch.width - 2, swatch.y + 2}, gf::Color::rgba(210, 35, 40), 3);
    } else if (material_preview_) {
        painter.draw_image((*material_preview_).resolve("material").image, swatch);
    } else {
        painter.fill_rect(swatch, gf::Color::rgba(color_.r, color_.g, color_.b, color_.a));
    }
    painter.stroke_rect(swatch, gf::Color::rgba(112, 132, 152), 1);
    painter.draw_line({swatch.x + 1, swatch.y + 1}, {swatch.x + swatch.width - 1, swatch.y + 1},
                      gf::Color::rgba(255, 255, 255, 120), 1);
    painter.draw_line({swatch.x + swatch.width - 1, swatch.y + 1},
                      {swatch.x + swatch.width - 1, swatch.y + swatch.height - 1},
                      gf::Color::rgba(25, 40, 55, 50), 1);
    if (show_opacity_ && color_.a < 255) {
        const OpacityIndicator indicator = opacity_indicator(color_);
        gf::FontSpec font{gf::FontRole::control, std::clamp(swatch.width * 0.38, 8.0, 11.0), 700, false};
        const double available = std::max(1.0, swatch.width - 6);
        gf::Size size = painter.measure_text_utf8(indicator.text, font);
        if (size.width > available) {
            font.size *= available / size.width;
            size = painter.measure_text_utf8(indicator.text, font);
        }
        const gf::Point origin{swatch.right() - 3 - size.width, swatch.bottom() - 3};
        const Color foreground = indicator.foreground, outline = indicator.outline;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                if (x || y) {
                    painter.draw_text_utf8({origin.x + x, origin.y + y}, indicator.text, font,
                                           gf::Color::rgba(outline.r, outline.g, outline.b));
                }
            }
        }
        painter.draw_text_utf8(origin, indicator.text, font,
                               gf::Color::rgba(foreground.r, foreground.g, foreground.b));
    }
}
namespace {
// The point an overlay hangs from: a zero-content control moved over the
// ribbon command that opened the overlay (ribbon commands are records, not
// controls, and an overlay anchors to a control).
class OverlayAnchor final : public gf::Control {
  public:
    explicit OverlayAnchor(gf::StableId id) : Control(std::move(id)) {
        hit_test_transparent = true;
        focusable = false;
        tab_stop = false;
    }
};
// The editor command a render-quality choice runs.
const char* const render_commands[] = {"render-crisp", "render-smooth", "render-smooth-4x"};
// A material sample for the Primary and Alt commands' images: the ink over a
// transparency checkerboard, at the ribbon's large icon size.
Image material_sample(const Ink& ink) {
    Image sample;
    sample.reset(32, 32, {255, 255, 255, 255});
    for (int y = 0; y < sample.height; ++y) {
        for (int x = 0; x < sample.width; ++x) {
            if (((x / 4) + (y / 4)) % 2) {
                sample.set(x, y, {220, 220, 220, 255});
            }
        }
    }
    if (ink.primary.a == 0 || (ink.pattern == Pattern::None && !ink.alternate)) {
        for (int step = 2; step < 30; ++step) {
            for (int width = -1; width <= 1; ++width) {
                sample.set(step + width, 31 - step, {210, 35, 40, 255});
            }
        }
        return sample;
    }
    Ink fill = ink;
    draw_shape(sample, Shape::Rectangle, {-1, -1}, {33, 33}, fill, false, true, fill.brush, &fill);
    return sample;
}
} // namespace

std::string font_label(const std::string& path) {
    const std::string name = std::filesystem::path(path).filename().string();
    if (name == "Cairo.ttf") {
        return "Cairo Unicode (Dingbats)";
    }
    if (name.starts_with("DynaPuff")) {
        return "DynaPuff";
    }
    if (name.starts_with("Bubble Sans")) {
        return "Bubble Sans";
    }
    if (name == "Anton-Regular.ttf") {
        return "Anton";
    }
    if (name == "TitanOne-Regular.ttf") {
        return "Titan One";
    }
    return std::filesystem::path(path).stem().string();
}

Ribbon::Ribbon(gf::StableId id, std::weak_ptr<Editor> editor) : Control(std::move(id)), editor_(std::move(editor)) {}

void Ribbon::initialize_control_tree() {
    bar_ = gf::make_control<gf::Ribbon>(gf::StableId("ribbon-bar"));
    (*bar_).application_text = tr("File");
    (*bar_).recent_caption = tr("Recent pictures");
    subscriptions_.push_back((*bar_).drop_down_item_invoked().subscribe(
        *this, gf::Delegate<const gf::RibbonDropDownInvocation&>::bind<Ribbon, &Ribbon::drop_down_chosen>(*this)));
    subscriptions_.push_back((*bar_).recent_item_invoked().subscribe(
        *this, gf::Delegate<const gf::RibbonRecentItemInvocation&>::bind<Ribbon, &Ribbon::recent_chosen>(*this)));
    subscriptions_.push_back((*bar_).selected_tab_changed().subscribe(
        *this, gf::Delegate<const gf::RibbonTabChange&>::bind<Ribbon, &Ribbon::tab_changed>(*this)));
    subscriptions_.push_back(
        (*bar_).minimized_changed().subscribe(*this, gf::Delegate<bool>::bind<Ribbon, &Ribbon::minimized_changed>(*this)));
    add_child(bar_);
    quick_access_ = gf::make_control<gf::QuickAccessToolbar>(gf::StableId("quick-access"));
    (*quick_access_).set_commands({{command("save", tr("Save")), {}, true},
                                   {command("undo", tr("Undo (Ctrl/Command+Z)")), {}, true},
                                   {command("redo", tr("Redo (Ctrl/Command+Y)")), {}, true}});
    (*quick_access_).visible = false;
    add_child(quick_access_);
    // The help question mark sits at the right of the tab row (RB decisions
    // 41 to 43). The English escape hatch must remain recognizable in every
    // non-English interface.
    help_ = gf::make_control<gf::Button>(gf::StableId("help"), "?");
    (*help_).accessible_name = tr("Paint Help (F1)");
    subscriptions_.push_back(
        (*help_).clicked().subscribe(*this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::help_clicked>(*this)));
    add_child(help_);
    if (current_language().tag != "en-us") {
        help_english_ = gf::make_control<gf::Button>(gf::StableId("help-english"), "Help! English!");
        subscriptions_.push_back((*help_english_).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::help_clicked>(*this)));
        add_child(help_english_);
    }
    atlas_ = gf::make_control<AtlasPanel>(gf::StableId("atlas-panel"), editor_);
    (*atlas_).visible = false;
    add_child(atlas_);
    icon_ = gf::make_control<IconPanel>(gf::StableId("icon-panel"), editor_);
    (*icon_).visible = false;
    add_child(icon_);
    anchor_ = gf::make_control<OverlayAnchor>(gf::StableId("ribbon-anchor"));
    add_child(anchor_);
    make_galleries();
    apply_tabs();
}

void Ribbon::on_attached_to_window() {
    // The editor keeps its authored theme override for its own chrome; the
    // ribbon shows the application's theme, compiled with Plan Paint's theme
    // package (themes/plan-paint: the bubble command faces).
    const std::shared_ptr<const gf::Theme> application = (*attached_window()).theme_ptr();
    (*bar_).set_theme_override(application);
    (*quick_access_).set_theme_override(application);
    (*help_).set_theme_override(application);
    (*quick_access_).set_commands({{command("save", tr("Save")), icon(14, 16), true},
                                   {command("undo", tr("Undo (Ctrl/Command+Z)")), icon(15, 16), true},
                                   {command("redo", tr("Redo (Ctrl/Command+Y)")), icon(16, 16), true}});
    fill_shapes();
    fill_palette();
    fill_patterns();
    fill_brushes();
    fill_sizes();
    fill_stamp_shapes();
    fill_gradient_presets();
    tabs_signature_.clear();
    synchronize();
}

void Ribbon::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const double width = bounds.width;
    set_child_layout(quick_access_, {0, 0, std::min(width, 240.0), quick_access_height});
    const gf::Size measured = (*bar_).measure({width, 10000});
    set_child_layout(bar_, {0, quick_access_height, width, measured.height});
    const gf::Ribbon::Parts parts = (*bar_).parts();
    const gf::Rect rail = parts.tab_rail.bounds;
    const double row_top = quick_access_height + rail.y;
    const double row_height = rail.height > 0 ? rail.height : 27;
    set_child_layout(help_, {width - 30, row_top, 30, row_height});
    if (help_english_) {
        set_child_layout(help_english_, {width - 150, row_top, 116, row_height});
    }
    const gf::Rect plane = parts.command_plane.bounds;
    const gf::Rect panel{plane.x, quick_access_height + plane.y, plane.width, plane.height};
    set_child_layout(atlas_, panel);
    set_child_layout(icon_, panel);
    set_child_layout(anchor_, anchor_rect_);
}

double Ribbon::ribbon_height() const {
    const double width = committed_arranged_bounds().width > 0 ? committed_arranged_bounds().width : 1200;
    const gf::Size measured = (*bar_).measure({width, 10000});
    return quick_access_height + measured.height;
}

void Ribbon::set_file_menu(std::vector<gf::MenuItemSpec> items, std::vector<gf::RibbonRecentItem> recent) {
    // The two-pane menu takes only commands and separators while there are
    // recent items, so the recent items go last and leave first.
    (*bar_).set_recent_items({});
    (*bar_).set_application_menu(std::move(items));
    (*bar_).set_recent_items(std::move(recent));
}

bool Ribbon::file_menu_open() const {
    return (*bar_).menu_open() || (*bar_).backstage_open();
}

std::string Ribbon::selected_tab_id() const {
    const std::optional<std::size_t> selected = (*bar_).selected_tab.read();
    const std::span<const gf::RibbonTabSpec> tabs = (*bar_).tabs();
    if (!selected || *selected >= tabs.size()) {
        return "";
    }
    return tabs[*selected].stable_id;
}

void Ribbon::select_tab(const std::string& tab_id) {
    const std::span<const gf::RibbonTabSpec> tabs = (*bar_).tabs();
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        if (tabs[index].stable_id == tab_id && (*bar_).tab_shown(index)) {
            (*bar_).selected_tab = std::optional<std::size_t>{index};
            return;
        }
    }
}

// The atlas and icon panels cover the command plane of their tabs.
void Ribbon::update_panels() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const std::string tab = selected_tab_id();
    const bool plane = !(*bar_).minimized.read() && !(*bar_).plane_hidden();
    const bool atlas = tab == "atlas-tab" && plane;
    const bool icon = tab == "icon-tab" && plane;
    if ((*atlas_).visible.read() != atlas) {
        (*atlas_).visible = atlas;
    }
    if ((*icon_).visible.read() != icon) {
        (*icon_).visible = icon;
    }
    if (atlas) {
        (*atlas_).synchronize();
    }
    if (icon) {
        (*icon_).synchronize();
    }
    if (editor) {
        (*editor).show_hotspot = tab == "atlas-tab" || tab == "icon-tab";
        (*editor).invalidate(gf::Dirty::layout | gf::Dirty::paint);
    }
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}

// Choosing another tab reopens a collapsed plane (the help's "click again or
// choose another tab to reopen"); a click on the selected tab collapses it.
void Ribbon::tab_changed(const gf::RibbonTabChange&) {
    close_popup();
    if ((*bar_).minimized.read()) {
        (*bar_).minimized = false;
    }
    update_panels();
}

// The ribbon's height changes, so the editor lays out again.
void Ribbon::minimized_changed(bool) {
    close_popup();
    update_panels();
}

void Ribbon::show_atlas() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const bool icon = editor && ((*editor).document.atlas.kind == AtlasKind::Icon ||
                                 (*editor).document.atlas.kind == AtlasKind::Cursor);
    (*bar_).minimized = false;
    synchronize();
    if (icon) {
        (*bar_).set_tab_set_availability("icon-tools", gf::RibbonTabSetAvailability::active);
    } else {
        select_tab("atlas-tab");
    }
    update_panels();
}

void Ribbon::show_transforms() {
    (*bar_).minimized = false;
    close_popup();
    synchronize();
    select_tab("tool-tab");
    update_panels();
}

void Ribbon::show_materials(bool fill) {
    (*bar_).minimized = false;
    close_popup();
    secondary_color_ = fill;
    synchronize();
    select_tab("patterns-tab");
    update_panels();
}

void Ribbon::show_tool_context() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    (*bar_).minimized = false;
    close_popup();
    synchronize();
    if (!editor) {
        return;
    }
    const Tool tool = (*editor).document.tool;
    const bool spiro = tool == Tool::Spirograph ||
                       ((*editor).spiro.active && (tool == Tool::Fill || (*editor).spiro.selected_peg >= 0));
    if (spiro) {
        (*bar_).set_tab_set_availability("spiro-tools", gf::RibbonTabSetAvailability::active);
    } else if (tool == Tool::Fill && (*editor).gradient_fill) {
        (*bar_).set_tab_set_availability("gradient-tools", gf::RibbonTabSetAvailability::active);
    } else if (tool == Tool::Text) {
        (*bar_).set_tab_set_availability("text-tools", gf::RibbonTabSetAvailability::active);
    } else {
        select_tab("tool-tab");
    }
    update_panels();
}

// Contextual sets show while their tool is in use; the context mode hides
// the tool tab meanwhile. A set becomes active (selected) only through
// show_tool_context, as the earlier strip's tool page did.
void Ribbon::synchronize_tab_sets() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    const Tool tool = (*editor).document.tool;
    const bool spiro = tool == Tool::Spirograph ||
                       ((*editor).spiro.active && (tool == Tool::Fill || (*editor).spiro.selected_peg >= 0));
    // The gradient tab stays available while the fill uses a gradient, so its
    // configuration survives other tools; it replaces the tool tab only for
    // the fill tool itself.
    const bool gradient = !spiro && (*editor).gradient_fill;
    const bool text = tool == Tool::Text;
    const bool icon = (*editor).document.atlas.kind == AtlasKind::Icon ||
                      (*editor).document.atlas.kind == AtlasKind::Cursor;
    const std::pair<const char*, bool> sets[] = {
        {"text-tools", text}, {"spiro-tools", spiro}, {"gradient-tools", gradient}, {"icon-tools", icon}};
    for (const std::pair<const char*, bool>& set : sets) {
        const gf::RibbonTabSetAvailability current = (*bar_).tab_set_availability(set.first);
        if (set.second && current == gf::RibbonTabSetAvailability::not_available) {
            (*bar_).set_tab_set_availability(set.first, gf::RibbonTabSetAvailability::available);
        } else if (!set.second && current != gf::RibbonTabSetAvailability::not_available) {
            (*bar_).set_tab_set_availability(set.first, gf::RibbonTabSetAvailability::not_available);
        }
    }
    const bool gradient_context = gradient && tool == Tool::Fill;
    const std::uint32_t modes = spiro || gradient_context || text ? context_mode : ordinary_mode;
    if ((*bar_).application_modes.read() != modes) {
        (*bar_).application_modes = modes;
    }
}

void Ribbon::synchronize() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !bar_) {
        return;
    }
    Document& document = (*editor).document;
    if (document.tool == Tool::Text) {
        ensure_fonts();
    }
    const std::string& face = (*editor).text.style.face_path;
    if (!face.empty() && std::find(font_paths_.begin(), font_paths_.end(), face) == font_paths_.end()) {
        font_paths_.push_back(face);
    }
    update_color_images();
    apply_tabs();
    synchronize_tab_sets();
    synchronize_commands();
    synchronize_galleries();
    synchronizing_ = true;
    synchronize_values();
    synchronize_sticky_ribbon();
    synchronize_gradient();
    synchronizing_ = false;
    // The Help button stays highlighted while the help sidebar shows.
    if (help_ && (*help_).selected.read() != (*editor).show_help) {
        (*help_).selected = (*editor).show_help;
    }
    update_panels();
}

std::shared_ptr<gf::Command> Ribbon::command_for(const std::string& id) const {
    const std::map<std::string, CommandEntry>::const_iterator found = commands_.find(id);
    if (found == commands_.end()) {
        return nullptr;
    }
    return (*found).second.command;
}
void Ribbon::set_checked(const std::string& id, bool checked) {
    const std::map<std::string, CommandEntry>::iterator found = commands_.find(id);
    if (found != commands_.end() && (*(*found).second.command).state().checked != checked) {
        (*(*found).second.command).set_checked(checked);
    }
}
void Ribbon::set_enabled(const std::string& id, bool enabled) {
    const std::map<std::string, CommandEntry>::iterator found = commands_.find(id);
    if (found != commands_.end() && (*(*found).second.command).state().enabled != enabled) {
        (*(*found).second.command).set_enabled(enabled);
    }
}
void Ribbon::set_text(const std::string& id, const std::string& text) {
    const std::map<std::string, CommandEntry>::iterator found = commands_.find(id);
    if (found != commands_.end() && (*(*found).second.command).state().text != text) {
        (*(*found).second.command).set_text(text);
    }
}

// Checked, enabled and changing texts of the ribbon's commands, from the
// editor (the earlier strip's `selected` rules).
void Ribbon::synchronize_commands() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    Document& document = (*editor).document;
    Spirograph& spiro = (*editor).spiro;
    for (std::size_t index = 0; index < std::size(ribbon_tools); ++index) {
        const bool selected = document.tool == ribbon_tools[index] ||
                              (index == 0 && document.tool == Tool::Lasso) ||
                              (index == 9 && document.tool == Tool::Freehand) ||
                              (index == 11 && (*editor).guide.active());
        set_checked("tool-" + std::to_string(index), selected);
    }
    set_checked("text", document.tool == Tool::Text);
    set_checked("brush-menu", document.tool == Tool::Brush);
    set_checked("edge-switch", document.shape_outline);
    set_checked("fill-switch", document.tool == Tool::Guide ? (*editor).guide.fill : document.shape_fill);
    set_checked("primary", !secondary_color_);
    set_checked("secondary", secondary_color_);
    set_checked("material-edge", !secondary_color_);
    set_checked("material-fill", secondary_color_);
    set_checked("heal-source", (*editor).set_heal_source);
    set_text("text-place", (*editor).text.note ? tr("Apply") : tr("Place text"));
    set_text("stamp-shapes-menu", stamp_shape_name(document.stamp_shape));
    set_enabled("context-stamp-apply", (*editor).can_apply_stamp_placement());
    const bool warp = (*editor).warp_active();
    set_enabled("warp-place", warp);
    set_enabled("warp-cancel", warp);
    set_enabled("rotate-custom", !warp);
    set_enabled("context-reshape", !warp);
    for (int guide : {0, 16}) {
        const std::string id = "spiro-guide-" + std::to_string(guide);
        set_checked(id, spiro.guide == guide);
        set_enabled(id, spiro.compatible(guide, spiro.insert));
    }
    for (int insert : {1, spiro_default_insert, 2}) {
        const std::string id = "spiro-insert-" + std::to_string(insert);
        set_checked(id, spiro.inserted && spiro.insert == insert);
        set_enabled(id, spiro.compatible(spiro.guide, insert));
    }
    set_text("spiro-guides-menu", tr(spiro_guides()[spiro.guide].name));
    set_text("spiro-inserts-menu", tr(spiro_inserts()[spiro.insert].name));
    const int peg = spiro.selected_peg;
    set_enabled("spiro-brushes-menu", peg >= 0);
    set_text("spiro-brushes-menu",
             peg < 0 ? tr("Select a peg · choose medium")
                     : std::string(tr("Peg ")) + std::to_string(peg + 1) + " · " +
                           tr(brush_names[static_cast<int>(spiro.pegs[peg].effect.brush)]));
    set_enabled("spiro-deselect", peg >= 0);
    set_enabled("spiro-holes-menu", spiro.inserted);
    set_enabled("spiro-load-primary", peg >= 0);
    set_enabled("spiro-load-alt", peg >= 0);
    set_checked("spiro-outside", spiro.outside || spiro.capsule());
    set_enabled("spiro-outside", !spiro.rack() && !spiro.capsule() && !spiro.quad());
    set_checked("spiro-fill", document.tool == Tool::Fill);
    set_checked("spiro-operate", document.tool == Tool::Spirograph && !(*editor).spiro_lift);
    set_checked("spiro-lift", document.tool == Tool::Spirograph && (*editor).spiro_lift);
    for (const char* id : {"spiro-peg-1", "spiro-peg-2", "spiro-peg-4", "spiro-remove", "spiro-fill", "spiro-lift"}) {
        set_enabled(id, spiro.inserted);
    }
    set_enabled("spiro-operate", spiro.inserted && !spiro.detached);
    set_checked("gradient-linear", (*editor).fill_gradient.kind == GradientKind::Linear);
    set_checked("gradient-radial", (*editor).fill_gradient.kind == GradientKind::Radial);
    set_enabled("gradient-add", (*editor).fill_gradient.stops.size() < 32);
    set_enabled("gradient-remove", (*editor).fill_gradient.stops.size() > 2);
}

// The Primary and Alt commands show their material; the image is replaced
// in place (same ImageId) when the material changes.
void Ribbon::update_color_images() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !attached_window()) {
        return;
    }
    const Document& document = (*editor).document;
    const Ink primary = document.tool == Tool::Pencil ? pencil_ink(document.primary_ink()) : document.primary_ink();
    Ink stored_alt = document.alt_ink;
    stored_alt.primary = document.ink.secondary;
    stored_alt.secondary.a = 0;
    stored_alt.transparent_pattern = true;
    const Ink secondary = document.tool == Tool::Pencil ? pencil_ink(stored_alt) : stored_alt;
    if (!primary_shown_ || !same_material(*primary_shown_, primary)) {
        primary_shown_ = primary;
        static_cast<void>(artwork("primary-material", material_sample(primary)));
        const OpacityIndicator indicator = opacity_indicator(primary.primary);
        (*command("primary", tr("Primary")))
            .set_description(tr("Opacity") + ": " + std::to_string(indicator.percent) + "% (" +
                             std::to_string(primary.primary.a) + "/255)");
    }
    if (!secondary_shown_ || !same_material(*secondary_shown_, secondary)) {
        secondary_shown_ = secondary;
        static_cast<void>(artwork("secondary-material", material_sample(secondary)));
        const OpacityIndicator indicator = opacity_indicator(secondary.primary);
        (*command("secondary", tr("Alt")))
            .set_description(tr("Opacity") + ": " + std::to_string(indicator.percent) + "% (" +
                             std::to_string(secondary.primary.a) + "/255)");
    }
}

// The hosted controls' values, from the editor (while synchronizing_, so
// their change events do not write back).
void Ribbon::synchronize_values() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const Document& document = (*editor).document;
    const Ink& material = (*editor).spiro.selected_peg >= 0 ? (*editor).spiro.pegs[(*editor).spiro.selected_peg].effect
                          : secondary_color_                ? document.alt_ink
                                                            : document.ink;
    const std::pair<std::shared_ptr<gf::NumericUpDown>, double> numbers[] = {
        {spiro_scale_, (*editor).spiro.scale},
        {grain_, material.grain_scale},
        {tooth_, material.paper_roughness},
        {load_, material.pigment_load},
        {angle_, material.material_angle},
        {tool_size_, static_cast<double>(document.ink.size)},
        {corner_radius_, document.ink.geometry.corner_radius},
        {geometry_organic_, document.ink.geometry.organic},
        {text_size_, static_cast<double>((*editor).text.style.size)},
        {stamp_width_, static_cast<double>((*editor).stamp_width)},
        {stamp_height_, static_cast<double>((*editor).stamp_height)},
        {stamp_scale_, (*editor).stamp_scale},
        {stamp_angle_, (*editor).stamp_angle},
        {mesh_spacing_, (*editor).mesh_spacing},
        {stamp_hardness_, (*editor).stamp_hardness},
        {effect_strength_, (*editor).effect_strength},
        {effect_scale_, (*editor).effect_scale},
        {effect_phase_, (*editor).effect_phase},
        {heal_hardness_, (*editor).heal_hardness},
        {heal_correction_, (*editor).heal_correction},
        {stabilizer_lag_, (*editor).stabilizer_lag},
        {text_outline_, (*editor).text.style.outline_width},
        {text_skew_, (*editor).text.style.skew},
        {text_perspective_, (*editor).text.style.perspective},
        {text_warp_, (*editor).text.style.warp},
    };
    for (const std::pair<std::shared_ptr<gf::NumericUpDown>, double>& each : numbers) {
        if (each.first && (*each.first).value.read() != each.second) {
            (*each.first).value = each.second;
        }
    }
    const std::pair<std::shared_ptr<gf::ComboBox>, std::size_t> choices[] = {
        {geometry_stroke_, static_cast<std::size_t>(document.ink.geometry.stroke)},
        {geometry_start_, static_cast<std::size_t>(document.ink.geometry.start)},
        {geometry_end_, static_cast<std::size_t>(document.ink.geometry.end)},
        {render_quality_, static_cast<std::size_t>(!document.ink.smooth ? 0 : document.ink.supersample ? 2 : 1)},
        {picker_mode_, static_cast<std::size_t>((*editor).picker_mode)},
        {mix_effect_, static_cast<std::size_t>((*editor).mix_effect)},
        {word_art_, static_cast<std::size_t>((*editor).text.style.word_art)},
    };
    for (const std::pair<std::shared_ptr<gf::ComboBox>, std::size_t>& each : choices) {
        if (each.first && each.second < (*each.first).items.read().size()) {
            (*each.first).selected_index = std::optional<std::size_t>{each.second};
        }
    }
    if (font_) {
        std::optional<std::size_t> font_index;
        if ((*editor).text.style.face_path.empty()) {
            font_index = (*editor).text.style.mono ? 1 : 0;
        } else {
            for (std::size_t index = 0; index < font_paths_.size(); ++index) {
                if (font_paths_[index] == (*editor).text.style.face_path) {
                    font_index = index + 2;
                    break;
                }
            }
        }
        if (font_index && *font_index < (*font_).items.read().size()) {
            (*font_).selected_index = font_index;
        }
    }
    const TextStyle& style = (*editor).text.style;
    for (const std::pair<const std::string, HostedEntry>& entry : hosted_) {
        const std::shared_ptr<gf::CheckBox> check = std::dynamic_pointer_cast<gf::CheckBox>(entry.second.control);
        if (!check) {
            continue;
        }
        const std::string& id = entry.first;
        const bool checked = id == "text-contour"          ? style.contour
                             : id == "text-bold"           ? style.bold
                             : id == "text-italic"         ? style.italic
                             : id == "text-underline"      ? style.underline
                             : id == "text-strikeout"      ? style.strikeout
                             : id == "text-opaque"         ? style.opaque
                             : id == "text-wrap"           ? style.word_wrap
                             : id == "guide-fill"          ? (*editor).guide.fill
                             : id == "heal-clone"          ? (*editor).heal_options.clone
                             : id == "heal-aligned"        ? (*editor).heal_options.aligned
                             : id == "heal-alpha"          ? (*editor).heal_options.preserve_alpha
                             : id == "stroke-stabilize"    ? (*editor).stabilize
                             : id == "dither-brush-noise"  ? (*editor).dither_brush_mode == DitherBrushMode::Noise
                             : id == "spray-glitter"       ? (*editor).glitter
                             : id == "picker-magnifier"    ? (*editor).picker_magnifier
                             : id == "show-rulers"         ? (*editor).show_rulers
                             : id == "show-grid"           ? (*editor).show_grid
                             : id == "show-status"         ? (*editor).show_status
                             : id == "transparent-selection" ? document.transparent_selection
                             : id == "stamp-transparent"   ? document.stamp_transparent
                             : id == "alt-carries-body"    ? document.alt_carries_body
                             : id == "smooth-lines"        ? document.ink.smooth
                             : id == "continuous-path"     ? document.continuous_path
                             : id == "edit-path-nodes"     ? (*editor).edit_path_nodes
                             : id == "outline"             ? document.shape_outline
                                                           : document.shape_fill;
        if ((*check).checked.read() != checked) {
            (*check).checked = checked;
        }
    }
    if (geometry_start_ && geometry_end_) {
        const bool markers = document.tool != Tool::Path || document.continuous_path;
        (*geometry_start_).enabled = markers;
        (*geometry_end_).enabled = markers;
    }
}

void Ribbon::cancel_spiro_drag() {
    if (spiro_drag_width_) {
        spiro_drag_width_ = 0;
        set_pointer_capture(false);
    }
}

std::optional<gf::Rect> Ribbon::command_bounds(const std::string& id) const {
    const gf::Rect origin = (*bar_).committed_arranged_bounds();
    for (const gf::Ribbon::CommandItem& item : (*bar_).realized_commands()) {
        if (item.stable_id == id && item.parts.command_face.present) {
            const gf::Rect face = item.parts.command_face.bounds;
            return gf::Rect{origin.x + face.x, origin.y + face.y, face.width, face.height};
        }
    }
    return std::nullopt;
}

// Places the overlay anchor over a command; false when the command is not
// shown.
bool Ribbon::move_anchor(const std::string& id) {
    const std::optional<gf::Rect> bounds = command_bounds(id);
    if (!bounds || !attached_window()) {
        return false;
    }
    anchor_rect_ = *bounds;
    invalidate(gf::Dirty::layout);
    (*attached_window()).perform_layout();
    return true;
}

// The ID of the enabled command with `prefix` under a window point, or "".
std::string Ribbon::command_at(gf::Point position, const std::string& prefix) const {
    const gf::Rect origin = (*bar_).absolute_bounds();
    for (const gf::Ribbon::CommandItem& item : (*bar_).realized_commands()) {
        if (!item.stable_id.starts_with(prefix) || !item.parts.command_face.present || item.states.disabled) {
            continue;
        }
        const gf::Rect face = item.parts.command_face.bounds;
        const gf::Rect absolute{origin.x + face.x, origin.y + face.y, face.width, face.height};
        if (absolute.contains(position)) {
            return item.stable_id;
        }
    }
    return "";
}

void Ribbon::on_pointer_preview(gf::PointerEvent& event) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor && spiro_drag_width_) {
        (*editor).spiro_tray_pointer(spiro_drag_width_, event);
        if (event.action == gf::PointerAction::up) {
            spiro_drag_width_ = 0;
            set_pointer_capture(false);
        }
        event.handled = true;
        return;
    }
    // Pegs drag from the tray into a hole of the wheel.
    if (editor && event.button == gf::PointerButton::primary && event.action == gf::PointerAction::down) {
        const std::string peg = command_at(event.position, "spiro-peg-");
        if (!peg.empty()) {
            const int width = std::stoi(peg.substr(10));
            (*editor).spiro_tray_pointer(width, event);
            spiro_drag_width_ = width;
            set_pointer_capture(true);
            event.handled = true;
            return;
        }
    }
    // A right click on a palette, pattern or brush item applies it to Alt.
    if (event.button != gf::PointerButton::secondary) {
        return;
    }
    if (event.action == gf::PointerAction::down) {
        const std::string item = gallery_item_at(event.position);
        if (!item.empty()) {
            secondary_target_ = item;
            set_pointer_capture(true);
            event.handled = true;
        }
    } else if (event.action == gf::PointerAction::up && !secondary_target_.empty()) {
        const std::string item = std::move(secondary_target_);
        secondary_target_.clear();
        set_pointer_capture(false);
        event.handled = true;
        if (gallery_item_at(event.position) == item) {
            secondary_color_ = true;
            apply_gallery_item(item);
        }
    }
}

void Ribbon::command_invoked(const gf::CommandInvocation& invocation) {
    const std::string& id = invocation.command_id;
    if (id.ends_with("-gallery")) {
        gallery_chosen(id);
        return;
    }
    activate(id);
}

void Ribbon::drop_down_chosen(const gf::RibbonDropDownInvocation& invocation) {
    const std::string& item = invocation.choice.item_id;
    menu_choice(item.starts_with("choice-") ? item.substr(7) : item);
}

void Ribbon::recent_chosen(const gf::RibbonRecentItemInvocation& invocation) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).execute(invocation.item_id);
    }
}

void Ribbon::help_clicked(gf::ButtonBase& button) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).execute(std::string(button.stable_id().value()));
    }
}

void Ribbon::check_clicked(gf::ButtonBase& check) {
    const std::string window_id(check.stable_id().value());
    const std::string prefix = "ribbon-bar.";
    activate(window_id.starts_with(prefix) ? window_id.substr(prefix.size()) : window_id);
}

// A ribbon command ran (its face, for a split): the earlier strip's click.
void Ribbon::activate(const std::string& id) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    // A field's label moves focus to its field.
    if (id.ends_with("-label")) {
        const std::map<std::string, HostedEntry>::iterator field = hosted_.find(id.substr(0, id.size() - 6));
        if (field != hosted_.end() && attached_window()) {
            static_cast<void>((*attached_window()).request_focus((*field).second.control));
        }
        return;
    }
    if (!popup_command_.empty() && popup_command_ == id) {
        close_popup();
        return;
    }
    if (id == "spiro-guides-menu" || id == "spiro-inserts-menu" || id == "spiro-holes-menu" ||
        id == "spiro-brushes-menu" || id == "gradient-stops-menu") {
        open_popup(id);
        return;
    }
    if (id == "size-menu") {
        static_cast<void>(open_drop_down_gallery(id, sizes_));
        return;
    }
    if (id == "stamp-shapes-menu") {
        static_cast<void>(open_drop_down_gallery(id, stamp_shapes_));
        return;
    }
    if (gradient_clicked(id)) {
        return;
    }
    Document& document = (*editor).document;
    if (id == "edit-custom-pattern") {
        (*editor).toggle_pattern_canvas();
        return;
    }
    if (id == "edge-switch" || id == "fill-switch") {
        (*editor).execute(id == "edge-switch" ? "outline" : "fill");
        return;
    }
    if (id.starts_with("spiro-peg-") && (*editor).spiro.active && (*editor).spiro.inserted) {
        Spirograph& spiro = (*editor).spiro;
        int hole = spiro.selected_peg;
        if (hole < 0) {
            for (int index = 0; index < spiro.hole_count(); ++index) {
                if (!spiro.pegs[index].seated) {
                    hole = index;
                    break;
                }
            }
        }
        if (hole >= 0) {
            const int width = std::stoi(id.substr(10));
            if (spiro.pegs[hole].seated) {
                spiro.pegs[hole].width = width;
                spiro.refresh_peg(hole);
            } else {
                spiro.seat(hole, {true, false, {}, width});
            }
            spiro.select_peg(hole);
            (*editor).refresh();
        }
        return;
    }
    if (id.starts_with("spiro-") && !id.starts_with("spiro-peg-")) {
        (*editor).spiro_choice(id);
        return;
    }
    if (id == "brush-menu") {
        (*editor).choose_tool(Tool::Brush);
        if ((*editor).brush_family == BrushFamily::Additive) {
            show_materials(false);
        } else {
            show_tool_context();
        }
        return;
    }
    if (id == "material-edge" || id == "material-fill") {
        show_materials(id == "material-fill");
        return;
    }
    if (id == "primary" || id == "secondary") {
        secondary_color_ = id == "secondary";
        synchronize();
        return;
    }
    if (id == "edit-colors") {
        (*editor).execute(secondary_color_ ? "secondary" : "primary");
        return;
    }
    if (id == "new-grain") {
        Ink& material = secondary_color_ ? document.alt_ink : document.ink;
        ++material.noise;
        document.sync_path();
        (*editor).refresh();
        return;
    }
    apply_choice(id);
}

void Ribbon::apply_choice(const std::string& id) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    Document& document = (*editor).document;
    if (id == "geometry-vary") {
        ++document.ink.geometry.seed;
    } else if (id == "stamp-transparent") {
        document.stamp_transparent = !document.stamp_transparent;
    } else if (id == "context-stamp-add") {
        (*editor).add_stamp_material();
    } else if (id == "carpet-settings") {
        (*editor).open_editor_dialog(EditorDialogKind::carpet);
    } else if (id == "heal-source") {
        (*editor).set_heal_source = true;
    } else if (id == "heal-clone") {
        (*editor).heal_options.clone = !(*editor).heal_options.clone;
    } else if (id == "heal-aligned") {
        (*editor).heal_options.aligned = !(*editor).heal_options.aligned;
    } else if (id == "heal-alpha") {
        (*editor).heal_options.preserve_alpha = !(*editor).heal_options.preserve_alpha;
    } else if (id == "stroke-stabilize") {
        (*editor).stabilize = !(*editor).stabilize;
    } else if (id == "dither-brush-noise") {
        (*editor).dither_brush_mode = (*editor).dither_brush_mode == DitherBrushMode::Neighborhood
                                          ? DitherBrushMode::Noise
                                          : DitherBrushMode::Neighborhood;
    } else if (id == "spray-glitter") {
        (*editor).glitter = !(*editor).glitter;
    } else if (id == "picker-magnifier") {
        (*editor).picker_magnifier = !(*editor).picker_magnifier;
    } else if (id == "context-stamp-clear") {
        (*editor).reset_stamp();
    } else if (id == "text-contour") {
        (*editor).text.style.contour = !(*editor).text.style.contour;
    } else if (id == "text-reset-effects") {
        TextStyle& style = (*editor).text.style;
        style.skew = style.perspective = style.warp = 0;
        style.word_art = WordArt::Plain;
        style.contour = false;
    } else if (id == "text-bold") {
        (*editor).text.style.bold = !(*editor).text.style.bold;
    } else if (id == "text-italic") {
        (*editor).text.style.italic = !(*editor).text.style.italic;
    } else if (id == "text-underline") {
        (*editor).text.style.underline = !(*editor).text.style.underline;
    } else if (id == "text-strikeout") {
        (*editor).text.style.strikeout = !(*editor).text.style.strikeout;
    } else if (id == "text-opaque") {
        (*editor).text.style.opaque = !(*editor).text.style.opaque;
    } else if (id == "rotate-custom") {
        if (rotation_) {
            (*editor).request_rotation((*rotation_).value.read());
        }
    } else if (id == "text-wrap") {
        (*editor).text.style.word_wrap = !(*editor).text.style.word_wrap;
    } else if (id == "text-primary" || id == "text-secondary") {
        (*editor).execute(id.substr(5));
    } else {
        (*editor).execute(id.starts_with("context-") ? id.substr(8) : id);
    }
    document.sync_curve();
    document.sync_path();
    (*editor).refresh();
}

// A choice from a split or drop-down command's menu (row ID without its
// "choice-" prefix).
void Ribbon::menu_choice(const std::string& id) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    close_popup();
    if (gradient_clicked(id)) {
        return;
    }
    if (id == "spirograph") {
        (*editor).start_spirograph();
        return;
    }
    Document& document = (*editor).document;
    if (id == "guide-swap-bezier" || id == "guide-swap-arc") {
        (*editor).begin_guide_swap(id == "guide-swap-bezier" ? CurveKind::Bezier : CurveKind::Arc);
    } else if (id == "path-swap-bezier" || id == "path-swap-arc") {
        (*editor).begin_path_swap(id == "path-swap-bezier" ? CurveKind::Bezier : CurveKind::Arc);
    } else if (id == "freehand-shape") {
        (*editor).choose_tool(Tool::Freehand);
        document.shape_fill = true;
    } else if (id.starts_with("eraser-")) {
        (*editor).eraser_mode = static_cast<EraserMode>(std::stoi(id.substr(7)));
        (*editor).eraser_soft = (*editor).eraser_mode == EraserMode::Soft;
        (*editor).choose_tool(Tool::Eraser);
        if ((*editor).eraser_mode >= EraserMode::Blur) {
            show_tool_context();
        }
    } else if (id.starts_with("lasso-")) {
        (*editor).lasso_mode = id == "lasso-free"    ? LassoMode::Free
                               : id == "lasso-tight" ? LassoMode::Tighten
                               : id == "lasso-wand"  ? LassoMode::Wand
                                                     : LassoMode::InnerVoid;
        (*editor).choose_tool(Tool::Lasso);
    } else if (id.starts_with("family-")) {
        if (id != "family-carpet") {
            (*editor).brush_family = (id == "family-additive" || id == "family-glitter") ? BrushFamily::Additive
                                     : id == "family-mix"                                 ? BrushFamily::Mix
                                     : id == "family-dither"                              ? BrushFamily::Dither
                                                                                          : BrushFamily::Heal;
        }
        if (id == "family-glitter") {
            select_brush(document.ink, Brush::Airbrush);
            document.ink.size = std::max(24, document.ink.size);
            (*editor).glitter = true;
        }
        (*editor).choose_tool(Tool::Brush);
        if (id == "family-carpet") {
            show_tool_context();
            (*editor).open_editor_dialog(EditorDialogKind::carpet);
        } else if (id == "family-additive") {
            show_materials(false);
        } else {
            show_tool_context();
        }
    } else if (id == "stamp-add") {
        (*editor).add_stamp_material();
    } else if (id == "stamp-clear") {
        (*editor).reset_stamp();
    } else {
        (*editor).execute(id);
    }
    document.sync_curve();
    document.sync_path();
    (*editor).refresh();
}

void Ribbon::ensure_fonts() {
    if (fonts_loaded_) {
        return;
    }
    fonts_loaded_ = true;
    for (const std::string& path : installed_fonts()) {
        if (std::find(font_paths_.begin(), font_paths_.end(), path) == font_paths_.end()) {
            font_paths_.push_back(path);
        }
    }
}

void Ribbon::font_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (synchronizing_ || !index || !editor) {
        return;
    }
    if (*index >= 2 && *index - 2 >= font_paths_.size()) {
        return;
    }
    (*editor).text.style.face_path = *index < 2 ? "" : font_paths_[*index - 2];
    (*editor).text.style.mono = *index == 1;
    (*editor).refresh();
}

// A hosted list changed: every shown list is read back into the editor (the
// lists the change did not touch hold the editor's values already).
void Ribbon::choice_changed(std::optional<std::size_t>) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (synchronizing_ || !editor) {
        return;
    }
    Document& document = (*editor).document;
    if (mix_effect_) {
        (*editor).mix_effect = static_cast<MixEffect>((*mix_effect_).selected_index.read().value_or(0));
    }
    if (picker_mode_) {
        (*editor).picker_mode = static_cast<SampleMode>((*picker_mode_).selected_index.read().value_or(0));
    }
    if (word_art_) {
        (*editor).text.style.word_art = static_cast<WordArt>((*word_art_).selected_index.read().value_or(0));
    }
    if (geometry_stroke_) {
        const std::size_t stroke = (*geometry_stroke_).selected_index.read().value_or(0);
        if (stroke < 3) {
            document.ink.geometry.stroke = static_cast<StrokeStyle>(stroke);
        }
    }
    if (geometry_start_ && geometry_end_) {
        const std::size_t first = (*geometry_start_).selected_index.read().value_or(0);
        const std::size_t last = (*geometry_end_).selected_index.read().value_or(0);
        if (first < endpoint_marker_count && last < endpoint_marker_count) {
            document.ink.geometry.start = static_cast<EndpointMarker>(first);
            document.ink.geometry.end = static_cast<EndpointMarker>(last);
        }
    }
    if (render_quality_) {
        const std::optional<std::size_t> chosen = (*render_quality_).selected_index.read();
        const std::size_t current = !document.ink.smooth ? 0 : document.ink.supersample ? 2 : 1;
        if (chosen && *chosen < 3 && *chosen != current) {
            (*editor).execute(render_commands[*chosen]);
        }
    }
    document.sync_curve();
    document.sync_path();
    (*editor).refresh();
}

void Ribbon::spiro_scale_changed(double value) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor) {
        (*editor).resize_spirograph(value);
    }
}

// A hosted number changed: every shown number is read back into the editor.
void Ribbon::options_changed(double) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (synchronizing_ || !editor) {
        return;
    }
    Document& document = (*editor).document;
    Ink& ink = (*editor).spiro.selected_peg >= 0 ? (*editor).spiro.pegs[(*editor).spiro.selected_peg].effect
               : secondary_color_                ? document.alt_ink
                                                 : document.ink;
    if (grain_ && tooth_ && load_ && angle_) {
        ink.grain_scale = (*grain_).value.read();
        ink.paper_roughness = (*tooth_).value.read();
        ink.pigment_load = (*load_).value.read();
        ink.material_angle = (*angle_).value.read();
        (*editor).spiro.refresh_peg((*editor).spiro.selected_peg);
    }
    if (tool_size_) {
        document.ink.size = static_cast<int>((*tool_size_).value.read());
    }
    if (corner_radius_) {
        document.ink.geometry.corner_radius = (*corner_radius_).value.read();
    }
    if (geometry_organic_) {
        document.ink.geometry.organic = (*geometry_organic_).value.read();
    }
    if ((*editor).text.note && sticky_padding_ && sticky_width_ && sticky_height_) {
        (*(*editor).text.note).padding = static_cast<int>((*sticky_padding_).value.read());
        const Rect bounds = (*editor).text.bounds;
        (*editor).text.resize({bounds.x, bounds.y, static_cast<int>((*sticky_width_).value.read()),
                               static_cast<int>((*sticky_height_).value.read())});
    }
    TextStyle& style = (*editor).text.style;
    if (text_size_) {
        style.size = static_cast<int>((*text_size_).value.read());
    }
    if (text_outline_) {
        style.outline_width = (*text_outline_).value.read();
    }
    if (text_skew_ && text_perspective_ && text_warp_) {
        style.skew = (*text_skew_).value.read();
        style.perspective = (*text_perspective_).value.read();
        style.warp = (*text_warp_).value.read();
    }
    if (stamp_width_ && stamp_height_) {
        (*editor).stamp_width = static_cast<int>((*stamp_width_).value.read());
        (*editor).stamp_height = static_cast<int>((*stamp_height_).value.read());
    }
    if (mesh_spacing_) {
        (*editor).mesh_spacing = (*mesh_spacing_).value.read();
    }
    if (effect_strength_ && effect_scale_ && effect_phase_) {
        (*editor).effect_strength = (*effect_strength_).value.read();
        (*editor).effect_scale = (*effect_scale_).value.read();
        (*editor).effect_phase = (*effect_phase_).value.read();
    }
    if (heal_hardness_ && heal_correction_) {
        (*editor).heal_hardness = (*heal_hardness_).value.read();
        (*editor).heal_correction = (*heal_correction_).value.read();
    }
    if (stabilizer_lag_) {
        (*editor).stabilizer_lag = (*stabilizer_lag_).value.read();
    }
    if (stamp_hardness_ && (*editor).stamp_hardness != (*stamp_hardness_).value.read()) {
        (*editor).stamp_hardness = (*stamp_hardness_).value.read();
        (*editor).update_stamp_hardness();
    }
    if (stamp_scale_ && stamp_angle_ &&
        ((*editor).stamp_scale != (*stamp_scale_).value.read() ||
         (*editor).stamp_angle != (*stamp_angle_).value.read())) {
        (*editor).stamp_scale = (*stamp_scale_).value.read();
        (*editor).stamp_angle = (*stamp_angle_).value.read();
        (*editor).regenerate_stamp();
    }
    document.sync_curve();
    document.sync_path();
    (*editor).refresh();
}
} // namespace paint::forms

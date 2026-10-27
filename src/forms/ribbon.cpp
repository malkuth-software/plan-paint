#include "forms/ribbon.hpp"
#include "codecs.hpp"
#include "forms/atlas.hpp"
#include "forms/dropper_icon.hpp"
#include "forms/editor.hpp"
#include "forms/icon_ribbon.hpp"
#include "forms/interface_theme.hpp"
#include "forms/ribbon_icons.hpp"
#include "forms/spirograph_view.hpp"
#include "localization.hpp"
#include "material.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <gui_forms/delegate.hpp>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
// Four-pixel gaps after the tools, guide and size groups; two-pixel divider gutters.
double home_x(double x) {
    if (x >= 1132) { return x - 28; }
    if (x >= 814) { return x - 21; }
    if (x >= 751) { return x - 16; }
    if (x >= 540) { return x - 9; }
    if (x >= 340) { return x - 7; }
    if (x >= 142) { return x - 10; }
    return x;
}
const Tool ribbon_tools[] = {Tool::Select, Tool::Lasso,  Tool::Pencil,    Tool::Fill,
                             Tool::Eraser, Tool::Picker, Tool::Magnifier, Tool::Brush,
                             Tool::Shape,  Tool::Path,   Tool::Stamp,     Tool::Guide};
const Shape home_shapes[] = {
    Shape::Line,    Shape::Bezier,     Shape::Oval,          Shape::Rectangle,      Shape::RoundedRectangle,
    Shape::Polygon, Shape::Triangle,   Shape::RightTriangle, Shape::Diamond,        Shape::Pentagon,
    Shape::Hexagon, Shape::RightArrow, Shape::LeftArrow,     Shape::UpArrow,        Shape::DownArrow,
    Shape::Star4,   Shape::Star5,      Shape::Star6,         Shape::RoundedCallout, Shape::Heart,
    Shape::Arc};
class WeightButton final : public gf::Button {
  public:
    WeightButton(gf::StableId id, std::string text, int weight)
        : Button(std::move(id), std::move(text)), weight_(weight) {}
    void on_paint(gf::Painter& painter, gf::Rect damage) override {
        Button::on_paint(painter, damage);
        double height = committed_arranged_bounds().height;
        painter.draw_line({68, height / 2}, {145, height / 2}, gf::Color::rgba(36, 60, 85),
                          std::min(12, weight_));
    }

  private:
    int weight_;
};
void paint_separator(gf::Painter& painter, double x, const gf::Control& owner) {
    const gf::GradientStop shade[] = {{0, interface_color(owner, gf::Color::rgba(143, 170, 201, 0))},
                                      {0.16, interface_color(owner, gf::Color::rgba(143, 170, 201, 125))},
                                      {0.83, interface_color(owner, gf::Color::rgba(143, 170, 201, 125))},
                                      {1, interface_color(owner, gf::Color::rgba(143, 170, 201, 0))}};
    const gf::GradientStop light[] = {{0, interface_color(owner, gf::Color::rgba(255, 255, 255, 0))},
                                      {0.16, interface_color(owner, gf::Color::rgba(255, 255, 255, 235))},
                                      {0.83, interface_color(owner, gf::Color::rgba(255, 255, 255, 235))},
                                      {1, interface_color(owner, gf::Color::rgba(255, 255, 255, 0))}};
    painter.fill_linear_gradient({x, 32, 1, 106}, {x, 32}, {x, 138}, shade);
    painter.fill_linear_gradient({x + 1, 32, 1, 106}, {x, 32}, {x, 138}, light);
}
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
std::shared_ptr<const gf::Theme> make_tab_theme() {
    gf::ThemeDefinition definition = (*ribbon_theme()).definition();
    definition.id = "rainstar-cobalt-tab-stack";
    gf::ControlRoleRecipes& recipes =
        definition.roles[static_cast<std::size_t>(gf::ControlVisualRole::button)];
    for (std::size_t state = 0; state < gf::control_surface_state_count; ++state) {
        for (int selected = 0; selected < 2; ++selected) {
            gf::ControlVisualRecipe& recipe = selected ? recipes.selected[state] : recipes.ordinary[state];
            const bool disabled = state == static_cast<std::size_t>(gf::ControlSurfaceState::disabled);
            const bool hot = state == static_cast<std::size_t>(gf::ControlSurfaceState::hot);
            gf::SurfaceMaterial material;
            material.corner_radius = 4;
            const gf::Color face = selected ? gf::Color::rgba(232, 242, 255)
                                   : hot    ? gf::Color::rgba(172, 198, 231)
                                            : gf::Color::rgba(155, 182, 218);
            material.fills = {
                gf::MaterialFillLayer::solid(face),
                gf::MaterialFillLayer::linear(
                    {0, 0}, {0, 1},
                    {{0, selected ? gf::Color::rgba(255, 255, 255, 145) : gf::Color::rgba(34, 70, 116, 39)},
                     {0.72, gf::Color::rgba(0, 0, 0, 0)},
                     {1, gf::Color::rgba(0, 0, 0, 0)}})};
            material.keylines = {
                {gf::MaterialEdge::top, gf::Color::rgba(255, 255, 255, selected ? 255 : 125), 1, 0},
                {gf::MaterialEdge::right, gf::Color::rgba(209, 224, 245), 1, 1},
                {gf::MaterialEdge::right, gf::Color::rgba(73, 108, 159), 1, 0}};
            material.shadows = {{{2, 0}, 0, 0, gf::Color::rgba(73, 108, 159), false},
                                {{3, 1}, 2, 0, gf::Color::rgba(24, 56, 97, 55), false}};
            recipe.material = material;
            recipe.text = disabled   ? gf::Color::rgba(93, 116, 148)
                          : selected ? gf::Color::rgba(23, 63, 121)
                                     : gf::Color::rgba(35, 63, 102);
            recipe.pressed_content_offset = {};
        }
    }
    return gf::Theme::create(std::move(definition));
}
std::shared_ptr<const gf::Theme> ribbon_tab_theme() {
    static const std::shared_ptr<const gf::Theme> theme = make_tab_theme();
    return theme;
}
class RibbonTabButton final : public gf::Button {
  public:
    RibbonTabButton(gf::StableId id, std::string text) : Button(std::move(id), std::move(text)) {}
    void on_paint(gf::Painter& painter, gf::Rect damage) override {
        Button::on_paint(painter, damage);
        // Continue the selected sheet into the ribbon; no enclosed bottom edge.
        if (selected() && !visual_context().high_contrast) {
            const gf::Rect bounds = client_rectangle();
            painter.fill_rect({0, bounds.height - 3, bounds.width, 3}, interface_color(*this, gf::Color::rgba(232, 242, 255)));
        }
    }
};
class DisclosureDivider final : public gf::Control {
  public:
    explicit DisclosureDivider(gf::StableId id) : Control(std::move(id)) {
        set_hit_test_transparent(true);
        set_dock(gf::DockStyle::bottom);
        set_requested_bounds({0, 0, 0, 14});
    }
    void on_paint(gf::Painter& painter, gf::Rect) override {
        const double right = std::max(3.0, client_rectangle().width - 3);
        painter.draw_line({3, 0.5}, {right, 0.5}, interface_color(*this, gf::Color::rgba(99, 132, 178)), 1);
        painter.draw_line({3, 1.5}, {right, 1.5}, interface_color(*this, gf::Color::rgba(255, 255, 255, 155)), 1);
    }
};
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
    if ((*this).text().empty()) {
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
    set_accessible_description(show_opacity_ ? tr("Opacity") + ": " +
        std::to_string(indicator.percent) + "% (" + std::to_string(color_.a) + "/255)" : "");
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
    set_accessible_name(text() + ": " +
                        (ink.brush == Brush::Round ? tr(pattern_names[static_cast<int>(ink.pattern)])
                                                   : tr(brush_names[static_cast<int>(ink.brush)])));
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
        text().empty() ? std::min(3.0, bounds.width * 0.12) : std::min(9.0, bounds.width * 0.16);
    gf::Rect swatch = text().empty() ? gf::Rect{inset, 3, bounds.width - inset * 2, bounds.height - 6}
                                     : gf::Rect{inset, 7, bounds.width - inset * 2, 30};
    if (pressed_visual() && !text().empty()) {
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
class ShapeSwitchButton final : public gf::Button {
  public:
    ShapeSwitchButton(gf::StableId id, bool fill) : Button(std::move(id), ""), fill_(fill) {}
    void on_paint(gf::Painter& painter, gf::Rect damage) override {
        Button::on_paint(painter, damage);
        painter.save();
        if (pressed_visual()) {
            painter.translate({1, 1});
        }
        const gf::Rect bounds = client_rectangle();
        const double side = std::min(13.0, bounds.width * 0.28);
        const gf::Rect sample{3, (bounds.height - side) / 2, side, side};
        const gf::Color blue = gf::Color::rgba(30, 91, 145);
        if (fill_) {
            painter.fill_rect(sample, blue);
        } else {
            painter.fill_rect(sample, gf::Color::rgba(255, 255, 255));
            painter.stroke_rect(sample, blue, 2);
        }
        const std::string caption = fill_ ? tr("Fill") : tr("Edge");
        gf::FontSpec font{gf::FontRole::control, std::clamp(bounds.width * 0.23, 8.5, 11.0), 500, false};
        const double available = std::max(1.0, bounds.width - side - 9);
        const gf::Size measured = painter.measure_text_utf8(caption, font);
        if (measured.width > available) {
            font.size = std::max(8.5, font.size * available / measured.width);
        }
        painter.draw_text_utf8(
            {side + 6, bounds.height / 2 + font.size * 0.32}, caption, font,
            gf::Color::rgba(34, 52, 72));
        painter.restore();
    }

  private:
    bool fill_;
};
class GuideGlyph final : public gf::Control {
  public:
    explicit GuideGlyph(gf::StableId id) : Control(std::move(id)) {
        set_hit_test_transparent(true);
        set_dock(gf::DockStyle::fill);
    }
    void on_paint(gf::Painter& painter, gf::Rect) override {
        painter.save();
        const std::shared_ptr<gf::Button> owner = std::dynamic_pointer_cast<gf::Button>(parent());
        if (owner && (*owner).pressed_visual()) {
            painter.translate({1, 1});
        }
        const double size = std::min(30.0, committed_arranged_bounds().width - 10),
                     left = (committed_arranged_bounds().width - size) * 0.5;
        const gf::Color color = gf::Color::rgba(38, 95, 140);
        painter.draw_line({left, 13}, {left, 13 + size}, color, 2);
        painter.draw_line({left, 13 + size}, {left + size, 13 + size}, color, 2);
        painter.draw_line({left + size, 13 + size}, {left, 13}, color, 2);
        painter.draw_line({left + size * 0.2, 13 + size * 0.47}, {left + size * 0.2, 13 + size * 0.8}, color,
                          1);
        painter.draw_line({left + size * 0.2, 13 + size * 0.8}, {left + size * 0.53, 13 + size * 0.8}, color,
                          1);
        painter.draw_line({left + size * 0.53, 13 + size * 0.8}, {left + size * 0.2, 13 + size * 0.47}, color,
                          1);
        for (int tick = 1; tick < 5; ++tick) {
            const double y = 13 + size * tick / 5;
            painter.draw_line({left, y}, {left + 3, y}, color, 1);
        }
        painter.restore();
    }
};
} // namespace
Ribbon::Ribbon(gf::StableId id, std::weak_ptr<Editor> editor)
    : Control(std::move(id)), editor_(std::move(editor)) {
    set_theme_override(ribbon_theme());
}
std::shared_ptr<gf::Button> Ribbon::button(const std::string& id, const std::string& text, int icon,
                                           gf::Rect bounds, bool tall, bool disclosure, bool split) {
    std::shared_ptr<gf::Button> result;
    if (disclosure) {
        std::shared_ptr<gf::DropDownButton> dropdown = gf::make_control<gf::DropDownButton>(
            gf::StableId(id), text, split ? gf::DropDownButtonMode::split : gf::DropDownButtonMode::menu);
        if (id == "tool-11") {
            (*dropdown).add_child(gf::make_control<GuideGlyph>(gf::StableId("guide-glyph")));
        }
        if (tall) {
            (*dropdown).set_drop_down_edge(gf::DropDownButtonEdge::bottom);
            (*dropdown).add_child(gf::make_control<DisclosureDivider>(gf::StableId(id + "-divider")));
        }
        (*dropdown).set_drop_down_width(tall ? 14 : 16);
        subscriptions_.push_back((*dropdown).drop_down_requested().subscribe(
            *this, gf::Delegate<gf::DropDownButton&>::bind<Ribbon, &Ribbon::dropdown>(*this)));
        subscriptions_.push_back((*dropdown).drop_down_close_requested().subscribe(
            *this, gf::Delegate<gf::DropDownButton&>::bind<Ribbon, &Ribbon::dropdown>(*this)));
        result = dropdown;

    } else if (id.ends_with("-tab")) {
        result = gf::make_control<RibbonTabButton>(gf::StableId(id), text);
    } else if (id == "edge-switch" || id == "fill-switch") {
        result = gf::make_control<ShapeSwitchButton>(gf::StableId(id), id == "fill-switch");
    } else {
        result = gf::make_control<gf::Button>(gf::StableId(id), text);
    }
    if (id.ends_with("-tab")) {
        (*result).set_theme_override(ribbon_tab_theme());
    } else if (id.starts_with("shape-") || id.starts_with("r-pattern-") ||
               id.starts_with("material-brush-") || id.starts_with("stamp-shape-")) {
        (*result).set_theme_override(gallery_theme());
    }
    (*result).set_requested_bounds(bounds);
    (*result).set_accessible_name(text.empty() ? id : text);
    (*result).set_use_mnemonic(false);
    (*result).set_font({gf::FontRole::control, 14, 400, false, 0.05});
    (*result).set_content_padding(tall ? gf::Insets{3, 4, 3, disclosure ? 16.0 : 4.0}
                                       : gf::Insets{4, 2, disclosure ? 18.0 : 4.0, 2});
    (*result).set_text_image_relation(tall ? gf::TextImageRelation::image_above_text
                                           : gf::TextImageRelation::image_before_text);
    (*result).set_image_gap(id == "paste" ? 1 : 4);
    (*result).set_text_line_spacing(1.1);
    (*result).set_text_alignment(tall || text.empty() || id.ends_with("-tab") || id == "help"
                                     ? gf::ContentAlignment::middle_center
                                     : gf::ContentAlignment::middle_left);
    (*result).set_image_alignment(tall || text.empty() ? gf::ContentAlignment::middle_center
                                                       : gf::ContentAlignment::middle_left);
    if (icon >= 0) {
        (*result).set_image_key(std::to_string(icon));
    }
    subscriptions_.push_back((*result).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::clicked>(*this)));
    button_pages_.push_back(building_page_);
    buttons_.push_back(result);
    add_child(result);
    return result;
}
void Ribbon::initialize_control_tree() {
    building_page_ = 0;
    button("home-tab", tr("Home"), -1, {57, 0, 61, 27});
    button("view-tab", tr("View"), -1, {118, 0, 63, 27});
    button("patterns-tab", tr("Materials"), -1, {184, 0, 100, 27});
    button("tool-tab", tr("Pencil"), -1, {286, 0, 142, 27});
    button("atlas-tab", tr("Atlas"), -1, {431, 0, 72, 27});
    button("icon-tab", tr("Icon"), -1, {506, 0, 72, 27});
    button("gradient-tab", tr("Gradients"), -1, {581, 0, 105, 27});
    atlas_ = gf::make_control<AtlasPanel>(gf::StableId("atlas-panel"), editor_);
    add_child(atlas_);
    icon_ = gf::make_control<IconPanel>(gf::StableId("icon-panel"), editor_);
    add_child(icon_);
    button("help", "?", -1, {1164, 0, 32, 27});
    // This escape hatch must remain recognizable in every non-English UI.
    if (current_language().tag != "en-us") {
        button("help-english", "Help! English!", -1, {1048, 0, 112, 27});
    }
    building_page_ = 1;
    button("save", tr("Save"), 14, {5, 35, 64, 34});
    button("undo", "", 15, {5, 81, 31, 30});
    button("redo", "", 16, {38, 81, 31, 30});
    button("paste", tr("Paste"), 0, {80, 34, 44, 83}, true, true, true);
    button("tool-0", "", 3, {142, 34, 36, 30}, false, true, true);
    button("crop", "", 4, {180, 34, 28, 30});
    button("resize", "", 5, {210, 34, 28, 30});
    button("rotate-menu", "", 6, {240, 34, 36, 30}, false, true);
    button("cut", "", 1, {278, 34, 28, 30});
    button("copy", "", 2, {308, 34, 28, 30});
    button("tool-2", "", 7, {144, 80, 28, 30});
    button("tool-3", "", 8, {174, 80, 34, 30}, false, true, true);
    std::shared_ptr<gf::Button> text = button("text", "", 9, {210, 80, 26, 30});
    (*text).set_accessible_name(tr("Text"));
    button("tool-4", "", 10, {240, 80, 36, 30}, false, true, true);
    button("tool-5", "", -1, {278, 80, 28, 30});
    button("tool-6", "", 12, {310, 80, 28, 30});
    button("brush-menu", tr("Brushes"), 13, {340, 34, 48, 83}, true, true, true);
    button("tool-10", tr("Stamp"), 20, {392, 34, 46, 83}, true, true, true);
    button("tool-9", tr("Path"), 21, {442, 34, 44, 83}, true, true, true);
    std::shared_ptr<gf::Button> guide_button =
        button("tool-11", tr("Guide"), -1, {490, 34, 44, 83}, true, true, true);
    (*guide_button).set_content_padding({0, 38, 0, 14});
    for (std::size_t i = 0; i < std::size(home_shapes); ++i) {
        button("shape-" + std::to_string(static_cast<int>(home_shapes[i])), "",
               100 + static_cast<int>(home_shapes[i]), {546 + 25.0 * (i % 7), 36 + 25.0 * (i / 7), 25, 25});
    }
    button("shapes-menu", "", -1, {722, 36, 18, 75}, false, true);
    button("edge-switch", "", -1, {751, 34, 54, 25});
    button("fill-switch", "", -1, {751, 60, 54, 25});
    button("size-menu", "", 23, {751, 88, 54, 27}, false, true);
    primary_ = gf::make_control<SwatchButton>(gf::StableId("primary"), tr("Primary"), Color{0, 0, 0, 255});
    secondary_ = gf::make_control<SwatchButton>(gf::StableId("secondary"), tr("Alt"), Color{255, 255, 255, 255});
    for (int i = 0; i < 2; ++i) {
        std::shared_ptr<SwatchButton> swatch = i == 0 ? primary_ : secondary_;
        (*swatch).set_show_opacity(true);
        (*swatch).set_requested_bounds({821 + i * 48.0, 36, 44, 80});
        (*swatch).set_font({gf::FontRole::control, 14, 400, false});
        (*swatch).set_content_padding({0, 40, 0, 0});
        subscriptions_.push_back((*swatch).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::clicked>(*this)));
        button_pages_.push_back(building_page_);
        buttons_.push_back(swatch);
        add_child(swatch);
    }
    for (int i = 0; i < 30; ++i) {
        std::shared_ptr<SwatchButton> swatch =
            gf::make_control<SwatchButton>(gf::StableId("swatch-" + std::to_string(i)), "", ribbon_color(i));
        (*swatch).set_requested_bounds({921.0 + (i % 10) * 20, 36.0 + (i / 10) * 24, 20, 23});
        (*swatch).set_accessible_name(tr("Palette color ") + std::to_string(i + 1));
        subscriptions_.push_back((*swatch).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::clicked>(*this)));
        button_pages_.push_back(building_page_);
        buttons_.push_back(swatch);
        add_child(swatch);
    }
    button("edit-colors", tr("Edit\ncolors"), 24, {1132, 34, 52, 83}, true);
    button("palette-pane", tr("Basic"), -1, {921, 110, 200, 22});
    add_options();
    // Tab order describes the authored interface, independently of visual overlap.
    for (std::size_t index = 0; index < children().size(); ++index) {
        (*children()[index]).set_tab_index(static_cast<std::uint32_t>(index));
    }
    show_page();
}
namespace {
class PatternButton final : public gf::Button {
  public:
    PatternButton(gf::StableId id, int pattern, std::shared_ptr<Image> tile) : Button(std::move(id)), pattern_(pattern), tile_(std::move(tile)) {
        set_theme_override(gallery_theme());
    }
    void on_paint(gf::Painter& painter, gf::Rect damage) override {
        Button::on_paint(painter, damage);
        Ink ink;
        ink.pattern = static_cast<Pattern>(pattern_);
        ink.custom_pattern = tile_;
        if (ink.pattern == Pattern::None) {
            painter.fill_rect({3, 3, 20, 18}, gf::Color::rgba(255, 255, 255));
            painter.draw_line({4, 20}, {22, 4}, gf::Color::rgba(210, 35, 40), 2.5);
            return;
        }
        ink.primary = {40, 80, 120, 255};
        ink.secondary = {245, 248, 252, 255};
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 25; ++x) {
                Color color = patterned(ink, x, y);
                painter.fill_rect({2.0 + x, 2.0 + y, 1, 1}, gf::Color::rgba(color.r, color.g, color.b));
            }
        }
    }

  private:
    int pattern_;
    std::shared_ptr<Image> tile_;
};
} // namespace
void Ribbon::option(std::shared_ptr<gf::Control> control, gf::Rect bounds) {
    (*control).set_requested_bounds(bounds);
    option_controls_.push_back(control);
    option_pages_.push_back(building_page_);
    add_child(control);
}
void Ribbon::check(const std::string& id, const std::string& text, gf::Rect bounds) {
    std::shared_ptr<gf::CheckBox> control = gf::make_control<gf::CheckBox>(gf::StableId(id), text);
    (*control).set_auto_check(false);
    (*control).set_font({gf::FontRole::control, 14, 400, false});
    subscriptions_.push_back((*control).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::clicked>(*this)));
    checks_.push_back(control);
    option(control, bounds);
}
std::shared_ptr<gf::NumericUpDown> Ribbon::number(const std::string& id, const std::string& text,
                                                  gf::Rect bounds, double minimum, double maximum,
                                                  double value, int decimals) {
    std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(gf::StableId(id + "-label"), text);
    (*label).set_font({gf::FontRole::control, 14, 400, false});
    const double label_width = id == "spiro-scale" ? 70 : id == "geometry-corners" ? 165 : 113;
    option(label, {bounds.x, bounds.y, label_width, bounds.height});
    std::shared_ptr<gf::NumericUpDown> control = gf::make_control<gf::NumericUpDown>(gf::StableId(id));
    (*control).set_range(minimum, maximum);
    (*control).set_decimal_places(static_cast<std::uint8_t>(decimals));
    (*control).set_increment(decimals ? 0.05 : 1);
    (*control).set_value(value);
    (*control).set_accessible_name(text);
    option(control, {bounds.x + label_width, bounds.y, bounds.width - label_width, bounds.height});
    subscriptions_.push_back((*control).value_changed().subscribe(
        *this, id.starts_with("gradient-")
                   ? gf::Delegate<double>::bind<Ribbon, &Ribbon::gradient_number_changed>(*this)
               : id == "spiro-scale"
                   ? gf::Delegate<double>::bind<Ribbon, &Ribbon::spiro_scale_changed>(*this)
                   : gf::Delegate<double>::bind<Ribbon, &Ribbon::options_changed>(*this)));
    return control;
}
namespace {
class SpiroPartButton final : public gf::Button {
    int kind_, index_;

  public:
    SpiroPartButton(gf::StableId id, std::string text, int kind, int index)
        : Button(std::move(id), std::move(text)), kind_(kind), index_(index) {}
    void on_paint(gf::Painter& painter, gf::Rect damage) override {
        Button::on_paint(painter, damage);
        const gf::Rect box = client_rectangle();
        paint_spiro_part(painter, {0, 0, box.width, box.height - 22}, kind_, index_);
    }
};
} // namespace
void Ribbon::spiro_button(const std::string& id, const std::string& text, int kind, int index,
                          gf::Rect bounds) {
    const std::shared_ptr<gf::Button> choice =
        gf::make_control<SpiroPartButton>(gf::StableId(id), text, kind, index);
    (*choice).set_requested_bounds(bounds);
    (*choice).set_content_padding({1, 48, 1, 1});
    (*choice).set_text_alignment(gf::ContentAlignment::bottom_center);
    (*choice).set_accessible_name(text);
    subscriptions_.push_back((*choice).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::clicked>(*this)));
    button_pages_.push_back(128);
    buttons_.push_back(choice);
    add_child(choice);
}
void Ribbon::add_options() {
    building_page_ = 128;
    for (int i = 0; i < 2; ++i) {
        const int guide = i == 0 ? 0 : 16;
        spiro_button("spiro-guide-" + std::to_string(guide), tr(spiro_guides()[guide].name), 0, guide,
                     {10 + i * 95.0, 35, 87, 76});
    }
    const int tray_inserts[] = {1, spiro_default_insert, 2};
    for (int i = 0; i < 3; ++i) {
        const int insert = tray_inserts[i];
        spiro_button("spiro-insert-" + std::to_string(insert), tr(spiro_inserts()[insert].name), 1, insert,
                     {220 + i * 88.0, 35, 80, 76});
    }
    button("spiro-guides-menu", tr("More"), -1, {10, 111, 182, 22}, false, true);
    button("spiro-inserts-menu", tr("More"), -1, {220, 111, 256, 22}, false, true);
    for (int i = 0; i < 3; ++i) {
        spiro_button("spiro-peg-" + std::to_string(1 << i),
                     i == 0   ? tr("Fine peg")
                     : i == 1 ? tr("Medium peg")
                              : tr("Bold peg"),
                     2, i, {500 + i * 80.0, 35, 72, 76});
    }
    button("spiro-fill", tr("Fill pegs"), 8, {755, 35, 140, 31});
    button("spiro-operate", tr("Operate"), -1, {902, 35, 126, 31});
    button("spiro-remove", tr("Remove insert"), -1, {755, 76, 140, 31});
    button("spiro-center", tr("Center guide"), -1, {902, 76, 126, 31});
    button("spiro-brushes-menu", tr("Peg medium"), -1, {500, 111, 232, 22}, false, true);
    button("spiro-outside", tr("Roll outside"), -1, {755, 111, 140, 22});
    button("spiro-deselect", tr("Deselect peg"), -1, {902, 111, 126, 22});
    spiro_scale_ = number("spiro-scale", tr("Scale"), {1040, 36, 140, 28}, .05, 64, 1.35, 2);
    button("spiro-lift", tr("Lift wheel"), -1, {1186, 36, 82, 28});
    button("spiro-holes-menu", tr("Choose hole"), -1, {1040, 76, 228, 31}, false, true);
    button("spiro-load-primary", tr("Load Primary"), -1, {1040, 111, 111, 22});
    button("spiro-load-alt", tr("Load Alt"), -1, {1157, 111, 111, 22});
    building_page_ = 2;
    button("zoom-in", tr("Zoom in"), 12, {8, 35, 65, 81}, true);
    button("zoom-out", tr("Zoom out"), 12, {76, 35, 65, 81}, true);
    button("actual-size", "100%", 5, {144, 35, 65, 81}, true);
    check("show-rulers", tr("Rulers"), {234, 36, 144, 25});
    check("show-grid", tr("Gridlines"), {234, 64, 144, 25});
    check("show-status", tr("Status bar"), {234, 92, 144, 25});
    button("full-screen", tr("Full screen"), 5, {400, 35, 98, 81}, true);
    button("fit", tr("Fit window"), 5, {507, 35, 98, 81}, true);
    building_page_ = 4;
    button("material-edge", tr("Primary"), -1, {10, 34, 114, 22});
    button("material-fill", tr("Alt"), -1, {10, 59, 114, 22});
    check("alt-carries-body", tr("Alt carries body"), {340, 111, 225, 22});
    const std::string material_names[] = {tr("Brush"),  tr("Calligraphy 1"), tr("Calligraphy 2"), tr("Spray can"),  tr("Oil"),
                                    tr("Crayon"), tr("Marker"),        tr("Pencil"),        tr("Watercolor"), tr("Bristle"),
                                    tr("Pastel"), tr("Charcoal"),      tr("Gel pen")};
    for (int i = 1; i < brush_count; ++i) {
        std::shared_ptr<gf::Button> choice =
            button("material-brush-" + std::to_string(i), material_names[i], -1,
                   {140.0 + ((i - 1) % 4) * 128, 35.0 + ((i - 1) / 4) * 27, 126, 25});
        (*choice).set_accessible_name(tr(brush_names[i]));
        (*choice).set_font({gf::FontRole::control, 11, 400, false});
        (*choice).set_content_padding({2, 1, 2, 1});
        (*choice).set_image_key(std::to_string(i));
    }
    for (int i = 0; i < pattern_count; ++i) {
        std::shared_ptr<gf::Button> swatch =
            gf::make_control<PatternButton>(gf::StableId("r-pattern-" + std::to_string(i)), i, (*editor_.lock()).custom_pattern);
        (*swatch).set_requested_bounds({670.0 + (i % 9) * 33, 36.0 + (i / 9) * 25, 29, 24});
        if (i == 0 || i == static_cast<int>(Pattern::None)) {
            (*swatch).set_requested_bounds({10, i == 0 ? 84.0 : 109.0, 114, 22});
            (*swatch).set_text(i == 0 ? tr("Solid") : tr("No color"));
            (*swatch).set_content_padding({32, 1, 1, 1});
        }
        (*swatch).set_accessible_name(tr(pattern_names[i]));
        subscriptions_.push_back((*swatch).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::clicked>(*this)));
        button_pages_.push_back(building_page_);
        buttons_.push_back(swatch);
        add_child(swatch);
    }

    button("edit-custom-pattern", tr("Edit custom pattern…"), -1, {670, 112, 293, 22});
    building_page_ = 4;
    grain_ = number("grain-scale", tr("Grain scale"), {982, 34, 273, 22}, 0.3, 4, 1, 2);
    tooth_ = number("paper-tooth", tr("Paper tooth"), {982, 58, 273, 22}, 0, 1, 0.65, 2);
    load_ = number("paint-load", tr("Paint load"), {982, 82, 273, 22}, 0, 1, 0.65, 2);
    angle_ = number("grain-angle", tr("Grain angle"), {982, 106, 202, 22}, -180, 180, -20);
    button("new-grain", tr("New"), -1, {1188, 106, 67, 22});
    building_page_ = 8;
    tool_size_ = number("tool-size", tr("Size (px)"), {12, 36, 204, 25}, 1, 1024, 3);
    check("soft-eraser", tr("Soft eraser"), {12, 65, 210, 25});
    building_page_ = 12;
    check("smooth-lines", tr("Smooth lines"), {244, 36, 210, 25});
    render_quality_ = gf::make_control<gf::ComboBox>(gf::StableId("render-quality"));
    (*render_quality_).set_items({tr("Crisp"), tr("Smooth"), tr("4× Smooth")});
    (*render_quality_).set_selected_index(1);
    (*render_quality_).set_accessible_name(tr("Line and shape rendering"));
    option(render_quality_, {244, 36, 210, 28});
    subscriptions_.push_back((*render_quality_).selected_index_changed().subscribe(
        *this, gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::render_quality_changed>(*this)));
    building_page_ = 8;
    check("continuous-path", tr("Continuous path"), {12, 65, 210, 25});
    corner_radius_ = number("geometry-corners", tr("Corner radius (px)"), {480, 36, 330, 28}, 0, 4096, 0, 1);
    geometry_organic_ = number("geometry-organic", tr("Organic"), {840, 36, 260, 28}, 0, 3, 0, 2);
    button("geometry-vary", tr("Vary"), -1, {1120, 36, 140, 28});
    const std::vector<std::string> markers{tr("None"), tr("Open arrow"), tr("Triangle"), tr("Diamond"),
        tr("Circle"), tr("Bar"), tr("Exactly one"), tr("Many"), tr("Zero or one"), tr("One or more"), tr("Zero or more")};
    geometry_start_ = gf::make_control<gf::ComboBox>(gf::StableId("geometry-start"));
    geometry_end_ = gf::make_control<gf::ComboBox>(gf::StableId("geometry-end"));
    for (int index = 0; index < 2; ++index) {
        const std::shared_ptr<gf::ComboBox> marker = index == 0 ? geometry_start_ : geometry_end_;
        const std::string id = index == 0 ? "geometry-start" : "geometry-end";
        const std::string name = index == 0 ? tr("Start marker") : tr("End marker");
        const std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(gf::StableId(id + "-label"), name);
        option(label, {840.0 + index * 220, 68, 200, 22});
        (*marker).set_items(markers);
        (*marker).set_accessible_name(name);
        (*marker).set_drop_down_width(240);
        (*marker).set_maximum_drop_down_items(endpoint_marker_count);
        option(marker, {840.0 + index * 220, 94, 200, 28});
        subscriptions_.push_back((*marker).selected_index_changed().subscribe(*this,
            gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::geometry_markers_changed>(*this)));
    }
    const std::shared_ptr<gf::Label> stroke_label = gf::make_control<gf::Label>(
        gf::StableId("geometry-stroke-label"), tr("Stroke style"));
    option(stroke_label, {480, 79, 165, 28});
    geometry_stroke_ = gf::make_control<gf::ComboBox>(gf::StableId("geometry-stroke"));
    (*geometry_stroke_).set_items({tr("Solid"), tr("Dashed"), tr("Dotted")});
    (*geometry_stroke_).set_accessible_name(tr("Stroke style"));
    option(geometry_stroke_, {645, 79, 165, 28});
    subscriptions_.push_back((*geometry_stroke_).selected_index_changed().subscribe(
        *this, gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::geometry_stroke_changed>(*this)));
    check("edit-path-nodes", tr("Edit nodes"), {244, 65, 210, 25});
    check("outline", tr("Edge"), {12, 94, 90, 25});
    check("fill", tr("Fill"), {116, 94, 100, 25});
    check("transparent-selection", tr("Transparent selection"), {12, 36, 210, 25});
    button("guide-clear", tr("Clear guide"), -1, {12, 36, 130, 30});
    button("guide-set", tr("Set guide"), -1, {152, 36, 130, 30});
    check("guide-fill", tr("Protect body"), {302, 36, 180, 30});
    button("context-select-all", tr("Select all"), -1, {12, 65, 95, 25});
    button("context-invert-selection", tr("Invert"), -1, {116, 65, 100, 25});
    button("context-crop", tr("Crop"), 4, {12, 94, 95, 25});
    button("context-delete", tr("Delete"), -1, {116, 94, 100, 25});
    button("stamp-shapes-menu", tr("Circle"), 100 + static_cast<int>(Shape::Circle), {12, 36, 204, 54}, false,
           true);
    check("stamp-transparent", tr("Transparent stamp"), {12, 94, 210, 25});
    button("context-stamp-apply", tr("Apply"), -1, {242, 36, 135, 30});
    button("context-stamp-cancel", tr("Cancel"), -1, {242, 79, 135, 30});
    std::shared_ptr<gf::Label> placement_hint = gf::make_control<gf::Label>(
        gf::StableId("stamp-placement-hint"), tr("Position preview; Apply to paint. Escape cancels."));
    (*placement_hint).set_text_wrapping(gf::TextWrapping::word);
    option(placement_hint, {12, 36, 204, 78});
    button("context-stamp-clear", tr("Clear stamp"), -1, {242, 36, 135, 30});
    button("context-zoom-in", tr("Zoom in"), 12, {12, 36, 95, 78}, true);
    button("context-zoom-out", tr("Zoom out"), 12, {117, 36, 99, 78}, true);
    std::shared_ptr<gf::Label> picker = gf::make_control<gf::Label>(
        gf::StableId("picker-hint"), tr("Click: Primary · right-click: Alt\nDrag: pan the view"));
    option(picker, {12, 36, 204, 74});
    picker_mode_ = gf::make_control<gf::ComboBox>(gf::StableId("picker-mode"));
    (*picker_mode_).set_items({tr("Exact pixel"), tr("Small average (5 × 5)"), tr("Representative (reject flecks)")});
    (*picker_mode_).set_selected_index(0);
    (*picker_mode_).set_accessible_name(tr("Eyedropper sampling"));
    option(picker_mode_, {242, 36, 280, 30});
    subscriptions_.push_back(
        (*picker_mode_)
            .selected_index_changed()
            .subscribe(
                *this,
                gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::picker_mode_changed>(*this)));
    check("picker-magnifier", tr("Local 8× magnifier"), {242, 79, 280, 28});
    stamp_width_ = number("stamp-width", tr("Width (px)"), {398, 36, 235, 27}, 1, 1024, 80);
    stamp_height_ = number("stamp-height", tr("Height (px)"), {398, 79, 235, 27}, 1, 1024, 80);
    stamp_scale_ = number("stamp-scale", tr("Scale"), {660, 36, 235, 27}, 0.1, 8, 1, 2);
    stamp_angle_ = number("stamp-angle", tr("Angle"), {660, 79, 235, 27}, -360, 360, 0, 1);
    stamp_hardness_ = number("stamp-hardness", tr("Hardness"), {924, 36, 230, 28}, 0, 1, 1, 2);
    button("context-stamp-add", tr("Add material"), -1, {242, 79, 135, 30});
    mix_effect_ = gf::make_control<gf::ComboBox>(gf::StableId("mix-effect"));
    for (int index = 0; index < mix_effect_count; ++index) {
        (*mix_effect_).add_item(mix_effect_name(static_cast<MixEffect>(index)));
    }
    (*mix_effect_).set_selected_index(0);
    (*mix_effect_).set_accessible_name(tr("Mix effect"));
    option(mix_effect_, {12, 36, 222, 30});
    subscriptions_.push_back(
        (*mix_effect_)
            .selected_index_changed()
            .subscribe(
                *this,
                gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::mix_effect_changed>(*this)));
    effect_strength_ = number("effect-strength", tr("Strength"), {260, 36, 230, 28}, 0, 1, 0.75, 2);
    effect_scale_ = number("effect-scale", tr("Scale (px)"), {510, 36, 230, 28}, 2, 128, 24);
    effect_phase_ = number("effect-phase", tr("Phase"), {760, 36, 230, 28}, -10, 10, 0, 2);
    button("carpet-settings", tr("Carpet settings…"), -1, {12, 36, 222, 30});
    button("heal-source", tr("Set source"), -1, {12, 36, 222, 30});
    heal_hardness_ = number("heal-hardness", tr("Hardness"), {260, 36, 230, 28}, 0, 1, 0.35, 2);
    heal_correction_ = number("heal-correction", tr("Color match"), {510, 36, 230, 28}, 0, 1, 1, 2);
    check("heal-clone", tr("Clone exactly"), {760, 36, 220, 28});
    check("heal-aligned", tr("Aligned source"), {430, 79, 210, 28});
    check("heal-alpha", tr("Preserve transparency"), {670, 79, 260, 28});
    check("stroke-stabilize", tr("Stabilizer"), {12, 79, 150, 28});
    stabilizer_lag_ = number("stroke-lag", tr("Lag (px)"), {180, 79, 220, 28}, 0.1, 50, 5, 1);
    check("spray-glitter", tr("Fine glitter"), {920, 79, 200, 28});
    check("dither-brush-noise", tr("Brightness / saturation noise"), {430, 79, 310, 28});
    building_page_ = 40;
    rotation_ = number("rotation-degrees", tr("Angle (°)"), {242, 36, 240, 28}, -360, 360, 15, 1);
    button("rotate-custom", tr("Apply rotation"), 6, {242, 79, 240, 30});
    mesh_spacing_ = number("mesh-spacing", tr("Spacing (px)"), {510, 36, 240, 28}, 8, 256, 60);
    button("context-reshape", tr("Start mesh"), 21, {510, 79, 240, 30});
    button("warp-place", tr("Place"), -1, {778, 36, 120, 30});
    button("warp-cancel", tr("Cancel"), -1, {778, 79, 120, 30});
    std::shared_ptr<gf::Label> warp_hint = gf::make_control<gf::Label>(
        gf::StableId("warp-hint"),
        tr("Gold edge handles skew; square handles resize.\nDrag the rotation handle to turn. "
        "Shift snaps to 15°. Escape deselects."));
    option(warp_hint, {920, 36, 320, 75});
    building_page_ = 16;
    std::vector<std::string> fonts{"Portsmouth", "Portsmouth Mono"};
    for (const std::string& path : font_paths_) {
        fonts.push_back(std::filesystem::path(path).stem().string());
    }
    font_ = gf::make_control<gf::ComboBox>(gf::StableId("text-font"));
    (*font_).set_items(std::move(fonts));
    (*font_).set_selected_index(0);
    (*font_).set_accessible_name(tr("Font family"));
    (*font_).set_maximum_drop_down_items(12);
    (*font_).set_drop_down_width(320);
    option(font_, {12, 37, 220, 28});
    subscriptions_.push_back((*font_).selected_index_changed().subscribe(
        *this, gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::font_changed>(*this)));
    text_size_ = number("text-size", tr("Font size"), {12, 79, 220, 28}, 6, 300, 24);
    check("text-bold", tr("Bold"), {244, 35, 86, 24});
    check("text-italic", tr("Italic"), {334, 35, 86, 24});
    check("text-underline", tr("Underline"), {244, 64, 107, 24});
    check("text-strikeout", tr("Strikeout"), {353, 64, 100, 24});
    check("text-opaque", tr("Opaque"), {244, 93, 100, 24});
    check("text-wrap", tr("Wrap"), {353, 93, 100, 24});
    check("text-contour", tr("Outline letters"), {467, 35, 192, 24});
    text_outline_ = number("text-outline-width", tr("Outline (px)"), {467, 64, 216, 24}, 1, 8, 2);
    word_art_ = gf::make_control<gf::ComboBox>(gf::StableId("text-word-art"));
    (*word_art_).set_items({tr("Plain"), tr("3D extrusion"), tr("Embossed"), tr("Color gradient")});
    (*word_art_).set_accessible_name(tr("WordArt style"));
    (*word_art_).set_selected_index(0);
    option(word_art_, {467, 93, 216, 24});
    subscriptions_.push_back(
        (*word_art_)
            .selected_index_changed()
            .subscribe(
                *this,
                gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::word_art_changed>(*this)));
    text_skew_ = number("text-skew", tr("Skew"), {696, 35, 218, 24}, -0.7, 0.7, 0, 2);
    text_perspective_ = number("text-perspective", tr("Perspective"), {696, 64, 218, 24}, -0.8, 0.8, 0, 2);
    text_warp_ = number("text-warp", tr("Bend"), {696, 93, 218, 24}, -0.4, 0.4, 0, 2);
    button("text-primary", tr("Primary"), 24, {928, 35, 62, 82}, true);
    button("text-secondary", tr("Alt"), 24, {994, 35, 62, 82}, true);
    button("text-place", tr("Place text"), -1, {1068, 35, 92, 25});
    button("text-cancel", tr("Cancel"), -1, {1166, 35, 92, 25});
    button("text-fit", tr("Fit height"), -1, {1068, 64, 92, 25});
    button("text-reset-effects", tr("Reset"), -1, {1166, 64, 92, 25});
    initialize_sticky_ribbon();
    initialize_gradient();
    building_page_ = 1;
}
double Ribbon::ribbon_height() const {
    return collapsed_ ? 27 : 143;
}
void Ribbon::show_atlas() {
    collapsed_ = false;
    const std::shared_ptr<Editor> editor = editor_.lock();
    const bool icon = editor && ((*editor).document.atlas.kind == AtlasKind::Icon ||
                                 (*editor).document.atlas.kind == AtlasKind::Cursor);
    page_ = icon ? 512 : 64;
    synchronize();
    invalidate(gf::Dirty::paint);
}
void Ribbon::show_transforms() {
    collapsed_ = false;
    page_ = 8;
    close_popup();
    synchronize();
}
void Ribbon::show_tool_context() {
    collapsed_ = false;
    std::shared_ptr<Editor> editor = editor_.lock();
    page_ = editor && ((*editor).document.tool == Tool::Spirograph ||
                       ((*editor).spiro.active &&
                        ((*editor).document.tool == Tool::Fill || (*editor).spiro.selected_peg >= 0)))
                ? 128
            : editor && (*editor).document.tool == Tool::Fill && (*editor).gradient_fill ? 256
            : editor && (*editor).document.tool == Tool::Text                            ? 16
                                                                                         : 8;
    close_popup();
    synchronize();
}
namespace {
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
} // namespace
void Ribbon::ensure_fonts() {
    if (fonts_loaded_) {
        return;
    }
    fonts_loaded_ = true;
    font_paths_ = installed_fonts();
    for (const std::string& path : font_paths_) {
        (*font_).add_item(font_label(path));
    }
}
void Ribbon::font_changed(std::optional<std::size_t> index) {
    if (synchronizing_ || !index) {
        return;
    }
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    (*editor).text.style.face_path = *index < 2 ? "" : font_paths_[*index - 2];
    (*editor).text.style.mono = *index == 1;
    (*editor).refresh();
}
void Ribbon::show_page() {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    const bool icon = (*editor).document.atlas.kind == AtlasKind::Icon ||
                      (*editor).document.atlas.kind == AtlasKind::Cursor;
    if (page_ == 512 && !icon) {
        page_ = 1;
    }
    (*atlas_).set_visible(page_ == 64 && !collapsed_);
    (*icon_).set_visible(page_ == 512 && !collapsed_);
    (*editor).show_hotspot = page_ == 64 || page_ == 512;
    if (page_ == 64) {
        (*atlas_).synchronize();
    }
    if (page_ == 512) {
        (*icon_).synchronize();
    }
    Tool tool = (*editor).document.tool;
    if (page_ == 256 && !(*editor).gradient_fill) {
        page_ = 8;
    }
    if (page_ == 128 && !(*editor).spiro.active) {
        page_ = 8;
    }
    if (page_ == 8 || page_ == 16) {
        page_ = tool == Tool::Text ? 16 : 8;
    }
    bool selection = tool == Tool::Select || tool == Tool::Lasso;
    bool ink = (*editor).spiro.selected_peg >= 0 || tool == Tool::Fill || tool == Tool::Brush ||
               tool == Tool::Shape || tool == Tool::Path || tool == Tool::Freehand;
    if (page_ == 4 && !ink) {
        page_ = 8;
    }
    bool material = (*editor).spiro.selected_peg >= 0 || tool == Tool::Brush || tool == Tool::Shape ||
                    tool == Tool::Path || tool == Tool::Freehand;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        gf::Button& control = *buttons_[i];
        std::string id(control.stable_id().value());
        bool visible = button_pages_[i] == 0 || (button_pages_[i] & page_) != 0;
        if (page_ == 8 && button_pages_[i] != 0) {
            if (id.starts_with("r-pattern-")) {
                visible = false;
            }
            if (id.starts_with("context-")) {
                visible = id.starts_with("context-stamp-") ? tool == Tool::Stamp
                          : id.starts_with("context-zoom-")                          ? tool == Tool::Magnifier
                                                                                     : selection;
            }
            if (id.starts_with("guide-")) {
                visible = tool == Tool::Guide;
            }
            if (id.starts_with("geometry-")) {
                visible = tool == Tool::Shape || tool == Tool::Path || tool == Tool::Freehand;
            }
            if (id == "carpet-settings") {
                visible = tool == Tool::Brush && (*editor).brush_family == BrushFamily::Carpet;
            }
            if (id == "heal-source") {
                visible = tool == Tool::Brush && (*editor).brush_family == BrushFamily::Heal;
                control.set_selected((*editor).set_heal_source);
            }
            if (id.starts_with("stamp-shape-") || id == "stamp-shapes-menu") {
                visible = tool == Tool::Stamp;
            }
        }
        const bool placement = (*editor).stamp_placement_active();
        if (id == "context-stamp-apply" || id == "context-stamp-cancel") {
            visible = visible && placement;
            control.set_enabled(id == "context-stamp-cancel" || (*editor).can_apply_stamp_placement());
        }
        if (id == "context-stamp-clear" || id == "context-stamp-add" || id == "stamp-shapes-menu") {
            visible = visible && !placement;
        }
        if (button_pages_[i] == 40) {
            visible = page_ == 8 && selection && (*editor).document.selection.active;
            control.set_enabled(id == "warp-place" || id == "warp-cancel" ? (*editor).warp_active()
                                                                          : !(*editor).warp_active());
        }
        if (id == "gradient-tab") {
            visible = (*editor).gradient_fill;
        }
        if (id == "icon-tab") {
            visible = icon;
        }
        if (id == "patterns-tab") {
            control.set_enabled(ink);
        }
        control.set_visible(sticky_visibility(id, visible) && (!collapsed_ || button_pages_[i] == 0));
    }
    for (std::size_t i = 0; i < option_controls_.size(); ++i) {
        gf::Control& control = *option_controls_[i];
        std::string id(control.stable_id().value());
        bool visible = (option_pages_[i] & page_) != 0;
        const bool geometry = tool == Tool::Shape || tool == Tool::Path || tool == Tool::Freehand;
        if (id == "render-quality") {
            visible = visible && geometry;
        }
        if (id.starts_with("geometry-")) { visible = visible && geometry; }
        if (id.starts_with("geometry-start") || id.starts_with("geometry-end")) {
            const Shape shape = (*editor).document.shape;
            visible = visible && (tool == Tool::Path || (tool == Tool::Shape &&
                (shape == Shape::Line || shape == Shape::Bezier || shape == Shape::Arc)));
            control.set_enabled(tool != Tool::Path || (*editor).document.continuous_path);
        }
        if (id == "smooth-lines") {
            visible = visible && !geometry;
        }
        if (page_ == 8 && visible) {
            if (id == "transparent-pattern") {
                visible = false;
            }
            if (id.starts_with("tool-size")) {
                visible = tool == Tool::Pencil || material;
            }
            if (id == "soft-eraser") {
                visible = false;
            }
            if (id == "mix-effect") {
                visible = tool == Tool::Brush && (*editor).brush_family == BrushFamily::Mix;
            }
            if (id.starts_with("effect-")) {
                visible = (tool == Tool::Brush && (*editor).brush_family == BrushFamily::Mix) ||
                          (tool == Tool::Eraser && (*editor).eraser_mode >= EraserMode::Blur);
            }
            if (id.starts_with("heal-")) {
                visible = tool == Tool::Brush && (*editor).brush_family == BrushFamily::Heal;
            }
            if (id.starts_with("stroke-")) {
                visible = tool == Tool::Brush || tool == Tool::Pencil;
            }
            if (id == "dither-brush-noise") {
                visible = tool == Tool::Brush && (*editor).brush_family == BrushFamily::Dither;
            }
            if (id == "spray-glitter") {
                visible = tool == Tool::Brush && (*editor).brush_family == BrushFamily::Additive &&
                          (*editor).document.ink.brush == Brush::Airbrush;
            }
            if (id == "smooth-lines") {
                visible = tool == Tool::Brush && (*editor).brush_family == BrushFamily::Additive;
            }
            if (id == "render-quality") {
                visible = tool == Tool::Shape || tool == Tool::Path || tool == Tool::Freehand;
            }
            if (id == "continuous-path" || id == "edit-path-nodes") {
                visible = tool == Tool::Path;
            }
            if (id == "outline" || id == "fill") {
                visible = tool == Tool::Shape || tool == Tool::Path || tool == Tool::Freehand;
            }
            if (id == "transparent-selection") {
                visible = selection;
            }
            if (id.starts_with("stamp-")) {
                visible = tool == Tool::Stamp;
            }
            if (id == "guide-fill") {
                visible = tool == Tool::Guide;
            }
            if (id.starts_with("picker-")) {
                visible = tool == Tool::Picker;
            }
        }
        if (option_pages_[i] == 40) {
            visible = page_ == 8 && selection && (*editor).document.selection.active;
        }
        const bool placement = (*editor).stamp_placement_active();
        if (id == "stamp-placement-hint") { visible = visible && placement; }
        if (id == "stamp-transparent" || id.starts_with("stamp-width") || id.starts_with("stamp-height")) {
            visible = visible && !placement;
        }
        control.set_visible(sticky_visibility(id, visible) && !collapsed_);
    }
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
    (*editor).invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Ribbon::mix_effect_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor) {
        (*editor).mix_effect = static_cast<MixEffect>(index.value_or(0));
        (*editor).refresh();
    }
}
void Ribbon::render_quality_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor && index && *index < 3) {
        const char* commands[] = {"render-crisp", "render-smooth", "render-smooth-4x"};
        (*editor).execute(commands[*index]);
    }
}
void Ribbon::geometry_stroke_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor && index && *index < 3) {
        (*editor).document.ink.geometry.stroke = static_cast<StrokeStyle>(*index);
        (*editor).document.sync_curve();
        (*editor).document.sync_path();
        (*editor).refresh();
    }
}
void Ribbon::geometry_markers_changed(std::optional<std::size_t>) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor) {
        const std::size_t first = (*geometry_start_).selected_index().value_or(0);
        const std::size_t last = (*geometry_end_).selected_index().value_or(0);
        if (first >= endpoint_marker_count || last >= endpoint_marker_count) { return; }
        (*editor).document.ink.geometry.start = static_cast<EndpointMarker>(first);
        (*editor).document.ink.geometry.end = static_cast<EndpointMarker>(last);
        (*editor).document.sync_curve();
        (*editor).document.sync_path();
        (*editor).refresh();
    }
}
void Ribbon::picker_mode_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor) {
        (*editor).picker_mode = static_cast<SampleMode>(index.value_or(0));
        (*editor).refresh();
    }
}
void Ribbon::word_art_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor) {
        (*editor).text.style.word_art = static_cast<WordArt>(index.value_or(0));
        (*editor).refresh();
    }
}
void Ribbon::spiro_scale_changed(double value) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor) {
        (*editor).resize_spirograph(value);
    }
}
void Ribbon::options_changed(double) {
    if (synchronizing_) {
        return;
    }
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    Ink& ink = (*editor).spiro.selected_peg >= 0 ? (*editor).spiro.pegs[(*editor).spiro.selected_peg].effect
               : secondary_color_                ? (*editor).document.alt_ink
                                                 : (*editor).document.ink;
    ink.grain_scale = (*grain_).value();
    ink.paper_roughness = (*tooth_).value();
    ink.pigment_load = (*load_).value();
    ink.material_angle = (*angle_).value();
    (*editor).spiro.refresh_peg((*editor).spiro.selected_peg);
    (*editor).document.ink.size = static_cast<int>((*tool_size_).value());
    (*editor).document.ink.geometry.corner_radius = (*corner_radius_).value();
    (*editor).document.ink.geometry.organic = (*geometry_organic_).value();
    if ((*editor).text.note) {
        (*(*editor).text.note).padding = static_cast<int>((*sticky_padding_).value());
        const Rect bounds = (*editor).text.bounds;
        (*editor).text.resize({bounds.x, bounds.y, static_cast<int>((*sticky_width_).value()),
                              static_cast<int>((*sticky_height_).value())});
    }
    (*editor).text.style.size = static_cast<int>((*text_size_).value());
    (*editor).text.style.outline_width = (*text_outline_).value();
    (*editor).text.style.skew = (*text_skew_).value();
    (*editor).text.style.perspective = (*text_perspective_).value();
    (*editor).text.style.warp = (*text_warp_).value();
    (*editor).stamp_width = static_cast<int>((*stamp_width_).value());
    (*editor).stamp_height = static_cast<int>((*stamp_height_).value());
    (*editor).mesh_spacing = (*mesh_spacing_).value();
    (*editor).effect_strength = (*effect_strength_).value();
    (*editor).effect_scale = (*effect_scale_).value();
    (*editor).effect_phase = (*effect_phase_).value();
    (*editor).heal_hardness = (*heal_hardness_).value();
    (*editor).heal_correction = (*heal_correction_).value();
    (*editor).stabilizer_lag = (*stabilizer_lag_).value();
    if ((*editor).stamp_hardness != (*stamp_hardness_).value()) {
        (*editor).stamp_hardness = (*stamp_hardness_).value();
        (*editor).update_stamp_hardness();
    }
    if ((*editor).stamp_scale != (*stamp_scale_).value() ||
        (*editor).stamp_angle != (*stamp_angle_).value()) {
        (*editor).stamp_scale = (*stamp_scale_).value();
        (*editor).stamp_angle = (*stamp_angle_).value();
        (*editor).regenerate_stamp();
    }
    (*editor).document.sync_curve();
    (*editor).document.sync_path();
    (*editor).refresh();
}
void Ribbon::apply_choice(const std::string& id) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    Document& document = (*editor).document;
    if (id.starts_with("r-pattern-")) {
        select_pattern(document.ink, static_cast<Pattern>(std::stoi(id.substr(10))));
    } else if (id == "geometry-vary") {
        ++document.ink.geometry.seed;
    } else if (id.starts_with("stamp-shape-")) {
        document.stamp_shape = static_cast<StampShape>(std::stoi(id.substr(12)));
    } else if (id == "stamp-transparent") {
        document.stamp_transparent = !document.stamp_transparent;
    } else if (id == "new-grain") {
        ++document.ink.noise;
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
        (*editor).dither_brush_mode = (*editor).dither_brush_mode == DitherBrushMode::Neighborhood ? DitherBrushMode::Noise : DitherBrushMode::Neighborhood;
    } else if (id == "spray-glitter") {
        (*editor).glitter = !(*editor).glitter;
    } else if (id == "picker-magnifier") {
        (*editor).picker_magnifier = !(*editor).picker_magnifier;
    } else if (id == "soft-eraser") {
        (*editor).eraser_soft = !(*editor).eraser_soft;
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
        (*editor).request_rotation((*rotation_).value());
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

void Ribbon::on_attached_to_window() {
    tooltips_ = std::make_shared<gf::ToolTip>(*attached_window());
    const std::string tool_names[] = {tr("Select"),    tr("Free-form selection"),
                                tr("Pencil"),    tr("Fill with color"),
                                tr("Eraser"),    tr("Color picker"),
                                tr("Magnifier"), tr("Brush"),
                                tr("Shape"),     tr("Path"),
                                tr("Stamp"),     tr("Guide")};
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        gf::Button& control = *buttons_[i];
        std::string id(control.stable_id().value());
        std::string name = control.text().empty() ? control.accessible_name() : control.text();
        if (id.starts_with("tool-") && id != "tool-tab") {
            name = tool_names[std::stoi(id.substr(5))];
        } else if (id.starts_with("shape-")) {
            name = tr(shape_names[std::stoi(id.substr(6))]);
        } else if (id == "help") {
            name = tr("Paint Help (F1)");
        } else if (id == "undo") {
            name = tr("Undo (Ctrl/Command+Z)");
        } else if (id == "redo") {
            name = tr("Redo (Ctrl/Command+Y)");
        } else if (id == "size-menu") {
            name = tr("Brush size");
        } else if (id == "crop") {
            name = tr("Crop");
        } else if (id == "resize") {
            name = tr("Resize");
        } else if (id == "rotate-menu") {
            name = tr("Rotate and flip");
        } else if (id == "cut") {
            name = tr("Cut (Ctrl/Command+X)");
        } else if (id == "copy") {
            name = tr("Copy (Ctrl/Command+C)");
        } else if (id == "shapes-menu") {
            name = tr("More shapes");
        } else if (id.starts_with("swatch-")) {
            name = to_hex(ribbon_color(std::stoi(id.substr(7))));
        }
        if (!name.empty()) {
            control.set_accessible_name(name);
            (*tooltips_).set_tool_tip(buttons_[i], name);
        }
    }
    tiny_icons_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{12, 12});
    small_icons_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{16, 16});
    medium_icons_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{24, 24});
    large_icons_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{32, 32});
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        gf::Button& control = *buttons_[i];
        if (control.stable_id().value() == "tool-5") {
            static_cast<void>(
                (*medium_icons_).add_png("dropper-hand", std::as_bytes(std::span(dropper_icon)), 1));
            control.set_image_key("dropper-hand");
            control.set_image_list(medium_icons_);
            control.set_content_padding({1, 1, 1, 1});
            continue;
        }
        if (control.stable_id().value().starts_with("material-brush-")) {
            continue;
        }
        if (control.stable_id().value() == "undo" || control.stable_id().value() == "redo" ||
            control.stable_id().value() == "save") {
            std::string key = control.image_key();
            if (!(*medium_icons_).contains_key(key)) {
                static_cast<void>((*medium_icons_).add_png(key, ribbon_icon_png(std::stoi(key), 24), 2));
            }
            control.set_image_list(medium_icons_);
            continue;
        }
        if (!control.image_key().empty()) {
            bind_icon(control, std::stoi(control.image_key()),
                      control.requested_bounds().height > 45 && control.requested_bounds().width > 25);
        }
    }
}
void Ribbon::prepare_material_previews() {
    if (brush_previews_) {
        return;
    }
    brush_previews_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{28, 20});
    fill_previews_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{28, 20});
    dynamic_previews_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{28, 20});
    for (int i = 0; i < brush_count; ++i) {
        Ink ink;
        ink.primary = {48, 83, 123, 255};
        ink.secondary = ink.primary;
        ink.brush = static_cast<Brush>(i);
        ink.size = 11;
        for (int fill = 0; fill < 3; ++fill) {
            Image sample;
            sample.reset(56, 40, {255, 255, 255, 255});
            if (fill == 2) {
                DynamicBrushStroke brush;
                brush.segment(sample, {5, 29}, {50, 10}, ink, false);
            } else if (fill == 1) {
                draw_shape(sample, Shape::Rectangle, {4, 4}, {51, 35}, ink, false, true, ink.brush);
            } else {
                stroke(sample, {5, 29}, {50, 10}, ink);
            }
            std::vector<std::uint8_t> png = encode_png(sample);
            std::shared_ptr<gf::ImageList> images = fill == 2   ? dynamic_previews_
                                                    : fill == 1 ? fill_previews_
                                                                : brush_previews_;
            static_cast<void>((*images).add_png(std::to_string(i), std::as_bytes(std::span(png)), 2));
        }
    }
}
void Ribbon::show_materials(bool fill) {
    collapsed_ = false;
    close_popup();
    secondary_color_ = fill;
    page_ = 4;
    synchronize();
}
void Ribbon::bind_icon(gf::Button& button, int icon, bool large) {
    if (icon < 0) {
        return;
    }
    std::shared_ptr<gf::ImageList> images = large ? large_icons_ : small_icons_;
    std::string key = std::to_string(icon);
    if (images && !(*images).contains_key(key)) {
        static_cast<void>((*images).add_png(key, ribbon_icon_png(icon, large ? 32 : 16), 2));
    }
    button.set_image_list(images);
    button.set_image_key(key);
}
void Ribbon::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const std::shared_ptr<Editor> editor = editor_.lock();
    const bool icon = editor && ((*editor).document.atlas.kind == AtlasKind::Icon ||
                                 (*editor).document.atlas.kind == AtlasKind::Cursor);
    const double horizontal_scale = bounds.width / 1200.0;
    set_child_layout(atlas_, {0, 27, bounds.width, 116});
    set_child_layout(icon_, {0, 27, bounds.width, 116});
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        gf::Rect rectangle = (*buttons_[i]).requested_bounds();
        const double button_scale =
            button_pages_[i] == 1 || button_pages_[i] == 0 ? horizontal_scale : bounds.width / 1280.0;
        if (button_pages_[i] == 1) { rectangle.x = home_x(rectangle.x); }
        rectangle.x = button_pages_[i] == 0 && rectangle.y < 27 && rectangle.x >= 56
                          ? 56 + (rectangle.x - 56) * button_scale
                          : rectangle.x * button_scale;
        rectangle.width *= button_scale;
        gf::Button& control = *buttons_[i];
        const std::string id(control.stable_id().value());
        if (id == "gradient-tab" && !icon) {
            rectangle.x -= 75 * button_scale;
        }
        if (id == "help-english") {
            rectangle.width = 116;
            rectangle.x = bounds.width - 150;
        }
        if (id == "help") {
            rectangle.width = 30;
            rectangle.x = bounds.width - rectangle.width;
        }
        if (id.ends_with("-tab")) {
            rectangle.y = control.selected() ? 0 : 4;
            rectangle.height = control.selected() && !collapsed_ ? 29 : control.selected() ? 27 : 23;
            rectangle.width += 3;
        }
        if (!control.image_key().empty() && id != "tool-5" && !id.starts_with("material-brush-") &&
            !id.starts_with("r-pattern-")) {
            const int icon = std::stoi(control.image_key());
            const bool tall = control.requested_bounds().height > 45 && control.requested_bounds().width > 25;
            const int size = tall ? (rectangle.width >= 38 ? 32 : 24) : (rectangle.width < 23 ? 12 : 16);
            const std::shared_ptr<gf::ImageList> images = size == 32   ? large_icons_
                                                          : size == 24 ? medium_icons_
                                                          : size == 16 ? small_icons_
                                                                       : tiny_icons_;
            if (images && !(*images).contains_key(control.image_key())) {
                static_cast<void>(
                    (*images).add_png(control.image_key(), ribbon_icon_png(icon, size == 12 ? 16 : size), 2));
            }
            control.set_image_list(images);
            if (tall) {
                control.set_content_padding({1, 4, 1, 16});
            } else if (control.text().empty()) {
                control.set_content_padding({0, 2, 0, 2});
            }
        }
        const bool tall_caption = control.requested_bounds().height > 45 && !control.text().empty();
        const bool material_caption = id.starts_with("material-brush-");
        const double font_size = material_caption ? std::clamp(11 * horizontal_scale, 8.0, 12.0)
                                 : tall_caption   ? std::clamp(12 * horizontal_scale, 8.0, 14.0)
                                                  : std::clamp(14 * horizontal_scale, 10.0, 15.0);
        gf::FontSpec caption_font{gf::FontRole::control, font_size,
            static_cast<std::uint16_t>(id.ends_with("-tab") && control.selected() ? 700 : 400), false, 0.05};
        if (window() && !control.text().empty() &&
            (tall_caption || id.ends_with("-tab") || id == "primary" || id == "secondary")) {
            const gf::Insets padding = control.content_padding();
            const double available = std::max(1.0, rectangle.width - padding.left - padding.right - 2);
            double widest = 0;
            std::size_t start = 0;
            do {
                const std::size_t end = control.text().find('\n', start);
                const std::string line = control.text().substr(start, end - start);
                widest = std::max(widest, (*window()).resolve_text_layout_utf8(line, caption_font).logical_size.width);
                if (end == std::string::npos) { break; }
                start = end + 1;
            } while (start < control.text().size());
            if (widest > available) {
                caption_font.size = std::max(9.0, caption_font.size * available / widest);
            }
        }
        control.set_font(caption_font);
        set_child_layout(buttons_[i], rectangle);
    }
    for (const std::shared_ptr<gf::Control>& control : option_controls_) {
        gf::Rect rectangle = (*control).requested_bounds();
        if (page_ == 4) {
            std::string id((*control).stable_id().value());
            if (id == "smooth-lines" || id == "render-quality") {
                rectangle = {140, 111, 190, 22};
            }
        }
        const std::shared_ptr<Editor> editor = editor_.lock();
        const std::string id((*control).stable_id().value());
        if (editor && page_ == 8) {
            const Tool tool = (*editor).document.tool;
            if (id.starts_with("tool-size") &&
                (tool == Tool::Brush || tool == Tool::Eraser || tool == Tool::Pencil)) {
                rectangle.x += 1000;
            }
            if (id == "smooth-lines" && (tool == Tool::Brush || tool == Tool::Pencil)) {
                rectangle = {450, 79, 200, 28};
            }
        }
        // Context panels authored on the older 1280 grid use their full width.
        const double option_scale = bounds.width / 1280.0;
        rectangle.x *= option_scale;
        rectangle.width *= option_scale;
        const gf::FontSpec font{gf::FontRole::control, std::clamp(14 * option_scale, 10.0, 15.0), 400, false};
        const std::shared_ptr<gf::Label> label = std::dynamic_pointer_cast<gf::Label>(control);
        const std::shared_ptr<gf::CheckBox> check = std::dynamic_pointer_cast<gf::CheckBox>(control);
        const std::shared_ptr<gf::ComboBox> combo = std::dynamic_pointer_cast<gf::ComboBox>(control);
        const std::shared_ptr<gf::NumericUpDown> number =
            std::dynamic_pointer_cast<gf::NumericUpDown>(control);
        if (label) {
            (*label).set_font(font);
        }
        if (check) {
            (*check).set_font(font);
            (*check).set_content_padding({0, 0, 0, 0});
        }
        if (combo) {
            (*combo).set_font(font);
        }
        if (number) {
            (*(*number).editor()).set_font(font);
            (*number).set_button_width(std::clamp(18 * option_scale, 12.0, 18.0));
        }
        set_child_layout(control, rectangle);
    }
}
void Ribbon::on_paint(gf::Painter& painter, gf::Rect) {
    double width = committed_arranged_bounds().width;
    const gf::GradientStop tabs[] = {{0, interface_color(*this, gf::Color::rgba(155, 181, 216))},
                                     {1, interface_color(*this, gf::Color::rgba(155, 181, 216))}};
    painter.fill_linear_gradient({0, 0, width, 27}, {0, 0}, {0, 27}, tabs);
    if (collapsed_) {
        painter.draw_line({0, 26}, {width, 26}, interface_color(*this, gf::Color::rgba(145, 172, 202)), 1);
        return;
    }
    // Broadly spaced stops keep the body continuous behind every tool group.
    const gf::GradientStop stops[] = {{0, interface_color(*this, gf::Color::rgba(232, 242, 255))},
                                      {0.28, interface_color(*this, gf::Color::rgba(202, 222, 251))},
                                      {0.62, interface_color(*this, gf::Color::rgba(165, 196, 241))},
                                      {1, interface_color(*this, gf::Color::rgba(118, 159, 223))}};
    painter.fill_linear_gradient({0, 27, width, 116}, {0, 27}, {0, 143}, stops);
    painter.draw_line({0, 27}, {width, 27}, interface_color(*this, gf::Color::rgba(171, 192, 216)), 1);
    painter.draw_line({0, 28}, {width, 28}, interface_color(*this, gf::Color::rgba(255, 255, 255, 225)), 1);
    painter.draw_line({0, 141}, {width, 141}, interface_color(*this, gf::Color::rgba(245, 251, 255, 210)), 1);
    painter.draw_line({0, 142}, {width, 142}, interface_color(*this, gf::Color::rgba(145, 172, 202)), 1);
    if (page_ == 64 || page_ == 512) {
        return;
    }
    if (page_ == 4) {
        const double scale = width / 1280;
        const gf::Rect tray{137 * scale, 32, 835 * scale, 83};
        const gf::GradientStop well[] = {{0, interface_color(*this, gf::Color::rgba(248, 251, 255))},
                                         {1, interface_color(*this, gf::Color::rgba(255, 255, 255))}};
        painter.fill_linear_gradient(tray, {0, tray.y}, {0, tray.bottom()}, well);
        painter.stroke_rect(tray, interface_color(*this, gf::Color::rgba(148, 172, 200)), 1);
        painter.draw_line({tray.x + 1, tray.y + 1}, {tray.right() - 1, tray.y + 1},
                          interface_color(*this, gf::Color::rgba(111, 142, 177, 75)), 1);
        painter.draw_line({tray.x, tray.bottom() + 1}, {tray.right(), tray.bottom() + 1},
                          interface_color(*this, gf::Color::rgba(255, 255, 255, 225)), 1);
        paint_separator(painter, 132 * width / 1280, *this);
        paint_separator(painter, 974 * width / 1280, *this);
        return;
    }
    if (page_ != 1) {
        const gf::FontSpec font{gf::FontRole::control, 12, 400, false, 0.08};
        std::shared_ptr<Editor> editor = editor_.lock();
        Tool tool = editor ? (*editor).document.tool : Tool::Pencil;
        bool selection = page_ == 8 && (tool == Tool::Select || tool == Tool::Lasso);
        const std::vector<std::string> labels =
            selection ? std::vector<std::string>{tr("Selection"), tr("Rotation"), tr("Mesh"), tr("Finish"), tr("Controls")}
            : page_ == 16
                ? std::vector<std::string>{tr("Font"), (*editor).text.note ? tr("Paper texture") : tr("Letter treatment"),
                    (*editor).text.note ? tr("Size") : tr("Transform"), tr("Colors"), tr("Finish text")}
            : page_ == 128
                ? std::vector<std::string>{tr("Guides"), tr("Inserts"), tr("Pegs · drag into a hole"), tr("Load and operate")}
            : page_ == 256 ? std::vector<std::string>{tr("Presets"), tr("Color stops"), tr("Direction and selected stop")}
            : page_ == 2   ? std::vector<std::string>{tr("Zoom"), tr("Show or hide"), tr("Display")}
            : page_ == 4   ? std::vector<std::string>{tr("Apply to"), tr("Brushes"), tr("Patterns")}
                           : std::vector<std::string>{tr("Tool settings")};
        const std::vector<double> edges = selection      ? std::vector<double>{0, 230, 496, 764, 906, width}
                                          : page_ == 16  ? std::vector<double>{0, 455, 690, 920, 1062, 1280}
                                          : page_ == 128 ? std::vector<double>{0, 210, 490, 745, 1280}
                                          : page_ == 256 ? std::vector<double>{0, 150, 620, 1280}
                                          : page_ == 2   ? std::vector<double>{0, 220, 390, 620}
                                          : page_ == 4   ? std::vector<double>{0, 132, 660, 974, width}
                                                         : std::vector<double>{0, width};
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (selection && i > 0 && !(*editor).document.selection.active) {
                continue;
            }
            if (page_ == 8 && i > 0 && !selection) {
                bool ink = (*editor).spiro.selected_peg >= 0 || tool == Tool::Fill || tool == Tool::Brush ||
                           tool == Tool::Shape || tool == Tool::Path || tool == Tool::Freehand;
                bool material = tool == Tool::Brush || tool == Tool::Shape || tool == Tool::Path ||
                                tool == Tool::Freehand;
                if ((i == 1 && !ink) || (i > 1 && !material)) {
                    continue;
                }
            }
            const double context_scale = width / 1280;
            double x = edges[i + 1] == width ? width : edges[i + 1] * context_scale;
            paint_separator(painter, x, *this);
            gf::Size size = painter.measure_text_utf8(labels[i], font);
            painter.draw_text_utf8(
                {(edges[i] * context_scale + x - size.width) / 2, page_ == 128 ? 139.0 : 132.0}, labels[i],
                font, interface_color(*this, gf::Color::rgba(72, 91, 112)));
        }
        return;
    }
    const double scale = width / 1200;
    for (double x : {74.0, 338.0, 540.0, 814.0}) {
        paint_separator(painter, (x == 338 ? 330 : home_x(x)) * scale, *this);
    }
    const gf::GradientStop gallery[] = {{0, interface_color(*this, gf::Color::rgba(248, 251, 255))},
                                        {1, interface_color(*this, gf::Color::rgba(255, 255, 255))}};
    painter.fill_linear_gradient({home_x(546) * scale, 36, 176 * scale, 75}, {0, 36}, {0, 111}, gallery);
    painter.stroke_rect({home_x(545.5) * scale, 35.5, 195 * scale, 76}, interface_color(*this, gf::Color::rgba(148, 172, 200)), 1);
    painter.draw_line({home_x(546) * scale, 36.5}, {home_x(721) * scale, 36.5}, interface_color(*this, gf::Color::rgba(111, 142, 177, 35)), 1);
    painter.draw_line({home_x(546) * scale, 112}, {home_x(741) * scale, 112}, interface_color(*this, gf::Color::rgba(255, 255, 255, 225)), 1);
    const std::string captions[] = {"", tr("Tools"), tr("Brushes"), tr("Shapes"), tr("Size"), tr("Colors")};
    const double centers[] = {102, 239, 438, 644, 778, 1010};
    const gf::FontSpec font{gf::FontRole::control, 12, 400, false, 0.08};
    for (int i = 0; i < 5; ++i) {
        gf::Size size = painter.measure_text_utf8(captions[i], font);
        painter.draw_text_utf8({home_x(centers[i]) * scale - size.width / 2, 132}, captions[i], font,
                               interface_color(*this, gf::Color::rgba(72, 91, 112)));
    }
}
void Ribbon::synchronize() {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    show_page();
    Document& document = (*editor).document;
    if (document.tool == Tool::Text) {
        ensure_fonts();
    }
    if (attached_window()) {
        prepare_material_previews();
    }
    (*primary_).set_material(document.tool == Tool::Pencil ? pencil_ink(document.primary_ink())
                                                           : document.primary_ink());
    Ink stored_alt = document.alt_ink;
    stored_alt.primary = document.ink.secondary;
    stored_alt.secondary.a = 0;
    stored_alt.transparent_pattern = true;
    (*secondary_).set_material(document.tool == Tool::Pencil ? pencil_ink(stored_alt) : stored_alt);
    if (tooltips_) {
        for (const std::shared_ptr<SwatchButton>& swatch : {primary_, secondary_}) {
            const std::string description = (*swatch).accessible_name() + "\n" + (*swatch).accessible_description();
            if ((*tooltips_).tool_tip(*swatch) != description) {
                (*tooltips_).set_tool_tip(swatch, description);
            }
        }
    }
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        gf::Button& control = *buttons_[i];
        std::string id(control.stable_id().value());
        if (id == "palette-pane") {
            control.set_text(palette_page_ == 0 ? tr("Basic") : palette_page_ == 1 ? tr("Custom") : tr("Themed"));
        }
        if (id.starts_with("swatch-")) {
            const int index = std::stoi(id.substr(7));
            const std::shared_ptr<SwatchButton> swatch = std::dynamic_pointer_cast<SwatchButton>(buttons_[i]);
            if (swatch) {
                (*swatch).set_color(palette_color(index));
                control.set_accessible_name(palette_page_ == 1 && index == 29 ? tr("Transparency")
                                            : palette_page_ == 2
                                                ? std::string(theme_name(index % 10)) + tr(" color ") +
                                                      std::to_string(index / 10 + 1)
                                                : std::string(palette_page_ == 1 ? tr("Custom") : tr("Basic")) +
                                                      tr(" color ") + std::to_string(index + 1));
                if (tooltips_) {
                    (*tooltips_)
                        .set_tool_tip(buttons_[i],
                                      control.accessible_name() + ": " + to_hex(palette_color(index)));
                }
            }
        }
        bool selected = false;
        if (id == "text-place") {
            control.set_text((*editor).text.note ? tr("Apply") : tr("Place text"));
        }
        if (id.starts_with("tool-") && id != "tool-tab") {
            selected = document.tool == ribbon_tools[std::stoi(id.substr(5))] ||
                       (id == "tool-0" && document.tool == Tool::Lasso) ||
                       (id == "tool-9" && document.tool == Tool::Freehand) ||
                       (id == "tool-11" && editor && (*editor).guide.active());
        }
        if (id.starts_with("shape-")) {
            selected =
                document.tool == Tool::Shape && static_cast<int>(document.shape) == std::stoi(id.substr(6));
        }
        if (id == "text") {
            selected = document.tool == Tool::Text;
        }
        if (id.starts_with("material-brush-")) {
            const Ink& choice = (*editor).spiro.selected_peg >= 0
                                    ? (*editor).spiro.pegs[(*editor).spiro.selected_peg].effect
                                : secondary_color_ ? document.alt_ink
                                                   : document.ink;
            selected = choice.pattern == Pattern::Solid &&
                       std::stoi(id.substr(15)) == static_cast<int>(choice.brush);
            control.set_image_list(secondary_color_               ? fill_previews_
                                   : document.tool == Tool::Brush ? dynamic_previews_
                                                                  : brush_previews_);
        }
        if (id == "material-edge" || id == "material-fill") {
            selected = secondary_color_ == (id == "material-fill");
        }
        if (id == "edge-switch") {
            selected = document.shape_outline;
            control.set_accessible_name(document.shape_outline ? tr("Edge on") : tr("Edge off"));
        }
        if (id == "fill-switch") {
            selected = document.tool == Tool::Guide ? (*editor).guide.fill : document.shape_fill;
            control.set_accessible_name(selected ? tr("Fill on") : tr("Fill off"));
        }
        if (id == "brush-menu") {
            selected = document.tool == Tool::Brush;
        }
        if (id == "help") {
            selected = (*editor).show_help;
        }
        if (id == "primary") {
            selected = !secondary_color_;
        }
        if (id == "secondary") {
            selected = secondary_color_;
        }
        if (id.ends_with("-tab")) {
            selected = id == (page_ == 1     ? "home-tab"
                              : page_ == 2   ? "view-tab"
                              : page_ == 4   ? "patterns-tab"
                              : page_ == 64  ? "atlas-tab"
                              : page_ == 512 ? "icon-tab"
                              : page_ == 256 ? "gradient-tab"
                                             : "tool-tab");
        }
        if (id.starts_with("r-pattern-")) {
            const Ink& choice = (*editor).spiro.selected_peg >= 0
                                    ? (*editor).spiro.pegs[(*editor).spiro.selected_peg].effect
                                    : secondary_color_ ? document.alt_ink : document.ink;
            selected = choice.brush == Brush::Round && static_cast<int>(choice.pattern) == std::stoi(id.substr(10));
        }
        if (id.starts_with("stamp-shape-")) {
            selected = static_cast<int>(document.stamp_shape) == std::stoi(id.substr(12));
        }
        if (id == "stamp-shapes-menu") {
            control.set_text(stamp_shape_name(document.stamp_shape));
            control.set_accessible_name(stamp_shape_name(document.stamp_shape));
            bind_icon(control, 100 + static_cast<int>(stamp_geometry_shape(document.stamp_shape)), false);
        }
        if (id == "tool-tab") {
            const std::string names[] = {tr("Selection"), tr("Selection"), tr("Pencil"),         tr("Fill"),
                                   tr("Text"),      tr("Eraser"),    tr("Color picker"),   tr("Magnifier"),
                                   tr("Brush"),     tr("Shape"),     tr("Path"),           tr("Stamp"),
                                   tr("Mesh"),      tr("Guide"),     tr("Freehand shape"), tr("Spirograph")};
            static_assert(std::size(names) == static_cast<std::size_t>(Tool::Spirograph) + 1);
            control.set_text(page_ == 128  ? tr("Spirograph")
                             : page_ == 32 ? tr("Transform")
                             : (*editor).text.note ? tr("Sticky note")
                                           : names[static_cast<int>(document.tool)]);
            control.set_accessible_name(page_ == 128 ? tr("Spirograph tools")
                                        : page_ == 32
                                            ? tr("Transform tools")
                                            : std::string(names[static_cast<int>(document.tool)]) + tr(" tools"));
        }
        if (id.starts_with("spiro-guide-")) {
            selected = (*editor).spiro.guide == std::stoi(id.substr(12));
            control.set_enabled((*editor).spiro.compatible(std::stoi(id.substr(12)), (*editor).spiro.insert));
        }
        if (id.starts_with("spiro-insert-")) {
            selected = (*editor).spiro.inserted && (*editor).spiro.insert == std::stoi(id.substr(13));
            control.set_enabled((*editor).spiro.compatible((*editor).spiro.guide, std::stoi(id.substr(13))));
        }
        if (id == "spiro-guides-menu") {
            control.set_text(tr(spiro_guides()[(*editor).spiro.guide].name));
            control.set_accessible_name(std::string(tr("Guide catalog: ")) + control.text());
        }
        if (id == "spiro-inserts-menu") {
            control.set_text(tr(spiro_inserts()[(*editor).spiro.insert].name));
            control.set_accessible_name(std::string(tr("Insert catalog: ")) + control.text());
        }
        if (id == "spiro-brushes-menu") {
            const int peg = (*editor).spiro.selected_peg;
            control.set_enabled(peg >= 0);
            control.set_text(peg < 0
                                 ? tr("Select a peg · choose medium")
                                 : std::string(tr("Peg ")) + std::to_string(peg + 1) + " · " +
                                       tr(brush_names[static_cast<int>((*editor).spiro.pegs[peg].effect.brush)]));
            control.set_accessible_name(control.text());
        }
        if (id == "spiro-deselect") {
            control.set_enabled((*editor).spiro.selected_peg >= 0);
        }
        if (id == "spiro-holes-menu") {
            control.set_enabled((*editor).spiro.inserted);
        }
        if (id == "spiro-load-primary" || id == "spiro-load-alt") {
            control.set_enabled((*editor).spiro.selected_peg >= 0);
        }
        if (id == "spiro-outside") {
            selected = (*editor).spiro.outside || (*editor).spiro.capsule();
            control.set_enabled(!(*editor).spiro.rack() && !(*editor).spiro.capsule() && !(*editor).spiro.quad());
        }
        if (id == "spiro-fill") {
            selected = document.tool == Tool::Fill;
        }
        if (id == "spiro-operate") {
            selected = document.tool == Tool::Spirograph && !(*editor).spiro_lift;
        }
        if (id == "spiro-lift") {
            selected = document.tool == Tool::Spirograph && (*editor).spiro_lift;
        }
        if (id.starts_with("spiro-peg-") || id == "spiro-remove" || id == "spiro-clear-pegs" ||
            id == "spiro-operate" || id == "spiro-fill" || id == "spiro-lift") {
            control.set_enabled((*editor).spiro.inserted &&
                                (id != "spiro-operate" || !(*editor).spiro.detached));
        }
        if (id == "gradient-linear") {
            selected = (*editor).fill_gradient.kind == GradientKind::Linear;
        }
        if (id == "gradient-radial") {
            selected = (*editor).fill_gradient.kind == GradientKind::Radial;
        }
        if (id == "gradient-add") {
            control.set_enabled((*editor).fill_gradient.stops.size() < 32);
        }
        if (id == "gradient-remove") {
            control.set_enabled((*editor).fill_gradient.stops.size() > 2);
        }
        control.set_selected(selected);
    }
    if (ordered_tab_page_ != page_) {
        // Backmost first, foreground last: paint and hit testing share this order.
        for (std::size_t index = buttons_.size(); index > 0; --index) {
            if ((*buttons_[index - 1]).stable_id().value().ends_with("-tab")) {
                (*buttons_[index - 1]).bring_to_front();
            }
        }
        for (const std::shared_ptr<gf::Button>& button : buttons_) {
            if ((*button).stable_id().value().ends_with("-tab") && (*button).selected()) {
                (*button).bring_to_front();
            }
        }
        ordered_tab_page_ = page_;
    }
    synchronizing_ = true;
    synchronize_sticky_ribbon();
    (*spiro_scale_).set_value((*editor).spiro.scale);
    const Ink& material = (*editor).spiro.selected_peg >= 0
                              ? (*editor).spiro.pegs[(*editor).spiro.selected_peg].effect
                          : secondary_color_ ? document.alt_ink
                                             : document.ink;
    (*grain_).set_value(material.grain_scale);
    (*tooth_).set_value(material.paper_roughness);
    (*load_).set_value(material.pigment_load);
    (*angle_).set_value(material.material_angle);
    (*tool_size_).set_value(document.ink.size);
    (*corner_radius_).set_value(document.ink.geometry.corner_radius);
    (*geometry_organic_).set_value(document.ink.geometry.organic);
    (*geometry_stroke_).set_selected_index(static_cast<std::size_t>(document.ink.geometry.stroke));
    (*geometry_start_).set_selected_index(static_cast<std::size_t>(document.ink.geometry.start));
    (*geometry_end_).set_selected_index(static_cast<std::size_t>(document.ink.geometry.end));
    (*render_quality_).set_selected_index(!document.ink.smooth ? 0 : document.ink.supersample ? 2 : 1);
    (*picker_mode_).set_selected_index(static_cast<std::size_t>((*editor).picker_mode));
    (*text_size_).set_value((*editor).text.style.size);
    (*stamp_width_).set_value((*editor).stamp_width);
    (*stamp_height_).set_value((*editor).stamp_height);
    (*stamp_scale_).set_value((*editor).stamp_scale);
    (*stamp_angle_).set_value((*editor).stamp_angle);
    (*mesh_spacing_).set_value((*editor).mesh_spacing);
    (*stamp_hardness_).set_value((*editor).stamp_hardness);
    (*effect_strength_).set_value((*editor).effect_strength);
    (*effect_scale_).set_value((*editor).effect_scale);
    (*effect_phase_).set_value((*editor).effect_phase);
    (*heal_hardness_).set_value((*editor).heal_hardness);
    (*heal_correction_).set_value((*editor).heal_correction);
    (*stabilizer_lag_).set_value((*editor).stabilizer_lag);
    (*mix_effect_).set_selected_index(static_cast<std::size_t>((*editor).mix_effect));
    std::optional<std::size_t> font_index;
    if ((*editor).text.style.face_path.empty()) {
        font_index = (*editor).text.style.mono ? 1 : 0;
    } else {
        for (std::size_t i = 0; i < font_paths_.size(); ++i) {
            if (font_paths_[i] == (*editor).text.style.face_path) {
                font_index = i + 2;
                break;
            }
        }
    }
    if (!font_index && !(*editor).text.style.face_path.empty()) {
        font_paths_.push_back((*editor).text.style.face_path);
        (*font_).add_item(font_label((*editor).text.style.face_path));
        font_index = font_paths_.size() + 1;
    }
    (*font_).set_selected_index(font_index);
    (*word_art_).set_selected_index(static_cast<std::size_t>((*editor).text.style.word_art));
    (*text_outline_).set_value((*editor).text.style.outline_width);
    (*text_skew_).set_value((*editor).text.style.skew);
    (*text_perspective_).set_value((*editor).text.style.perspective);
    (*text_warp_).set_value((*editor).text.style.warp);
    for (const std::shared_ptr<gf::CheckBox>& check : checks_) {
        std::string id((*check).stable_id().value());
        if (id.starts_with("text-")) {
            const TextStyle& style = (*editor).text.style;
            (*check).set_checked(id == "text-contour"     ? style.contour
                                 : id == "text-bold"      ? style.bold
                                 : id == "text-italic"    ? style.italic
                                 : id == "text-underline" ? style.underline
                                 : id == "text-strikeout" ? style.strikeout
                                 : id == "text-opaque"    ? style.opaque
                                                          : style.word_wrap);
            continue;
        }
        (*check).set_checked(id == "guide-fill"         ? (*editor).guide.fill
                             : id == "heal-clone"       ? (*editor).heal_options.clone
                             : id == "heal-aligned"     ? (*editor).heal_options.aligned
                             : id == "heal-alpha"       ? (*editor).heal_options.preserve_alpha
                             : id == "stroke-stabilize" ? (*editor).stabilize
                             : id == "dither-brush-noise"
                                 ? (*editor).dither_brush_mode == DitherBrushMode::Noise
                             : id == "spray-glitter"         ? (*editor).glitter
                             : id == "picker-magnifier"      ? (*editor).picker_magnifier
                             : id == "soft-eraser"           ? (*editor).eraser_soft
                             : id == "show-rulers"           ? (*editor).show_rulers
                             : id == "show-grid"             ? (*editor).show_grid
                             : id == "show-status"           ? (*editor).show_status
                             : id == "transparent-pattern"   ? material.transparent_pattern
                             : id == "transparent-selection" ? document.transparent_selection
                             : id == "stamp-transparent"     ? document.stamp_transparent
                             : id == "alt-carries-body"      ? document.alt_carries_body
                             : id == "smooth-lines"          ? document.ink.smooth
                             : id == "continuous-path"       ? document.continuous_path
                             : id == "edit-path-nodes"       ? (*editor).edit_path_nodes
                             : id == "outline"               ? document.shape_outline
                                                             : document.shape_fill);
    }
    synchronize_gradient();
    synchronizing_ = false;
}
void Ribbon::cancel_spiro_drag() {
    if (spiro_drag_width_) {
        spiro_drag_width_ = 0;
        set_pointer_capture(false);
    }
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
    if (editor && event.button == gf::PointerButton::primary && event.action == gf::PointerAction::down) {
        for (const std::shared_ptr<gf::Button>& choice : buttons_) {
            const std::string id((*choice).stable_id().value());
            if (id.starts_with("spiro-peg-") && (*choice).visible() && (*choice).enabled() &&
                (*choice).absolute_bounds().contains(event.position)) {
                const int width = std::stoi(id.substr(10));
                (*editor).spiro_tray_pointer(width, event);
                spiro_drag_width_ = width;
                set_pointer_capture(true);
                event.handled = true;
                return;
            }
        }
    }
    if (event.button != gf::PointerButton::secondary) {
        return;
    }
    if (event.action == gf::PointerAction::down) {
        for (const std::shared_ptr<gf::Button>& choice : buttons_) {
            std::string id((*choice).stable_id().value());
            if ((id.starts_with("swatch-") || id.starts_with("r-pattern-") ||
                 id.starts_with("material-brush-")) &&
                (*choice).visible() && (*choice).enabled() &&
                (*choice).absolute_bounds().contains(event.position)) {
                secondary_target_ = choice;
                set_pointer_capture(true);
                event.handled = true;
                return;
            }
        }
    } else if (event.action == gf::PointerAction::up && secondary_target_) {
        std::shared_ptr<gf::Button> choice = std::move(secondary_target_);
        set_pointer_capture(false);
        event.handled = true;
        if ((*choice).absolute_bounds().contains(event.position)) {
            secondary_color_ = true;
            clicked(*choice);
        }
    }
}
Color Ribbon::palette_color(int index) const {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (palette_page_ == 1 && editor) {
        return index == 29 ? Color{255, 255, 255, 0}
                           : (*editor).custom_colors.colors.at(static_cast<std::size_t>(index));
    }
    return palette_page_ == 2 ? themed_color(index) : ribbon_color(index);
}
void Ribbon::clicked(gf::ButtonBase& button) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    std::string id(button.stable_id().value());
    if (gradient_clicked(id)) {
        return;
    }
    if (id == "edit-custom-pattern") { (*editor).toggle_pattern_canvas(); return; }
    if (popup_owner_.get() == &button) {
        close_popup();
        return;
    }
    if (id.starts_with("r-pattern-") && (*editor).spiro.selected_peg >= 0) {
        select_pattern((*editor).spiro.pegs[(*editor).spiro.selected_peg].effect,
                       static_cast<Pattern>(std::stoi(id.substr(10))));
        (*editor).spiro.refresh_peg((*editor).spiro.selected_peg);
        (*editor).refresh();
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
            for (int i = 0; i < spiro.hole_count(); ++i) {
                if (!spiro.pegs[i].seated) { hole = i; break; }
            }
        }
        if (hole >= 0) {
            const int width = std::stoi(id.substr(10));
            if (spiro.pegs[hole].seated) { spiro.pegs[hole].width = width; spiro.refresh_peg(hole); }
            else { spiro.seat(hole, {true, false, {}, width}); }
            spiro.select_peg(hole);
            (*editor).refresh();
        }
        return;
    }
    if (id.starts_with("spiro-") && !id.starts_with("spiro-peg-")) {
        (*editor).spiro_choice(id);
        return;
    }
    if (id == "palette-pane") {
        palette_page_ = (palette_page_ + 1) % 3;
        synchronize();
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
    if (id == "outline-menu" || id == "fill-menu" || id == "material-edge" || id == "material-fill") {
        show_materials(id == "fill-menu" || id == "material-fill");
        return;
    }
    if (id == "alt-carries-body") {
        (*editor).execute("alt-carries-body");
        return;
    }
    if (id.starts_with("material-brush-")) {
        Brush selected = static_cast<Brush>(std::stoi(id.substr(15)));
        if ((*editor).spiro.assign_brush(selected)) {
            (*editor).refresh();
            return;
        }
        (*editor).brush_family = BrushFamily::Additive;
        Ink& material = secondary_color_ ? (*editor).document.alt_ink : (*editor).document.ink;
        select_brush(material, selected);
        if (secondary_color_) {
            (*editor).document.shape_fill_brush = selected;
        }
        (*editor).document.sync_curve();
        (*editor).document.sync_path();
        (*editor).refresh();
        return;
    }
    if (id.ends_with("-tab")) {
        close_popup();
        int next = id == "home-tab"       ? 1
                   : id == "view-tab"     ? 2
                   : id == "patterns-tab" ? 4
                   : id == "atlas-tab"    ? 64
                   : id == "icon-tab"     ? 512
                   : id == "gradient-tab" ? 256
                   : ((*editor).document.tool == Tool::Spirograph ||
                      ((*editor).spiro.active &&
                       ((*editor).document.tool == Tool::Fill || (*editor).spiro.selected_peg >= 0)))
                       ? 128
                   : (*editor).document.tool == Tool::Text ? 16
                                                           : 8;
        collapsed_ = next == page_ ? !collapsed_ : false;
        page_ = next;
        synchronize();
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
    if (id == "transparent-pattern") {
        Ink& material = secondary_color_ ? (*editor).document.alt_ink : (*editor).document.ink;
        material.transparent_pattern = !material.transparent_pattern;
        (*editor).document.sync_path();
        (*editor).refresh();
        return;
    }
    if (id == "new-grain") {
        Ink& material = secondary_color_ ? (*editor).document.alt_ink : (*editor).document.ink;
        ++material.noise;
        (*editor).document.sync_path();
        (*editor).refresh();
        return;
    }
    if (id.starts_with("r-pattern-")) {
        Ink& material = secondary_color_ ? (*editor).document.alt_ink : (*editor).document.ink;
        select_pattern(material, static_cast<Pattern>(std::stoi(id.substr(10))));
        if (secondary_color_) {
            (*editor).document.shape_fill_brush = Brush::Round;
        }
        (*editor).document.sync_curve();
        (*editor).document.sync_path();
        (*editor).refresh();
        return;
    }
    if (id.starts_with("swatch-")) {
        Color& color = secondary_color_ ? (*editor).document.ink.secondary : (*editor).document.ink.primary;
        color = palette_color(std::stoi(id.substr(7)));
        Ink& material = secondary_color_ ? (*editor).document.alt_ink : (*editor).document.ink;
        if (material.pattern == Pattern::None) {
            select_pattern(material, Pattern::Solid);
        }
        (*editor).document.sync_curve();
        (*editor).document.sync_path();
        (*editor).refresh();
        return;
    }
    if (id.starts_with("shape-")) {
        (*editor).choose_shape(static_cast<Shape>(std::stoi(id.substr(6))));
        return;
    }
    apply_choice(id);
}
std::shared_ptr<gf::Button> Ribbon::add_popup_button(gf::Panel& panel, const std::string& id,
                                                     const std::string& text, int icon, gf::Rect bounds,
                                                     bool selected) {
    std::shared_ptr<gf::Button> item;
    if (id.starts_with("size-")) {
        item = gf::make_control<WeightButton>(gf::StableId("popup-" + id), text, std::stoi(id.substr(5)));
    } else if (id.starts_with("spiro-guide-") || id.starts_with("spiro-insert-")) {
        const bool guide = id.starts_with("spiro-guide-");
        item = gf::make_control<SpiroPartButton>(gf::StableId("popup-" + id), text, guide ? 0 : 1,
                                                 std::stoi(id.substr(guide ? 12 : 13)));
    } else {
        item = gf::make_control<gf::Button>(gf::StableId("popup-" + id), text);
    }
    (*item).set_requested_bounds(bounds);
    (*item).set_font({gf::FontRole::control, 14, 400, false, 0.05});
    (*item).set_text_image_relation(gf::TextImageRelation::image_before_text);
    (*item).set_text_alignment(gf::ContentAlignment::middle_left);
    (*item).set_image_alignment(gf::ContentAlignment::middle_left);
    (*item).set_content_padding({8, 4, 8, 4});
    (*item).set_selected(selected);
    bind_icon(*item, icon, false);
    popup_subscriptions_.push_back((*item).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::popup_clicked>(*this)));
    if (id.starts_with("shape-")) {
        std::string name = tr(shape_names[std::stoi(id.substr(6))]);
        (*item).set_accessible_name(name);
        std::string key = std::to_string(icon);
        if (!(*medium_icons_).contains_key(key)) {
            static_cast<void>((*medium_icons_).add_png(key, ribbon_icon_png(icon, 24), 2));
        }
        (*item).set_image_list(medium_icons_);
        (*item).set_content_padding({4, 4, 4, 4});
    }
    if (id.starts_with("spiro-guide-") || id.starts_with("spiro-insert-")) {
        (*item).set_content_padding({2, 40, 2, 2});
        (*item).set_font({gf::FontRole::control, 11, 400, false});
        (*item).set_text_alignment(gf::ContentAlignment::bottom_center);
    }
    panel.add_child(item);
    return item;
}
void Ribbon::dropdown(gf::DropDownButton& button) {
    if (popup_owner_.get() == &button) {
        close_popup();
        return;
    }
    close_popup();
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    Document& document = (*editor).document;
    std::string id(button.stable_id().value());
    std::shared_ptr<gf::Panel> panel = gf::make_control<gf::Panel>(gf::StableId("ribbon-popup-content"));
    (*panel).set_theme_override(gallery_theme());
    gf::SurfaceMaterial material;
    material.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(250, 252, 255))};
    material.border = gf::MaterialBorder{gf::Color::rgba(141, 164, 190), 1};
    material.shadows.push_back({{0, 3}, 7, 0, gf::Color::rgba(30, 50, 75, 60), false});
    (*panel).set_authored_surface_material(material);
    double width = 232, height = 10;
    if (id == "gradient-presets-menu") {
        const gf::Size size = gradient_gallery(*panel);
        width = size.width;
        height = size.height;
    } else if (id == "spiro-guides-menu" || id == "spiro-inserts-menu") {
        const bool guides = id == "spiro-guides-menu";
        const int count = static_cast<int>(guides ? spiro_guides().size() : spiro_inserts().size());
        width = 568;
        height = 8 + 76 * ((count + 4) / 5);
        for (int i = 0; i < count; ++i) {
            const bool selected = guides ? (*editor).spiro.guide == i : (*editor).spiro.insert == i;
            const std::shared_ptr<gf::Button> part = add_popup_button(
                *panel, std::string(guides ? "spiro-guide-" : "spiro-insert-") + std::to_string(i),
                guides ? tr(spiro_guides()[i].name) : tr(spiro_inserts()[i].name), -1,
                {4.0 + (i % 5) * 112, 4.0 + (i / 5) * 76, 110, 74}, selected);
            (*part).set_enabled(guides ? (*editor).spiro.compatible(i, (*editor).spiro.insert)
                                       : (*editor).spiro.compatible((*editor).spiro.guide, i));
        }
    } else if (id == "spiro-holes-menu") {
        const Spirograph& spiro = (*editor).spiro;
        const int columns = std::max(1, (spiro.hole_count() + 11) / 12);
        const int rows = (spiro.hole_count() + columns - 1) / columns;
        width = 280 * columns + 8;
        height = 8 + 30 * rows;
        for (int i = 0; i < spiro.hole_count(); ++i) {
            const std::string label = std::to_string(i + 1) + " · " +
                (spiro.pegs[i].seated ? tr("Select peg") : tr("Place fine peg"));
            add_popup_button(*panel, "spiro-hole-" + std::to_string(i), label, -1,
                             {4.0 + 280 * (i / rows), 4.0 + 30 * (i % rows), 278, 29},
                             spiro.selected_peg == i);
        }
    } else if (id == "spiro-brushes-menu") {
        prepare_material_previews();
        width = 436;
        height = 8 + 32 * ((brush_count + 1) / 2);
        for (int i = 0; i < brush_count; ++i) {
            const std::shared_ptr<gf::Button> part = add_popup_button(
                *panel, "peg-brush-" + std::to_string(i), tr(brush_names[i]), -1,
                {4.0 + (i % 2) * 214, 4.0 + (i / 2) * 32, 212, 31},
                (*editor).spiro.selected_peg >= 0 &&
                    static_cast<int>((*editor).spiro.pegs[(*editor).spiro.selected_peg].effect.brush) == i);
            (*part).set_image_key(std::to_string(i));
            (*part).set_image_list(dynamic_previews_);
        }
    } else if (id == "stamp-shapes-menu") {
        width = 280;
        height = 8 + 34 * ((stamp_shape_count + 6) / 7);
        for (int i = 0; i < stamp_shape_count; ++i) {
            StampShape shape = static_cast<StampShape>(i);
            std::shared_ptr<gf::Button> item = add_popup_button(
                *panel, "stamp-shape-" + std::to_string(i), "",
                100 + static_cast<int>(stamp_geometry_shape(shape)),
                {6.0 + (i % 7) * 38, 5.0 + (i / 7) * 34, 34, 33}, document.stamp_shape == shape);
            (*item).set_accessible_name(stamp_shape_name(shape));
        }
    } else if (id == "shapes-menu") {
        width = 280;
        height = 8 + 34 * ((shape_count + 6) / 7);
        for (int i = 0; i < shape_count; ++i) {
            add_popup_button(*panel, "shape-" + std::to_string(i), "", 100 + i,
                             {6.0 + (i % 7) * 38, 5.0 + (i / 7) * 34, 34, 33},
                             document.shape == static_cast<Shape>(i));
        }
    } else if (id == "size-menu") {
        const int values[] = {1, 2, 3, 4, 5, 7, 8, 12, 16, 24, 32, 48, 64};
        width = 172;
        height = 8 + 28 * (std::size(values) + 1);
        for (std::size_t i = 0; i < std::size(values); ++i) {
            add_popup_button(*panel, "size-" + std::to_string(values[i]), std::to_string(values[i]) + tr(" px"),
                             -1, {4, 4 + i * 28.0, width - 8, 27}, document.ink.size == values[i]);
        }
        add_popup_button(*panel, "custom-size", tr("Custom size…"), -1,
                         {4, 4 + std::size(values) * 28.0, width - 8, 27});
    } else if (id == "tool-pattern-menu") {
        pattern_previews_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{82, 28});
        width = 440;
        const int rows = (pattern_count - 1 + 3) / 4;
        height = 8 + rows * 62 + 90;
        int slot = 0;
        for (int i = 0; i < pattern_count; ++i) {
            if (i == static_cast<int>(Pattern::None)) { continue; }
            Image sample;
            sample.reset(164, 56);
            Ink ink;
            ink.primary = {47, 73, 103, 255};
            ink.pattern = static_cast<Pattern>(i);
            ink.custom_pattern = (*editor).custom_pattern;
            for (int y = 0; y < sample.height; ++y) {
                for (int x = 0; x < sample.width; ++x) { sample.set(x, y, patterned(ink, x / 2, y / 2)); }
            }
            const std::vector<std::uint8_t> png = encode_png(sample);
            static_cast<void>((*pattern_previews_).add_png(std::to_string(i), std::as_bytes(std::span(png)), 2));
            const std::shared_ptr<gf::Button> item = add_popup_button(*panel, "pattern-" + std::to_string(i), tr(pattern_names[i]), -1,
                {5.0 + (slot % 4) * 107, 4.0 + (slot / 4) * 62, 105, 60}, static_cast<int>(document.ink.pattern) == i);
            (*item).set_font({gf::FontRole::control, 12, 400, false});
            (*item).set_image_list(pattern_previews_);
            (*item).set_image_key(std::to_string(i));
            (*item).set_text_image_relation(gf::TextImageRelation::image_above_text);
            (*item).set_text_alignment(gf::ContentAlignment::middle_center);
            ++slot;
        }
        const double bottom = 8 + rows * 62;
        add_popup_button(*panel, "edit-custom-pattern", tr("Edit custom pattern…"), -1, {5, bottom, width - 10, 27});
        add_popup_button(*panel, "fill-off", tr("No fill"), -1, {5, bottom + 29, width - 10, 27}, !document.shape_fill);
        add_popup_button(*panel, "transparent-pattern", tr("Transparent second color"), -1,
                         {5, bottom + 58, width - 10, 27}, document.ink.transparent_pattern);
    } else {
        std::vector<std::string> ids, texts;
        if (id == "tool-0") {
            ids = {"tool-0",     "lasso-free", "lasso-tight",      "lasso-void",
                   "lasso-wand", "select-all", "invert-selection", "transparent-selection", "selection-dither"};
            texts = {tr("Rectangular selection"),
                     tr("Free-form selection"),
                     tr("Tightening lasso"),
                     tr("Inner-void lasso"),
                     tr("Magic wand (all similar colors)"),
                     tr("Select all"),
                     tr("Invert selection"),
                     tr("Transparent selection"), tr("Dither / posterize…")};
        }
        if (id == "tool-4") {
            ids = {"eraser-0", "eraser-1", "eraser-2", "eraser-3", "eraser-4"};
            texts = {tr("Hard eraser"), tr("Soft eraser"), tr("Blur"), tr("Sharpen"), tr("Smudge")};
        }
        if (id == "paste") {
            ids = {"paste", "open"};
            texts = {tr("Paste"), tr("Open image…")};
        }
        if (id == "rotate-menu") {
            ids = {"rotate-right", "rotate-left", "rotate-180", "flip-horizontal", "flip-vertical", "numeric-transform"};
            texts = {tr("Rotate right 90°"), tr("Rotate left 90°"), tr("Rotate 180°"), tr("Flip horizontal"),
                     tr("Flip vertical"), tr("Transform") + "…"};
        }
        if (id == "outline-menu") {
            ids = {"outline-on", "outline-off"};
            texts = {tr("Solid outline"), tr("No outline")};
        }
        if (id == "fill-menu") {
            ids = {"fill-on", "fill-off"};
            texts = {tr("Solid fill"), tr("No fill")};
        }
        if (id == "view-menu") {
            ids = {"zoom-in", "zoom-out", "actual-size", "fit"};
            texts = {tr("Zoom in"), tr("Zoom out"), "100%", tr("Fit canvas")};
        }
        if (id == "tool-9") {
            ids = path_swap_menu_
                      ? std::vector<std::string>{"path-swap-bezier", "path-swap-arc"}
                      : std::vector<std::string>{"tool-9", "freehand-shape", "spirograph", "path-swap"};
            texts = path_swap_menu_ ? std::vector<std::string>{tr("Bézier"), tr("Arc")}
                                    : std::vector<std::string>{tr("Continue path"), tr("Freehand shape"),
                                                               tr("Spirograph"), tr("Swap segment ▸")};
            path_swap_menu_ = false;
        }
        if (id == "tool-11") {
            ids = guide_swap_menu_ ? std::vector<std::string>{"guide-swap-bezier", "guide-swap-arc"}
                                   : std::vector<std::string>{"guide-edit", "guide-swap", "guide-clear"};
            texts = guide_swap_menu_
                        ? std::vector<std::string>{tr("Bézier"), tr("Arc")}
                        : std::vector<std::string>{tr("Edit guide"), tr("Swap segment ▸"), tr("Unset guide")};
            guide_swap_menu_ = false;
        }
        if (id == "tool-3") {
            ids = {"bucket-solid", "bucket-gradient"};
            texts = {tr("Solid / material fill"), tr("Use gradient")};
        }
        if (id == "brush-menu") {
            ids = {"family-additive", "family-glitter", "family-mix", "family-heal", "family-carpet", "family-dither"};
            texts = {tr("Additive brushes"), tr("Fine glitter"), tr("Mix existing pixels"), tr("Heal / continuous clone"),
                     tr("Carpet generator…"), tr("Dithering brush")};
        }
        if (id == "tool-10") {
            ids = {"tool-10", "sticky-note", "stamp-add", "stamp-clear"};
            texts = {tr("Capture / place stamp"), tr("Sticky note"), tr("Add material"), tr("Clear captured stamp")};
        }
        height = 8 + 30 * ids.size();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            add_popup_button(*panel, ids[i], texts[i], -1, {4, 4 + i * 30.0, width - 8, 29});
        }
    }
    popup_owner_ = std::dynamic_pointer_cast<gf::DropDownButton>(button.shared_from_this());
    gf::AnchoredPopupPlacement placement;
    placement.preferred_size = {width, height};
    placement.gap = 2;
    popup_layer_ =
        gf::make_control<gf::AnchoredPopupLayer>(gf::StableId("ribbon-popup"), popup_owner_, placement);
    (*popup_layer_).set_content(panel);
    popup_subscriptions_.push_back(
        (*popup_layer_)
            .dismiss_requested()
            .subscribe(*this, gf::Delegate<gf::PopupDismissReason>::bind<Ribbon, &Ribbon::dismissed>(*this)));
    popup_ = (*attached_window()).open_popup(popup_owner_, popup_layer_);
    if (id == "stamp-shapes-menu") {
        for (int i = 0; i < stamp_shape_count; ++i) {
            gf::Control::Ptr shape = (*attached_window()).find("popup-stamp-shape-" + std::to_string(i));
            (*tooltips_).set_tool_tip(shape, stamp_shape_name(static_cast<StampShape>(i)));
        }
    }
    if (id == "shapes-menu") {
        for (int i = 0; i < shape_count; ++i) {
            gf::Control::Ptr shape = (*attached_window()).find("popup-shape-" + std::to_string(i));
            (*tooltips_).set_tool_tip(shape, tr(shape_names[i]));
        }
    }
    gf::FocusScopeOptions focus_options;
    // The anchored layer deliberately passes clicks on its owner through.
    // A contained keyboard scope would reject that owner's pointer focus,
    // preventing the click from ever reaching its toggle action.
    focus_options.contain_focus = false;
    focus_scope_ = (*attached_window()).begin_focus_scope(popup_layer_, {}, focus_options);
    (*popup_owner_).set_drop_down_open(true);
}
void Ribbon::dismissed(gf::PopupDismissReason) {
    close_popup();
}
void Ribbon::close_popup() {
    if (popup_owner_ && (*popup_owner_).stable_id().value() == "shapes-menu" && attached_window()) {
        for (int i = 0; i < shape_count; ++i) {
            gf::Control::Ptr shape = (*attached_window()).find("popup-shape-" + std::to_string(i));
            if (shape) {
                static_cast<void>((*tooltips_).remove_tool_tip(*shape));
            }
        }
    }
    if (focus_scope_ && attached_window()) {
        static_cast<void>((*attached_window()).end_focus_scope(focus_scope_));
        focus_scope_ = {};
    }
    popup_.disconnect();
    if (popup_owner_) {
        (*popup_owner_).set_drop_down_open(false);
    }
    popup_owner_.reset();
    popup_layer_.reset();
    popup_subscriptions_.clear();
}
void Ribbon::popup_clicked(gf::ButtonBase& button) {
    std::string id(button.stable_id().value().substr(6));
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    close_popup();
    if (gradient_clicked(id)) {
        return;
    }
    if (id == "edit-custom-pattern") { (*editor).toggle_pattern_canvas(); return; }
    if (id.starts_with("peg-brush-")) {
        (*editor).spiro.assign_brush(static_cast<Brush>(std::stoi(id.substr(10))));
        (*editor).refresh();
        return;
    }
    if (id.starts_with("spiro-guide-") || id.starts_with("spiro-insert-") || id.starts_with("spiro-hole-")) {
        (*editor).spiro_choice(id);
        return;
    }
    if (id == "spirograph") {
        close_popup();
        (*editor).start_spirograph();
        return;
    }
    Document& document = (*editor).document;
    if (id.starts_with("stamp-shape-")) {
        (*editor).reset_stamp();
        document.stamp_shape = static_cast<StampShape>(std::stoi(id.substr(12)));
    } else if (id.starts_with("shape-")) {
        (*editor).choose_shape(static_cast<Shape>(std::stoi(id.substr(6))));
    } else if (id.starts_with("fill-brush-") || id.starts_with("edge-brush-")) {
        if (id.starts_with("fill-brush-")) {
            select_brush(document.alt_ink, static_cast<Brush>(std::stoi(id.substr(11))));
            document.shape_fill_brush = document.alt_ink.brush;
            document.shape_fill = true;
            (*editor).refresh();
        } else {
            select_brush(document.ink, static_cast<Brush>(std::stoi(id.substr(11))));
            document.shape_outline = true;
        }
    } else if (id.starts_with("brush-")) {
        if ((*editor).spiro.assign_brush(static_cast<Brush>(std::stoi(id.substr(6))))) {
            (*editor).refresh();
            return;
        }
        select_brush(document.ink, static_cast<Brush>(std::stoi(id.substr(6))));
        (*editor).choose_tool(Tool::Brush);
    } else if (id.starts_with("size-")) {
        document.ink.size = std::stoi(id.substr(5));
    } else if (id.starts_with("pattern-")) {
        select_pattern(document.ink, static_cast<Pattern>(std::stoi(id.substr(8))));
        document.shape_fill = true;
        (*editor).refresh();
    } else if (id == "outline-on" || id == "outline-off") {
        document.shape_outline = id == "outline-on";
        if (document.shape_outline) {
            select_pattern(document.ink, Pattern::Solid);
        }
    } else if (id == "fill-on" || id == "fill-off") {
        document.shape_fill = id == "fill-on";
    } else if (id == "guide-swap") {
        guide_swap_menu_ = true;
        const std::shared_ptr<gf::DropDownButton> owner =
            std::dynamic_pointer_cast<gf::DropDownButton>((*attached_window()).find("tool-11"));
        if (owner) {
            dropdown(*owner);
        }
        return;
    } else if (id == "guide-swap-bezier" || id == "guide-swap-arc") {
        (*editor).begin_guide_swap(id == "guide-swap-bezier" ? CurveKind::Bezier : CurveKind::Arc);
    } else if (id == "path-swap") {
        path_swap_menu_ = true;
        const std::shared_ptr<gf::DropDownButton> owner =
            std::dynamic_pointer_cast<gf::DropDownButton>((*attached_window()).find("tool-9"));
        if (owner) {
            dropdown(*owner);
        }
        return;
    } else if (id == "path-swap-bezier" || id == "path-swap-arc") {
        (*editor).begin_path_swap(id == "path-swap-bezier" ? CurveKind::Bezier : CurveKind::Arc);
    } else if (id == "custom-size") {
        (*editor).open_editor_dialog(EditorDialogKind::tool_size);
    } else if (id == "freehand-shape") {
        (*editor).choose_tool(Tool::Freehand);
        document.shape_fill = true;
        (*editor).refresh();
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
                                     : id == "family-mix"    ? BrushFamily::Mix
                                     : id == "family-dither" ? BrushFamily::Dither
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
} // namespace paint::forms

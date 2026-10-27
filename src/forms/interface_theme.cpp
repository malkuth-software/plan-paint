#include "forms/interface_theme.hpp"
#include "color_tools.hpp"
#include "forms/editor.hpp"
#include <cmath>
namespace paint::forms {
namespace gf = gui_forms;
gf::Color interface_color(gf::Color color, int hue) {
    if (hue == 220 || color.blue <= color.red || color.blue < color.green || color.green < color.red) {
        return color;
    }
    ColorCoordinates original =
        picker_coordinates(PickerSpace::OKHSL, {color.red, color.green, color.blue, 255});
    const Color target = picker_color(PickerSpace::RGB, {hue / 360.0, 0.65, 0.85});
    const Color base = picker_color(PickerSpace::RGB, {220.0 / 360.0, 0.65, 0.85});
    original.hue +=
        picker_coordinates(PickerSpace::OKHSL, target).hue - picker_coordinates(PickerSpace::OKHSL, base).hue;
    original.hue -= std::floor(original.hue);
    const Color tinted = picker_color(PickerSpace::OKHSL, original);
    return gf::Color::rgba(tinted.r, tinted.g, tinted.b, color.alpha);
}
gf::Color interface_color(const gf::Control& control, gf::Color color) {
    const EditorDialog* dialog = dynamic_cast<const EditorDialog*>(&control);
    if (dialog) {
        return interface_color(color, (*dialog).interface_hue());
    }
    const Editor* editor = dynamic_cast<const Editor*>(&control);
    std::shared_ptr<gf::Control> parent = control.parent();
    while (!editor && parent) {
        dialog = dynamic_cast<const EditorDialog*>(parent.get());
        if (dialog) {
            return interface_color(color, (*dialog).interface_hue());
        }
        editor = dynamic_cast<const Editor*>(parent.get());
        parent = (*parent).parent();
    }
    return interface_color(color,
                           editor ? ((*editor).preview_interface_hue >= 0 ? (*editor).preview_interface_hue
                                                                          : (*editor).settings.interface_hue)
                                  : 220);
}
namespace {
void tint_border(std::optional<gf::MaterialBorder>& border, int hue) {
    if (border) {
        (*border).color = interface_color((*border).color, hue);
    }
}
void tint_recipe(gf::ControlVisualRecipe& recipe, int hue) {
    recipe.text = interface_color(recipe.text, hue);
    recipe.muted_text = interface_color(recipe.muted_text, hue);
    recipe.glyph = interface_color(recipe.glyph, hue);
    recipe.focus_ring = interface_color(recipe.focus_ring, hue);
    recipe.default_ring = interface_color(recipe.default_ring, hue);
    gf::SurfaceMaterial& material = recipe.material;
    for (gf::MaterialFillLayer& fill : material.fills) {
        fill.color = interface_color(fill.color, hue);
        for (gf::GradientStop& stop : fill.stops) {
            stop.color = interface_color(stop.color, hue);
        }
    }
    for (gf::MaterialShadow& shadow : material.shadows) {
        shadow.color = interface_color(shadow.color, hue);
    }
    for (gf::MaterialKeyline& line : material.keylines) {
        line.color = interface_color(line.color, hue);
    }
    tint_border(material.border, hue);
    tint_border(material.border_edges.top, hue);
    tint_border(material.border_edges.bottom, hue);
    tint_border(material.border_edges.left, hue);
    tint_border(material.border_edges.right, hue);
}
std::shared_ptr<const gf::Theme> tint_theme(const std::shared_ptr<const gf::Theme>& source, int hue) {
    if (hue == 220) {
        return source;
    }
    gf::ThemeDefinition definition = (*source).definition();
    definition.id += "-hue-" + std::to_string(hue);
    gf::BasicControlStyle& c = definition.compatibility;
    gf::Color* colors[] = {&c.face, &c.face_light,    &c.paper,  &c.highlight,    &c.border, &c.dark_border,
                           &c.text, &c.disabled_text, &c.accent, &c.accent_light, &c.link,   &c.visited_link};
    for (gf::Color* color : colors) {
        *color = interface_color(*color, hue);
    }
    for (gf::ControlRoleRecipes& role : definition.roles) {
        for (gf::ControlVisualRecipe& recipe : role.ordinary) {
            tint_recipe(recipe, hue);
        }
        for (gf::ControlVisualRecipe& recipe : role.selected) {
            tint_recipe(recipe, hue);
        }
        // Accessibility high-contrast recipes retain their original contrast colors.
    }
    return gf::Theme::create(std::move(definition));
}
} // namespace
void InterfaceThemes::visit(gf::Control& control, int hue) {
    std::shared_ptr<const gf::Theme> current = control.theme_override();
    if (current) {
        Entry& entry = entries_[&control];
        if (entry.control.expired() || (current != entry.applied && current != entry.source)) {
            entry = {control.shared_from_this(), current, {}, -1};
        }
        if (entry.hue != hue) {
            std::shared_ptr<const gf::Theme>& cached = cache_[entry.source];
            if (!cached) {
                cached = tint_theme(entry.source, hue);
            }
            entry.applied = cached;
            entry.hue = hue;
            control.set_theme_override(entry.applied);
            control.invalidate(gf::Dirty::paint);
        }
    }
    for (const std::shared_ptr<gf::Control>& child : control.children()) {
        visit(*child, hue);
    }
}
void InterfaceThemes::apply(gf::Control& root, int hue) {
    if (cached_hue_ != hue) {
        cache_.clear();
        cached_hue_ = hue;
    }
    std::map<const gf::Control*, Entry>::iterator entry = entries_.begin();
    while (entry != entries_.end()) {
        if ((*entry).second.control.expired()) {
            entry = entries_.erase(entry);
        } else {
            ++entry;
        }
    }
    visit(root, hue);
}
} // namespace paint::forms

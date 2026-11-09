// The ribbon's stock galleries (RIBBON_STUDY.md section 5.2): shapes, the
// palette, patterns and brushes in the ribbon; line weights and stamp shapes
// as drop-down galleries; gradient presets on the gradient tab. Items are
// records with generated images; choosing one commits the gallery's value
// command, whose Command runs Ribbon::command_invoked.
#include "codecs.hpp"
#include "desktop.hpp"
#include "forms/editor.hpp"
#include "forms/ribbon.hpp"
#include "gradient.hpp"
#include "localization.hpp"
#include "material.hpp"
#include <algorithm>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
// The line weights the size gallery offers, in pixels.
const int weight_values[] = {1, 2, 3, 4, 5, 7, 8, 12, 16, 24, 32, 48, 64};
// A palette swatch: the colour over a light frame, or a white square with a
// red stroke for full transparency.
Image swatch_image(Color color) {
    Image image;
    image.reset(20, 20, {112, 132, 152, 255});
    for (int y = 1; y < 19; ++y) {
        for (int x = 1; x < 19; ++x) {
            image.set(x, y, color.a == 0 ? Color{255, 255, 255, 255} : color);
        }
    }
    if (color.a == 0) {
        for (int step = 3; step < 17; ++step) {
            image.set(step, 19 - step, {210, 35, 40, 255});
            image.set(step + 1, 19 - step, {210, 35, 40, 255});
        }
    }
    return image;
}
// A pattern sample, as the earlier pattern buttons painted it.
Image pattern_image(int pattern, const std::shared_ptr<Image>& custom) {
    Image image;
    image.reset(25, 24, {255, 255, 255, 255});
    Ink ink;
    ink.pattern = static_cast<Pattern>(pattern);
    ink.custom_pattern = custom;
    if (ink.pattern == Pattern::None) {
        for (int step = 2; step < 22; ++step) {
            image.set(step, 23 - step, {210, 35, 40, 255});
        }
        return image;
    }
    ink.primary = {40, 80, 120, 255};
    ink.secondary = {245, 248, 252, 255};
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            image.set(x, y, patterned(ink, x, y));
        }
    }
    return image;
}
// A line weight: a horizontal band of the weight's thickness, capped at the
// image's height.
Image weight_image(int weight) {
    Image image;
    image.reset(96, 20, {255, 255, 255, 0});
    const int thickness = std::clamp(weight, 1, 16);
    const int top = (image.height - thickness) / 2;
    for (int y = top; y < top + thickness; ++y) {
        for (int x = 4; x < image.width - 4; ++x) {
            image.set(x, y, {36, 60, 85, 255});
        }
    }
    return image;
}
// A brush preview: a stroke, a filled rectangle or a dynamic stroke.
Image brush_image(int brush, int kind) {
    Ink ink;
    ink.primary = {48, 83, 123, 255};
    ink.secondary = ink.primary;
    ink.brush = static_cast<Brush>(brush);
    ink.size = 11;
    Image sample;
    sample.reset(56, 40, {255, 255, 255, 255});
    if (kind == 2) {
        DynamicBrushStroke stroke_state;
        stroke_state.segment(sample, {5, 29}, {50, 10}, ink, false);
    } else if (kind == 1) {
        draw_shape(sample, Shape::Rectangle, {4, 4}, {51, 35}, ink, false, true, ink.brush);
    } else {
        stroke(sample, {5, 29}, {50, 10}, ink);
    }
    return sample;
}
} // namespace

Ribbon::GalleryEntry Ribbon::make_gallery(const std::string& id, const std::string& text, gf::RibbonGalleryForm form) {
    GalleryEntry entry;
    entry.value = std::make_shared<gf::GalleryCommand>(command(id, text));
    entry.gallery = gf::make_control<gf::RibbonGallery>(gf::StableId(id), form);
    (*entry.gallery).command = entry.value;
    (*entry.gallery).accessible_name = text;
    return entry;
}

// The galleries' shapes and column ranges; their items come with the images,
// once the ribbon is attached to a window.
void Ribbon::make_galleries() {
    const gf::RibbonGalleryForm in_ribbon = gf::RibbonGalleryForm::in_ribbon;
    const gf::RibbonGalleryForm drop_down = gf::RibbonGalleryForm::drop_down;
    shapes_ = make_gallery("shapes-gallery", tr("Shapes"), in_ribbon);
    (*shapes_.gallery).item_size = gf::Size{28, 28};
    (*shapes_.gallery).set_column_range(3, 7);
    (*shapes_.gallery).set_drop_down_size(7, 6);
    palette_ = make_gallery("palette-gallery", tr("Palette"), in_ribbon);
    (*palette_.gallery).item_size = gf::Size{22, 22};
    (*palette_.gallery).set_column_range(4, 10);
    (*palette_.gallery).set_drop_down_size(10, 9);
    patterns_ = make_gallery("patterns-gallery", tr("Patterns"), in_ribbon);
    (*patterns_.gallery).item_size = gf::Size{31, 26};
    (*patterns_.gallery).set_column_range(4, 9);
    (*patterns_.gallery).set_drop_down_size(9, 3);
    brushes_ = make_gallery("brushes-gallery", tr("Brushes"), in_ribbon);
    (*brushes_.gallery).item_size = gf::Size{64, 46};
    (*brushes_.gallery).show_labels = true;
    (*brushes_.gallery).visible_rows = std::size_t{2};
    (*brushes_.gallery).set_column_range(2, 4);
    (*brushes_.gallery).set_drop_down_size(4, 3);
    gradient_presets_ = make_gallery("gradient-presets-gallery", tr("Presets"), in_ribbon);
    (*gradient_presets_.gallery).item_size = gf::Size{124, 46};
    (*gradient_presets_.gallery).show_labels = true;
    (*gradient_presets_.gallery).visible_rows = std::size_t{2};
    (*gradient_presets_.gallery).set_column_range(2, 3);
    (*gradient_presets_.gallery).set_drop_down_size(3, 5);
    sizes_ = make_gallery("size-gallery", tr("Brush size"), drop_down);
    (*sizes_.gallery).item_size = gf::Size{150, 26};
    (*sizes_.gallery).show_labels = true;
    (*sizes_.gallery).set_drop_down_size(1, std::size(weight_values) + 1);
    stamp_shapes_ = make_gallery("stamp-shape-gallery", tr("Stamp shape"), drop_down);
    (*stamp_shapes_.gallery).item_size = gf::Size{34, 33};
    (*stamp_shapes_.gallery).set_drop_down_size(7, (stamp_shape_count + 6) / 7);
}

void Ribbon::fill_shapes() {
    std::vector<gf::RibbonGalleryItem> items;
    items.reserve(shape_count);
    for (int index = 0; index < shape_count; ++index) {
        items.push_back({"shape-" + std::to_string(index), tr(shape_names[index]), icon(100 + index, 24), "", true});
    }
    (*shapes_.gallery).set_items({}, std::move(items));
}

// Basic, custom and themed colours, as categories (the earlier strip's
// "palette-pane" cycled through them).
void Ribbon::fill_palette() {
    std::vector<gf::RibbonGalleryCategory> categories = {
        {"basic", tr("Basic")}, {"custom", tr("Custom")}, {"themed", tr("Themed")}};
    std::vector<gf::RibbonGalleryItem> items;
    items.reserve(90);
    custom_shown_.clear();
    for (int index = 0; index < 30; ++index) {
        const std::string id = "swatch-" + std::to_string(index);
        items.push_back({id, tr("Basic") + tr(" color ") + std::to_string(index + 1),
                         artwork(id, swatch_image(palette_color(id))), "basic", true});
    }
    for (int index = 0; index < 30; ++index) {
        const std::string id = "custom-" + std::to_string(index);
        const Color color = palette_color(id);
        custom_shown_.push_back(color);
        items.push_back({id, index == 29 ? tr("Transparency") : tr("Custom") + tr(" color ") + std::to_string(index + 1),
                         artwork(id, swatch_image(color)), "custom", true});
    }
    for (int index = 0; index < 30; ++index) {
        const std::string id = "themed-" + std::to_string(index);
        items.push_back({id, std::string(theme_name(index % 10)) + tr(" color ") + std::to_string(index / 10 + 1),
                         artwork(id, swatch_image(palette_color(id))), "themed", true});
    }
    (*palette_.gallery).set_items(std::move(categories), std::move(items));
}

void Ribbon::fill_patterns() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const std::shared_ptr<Image> custom = editor ? (*editor).custom_pattern : nullptr;
    std::vector<gf::RibbonGalleryItem> items;
    items.reserve(pattern_count);
    for (int index = 0; index < pattern_count; ++index) {
        const std::string id = "r-pattern-" + std::to_string(index);
        const std::string label = index == 0                                  ? tr("Solid")
                                  : index == static_cast<int>(Pattern::None) ? tr("No color")
                                                                              : tr(pattern_names[index]);
        items.push_back({id, label, artwork(id, pattern_image(index, custom)), "", true});
    }
    (*patterns_.gallery).set_items({}, std::move(items));
}

// The brush gallery shows strokes for the edge, filled rectangles for the
// Alt (fill) material, and dynamic strokes for the Brush tool.
void Ribbon::fill_brushes() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const std::string set = secondary_color_                                  ? "fill"
                            : editor && (*editor).document.tool == Tool::Brush ? "dynamic"
                                                                               : "edge";
    const int kind = set == "fill" ? 1 : set == "dynamic" ? 2 : 0;
    const std::string names[] = {tr("Brush"),  tr("Calligraphy 1"), tr("Calligraphy 2"), tr("Spray can"),  tr("Oil"),
                                 tr("Crayon"), tr("Marker"),        tr("Pencil"),        tr("Watercolor"), tr("Bristle"),
                                 tr("Pastel"), tr("Charcoal"),      tr("Gel pen")};
    static_assert(std::size(names) == brush_count);
    std::vector<gf::RibbonGalleryItem> items;
    items.reserve(brush_count);
    for (int index = 1; index < brush_count; ++index) {
        const std::string image = "brush-" + set + "-" + std::to_string(index);
        std::map<std::string, gf::ImageId>::iterator found = images_.find(image);
        const gf::ImageId id = found != images_.end() ? (*found).second : artwork(image, brush_image(index, kind));
        items.push_back({"material-brush-" + std::to_string(index), names[index], id, "", true});
    }
    (*brushes_.gallery).set_items({}, std::move(items));
    brush_images_ = set;
}

void Ribbon::fill_sizes() {
    std::vector<gf::RibbonGalleryItem> items;
    items.reserve(std::size(weight_values) + 1);
    for (int weight : weight_values) {
        const std::string id = "size-" + std::to_string(weight);
        items.push_back({id, std::to_string(weight) + tr(" px"), artwork(id, weight_image(weight)), "", true});
    }
    items.push_back({"custom-size", tr("Custom size…"), {}, "", true});
    (*sizes_.gallery).set_items({}, std::move(items));
}

void Ribbon::fill_stamp_shapes() {
    std::vector<gf::RibbonGalleryItem> items;
    items.reserve(stamp_shape_count);
    for (int index = 0; index < stamp_shape_count; ++index) {
        const StampShape shape = static_cast<StampShape>(index);
        items.push_back({"stamp-shape-" + std::to_string(index), stamp_shape_name(shape),
                         icon(100 + static_cast<int>(stamp_geometry_shape(shape)), 24), "", true});
    }
    (*stamp_shapes_.gallery).set_items({}, std::move(items));
}

void Ribbon::fill_gradient_presets() {
    const std::span<const GradientPreset> presets = gradient_presets();
    std::vector<gf::RibbonGalleryItem> items;
    items.reserve(presets.size());
    for (std::size_t index = 0; index < presets.size(); ++index) {
        Image sample;
        sample.reset(120, 30);
        const GradientSampler sampler(presets[index].gradient, {0, 0, 120, 30});
        for (int y = 0; y < sample.height; ++y) {
            for (int x = 0; x < sample.width; ++x) {
                sample.set(x, y, sampler.at(x / 119.0));
            }
        }
        const std::string id = "gradient-preset-" + std::to_string(index);
        items.push_back({id, presets[index].name, artwork(id, sample), "", true});
    }
    (*gradient_presets_.gallery).set_items({}, std::move(items));
}

Color Ribbon::palette_color(const std::string& item_id) const {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const std::size_t dash = item_id.rfind('-');
    const int index = std::clamp(std::stoi(item_id.substr(dash + 1)), 0, 29);
    if (item_id.starts_with("custom-")) {
        if (index == 29 || !editor) {
            return Color{255, 255, 255, 0};
        }
        return (*editor).custom_colors.colors.at(static_cast<std::size_t>(index));
    }
    if (item_id.starts_with("themed-")) {
        return themed_color(index);
    }
    return ribbon_color(index);
}

// Gallery selections follow the editor; the brush gallery changes its
// preview set, and custom colours their images, when they change.
void Ribbon::synchronize_galleries() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !attached_window()) {
        return;
    }
    const Document& document = (*editor).document;
    const std::string brush_set = secondary_color_ ? "fill" : document.tool == Tool::Brush ? "dynamic" : "edge";
    if (brush_set != brush_images_) {
        fill_brushes();
    }
    for (std::size_t index = 0; index < custom_shown_.size() && index < 29; ++index) {
        const std::string id = "custom-" + std::to_string(index);
        const Color color = palette_color(id);
        if (!equal(color, custom_shown_[index])) {
            custom_shown_[index] = color;
            static_cast<void>(artwork(id, swatch_image(color)));
        }
    }
    const Ink& choice = (*editor).spiro.selected_peg >= 0 ? (*editor).spiro.pegs[(*editor).spiro.selected_peg].effect
                        : secondary_color_                ? document.alt_ink
                                                          : document.ink;
    std::optional<std::string> shape;
    if (document.tool == Tool::Shape) {
        shape = "shape-" + std::to_string(static_cast<int>(document.shape));
    }
    std::optional<std::string> pattern;
    if (choice.brush == Brush::Round) {
        pattern = "r-pattern-" + std::to_string(static_cast<int>(choice.pattern));
    }
    std::optional<std::string> brush;
    if (choice.pattern == Pattern::Solid && choice.brush != Brush::Round) {
        brush = "material-brush-" + std::to_string(static_cast<int>(choice.brush));
    }
    std::optional<std::string> size;
    if (std::find(std::begin(weight_values), std::end(weight_values), document.ink.size) != std::end(weight_values)) {
        size = "size-" + std::to_string(document.ink.size);
    }
    const std::optional<std::string> stamp = "stamp-shape-" + std::to_string(static_cast<int>(document.stamp_shape));
    const std::pair<GalleryEntry*, std::optional<std::string>> values[] = {
        {&shapes_, shape}, {&patterns_, pattern}, {&brushes_, brush}, {&sizes_, size}, {&stamp_shapes_, stamp}};
    for (const std::pair<GalleryEntry*, std::optional<std::string>>& each : values) {
        if ((*(*each.first).value).value() != each.second) {
            (*(*each.first).value).set_value(each.second);
        }
    }
}

// Opens a drop-down gallery below the command that asked, or closes it.
bool Ribbon::open_drop_down_gallery(const std::string& command_id, GalleryEntry& entry) {
    if ((*entry.gallery).overlay_open()) {
        (*entry.gallery).close_overlay();
        return false;
    }
    close_popup();
    if (!move_anchor(command_id)) {
        return false;
    }
    return (*entry.gallery).open_overlay(anchor_);
}

void Ribbon::gallery_chosen(const std::string& gallery_id) {
    const GalleryEntry* const entries[] = {&shapes_, &palette_, &patterns_, &brushes_,
                                           &sizes_,  &stamp_shapes_, &gradient_presets_};
    for (const GalleryEntry* entry : entries) {
        if ((*(*entry).value).stable_id() == gallery_id) {
            const std::optional<std::string> item = (*(*entry).value).value();
            if (item) {
                apply_gallery_item(*item);
            }
            return;
        }
    }
}

// A gallery item was chosen (or right-clicked, for Alt): the earlier strip's
// gallery buttons.
void Ribbon::apply_gallery_item(const std::string& item_id) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    Document& document = (*editor).document;
    if (item_id.starts_with("gradient-preset-")) {
        static_cast<void>(gradient_clicked(item_id));
        return;
    }
    if (item_id.starts_with("shape-")) {
        (*editor).choose_shape(static_cast<Shape>(std::stoi(item_id.substr(6))));
        return;
    }
    if (item_id == "custom-size") {
        (*editor).open_editor_dialog(EditorDialogKind::tool_size);
        return;
    }
    if (item_id.starts_with("size-")) {
        document.ink.size = std::stoi(item_id.substr(5));
    } else if (item_id.starts_with("stamp-shape-")) {
        (*editor).reset_stamp();
        document.stamp_shape = static_cast<StampShape>(std::stoi(item_id.substr(12)));
    } else if (item_id.starts_with("swatch-") || item_id.starts_with("custom-") || item_id.starts_with("themed-")) {
        Color& color = secondary_color_ ? document.ink.secondary : document.ink.primary;
        color = palette_color(item_id);
        Ink& material = secondary_color_ ? document.alt_ink : document.ink;
        if (material.pattern == Pattern::None) {
            select_pattern(material, Pattern::Solid);
        }
    } else if (item_id.starts_with("r-pattern-")) {
        const Pattern pattern = static_cast<Pattern>(std::stoi(item_id.substr(10)));
        if ((*editor).spiro.selected_peg >= 0) {
            select_pattern((*editor).spiro.pegs[(*editor).spiro.selected_peg].effect, pattern);
            (*editor).spiro.refresh_peg((*editor).spiro.selected_peg);
        } else {
            Ink& material = secondary_color_ ? document.alt_ink : document.ink;
            select_pattern(material, pattern);
            if (secondary_color_) {
                document.shape_fill_brush = Brush::Round;
            }
        }
    } else if (item_id.starts_with("material-brush-")) {
        const Brush selected = static_cast<Brush>(std::stoi(item_id.substr(15)));
        if ((*editor).spiro.assign_brush(selected)) {
            (*editor).refresh();
            return;
        }
        (*editor).brush_family = BrushFamily::Additive;
        Ink& material = secondary_color_ ? document.alt_ink : document.ink;
        select_brush(material, selected);
        if (secondary_color_) {
            document.shape_fill_brush = selected;
        }
    }
    document.sync_curve();
    document.sync_path();
    (*editor).refresh();
}

// The palette, pattern or brush item under a window point, or "".
std::string Ribbon::gallery_item_at(gf::Point position) const {
    const GalleryEntry* const entries[] = {&palette_, &patterns_, &brushes_};
    for (const GalleryEntry* entry : entries) {
        const gf::RibbonGallery& gallery = *(*entry).gallery;
        if (!gallery.visible.read()) {
            continue;
        }
        const gf::Rect origin = gallery.absolute_bounds();
        if (!origin.contains(position)) {
            continue;
        }
        for (const gf::RibbonGallery::Item& item : gallery.realized_items()) {
            const gf::Rect face = item.parts.item_face.bounds;
            const gf::Rect absolute{origin.x + face.x, origin.y + face.y, face.width, face.height};
            if (item.parts.item_face.present && !item.states.disabled && absolute.contains(position)) {
                return item.stable_id;
            }
        }
    }
    return "";
}
} // namespace paint::forms

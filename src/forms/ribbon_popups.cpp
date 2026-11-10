// Plan Paint's own pickers, shown in an overlay anchored to the ribbon
// command that opened them (owner decision for the ribbon port, option b):
// the spirograph's guide, insert, hole and peg-medium catalogs, whose parts
// paint themselves, and the gradient's stop strip.
#include "codecs.hpp"
#include "forms/editor.hpp"
#include "forms/ribbon.hpp"
#include "forms/spirograph_view.hpp"
#include "localization.hpp"
#include "material.hpp"
#include <gui_forms/delegate.hpp>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
// A spirograph guide or insert, painted above its name.
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

std::shared_ptr<gf::Button> Ribbon::add_popup_button(gf::Panel& panel, const std::string& id,
                                                     const std::string& text, gf::Rect bounds, bool selected) {
    std::shared_ptr<gf::Button> item;
    if (id.starts_with("spiro-guide-") || id.starts_with("spiro-insert-")) {
        const bool guide = id.starts_with("spiro-guide-");
        item = gf::make_control<SpiroPartButton>(gf::StableId("popup-" + id), text, guide ? 0 : 1,
                                                 std::stoi(id.substr(guide ? 12 : 13)));
        (*item).content_padding = {2, 40, 2, 2};
        (*item).font = gf::FontSpec{gf::FontRole::control, 11, 400, false};
        (*item).text_alignment = gf::ContentAlignment::bottom_center;
    } else {
        item = gf::make_control<gf::Button>(gf::StableId("popup-" + id), text);
        (*item).font = gf::FontSpec{gf::FontRole::control, 14, 400, false, 0.05};
        (*item).text_image_relation = gf::TextImageRelation::image_before_text;
        (*item).text_alignment = gf::ContentAlignment::middle_left;
        (*item).image_alignment = gf::ContentAlignment::middle_left;
        (*item).content_padding = {8, 4, 8, 4};
    }
    (*item).set_requested_bounds(bounds);
    (*item).selected = selected;
    popup_subscriptions_.push_back((*item).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Ribbon, &Ribbon::popup_clicked>(*this)));
    panel.add_child(item);
    return item;
}

// Opens one of Plan Paint's pickers below the command `id`.
void Ribbon::open_popup(const std::string& id) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    close_popup();
    if (!editor || !move_anchor(id)) {
        return;
    }
    std::shared_ptr<gf::Panel> panel = gf::make_control<gf::Panel>(gf::StableId("ribbon-popup-content"));
    gf::SurfaceMaterial material;
    material.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(250, 252, 255))};
    material.border = gf::MaterialBorder{gf::Color::rgba(141, 164, 190), 1};
    material.shadows.push_back({{0, 3}, 7, 0, gf::Color::rgba(30, 50, 75, 60), false});
    (*panel).set_authored_surface_material(material);
    double width = 232, height = 10;
    Spirograph& spiro = (*editor).spiro;
    if (id == "gradient-stops-menu") {
        const gf::Size size = gradient_stop_panel(*panel);
        width = size.width;
        height = size.height;
    } else if (id == "spiro-guides-menu" || id == "spiro-inserts-menu") {
        const bool guides = id == "spiro-guides-menu";
        const int count = static_cast<int>(guides ? spiro_guides().size() : spiro_inserts().size());
        width = 568;
        height = 8 + 76 * ((count + 4) / 5);
        for (int index = 0; index < count; ++index) {
            const bool selected = guides ? spiro.guide == index : spiro.insert == index;
            const std::shared_ptr<gf::Button> part = add_popup_button(
                *panel, std::string(guides ? "spiro-guide-" : "spiro-insert-") + std::to_string(index),
                guides ? tr(spiro_guides()[index].name) : tr(spiro_inserts()[index].name),
                {4.0 + (index % 5) * 112, 4.0 + (index / 5) * 76, 110, 74}, selected);
            (*part).enabled = guides ? spiro.compatible(index, spiro.insert) : spiro.compatible(spiro.guide, index);
        }
    } else if (id == "spiro-holes-menu") {
        const int columns = std::max(1, (spiro.hole_count() + 11) / 12);
        const int rows = std::max(1, (spiro.hole_count() + columns - 1) / columns);
        width = 280 * columns + 8;
        height = 8 + 30 * rows;
        for (int index = 0; index < spiro.hole_count(); ++index) {
            const std::string label =
                std::to_string(index + 1) + " · " + (spiro.pegs[index].seated ? tr("Select peg") : tr("Place fine peg"));
            add_popup_button(*panel, "spiro-hole-" + std::to_string(index), label,
                             {4.0 + 280 * (index / rows), 4.0 + 30 * (index % rows), 278, 29},
                             spiro.selected_peg == index);
        }
    } else if (id == "spiro-brushes-menu") {
        if (!popup_images_) {
            popup_images_ = std::make_shared<gf::ImageList>(*attached_window(), gf::Size{28, 20});
            for (int index = 0; index < brush_count; ++index) {
                Ink ink;
                ink.primary = {48, 83, 123, 255};
                ink.secondary = ink.primary;
                ink.brush = static_cast<Brush>(index);
                ink.size = 11;
                Image sample;
                sample.reset(56, 40, {255, 255, 255, 255});
                DynamicBrushStroke stroke_state;
                stroke_state.segment(sample, {5, 29}, {50, 10}, ink, false);
                const std::vector<std::uint8_t> png = encode_png(sample);
                static_cast<void>((*popup_images_).add_png(std::to_string(index), std::as_bytes(std::span(png)), 2));
            }
        }
        width = 436;
        height = 8 + 32 * ((brush_count + 1) / 2);
        for (int index = 0; index < brush_count; ++index) {
            const bool selected =
                spiro.selected_peg >= 0 && static_cast<int>(spiro.pegs[spiro.selected_peg].effect.brush) == index;
            const std::shared_ptr<gf::Button> part =
                add_popup_button(*panel, "peg-brush-" + std::to_string(index), tr(brush_names[index]),
                                 {4.0 + (index % 2) * 214, 4.0 + (index / 2) * 32, 212, 31}, selected);
            (*part).set_image_list(popup_images_);
            (*part).set_image_key(std::to_string(index));
        }
    } else {
        return;
    }
    gf::AnchoredOverlayPlacement placement;
    placement.preferred_size = {width, height};
    placement.gap = 2;
    popup_layer_ = gf::make_control<gf::AnchoredOverlayLayer>(gf::StableId("ribbon-popup"), anchor_, placement);
    (*popup_layer_).set_content(panel);
    popup_subscriptions_.push_back((*popup_layer_).dismiss_requested().subscribe(
        *this, gf::Delegate<gf::OverlayDismissReason>::bind<Ribbon, &Ribbon::dismissed>(*this)));
    popup_ = (*attached_window()).open_overlay(anchor_, popup_layer_);
    popup_command_ = id;
    gf::FocusScopeOptions focus_options;
    // The anchored layer passes clicks on its owner through; a contained
    // keyboard scope would reject the ribbon's pointer focus, so the click
    // that closes the picker could not reach its command.
    focus_options.contain_focus = false;
    focus_scope_ = (*attached_window()).begin_focus_scope(popup_layer_, {}, focus_options);
}

void Ribbon::dismissed(gf::OverlayDismissReason) {
    close_popup();
}

// Closes Plan Paint's picker and the drop-down galleries (the ribbon closes
// its own menus).
void Ribbon::close_popup() {
    if (focus_scope_ && attached_window()) {
        static_cast<void>((*attached_window()).end_focus_scope(focus_scope_));
        focus_scope_ = {};
    }
    popup_.disconnect();
    popup_layer_.reset();
    popup_subscriptions_.clear();
    popup_command_.clear();
    for (GalleryEntry* entry : {&sizes_, &stamp_shapes_}) {
        if ((*entry).gallery && (*(*entry).gallery).overlay_open()) {
            (*(*entry).gallery).close_overlay();
        }
    }
}

void Ribbon::popup_clicked(gf::ButtonBase& button) {
    const std::string id(button.stable_id().value().substr(6));
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    close_popup();
    if (id.starts_with("peg-brush-")) {
        (*editor).spiro.assign_brush(static_cast<Brush>(std::stoi(id.substr(10))));
        (*editor).refresh();
        return;
    }
    if (id.starts_with("spiro-guide-") || id.starts_with("spiro-insert-") || id.starts_with("spiro-hole-")) {
        (*editor).spiro_choice(id);
    }
}
} // namespace paint::forms

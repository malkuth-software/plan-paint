#include "forms/editor.hpp"
#include "localization.hpp"
namespace paint::forms {
namespace gf = gui_forms;
void Ribbon::initialize_sticky_ribbon() {
    sticky_paper_ = gf::make_control<gf::ComboBox>(gf::StableId("sticky-paper"));
    (*sticky_paper_)
        .set_items({tr("Book paper"), tr("Copy paper"), tr("Recycled paper"), tr("Wove paper"),
                    tr("Laid paper"), tr("Bible paper"), tr("Parchment"), tr("Cotton rag"), tr("Kraft paper"),
                    tr("Night paper")});
    (*sticky_paper_).set_selected_index(0);
    (*sticky_paper_).set_accessible_name(tr("Paper texture"));
    option(sticky_paper_, {467, 35, 216, 28});
    subscriptions_.push_back(
        (*sticky_paper_)
            .selected_index_changed()
            .subscribe(*this,
                       gf::Delegate<std::optional<std::size_t>>::bind<Ribbon, &Ribbon::sticky_paper_changed>(
                           *this)));
    sticky_padding_ = number("sticky-padding", tr("Padding"), {467, 66, 216, 24}, 0, 128, 16);
    const std::shared_ptr<gf::Label> label =
        gf::make_control<gf::Label>(gf::StableId("sticky-color-label"), tr("Paper color"));
    option(label, {467, 96, 102, 24});
    sticky_color_ = gf::make_control<gf::TextBox>(gf::StableId("sticky-color"), "#FFF2A6");
    (*sticky_color_).set_accessible_name(tr("Paper color"));
    (*sticky_color_).set_maximum_length(7);
    option(sticky_color_, {569, 93, 114, 27});
    subscriptions_.push_back(
        (*sticky_color_)
            .text_changed()
            .subscribe(*this,
                       gf::Delegate<const std::string&>::bind<Ribbon, &Ribbon::sticky_color_changed>(*this)));
    sticky_width_ = number("sticky-width", tr("Width (px)"), {696, 35, 218, 24}, 24, 4096, 256);
    sticky_height_ = number("sticky-height", tr("Height (px)"), {696, 64, 218, 24}, 24, 4096, 192);
    button("sticky-new-paper", tr("New paper"), -1, {696, 93, 218, 24});
}
bool Ribbon::sticky_visibility(const std::string& id, bool visible) const {
    const std::shared_ptr<Editor> editor = editor_.lock();
    const bool sticky = editor && (*editor).text.active && (*editor).text.note.has_value();
    if (id.starts_with("sticky-")) {
        return visible && sticky;
    }
    if (sticky &&
        (id == "text-opaque" || id == "text-contour" || id.starts_with("text-outline-") ||
         id == "text-word-art" || id.starts_with("text-skew") || id.starts_with("text-perspective") ||
         id.starts_with("text-warp") || id == "text-reset-effects" || id == "text-secondary")) {
        return false;
    }
    return visible;
}
void Ribbon::synchronize_sticky_ribbon() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !(*editor).text.note) {
        return;
    }
    const TextSession& text = (*editor).text;
    (*sticky_paper_).set_selected_index(static_cast<std::size_t>((*text.note).paper));
    (*sticky_padding_).set_value((*text.note).padding);
    (*sticky_width_).set_value(text.bounds.w);
    (*sticky_height_).set_value(text.bounds.h);
    // Keep partial hex input intact until it is a complete color or focus moves.
    if (!attached_window() || (*attached_window()).focused_control() != sticky_color_) {
        (*sticky_color_).set_text(to_hex((*text.note).paper_color));
    }
}
void Ribbon::sticky_paper_changed(std::optional<std::size_t> index) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!synchronizing_ && editor && (*editor).text.note && index && *index < 10) {
        (*(*editor).text.note).paper = static_cast<int>(*index);
        (*editor).refresh();
    }
}
void Ribbon::sticky_color_changed(const std::string& value) {
    const std::shared_ptr<Editor> editor = editor_.lock();
    Color color;
    if (!synchronizing_ && editor && (*editor).text.note && from_hex(value, color)) {
        (*(*editor).text.note).paper_color = color;
        (*editor).refresh();
    }
}
} // namespace paint::forms

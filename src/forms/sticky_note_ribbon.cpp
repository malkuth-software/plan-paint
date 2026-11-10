#include "forms/editor.hpp"
#include "localization.hpp"
namespace paint::forms {
namespace gf = gui_forms;
// A sticky note's groups on the text tab, in place of the letter treatment
// and transform groups.
void Ribbon::sticky_groups(gf::RibbonTabSpec& tab) {
    gf::RibbonGroupLayout paired;
    paired.rows = 2;
    gf::RibbonGroupSpec paper;
    paper.stable_id = "sticky-paper-group";
    paper.caption = tr("Paper texture");
    paper.layouts = {paired};
    gf::RibbonCommandSpec papers = hosted("sticky-paper", tr("Paper texture"), gf::RibbonCommandKind::combo_box, 180);
    papers.choices = {tr("Book paper"), tr("Copy paper"), tr("Recycled paper"), tr("Wove paper"),
                      tr("Laid paper"), tr("Bible paper"), tr("Parchment"),     tr("Cotton rag"),
                      tr("Kraft paper"), tr("Night paper")};
    paper.commands.push_back(std::move(papers));
    number(paper, "sticky-padding", tr("Padding"));
    paper.commands.push_back(button("sticky-color-label", tr("Paper color"), -1, gf::RibbonCommandSize::small));
    paper.commands.push_back(hosted("sticky-color", tr("Paper color"), gf::RibbonCommandKind::text_box, 96));
    paper.commands.back().show_label = false;
    tab.groups.push_back(std::move(paper));
    gf::RibbonGroupSpec size;
    size.stable_id = "sticky-size";
    size.caption = tr("Size");
    size.layouts = {paired};
    number(size, "sticky-width", tr("Width (px)"));
    number(size, "sticky-height", tr("Height (px)"));
    size.commands.push_back(button("sticky-new-paper", tr("New paper"), -1, gf::RibbonCommandSize::small));
    tab.groups.push_back(std::move(size));
}
void Ribbon::synchronize_sticky_ribbon() {
    const std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor || !(*editor).text.note) {
        return;
    }
    const TextSession& text = (*editor).text;
    if (sticky_paper_) {
        (*sticky_paper_).selected_index = std::optional<std::size_t>{static_cast<std::size_t>((*text.note).paper)};
    }
    if (sticky_padding_) {
        (*sticky_padding_).value = static_cast<double>((*text.note).padding);
    }
    if (sticky_width_ && sticky_height_) {
        (*sticky_width_).value = static_cast<double>(text.bounds.w);
        (*sticky_height_).value = static_cast<double>(text.bounds.h);
    }
    // Keep partial hex input intact until it is a complete color or focus moves.
    if (sticky_color_ && (!attached_window() || (*attached_window()).focused_control() != sticky_color_)) {
        (*sticky_color_).text = to_hex((*text.note).paper_color);
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

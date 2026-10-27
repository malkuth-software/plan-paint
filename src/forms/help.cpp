#include "localization.hpp"
#include "forms/help.hpp"
#include "help_content.hpp"
#include <algorithm>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
class HelpHeader final : public gf::Button {
  public:
    HelpHeader(gf::StableId id, std::string text, bool expanded)
        : Button(std::move(id), "  " + text), expanded_(expanded) {}
    void set_expanded(bool expanded) {
        expanded_ = expanded;
        invalidate(gf::Dirty::paint);
    }
    void on_paint(gf::Painter& painter, gf::Rect damage) override {
        Button::on_paint(painter, damage);
        double y = client_rectangle().height * 0.5;
        for (int row = 0; row < 7; ++row) {
            if (expanded_) {
                painter.draw_line({5.0 + row * 0.5, y - 3 + row}, {12.0 - row * 0.5, y - 3 + row},
                                  gf::Color::rgba(0, 0, 0), 1);
            } else {
                double width = 3 - std::abs(row - 3);
                painter.draw_line({6, y - 3 + row}, {6 + width, y - 3 + row}, gf::Color::rgba(0, 0, 0), 1);
            }
        }
    }

  private:
    bool expanded_;
};
} // namespace
HelpBook::HelpBook(gf::StableId id) : ScrollableControl(std::move(id)) {
    set_auto_scroll(true);
    gf::ThemeDefinition theme = gf::windows_professional_theme_definition();
    theme.id = "rainstar-help-book";
    for (std::size_t i = 0; i < gf::control_surface_state_count; ++i) {
        gf::ControlVisualRecipe& recipe =
            theme.roles[static_cast<std::size_t>(gf::ControlVisualRole::button)].ordinary[i];
        recipe.material.fills = {gf::MaterialFillLayer::solid(
            i == static_cast<std::size_t>(gf::ControlSurfaceState::hot) ? gf::Color::rgba(230, 225, 150)
                                                                        : gf::Color::rgba(238, 234, 168))};
        recipe.material.border.reset();
        recipe.material.corner_radius = 0;
        recipe.text = gf::Color::rgba(0, 0, 0);
    }
    set_theme_override(gf::Theme::create(std::move(theme)));
}
void HelpBook::initialize_control_tree() {
    close_ = gf::make_control<gf::Button>(gf::StableId("help-close"), "×");
    (*close_).set_font({gf::FontRole::control, 24, 400, false});
    (*close_).set_accessible_name(tr("Close Paint Help"));
    add_child(close_);
    title_ = gf::make_control<gf::Label>(gf::StableId("help-title"), tr("Plan Paint Help"));
    introduction_ = gf::make_control<gf::Label>(gf::StableId("help-welcome"), tr(help_welcome));
    for (const std::shared_ptr<gf::Label>& label : {title_, introduction_}) {
        (*label).set_font({gf::FontRole::control, 18, 400, false});
        (*label).set_foreground(gf::Color::rgba(0, 0, 0));
        (*label).set_text_wrapping(gf::TextWrapping::word);
        if (current_language().right_to_left) { (*label).set_alignment(gf::HorizontalAlignment::far); }
        add_child(label);
    }
    for (std::size_t i = 0; i < std::size(help_topics); ++i) {
        const HelpTopic& topic = help_topics[i];
        std::shared_ptr<gf::Button> header = gf::make_control<HelpHeader>(
            gf::StableId("help-topic-" + std::to_string(i)), tr(topic.title), topic.open);
        (*header).set_font({gf::FontRole::control, 18, 400, false});
        (*header).set_text_alignment(gf::ContentAlignment::middle_left);
        (*header).set_content_padding({4, 1, 4, 1});
        (*header).set_accessible_name(tr(topic.title));
        subscriptions_.push_back((*header).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<HelpBook, &HelpBook::toggle>(*this)));
        std::shared_ptr<gf::Label> body = gf::make_control<gf::Label>(
            gf::StableId("help-body-" + std::to_string(i)), tr(topic.body));
        (*body).set_font({gf::FontRole::control, 18, 400, false});
        (*body).set_foreground(gf::Color::rgba(0, 0, 0));
        (*body).set_text_wrapping(gf::TextWrapping::word);
        if (current_language().right_to_left) { (*body).set_alignment(gf::HorizontalAlignment::far); }
        (*body).set_visible(topic.open);
        headers_.push_back(header);
        bodies_.push_back(body);
        add_child(header);
        add_child(body);
    }
}
gf::Event<gf::ButtonBase&>& HelpBook::close_clicked() {
    return (*close_).clicked();
}
void HelpBook::toggle(gf::ButtonBase& button) {
    std::size_t index = std::stoul(std::string(button.stable_id().value().substr(11)));
    bool open = !(*bodies_[index]).visible();
    (*bodies_[index]).set_visible(open);
    (*std::static_pointer_cast<HelpHeader>(headers_[index])).set_expanded(open);
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void HelpBook::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    double width = std::max(1.0, bounds.width - 40);
    double y = 52;
    set_child_layout(close_, {10, 9, 30, 30});
    set_child_layout(title_, {48, 12, width - 36, 28});
    std::vector<gf::Rect> rectangles;
    std::vector<std::shared_ptr<gf::Control>> controls{introduction_};
    for (std::size_t i = 0; i < headers_.size(); ++i) {
        controls.push_back(headers_[i]);
        if ((*bodies_[i]).visible()) {
            controls.push_back(bodies_[i]);
        }
    }
    for (const std::shared_ptr<gf::Control>& control : controls) {
        double height = std::max(24.0, (*control).measure({width, 100000}).height);
        rectangles.push_back({12, y, width, height});
        y += height + 7;
    }
    arrange_scroll_viewport({bounds.width, bounds.height}, {width + 24, y + 5});
    gf::Point offset = scroll_position();
    for (std::size_t i = 0; i < controls.size(); ++i) {
        gf::Rect rectangle = rectangles[i];
        rectangle.y -= offset.y;
        set_child_layout(controls[i], rectangle);
    }
}
void HelpBook::on_paint(gf::Painter& painter, gf::Rect) {
    gf::Rect bounds = client_rectangle();
    painter.fill_rect(bounds, gf::Color::rgba(255, 255, 211));
    painter.stroke_rect(bounds, gf::Color::rgba(160, 157, 125), 1);
    painter.draw_line({12, 44}, {bounds.width - 25, 44},
                      gf::Color::rgba(190, 185, 146), 1);
}
} // namespace paint::forms

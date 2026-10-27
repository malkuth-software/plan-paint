#pragma once
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/controls/scrollable_control/scrollable_control.hpp>
namespace paint::forms {
class HelpBook final : public gui_forms::ScrollableControl {
  public:
    explicit HelpBook(gui_forms::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gui_forms::Rect bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    gui_forms::Event<gui_forms::ButtonBase&>& close_clicked();

  private:
    std::shared_ptr<gui_forms::Label> title_, introduction_;
    std::shared_ptr<gui_forms::Button> close_;
    std::vector<std::shared_ptr<gui_forms::Button>> headers_;
    std::vector<std::shared_ptr<gui_forms::Label>> bodies_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_;
    void toggle(gui_forms::ButtonBase& button);
};
} // namespace paint::forms

#pragma once
#include <gui_forms/components/business_card/business_card.hpp>
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/controls/scrollable_control.hpp>
namespace paint::forms {
class HelpBook final : public gui_forms::ScrollableControl {
  public:
    explicit HelpBook(gui_forms::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gui_forms::Rect bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    gui_forms::Event<gui_forms::ButtonBase&>& close_clicked();
    // Malkuthsoft's card at the foot of the topics: GUI.Forms' business card
    // on the page, printing the process identity (identity.hpp). A click
    // opens it in the hand over the window; there its website goes to the
    // website opener. Holding D while right-clicking its vendor name toggles
    // GUI.Forms' diagnostic overlay. The book owns it.
    [[nodiscard]] gui_forms::BusinessCard& card() { return *card_; }

  private:
    std::shared_ptr<gui_forms::Label> title_, introduction_;
    std::shared_ptr<gui_forms::Button> close_;
    std::shared_ptr<gui_forms::BusinessCard> card_;
    // The card in the hand whose website request is subscribed (the window's
    // own card, kept while put away; observed, never owned).
    std::weak_ptr<gui_forms::BusinessCard> hand_card_;
    gui_forms::SubscriptionToken card_open_{};
    gui_forms::SubscriptionToken hand_website_{};
    std::vector<std::shared_ptr<gui_forms::Button>> headers_;
    std::vector<std::shared_ptr<gui_forms::Label>> bodies_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_;
    void toggle(gui_forms::ButtonBase& button);
    // The page card asked to open: shows the window's card in the hand and
    // routes its website request to the opener.
    void card_open_requested(gui_forms::BusinessCardRequest& request);
    void website_requested(gui_forms::BusinessCardRequest& request);
    void on_dispose() noexcept override;
};
} // namespace paint::forms

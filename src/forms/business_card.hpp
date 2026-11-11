#pragma once
#include <gui_forms/basic_controls.hpp>
#include <string>
#include <string_view>
namespace paint::forms {
// Malkuthsoft's business card, lying at the foot of the help book like
// PlaySuite's beside its dedication: cream stock with a soft shadow, the name
// in large letters and the website beneath. The name turns gold under the
// pointer; a click on the card (or Enter while it has focus) opens the website.
//
// A hidden switch, the only way to turn it on: holding D while right-clicking
// the name turns on GUI.Forms' invalidation overlay for every window until
// Plan Paint closes, and the whole name is printed red while it is on. The
// switch is the process's (invalidation_overlay_default), so a card made later
// shows red too; nothing turns it off.
class BusinessCard final : public gui_forms::Button {
  public:
    static constexpr std::string_view name = "Malkuthsoft";
    static constexpr std::string_view website = "https://malkuthsoft.com";
    // The card's width over its height (a business card's 85 by 55 mm).
    static constexpr double aspect = 85.0 / 55.0;
    // Room around the card for its shadow, in points (right and below).
    static constexpr double shadow_room = 5.0;

    explicit BusinessCard(gui_forms::StableId id);
    // Whether D is held now. The editor keeps it from its key preview (key
    // events go to the focused control, which is seldom the card).
    bool d_held = false;
    // Where a click sends the website: open_link, or a test's recorder.
    void (*opener)(const std::string& address);

    // The band of the name's line, in this control's points: right-clicks and
    // hover there count as on the name.
    [[nodiscard]] gui_forms::Rect name_band() const;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    void on_pointer(gui_forms::PointerEvent& event) override;

  private:
    bool over_name_ = false;
    gui_forms::SubscriptionToken click_;
    void card_clicked(gui_forms::ButtonBase& button);
};
} // namespace paint::forms

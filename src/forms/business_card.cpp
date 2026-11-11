#include "forms/business_card.hpp"
#include "forms/open_link.hpp"
#include "localization.hpp"
#include <gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp>
#include <algorithm>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
// The card itself inside the control: the control's bounds less the shadow's room.
gf::Rect card_rectangle(gf::Rect client) {
    const double width = std::max(1.0, client.width - BusinessCard::shadow_room);
    const double height = std::max(1.0, client.height - BusinessCard::shadow_room);
    const gf::Rect result{0, 0, width, height};
    return result;
}
// The name's type size and baseline follow the card's width so the card
// prints the same at any size the book gives it.
double name_size(gf::Rect card) {
    const double size = std::max(10.0, card.width * 0.105);
    return size;
}
double name_baseline(gf::Rect card) {
    const double baseline = card.y + card.height * 0.48;
    return baseline;
}
} // namespace
BusinessCard::BusinessCard(gf::StableId id) : Button(std::move(id), ""), opener(&open_link) {
    accessible_name = tr("Malkuthsoft business card; opens malkuthsoft.com");
    click_ = clicked().subscribe(*this, gf::Delegate<gf::ButtonBase&>::bind<BusinessCard, &BusinessCard::card_clicked>(*this));
}
gf::Rect BusinessCard::name_band() const {
    const gf::Rect card = card_rectangle(client_rectangle());
    const double size = name_size(card);
    const gf::Rect band{card.x + 8, name_baseline(card) - size, std::max(1.0, card.width - 16), size * 1.3};
    return band;
}
void BusinessCard::card_clicked(gf::ButtonBase&) {
    if (opener != nullptr) {
        opener(std::string(website));
    }
}
void BusinessCard::on_pointer(gf::PointerEvent& event) {
    // Pointer events carry window coordinates; the band is in this control's.
    const gf::Rect band = name_band();
    const gf::Point local = point_from_window(event.position);
    const bool over = event.action != gf::PointerAction::leave && band.contains(local);
    if (over != over_name_) {
        over_name_ = over;
        invalidate(gf::Dirty::paint);
    }
    if (event.button == gf::PointerButton::secondary) {
        // The hidden switch. A right-click anywhere else on the card does nothing.
        if (event.action == gf::PointerAction::down && d_held && over) {
            gf::set_invalidation_overlay_default(true);
            invalidate(gf::Dirty::paint);
        }
        event.handled = true;
        return;
    }
    Button::on_pointer(event);
}
void BusinessCard::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect card = card_rectangle(client_rectangle());
    // A soft shadow cast down and to the right: three widening layers.
    for (int layer = 3; layer >= 1; --layer) {
        const double reach = layer * 1.5;
        const gf::Rect shade{card.x + reach, card.y + reach, card.width, card.height};
        painter.fill_rect(shade, gf::Color::rgba(60, 52, 30, static_cast<std::uint8_t>(18 + (3 - layer) * 10)));
    }
    painter.fill_rect(card, gf::Color::rgba(252, 250, 243));
    painter.stroke_rect(card, gf::Color::rgba(196, 189, 166), 1);

    const double size = name_size(card);
    const gf::FontSpec name_font{gf::FontRole::control, size, 600, false};
    gf::Color name_colour = gf::Color::rgba(33, 38, 50);
    if (over_name_) {
        name_colour = gf::Color::rgba(176, 130, 34);
    }
    if (gf::invalidation_overlay_default()) {
        name_colour = gf::Color::rgba(200, 28, 34);
    }
    const gf::Size name_extent = painter.measure_text_utf8(name, name_font);
    const double baseline = name_baseline(card);
    painter.draw_text_utf8({card.x + (card.width - name_extent.width) * 0.5, baseline}, name, name_font, name_colour);

    const double rule_y = baseline + size * 0.45;
    painter.draw_line({card.x + card.width * 0.3, rule_y}, {card.x + card.width * 0.7, rule_y},
                      gf::Color::rgba(190, 178, 140), 1);

    const gf::FontSpec site_font{gf::FontRole::control, std::max(9.0, card.width * 0.048), 400, false};
    const std::string_view site = "malkuthsoft.com";
    const gf::Size site_extent = painter.measure_text_utf8(site, site_font);
    const double site_x = card.x + (card.width - site_extent.width) * 0.5;
    const double site_baseline = card.y + card.height * 0.74;
    const gf::Color link = gf::Color::rgba(38, 84, 160);
    painter.draw_text_utf8({site_x, site_baseline}, site, site_font, link);
    if (hovered_visual()) {
        painter.draw_line({site_x, site_baseline + 2}, {site_x + site_extent.width, site_baseline + 2}, link, 1);
    }
    if (focus_cue_visible()) {
        const gf::Rect cue{card.x + 3, card.y + 3, card.width - 6, card.height - 6};
        painter.stroke_rect(cue, gf::Color::rgba(38, 84, 160, 150), 1);
    }
}
} // namespace paint::forms

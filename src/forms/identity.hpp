#pragma once
#include <gui_forms/components/business_card/business_card.hpp>
#include <string>
namespace paint::forms {
// Plan Paint's identity as GUI.Forms' business card prints it (one card for
// every GUI.Forms program, cp34): Malkuthsoft's name and website, the product
// and its release version, the copyright and the credits of the About box.
[[nodiscard]] gui_forms::ProgramIdentity plan_paint_identity();
// Sets plan_paint_identity as the process's (gui_forms::set_program_identity).
// main calls it before the first window; tests call it before showing help.
// UI thread.
void install_program_identity();
// Where a card's website request goes: open_link, or a test's recorder.
// Plan Paint's card asks only for its own fixed address.
using WebsiteOpener = void (*)(const std::string& address);
void set_website_opener(WebsiteOpener opener) noexcept;
[[nodiscard]] WebsiteOpener website_opener() noexcept;
} // namespace paint::forms

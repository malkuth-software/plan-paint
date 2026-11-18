#include "forms/identity.hpp"
#include "forms/open_link.hpp"
namespace paint::forms {
namespace gf = gui_forms;
namespace {
// The opener a card's website request uses; replaced by tests. UI thread.
WebsiteOpener current_opener = &open_link;
} // namespace
gf::ProgramIdentity plan_paint_identity() {
    gf::ProgramIdentity identity{};
    identity.product = "Plan Paint";
    identity.version = RAINSTAR_VERSION;
    identity.vendor = "Malkuthsoft";
    identity.website = "https://malkuthsoft.com";
    // The About box's first licence line and its credits (forms/about.form).
    identity.copyright = "Copyright (c) 2026 joshuah.rainstar@gmail.com";
    identity.credits = "Author: Astra. Sponsor: Rainstar.";
    return identity;
}
void install_program_identity() {
    gf::set_program_identity(plan_paint_identity());
}
void set_website_opener(WebsiteOpener opener) noexcept {
    current_opener = opener;
}
WebsiteOpener website_opener() noexcept {
    return current_opener;
}
} // namespace paint::forms

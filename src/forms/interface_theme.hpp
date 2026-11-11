#pragma once
#include <gui_forms/control.hpp>
#include <memory>
namespace paint::forms {
// Authored chrome only: icons, artwork, material samples and gold selections stay intact.
gui_forms::Color interface_color(gui_forms::Color color, int hue);
gui_forms::Color interface_color(const gui_forms::Control& control, gui_forms::Color color);
// The interface hue is one override on a root (the editor, or its dialog,
// which opens as an overlay outside the editor's subtree): the attached
// window's theme, which carries Plan Paint's theme package, tinted to the
// hue. At the authored royal blue (220) the root has no override and shows
// the window's theme unchanged.
class InterfaceThemes {
  public:
    // A detached root is left unchanged; apply again once it is attached.
    void apply(gui_forms::Control& root, int hue);

  private:
    // The last tint, shared by every root while the window theme and hue hold.
    std::shared_ptr<const gui_forms::Theme> source_;
    std::shared_ptr<const gui_forms::Theme> tinted_;
    int tinted_hue_ = -1;
};
} // namespace paint::forms

#pragma once
#include <gui_forms/control.hpp>
#include <map>
namespace paint::forms {
// Authored chrome only: icons, artwork, material samples and gold selections stay intact.
gui_forms::Color interface_color(gui_forms::Color color, int hue);
gui_forms::Color interface_color(const gui_forms::Control& control, gui_forms::Color color);
class InterfaceThemes {
  public:
    void apply(gui_forms::Control& root, int hue);

  private:
    struct Entry {
        std::weak_ptr<gui_forms::Control> control;
        std::shared_ptr<const gui_forms::Theme> source, applied;
        int hue = -1;
    };
    std::map<const gui_forms::Control*, Entry> entries_;
    std::map<std::shared_ptr<const gui_forms::Theme>, std::shared_ptr<const gui_forms::Theme>> cache_;
    int cached_hue_ = -1;
    void visit(gui_forms::Control& control, int hue);
};
} // namespace paint::forms

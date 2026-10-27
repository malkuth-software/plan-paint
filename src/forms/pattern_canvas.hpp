#pragma once
#include "image.hpp"
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/controls/panel/numeric_up_down/numeric_up_down.hpp>
namespace paint::forms {
class Editor;
class PatternCanvas final : public gui_forms::Control {
  public:
    PatternCanvas(gui_forms::StableId id, std::weak_ptr<Editor> editor, std::shared_ptr<Image> tile);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gui_forms::Rect bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    void on_pointer(gui_forms::PointerEvent& event) override;
    void on_key_preview(gui_forms::KeyEvent& event) override;
    void resize_tile(int width, int height);
    void undo(bool redo = false);
    gui_forms::Rect tile_bounds() const;

  private:
    std::weak_ptr<Editor> editor_;
    std::shared_ptr<Image> tile_;
    std::shared_ptr<gui_forms::NumericUpDown> width_, height_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_;
    std::vector<Image> undo_, redo_;
    bool drawing_ = false, black_ = true;
    Point last_;
    Point keyboard_cell_{};
    double cell_ = 32;
    gui_forms::Point origin_;
    void clicked(gui_forms::ButtonBase& button);
    void checkpoint();
    void changed();
    void pencil(Point point);
};
} // namespace paint::forms

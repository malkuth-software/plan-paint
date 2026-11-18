#pragma once
#include "document.hpp"
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/controls/flow_layout_panel_compat.hpp>
namespace paint::forms {
std::string icon_format_name(IconFormat format);
class Editor;
class AtlasThumbnail final : public gui_forms::Button {
  public:
    AtlasThumbnail(gui_forms::StableId id, std::weak_ptr<Editor> editor, int index);
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    void arrange(gui_forms::Rect bounds) override;
    void synchronize();

  protected:
    // A secondary press marks the reference frame and ends the route without
    // the button's handling; any other press records whether Control or
    // Command (the sequence modifier) is held, then the button handles it.
    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_up(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_move(gui_forms::MouseEventArgs& arguments) override;
    void on_key_down(gui_forms::KeyEventArgs& arguments) override;
    void on_key_up(gui_forms::KeyEventArgs& arguments) override;

  private:
    // True when the secondary button's event was taken here (route ended).
    bool take_secondary(gui_forms::MouseEventArgs& arguments);
    std::weak_ptr<Editor> editor_;
    int index_;
    bool sequence_ = false;
    std::uint64_t epoch_ = 0;
    gui_forms::ImageId image_;
    gui_forms::SubscriptionToken click_;
    void update_image();
    void clicked(gui_forms::ButtonBase& control);
    void on_detaching_from_window(gui_forms::SurfaceRoot& window) noexcept override;
};
class AtlasPanel final : public gui_forms::Control {
  public:
    AtlasPanel(gui_forms::StableId id, std::weak_ptr<Editor> editor, bool gallery = false);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gui_forms::Rect bounds) override;
    void synchronize();
    void reveal_current();

  private:
    std::weak_ptr<Editor> editor_;
    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> strip_;
    std::vector<std::shared_ptr<AtlasThumbnail>> thumbnails_;
    std::vector<std::shared_ptr<gui_forms::Button>> buttons_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_;
    std::shared_ptr<gui_forms::Label> status_;
    std::shared_ptr<gui_forms::CheckBox> wrap_, alpha_;
    std::shared_ptr<gui_forms::Button> clear_reference_;
    bool gallery_ = false;
    AtlasKind kind_ = AtlasKind::None;
    int count_ = -1, active_ = -2;
    void clicked(gui_forms::ButtonBase& control);
};
} // namespace paint::forms

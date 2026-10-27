#pragma once
#include "forms/atlas.hpp"

namespace paint::forms {
class IconPanel final : public gui_forms::Control {
  public:
    IconPanel(gui_forms::StableId id, std::weak_ptr<Editor> editor);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gui_forms::Rect bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    void synchronize();

  private:
    std::weak_ptr<Editor> editor_;
    std::shared_ptr<gui_forms::FlowLayoutPanel> strip_;
    std::shared_ptr<gui_forms::Label> status_;
    std::vector<std::shared_ptr<gui_forms::Label>> groups_;
    std::vector<std::shared_ptr<gui_forms::Button>> commands_;
    std::vector<std::shared_ptr<AtlasThumbnail>> thumbnails_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_;
    AtlasKind kind_ = AtlasKind::None;
    int count_ = -1, active_ = -1;
    void clicked(gui_forms::ButtonBase& control);
};
} // namespace paint::forms

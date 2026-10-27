#pragma once
#include "document.hpp"
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/components/tool_tip/tool_tip.hpp>
#include <gui_forms/controls/button_base/button/drop_down_button/drop_down_button.hpp>
#include <gui_forms/controls/panel/anchored_popup_layer/anchored_popup_layer.hpp>
#include <gui_forms/controls/panel/combo_box/combo_box.hpp>
#include <gui_forms/controls/panel/numeric_up_down/numeric_up_down.hpp>
#include <gui_forms/window.hpp>
namespace paint::forms {
class Editor;
class AtlasPanel;
class IconPanel;
Color ribbon_color(int index);
std::shared_ptr<const gui_forms::Theme> ribbon_theme();
std::shared_ptr<const gui_forms::Theme> gallery_theme();
struct OpacityIndicator {
    int percent = 100;
    std::string text;
    Color foreground, outline;
};
OpacityIndicator opacity_indicator(Color color);
class SwatchButton final : public gui_forms::Button {
  public:
    SwatchButton(gui_forms::StableId id, std::string text, Color color);
    void set_color(Color color);
    void set_material(const Ink& ink);
    void set_show_opacity(bool show);
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;

  private:
    Color color_;
    bool show_opacity_ = false;
    void update_opacity_description();
    std::shared_ptr<gui_forms::ImageList> material_preview_;
    std::vector<Color> material_pixels_;
    std::optional<Ink> material_ink_;
};
class Ribbon final : public gui_forms::Control {
  public:
    Ribbon(gui_forms::StableId id, std::weak_ptr<Editor> editor);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gui_forms::Rect bounds) override;
    void on_pointer_preview(gui_forms::PointerEvent& event) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    void synchronize();
    void close_popup();
    void show_tool_context();
    void cancel_spiro_drag();
    void show_transforms();
    void show_atlas();
    double ribbon_height() const;

  protected:
    void on_attached_to_window() override;

  private:
    std::weak_ptr<Editor> editor_;
    std::shared_ptr<AtlasPanel> atlas_;
    std::shared_ptr<IconPanel> icon_;
    std::shared_ptr<gui_forms::ImageList> tiny_icons_, small_icons_, medium_icons_, large_icons_,
        brush_previews_, dynamic_previews_, pattern_previews_, fill_previews_;
    std::shared_ptr<gui_forms::ToolTip> tooltips_;
    std::vector<std::shared_ptr<gui_forms::Button>> buttons_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_, popup_subscriptions_;
    std::shared_ptr<gui_forms::AnchoredPopupLayer> popup_layer_;
    gui_forms::PopupToken popup_;
    gui_forms::FocusScopeId focus_scope_;
    std::shared_ptr<gui_forms::DropDownButton> popup_owner_;
    std::shared_ptr<SwatchButton> primary_, secondary_;
    bool secondary_color_ = false, path_swap_menu_ = false, guide_swap_menu_ = false;
    std::shared_ptr<gui_forms::Button> secondary_target_;
    void show_materials(bool fill);
    void prepare_material_previews();
    int page_ = 1, building_page_ = 1, palette_page_ = 0, ordered_tab_page_ = -1;
    Color palette_color(int index) const;
    int spiro_drag_width_ = 0;
    void spiro_button(const std::string& id, const std::string& text, int kind, int index,
                      gui_forms::Rect bounds);
    bool synchronizing_ = false, fonts_loaded_ = false, collapsed_ = false;
    void ensure_fonts();
    std::vector<int> button_pages_;
    std::vector<std::shared_ptr<gui_forms::Control>> option_controls_;
    std::vector<int> option_pages_;
    std::vector<std::shared_ptr<gui_forms::CheckBox>> checks_;
    std::shared_ptr<gui_forms::NumericUpDown> grain_, tooth_, load_, angle_, tool_size_, text_size_;
    std::shared_ptr<gui_forms::NumericUpDown> corner_radius_, geometry_organic_;
    std::shared_ptr<gui_forms::ComboBox> geometry_stroke_;
    void geometry_stroke_changed(std::optional<std::size_t> index);
    std::shared_ptr<gui_forms::ComboBox> geometry_start_, geometry_end_;
    void geometry_markers_changed(std::optional<std::size_t> index);
    std::shared_ptr<gui_forms::NumericUpDown> stamp_width_, stamp_height_, stamp_scale_, stamp_angle_,
        rotation_, mesh_spacing_;
    std::shared_ptr<gui_forms::ComboBox> font_, word_art_, picker_mode_, mix_effect_, eraser_mode_, render_quality_;
    std::shared_ptr<gui_forms::NumericUpDown> effect_strength_, effect_scale_, effect_phase_, heal_hardness_,
        heal_correction_, stabilizer_lag_, stamp_hardness_;
    void mix_effect_changed(std::optional<std::size_t> index);
    void word_art_changed(std::optional<std::size_t> index);
    std::shared_ptr<gui_forms::NumericUpDown> text_skew_, text_perspective_, text_warp_, text_outline_;
    void render_quality_changed(std::optional<std::size_t> index);
    void picker_mode_changed(std::optional<std::size_t> index);
    std::vector<std::string> font_paths_;
    void font_changed(std::optional<std::size_t> index);
    void add_options();
    void initialize_sticky_ribbon();
    void synchronize_sticky_ribbon();
    bool sticky_visibility(const std::string& id, bool visible) const;
    void sticky_paper_changed(std::optional<std::size_t> index);
    void sticky_color_changed(const std::string& value);
    std::shared_ptr<gui_forms::ComboBox> sticky_paper_;
    std::shared_ptr<gui_forms::TextBox> sticky_color_;
    std::shared_ptr<gui_forms::NumericUpDown> sticky_padding_, sticky_width_, sticky_height_;
    void initialize_gradient();
    void synchronize_gradient();
    bool gradient_clicked(const std::string& id);
    void gradient_number_changed(double value);
    void gradient_stop_changed(std::optional<std::size_t> index);
    gui_forms::Size gradient_gallery(gui_forms::Panel& panel);
    std::shared_ptr<gui_forms::Control> gradient_strip_;
    std::shared_ptr<gui_forms::NumericUpDown> gradient_angle_, gradient_position_;
    std::shared_ptr<gui_forms::ComboBox> gradient_stops_;
    std::shared_ptr<gui_forms::ImageList> gradient_previews_;
    std::shared_ptr<SwatchButton> gradient_color_;
    bool gradient_synchronizing_ = false;

    void options_changed(double value);
    void spiro_scale_changed(double value);
    std::shared_ptr<gui_forms::NumericUpDown> spiro_scale_;
    void apply_choice(const std::string& id);
    void show_page();
    void check(const std::string& id, const std::string& text, gui_forms::Rect bounds);
    std::shared_ptr<gui_forms::NumericUpDown> number(const std::string& id, const std::string& text,
                                                     gui_forms::Rect bounds, double minimum, double maximum,
                                                     double value, int decimals = 0);
    void option(std::shared_ptr<gui_forms::Control> control, gui_forms::Rect bounds);

    void clicked(gui_forms::ButtonBase& button);
    void dropdown(gui_forms::DropDownButton& button);
    void dismissed(gui_forms::PopupDismissReason reason);
    void popup_clicked(gui_forms::ButtonBase& button);
    void bind_icon(gui_forms::Button& button, int icon, bool large);
    std::shared_ptr<gui_forms::Button> button(const std::string& id, const std::string& text, int icon,
                                              gui_forms::Rect bounds, bool tall = false,
                                              bool dropdown = false, bool split = false);
    std::shared_ptr<gui_forms::Button> add_popup_button(gui_forms::Panel& panel, const std::string& id,
                                                        const std::string& text, int icon,
                                                        gui_forms::Rect bounds, bool selected = false);
};
} // namespace paint::forms

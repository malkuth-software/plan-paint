#pragma once
#include "document.hpp"
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/commands/gallery_command/gallery_command.hpp>
#include <gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp>
#include <gui_forms/controls/panel/combo_box/combo_box.hpp>
#include <gui_forms/controls/panel/numeric_up_down/numeric_up_down.hpp>
#include <gui_forms/controls/panel/text_box/text_box.hpp>
#include <gui_forms/controls/quick_access_toolbar/quick_access_toolbar.hpp>
#include <gui_forms/controls/ribbon/ribbon.hpp>
#include <gui_forms/controls/ribbon_gallery/ribbon_gallery.hpp>
#include <gui_forms/window.hpp>
#include <map>
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
// A font file's display name in the font list.
std::string font_label(const std::string& path);
// A colour or material swatch button, used by the colour dialogs.
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

// Plan Paint's ribbon: the canonical gui_forms::Ribbon (tabs, groups and
// commands as records bound to Commands; stock galleries for shapes, the
// palette, patterns, brushes, line weights, stamp shapes and gradient
// presets), the Quick Access Toolbar above it, the help buttons at the right
// of its tab row, and Plan Paint's own panels: the atlas and icon panels over
// the command plane of their tabs, and the spirograph and gradient-stop
// pickers in overlays anchored to the command that opened them.
//
// Commands are named by stable IDs that the editor and the tests share
// ("tool-2", "shape-3", "grain-scale"). A hosted control's window ID is
// "ribbon-bar.<command ID>". home-tab, view-tab, patterns-tab, tool-tab and
// atlas-tab are core tabs; text-tab, spiro-tab, gradient-tab and icon-tab
// are contextual tab sets, shown while their tool or document kind is in
// use (the tool tab hides then, by application mode). The tool tab is
// rebuilt for the current tool: its groups are those the tool needs, and
// set_tabs keeps the hosted controls of the commands that stay, by ID.
class Ribbon final : public gui_forms::Control {
  public:
    Ribbon(gui_forms::StableId id, std::weak_ptr<Editor> editor);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gui_forms::Rect bounds) override;
    void on_pointer_preview(gui_forms::PointerEvent& event) override;
    // Reads the editor's state into the ribbon: tab structure, command
    // states, gallery selections and hosted values.
    void synchronize();
    void close_popup();
    void show_tool_context();
    void cancel_spiro_drag();
    void show_transforms();
    void show_atlas();
    // The Quick Access Toolbar's row plus the ribbon's measured height.
    double ribbon_height() const;
    // The File tab: the application menu (command and separator items) and
    // the recent pictures (item IDs "recent-<n>", run as editor commands).
    void set_file_menu(std::vector<gui_forms::MenuItemSpec> items, std::vector<gui_forms::RibbonRecentItem> recent);
    bool file_menu_open() const;
    // Inspection: the Command behind the ribbon command `id` (its text,
    // description, enabled and checked state), or null for an unknown ID.
    std::shared_ptr<gui_forms::Command> command_for(const std::string& id) const;

  protected:
    void on_attached_to_window() override;

  private:
    // Application modes: the tool tab shows in the ordinary mode; a
    // contextual tool (text, spirograph, gradient fill) switches to the
    // context mode, which hides the tool tab while its contextual tab shows.
    static constexpr std::uint32_t ordinary_mode = 1U;
    static constexpr std::uint32_t context_mode = 2U;
    // The Quick Access Toolbar's row above the ribbon, in logical units.
    static constexpr double quick_access_height = 26;
    // A ribbon command's Command and its invoked subscription; one per ID.
    struct CommandEntry {
        std::shared_ptr<gui_forms::Command> command;
        gui_forms::SubscriptionToken invoked;
    };
    // A stock gallery and the value command its selection follows.
    struct GalleryEntry {
        std::shared_ptr<gui_forms::RibbonGallery> gallery;
        std::shared_ptr<gui_forms::GalleryCommand> value;
    };
    // A hosted control and its change subscription, by command ID; replaced
    // when set_tabs makes a new control for the ID.
    struct HostedEntry {
        std::shared_ptr<gui_forms::Control> control;
        gui_forms::SubscriptionToken changed;
    };

    std::weak_ptr<Editor> editor_;
    std::shared_ptr<gui_forms::Ribbon> bar_;
    std::shared_ptr<gui_forms::QuickAccessToolbar> quick_access_;
    std::shared_ptr<gui_forms::Button> help_, help_english_;
    std::shared_ptr<AtlasPanel> atlas_;
    std::shared_ptr<IconPanel> icon_;
    // Where an overlay anchors: moved over the command that opened it.
    std::shared_ptr<gui_forms::Control> anchor_;
    std::map<std::string, CommandEntry> commands_;
    std::map<std::string, std::shared_ptr<gui_forms::Command>> menu_commands_;
    std::map<std::string, HostedEntry> hosted_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_, popup_subscriptions_;
    GalleryEntry shapes_, palette_, patterns_, brushes_, sizes_, stamp_shapes_, gradient_presets_;
    // Loaded images, by "<size>/<icon>" for ribbon icons and by name for
    // generated artwork, which is replaced in place (same ImageId).
    std::map<std::string, gui_forms::ImageId> images_;
    std::shared_ptr<gui_forms::ImageList> popup_images_;
    // Plan Paint's own picker in an overlay, and the command that opened it.
    std::shared_ptr<gui_forms::AnchoredOverlayLayer> popup_layer_;
    gui_forms::OverlayToken popup_;
    gui_forms::FocusScopeId focus_scope_;
    std::string popup_command_;
    gui_forms::Rect anchor_rect_{};
    // The structure last given to set_tabs (tab, group and command IDs and
    // tab texts); set_tabs runs only when it changes.
    std::string tabs_signature_;
    // Which preview set the brush gallery shows ("edge", "fill", "dynamic").
    std::string brush_images_;
    // The materials and custom colours the generated images show now.
    std::optional<Ink> primary_shown_, secondary_shown_;
    std::vector<Color> custom_shown_;
    bool secondary_color_ = false, synchronizing_ = false, gradient_synchronizing_ = false;
    bool fonts_loaded_ = false;
    int spiro_drag_width_ = 0;
    std::string secondary_target_;
    std::vector<std::string> font_paths_;

    // Hosted controls, null while their command is not on a tab.
    std::shared_ptr<gui_forms::NumericUpDown> grain_, tooth_, load_, angle_, tool_size_, text_size_;
    std::shared_ptr<gui_forms::NumericUpDown> corner_radius_, geometry_organic_;
    std::shared_ptr<gui_forms::NumericUpDown> stamp_width_, stamp_height_, stamp_scale_, stamp_angle_, rotation_,
        mesh_spacing_;
    std::shared_ptr<gui_forms::NumericUpDown> effect_strength_, effect_scale_, effect_phase_, heal_hardness_,
        heal_correction_, stabilizer_lag_, stamp_hardness_;
    std::shared_ptr<gui_forms::NumericUpDown> text_skew_, text_perspective_, text_warp_, text_outline_;
    std::shared_ptr<gui_forms::NumericUpDown> spiro_scale_;
    std::shared_ptr<gui_forms::NumericUpDown> sticky_padding_, sticky_width_, sticky_height_;
    std::shared_ptr<gui_forms::NumericUpDown> gradient_angle_, gradient_position_;
    std::shared_ptr<gui_forms::ComboBox> geometry_stroke_, geometry_start_, geometry_end_;
    std::shared_ptr<gui_forms::ComboBox> font_, word_art_, picker_mode_, mix_effect_, render_quality_;
    std::shared_ptr<gui_forms::ComboBox> sticky_paper_, gradient_stops_;
    std::shared_ptr<gui_forms::TextBox> sticky_color_;

    // -- structure (ribbon_tabs.cpp) ------------------------------------------
    std::vector<gui_forms::RibbonTabSpec> build_tabs();
    gui_forms::RibbonTabSpec home_tab();
    gui_forms::RibbonTabSpec view_tab();
    gui_forms::RibbonTabSpec materials_tab();
    gui_forms::RibbonTabSpec tool_tab();
    gui_forms::RibbonTabSpec text_tab();
    gui_forms::RibbonTabSpec spiro_tab();
    gui_forms::RibbonTabSpec gradient_tab();
    void apply_tabs();
    void bind_hosted();
    std::shared_ptr<gui_forms::Command> command(const std::string& id, const std::string& text);
    gui_forms::MenuItemSpec menu_item(const std::string& id, const std::string& text);
    gui_forms::RibbonCommandSpec button(const std::string& id, const std::string& text, int icon,
                                        gui_forms::RibbonCommandSize size,
                                        gui_forms::RibbonCommandKind kind = gui_forms::RibbonCommandKind::button);
    gui_forms::RibbonCommandSpec hosted(const std::string& id, const std::string& text,
                                        gui_forms::RibbonCommandKind kind, double width = 0);
    void number(gui_forms::RibbonGroupSpec& group, const std::string& id, const std::string& text, double width = 0);
    gui_forms::RibbonCommandSpec gallery_command(const std::string& id, const GalleryEntry& entry);
    gui_forms::ImageId icon(int index, int size);
    gui_forms::ImageId artwork(const std::string& name, const Image& image);

    // -- galleries (ribbon_galleries.cpp) -----------------------------------------
    GalleryEntry make_gallery(const std::string& id, const std::string& text, gui_forms::RibbonGalleryForm form);
    void make_galleries();
    void fill_shapes();
    void fill_palette();
    void fill_patterns();
    void fill_brushes();
    void fill_sizes();
    void fill_stamp_shapes();
    void fill_gradient_presets();
    void synchronize_galleries();
    bool open_drop_down_gallery(const std::string& command_id, GalleryEntry& entry);
    void gallery_chosen(const std::string& gallery_id);
    void apply_gallery_item(const std::string& item_id);
    Color palette_color(const std::string& item_id) const;

    // -- behaviour (ribbon.cpp) -----------------------------------------------
    void command_invoked(const gui_forms::CommandInvocation& invocation);
    void drop_down_chosen(const gui_forms::RibbonDropDownInvocation& invocation);
    void recent_chosen(const gui_forms::RibbonRecentItemInvocation& invocation);
    void help_clicked(gui_forms::ButtonBase& button);
    void activate(const std::string& id);
    void apply_choice(const std::string& id);
    void menu_choice(const std::string& id);
    void select_tab(const std::string& tab_id);
    std::string selected_tab_id() const;
    void update_panels();
    void tab_changed(const gui_forms::RibbonTabChange& change);
    void minimized_changed(bool minimized);
    void show_materials(bool fill);
    void synchronize_commands();
    void synchronize_tab_sets();
    void synchronize_values();
    void update_color_images();
    void set_checked(const std::string& id, bool checked);
    void set_enabled(const std::string& id, bool enabled);
    void set_text(const std::string& id, const std::string& text);
    std::optional<gui_forms::Rect> command_bounds(const std::string& id) const;
    bool move_anchor(const std::string& id);
    std::string command_at(gui_forms::Point position, const std::string& prefix) const;
    std::string gallery_item_at(gui_forms::Point position) const;
    void ensure_fonts();
    void options_changed(double value);
    void spiro_scale_changed(double value);
    void choice_changed(std::optional<std::size_t> index);
    void font_changed(std::optional<std::size_t> index);
    void check_clicked(gui_forms::ButtonBase& check);

    // -- Plan Paint's own pickers (ribbon_popups.cpp) ---------------------------
    void open_popup(const std::string& id);
    void dismissed(gui_forms::OverlayDismissReason reason);
    void popup_clicked(gui_forms::ButtonBase& button);
    std::shared_ptr<gui_forms::Button> add_popup_button(gui_forms::Panel& panel, const std::string& id,
                                                        const std::string& text, gui_forms::Rect bounds,
                                                        bool selected = false);

    // -- sticky notes (sticky_note_ribbon.cpp) --------------------------------
    void sticky_groups(gui_forms::RibbonTabSpec& tab);
    void synchronize_sticky_ribbon();
    void sticky_paper_changed(std::optional<std::size_t> index);
    void sticky_color_changed(const std::string& value);

    // -- gradients (gradient_controls.cpp) --------------------------------------
    gui_forms::Size gradient_stop_panel(gui_forms::Panel& panel);
    void synchronize_gradient();
    bool gradient_clicked(const std::string& id);
    void gradient_number_changed(double value);
    void gradient_stop_changed(std::optional<std::size_t> index);
};
} // namespace paint::forms

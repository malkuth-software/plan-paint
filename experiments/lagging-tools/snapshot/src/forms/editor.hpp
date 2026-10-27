#pragma once
#include "color_tools.hpp"
#include "desktop.hpp"
#include "document.hpp"
#include "forms/dialog.hpp"
#include "forms/help.hpp"
#include "forms/ribbon.hpp"
#include "material.hpp"
#include "paint_tools.hpp"
#include "text_session.hpp"
#include "warp_session.hpp"
#include <chrono>
#include <gui_forms/application.hpp>
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/canvas.hpp>
#include <gui_forms/controls/menu_strip/menu_strip.hpp>
#include <gui_forms/controls/panel/combo_box/combo_box.hpp>
#include <gui_forms/controls/range_control/track_bar/track_bar.hpp>
#include <gui_forms/delegate.hpp>
#include <gui_forms/host.hpp>
namespace paint::forms {
class Editor;
class PaintCanvas final : public gui_forms::RasterCanvas {
  public:
    PaintCanvas(gui_forms::StableId id, std::weak_ptr<Editor> editor);
    void on_pointer(gui_forms::PointerEvent& event) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;

  protected:
    void on_attached_to_window() override;
    void on_detaching_from_window(gui_forms::Window& former_window) noexcept override;
    void on_dispose() noexcept override;

  public:
    void on_paint_overlay(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    void on_text_input(gui_forms::TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    void on_frame(gui_forms::FrameTime now) override;
    gui_forms::SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(gui_forms::SemanticAction action, std::string_view value) override;

  private:
    std::weak_ptr<Editor> editor_;
    gui_forms::ImageId backing_, repeated_, reference_;
    CanvasBacking loaded_backing_ = CanvasBacking::Count;
    void update_backing();
    std::uint64_t atlas_revision_ = 0;
    void paint_atlas_context(gui_forms::Painter& painter, const Editor& editor);
};
class Editor final : public gui_forms::Control {
  public:
    explicit Editor(gui_forms::StableId id);
    ~Editor() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void poll_warp();
    void start_reshape();
    void request_rotation(double degrees);
    void request_skew(int width, int height, bool scale, double horizontal, double vertical);
    void finish_warp(bool place);
    void cancel_warp();
    bool warp_active() const;
    bool background_busy() const;
    void reset_stamp();
    void add_stamp_material();
    void update_stamp_hardness();
    double stamp_hardness = 1;
    BrushFamily brush_family = BrushFamily::Additive;
    MixEffect mix_effect = MixEffect::Ripple;
    EraserMode eraser_mode = EraserMode::Hard;
    double effect_strength = 0.75, effect_scale = 24, effect_phase = 0;
    double heal_hardness = 0.35, heal_correction = 1;
    bool set_heal_source = true, stabilize = false, glitter = false;
    double stabilizer_lag = 5, tracker_noise = 1.5, tracker_momentum = 60;
    bool use_state_tracker = false, tracker_sparse = true;
    void regenerate_stamp();
    int stamp_width = 80, stamp_height = 80;
    double stamp_scale = 1, stamp_angle = 0, rotation_angle = 0, mesh_spacing = 60;
    void select_frame(int index, bool sequence = false);
    void set_reference_frame(int index);
    bool atlas_wrap = false, atlas_preserve_alpha = false;
    Image atlas_reference;
    int reference_frame = -1;
    std::uint64_t canvas_revision = 1;
    Guide guide;
    LassoMode lasso_mode = LassoMode::Free;
    double lasso_tolerance = 0.025;
    bool pick_hotspot = false, show_hotspot = false;
    Document document;
    CustomColors custom_colors;
    EditorSettings settings;
    RecentFiles recent;
    int jpeg_quality = 92;
    TextSession text;
    void text_input(gui_forms::TextInputEvent& event);
    bool text_key(gui_forms::KeyEvent& event);
    bool text_command(const std::string& command);
    void finish_text(bool place);
    void text_focus(bool focused);
    void text_frame();
    void selection_frame();
    bool show_help = false, eraser_soft = false;
    SampleMode picker_mode = SampleMode::Exact;
    bool picker_magnifier = false;
    bool show_rulers = false, show_grid = false, show_status = true, full_screen = false;
    void open_editor_dialog(EditorDialogKind kind, bool secondary = false);
    void close_editor_dialog();
    void arrange(gui_forms::Rect bounds) override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect damage) override;
    void paint_canvas_overlay(gui_forms::Painter& painter, gui_forms::Rect damage);
    void on_key_preview(gui_forms::KeyEvent& event) override;
    void on_drag(gui_forms::DragEvent& event) override;
    void ready(gui_forms::Window& window, gui_forms::ApplicationWindowHandle handle,
               const std::string& initial_path = {});
    void closing(gui_forms::HostCloseRequest& request);
    void execute(const std::string& command);
    void choose_tool(Tool tool);
    void on_attached_to_window() override;
    void on_detaching_from_window(gui_forms::Window& former_window) noexcept override;
    void choose_shape(Shape shape);
    void begin_path_swap(CurveKind kind);
    void begin_guide_swap(CurveKind kind);
    void edit_guide();
    void unset_guide();
    void refresh();
    void open_file(const std::string& path);
    bool save(bool save_as);
    void save_path(const std::string& path);
    std::string pending_save_path, deferred_command, deferred_open_path;
    void complete_deferred_save();
    gui_forms::RasterCanvas& canvas();
    void pointer(const gui_forms::PointerEvent& event);

  private:
    bool help_shortcut();
    void close_help(gui_forms::ButtonBase& button);
    int hit_path_node(Point point) const;
    Point snap_path_point(Point point) const;
    bool path_preview_point(Point& point) const;
    void publish_path_preview();
    gui_forms::AcceleratorToken help_accelerator_;
    WarpWorker warp_worker_;
    enum class WarpMode { none, mesh, rotation, transform };
    WarpMode warp_mode_ = WarpMode::none;
    FloatingSelection warp_original_;
    ReshapeMesh reshape_mesh_;
    std::shared_ptr<const ConvWarpField> warp_field_, stamp_field_;
    Image stamp_preview_, hard_stamp_, stamp_basis_;
    bool adding_stamp_material_ = false;
    std::vector<Point> stamp_boundary_;
    gui_forms::ImageId stamp_image_;
    void publish_stamp_preview();
    std::uint64_t warp_generation_ = 0, stamp_generation_ = 0, stamp_source_generation_ = 0;
    bool warp_pending_ = false, warp_commit_ = false, warp_whole_image_ = false;
    bool stamp_pending_ = false, rotation_dragging_ = false;
    int mesh_node_ = -1;
    double rotation_pointer_start_ = 0;
    void initialize_warp();
    void start_rotation();
    void commit_warp();
    AffineMap rotation_map() const;
    gui_forms::Point rotation_handle() const;
    bool warp_pointer(const gui_forms::PointerEvent& event, Point point);
    void paint_warp_overlay(gui_forms::Painter& painter);
    void stamp_at(Point point, bool checkpoint = true);
    std::shared_ptr<gui_forms::RasterCanvas> canvas_;
    std::shared_ptr<gui_forms::Label> status_, cursor_status_, dimensions_status_, selection_status_;
    std::shared_ptr<gui_forms::Button> zoom_out_, zoom_in_, zoom_reset_;
    std::shared_ptr<gui_forms::TrackBar> zoom_slider_;
    std::optional<gui_forms::Point> cursor_client_;
    bool synchronizing_zoom_ = false;
    std::shared_ptr<gui_forms::MenuStrip> menu_;
    std::shared_ptr<Ribbon> ribbon_;
    std::shared_ptr<HelpBook> help_;
    std::shared_ptr<EditorDialog> editor_dialog_;
    gui_forms::PopupToken editor_dialog_popup_;
    gui_forms::FocusScopeId editor_dialog_focus_;
    std::vector<std::shared_ptr<gui_forms::Command>> commands_;
    std::vector<gui_forms::SubscriptionToken> subscriptions_, menu_subscriptions_;
    void rebuild_file_menu();
    gui_forms::ApplicationWindowHandle handle_;
    std::uint64_t next_dialog_id_ = 1;
    gui_forms::FrameRequestToken text_caret_frame_, selection_frame_;
    std::vector<std::vector<Point>> selection_contours_;
    std::vector<std::uint8_t> selection_contour_mask_;
    int selection_contour_width_ = 0, selection_contour_height_ = 0;
    void update_selection_contours();
    void paint_selection_contours(gui_forms::Painter& painter);
    bool text_caret_visible_ = true;
    int text_drag_ = -3;
    Rect text_drag_bounds_;
    Point text_drag_start_;
    void paint_atlas_overlay(gui_forms::Painter& painter);
    void paint_tool_preview(gui_forms::Painter& painter);
    void paint_guide_overlay(gui_forms::Painter& painter);
    bool guide_pointer(const gui_forms::PointerEvent& event, Point point);
    std::optional<CurveKind> path_swap_kind_;
    int path_swap_segment_ = -1, path_swap_handle_ = -1;
    bool path_swap_checkpoint_ = false;
    bool path_swap_pointer(const gui_forms::PointerEvent& event, Point point);
    void paint_path_swap(gui_forms::Painter& painter);
    std::optional<CurveKind> guide_swap_kind_;
    int guide_swap_segment_ = -1, guide_swap_handle_ = -1;
    Tool guide_previous_tool_ = Tool::Pencil;
    bool guide_swap_pointer(const gui_forms::PointerEvent& event, Point point);
    int guide_node_ = -1;
    bool guide_moving_ = false, guide_connecting_ = false, guide_extending_ = false;
    Point guide_last_;
    Image paint_base_;
    void paint_segment(Point start, Point end);
    bool atlas_painting() const;
    void paint_text_overlay(gui_forms::Painter& painter);
    void text_pointer(const gui_forms::PointerEvent& event, Point point);
    void reset_text_caret();
    gui_forms::Rect text_caret_damage() const;
    Point start_, last_, current_, selection_offset_, handle_offset_;
    gui_drawing::PointF pan_origin_;
    gui_forms::Point pan_start_;
    std::vector<Point> lasso_;
    Image preview_;
    EraserStroke eraser_;
    MaterialStroke material_;
    DynamicBrushStroke dynamic_brush_;
    TransformStroke transform_brush_;
    HealingBrush healing_brush_;
    StrokeStabilizer stabilizer_;
    StrokeStateTracker state_tracker_;
    SparseStrokeTracker sparse_tracker_;
    std::chrono::steady_clock::time_point stroke_time_;
    bool shift_ = false, control_ = false, alt_ = false;
    int selection_edit_ = 0;
    int path_node_ = -1;
    Ink gesture_ink_, gesture_fill_ink_;
    bool dragging_ = false, moving_selection_ = false, preview_active_ = false, panning_ = false;
    bool handle_checkpoint_ = false;
    int curve_handle_ = -1;
    int resize_handle_ = -1;
    bool resize_selection_ = false;
    Point resize_start_;
    Rect resize_original_, resize_preview_;
    gui_forms::ImageId transform_image_;
    gui_forms::Rect transform_destination_;
    void update_transform_preview();
    void clear_transform_preview();
    bool resize_pointer(const gui_forms::PointerEvent& event, Point point);
    void paint_resize_overlay(gui_forms::Painter& painter);
    void update_status();
    void update_cursor_status();
    void zoom_slider_changed(double value);
    void status_clicked(gui_forms::ButtonBase& button);
    void command_invoked(const gui_forms::CommandInvocation& invocation);
    gui_forms::MenuItemSpec menu_item(const std::string& id, const std::string& text);
    void begin(Point point, bool secondary);
    void move(Point point);
    void apply_move(Point point, bool publish = true);
    void apply_curve(const std::vector<Point>& points);
    void end(Point point);
    void release_gesture();
    void finish_controls();
    void zoom(double factor, gui_forms::Point anchor);
    void copy();
    void paste();
    void edit_color(bool secondary);
    bool can_replace();
    gui_forms::HostServices& services();
    void error(const std::string& message);
    gui_forms::HostDialogResult dialog(gui_forms::HostDialogRequestPayload payload);
    gui_forms::Point screen(Point point) const;
};
} // namespace paint::forms

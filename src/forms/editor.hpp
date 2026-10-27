#pragma once
#include "carpet.hpp"
#include "recovery.hpp"
#include "file_change.hpp"
#include "file_reveal.hpp"
#include <future>
#include <gui_forms/timer.hpp>
#include "forms/interface_theme.hpp"
#include "forms/pattern_canvas.hpp"
#include "color_tools.hpp"
#include "desktop.hpp"
#include "document.hpp"
#include "forms/dialog.hpp"
#include "dither.hpp"
#include "forms/help.hpp"
#include "forms/ribbon.hpp"
#include "material.hpp"
#include "paint_tools.hpp"
#include "spirograph.hpp"
#include "text_session.hpp"
#include "warp_session.hpp"
#include <gui_forms/application.hpp>
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/canvas.hpp>
#include <gui_forms/live_surface.hpp>
#include <gui_forms/controls/menu_strip/menu_strip.hpp>
#include <gui_forms/controls/panel/combo_box/combo_box.hpp>
#include <gui_forms/controls/range_control/track_bar/track_bar.hpp>
#include <gui_forms/delegate.hpp>
#include <gui_forms/host.hpp>
namespace paint::forms {
class Editor;
struct CanvasWorkStatistics {
    std::uint64_t view_preparations = 0, view_renders = 0, stamp_preparations = 0, stamp_renders = 0;
};
class PaintCanvas final : public gui_forms::RasterCanvas {
  public:
    PaintCanvas(gui_forms::StableId id, std::weak_ptr<Editor> editor);
    ~PaintCanvas() override;
    void publish_pixels(const Image& source, Rect damage);
    std::shared_ptr<gui_forms::LiveSurface> display_surface() const { return display_surface_; }
    void set_transparency_colors(gui_forms::Color first, gui_forms::Color second);
    void publish_source(const Image& source, Rect damage);
    void poll_view();
    void prepare_display();
    void display_frame(gui_forms::FrameTime now);
    void preparation_frame(gui_forms::FrameTime now);
    void settle_view();
    const CanvasWorkStatistics& work_statistics() const noexcept {
        return work_statistics_;
    }
    void set_zoom(double zoom);
    void set_view_origin(gui_drawing::PointF origin);
    void set_view(double zoom, gui_drawing::PointF origin);
    void arrange(gui_forms::Rect bounds) override;
    bool view_busy() const;
    std::string view_error;

    double view_angle = 0;
    gui_drawing::PointF document_point(gui_drawing::PointF point) const noexcept;
    gui_forms::Rect view_bounds() const noexcept;
    Point visible_paste_origin(int width, int height);
    gui_forms::Point rotation_handle() const noexcept;
    void stroke_outline(gui_forms::Painter& painter, gui_drawing::RectI pixels, gui_forms::Color color,
                        double width) const;

    gui_forms::Rect bitmap_to_client(gui_drawing::RectI pixels) const noexcept;
    gui_drawing::PointF client_to_bitmap(gui_forms::Point client) const noexcept;
    gui_drawing::PointF view_point(gui_drawing::PointF point) const noexcept;
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
    friend class Editor;
    std::shared_ptr<gui_forms::LiveSurface> display_surface_, viewport_surface_, viewport_source_;
    std::uint64_t viewport_generation_ = 0;
    gui_forms::Rect viewport_bounds_;
    gui_drawing::PointF viewport_origin_;
    double viewport_zoom_ = 0, viewport_scale_ = 0;
    std::size_t translucent_pixels_ = 0;
    gui_forms::Color transparency_first_ = gui_forms::Color::rgba(246, 247, 249);
    gui_forms::Color transparency_second_ = gui_forms::Color::rgba(211, 215, 220);
    void paint_display(gui_forms::Painter& painter);
    CanvasWorkStatistics work_statistics_;
    gui_forms::FrameRequestToken display_request_, preparation_request_;
    gui_forms::FrameTime next_display_{}, preparation_deadline_{};
    void render_display();
    std::weak_ptr<Editor> editor_;
    gui_forms::ImageId backing_, repeated_, reference_, rotated_;
    Image view_source_;
    WarpWorker view_worker_;
    std::shared_ptr<const ConvWarpField> view_field_;
    std::uint64_t source_generation_ = 1, prepared_generation_ = 0, rendered_generation_ = 0,
                  rendered_revision_ = 0;
    double rendered_angle_ = 0, rendered_zoom_ = 0;
    gui_drawing::PointF rendered_origin_;
    gui_forms::Size rendered_size_;
    bool view_worker_initialized_ = false;
    void paint_rotated(gui_forms::Painter& painter, const Editor& editor);
    void prepare_view(gui_forms::FrameTime now = gui_forms::FrameClock::now());

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
    void poll_transform_preview();
    void prepare_stamp_view();
    void render_stamp_view();
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
    CarpetParameters carpet_parameters;
    Image carpet_tile;
    BrushFamily brush_family = BrushFamily::Additive;
    DitherBrushMode dither_brush_mode = DitherBrushMode::Neighborhood;
    DitherOptions dither_options;
    bool gradient_fill = false;
    Gradient fill_gradient = gradient_presets().front().gradient;
    std::size_t gradient_stop = 0;
    std::vector<std::uint8_t> canvas_selection_mask() const;
    MixEffect mix_effect = MixEffect::Ripple;
    EraserMode eraser_mode = EraserMode::Hard;
    double effect_strength = 0.75, effect_scale = 24, effect_phase = 0;
    double heal_hardness = 0.35, heal_correction = 1;
    HealingOptions heal_options;
    bool set_heal_source = true, stabilize = false, glitter = false;
    double stabilizer_lag = 5;
    void regenerate_stamp();
    int stamp_width = 80, stamp_height = 80;
    double stamp_scale = 1, stamp_angle = 0, rotation_angle = 0, mesh_spacing = 60;
    void select_frame(int index, bool sequence = false);
    void set_reference_frame(int index);
    bool atlas_wrap = false, atlas_preserve_alpha = false;
    bool icon_index_paint = false;
    unsigned icon_primary_index = 0, icon_alt_index = 1;
    void choose_icon_index(unsigned entry, bool secondary);
    bool icon_palette_available();
    Image atlas_reference;
    int reference_frame = -1;
    std::uint64_t canvas_revision = 1;
    Guide guide;
    LassoMode lasso_mode = LassoMode::Free;
    double lasso_tolerance = 0.025;
    bool pick_hotspot = false, show_hotspot = false;
    Document document;
    std::shared_ptr<Image> custom_pattern = std::make_shared<Image>();
    std::uint64_t custom_pattern_revision = 1;
    bool pattern_editing = false;
    void toggle_pattern_canvas();
    void store_custom_pattern();
    void load_custom_pattern();
    CustomColors custom_colors;
    EditorSettings settings;
    InterfaceThemes interface_themes;
    int preview_interface_hue = -1;
    void start_recovery(const std::string& initial_path);
    void recovery_tick();
    void recover_document();
    void reset_recovery(bool discard, const std::string& opened_path = "");
    void settle_recovery();
    std::filesystem::path recovery_root;
    std::unique_ptr<RecoverySession> recovery_session;
    std::unique_ptr<gui_forms::Timer> recovery_timer;
    gui_forms::SubscriptionToken recovery_subscription;
    std::future<void> recovery_job;
    RecoveryRecord recovery_metadata;
    std::chrono::steady_clock::time_point recovery_deadline{};
    std::uint64_t recovery_capture_revision = 0;
    bool recovery_failed = false, recovery_startup_pending = false;
    std::string recovery_notice;

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
    std::optional<IconEffectSettings> last_icon_effect;
    bool export_toolbar_strip(const Image& image, bool bmp, const std::string& source_path);
    bool export_png_image(const Image& image, const std::string& suggested_name);
    std::string choose_export_directory(const std::string& initial_directory);
    std::string choose_asset_directory(const std::string& initial_directory);
    void copy_alpha_image(const Image& source);
    Image read_alpha_mask();
    void copy_asset_path(const std::string& path);
    RevealHandler reveal_asset = &reveal_in_file_manager;
    void open_browser_asset(const AssetPreview& preview);
    void use_asset_stamp(Image image);
    void insert_sticky_note();
    StickyNoteSettings sticky_note_settings;
    bool stamp_placement_active() const { return stamp_placement_; }
    bool can_apply_stamp_placement() const;
    void apply_stamp_placement();
    void cancel_stamp_placement();
    std::string asset_directory, asset_bookmark_path;
    std::vector<std::string> batch_sources;
    std::vector<std::string> choose_batch_sources(bool multiple = true);
    std::string choose_palette_file();
    bool export_palette(const std::vector<Color>& colors);
    bool save_export_report(const std::string& report,
                            const std::string& suggested_name = "icon-export-report.json");
    bool export_icon_variant(const std::vector<std::uint8_t>& bytes, bool cursor,
                             const std::string& suggested_name, const std::string& source_path);
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
    void open_container(ImageContainer image, const std::string& path);
    void open_generated_image(Image image);
    bool save(bool save_as);
    bool save_path(const std::string& path);
    void file_watch_tick();
    std::string pending_save_path, deferred_command, deferred_open_path;
    Image deferred_generated_image;
    void complete_deferred_save();
    PaintCanvas& canvas();
    Spirograph spiro;
    bool spiro_lift = false;
    bool edit_path_nodes = false;
    void start_spirograph();
    void resize_spirograph(double scale);
    void spiro_choice(const std::string& id);
    void spiro_tray_pointer(int width, const gui_forms::PointerEvent& event);
    void cancel_spiro_drag();
    void rotate_view(double radians);
    void pointer(const gui_forms::PointerEvent& event);
    void canvas_control_action(int action);

  private:
    void reset_file_watch();
    void reload_file();
    void reload_clicked(gui_forms::ButtonBase& button);
    bool confirm_disk_action(const std::string& message);
    std::string watched_path_, file_notice_;
    FileStamp watched_stamp_, disk_stamp_;
    std::uint64_t file_watch_generation_ = 0, file_check_generation_ = 0;
    std::future<FileStamp> file_check_;
    std::unique_ptr<gui_forms::Timer> file_timer_;
    gui_forms::SubscriptionToken file_subscription_;
    std::shared_ptr<gui_forms::Label> file_notice_label_;
    std::shared_ptr<gui_forms::Button> file_reload_;
    void initialize_canvas_controls();
    void install_generated_image(Image image);
    void arrange_canvas_controls(gui_forms::Rect area);
    void canvas_control_pointer(gui_forms::PointerAction action, gui_forms::PointerButton button);
    void canvas_control_clicked(gui_forms::ButtonBase& button);
    std::shared_ptr<gui_forms::Panel> canvas_controls_;
    std::array<std::shared_ptr<gui_forms::NumericUpDown>, 3> canvas_positions_;
    std::vector<std::shared_ptr<gui_forms::Button>> canvas_actions_;
    std::shared_ptr<gui_forms::CheckBox> canvas_alt_, canvas_shift_, canvas_center_;
    Point canvas_position_;
    bool canvas_control_stroke_ = false, canvas_control_dispatch_ = false;
    gui_forms::PointerButton canvas_control_button_ = gui_forms::PointerButton::primary;
    std::shared_ptr<PatternCanvas> pattern_canvas_;
    std::string pattern_storage_path_;
    bool help_shortcut();
    void close_help(gui_forms::ButtonBase& button);
    int hit_path_node(Point point) const;
    Point snap_path_point(Point point) const;
    bool path_preview_point(Point& point) const;
    void publish_path_preview();
    gui_forms::AcceleratorToken help_accelerator_;
    WarpWorker warp_worker_, transform_preview_worker_;
    std::shared_ptr<const ConvWarpField> transform_preview_field_;
    Image transform_preview_source_;
    std::uint64_t transform_preview_generation_ = 1;
    bool transform_preview_pending_ = false, transform_preview_stamp_ = false;
    std::uint64_t stamp_view_generation_ = 0;
    gui_forms::ImageId stamp_view_image_;
    gui_forms::Rect stamp_view_destination_;
    std::uint64_t stamp_pixels_revision_ = 1, stamp_rendered_revision_ = 0;
    bool stamp_rendered_prepared_ = false;
    Point stamp_rendered_origin_;
    gui_drawing::PointF stamp_rendered_view_origin_;
    double stamp_rendered_angle_ = 0, stamp_rendered_zoom_ = 0;
    gui_forms::Rect stamp_rendered_viewport_;
    void begin_transform_preview(bool stamp = false);
    void end_transform_preview();
    enum class WarpMode { none, mesh, rotation, transform };
    WarpMode warp_mode_ = WarpMode::none;
    FloatingSelection warp_original_;
    ReshapeMesh reshape_mesh_;
    std::shared_ptr<const ConvWarpField> warp_field_, stamp_field_;
    Image stamp_preview_, hard_stamp_, stamp_basis_;
    bool adding_stamp_material_ = false;
    bool stamp_placement_ = false;
    Point stamp_placement_center_;
    std::uint64_t stamp_placement_revision_ = 0, stamp_placement_epoch_ = 0, stamp_preview_generation_ = 0;
    void composite_stamp(Image& target, const Image& base, Point point) const;
    Image stamp_placement_image() const;
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
    std::shared_ptr<PaintCanvas> canvas_;
    std::shared_ptr<gui_forms::Label> status_, cursor_status_, dimensions_status_, selection_status_;
    std::shared_ptr<gui_forms::Button> zoom_out_, zoom_in_, zoom_reset_, tool_size_status_;
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
    enum class SpiroDrag { None, Guide, Resize, Wheel, Lift, PegPending, Peg };
    SpiroDrag spiro_drag_ = SpiroDrag::None;
    Point spiro_grab_{}, spiro_pointer_{}, spiro_drag_center_{};
    double spiro_resize_scale_ = 1, spiro_resize_radius_ = 1;
    double spiro_lift_angle_ = 0, spiro_lift_offset_ = 0;
    bool spiro_lift_detached_ = false;
    Point spiro_lift_center_{};
    SpirographStroke spiro_stroke_;
    SpiroPeg spiro_carried_{};
    int spiro_origin_hole_ = -1, spiro_target_hole_ = -1;
    bool spiro_checkpoint_ = false;
    void paint_spiro_overlay(gui_forms::Painter& painter);
    bool spiro_pointer(const gui_forms::PointerEvent& event, Point point);
    int spiro_hole_at(Point point, bool empty_only) const;
    double spiro_grip_radius() const;
    void drop_spiro_peg();
    void paint_guide_overlay(gui_forms::Painter& painter);
    bool guide_pointer(const gui_forms::PointerEvent& event, Point point);
    std::optional<CurveKind> path_swap_kind_;
    int path_swap_segment_ = -1, path_swap_handle_ = -1;
    bool path_swap_checkpoint_ = false;
    bool path_swap_pointer(const gui_forms::PointerEvent& event, Point point);
    void paint_path_swap(gui_forms::Painter& painter);
    int hit_curve_handle(const CurveGeometry& geometry, Point point, double radius) const;
    int hovered_curve_handle(const CurveGeometry& geometry, double radius) const;
    int hovered_path_node() const;
    std::optional<CurveKind> guide_swap_kind_;
    int guide_swap_segment_ = -1, guide_swap_handle_ = -1;
    Tool guide_previous_tool_ = Tool::Pencil;
    bool guide_swap_pointer(const gui_forms::PointerEvent& event, Point point);
    int guide_node_ = -1;
    bool guide_moving_ = false, guide_connecting_ = false, guide_extending_ = false;
    Point guide_last_;
    Image paint_base_;
    bool begin_indexed_paint(Point point, bool secondary);
    void move_indexed_paint(Point point);
    bool indexed_gesture_ = false, indexed_checkpoint_ = false;
    unsigned indexed_entry_ = 0;
    std::vector<std::uint8_t> indexed_allowed_;
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
    DitherBrushStroke dither_brush_;
    std::vector<std::uint8_t> dither_brush_mask_;
    TransformStroke transform_brush_;
    HealingBrush healing_brush_;
    StrokeStabilizer stabilizer_;
    CarpetStroke carpet_stroke_;
    bool shift_ = false, control_ = false, alt_ = false;
    int selection_edit_ = 0;
    int path_node_ = -1;
    Ink gesture_ink_, gesture_fill_ink_;
    bool dragging_ = false, moving_selection_ = false, preview_active_ = false, panning_ = false;
    bool handle_checkpoint_ = false;
    bool rotating_view_ = false;
    double rotation_grab_angle_ = 0, rotation_start_angle_ = 0;
    gui_forms::Point rotation_pivot_;
    gui_drawing::PointF rotation_document_pivot_;
    bool picker_pending_ = false, picker_secondary_ = false;
    bool placing_shape_ = false;
    gui_forms::PointerButton placement_button_ = gui_forms::PointerButton::primary;
    int curve_handle_ = -1;
    int resize_handle_ = -1;
    bool resize_selection_ = false;
    Point resize_start_;
    Rect resize_original_, resize_preview_;
    AffineMap shear_map_;
    std::vector<std::uint8_t> warp_coverage_;
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
    void end(Point point);
    void release_gesture();
    void finish_controls(bool deselect = false);
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

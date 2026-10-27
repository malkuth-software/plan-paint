#pragma once
#include "desktop.hpp"
#include "document.hpp"
#include "imgui.h"
#include "material.hpp"
#include "platform.hpp"
#include "text.hpp"
#include "warp_session.hpp"
#include <SDL3/SDL.h>
namespace paint {
enum class Command {
    New,
    Open,
    OpenRecent,
    Save,
    SaveAs,
    Print,
    PageSetup,
    PrintPreview,
    Acquire,
    WallpaperFill,
    WallpaperTile,
    WallpaperCenter,
    Quit,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    PasteFrom,
    SelectAll,
    InvertSelection,
    Crop,
    Resize,
    RotateRight,
    RotateLeft,
    Rotate180,
    FlipHorizontal,
    FlipVertical,
    Invert,
    Delete,
    Properties,
    About
};
struct TextEditSnapshot {
    std::string content;
    std::size_t caret = 0, anchor = 0;
};
struct Application {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* canvas_texture = nullptr;
    SDL_Texture* stamp_texture = nullptr;
    SDL_Texture* text_texture = nullptr;
    SDL_Texture* material_texture = nullptr;
    std::string material_preview_signature;
    void material_preview(ImVec2 position);
    SDL_Texture* atlas_texture = nullptr;
    std::uint64_t atlas_texture_epoch = ~std::uint64_t(0), atlas_texture_generation = ~std::uint64_t(0);
    int atlas_texture_height = 48;
    bool atlas_tab = false, atlas_grid_dialog = false, icon_sizes_dialog = false;
    bool hotspot_pick = false, atlas_gallery_active = false;
    AtlasGrid atlas_grid;
    bool icon_sizes[8] = {true, false, true, true, false, false, false, true};
    std::string icon_save_path;
    void open_image(const std::string& path);
    void select_atlas_frame(int index, bool control = false);
    void step_atlas(int direction);
    void atlas_ribbon(float width);
    void atlas_dialogs();
    void atlas_thumbnail(int index);
    void refresh_atlas_texture();
    void atlas_overlay(ImDrawList& draw, ImVec2 origin);
    Document document;
    FileDialog dialog;
    RecentFiles recent_files;
    CustomColors custom_colors;
    int custom_color_slot = 0;
    Color original_color;
    std::string last_window_title;
    std::string recent_to_open;
    bool running = true, show_help = false, view_tab = false, text_tab = false, patterns_tab = false;
    bool show_grid = false, show_rulers = false, show_status = true, full_screen = false;
    bool texture_dirty = true, preview_active = false, dragging = false, moving_selection = false;
    bool color_dialog = false, resize_dialog = false, properties_dialog = false, about_dialog = false;
    bool text_active = false, unsaved_dialog = false, text_focus = false;
    bool print_preview = false, black_white = false;
    Command deferred_command = Command::New;
    bool deferred_after_save = false;
    bool primary_slot = true;
    bool right_gesture = false;
    Image preview, gesture_base, stamp_preview;
    MaterialStroke material_stroke;
    EraserStroke eraser_stroke;
    bool eraser_soft = false;
    bool zoom_scroll_pending = false;
    ImVec2 zoom_scroll;
    std::string path_preview_signature;
    std::string path_style_signature;
    bool path_preview_paused = false;
    Point down, last, hover, selection_start;
    std::vector<Point> lasso;
    Point curve_pending_end, curve_handle_offset;
    int curve_handle = -1;
    bool curve_handle_checkpoint = false;
    std::string curve_preview_signature;
    float zoom = 1.0f;
    int stamp_width = 80, stamp_height = 80;
    double stamp_scale = 1.0, stamp_angle = 0.0;
    int resize_width = 960, resize_height = 640;
    bool resize_percent = true, resize_lock = true, resize_scale = true;
    int resize_original_width = 960, resize_original_height = 640;
    int resize_handle = -1;
    int canvas_handle = -1;
    Rect handle_original;
    Rect handle_preview;
    Image selection_original;
    std::string error, status = "For Help, press F1";
    std::string screenshot_path;
    int screenshot_frame = 0;
    int rendered_frames = 0;
    std::uint64_t texture_generation = 0;
    char text_buffer[8192] = {};
    TextStyle text_style;
    std::vector<std::string> font_paths;
    void prepare_text_font();
    Point text_origin;
    int text_width = 440, text_height = 160;
    std::size_t text_caret = 0, text_anchor = 0;
    bool text_editing = true;
    TextLayout text_layout;
    Image text_preview;
    std::string text_preview_signature;
    std::vector<TextEditSnapshot> text_undo, text_redo;
    Rect text_drag_bounds;
    ImVec2 text_drag_mouse;
    double text_caret_epoch = 0;
    void refresh_text_preview();
    void text_canvas(ImVec2 canvas_origin);
    void cancel_text();
    void replace_text_selection(const std::string& inserted);
    void text_history(bool redo);

    Lab edited_lab;
    float edited_rgb[3] = {};
    char edited_hex[16] = {};
    int jpeg_quality = 95;
    WarpWorker warp_worker;
    std::shared_ptr<const ConvWarpField> reshape_field;
    std::shared_ptr<const ConvWarpField> stamp_field;
    ReshapeMesh reshape_mesh;
    Point reshape_origin;
    bool reshape_active = false, reshape_render_pending = false, reshape_commit_pending = false;
    bool stamp_render_pending = false, transform_selection = false;
    bool transform_active = false;
    int active_mesh_node = -1;
    double mesh_spacing = 60.0;
    std::uint64_t mesh_generation = 0, stamp_generation = 0;
    std::uint64_t stamp_source_generation = 0;
    double skew_horizontal = 0.0, skew_vertical = 0.0;
    std::shared_ptr<const ConvWarpField> rotation_field;
    FloatingSelection rotation_original;
    bool rotation_active = false, rotation_dragging = false, rotation_render_pending = false;
    bool rotation_commit_pending = false, rotation_place_pending = false, rotation_whole_image = false;
    double rotation_angle = 0, rotation_pointer_start = 0, requested_rotation = 0;
    std::uint64_t rotation_generation = 0;
    void start_rotation();
    void finish_rotation(bool place = false);
    void request_rotation(double degrees);
    void rotation_result(WarpResult& result);
    AffineMap rotation_map() const;
    bool rotation_control(ImVec2 origin, Point point, bool window_hovered);
    void draw_rotation_control(ImVec2 origin);
    void start_reshape();
    void finish_reshape();
    void poll_warp();
    void request_skew(int width, int height);
    explicit Application(SDL_Window* input_window, SDL_Renderer* input_renderer, bool preferences = true);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    void event(const SDL_Event& event);
    void frame();
    void ribbon(float width);
    void canvas(float width, float height);
    void help(float x, float y, float width, float height);
    void dialogs();
    void keyboard();
    void command(Command command);
    void execute(Command command);
    void choose_tool(Tool tool);
    void choose_shape(Shape shape);
    void begin_color();
    void edit_color(Color color);
    void color_editor();
    void stamp_controls();
    void reset_stamp();
    void refresh_texture(const Image& image);
    void begin_gesture(Point point, bool right);
    void update_gesture(Point point);
    void end_gesture(Point point);
    void finish_text();
    void finish_curve();
    void clear_curve_controls();
    void begin_curve_gesture(Point point, bool right);
    void update_curve_gesture(Point point);
    void end_curve_gesture(Point point);
    bool curve_control(Point point, bool window_hovered);
    void draw_curve_controls(ImDrawList& draw, ImVec2 origin);
    void refresh_curve_preview(Point point, bool over);
    void regenerate_stamp();
    void file_results();
    void save_to(const std::string& path);
    void report(const std::exception& exception);
};
ImU32 packed(Color color);
void classic_icon(ImDrawList& draw, int icon, ImVec2 position, float size,
                  ImU32 color = IM_COL32(40, 70, 100, 255));
bool ribbon_button(const char* id, const char* label, int icon, ImVec2 position, ImVec2 size,
                   bool selected = false, const char* tooltip = nullptr);
} // namespace paint

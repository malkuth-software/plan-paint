#include "application.hpp"
#include "codecs.hpp"
#include "conv.hpp"
#include "gui_scope.hpp"
#include "idle_render.hpp"
#include "imgui_impl_sdlrenderer3.h"
#include "paths.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <stdexcept>
namespace paint {
static std::string display_filename(const std::string& path) {
    std::u8string name = path_from_utf8(path).filename().u8string();
    return std::string(name.begin(), name.end());
}
ImU32 packed(Color color) {
    return IM_COL32(color.r, color.g, color.b, color.a);
}
Application::Application(SDL_Window* input_window, SDL_Renderer* input_renderer, bool preferences)
    : window(input_window), renderer(input_renderer) {
    warp_worker.set_completion(wake_event_loop);
    if (preferences) {
        try {
            recent_files.storage_path = preference_directory() + "recent-files.bin";
            recent_files.load();
            custom_colors.storage_path = preference_directory() + "custom-colors.bin";
            custom_colors.load();
        } catch (const std::exception&) {
            recent_files.storage_path.clear();
            custom_colors.storage_path.clear();
        }
    }
}
Application::~Application() {
    SDL_DestroyTexture(canvas_texture);
    SDL_DestroyTexture(stamp_texture);
    SDL_DestroyTexture(text_texture);
    SDL_DestroyTexture(material_texture);
    SDL_DestroyTexture(atlas_texture);
}
void Application::report(const std::exception& exception) {
    error = exception.what();
    status = error;
}
void Application::event(const SDL_Event& event_value) {
    try {
        if (event_value.type == SDL_EVENT_QUIT || event_value.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            command(Command::Quit);
        }
        if (event_value.type == SDL_EVENT_DROP_FILE && event_value.drop.data) {
            if (reshape_active || rotation_active || transform_active) {
                status = "Finish reshaping before dropping another picture.";
                return;
            }
            Image pasted = load_image(event_value.drop.data);
            finish_text();
            finish_curve();
            document.paste(pasted);
            texture_dirty = true;
            status = "Pasted image. Drag to position it, then press Escape to place it.";
        }
    } catch (const std::exception& exception) {
        report(exception);
    }
}
void Application::save_to(const std::string& path) {
    finish_text();
    document.sync_curve();
    document.sync_path();
    document.commit_selection();
    document.sync_atlas();
    std::string extension = image_extension(path);
    bool container = extension == ".ico" || extension == ".cur";
    bool collection = document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor;
    if (container && !collection) {
        icon_save_path = path;
        icon_sizes_dialog = true;
        return;
    }
    if (container) {
        save_container(document.output_container(extension == ".cur"), path);
    } else {
        save_image(document.output_image(), path, jpeg_quality);
    }
    if (collection && !container) {
        status = "Exported current Atlas image to " + display_filename(path);
        deferred_after_save = false;
        return;
    }
    if (container) {
        document.atlas.kind = extension == ".cur" ? AtlasKind::Cursor : AtlasKind::Icon;
    }
    document.filename = path;
    document.saved_revision = document.revision;
    recent_files.remember(path);
    status = "Saved " + display_filename(path);
    if (deferred_after_save) {
        deferred_after_save = false;
        execute(deferred_command);
    }
}
void Application::file_results() {
    FileResult result = dialog.take();
    if (!result.error.empty()) {
        error = result.error;
    }
    if (result.completed && result.path.empty()) {
        deferred_after_save = false;
        return;
    }
    if (result.action == FileAction::Save) {
        save_to(result.path);
    } else if (result.action == FileAction::Open) {
        open_image(result.path);
        recent_files.remember(result.path);
        text_active = false;
        clear_curve_controls();
        texture_dirty = true;
    } else if (result.action == FileAction::Paste) {
        document.paste(load_image(result.path));
        texture_dirty = true;
    }
}
void Application::command(Command requested) {
    try {
        if (rotation_active && requested != Command::Undo) {
            status = "Release the rotation handle and wait for the CONV result before another command.";
            return;
        }
        if (transform_active || reshape_commit_pending) {
            status = "Please wait for the current transform.";
            return;
        }
        if (reshape_active && requested != Command::Undo) {
            finish_reshape();
            status = "Press Escape to finish reshaping before another command.";
            return;
        }
        if ((requested == Command::New || requested == Command::Open || requested == Command::OpenRecent ||
             requested == Command::Quit) &&
            (document.dirty() || (text_active && text_buffer[0]))) {
            deferred_command = requested;
            unsaved_dialog = true;
            return;
        }
        execute(requested);
    } catch (const std::exception& exception) {
        report(exception);
    }
}
void Application::execute(Command requested) {
    switch (requested) {
    case Command::New:
        document.new_image();
        text_active = false;
        clear_curve_controls();
        break;
    case Command::Open:
        dialog.show(window, FileAction::Open, document.filename);
        break;
    case Command::OpenRecent:
        open_image(recent_to_open);
        recent_files.remember(recent_to_open);
        text_active = false;
        clear_curve_controls();
        break;
    case Command::Save:
        if (document.filename.empty() || !writable_image_path(document.filename)) {
            std::filesystem::path suggested =
                path_from_utf8(document.filename.empty() ? "Untitled.png" : document.filename);
            suggested.replace_extension(".png");
            dialog.show(window, FileAction::Save, path_to_utf8(suggested));
        } else {
            save_to(document.filename);
        }
        break;
    case Command::SaveAs: {
        std::filesystem::path suggested =
            path_from_utf8(document.filename.empty() ? "Untitled.png" : document.filename);
        if (!writable_image_path(path_to_utf8(suggested))) {
            suggested.replace_extension(".png");
        }
        dialog.show(window, FileAction::Save, path_to_utf8(suggested));
        break;
    }
    case Command::PrintPreview:
        finish_text();
        document.sync_curve();
        document.sync_path();
        print_preview = true;
        break;
    case Command::PageSetup:
        page_setup();
        break;
    case Command::Acquire: {
        std::string acquired;
        if (acquire_picture(acquired)) {
            finish_text();
            document.sync_curve();
            document.paste(load_image(acquired));
            std::error_code removal_error;
            std::filesystem::remove(path_from_utf8(acquired), removal_error);
            status = "Acquired picture. Move it, then press Escape to place it.";
        } else {
#ifdef _WIN32
            status = "Image acquisition canceled.";
#else
            status = "Save the picture in the capture app, then drag it here or use Paste from.";
#endif
        }
        break;
    }
    case Command::WallpaperFill:
    case Command::WallpaperTile:
    case Command::WallpaperCenter: {
        finish_text();
        document.sync_curve();
        document.sync_path();
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
        if (!mode) {
            throw std::runtime_error("Paint could not determine the desktop size.");
        }
        WallpaperLayout layout = requested == Command::WallpaperTile     ? WallpaperLayout::Tile
                                 : requested == Command::WallpaperCenter ? WallpaperLayout::Center
                                                                         : WallpaperLayout::Fill;
        Image wallpaper = wallpaper_image(document.visible_image(), (*mode).w, (*mode).h, layout);
        set_wallpaper(desktop_export(wallpaper, "Wallpaper"));
        status = "Desktop background updated.";
        break;
    }
    case Command::Print: {
        finish_text();
        document.sync_curve();
        document.sync_path();
        Image printable = document.visible_image();
        if (!print_image(printable)) {
            status = "Printing canceled or no system print service is available.";
        }
        break;
    }
    case Command::Quit: {
        std::lock_guard<std::mutex> lock(dialog.mutex);
        if (dialog.pending) {
            status = "Finish or cancel the file dialog before closing Paint.";
            break;
        }
        running = false;
        break;
    }
    case Command::Undo:
        if (document.curve.base && !document.curve.line_set) {
            finish_curve();
            break;
        }
        rotation_active = false;
        rotation_dragging = false;
        rotation_commit_pending = false;
        rotation_render_pending = false;
        rotation_place_pending = false;
        rotation_field.reset();
        rotation_original = {};
        ++rotation_generation;
        reshape_active = false;
        reshape_field.reset();
        reshape_mesh = {};
        reshape_render_pending = false;
        reshape_commit_pending = false;
        text_active = false;
        clear_curve_controls();
        preview_active = false;
        document.undo();
        path_preview_paused = true;
        if (document.path.session != 0) {
            status = "Path step undone. Remaining junctions are available; click to continue.";
        }
        break;
    case Command::Redo:
        clear_curve_controls();
        document.redo();
        path_preview_paused = true;
        if (document.path.session != 0) {
            status = "Path step restored. Right-click ends geometry; Escape releases the path.";
        }
        break;
    case Command::Copy:
    case Command::Cut: {
        Image copied = document.selection.active ? document.selection.image : document.visible_image();
        copy_to_clipboard(copied);
        if (requested == Command::Cut) {
            document.commit_path();
            finish_curve();
            if (document.selection.active) {
                document.delete_selection();
            } else {
                document.checkpoint();
                document.image.reset(document.image.width, document.image.height, document.ink.secondary);
            }
        }
        break;
    }
    case Command::Paste: {
        Image pasted;
        if (paste_from_clipboard(pasted)) {
            document.paste(pasted);
        } else {
            status = "The clipboard does not contain an image. Try Paste from or drag an image here.";
        }
        break;
    }
    case Command::PasteFrom:
        dialog.show(window, FileAction::Paste, "");
        break;
    case Command::SelectAll:
        document.select_all();
        document.tool = Tool::Select;
        break;
    case Command::InvertSelection:
        document.invert_selection();
        document.tool = Tool::Select;
        break;
    case Command::Crop:
        document.crop();
        break;
    case Command::Resize:
        resize_original_width =
            document.selection.active ? document.selection.image.width : document.image.width;
        resize_original_height =
            document.selection.active ? document.selection.image.height : document.image.height;
        resize_width = resize_percent ? 100 : resize_original_width;
        resize_height = resize_percent ? 100 : resize_original_height;
        resize_dialog = true;
        break;
    case Command::RotateRight:
        document.rotate(1);
        break;
    case Command::RotateLeft:
        document.rotate(3);
        break;
    case Command::Rotate180:
        document.rotate(2);
        break;
    case Command::FlipHorizontal:
        document.flip(true);
        break;
    case Command::FlipVertical:
        document.flip(false);
        break;
    case Command::Invert:
        document.invert_colors();
        break;
    case Command::Delete:
        document.commit_path();
        finish_curve();
        if (document.selection.active) {
            document.delete_selection();
        } else {
            document.checkpoint();
            document.image.reset(document.image.width, document.image.height, document.ink.secondary);
        }
        break;
    case Command::Properties:
        document.commit_selection();
        black_white = false;
        resize_width = document.image.width;
        resize_height = document.image.height;
        properties_dialog = true;
        break;
    case Command::About:
        about_dialog = true;
        break;
    }
    texture_dirty = true;
}
void Application::choose_tool(Tool tool) {
    if (rotation_active) {
        finish_rotation();
        return;
    }
    if (reshape_active) {
        finish_reshape();
        return;
    }
    if (transform_active) {
        return;
    }
    if (tool == Tool::Reshape) {
        start_reshape();
        if (reshape_active) {
            document.tool = tool;
        }
        return;
    }
    finish_text();
    if (tool != document.tool) {
        finish_curve();
        document.commit_path();
    }
    if (tool != Tool::Select && tool != Tool::Lasso && tool != Tool::Reshape) {
        document.commit_selection();
    }
    document.tool = tool;
    texture_dirty = true;
}
void Application::begin_color() {
    Color color = primary_slot ? document.ink.primary : document.ink.secondary;
    original_color = color;
    edit_color(color);
    color_dialog = true;
}
void Application::edit_color(Color color) {
    edited_lab = to_oklab(color);
    edited_rgb[0] = color.r / 255.0f;
    edited_rgb[1] = color.g / 255.0f;
    edited_rgb[2] = color.b / 255.0f;
    std::string hex = to_hex(color);
    std::snprintf(edited_hex, sizeof(edited_hex), "%s", hex.c_str());
}
void Application::keyboard() {
    ImGuiIO& io = ImGui::GetIO();
    bool shortcut = io.KeyCtrl || io.KeySuper;
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
        show_help = !show_help;
    }
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        if (atlas_gallery_active && !io.WantTextInput && !dragging && !transform_active && !reshape_active &&
            !rotation_active) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                step_atlas(-1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                step_atlas(1);
            }
        }
        return;
    }
    if (text_active) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancel_text();
        } else if (shortcut && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            finish_text();
        } else if (shortcut && ImGui::IsKeyPressed(ImGuiKey_S)) {
            command(Command::Save);
        }
        return;
    }
    if (io.WantTextInput || transform_active || reshape_commit_pending) {
        return;
    }
    if (shortcut) {
        if (ImGui::IsKeyPressed(ImGuiKey_N)) {
            command(Command::New);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O)) {
            command(Command::Open);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            command(io.KeyShift ? Command::SaveAs : Command::Save);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_P)) {
            command(Command::Print);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            command(io.KeyShift ? Command::Redo : Command::Undo);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
            command(Command::Redo);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            command(Command::Cut);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_C)) {
            command(Command::Copy);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V)) {
            command(Command::Paste);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            command(Command::SelectAll);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            command(Command::Properties);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            command(Command::Resize);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_I)) {
            command(Command::Invert);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G)) {
            show_grid = !show_grid;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        command(Command::Delete);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F12)) {
        command(Command::SaveAs);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        hotspot_pick = false;
        resize_handle = -1;
        canvas_handle = -1;
        selection_original = {};
        dragging = false;
        moving_selection = false;
        lasso.clear();
        material_stroke.clear();
        eraser_stroke.clear();
        if (document.tool == Tool::Stamp) {
            reset_stamp();
        }
        if (rotation_active) {
            finish_rotation(true);
            return;
        }
        if (reshape_active) {
            finish_reshape();
            return;
        }
        finish_text();
        finish_curve();
        if (document.path.session != 0) {
            status = "Path released. Click to begin a new path.";
        }
        document.commit_path();
        document.commit_selection();
        preview_active = false;
        texture_dirty = true;
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
        full_screen = !full_screen;
        SDL_SetWindowFullscreen(window, full_screen);
    }
    if (document.tool == Tool::Stamp && !shortcut) {
        bool changed = false;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            stamp_angle += io.KeyShift ? -15.0 : 15.0;
            changed = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            stamp_scale = std::min(8.0, stamp_scale * 1.1);
            changed = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            stamp_scale = std::max(0.1, stamp_scale / 1.1);
            changed = true;
        }
        if (changed) {
            regenerate_stamp();
        }
    }
    if (document.atlas.kind != AtlasKind::None && !document.selection.active && !dragging &&
        !reshape_active && !rotation_active && !transform_active) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            step_atlas(-1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            step_atlas(1);
        }
    }
    if (document.selection.active) {
        int amount = io.KeyShift ? 10 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            document.selection.x -= amount;
            texture_dirty = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            document.selection.x += amount;
            texture_dirty = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            document.selection.y -= amount;
            texture_dirty = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            document.selection.y += amount;
            texture_dirty = true;
        }
    }
}
void Application::refresh_texture(const Image& image) {
    if (canvas_texture) {
        float width = 0, height = 0;
        SDL_GetTextureSize(canvas_texture, &width, &height);
        if (static_cast<int>(width) != image.width || static_cast<int>(height) != image.height) {
            SDL_DestroyTexture(canvas_texture);
            canvas_texture = nullptr;
        }
    }
    if (!canvas_texture) {
        canvas_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                           image.width, image.height);
    }
    if (!canvas_texture) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetTextureScaleMode(canvas_texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(canvas_texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(canvas_texture, nullptr, image.pixels.data(), image.width * 4);
    texture_dirty = false;
    ++texture_generation;
}
static Rect drag_bounds(Point a, Point b, bool square) {
    if (square) {
        double extent = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
        b.x = a.x + (b.x >= a.x ? extent : -extent);
        b.y = a.y + (b.y >= a.y ? extent : -extent);
    }
    Rect bounds{static_cast<int>(std::floor(std::min(a.x, b.x))),
                static_cast<int>(std::floor(std::min(a.y, b.y))),
                std::max(1, static_cast<int>(std::ceil(std::abs(b.x - a.x)))),
                std::max(1, static_cast<int>(std::ceil(std::abs(b.y - a.y))))};
    return bounds;
}
static Point constrained(Point start, Point end) {
    double dx = end.x - start.x, dy = end.y - start.y;
    double length = std::hypot(dx, dy);
    double angle = std::round(std::atan2(dy, dx) / (std::numbers::pi / 4.0)) * (std::numbers::pi / 4.0);
    return {start.x + std::cos(angle) * length, start.y + std::sin(angle) * length};
}
void Application::begin_gesture(Point point, bool right) {
    down = point;
    last = point;
    right_gesture = right;
    dragging = true;
    if (transform_active || reshape_commit_pending) {
        dragging = false;
        return;
    }
    if (right && (document.tool == Tool::Path || document.tool == Tool::Stamp)) {
        dragging = false;
        if (document.tool == Tool::Path) {
            document.end_path_geometry();
            status = "Geometry ended. Click an old node to branch, or elsewhere to start a new run. Escape "
                     "releases all nodes.";
            texture_dirty = true;
        } else {
            reset_stamp();
        }
        return;
    }
    if (reshape_active) {
        active_mesh_node = -1;
        Point local{point.x - reshape_origin.x, point.y - reshape_origin.y};
        for (std::size_t i = 0; i < reshape_mesh.nodes.size(); ++i) {
            Point target = reshape_mesh.nodes[i].target;
            if (std::hypot(target.x - local.x, target.y - local.y) * zoom < 10) {
                active_mesh_node = static_cast<int>(i);
                break;
            }
        }
        if (active_mesh_node < 0) {
            dragging = false;
        }
        return;
    }
    if (document.tool == Tool::Select && document.selection.active) {
        Rect bounds{document.selection.x, document.selection.y, document.selection.image.width,
                    document.selection.image.height};
        if (point.x >= bounds.x && point.y >= bounds.y && point.x < bounds.x + bounds.w &&
            point.y < bounds.y + bounds.h) {
            moving_selection = true;
            selection_start = {static_cast<double>(bounds.x), static_cast<double>(bounds.y)};
            return;
        }
    }
    if (document.tool != Tool::Reshape) {
        document.commit_selection();
    }
    Ink ink = document.ink;
    if (right) {
        std::swap(ink.primary, ink.secondary);
    }
    switch (document.tool) {
    case Tool::Pencil:
    case Tool::Brush:
    case Tool::Eraser:
        document.checkpoint();
        if (document.tool == Tool::Pencil) {
            ink = pencil_ink(ink);
        }
        if (document.tool == Tool::Eraser) {
            ink = document.ink;
        }
        eraser_stroke.clear();
        material_stroke.clear();
        if (document.tool == Tool::Brush && (ink.brush == Brush::Round || textured_brush(ink.brush))) {
            material_stroke.segment(document.image, point, point, ink);
        } else if (document.tool == Tool::Eraser) {
            eraser_stroke.segment(document.image, point, point, ink.size, eraser_soft);
        } else if (document.tool == Tool::Pencil) {
            pixel_line(document.image, point, point, ink);
        } else {
            dab(document.image, point, ink);
        }
        break;
    case Tool::Fill:
        document.checkpoint();
        flood(document.image, static_cast<int>(point.x), static_cast<int>(point.y), ink);
        dragging = false;
        break;
    case Tool::Picker:
        if (right || !primary_slot) {
            document.ink.secondary = document.image.get(static_cast<int>(point.x), static_cast<int>(point.y));
        } else {
            document.ink.primary = document.image.get(static_cast<int>(point.x), static_cast<int>(point.y));
        }
        dragging = false;
        break;
    case Tool::Magnifier:
        zoom = std::clamp(zoom * (right ? 0.5f : 2.0f), 0.125f, 16.0f);
        dragging = false;
        break;
    case Tool::Shape:
        if (document.shape == Shape::Polygon) {
            document.tool = Tool::Path;
            document.continuous_path = false;
            begin_gesture(point, right);
            return;
        }
        if (document.shape == Shape::Bezier || document.shape == Shape::Arc) {
            begin_curve_gesture(point, right);
        } else {
            gesture_base = document.image;
            preview = gesture_base;
            preview_active = true;
        }
        break;
    case Tool::Path: {
        int snap = -1;
        for (std::size_t i = 0; i < document.path.nodes.size(); ++i) {
            if (std::hypot(document.path.nodes[i].x - point.x, document.path.nodes[i].y - point.y) * zoom <
                9.0) {
                snap = static_cast<int>(i);
                break;
            }
        }
        if (snap >= 0) {
            point = document.path.nodes[snap];
        }
        document.add_path_node(point);
        path_preview_paused = false;
        if (!document.continuous_path && document.path.nodes.size() - document.path.start > 2 &&
            point.x == document.path.nodes[document.path.start].x &&
            point.y == document.path.nodes[document.path.start].y) {
            document.end_path_geometry();
        }
        status = "Click to add a segment; right-click ends this run. Undo removes one node; Escape releases "
                 "the path.";
        dragging = false;
        break;
    }
    case Tool::Select:
    case Tool::Lasso:
        lasso.clear();
        lasso.push_back(point);
        break;
    case Tool::Stamp:
        if (document.stamp.pixels.empty()) {
            int width = stamp_width, height = (document.stamp_shape == StampShape::Circle ||
                                               document.stamp_shape == StampShape::Square)
                                                  ? stamp_width
                                                  : stamp_height;
            Rect bounds{static_cast<int>(point.x - width / 2.0), static_cast<int>(point.y - height / 2.0),
                        width, height};
            document.stamp = make_stamp(document.image, bounds, document.stamp_shape,
                                        document.stamp_transparent, document.ink.secondary);
            stamp_scale = 1.0;
            stamp_angle = 0.0;
            stamp_field.reset();
            ++stamp_source_generation;
            regenerate_stamp();
            status = "Stamp lifted. Click to repeat it; R rotates, + and - change size. Right-click or "
                     "Escape lifts a new stamp.";
        } else if (warp_worker.busy() || stamp_render_pending || stamp_preview.pixels.empty()) {
            status = "The stamp is preparing; it will be ready in a moment.";
        } else {
            document.checkpoint();
            composite(document.image, stamp_preview, static_cast<int>(point.x - stamp_preview.width / 2.0),
                      static_cast<int>(point.y - stamp_preview.height / 2.0));
        }
        dragging = false;
        break;
    case Tool::Text:
        finish_text();
        text_active = true;
        text_focus = true;
        text_origin = point;
        text_buffer[0] = '\0';
        text_tab = true;
        text_editing = true;
        text_caret = text_anchor = 0;
        text_undo.clear();
        text_redo.clear();
        text_preview_signature.clear();
        text_width = std::max(80, std::min(440, document.image.width - static_cast<int>(point.x)));
        text_height = 160;
        text_caret_epoch = ImGui::GetTime();
        dragging = false;
        break;
    case Tool::Reshape:
        dragging = false;
        status = "Select an object with the lasso, then choose Reshape.";
        break;
    }
    texture_dirty = true;
}
void Application::update_gesture(Point point) {
    if (!dragging) {
        return;
    }
    if (document.curve.base && !document.curve.line_set) {
        update_curve_gesture(point);
        last = point;
        return;
    }
    if (reshape_active && active_mesh_node >= 0) {
        Point target{point.x - reshape_origin.x, point.y - reshape_origin.y};
        if (move_reshape_node(reshape_mesh, static_cast<std::size_t>(active_mesh_node), target)) {
            ++mesh_generation;
            reshape_render_pending = true;
            status = "Drag knobs; Escape commits.";
        } else {
            status = "That move would fold the mesh. Keep the blue triangles open.";
        }
        last = point;
        return;
    }
    if (moving_selection) {
        document.selection.x = static_cast<int>(selection_start.x + point.x - down.x);
        document.selection.y = static_cast<int>(selection_start.y + point.y - down.y);
        texture_dirty = true;
        return;
    }
    Ink ink = document.ink;
    if (right_gesture) {
        std::swap(ink.primary, ink.secondary);
    }
    if (document.tool == Tool::Pencil || document.tool == Tool::Brush || document.tool == Tool::Eraser) {
        if (document.tool == Tool::Pencil) {
            ink = pencil_ink(ink);
        }
        if (document.tool == Tool::Eraser) {
            ink = document.ink;
        }
        if (document.tool == Tool::Brush && (ink.brush == Brush::Round || textured_brush(ink.brush))) {
            material_stroke.segment(document.image, last, point, ink);
        } else if (document.tool == Tool::Eraser) {
            eraser_stroke.segment(document.image, last, point, ink.size, eraser_soft);
        } else if (document.tool == Tool::Pencil) {
            pixel_line(document.image, last, point, ink);
        } else {
            stroke(document.image, last, point, ink);
        }
        texture_dirty = true;
    } else if (document.tool == Tool::Shape && preview_active) {
        preview = gesture_base;
        Point end = point;
        if (ImGui::GetIO().KeyShift) {
            if (document.shape == Shape::Line) {
                end = constrained(down, point);
            } else {
                Rect bounds = drag_bounds(down, point, true);
                end = {static_cast<double>(bounds.x + bounds.w), static_cast<double>(bounds.y + bounds.h)};
            }
        }
        draw_shape(preview, document.shape, down, end, ink, document.shape_outline, document.shape_fill,
                   document.shape_fill_brush);
        texture_dirty = true;
    } else if (document.tool == Tool::Lasso) {
        lasso.push_back(point);
    }
    last = point;
}
void Application::end_gesture(Point point) {
    if (!dragging) {
        return;
    }
    update_gesture(point);
    dragging = false;
    material_stroke.clear();
    if (reshape_active) {
        active_mesh_node = -1;
        return;
    }
    if (moving_selection) {
        moving_selection = false;
        return;
    }
    if (document.curve.base && !document.curve.line_set) {
        end_curve_gesture(point);
    } else if (document.tool == Tool::Shape && preview_active) {
        document.checkpoint();
        document.image = std::move(preview);
        preview_active = false;
    } else if (document.tool == Tool::Select || document.tool == Tool::Lasso) {
        Rect bounds = drag_bounds(down, point, false);
        if (document.tool == Tool::Lasso && lasso.size() > 2) {
            double left = lasso[0].x, right = left, top = lasso[0].y, bottom = top;
            for (Point p : lasso) {
                left = std::min(left, p.x);
                right = std::max(right, p.x);
                top = std::min(top, p.y);
                bottom = std::max(bottom, p.y);
            }
            bounds = drag_bounds({left, top}, {right, bottom}, false);
            document.select(bounds, lasso);
        } else if (bounds.w > 1 && bounds.h > 1) {
            document.select(bounds);
        }
        lasso.clear();
    }
    texture_dirty = true;
}
void Application::canvas(float width, float height) {
    ImGui::SetCursorPos(ImVec2(0, atlas_tab ? 154.0f : 131.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(207, 219, 234, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7, 7));
    ImGui::SetNextWindowContentSize({document.image.width * zoom + 22 + (show_rulers ? 25 : 0),
                                     document.image.height * zoom + 22 + (show_rulers ? 20 : 0)});
    ImGui::BeginChild("Canvas workspace", ImVec2(width, height), false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysUseWindowPadding);
    GuiScope child_scope(GuiEnd::Child, 1, 1);
    if (zoom_scroll_pending) {
        ImGui::SetScrollX(zoom_scroll.x);
        ImGui::SetScrollY(zoom_scroll.y);
        zoom_scroll_pending = false;
    }
    float previous_zoom = zoom;
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    if (show_rulers) {
        ImVec2 ruler = ImGui::GetCursorScreenPos();
        draw.AddRectFilled(ruler, ImVec2(ruler.x + document.image.width * zoom + 25, ruler.y + 20),
                           IM_COL32(247, 248, 250, 255));
        for (int i = 0; i < document.image.width; i += 50) {
            float x = ruler.x + 25 + i * zoom;
            draw.AddLine(ImVec2(x, ruler.y + 12), ImVec2(x, ruler.y + 20), IM_COL32(70, 80, 95, 255));
            char label[24] = {};
            std::snprintf(label, sizeof(label), "%d", i);
            draw.AddText(ImVec2(x + 2, ruler.y), IM_COL32(60, 65, 75, 255), label);
        }
        ImGui::Dummy(ImVec2(25, 20));
    }
    ImVec2 origin = ImGui::GetCursorScreenPos();
    if (show_rulers) {
        origin.x += 25;
        ImGui::SetCursorScreenPos(origin);
        draw.AddRectFilled({origin.x - 25, origin.y}, {origin.x, origin.y + document.image.height * zoom},
                           IM_COL32(247, 248, 250, 255));
        for (int pixel = 0; pixel < document.image.height; pixel += 50) {
            float tick_y = origin.y + pixel * zoom;
            draw.AddLine({origin.x - 7, tick_y}, {origin.x, tick_y}, IM_COL32(70, 80, 95, 255));
            char label[24] = {};
            std::snprintf(label, sizeof(label), "%d", pixel);
            draw.AddText(ImGui::GetFont(), 13.0f, {origin.x - 24, tick_y + 2}, IM_COL32(60, 65, 75, 255),
                         label);
        }
    }
    float image_width = document.image.width * zoom, image_height = document.image.height * zoom;
    ImGui::InvisibleButton("Drawing canvas", ImVec2(image_width + 8, image_height + 8),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool over = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    Point point{(io.MousePos.x - origin.x) / zoom, (io.MousePos.y - origin.y) / zoom};
    if (point.x != hover.x || point.y != hover.y) {
        path_preview_paused = false;
    }
    hover = point;
    bool inside =
        point.x >= 0 && point.y >= 0 && point.x < document.image.width && point.y < document.image.height;
    if (over && (io.KeyCtrl || io.KeySuper) && io.MouseWheel != 0) {
        zoom = std::clamp(zoom * (io.MouseWheel > 0 ? 1.25f : 0.8f), 0.125f, 16.0f);
    }
    if (hotspot_pick && document.atlas.kind == AtlasKind::Cursor && over && inside &&
        ImGui::IsMouseClicked(0)) {
        document.set_hotspot(static_cast<int>(point.x), static_cast<int>(point.y));
        hotspot_pick = false;
        return;
    }
    bool window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    bool over_curve_handle = curve_control(point, window_hovered);
    bool over_handle = rotation_control(origin, point, window_hovered) || over_curve_handle;
    Rect active_bounds = document.selection.active
                             ? Rect{document.selection.x, document.selection.y,
                                    document.selection.image.width, document.selection.image.height}
                             : Rect{0, 0, document.image.width, document.image.height};
    const double handle_x[8] = {0, 0.5, 1, 1, 1, 0.5, 0, 0};
    const double handle_y[8] = {0, 0, 0, 0.5, 1, 1, 1, 0.5};
    for (int i = 0; i < 8; ++i) {
        if (document.curve.base || text_active || reshape_active || rotation_active || transform_active ||
            (!document.selection.active && document.fixed_canvas())) {
            break;
        }
        if (!document.selection.active && i != 3 && i != 4 && i != 5) {
            continue;
        }
        double hx = active_bounds.x + active_bounds.w * handle_x[i];
        double hy = active_bounds.y + active_bounds.h * handle_y[i];
        if (std::abs(point.x - hx) * zoom <= 6 && std::abs(point.y - hy) * zoom <= 6 && over) {
            over_handle = true;
            ImGui::SetMouseCursor(i == 1 || i == 5   ? ImGuiMouseCursor_ResizeNS
                                  : i == 3 || i == 7 ? ImGuiMouseCursor_ResizeEW
                                  : i == 0 || i == 4 ? ImGuiMouseCursor_ResizeNWSE
                                                     : ImGuiMouseCursor_ResizeNESW);
            if (ImGui::IsMouseClicked(0)) {
                down = point;
                handle_original = active_bounds;
                handle_preview = active_bounds;
                if (document.selection.active) {
                    resize_handle = i;
                    selection_original = document.selection.image;
                } else {
                    canvas_handle = i;
                }
            }
        }
    }
    if (resize_handle >= 0 || canvas_handle >= 0) {
        int handle = resize_handle >= 0 ? resize_handle : canvas_handle;
        int dx = static_cast<int>(std::round(point.x - down.x)),
            dy = static_cast<int>(std::round(point.y - down.y));
        handle_preview = handle_original;
        if (handle == 0 || handle == 6 || handle == 7) {
            handle_preview.x += dx;
            handle_preview.w -= dx;
        }
        if (handle == 2 || handle == 3 || handle == 4) {
            handle_preview.w += dx;
        }
        if (handle == 0 || handle == 1 || handle == 2) {
            handle_preview.y += dy;
            handle_preview.h -= dy;
        }
        if (handle == 4 || handle == 5 || handle == 6) {
            handle_preview.h += dy;
        }
        handle_preview.w = std::clamp(handle_preview.w, 1, 32768);
        handle_preview.h = std::clamp(handle_preview.h, 1, 32768);
        if (ImGui::IsMouseReleased(0)) {
            try {
                if (resize_handle >= 0) {
                    Image resized;
                    conv_resize(selection_original, handle_preview.w, handle_preview.h, resized);
                    document.selection.image = std::move(resized);
                    document.selection.coverage.clear();
                    document.selection.outline.clear();
                    document.selection.x = handle_preview.x;
                    document.selection.y = handle_preview.y;
                } else {
                    document.resize(handle_preview.w, handle_preview.h, false);
                }
                texture_dirty = true;
            } catch (const std::exception& exception) {
                report(exception);
            }
            resize_handle = -1;
            canvas_handle = -1;
            selection_original = {};
        }
    }
    if (over && inside && !text_active && !over_handle && !rotation_active && resize_handle < 0 &&
        canvas_handle < 0) {
        ImGui::SetMouseCursor(document.tool == Tool::Select ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_None);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            begin_gesture(point, false);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            begin_gesture(point, true);
        }
        char location[64] = {};
        std::snprintf(location, sizeof(location), "%d, %d px", static_cast<int>(point.x),
                      static_cast<int>(point.y));
        if (!dragging && !document.curve.base && document.tool != Tool::Stamp &&
            document.tool != Tool::Path) {
            status = location;
        }
    }
    if (dragging && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1))) {
        if (point.x != last.x || point.y != last.y) {
            update_gesture(point);
        }
    }
    if (dragging && (ImGui::IsMouseReleased(0) || ImGui::IsMouseReleased(1))) {
        end_gesture(point);
    }
    if (zoom != previous_zoom) {
        zoom_scroll = {ImGui::GetScrollX() + float(point.x) * (zoom - previous_zoom),
                       ImGui::GetScrollY() + float(point.y) * (zoom - previous_zoom)};
        zoom_scroll_pending = true;
    }
    refresh_curve_preview(point, over && inside);
    Point path_next;
    bool show_path_next = false;
    if (document.tool == Tool::Path && !document.path.nodes.empty()) {
        if (document.path.extending && !path_preview_paused && inside && over) {
            Point next = point;
            for (std::size_t i = 0; i < document.path.nodes.size(); ++i) {
                if (std::hypot(document.path.nodes[i].x - point.x, document.path.nodes[i].y - point.y) *
                        zoom <
                    9) {
                    next = document.path.nodes[i];
                    break;
                }
            }
            path_next = next;
            show_path_next = true;
        }
        std::string signature = std::to_string(document.path.session) + ":" +
                                std::to_string(document.path.start) + ":" +
                                std::to_string(document.path.extending) + ":";
        for (Point node : document.path.nodes) {
            signature += std::to_string(node.x) + "," + std::to_string(node.y) + ";";
        }
        if (show_path_next) {
            signature += ">" + std::to_string(path_next.x) + "," + std::to_string(path_next.y);
        }
        std::string style_signature =
            std::to_string(document.ink.grain_scale) + ":" + std::to_string(document.ink.paper_roughness) +
            ":" + std::to_string(document.ink.pigment_load) + ":" +
            std::to_string(document.ink.material_angle) + ":" + std::to_string(document.ink.noise) + ":" +
            std::to_string(packed(document.ink.secondary)) + ":" + std::to_string(document.shape_fill) +
            std::to_string(document.shape_outline) + std::to_string(document.continuous_path) +
            std::to_string(document.ink.transparent_pattern) + ":" +
            std::to_string(int(document.shape_fill_brush)) + ":" + std::to_string(document.revision) + ":";
        style_signature +=
            std::to_string(document.ink.size) + ":" + std::to_string(packed(document.ink.primary)) + ":" +
            std::to_string(int(document.ink.brush)) + ":" + std::to_string(int(document.ink.pattern));
        if (style_signature != path_style_signature) {
            document.sync_path();
            path_style_signature = style_signature;
        }
        signature += style_signature;
        if (signature != path_preview_signature) {
            texture_dirty = true;
            path_preview_signature = signature;
        }
    } else if (!path_preview_signature.empty()) {
        path_preview_signature.clear();
        texture_dirty = true;
    }
    if (texture_dirty) {
        if (show_path_next) {
            Image visible = document.path_image(&path_next);
            refresh_texture(visible);
        } else if (preview_active) {
            refresh_texture(preview);
        } else if (document.selection.active) {
            Image visible = document.visible_image();
            refresh_texture(visible);
        } else {
            refresh_texture(document.image);
        }
    }
    draw.AddRectFilled(ImVec2(origin.x + 3, origin.y + 3),
                       ImVec2(origin.x + image_width + 3, origin.y + image_height + 3),
                       IM_COL32(140, 157, 178, 255));
    draw.AddRectFilled(origin, ImVec2(origin.x + image_width, origin.y + image_height),
                       IM_COL32(255, 255, 255, 255));
    // Transparency uses a small checkerboard whose size stays legible at every zoom.
    ImVec2 viewport_start = ImGui::GetWindowPos();
    ImVec2 viewport_size = ImGui::GetWindowSize();
    int checker_left = std::max(0, static_cast<int>((viewport_start.x - origin.x) / 12.0f) * 12);
    int checker_top = std::max(0, static_cast<int>((viewport_start.y - origin.y) / 12.0f) * 12);
    int checker_right =
        static_cast<int>(std::min(image_width, viewport_start.x + viewport_size.x - origin.x));
    int checker_bottom =
        static_cast<int>(std::min(image_height, viewport_start.y + viewport_size.y - origin.y));
    for (int y = checker_top; y < checker_bottom; y += 12) {
        for (int x = checker_left; x < checker_right; x += 12) {
            if (((x / 12 + y / 12) & 1) == 0) {
                draw.AddRectFilled(ImVec2(origin.x + x, origin.y + y),
                                   ImVec2(origin.x + std::min(static_cast<float>(x + 12), image_width),
                                          origin.y + std::min(static_cast<float>(y + 12), image_height)),
                                   IM_COL32(240, 240, 240, 255));
            }
        }
    }
    draw.AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(canvas_texture)), origin,
                  ImVec2(origin.x + image_width, origin.y + image_height));
    atlas_overlay(draw, origin);
    if (show_grid && zoom >= 4) {
        for (int x = 0; x <= document.image.width; ++x) {
            draw.AddLine(ImVec2(origin.x + x * zoom, origin.y),
                         ImVec2(origin.x + x * zoom, origin.y + image_height), IM_COL32(170, 180, 190, 100));
        }
        for (int y = 0; y <= document.image.height; ++y) {
            draw.AddLine(ImVec2(origin.x, origin.y + y * zoom),
                         ImVec2(origin.x + image_width, origin.y + y * zoom), IM_COL32(170, 180, 190, 100));
        }
    }
    draw.AddRect(ImVec2(origin.x - 1, origin.y - 1), ImVec2(origin.x + image_width, origin.y + image_height),
                 IM_COL32(150, 162, 176, 255));
    if (document.selection.active && !reshape_active && !rotation_active) {
        FloatingSelection& selected = document.selection;
        ImVec2 a(origin.x + selected.x * zoom, origin.y + selected.y * zoom);
        ImVec2 b(a.x + selected.image.width * zoom, a.y + selected.image.height * zoom);
        draw.AddRect(a, b, IM_COL32(20, 98, 190, 255));
        ImVec2 handles[8] = {{a.x, a.y}, {(a.x + b.x) / 2, a.y}, {b.x, a.y}, {b.x, (a.y + b.y) / 2},
                             {b.x, b.y}, {(a.x + b.x) / 2, b.y}, {a.x, b.y}, {a.x, (a.y + b.y) / 2}};
        for (int i = 0; i < 8; ++i) {
            draw.AddRectFilled(ImVec2(handles[i].x - 3, handles[i].y - 3),
                               ImVec2(handles[i].x + 3, handles[i].y + 3), IM_COL32(255, 255, 255, 255));
            draw.AddRect(ImVec2(handles[i].x - 3, handles[i].y - 3),
                         ImVec2(handles[i].x + 3, handles[i].y + 3), IM_COL32(30, 95, 160, 255));
        }
    } else if (!rotation_active) {
        ImVec2 handles[3] = {{origin.x + image_width, origin.y + image_height / 2},
                             {origin.x + image_width / 2, origin.y + image_height},
                             {origin.x + image_width, origin.y + image_height}};
        for (int i = 0; i < 3; ++i) {
            draw.AddRectFilled(ImVec2(handles[i].x, handles[i].y), ImVec2(handles[i].x + 5, handles[i].y + 5),
                               IM_COL32(255, 255, 255, 255));
            draw.AddRect(ImVec2(handles[i].x, handles[i].y), ImVec2(handles[i].x + 5, handles[i].y + 5),
                         IM_COL32(95, 112, 135, 255));
        }
    }
    draw_rotation_control(origin);
    if (resize_handle >= 0 || canvas_handle >= 0) {
        ImVec2 a(origin.x + handle_preview.x * zoom, origin.y + handle_preview.y * zoom);
        ImVec2 b(a.x + handle_preview.w * zoom, a.y + handle_preview.h * zoom);
        draw.AddRect(a, b, IM_COL32(0, 100, 210, 255), 0, 0, 2);
        char dimensions[64] = {};
        std::snprintf(dimensions, sizeof(dimensions), "%d x %d px", handle_preview.w, handle_preview.h);
        draw.AddText(ImVec2(b.x + 8, b.y + 5), IM_COL32(20, 50, 85, 255), dimensions);
    }
    if (reshape_active) {
        for (const MeshTriangle& triangle : reshape_mesh.triangles) {
            for (int edge = 0; edge < 3; ++edge) {
                Point a = reshape_mesh.nodes[triangle.nodes[edge]].target;
                Point b = reshape_mesh.nodes[triangle.nodes[(edge + 1) % 3]].target;
                draw.AddLine(ImVec2(origin.x + static_cast<float>(a.x + reshape_origin.x) * zoom,
                                    origin.y + static_cast<float>(a.y + reshape_origin.y) * zoom),
                             ImVec2(origin.x + static_cast<float>(b.x + reshape_origin.x) * zoom,
                                    origin.y + static_cast<float>(b.y + reshape_origin.y) * zoom),
                             IM_COL32(0, 105, 205, 125));
            }
        }
        for (std::size_t i = 0; i < reshape_mesh.nodes.size(); ++i) {
            const MeshNode& node = reshape_mesh.nodes[i];
            ImVec2 center(origin.x + static_cast<float>(node.target.x + reshape_origin.x) * zoom,
                          origin.y + static_cast<float>(node.target.y + reshape_origin.y) * zoom);
            draw.AddCircleFilled(center, 6, IM_COL32(255, 255, 255, 255));
            draw.AddCircleFilled(center, 4.5f,
                                 static_cast<int>(i) == active_mesh_node ? IM_COL32(245, 155, 20, 255)
                                                                         : IM_COL32(0, 110, 220, 255));
        }
    }
    if (document.tool == Tool::Path && !document.path.nodes.empty()) {
        for (Point node : document.path.nodes) {
            if (std::hypot(node.x - point.x, node.y - point.y) * zoom < 12) {
                ImVec2 center(origin.x + static_cast<float>(node.x) * zoom,
                              origin.y + static_cast<float>(node.y) * zoom);
                draw.AddCircleFilled(center, 5, IM_COL32(0, 120, 215, 255));
                draw.AddCircle(center, 6, IM_COL32(255, 255, 255, 255));
            }
        }
    }
    draw_curve_controls(draw, origin);
    if (dragging && (document.tool == Tool::Select || document.tool == Tool::Lasso) && !moving_selection) {
        if (document.tool == Tool::Select) {
            Rect bounds = drag_bounds(down, point, false);
            draw.AddRect(
                ImVec2(origin.x + bounds.x * zoom, origin.y + bounds.y * zoom),
                ImVec2(origin.x + (bounds.x + bounds.w) * zoom, origin.y + (bounds.y + bounds.h) * zoom),
                IM_COL32(0, 100, 190, 255));
        } else {
            for (std::size_t i = 1; i < lasso.size(); ++i) {
                draw.AddLine(ImVec2(origin.x + static_cast<float>(lasso[i - 1].x) * zoom,
                                    origin.y + static_cast<float>(lasso[i - 1].y) * zoom),
                             ImVec2(origin.x + static_cast<float>(lasso[i].x) * zoom,
                                    origin.y + static_cast<float>(lasso[i].y) * zoom),
                             IM_COL32(0, 100, 190, 255));
            }
        }
    }
    if (over && inside && !text_active) {
        draw.PushClipRect(origin, {origin.x + image_width, origin.y + image_height}, true);
        if (document.tool == Tool::Eraser) {
            float radius = std::max(.5f, document.ink.size * .5f) * zoom;
            if (eraser_soft) {
                // Concentric translucent shells show the same spherical falloff as the eraser.
                const int shells = 32;
                double previous = 0;
                for (int ring = shells; ring > 0; --ring) {
                    double fraction = (ring - .5) / shells;
                    double alpha = .42 * std::sqrt(1 - fraction * fraction);
                    int layer = int(255 * (alpha - previous) / (1 - previous));
                    draw.AddCircleFilled(io.MousePos, radius * ring / shells, IM_COL32(245, 65, 118, layer),
                                         64);
                    previous = alpha;
                }
            } else {
                draw.AddCircleFilled(io.MousePos, radius, IM_COL32(245, 65, 118, 100), 64);
            }
            draw.AddCircle(io.MousePos, radius, IM_COL32(199, 37, 89, 220), 64, 1.25f);
        } else if (document.tool == Tool::Pencil && !dragging) {
            int px = int(std::floor(point.x)), py = int(std::floor(point.y));
            Ink pencil_ink = document.ink;
            if (ImGui::IsMouseDown(1)) {
                std::swap(pencil_ink.primary, pencil_ink.secondary);
            }
            ImVec2 a(origin.x + px * zoom, origin.y + py * zoom), b(a.x + zoom, a.y + zoom);
            draw.AddRectFilled(a, b, packed(patterned(pencil_ink, px, py)));
            if (zoom >= 4) {
                draw.AddRect(a, b, IM_COL32(255, 255, 255, 230));
                draw.AddRect({a.x - 1, a.y - 1}, {b.x + 1, b.y + 1}, IM_COL32(30, 30, 30, 220));
            }
        } else if (document.tool == Tool::Magnifier && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1)) {
            float radius = 64, magnification = zoom * 2;
            ImVec2 a(io.MousePos.x - radius, io.MousePos.y - radius),
                b(io.MousePos.x + radius, io.MousePos.y + radius);
            ImVec2 uv0(float(point.x - radius / magnification) / document.image.width,
                       float(point.y - radius / magnification) / document.image.height);
            ImVec2 uv1(float(point.x + radius / magnification) / document.image.width,
                       float(point.y + radius / magnification) / document.image.height);
            // Crop at the picture boundary; UVs never repeat edge pixels outside the document.
            a.x = std::max(a.x, io.MousePos.x - float(point.x) * magnification);
            a.y = std::max(a.y, io.MousePos.y - float(point.y) * magnification);
            b.x = std::min(b.x, io.MousePos.x + float(document.image.width - point.x) * magnification);
            b.y = std::min(b.y, io.MousePos.y + float(document.image.height - point.y) * magnification);
            uv0.x = std::max(0.0f, uv0.x);
            uv0.y = std::max(0.0f, uv0.y);
            uv1.x = std::min(1.0f, uv1.x);
            uv1.y = std::min(1.0f, uv1.y);
            draw.AddRectFilled(a, b, IM_COL32(245, 245, 245, 255));
            draw.AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(canvas_texture)), a, b,
                          uv0, uv1);
            draw.AddRect(a, b, IM_COL32(45, 82, 117, 255), 0, 0, 2);
        }
        draw.PopClipRect();
        if (document.tool == Tool::Stamp) {
            if (stamp_texture && !document.stamp.pixels.empty()) {
                ImVec2 a(io.MousePos.x - stamp_preview.width * zoom / 2,
                         io.MousePos.y - stamp_preview.height * zoom / 2);
                draw.AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(stamp_texture)), a,
                              ImVec2(a.x + stamp_preview.width * zoom, a.y + stamp_preview.height * zoom),
                              ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 180));
            } else {
                float w = stamp_width * zoom, h = (document.stamp_shape == StampShape::Circle ||
                                                           document.stamp_shape == StampShape::Square
                                                       ? stamp_width
                                                       : stamp_height) *
                                                  zoom;
                ImVec2 a(io.MousePos.x - w / 2, io.MousePos.y - h / 2),
                    b(io.MousePos.x + w / 2, io.MousePos.y + h / 2);
                if (document.stamp_shape == StampShape::Circle) {
                    draw.AddEllipse(io.MousePos, ImVec2(w / 2, h / 2), IM_COL32(0, 90, 200, 255));
                } else {
                    draw.AddRect(a, b, IM_COL32(0, 90, 200, 255),
                                 document.stamp_shape == StampShape::Pill ? std::min(w, h) / 2 : 0);
                }
            }
        }
        draw.AddLine(ImVec2(io.MousePos.x - 7, io.MousePos.y), ImVec2(io.MousePos.x + 7, io.MousePos.y),
                     IM_COL32(30, 30, 30, 255));
        draw.AddLine(ImVec2(io.MousePos.x, io.MousePos.y - 7), ImVec2(io.MousePos.x, io.MousePos.y + 7),
                     IM_COL32(30, 30, 30, 255));
    }
    if (text_active) {
        text_canvas(origin);
    }
}
void Application::dialogs() {
    color_editor();
    atlas_dialogs();
    if (resize_dialog) {
        ImGui::OpenPopup("Resize and Skew");
        resize_dialog = false;
    }
    if (ImGui::BeginPopupModal("Resize and Skew", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        GuiScope popup_scope(GuiEnd::Popup);
        ImGui::TextUnformatted(document.selection.active ? "Resize selected content" : "Resize the picture");
        bool previous = resize_percent;
        if (ImGui::RadioButton("Percentage", resize_percent)) {
            resize_percent = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Pixels", !resize_percent)) {
            resize_percent = false;
        }
        if (previous != resize_percent) {
            resize_width = resize_percent ? 100 : resize_original_width;
            resize_height = resize_percent ? 100 : resize_original_height;
        }
        if (ImGui::InputInt("Horizontal", &resize_width) && resize_lock) {
            resize_height =
                resize_percent ? resize_width
                               : static_cast<int>(std::round(static_cast<double>(resize_width) *
                                                             resize_original_height / resize_original_width));
        }
        if (ImGui::InputInt("Vertical", &resize_height) && resize_lock) {
            resize_width = resize_percent
                               ? resize_height
                               : static_cast<int>(std::round(static_cast<double>(resize_height) *
                                                             resize_original_width / resize_original_height));
        }
        ImGui::Checkbox("Maintain aspect ratio", &resize_lock);
        ImGui::Checkbox("Scale artwork (CONV*)", &resize_scale);
        ImGui::TextDisabled("Turn scaling off to change the canvas boundary.");
        ImGui::Separator();
        ImGui::TextUnformatted("Skew (degrees)");
        ImGui::InputDouble("Horizontal angle", &skew_horizontal, 1, 5, "%.1f");
        ImGui::InputDouble("Vertical angle", &skew_vertical, 1, 5, "%.1f");
        skew_horizontal = std::clamp(skew_horizontal, -80.0, 80.0);
        skew_vertical = std::clamp(skew_vertical, -80.0, 80.0);
        if (ImGui::Button("OK", ImVec2(90, 0))) {
            double intended_width = resize_percent
                                        ? static_cast<double>(resize_original_width) * resize_width / 100.0
                                        : resize_width;
            double intended_height = resize_percent
                                         ? static_cast<double>(resize_original_height) * resize_height / 100.0
                                         : resize_height;
            if (intended_width < 1 || intended_width > 32768 || intended_height < 1 ||
                intended_height > 32768) {
                throw std::runtime_error("Choose dimensions between 1 and 32768 pixels.");
            }
            int width = resize_percent
                            ? static_cast<int>(std::round(static_cast<double>(resize_original_width) *
                                                          resize_width / 100.0))
                            : resize_width;
            int height = resize_percent
                             ? static_cast<int>(std::round(static_cast<double>(resize_original_height) *
                                                           resize_height / 100.0))
                             : resize_height;
            if (skew_horizontal != 0.0 || skew_vertical != 0.0) {
                request_skew(width, height);
            } else {
                document.resize(width, height, resize_scale);
            }
            texture_dirty = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (print_preview) {
        ImGui::OpenPopup("Print preview");
        print_preview = false;
    }
    ImGui::SetNextWindowSize({650, 640}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Print preview", nullptr, ImGuiWindowFlags_NoResize)) {
        GuiScope popup_scope(GuiEnd::Popup);
        if (ImGui::Button("Print...")) {
            command(Command::Print);
        }
        ImGui::SameLine();
        if (ImGui::Button("Page setup...")) {
            page_setup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close preview") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextWrapped(
            "Artwork preview. The system print dialog controls paper, orientation and copies.");
        ImVec2 available = ImGui::GetContentRegionAvail();
        float factor =
            std::min((available.x - 24) / document.image.width, (available.y - 24) / document.image.height);
        ImVec2 start = ImGui::GetCursorScreenPos();
        start.x += (available.x - document.image.width * factor) / 2;
        start.y += 12;
        ImVec2 end(start.x + document.image.width * factor, start.y + document.image.height * factor);
        ImDrawList& draw = *ImGui::GetWindowDrawList();
        draw.AddRectFilled({start.x + 4, start.y + 4}, {end.x + 4, end.y + 4}, IM_COL32(160, 171, 184, 255));
        draw.AddRectFilled(start, end, IM_COL32(255, 255, 255, 255));
        draw.AddImage(reinterpret_cast<ImTextureID>(canvas_texture), start, end);
        draw.AddRect(start, end, IM_COL32(90, 108, 130, 255));
    }
    if (properties_dialog) {
        ImGui::OpenPopup("Image Properties");
        properties_dialog = false;
    }
    if (ImGui::BeginPopupModal("Image Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        GuiScope popup_scope(GuiEnd::Popup);
        ImGui::Text("Width: %d pixels   Height: %d pixels", document.image.width, document.image.height);
        ImGui::Text("RGBA color; %.2f MB of pixel storage", document.image.pixels.size() * 4 / 1048576.0);
        ImGui::InputInt("Canvas width", &resize_width);
        ImGui::InputInt("Canvas height", &resize_height);
        ImGui::SliderInt("JPEG quality", &jpeg_quality, 1, 100);
        ImGui::Checkbox("Convert to black and white", &black_white);
        if (ImGui::Button("OK", ImVec2(90, 0))) {
            document.resize(resize_width, resize_height, false);
            if (black_white) {
                for (Color& pixel : document.image.pixels) {
                    unsigned brightness = 54u * pixel.r + 183u * pixel.g + 19u * pixel.b;
                    std::uint8_t value = brightness >= 128u * 256u ? 255 : 0;
                    pixel.r = value;
                    pixel.g = value;
                    pixel.b = value;
                }
            }
            texture_dirty = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (about_dialog) {
        ImGui::OpenPopup("About Plan Paint");
        about_dialog = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
    bool show_about =
        ImGui::BeginPopupModal("About Plan Paint", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (show_about) {
        GuiScope popup_scope(GuiEnd::Popup);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.0f);
        ImGui::TextUnformatted(
            "To the Holy One, blessed be He, from whom all good things come. "
            "This work is dedicated in gratitude for the nourishment that sustains human life, "
            "the energy that powers our tools, and the opportunity to weave information into "
            "works of use and beauty.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Plan Paint\n\nAuthor: Astra\nSponsor: Rainstar\n\n"
                               "Copyright (c) 2026 joshuah.rainstar@gmail.com\n"
                               "Licensed under the PolyForm Noncommercial License 1.0.0.\n"
                               "Free for non-commercial use; see LICENSE for the terms.\n\n"
                               "An independent implementation inspired by Windows 7/10 Paint. "
                               "Microsoft and Windows are trademarks of Microsoft Corporation.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (unsaved_dialog) {
        ImGui::OpenPopup("Save changes?");
        unsaved_dialog = false;
    }
    if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        GuiScope popup_scope(GuiEnd::Popup);
        ImGui::TextUnformatted("Do you want to save your picture before continuing?");
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
            deferred_after_save = true;
            execute(Command::Save);
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
            execute(deferred_command);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (!error.empty()) {
        ImGui::OpenPopup("Plan Paint message");
    }
    if (ImGui::BeginPopupModal("Plan Paint message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        GuiScope popup_scope(GuiEnd::Popup);
        ImGui::PushTextWrapPos(440);
        ImGui::TextUnformatted(error.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            error.clear();
            ImGui::CloseCurrentPopup();
        }
    }
}
void Application::frame() {
    try {
        file_results();
        poll_warp();
        keyboard();
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Plan Paint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        GuiScope root_scope(GuiEnd::Window, 1, 0);
        ribbon(io.DisplaySize.x);
        float sidebar_width = show_help ? std::min(370.0f, io.DisplaySize.x * 0.38f) : 0.0f;
        float status_height = show_status ? 27.0f : 0.0f;
        float ribbon_height = atlas_tab ? 154.0f : 131.0f;
        canvas(io.DisplaySize.x - sidebar_width, io.DisplaySize.y - ribbon_height - status_height);
        if (show_help) {
            help(io.DisplaySize.x - sidebar_width, ribbon_height, sidebar_width,
                 io.DisplaySize.y - ribbon_height - status_height);
        }
        if (show_status) {
            ImDrawList& status_draw = *ImGui::GetWindowDrawList();
            status_draw.PushClipRect({8, io.DisplaySize.y - 27},
                                     {std::max(380.0f, io.DisplaySize.x - 445), io.DisplaySize.y}, true);
            ImGui::SetCursorPos(ImVec2(8, io.DisplaySize.y - 22));
            ImGui::TextUnformatted(status.c_str());
            status_draw.PopClipRect();
            ImGui::SetCursorPos(ImVec2(std::max(390.0f, io.DisplaySize.x - 420), io.DisplaySize.y - 22));
            ImGui::Text("%d x %d px", document.image.width, document.image.height);
            ImGui::SetCursorPos({io.DisplaySize.x - 265, io.DisplaySize.y - 22});
            ImGui::Text("%.0f%%", zoom * 100);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 1});
            ImGui::SetCursorPos({io.DisplaySize.x - 203, io.DisplaySize.y - 24});
            if (ImGui::Button("-##Zoom out", {22, 20})) {
                zoom = std::max(0.125f, zoom / 2);
            }
            ImGui::SetItemTooltip("Zoom out");
            ImGui::SetCursorPos({io.DisplaySize.x - 178, io.DisplaySize.y - 24});
            ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("##Zoom", &zoom, 0.125f, 16.0f, "", ImGuiSliderFlags_Logarithmic);
            ImGui::SetCursorPos({io.DisplaySize.x - 33, io.DisplaySize.y - 24});
            if (ImGui::Button("+##Zoom in", {22, 20})) {
                zoom = std::min(16.0f, zoom * 2);
            }
            ImGui::SetItemTooltip("Zoom in");
            ImGui::PopStyleVar();
        }
        dialogs();

        std::string title = document.filename.empty() ? "Untitled" : display_filename(document.filename);
        if ((document.dirty() || (text_active && text_buffer[0]))) {
            title += " *";
        }
        title += " - Plan Paint";
        if (title != last_window_title) {
            SDL_SetWindowTitle(window, title.c_str());
            last_window_title = title;
        }
    } catch (const std::exception& exception) {
        report(exception);
    }
}
} // namespace paint

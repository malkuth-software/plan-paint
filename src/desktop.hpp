#pragma once
#include "canvas_backing.hpp"
#include "image.hpp"
struct SDL_Window;
#include <array>
namespace paint {
enum class WallpaperLayout { Fill, Tile, Center };
struct RecentFiles {
    std::string storage_path;
    std::vector<std::string> paths;
    void load();
    void remember(const std::string& path);
};
struct EditorSettings {
    std::string storage_path;
    std::string language = "system";
    bool canvas_controls = false;
    double scroll_distance = 6;
    int interface_hue = 220;
    bool recovery_enabled = true;
    int recovery_seconds = 60;
    CanvasBacking canvas_backing = CanvasBacking::PaleFelt;
    bool solid_transparency = false;
    bool drag_shapes = false;
    bool rotate_view = false;
    Color transparency_color{255, 128, 192, 255};
    void load();
    void save() const;
};
struct CustomColors {
    std::string storage_path;
    std::array<Color, 29> colors;
    std::uint32_t occupied = 0;
    CustomColors();
    void load();
    void store(int slot, Color color);
};
Color office_color(int index);
Color themed_color(int index);
const char* theme_name(int column);
std::string preference_directory();
std::string desktop_export(const Image& image, const char* purpose);
Image wallpaper_image(const Image& image, int width, int height, WallpaperLayout layout);
void set_wallpaper(const std::string& path);
// Windows returns an acquired file; other desktops open their acquisition app.
bool acquire_picture(std::string& acquired_path);
} // namespace paint

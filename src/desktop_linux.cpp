#include "desktop.hpp"
#include <gtk/gtk.h>
#include <stdexcept>
namespace paint {
static bool launch_desktop_program(const char* const* arguments, bool wait) {
    gchar* executable = g_find_program_in_path(arguments[0]);
    if (!executable) {
        return false;
    }
    g_free(executable);
    SDL_Process* process = SDL_CreateProcess(arguments, false);
    if (!process) {
        return false;
    }
    int exit_code = 0;
    bool success = !wait || (SDL_WaitProcess(process, true, &exit_code) && exit_code == 0);
    SDL_DestroyProcess(process);
    return success;
}

bool acquire_picture(std::string&) {
    const char* simple_scan[] = {"simple-scan", nullptr};
    const char* xsane[] = {"xsane", nullptr};
    if (!launch_desktop_program(simple_scan, false) && !launch_desktop_program(xsane, false)) {
        throw std::runtime_error(
            "Install Document Scanner (simple-scan) or XSane, then try this command again.");
    }
    return false;
}
void set_wallpaper(const std::string& path) {
    const char* desktop = SDL_getenv("XDG_CURRENT_DESKTOP");
    std::string name = desktop ? desktop : "";
    if (name.find("KDE") != std::string::npos) {
        const char* arguments[] = {"plasma-apply-wallpaperimage", path.c_str(), nullptr};
        if (launch_desktop_program(arguments, true)) {
            return;
        }
        throw std::runtime_error("KDE could not apply the wallpaper. Check plasma-apply-wallpaperimage.");
    }
    if (name.find("GNOME") == std::string::npos && name.find("Unity") == std::string::npos &&
        name.find("Cinnamon") == std::string::npos && name.find("Budgie") == std::string::npos) {
        throw std::runtime_error("Wallpaper integration supports GNOME, Cinnamon, Budgie and KDE. Use this "
                                 "desktop's background settings with a saved picture.");
    }
    const char* schema_name = name.find("Cinnamon") != std::string::npos ? "org.cinnamon.desktop.background"
                                                                         : "org.gnome.desktop.background";
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    GSettingsSchema* schema = source ? g_settings_schema_source_lookup(source, schema_name, TRUE) : nullptr;
    if (!schema) {
        throw std::runtime_error("This desktop has no supported wallpaper service. Use its background "
                                 "settings with the saved picture.");
    }
    GSettings* settings = g_settings_new_full(schema, nullptr, nullptr);
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    bool success = uri && g_settings_set_string(settings, "picture-uri", uri);
    if (success && g_settings_schema_has_key(schema, "picture-uri-dark")) {
        success = g_settings_set_string(settings, "picture-uri-dark", uri);
    }
    if (success && g_settings_schema_has_key(schema, "picture-options")) {
        success = g_settings_set_string(settings, "picture-options", "stretched");
    }
    g_settings_sync();
    g_free(uri);
    g_object_unref(settings);
    g_settings_schema_unref(schema);
    if (!success) {
        throw std::runtime_error("The desktop rejected the wallpaper change.");
    }
}
} // namespace paint

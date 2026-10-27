#include "forms/editor.hpp"
#include "paths.hpp"
#include "localization.hpp"
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
int main(int argc, char** argv) {
    try {
        paint::EditorSettings startup_settings;
        try {
            startup_settings.storage_path = paint::path_to_utf8(paint::path_from_utf8(paint::preference_directory()) / "settings.txt");
            startup_settings.load();
        } catch (const std::exception&) {
            // Preference storage failure must not prevent starting in English.
            startup_settings.language = "en-us";
        }
        std::string initial_path;
        std::string session_language = startup_settings.language;
        bool session_canvas_controls = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument.starts_with("--language=")) { session_language = argument.substr(11); }
            else if (argument == "--canvas-controls") { session_canvas_controls = true; }
            else if (initial_path.empty()) { initial_path = argument; }
        }
        paint::initialize_language(paint::application_language_directory(), session_language);
        const std::shared_ptr<paint::forms::Editor> editor =
            gui_forms::make_control<paint::forms::Editor>(gui_forms::StableId("paint.editor"));
        try {
            std::filesystem::path preferences = paint::path_from_utf8(paint::preference_directory());
            (*editor).custom_colors.storage_path = paint::path_to_utf8(preferences / "custom-colors.bin");
            (*editor).custom_colors.load();
            (*editor).settings.storage_path = paint::path_to_utf8(preferences / "settings.txt");
            (*editor).settings.load();
            (*editor).recent.storage_path = paint::path_to_utf8(preferences / "recent.bin");
            (*editor).recent.load();
            (*editor).asset_bookmark_path = paint::path_to_utf8(preferences / "asset-folders.txt");
        } catch (const std::exception&) {
            (*editor).custom_colors.storage_path.clear();
        }
        if (session_canvas_controls) { (*editor).settings.canvas_controls = true; }
        std::unique_ptr<gui_forms::Window> window =
            std::make_unique<gui_forms::Window>(editor, gui_forms::Size{1280, 820});
        gui_forms::ApplicationWindowOptions options;
        options.title = "Plan Paint";
        options.initial_size = {1280, 820};
        options.minimum_size = {800, 520};
        options.print_metrics_on_close = false;
        options.ready = std::bind(&paint::forms::Editor::ready, editor, std::placeholders::_1,
                                  std::placeholders::_2, initial_path);
        options.closing = std::bind(&paint::forms::Editor::closing, editor, std::placeholders::_1);
        gui_forms::ApplicationResult result =
            gui_forms::Application::run(std::move(window), std::move(options));
        if (result.callback_exception) {
            std::rethrow_exception(result.callback_exception);
        }
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "Plan Paint: " << exception.what() << '\n';
        return 1;
    }
}

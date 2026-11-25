#include "forms/model_tools.hpp"
#include "forms/editor.hpp"
#include "forms/identity.hpp"
#include "command_line.hpp"
#include "paths.hpp"
#include "localization.hpp"
#include "plan_paint_theme.hpp"
#include <gui_forms/application.hpp>
#include <gui_forms/automation/marks/automation_marks.hpp>
#include <clocale>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string_view>
namespace {
void report_theme_findings(const std::vector<gui_forms::ThemeCompileDiagnostic>& findings) {
    for (const gui_forms::ThemeCompileDiagnostic& finding : findings) {
        std::cerr << "Plan Paint theme: " << finding.code << ' ' << finding.file << ':' << finding.line << ':'
                  << finding.column << ' ' << finding.rule << ' ' << finding.property << ' ' << finding.detail
                  << '\n';
    }
}
// The application comes first (GUI.Forms' Application-first lifecycle): it
// compiles the GUI.Forms base and Plan Paint's package once, and the window
// made after it joins it. A refused package must not stop the program: its
// findings go to standard error and the base theme alone is used.
gui_forms::ApplicationCreateResult create_application() {
    gui_forms::ApplicationConfig config{};
    config.theme.base = gui_forms::ThemeBase::gui_forms;
    config.theme.packages.push_back(paint::plan_paint_theme());
    gui_forms::ApplicationCreateResult created = gui_forms::Application::create(std::move(config));
    report_theme_findings(created.findings);
    if (created || created.error != gui_forms::ApplicationCreateError::theme_refused) {
        return created;
    }
    gui_forms::ApplicationConfig fallback{};
    fallback.theme.base = gui_forms::ThemeBase::gui_forms;
    return gui_forms::Application::create(std::move(fallback));
}
} // namespace
// Whether GUI.Forms serves this launch over MCP: an MCP option, or
// GUI_FORMS_MCP set to 1 or shared. Read before take_mcp_arguments removes
// the options from argv.
static bool served_by_mcp(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--mcp" || argument == "--mcp-shared" || argument == "--mcp-folder" ||
            argument == "--mcp-script" || argument == "--model-directed" || argument == "--mcp-appearance-review") {
            return true;
        }
    }
    const char* variable = std::getenv("GUI_FORMS_MCP");
    if (variable == nullptr) { return false; }
    const std::string_view value(variable);
    return value == "1" || value == "shared" || value == "folder";
}

int main(int argc, char** argv) {
#ifdef __linux__
    // The process locale is the program's: GUI.Forms' Linux host no longer
    // sets it (D15), and its X input method follows LC_CTYPE, so input methods
    // and compose sequences need the environment's character type. Only
    // LC_CTYPE: numbers in documents and settings stay locale-independent.
    // std-rules: allow(locale-change) the program, not the library, chooses its locale
    std::setlocale(LC_CTYPE, "");
#endif
    try {
        paint::EditorSettings startup_settings;
        try {
            startup_settings.storage_path = paint::path_to_utf8(paint::path_from_utf8(paint::preference_directory()) / "settings.txt");
            startup_settings.load();
        } catch (const std::exception&) {
            // Preference storage failure must not prevent starting in English.
            startup_settings.language = "en-us";
        }
        // GUI.Forms reads its MCP launch options itself; they never reach
        // Plan Paint's parser.
        const bool served = served_by_mcp(argc, argv);
        gui_forms::automation::take_mcp_arguments(argc, argv);
        std::vector<std::string> arguments = paint::utf8_arguments(argc, argv);
        // Windows' arguments come from the wide command line, which still
        // holds them.
        for (std::size_t index = 0; index < arguments.size();) {
            const std::string& argument = arguments[index];
            if (argument == "--mcp" || argument == "--mcp-shared" || argument == "--mcp-folder" ||
                argument == "--model-directed" || argument == "--mcp-appearance-review" || argument == "--mcp-socket" ||
                argument == "--help") {
                arguments.erase(arguments.begin() + static_cast<std::ptrdiff_t>(index));
            } else if (argument == "--mcp-script") {
                const std::size_t taken = index + 1 < arguments.size() ? 2 : 1;
                arguments.erase(arguments.begin() + static_cast<std::ptrdiff_t>(index),
                                arguments.begin() + static_cast<std::ptrdiff_t>(index + taken));
            } else {
                ++index;
            }
        }
        const paint::CommandLine command_line = paint::parse_command_line(arguments);
        for (const std::string& ignored : command_line.ignored_options) {
            std::cerr << "Plan Paint: ignoring unknown option " << ignored << '\n';
        }
        const std::string initial_path = command_line.initial_path;
        const std::string session_language =
            command_line.language.empty() ? startup_settings.language : command_line.language;
        const bool session_canvas_controls = command_line.canvas_controls;
        paint::initialize_language(paint::application_language_directory(), session_language);
        gui_forms::ApplicationCreateResult created = create_application();
        if (!created) {
            std::cerr << "Plan Paint: the GUI.Forms application could not be created.\n";
            return 1;
        }
        gui_forms::Application& application = *created.application;
        // The identity GUI.Forms' business card prints, before any window.
        paint::forms::install_program_identity();
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
        (*editor).served_launch = served;
        // The editor's tools and marks for a model (--mcp): kept while the
        // editor runs.
        const paint::forms::EditorModelTools model_tools(*editor);
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
        gui_forms::ApplicationResult result = application.run(std::move(window), std::move(options));
        if (result.callback_exception) {
            std::rethrow_exception(result.callback_exception);
        }
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "Plan Paint: " << exception.what() << '\n';
        return 1;
    }
}

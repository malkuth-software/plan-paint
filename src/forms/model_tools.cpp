#include "forms/model_tools.hpp"

#include "forms/editor.hpp"

#include <gui_forms/automation/marks/automation_marks.hpp>
#include <gui_forms/commands/command/command.hpp>
#include <stxlib/json.hpp>
#include <stxlib/text.hpp>

#include <array>
#include <cstdint>
#include <exception>
#include <string>

namespace gf = gui_forms;
namespace automation = gui_forms::automation;

namespace paint::forms {

namespace {

// Commands that discard work or reach outside the picture: a new picture
// or an opened one replaces the current one (its unsaved changes go behind
// a Save changes? question the user answers), reload and recover replace
// it from disk, save and save-as write files, the wallpaper commands change
// the desktop, and Exit closes the program.
constexpr std::array<std::string_view, 11> destructive_commands{
    "new", "open", "reload", "recover", "save", "save-as", "quit",
    "wallpaper-fill", "wallpaper-tile", "wallpaper-center", "crop"};

// Dialog openers whose names do not end in an ellipsis, and the tool that
// does the same without the dialog (empty for none).
struct DialogOpener final {
    const char* id;
    const char* tool;
};
constexpr std::array<DialogOpener, 4> dialog_openers{{
    {"resize", "plan_paint.resize_image"},
    {"ribbon-bar.resize", "plan_paint.resize_image"},
    {"properties", "plan_paint.resize_image"},
    {"backstage.properties", "plan_paint.resize_image"},
}};

constexpr const char* resize_schema =
    "{\"type\":\"object\",\"properties\":{"
    "\"width\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":32768,\"description\":\"New width in pixels.\"},"
    "\"height\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":32768,\"description\":\"New height in pixels.\"},"
    "\"scale\":{\"type\":\"boolean\",\"description\":\"true scales the picture (or the selection) to the size; "
    "false changes the canvas size, filling new area with the secondary colour. Default true.\"}},"
    "\"required\":[\"width\",\"height\"],\"additionalProperties\":false}";
constexpr const char* new_image_schema =
    "{\"type\":\"object\",\"properties\":{"
    "\"width\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":32768},"
    "\"height\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":32768}},"
    "\"additionalProperties\":false}";
constexpr const char* no_arguments_schema = "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";

void reply_text(automation::ModelToolReply& reply, const std::string& text) {
    static_cast<void>(reply.text(text.data(), text.size()));
}

// The call's arguments as one JSON object; false, failing the reply, for
// anything else.
bool read_arguments(const automation::ModelToolCall& call, stxlib::JsonDocument& document,
                    automation::ModelToolReply& reply) {
    const stxlib::OperationResult<stxlib::TextView> text =
        stxlib::validate_utf8(call.arguments_json, call.arguments_bytes);
    if (text.failed()) {
        reply.fail(automation::ModelToolError::invalid_arguments, "the arguments are not UTF-8", nullptr);
        return false;
    }
    stxlib::OperationResult<stxlib::JsonDocument> parsed = stxlib::JsonDocument::parse(*text.value());
    if (parsed.failed() || !(*parsed.value()).is((*parsed.value()).root(), stxlib::JsonKind::object)) {
        reply.fail(automation::ModelToolError::invalid_arguments, "the arguments are not one JSON object", nullptr);
        return false;
    }
    document = std::move(parsed).take_value();
    return true;
}

// The integer member `name` within 1..32768, into `value`; `fallback` when
// the member is absent and `required` is false.
bool dimension(const stxlib::JsonDocument& document, stxlib::TextView name, bool required, int fallback, int& value,
               automation::ModelToolReply& reply) {
    const stxlib::Optional<stxlib::i64> read = document.member_integer(document.root(), name);
    const std::string label(name.data(), name.count());
    if (read.absent()) {
        if (!required) {
            value = fallback;
            return true;
        }
        const std::string message = label + " is a whole number of pixels from 1 to 32768";
        reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(), nullptr);
        return false;
    }
    const stxlib::i64 pixels = *read.value();
    if (pixels < 1 || pixels > 32768) {
        const std::string message = label + " is from 1 to 32768 pixels";
        reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(), nullptr);
        return false;
    }
    value = static_cast<int>(pixels);
    return true;
}

std::string size_text(const Document& document) {
    const std::string text = std::to_string(document.image.width) + " by " + std::to_string(document.image.height);
    return text;
}

} // namespace

void mark_command(gf::Command& command, std::string_view id) {
    std::string_view bare = id;
    constexpr std::string_view row_prefix = "choice-";
    if (bare.starts_with(row_prefix)) bare.remove_prefix(row_prefix.size());
    for (const std::string_view listed : destructive_commands) {
        if (listed == bare) {
            command.set_destructive(true);
            return;
        }
    }
}

EditorModelTools::EditorModelTools(Editor& editor) : editor_(editor) {
    for (const DialogOpener& opener : dialog_openers) {
        static_cast<void>(automation::set_opens_dialog(opener.id, true, opener.tool));
    }
    automation::ModelToolSpec info{};
    info.name = "plan_paint.image_info";
    info.title = "Picture";
    info.description = "The picture's size, file name, whether it has unsaved changes, and whether a selection is "
                       "active. Reads only.";
    info.input_schema = no_arguments_schema;
    info.access = automation::ModelToolAccess::read_only;
    static_cast<void>(automation::register_model_tool(info, &EditorModelTools::image_info, this));

    automation::ModelToolSpec resize{};
    resize.name = "plan_paint.resize_image";
    resize.title = "Resize";
    resize.description = "Resizes the picture, or the active selection, to a size in pixels, as the Resize dialog "
                         "and the Properties dialog's size do, without a dialog. Undo takes it back.";
    resize.input_schema = resize_schema;
    resize.access = automation::ModelToolAccess::acting;
    resize.settles = true;
    static_cast<void>(automation::register_model_tool(resize, &EditorModelTools::resize_image, this));

    automation::ModelToolSpec fresh{};
    fresh.name = "plan_paint.new_image";
    fresh.title = "New picture";
    fresh.description = "Replaces the picture with a new blank one (default 960 by 640), as New does. Refused while "
                        "the picture has unsaved changes: the user decides whether to save them.";
    fresh.input_schema = new_image_schema;
    fresh.access = automation::ModelToolAccess::acting;
    fresh.destructive = true;
    fresh.settles = true;
    static_cast<void>(automation::register_model_tool(fresh, &EditorModelTools::new_image, this));
}

EditorModelTools::~EditorModelTools() {
    static_cast<void>(automation::unregister_model_tool("plan_paint.image_info"));
    static_cast<void>(automation::unregister_model_tool("plan_paint.resize_image"));
    static_cast<void>(automation::unregister_model_tool("plan_paint.new_image"));
    for (const DialogOpener& opener : dialog_openers) {
        static_cast<void>(automation::set_opens_dialog(opener.id, false, nullptr));
    }
}

void EditorModelTools::image_info(void* context, const automation::ModelToolCall&, automation::ModelToolReply& reply) {
    const EditorModelTools& tools = *static_cast<const EditorModelTools*>(context);
    const Document& document = tools.editor_.document;
    std::string text = "picture: " + size_text(document) + " pixels\n";
    text += "file: " + (document.filename.empty() ? std::string("untitled") : document.filename) + "\n";
    text += document.dirty() ? "unsaved changes: yes\n" : "unsaved changes: no\n";
    text += document.selection.active ? "selection: active\n" : "selection: none\n";
    reply_text(reply, text);
}

void EditorModelTools::resize_image(void* context, const automation::ModelToolCall& call,
                                    automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    int width = 0;
    int height = 0;
    if (!dimension(arguments, "width", true, 0, width, reply)) return;
    if (!dimension(arguments, "height", true, 0, height, reply)) return;
    const stxlib::Optional<bool> scale_member = arguments.member_boolean(arguments.root(), "scale");
    const bool scale = scale_member.absent() || *scale_member.value();
    Editor& editor = tools.editor_;
    try {
        editor.document.resize(width, height, scale);
    } catch (const std::exception& refused) {
        // The document's own refusals (an atlas sheet, an icon over 256
        // pixels) say what to do.
        reply.fail(automation::ModelToolError::unsupported_action, refused.what(), nullptr);
        return;
    }
    editor.refresh();
    const std::string what = editor.document.selection.active ? "selection" : "picture";
    reply_text(reply, "resized the " + what + " to " + std::to_string(width) + " by " + std::to_string(height) +
                          (scale ? " (scaled)\n" : " (canvas)\n"));
}

void EditorModelTools::new_image(void* context, const automation::ModelToolCall& call,
                                 automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    int width = 0;
    int height = 0;
    if (!dimension(arguments, "width", false, 960, width, reply)) return;
    if (!dimension(arguments, "height", false, 640, height, reply)) return;
    Editor& editor = tools.editor_;
    if (editor.document.dirty()) {
        reply.fail(automation::ModelToolError::needs_user,
                   "the picture has unsaved changes",
                   "ask the user to save or discard them; a model never discards them");
        return;
    }
    editor.document.new_image(width, height);
    editor.refresh();
    reply_text(reply, "a new " + std::to_string(width) + " by " + std::to_string(height) + " picture\n");
}

} // namespace paint::forms

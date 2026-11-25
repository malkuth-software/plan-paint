#include "forms/model_tools.hpp"

#include "forms/editor.hpp"
#include "codecs.hpp"
#include "paths.hpp"
#include "raster.hpp"

#include <gui_forms/automation/marks/automation_marks.hpp>
#include <gui_forms/commands/command/command.hpp>
#include <stxlib/json.hpp>
#include <stxlib/text.hpp>

#include <array>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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
constexpr std::array<DialogOpener, 5> dialog_openers{{
    {"save-as", "plan_paint_save"},
    {"resize", "plan_paint_resize_image"},
    {"ribbon-bar.resize", "plan_paint_resize_image"},
    {"properties", "plan_paint_resize_image"},
    {"backstage.properties", "plan_paint_resize_image"},
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
constexpr const char* save_schema =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Save as this file (.png, .bmp, .jpg, .tif, .tga); "
    "without it, saves to the picture's own file.\"},"
    "\"overwrite\":{\"type\":\"boolean\",\"description\":\"Replace a file already at path.\"}},"
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

// The app note a model reads with the server's instructions and as the
// gui://app/note resource.
constexpr const char* app_note =
    "Plan Paint is a raster editor. Picture coordinates are pixels from the picture's top-left corner; "
    "plan_paint_to_window converts them to window points for pointer, and plan_paint_image_info gives the "
    "picture's corners in the window.\n"
    "- Draw with plan_paint_draw (shapes) and plan_paint_stroke (lines through points), in picture pixels and "
    "#RRGGBB colours; each is one undo step. plan_paint_set_colour sets the Primary or Alt colour; the palette's "
    "swatches name their #RRGGBB.\n"
    "- With the pointer, the Shape tool's default gesture is click to place: after the press the shape follows the "
    "pointer and floats until the next click places it; Escape or a right-click cancels it. Bezier and arc curves "
    "and paths stay editable until Escape (command_release) or another tool commits them. Replies name anything "
    "floating as pending.\n"
    "- With the pointer, Edge and Fill paint with the Primary colour unless the colour group gives Fill the Alt "
    "colour.\n"
    "- Resize and Properties open dialogs: use plan_paint_resize_image instead. New, Open, Save and Exit are "
    "destructive and ask the user once per launch.\n"
    "- To finish: save with plan_paint_save (a path for a new picture), then call disconnect. A disconnect with "
    "unsaved changes closes nothing and tells you what to save; it is never shown to the user.";

constexpr const char* draw_schema_head =
    "{\"type\":\"object\",\"properties\":{"
    "\"shape\":{\"type\":\"string\",\"description\":\"The shape, as on the ribbon's Shapes gallery.\",\"enum\":[";
constexpr const char* draw_schema_tail =
    "]},"
    "\"from\":{\"type\":\"object\",\"description\":\"One corner of the shape's box (a line's start), in picture "
    "pixels.\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],"
    "\"additionalProperties\":false},"
    "\"to\":{\"type\":\"object\",\"description\":\"The opposite corner (a line's end), in picture pixels.\","
    "\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],"
    "\"additionalProperties\":false},"
    "\"colour\":{\"type\":\"string\",\"description\":\"The outline's colour, #RRGGBB or #RRGGBBAA; default the "
    "Primary colour. \\\"none\\\" draws no outline.\"},"
    "\"fill\":{\"type\":\"string\",\"description\":\"The fill's colour, #RRGGBB or #RRGGBBAA; default no fill.\"},"
    "\"size\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100,\"description\":\"The outline's width in pixels; "
    "default 3.\"}},"
    "\"required\":[\"shape\",\"from\",\"to\"],\"additionalProperties\":false}";
constexpr const char* stroke_schema =
    "{\"type\":\"object\",\"properties\":{"
    "\"points\":{\"type\":\"array\",\"minItems\":2,\"maxItems\":4096,\"description\":\"The line's points in picture "
    "pixels, in order.\",\"items\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":"
    "\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false}},"
    "\"colour\":{\"type\":\"string\",\"description\":\"#RRGGBB or #RRGGBBAA; default the Primary colour.\"},"
    "\"size\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100,\"description\":\"The line's width in pixels; "
    "default 3.\"}},"
    "\"required\":[\"points\"],\"additionalProperties\":false}";
constexpr const char* set_colour_schema =
    "{\"type\":\"object\",\"properties\":{"
    "\"hex\":{\"type\":\"string\",\"description\":\"#RRGGBB or #RRGGBBAA.\"},"
    "\"which\":{\"type\":\"string\",\"enum\":[\"primary\",\"alt\"],\"description\":\"Default primary.\"}},"
    "\"required\":[\"hex\"],\"additionalProperties\":false}";
constexpr const char* to_window_schema =
    "{\"type\":\"object\",\"properties\":{"
    "\"points\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":256,\"description\":\"Points in picture pixels.\","
    "\"items\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},"
    "\"required\":[\"x\",\"y\"],\"additionalProperties\":false}}},"
    "\"required\":[\"points\"],\"additionalProperties\":false}";

// A shape's name for a model: the gallery's name in lower case, words
// joined by underscores ("rounded_rectangle", "five-point_star").
std::string shape_tool_name(int shape) {
    std::string name{};
    for (const char* letter = shape_names[shape]; *letter != '\0'; ++letter) {
        const char c = *letter;
        if (c == ' ') {
            name += '_';
        } else if (c >= 'A' && c <= 'Z') {
            name += static_cast<char>(c - 'A' + 'a');
        } else {
            name += c;
        }
    }
    return name;
}

// The shapes draw takes: two corners make each of them. Bezier, polygon
// and arc need more points than two; they are drawn with the pointer.
bool two_point_shape(int shape) {
    const Shape kind = static_cast<Shape>(shape);
    const bool simple = kind != Shape::Bezier && kind != Shape::Polygon && kind != Shape::Arc;
    return simple;
}

std::string hex_text(Color color) {
    std::array<char, 12> text{};
    if (color.a == 255) {
        static_cast<void>(std::snprintf(text.data(), text.size(), "#%02X%02X%02X", color.r, color.g, color.b));
    } else {
        static_cast<void>(
            std::snprintf(text.data(), text.size(), "#%02X%02X%02X%02X", color.r, color.g, color.b, color.a));
    }
    return std::string(text.data());
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "#RRGGBB" or "#RRGGBBAA" into `color`; false for anything else.
bool parse_hex(stxlib::TextView text, Color& color) {
    const std::size_t count = text.count();
    if ((count != 7U && count != 9U) || text.data()[0] != '#') return false;
    std::array<int, 4> channels{0, 0, 0, 255};
    for (std::size_t channel = 0U; channel < (count - 1U) / 2U; ++channel) {
        const int high = hex_digit(text.data()[1U + channel * 2U]);
        const int low = hex_digit(text.data()[2U + channel * 2U]);
        if (high < 0 || low < 0) return false;
        channels[channel] = high * 16 + low;
    }
    color = Color{static_cast<std::uint8_t>(channels[0]), static_cast<std::uint8_t>(channels[1]),
                  static_cast<std::uint8_t>(channels[2]), static_cast<std::uint8_t>(channels[3])};
    return true;
}

// The colour member `name` into `color`; `fallback` when absent. Fails the
// reply for a value that is no colour.
bool colour_member(const stxlib::JsonDocument& document, stxlib::TextView name, Color fallback, Color& color,
                   automation::ModelToolReply& reply) {
    const stxlib::Optional<stxlib::TextView> text = document.member_text(document.root(), name);
    if (text.absent()) {
        color = fallback;
        return true;
    }
    if (parse_hex(*text.value(), color)) return true;
    const std::string message = std::string(name.data(), name.count()) + " is a colour: #RRGGBB or #RRGGBBAA";
    reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(), nullptr);
    return false;
}

// A point object {x, y} at `value` into `point`.
bool read_point(const stxlib::JsonDocument& document, stxlib::JsonRef value, Point& point) {
    const stxlib::Optional<stxlib::JsonRef> x = document.member(value, "x");
    const stxlib::Optional<stxlib::JsonRef> y = document.member(value, "y");
    if (x.absent() || y.absent()) return false;
    const stxlib::Optional<double> px = document.number(*x.value());
    const stxlib::Optional<double> py = document.number(*y.value());
    if (px.absent() || py.absent()) return false;
    if (*px.value() < -65536.0 || *px.value() > 65536.0 || *py.value() < -65536.0 || *py.value() > 65536.0) {
        return false;
    }
    point = Point{*px.value(), *py.value()};
    return true;
}

// The array member `name` of points into `points`, `minimum` or more.
bool point_list(const stxlib::JsonDocument& document, stxlib::TextView name, std::size_t minimum,
                std::size_t maximum, std::vector<Point>& points, automation::ModelToolReply& reply) {
    const std::string label(name.data(), name.count());
    const stxlib::Optional<stxlib::JsonRef> list = document.member(document.root(), name);
    if (list.absent() || !document.is(*list.value(), stxlib::JsonKind::array) ||
        document.child_count(*list.value()) < minimum || document.child_count(*list.value()) > maximum) {
        const std::string message = label + " is an array of " + std::to_string(minimum) + " to " +
                                    std::to_string(maximum) + " points {x, y} in picture pixels";
        reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(), nullptr);
        return false;
    }
    stxlib::Optional<stxlib::JsonRef> item = document.first_child(*list.value());
    while (const stxlib::JsonRef* const value = item.value()) {
        Point point{};
        if (!read_point(document, *value, point)) {
            const std::string message = "each of " + label + " is {x, y}, numbers of picture pixels";
            reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(), nullptr);
            return false;
        }
        points.push_back(point);
        item = document.next_sibling(*value);
    }
    return true;
}

// The point member `name` into `point`.
bool point_member(const stxlib::JsonDocument& document, stxlib::TextView name, Point& point,
                  automation::ModelToolReply& reply) {
    const stxlib::Optional<stxlib::JsonRef> value = document.member(document.root(), name);
    if (value.absent() || !read_point(document, *value.value(), point)) {
        const std::string message = std::string(name.data(), name.count()) + " is {x, y} in picture pixels";
        reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(), nullptr);
        return false;
    }
    return true;
}

// The width member "size" within 1..100; 3 when absent.
bool width_member(const stxlib::JsonDocument& document, int& size, automation::ModelToolReply& reply) {
    const stxlib::Optional<stxlib::i64> read = document.member_integer(document.root(), "size");
    if (read.absent()) {
        size = 3;
        return true;
    }
    if (*read.value() < 1 || *read.value() > 100) {
        reply.fail(automation::ModelToolError::invalid_arguments, "size is from 1 to 100 pixels", nullptr);
        return false;
    }
    size = static_cast<int>(*read.value());
    return true;
}

// A plain solid ink of `color` and `size`: what the model asked for, not
// the ribbon's current brush, pattern or material.
Ink plain_ink(Color color, int size) {
    Ink ink{};
    ink.primary = color;
    ink.size = size;
    ink.pattern = Pattern::Solid;
    ink.brush = Brush::Round;
    return ink;
}

// What floats in the editor now, or empty: the text a pending state names,
// and how it is settled.
void floating_state(const Editor& editor, std::string& what, std::string& how) {
    const Document& document = editor.document;
    if (editor.placing_shape()) {
        what = "a " + shape_tool_name(static_cast<int>(document.shape)) +
               " follows the pointer and is not yet placed";
        how = "click on the canvas to place it; Escape or a right-click cancels it";
    } else if (document.curve.base) {
        what = "a curve is still editable by its handles";
        how = "Escape (command_release) or another tool commits it";
    } else if (!document.path.nodes.empty() && document.path.base) {
        what = "a path is still editable by its nodes";
        how = "Escape (command_release) or another tool commits it";
    } else if (document.selection.active && !document.selection.on_canvas) {
        what = "a pasted selection floats over the picture";
        how = "Escape (command_release) places it; Delete removes it";
    }
}

// Window point of picture point `point` (window coordinates, as pointer
// takes them).
gf::Point window_point(Editor& editor, Point point) {
    PaintCanvas& canvas = editor.canvas();
    const gf::Rect bounds = canvas.absolute_bounds();
    const gf::Point client = canvas.bitmap_point_to_client(gui_drawing::PointF{point.x, point.y});
    return gf::Point{bounds.x + client.x, bounds.y + client.y};
}

std::string point_text(gf::Point point) {
    std::array<char, 48> text{};
    static_cast<void>(std::snprintf(text.data(), text.size(), "%.1f,%.1f", point.x, point.y));
    return std::string(text.data());
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

void note_model_pending(const Editor& editor) {
    // The last state named, so an unchanged editor costs a comparison.
    static std::string named{};
    std::string what{};
    std::string how{};
    floating_state(editor, what, how);
    if (what == named) return;
    named = what;
    if (what.empty()) {
        automation::clear_model_pending_state("plan_paint_floating");
    } else {
        static_cast<void>(automation::set_model_pending_state("plan_paint_floating", what.c_str(), how.c_str()));
    }
}

PendingStateNote::~PendingStateNote() {
    note_model_pending(editor_);
}

EditorModelTools::EditorModelTools(Editor& editor) : editor_(editor) {
    for (const DialogOpener& opener : dialog_openers) {
        static_cast<void>(automation::set_opens_dialog(opener.id, true, opener.tool));
    }
    automation::ModelToolSpec info{};
    info.name = "plan_paint_image_info";
    info.title = "Picture";
    info.description = "The picture's size, file name, whether it has unsaved changes, and whether a selection is "
                       "active. Reads only.";
    info.input_schema = no_arguments_schema;
    info.access = automation::ModelToolAccess::read_only;
    static_cast<void>(automation::register_model_tool(info, &EditorModelTools::image_info, this));

    automation::ModelToolSpec resize{};
    resize.name = "plan_paint_resize_image";
    resize.title = "Resize";
    resize.description = "Resizes the picture, or the active selection, to a size in pixels, as the Resize dialog "
                         "and the Properties dialog's size do, without a dialog. Undo takes it back.";
    resize.input_schema = resize_schema;
    resize.access = automation::ModelToolAccess::acting;
    resize.settles = true;
    static_cast<void>(automation::register_model_tool(resize, &EditorModelTools::resize_image, this));

    automation::ModelToolSpec fresh{};
    fresh.name = "plan_paint_new_image";
    fresh.title = "New picture";
    fresh.description = "Replaces the picture with a new blank one (default 960 by 640), as New does. Refused while "
                        "the picture has unsaved changes (the user decides whether to save them), so it never "
                        "discards work.";
    fresh.input_schema = new_image_schema;
    fresh.access = automation::ModelToolAccess::acting;
    // Not destructive: it refuses while anything is unsaved, so it never
    // discards work (round 3: the per-launch question held the first
    // picture back for nothing).
    fresh.settles = true;
    static_cast<void>(automation::register_model_tool(fresh, &EditorModelTools::new_image, this));

    automation::ModelToolSpec saving{};
    saving.name = "plan_paint_save";
    saving.title = "Save";
    saving.description = "Saves the picture without a dialog: to its own file, or with path as a new file (Save "
                         "As). Save first, then close: the close then needs no question.";
    saving.input_schema = save_schema;
    saving.access = automation::ModelToolAccess::acting;
    saving.settles = true;
    static_cast<void>(automation::register_model_tool(saving, &EditorModelTools::save, this));

    draw_schema_ = draw_schema_head;
    bool first = true;
    for (int shape = 0; shape < shape_count; ++shape) {
        if (!two_point_shape(shape)) continue;
        if (!first) draw_schema_ += ',';
        draw_schema_ += '"' + shape_tool_name(shape) + '"';
        first = false;
    }
    draw_schema_ += draw_schema_tail;
    automation::ModelToolSpec shape{};
    shape.name = "plan_paint_draw";
    shape.title = "Draw a shape";
    shape.description = "Draws a shape into the picture between two corners in picture pixels, outlined and "
                        "optionally filled in #RRGGBB colours, committed at once (one undo step; nothing floats). "
                        "Inside an active selection only.";
    shape.input_schema = draw_schema_.c_str();
    shape.access = automation::ModelToolAccess::acting;
    shape.settles = true;
    static_cast<void>(automation::register_model_tool(shape, &EditorModelTools::draw, this));

    automation::ModelToolSpec line{};
    line.name = "plan_paint_stroke";
    line.title = "Draw a line";
    line.description = "Draws a line through points in picture pixels with a round brush, committed at once "
                       "(one undo step).";
    line.input_schema = stroke_schema;
    line.access = automation::ModelToolAccess::acting;
    line.settles = true;
    static_cast<void>(automation::register_model_tool(line, &EditorModelTools::stroke, this));

    automation::ModelToolSpec colour{};
    colour.name = "plan_paint_set_colour";
    colour.title = "Set a colour";
    colour.description = "Sets the Primary (default) or Alt colour to #RRGGBB or #RRGGBBAA, as the palette does.";
    colour.input_schema = set_colour_schema;
    colour.access = automation::ModelToolAccess::acting;
    colour.settles = true;
    static_cast<void>(automation::register_model_tool(colour, &EditorModelTools::set_colour, this));

    automation::ModelToolSpec convert{};
    convert.name = "plan_paint_to_window";
    convert.title = "Picture to window";
    convert.description = "Converts points in picture pixels to window points, for pointer's position and path "
                          "(the view's zoom, scroll and rotation applied). Reads only.";
    convert.input_schema = to_window_schema;
    convert.access = automation::ModelToolAccess::read_only;
    static_cast<void>(automation::register_model_tool(convert, &EditorModelTools::to_window, this));

    static_cast<void>(automation::set_model_app_note(app_note));
}

EditorModelTools::~EditorModelTools() {
    static_cast<void>(automation::unregister_model_tool("plan_paint_image_info"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_resize_image"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_new_image"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_save"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_draw"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_stroke"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_set_colour"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_to_window"));
    automation::clear_model_pending_state("plan_paint_floating");
    static_cast<void>(automation::set_model_app_note(nullptr));
    for (const DialogOpener& opener : dialog_openers) {
        static_cast<void>(automation::set_opens_dialog(opener.id, false, nullptr));
    }
}

void EditorModelTools::image_info(void* context, const automation::ModelToolCall&, automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    Editor& editor = tools.editor_;
    const Document& document = editor.document;
    std::string what{};
    std::string how{};
    floating_state(editor, what, how);
    std::string text = "picture: " + size_text(document) + " pixels\n";
    text += "file: " + (document.filename.empty() ? std::string("untitled") : document.filename) + "\n";
    // A floating shape is an unsaved change too: placing it changes the
    // picture (round 2: "unsaved changes: no" with a rectangle floating).
    text += document.dirty() || !what.empty() ? "unsaved changes: yes\n" : "unsaved changes: no\n";
    if (!what.empty()) text += "pending: " + what + " (" + how + ")\n";
    text += document.selection.active ? "selection: active\n" : "selection: none\n";
    text += "colours: primary " + hex_text(document.ink.primary) + ", alt " + hex_text(document.ink.secondary) + "\n";
    const double width = static_cast<double>(document.image.width);
    const double height = static_cast<double>(document.image.height);
    text += "in the window: top-left " + point_text(window_point(editor, Point{0.0, 0.0})) + ", top-right " +
            point_text(window_point(editor, Point{width, 0.0})) + ", bottom-left " +
            point_text(window_point(editor, Point{0.0, height})) + " (plan_paint_to_window converts any point)\n";
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

void EditorModelTools::save(void* context, const automation::ModelToolCall& call, automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    Editor& editor = tools.editor_;
    std::string path = editor.document.filename;
    const stxlib::Optional<stxlib::TextView> named = arguments.member_text(arguments.root(), "path");
    const stxlib::Optional<bool> overwrite_member = arguments.member_boolean(arguments.root(), "overwrite");
    const bool overwrite = overwrite_member.present() && *overwrite_member.value();
    if (const stxlib::TextView* const given = named.value()) {
        const std::string wanted((*given).data(), (*given).count());
        std::error_code exists_error;
        const bool exists = std::filesystem::exists(path_from_utf8(wanted), exists_error);
        if (exists && !overwrite && wanted != editor.document.filename) {
            const std::string message = "a file is already at " + wanted;
            reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(),
                       "give another path, or overwrite: true to replace it");
            return;
        }
        path = wanted;
    } else if (path.empty()) {
        reply.fail(automation::ModelToolError::invalid_arguments, "the picture has no file yet",
                   "give path, the file to save as");
        return;
    }
    const std::string extension = image_extension(path);
    if (!writable_image_path(path) || extension == ".ico" || extension == ".cur") {
        const std::string message = "Plan Paint cannot save " + path + " this way";
        reply.fail(automation::ModelToolError::invalid_arguments, message.c_str(),
                   "a .png, .bmp, .jpg, .tif or .tga path");
        return;
    }
    try {
        editor.finish_controls();
        if (!editor.save_path(path)) {
            reply.fail(automation::ModelToolError::needs_user, "the picture was not saved",
                       "the reply names the program's question, if it asked one");
            return;
        }
    } catch (const std::exception& failure) {
        reply.fail(automation::ModelToolError::internal, failure.what(), "check the path");
        return;
    }
    reply_text(reply, "saved the picture to " + path + "\n");
}

void EditorModelTools::draw(void* context, const automation::ModelToolCall& call, automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    const stxlib::Optional<stxlib::TextView> named = arguments.member_text(arguments.root(), "shape");
    int shape = -1;
    if (const stxlib::TextView* const name = named.value()) {
        const std::string wanted((*name).data(), (*name).count());
        for (int index = 0; index < shape_count; ++index) {
            if (two_point_shape(index) && shape_tool_name(index) == wanted) shape = index;
        }
    }
    if (shape < 0) {
        reply.fail(automation::ModelToolError::invalid_arguments, "shape is one of the names the schema lists",
                   "rectangle, oval, line, rounded_rectangle, triangle, ...");
        return;
    }
    Point from{};
    Point to{};
    if (!point_member(arguments, "from", from, reply) || !point_member(arguments, "to", to, reply)) return;
    Editor& editor = tools.editor_;
    Document& document = editor.document;
    const stxlib::Optional<stxlib::TextView> outline_text = arguments.member_text(arguments.root(), "colour");
    const bool outline = outline_text.absent() || std::string_view((*outline_text.value()).data(),
                                                                   (*outline_text.value()).count()) != "none";
    Color edge{};
    if (outline && !colour_member(arguments, "colour", document.ink.primary, edge, reply)) return;
    const stxlib::Optional<stxlib::JsonRef> fill_member = arguments.member(arguments.root(), "fill");
    const bool filled = !fill_member.absent();
    Color body{};
    if (filled && !colour_member(arguments, "fill", document.ink.primary, body, reply)) return;
    if (!outline && !filled) {
        reply.fail(automation::ModelToolError::invalid_arguments, "a shape with no outline and no fill draws nothing",
                   "give colour or fill");
        return;
    }
    int size = 3;
    if (!width_member(arguments, size, reply)) return;
    // Whatever floats is settled first, as choosing another tool does.
    editor.finish_controls();
    const Ink edge_ink = plain_ink(edge, size);
    const Ink body_ink = plain_ink(body, size);
    Image drawn = document.image;
    draw_shape(drawn, static_cast<Shape>(shape), from, to, edge_ink, outline, filled, Brush::Round, &body_ink);
    document.constrain_selection(drawn, document.image);
    document.checkpoint();
    document.image = std::move(drawn);
    editor.refresh();
    reply_text(reply, "drew a " + shape_tool_name(shape) + " from " + point_text(gf::Point{from.x, from.y}) + " to " +
                          point_text(gf::Point{to.x, to.y}) + (outline ? ", outline " + hex_text(edge) : "") +
                          (filled ? ", fill " + hex_text(body) : "") + "\n");
}

void EditorModelTools::stroke(void* context, const automation::ModelToolCall& call, automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    std::vector<Point> points{};
    if (!point_list(arguments, "points", 2U, 4096U, points, reply)) return;
    Editor& editor = tools.editor_;
    Document& document = editor.document;
    Color color{};
    if (!colour_member(arguments, "colour", document.ink.primary, color, reply)) return;
    int size = 3;
    if (!width_member(arguments, size, reply)) return;
    editor.finish_controls();
    const Ink ink = plain_ink(color, size);
    Image drawn = document.image;
    for (std::size_t index = 1U; index < points.size(); ++index) {
        paint::stroke(drawn, points[index - 1U], points[index], ink);
    }
    document.constrain_selection(drawn, document.image);
    document.checkpoint();
    document.image = std::move(drawn);
    editor.refresh();
    reply_text(reply, "drew a line through " + std::to_string(points.size()) + " points in " + hex_text(color) + "\n");
}

void EditorModelTools::set_colour(void* context, const automation::ModelToolCall& call,
                                  automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    const stxlib::Optional<stxlib::TextView> hex = arguments.member_text(arguments.root(), "hex");
    Color color{};
    if (hex.absent() || !parse_hex(*hex.value(), color)) {
        reply.fail(automation::ModelToolError::invalid_arguments, "hex is a colour: #RRGGBB or #RRGGBBAA", nullptr);
        return;
    }
    const stxlib::Optional<stxlib::TextView> which = arguments.member_text(arguments.root(), "which");
    bool alt = false;
    if (const stxlib::TextView* const name = which.value()) {
        const std::string_view text((*name).data(), (*name).count());
        if (text != "primary" && text != "alt") {
            reply.fail(automation::ModelToolError::invalid_arguments, "which is primary or alt", nullptr);
            return;
        }
        alt = text == "alt";
    }
    Editor& editor = tools.editor_;
    Document& document = editor.document;
    if (alt) {
        document.ink.secondary = color;
    } else {
        document.ink.primary = color;
    }
    // As the palette does: a colour paints, whatever pattern was chosen.
    Ink& material = alt ? document.alt_ink : document.ink;
    if (material.pattern == Pattern::None) select_pattern(material, Pattern::Solid);
    editor.refresh();
    reply_text(reply, std::string(alt ? "alt" : "primary") + " colour: " + hex_text(color) + "\n");
}

void EditorModelTools::to_window(void* context, const automation::ModelToolCall& call,
                                 automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    std::vector<Point> points{};
    if (!point_list(arguments, "points", 1U, 256U, points, reply)) return;
    std::string text = "window points:";
    for (const Point& point : points) text += " " + point_text(window_point(tools.editor_, point));
    reply_text(reply, text + "\n");
}

} // namespace paint::forms

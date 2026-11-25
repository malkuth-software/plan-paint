#include "forms/model_tools.hpp"

#include "forms/editor.hpp"
#include "codecs.hpp"
#include "gradient.hpp"
#include "paths.hpp"
#include "raster.hpp"
#include "text.hpp"

#include <gui_forms/automation/marks/automation_marks.hpp>
#include <gui_forms/commands/command/command.hpp>
#include <stxlib/json.hpp>
#include <stxlib/text.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <numbers>
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
    "- Draw with plan_paint_draw (shapes, rotatable), plan_paint_path (filled polygons and Bezier paths), "
    "plan_paint_stroke (lines through points), plan_paint_fill (bucket fill), plan_paint_gradient and "
    "plan_paint_text, in picture pixels and #RRGGBB colours; each is one undo step. A point is {x, y}, as in "
    "GUI.Forms' tools; an [x, y] pair works too. plan_paint_sample reads "
    "colours back. A batch holds up to 512 steps, so a whole drawing can be one call. plan_paint_set_colour sets "
    "the Primary or Alt colour; the palette's swatches name their #RRGGBB.\n"
    "- If the picture is cut off by the window, command_fit fits it to the view (a launch for a model fits it "
    "when the window first shows).\n"
    "- With the pointer, the Shape tool's default gesture is click to place: after the press the shape follows the "
    "pointer and floats until the next click places it; Escape or a right-click cancels it. Bezier and arc curves "
    "and paths stay editable until Escape (command_release) or another tool commits them. Replies name anything "
    "floating as pending.\n"
    "- With the pointer, Edge and Fill paint with the Primary colour unless the colour group gives Fill the Alt "
    "colour.\n"
    "- Resize and Properties open dialogs: use plan_paint_resize_image instead. New, Open, Save and Exit are "
    "destructive and ask the user once per launch.\n"
    "- To finish: save with plan_paint_save (a path for a new picture; a save you ask for with a path asks "
    "nobody), then call release_control to leave the program open with the user, or disconnect. A disconnect "
    "with unsaved changes closes nothing and tells you what to save; it is never shown to the user.";

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
    "default 3.\"},"
    "\"rotation\":{\"type\":\"number\",\"description\":\"Degrees clockwise about the box's centre; default 0.\"}},"
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
// The schemas of the round-4 tools (filled paths, bucket fill, gradient,
// text and sampling); a point is {x, y} (or [x, y]) in picture pixels.
constexpr const char* path_schema =
    "{\"type\":\"object\",\"properties\":{\"points\":{\"type\":\"array\",\"minItems\":2,\"maxItems\":4096,\"descr"
    "iption\":\"The path's points in picture pixels, in order. A point with c1 and c2 ends a cubic Bezier segment"
    " from the point before it, c1 and c2 its control points; a point without them ends a straight edge.\",\"item"
    "s\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"c1\":{\"t"
    "ype\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\""
    ",\"y\"],\"additionalProperties\":false},\"c2\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number"
    "\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false}},\"required\":[\""
    "x\",\"y\"],\"additionalProperties\":false}},\"closed\":{\"type\":\"boolean\",\"description\":\"Close the pat"
    "h back to its first point; default true.\"},\"fill\":{\"type\":\"string\",\"description\":\"The fill's colou"
    "r, #RRGGBB or #RRGGBBAA; default no fill.\"},\"colour\":{\"type\":\"string\",\"description\":\"The outline's"
    " colour; default the Primary colour. \\\"none\\\" draws no outline.\"},\"size\":{\"type\":\"integer\",\"mini"
    "mum\":1,\"maximum\":100,\"description\":\"The outline's width in pixels; default 3.\"},\"rotation\":{\"type\""
    ":\"number\",\"description\":\"Degrees clockwise about the points' centre; default 0.\"}},\"required\":[\"poi"
    "nts\"],\"additionalProperties\":false}";
constexpr const char* fill_schema =
    "{\"type\":\"object\",\"properties\":{\"at\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"}"
    ",\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false},\"colour\":{\"type\""
    ":\"string\",\"description\":\"#RRGGBB or #RRGGBBAA; default the Primary colour.\"}},\"required\":[\"at\"],\""
    "additionalProperties\":false}";
constexpr const char* gradient_schema =
    "{\"type\":\"object\",\"properties\":{\"from\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\""
    "},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false},\"to\":{\"type\":\""
    "object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"]"
    ",\"additionalProperties\":false},\"points\":{\"type\":\"array\",\"minItems\":3,\"maxItems\":4096,\"descripti"
    "on\":\"A polygon to fill instead of the from-to box (from and to still set the gradient's extent).\",\"items"
    "\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},\"required\""
    ":[\"x\",\"y\"],\"additionalProperties\":false}},\"stops\":{\"type\":\"array\",\"minItems\":2,\"maxItems\":32"
    ",\"items\":{\"type\":\"object\",\"properties\":{\"position\":{\"type\":\"number\",\"minimum\":0,\"maximum\":"
    "1},\"colour\":{\"type\":\"string\"}},\"required\":[\"position\",\"colour\"],\"additionalProperties\":false}}"
    ",\"kind\":{\"type\":\"string\",\"enum\":[\"linear\",\"radial\"],\"description\":\"Default linear.\"},\"angle"
    "\":{\"type\":\"number\",\"description\":\"A linear gradient's direction in degrees: 0 runs left to right, 90"
    " top to bottom.\"}},\"required\":[\"from\",\"to\",\"stops\"],\"additionalProperties\":false}";
constexpr const char* text_schema =
    "{\"type\":\"object\",\"properties\":{\"at\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"}"
    ",\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false},\"text\":{\"type\":"
    "\"string\",\"maxLength\":4096,\"description\":\"The text; its top-left corner is at.\"},\"size\":{\"type\":\""
    "integer\",\"minimum\":4,\"maximum\":512,\"description\":\"Pixels; default 24.\"},\"colour\":{\"type\":\"stri"
    "ng\",\"description\":\"#RRGGBB or #RRGGBBAA; default the Primary colour.\"},\"bold\":{\"type\":\"boolean\"},"
    "\"italic\":{\"type\":\"boolean\"},\"mono\":{\"type\":\"boolean\"}},\"required\":[\"at\",\"text\"],\"addition"
    "alProperties\":false}";
constexpr const char* sample_schema =
    "{\"type\":\"object\",\"properties\":{\"points\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":1024,\"descr"
    "iption\":\"Points in picture pixels.\",\"items\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"numb"
    "er\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false}}},\"required\""
    ":[\"points\"],\"additionalProperties\":false}";
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

// A point at `value` into `point`: the object {x, y}, the canonical form
// the schemas name, or the pair [x, y], as GUI.Forms' capture also takes
// (1.0.5 review: the two shapes differed between the tools).
bool read_point(const stxlib::JsonDocument& document, stxlib::JsonRef value, Point& point) {
    const bool pair = document.is(value, stxlib::JsonKind::array) && document.child_count(value) == 2U;
    const stxlib::Optional<stxlib::JsonRef> x = pair ? document.element(value, 0U) : document.member(value, "x");
    const stxlib::Optional<stxlib::JsonRef> y = pair ? document.element(value, 1U) : document.member(value, "y");
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
            const std::string message = "each of " + label + " is {x, y} (or [x, y]), numbers of picture pixels";
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
        const std::string message = std::string(name.data(), name.count()) + " is {x, y} (or [x, y]) in picture pixels";
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

std::string point_number(double value) {
    std::array<char, 32> text{};
    static_cast<void>(std::snprintf(text.data(), text.size(), "%g", value));
    return std::string(text.data());
}

// `points` turned `degrees` clockwise about `centre` (picture y runs down,
// so a positive angle turns clockwise on screen).
void rotate_points(std::vector<Point>& points, double degrees, Point centre) {
    if (degrees == 0.0) return;
    const double radians = degrees * std::numbers::pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    for (Point& point : points) {
        const double dx = point.x - centre.x;
        const double dy = point.y - centre.y;
        point = Point{centre.x + dx * cosine - dy * sine, centre.y + dx * sine + dy * cosine};
    }
}

// The centre of the points' bounding box.
Point box_centre(const std::vector<Point>& points) {
    double left = points.front().x;
    double right = left;
    double top = points.front().y;
    double bottom = top;
    for (const Point& point : points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return Point{(left + right) / 2.0, (top + bottom) / 2.0};
}

// The number member "rotation" in degrees; 0 when absent.
bool rotation_member(const stxlib::JsonDocument& document, double& degrees, automation::ModelToolReply& reply) {
    const stxlib::Optional<stxlib::JsonRef> member = document.member(document.root(), "rotation");
    if (member.absent()) {
        degrees = 0.0;
        return true;
    }
    const stxlib::Optional<double> value = document.number(*member.value());
    if (value.absent() || !std::isfinite(*value.value()) || std::abs(*value.value()) > 3600.0) {
        reply.fail(automation::ModelToolError::invalid_arguments, "rotation is a number of degrees", nullptr);
        return false;
    }
    degrees = *value.value();
    return true;
}

// Steps a cubic Bezier segment is drawn with.
constexpr int bezier_steps = 24;

// The path member "points" flattened to straight edges: a point with c1
// and c2 ends a cubic Bezier segment from the point before it.
bool path_points(const stxlib::JsonDocument& document, std::vector<Point>& points,
                 automation::ModelToolReply& reply) {
    const stxlib::Optional<stxlib::JsonRef> list = document.member(document.root(), "points");
    if (list.absent() || !document.is(*list.value(), stxlib::JsonKind::array) ||
        document.child_count(*list.value()) < 2U || document.child_count(*list.value()) > 4096U) {
        reply.fail(automation::ModelToolError::invalid_arguments,
                   "points is an array of 2 to 4096 points {x, y}, each with c1 and c2 for a Bezier segment",
                   nullptr);
        return false;
    }
    stxlib::Optional<stxlib::JsonRef> item = document.first_child(*list.value());
    while (const stxlib::JsonRef* const value = item.value()) {
        Point end{};
        if (!read_point(document, *value, end)) {
            reply.fail(automation::ModelToolError::invalid_arguments, "each point is {x, y} (or [x, y]) in picture pixels",
                       nullptr);
            return false;
        }
        const stxlib::Optional<stxlib::JsonRef> first = document.member(*value, "c1");
        const stxlib::Optional<stxlib::JsonRef> second = document.member(*value, "c2");
        if (first.present() != second.present()) {
            reply.fail(automation::ModelToolError::invalid_arguments, "a Bezier point names both c1 and c2",
                       nullptr);
            return false;
        }
        if (first.present() && !points.empty()) {
            Point c1{};
            Point c2{};
            if (!read_point(document, *first.value(), c1) || !read_point(document, *second.value(), c2)) {
                reply.fail(automation::ModelToolError::invalid_arguments, "c1 and c2 are {x, y} in picture pixels",
                           nullptr);
                return false;
            }
            const Point start = points.back();
            for (int step = 1; step <= bezier_steps; ++step) {
                const double t = static_cast<double>(step) / bezier_steps;
                const double u = 1.0 - t;
                const double a = u * u * u;
                const double b = 3.0 * u * u * t;
                const double c = 3.0 * u * t * t;
                const double d = t * t * t;
                points.push_back(Point{a * start.x + b * c1.x + c * c2.x + d * end.x,
                                       a * start.y + b * c1.y + c * c2.y + d * end.y});
            }
        } else {
            points.push_back(end);
        }
        item = document.next_sibling(*value);
    }
    return true;
}

// Commits `drawn` as the picture: inside the selection only, one undo step.
void commit_drawing(Editor& editor, Image& drawn) {
    Document& document = editor.document;
    document.constrain_selection(drawn, document.image);
    document.checkpoint();
    document.image = std::move(drawn);
    editor.refresh();
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
    info.description = "The picture's size, file name, whether it has unsaved changes, what is pending (a shape "
                       "still floating), whether a selection is active, the Primary and Alt colours, and the "
                       "picture's corners in the window (for pointer). Reads only.";
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

    automation::ModelToolSpec outline{};
    outline.name = "plan_paint_path";
    outline.title = "Draw a path";
    outline.description = "Draws a polygon or path through points in picture pixels: straight edges, or cubic Bezier "
                          "segments where a point gives c1 and c2; closed and filled in #RRGGBB, outlined or not, "
                          "turned by rotation. One undo step; nothing floats.";
    outline.input_schema = path_schema;
    outline.access = automation::ModelToolAccess::acting;
    outline.settles = true;
    static_cast<void>(automation::register_model_tool(outline, &EditorModelTools::path, this));

    automation::ModelToolSpec bucket{};
    bucket.name = "plan_paint_fill";
    bucket.title = "Fill";
    bucket.description = "Fills the area of one colour around a point with a colour, as the Fill tool does. One "
                         "undo step.";
    bucket.input_schema = fill_schema;
    bucket.access = automation::ModelToolAccess::acting;
    bucket.settles = true;
    static_cast<void>(automation::register_model_tool(bucket, &EditorModelTools::fill, this));

    automation::ModelToolSpec blend{};
    blend.name = "plan_paint_gradient";
    blend.title = "Gradient";
    blend.description = "Paints a linear or radial gradient through colour stops over the box from-to, or over a "
                        "polygon. One undo step.";
    blend.input_schema = gradient_schema;
    blend.access = automation::ModelToolAccess::acting;
    blend.settles = true;
    static_cast<void>(automation::register_model_tool(blend, &EditorModelTools::gradient, this));

    automation::ModelToolSpec words{};
    words.name = "plan_paint_text";
    words.title = "Text";
    words.description = "Draws text with its top-left corner at a point, in Plan Paint's own face, committed at once. "
                        "One undo step.";
    words.input_schema = text_schema;
    words.access = automation::ModelToolAccess::acting;
    words.settles = true;
    static_cast<void>(automation::register_model_tool(words, &EditorModelTools::text, this));

    automation::ModelToolSpec probe{};
    probe.name = "plan_paint_sample";
    probe.title = "Sample colours";
    probe.description = "The picture's colour at each point in picture pixels, as #RRGGBB or #RRGGBBAA, to check a "
                        "drawing without a capture. Reads only.";
    probe.input_schema = sample_schema;
    probe.access = automation::ModelToolAccess::read_only;
    static_cast<void>(automation::register_model_tool(probe, &EditorModelTools::sample, this));

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
    static_cast<void>(automation::unregister_model_tool("plan_paint_path"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_fill"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_gradient"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_text"));
    static_cast<void>(automation::unregister_model_tool("plan_paint_sample"));
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
    double rotation = 0.0;
    if (!rotation_member(arguments, rotation, reply)) return;
    // Whatever floats is settled first, as choosing another tool does.
    editor.finish_controls();
    const Ink edge_ink = plain_ink(edge, size);
    const Ink body_ink = plain_ink(body, size);
    Image drawn = document.image;
    const Shape kind = static_cast<Shape>(shape);
    // draw_shape's own outline, turned about the box's centre.
    std::vector<Point> outline_points = shape_points(kind, from, to);
    rotate_points(outline_points, rotation, Point{(from.x + to.x) / 2.0, (from.y + to.y) / 2.0});
    polygon(drawn, outline_points, edge_ink, outline, filled, kind != Shape::Line, Brush::Round, &body_ink);
    commit_drawing(editor, drawn);
    reply_text(reply, "drew a " + shape_tool_name(shape) + " from " + point_text(gf::Point{from.x, from.y}) + " to " +
                          point_text(gf::Point{to.x, to.y}) +
                          (rotation != 0.0 ? ", turned " + point_number(rotation) + " degrees" : "") +
                          (outline ? ", outline " + hex_text(edge) : "") + (filled ? ", fill " + hex_text(body) : "") +
                          "\n");
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

void EditorModelTools::path(void* context, const automation::ModelToolCall& call, automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    std::vector<Point> points{};
    if (!path_points(arguments, points, reply)) return;
    Editor& editor = tools.editor_;
    Document& document = editor.document;
    const stxlib::Optional<stxlib::TextView> outline_text = arguments.member_text(arguments.root(), "colour");
    const bool outline = outline_text.absent() || std::string_view((*outline_text.value()).data(),
                                                                   (*outline_text.value()).count()) != "none";
    Color edge{};
    if (outline && !colour_member(arguments, "colour", document.ink.primary, edge, reply)) return;
    const bool filled = arguments.member(arguments.root(), "fill").present();
    Color body{};
    if (filled && !colour_member(arguments, "fill", document.ink.primary, body, reply)) return;
    if (!outline && !filled) {
        reply.fail(automation::ModelToolError::invalid_arguments, "a path with no outline and no fill draws nothing",
                   "give colour or fill");
        return;
    }
    const stxlib::Optional<bool> closed_member = arguments.member_boolean(arguments.root(), "closed");
    const bool closed = closed_member.absent() || *closed_member.value();
    if (filled && !closed) {
        reply.fail(automation::ModelToolError::invalid_arguments, "an open path has no inside to fill",
                   "leave closed true, or give no fill");
        return;
    }
    int size = 3;
    if (!width_member(arguments, size, reply)) return;
    double rotation = 0.0;
    if (!rotation_member(arguments, rotation, reply)) return;
    editor.finish_controls();
    rotate_points(points, rotation, box_centre(points));
    const Ink edge_ink = plain_ink(edge, size);
    const Ink body_ink = plain_ink(body, size);
    Image drawn = document.image;
    polygon(drawn, points, edge_ink, outline, filled, closed, Brush::Round, &body_ink);
    commit_drawing(editor, drawn);
    reply_text(reply, std::string("drew a ") + (closed ? "closed" : "open") + " path of " +
                          std::to_string(points.size()) + " points" + (outline ? ", outline " + hex_text(edge) : "") +
                          (filled ? ", fill " + hex_text(body) : "") + "\n");
}

void EditorModelTools::fill(void* context, const automation::ModelToolCall& call, automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    Point at{};
    if (!point_member(arguments, "at", at, reply)) return;
    Editor& editor = tools.editor_;
    Document& document = editor.document;
    Color color{};
    if (!colour_member(arguments, "colour", document.ink.primary, color, reply)) return;
    const int x = static_cast<int>(std::floor(at.x));
    const int y = static_cast<int>(std::floor(at.y));
    if (!document.image.contains(x, y)) {
        reply.fail(automation::ModelToolError::invalid_arguments, "at is outside the picture",
                   "plan_paint_image_info gives the picture's size");
        return;
    }
    editor.finish_controls();
    const Color replaced = document.image.get(x, y);
    Image drawn = document.image;
    flood(drawn, x, y, plain_ink(color, 1));
    commit_drawing(editor, drawn);
    reply_text(reply, "filled the " + hex_text(replaced) + " area at " + point_text(gf::Point{at.x, at.y}) + " with " +
                          hex_text(color) + "\n");
}

void EditorModelTools::gradient(void* context, const automation::ModelToolCall& call,
                                automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    Point from{};
    Point to{};
    if (!point_member(arguments, "from", from, reply) || !point_member(arguments, "to", to, reply)) return;
    Gradient blend{};
    blend.stops.clear();
    const stxlib::Optional<stxlib::JsonRef> stops = arguments.member(arguments.root(), "stops");
    if (stops.absent() || !arguments.is(*stops.value(), stxlib::JsonKind::array) ||
        arguments.child_count(*stops.value()) < 2U || arguments.child_count(*stops.value()) > 32U) {
        reply.fail(automation::ModelToolError::invalid_arguments, "stops is 2 to 32 {position, colour}", nullptr);
        return;
    }
    stxlib::Optional<stxlib::JsonRef> item = arguments.first_child(*stops.value());
    while (const stxlib::JsonRef* const value = item.value()) {
        const stxlib::Optional<stxlib::JsonRef> position = arguments.member(*value, "position");
        const stxlib::Optional<stxlib::TextView> colour = arguments.member_text(*value, "colour");
        const stxlib::Optional<double> number =
            position.present() ? arguments.number(*position.value()) : stxlib::Optional<double>{};
        GradientStop stop{};
        if (number.absent() || colour.absent() || !parse_hex(*colour.value(), stop.color) ||
            !(*number.value() >= 0.0 && *number.value() <= 1.0)) {
            reply.fail(automation::ModelToolError::invalid_arguments,
                       "each stop is {position from 0 to 1, colour #RRGGBB or #RRGGBBAA}", nullptr);
            return;
        }
        stop.position = *number.value();
        blend.stops.push_back(stop);
        item = arguments.next_sibling(*value);
    }
    const stxlib::Optional<stxlib::TextView> kind = arguments.member_text(arguments.root(), "kind");
    if (const stxlib::TextView* const name = kind.value()) {
        const std::string_view text((*name).data(), (*name).count());
        if (text != "linear" && text != "radial") {
            reply.fail(automation::ModelToolError::invalid_arguments, "kind is linear or radial", nullptr);
            return;
        }
        blend.kind = text == "radial" ? GradientKind::Radial : GradientKind::Linear;
    }
    const stxlib::Optional<stxlib::JsonRef> angle = arguments.member(arguments.root(), "angle");
    if (angle.present()) {
        const stxlib::Optional<double> degrees = arguments.number(*angle.value());
        if (degrees.absent() || !std::isfinite(*degrees.value())) {
            reply.fail(automation::ModelToolError::invalid_arguments, "angle is a number of degrees", nullptr);
            return;
        }
        blend.angle = *degrees.value();
    }
    normalize_gradient(blend);
    std::vector<Point> area{};
    if (arguments.member(arguments.root(), "points").present()) {
        if (!point_list(arguments, "points", 3U, 4096U, area, reply)) return;
    }
    const int left = static_cast<int>(std::floor(std::min(from.x, to.x)));
    const int top = static_cast<int>(std::floor(std::min(from.y, to.y)));
    const int right = static_cast<int>(std::ceil(std::max(from.x, to.x)));
    const int bottom = static_cast<int>(std::ceil(std::max(from.y, to.y)));
    if (right <= left || bottom <= top) {
        reply.fail(automation::ModelToolError::invalid_arguments, "from and to make an empty box", nullptr);
        return;
    }
    Editor& editor = tools.editor_;
    Document& document = editor.document;
    editor.finish_controls();
    const GradientSampler sampler(blend, Rect{left, top, right - left, bottom - top});
    Image drawn = document.image;
    int painted_left = left;
    int painted_top = top;
    int painted_right = right;
    int painted_bottom = bottom;
    if (!area.empty()) {
        painted_left = drawn.width;
        painted_top = drawn.height;
        painted_right = 0;
        painted_bottom = 0;
        for (const Point& point : area) {
            painted_left = std::min(painted_left, static_cast<int>(std::floor(point.x)));
            painted_top = std::min(painted_top, static_cast<int>(std::floor(point.y)));
            painted_right = std::max(painted_right, static_cast<int>(std::ceil(point.x)));
            painted_bottom = std::max(painted_bottom, static_cast<int>(std::ceil(point.y)));
        }
    }
    painted_left = std::max(painted_left, 0);
    painted_top = std::max(painted_top, 0);
    painted_right = std::min(painted_right, drawn.width);
    painted_bottom = std::min(painted_bottom, drawn.height);
    for (int y = painted_top; y < painted_bottom; ++y) {
        for (int x = painted_left; x < painted_right; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            if (!area.empty() && !inside_polygon(area, px, py)) continue;
            drawn.blend(x, y, sampler.sample(px, py));
        }
    }
    commit_drawing(editor, drawn);
    reply_text(reply, std::string("painted a ") + (blend.kind == GradientKind::Radial ? "radial" : "linear") +
                          " gradient of " + std::to_string(blend.stops.size()) + " stops over " +
                          (area.empty() ? "the box " + point_text(gf::Point{from.x, from.y}) + " to " +
                                              point_text(gf::Point{to.x, to.y})
                                        : "a polygon of " + std::to_string(area.size()) + " points") +
                          "\n");
}

void EditorModelTools::text(void* context, const automation::ModelToolCall& call, automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    Point at{};
    if (!point_member(arguments, "at", at, reply)) return;
    const stxlib::Optional<stxlib::TextView> words = arguments.member_text(arguments.root(), "text");
    if (words.absent() || (*words.value()).count() == 0U || (*words.value()).count() > 4096U) {
        reply.fail(automation::ModelToolError::invalid_arguments, "text is 1 to 4096 bytes of UTF-8", nullptr);
        return;
    }
    Editor& editor = tools.editor_;
    Document& document = editor.document;
    Color color{};
    if (!colour_member(arguments, "colour", document.ink.primary, color, reply)) return;
    TextStyle style{};
    const stxlib::Optional<stxlib::i64> size = arguments.member_integer(arguments.root(), "size");
    if (size.present()) {
        if (*size.value() < 4 || *size.value() > 512) {
            reply.fail(automation::ModelToolError::invalid_arguments, "size is from 4 to 512 pixels", nullptr);
            return;
        }
        style.size = static_cast<int>(*size.value());
    }
    const stxlib::Optional<bool> bold = arguments.member_boolean(arguments.root(), "bold");
    const stxlib::Optional<bool> italic = arguments.member_boolean(arguments.root(), "italic");
    const stxlib::Optional<bool> mono = arguments.member_boolean(arguments.root(), "mono");
    style.bold = bold.present() && *bold.value();
    style.italic = italic.present() && *italic.value();
    style.mono = mono.present() && *mono.value();
    style.word_wrap = false;
    const std::string content((*words.value()).data(), (*words.value()).count());
    editor.finish_controls();
    Image drawn = document.image;
    draw_text(drawn, at, content, style, color, Color{0, 0, 0, 0}, std::string{});
    commit_drawing(editor, drawn);
    reply_text(reply, "drew " + std::to_string(content.size()) + " bytes of text at " +
                          point_text(gf::Point{at.x, at.y}) + " in " + hex_text(color) + "\n");
}

void EditorModelTools::sample(void* context, const automation::ModelToolCall& call,
                              automation::ModelToolReply& reply) {
    EditorModelTools& tools = *static_cast<EditorModelTools*>(context);
    stxlib::JsonDocument arguments{};
    if (!read_arguments(call, arguments, reply)) return;
    std::vector<Point> points{};
    if (!point_list(arguments, "points", 1U, 1024U, points, reply)) return;
    const Image& image = tools.editor_.document.image;
    std::string text = "colours:\n";
    for (const Point& point : points) {
        const int x = static_cast<int>(std::floor(point.x));
        const int y = static_cast<int>(std::floor(point.y));
        text += point_text(gf::Point{point.x, point.y}) + " ";
        text += image.contains(x, y) ? hex_text(image.get(x, y)) : std::string("outside");
        text += '\n';
    }
    reply_text(reply, text);
}

} // namespace paint::forms

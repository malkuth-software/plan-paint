#include "forms/editor.hpp"
#include "alpha_tools.hpp"
#include "codecs.hpp"
#include "cursors/tool_cursors.hpp"
#include "forms/atlas.hpp"
#include "forms/display.hpp"
#include "localization.hpp"
#include "platform.hpp"
#include "paths.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <gui_forms/host.hpp>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
namespace paint::forms {
namespace gf = gui_forms;
namespace {
const Tool tools[] = {Tool::Select,    Tool::Lasso, Tool::Pencil, Tool::Fill, Tool::Eraser, Tool::Picker,
                      Tool::Magnifier, Tool::Brush, Tool::Shape,  Tool::Path, Tool::Stamp,  Tool::Guide};
bool centered_shape(Shape shape) {
    return shape != Shape::Line && shape != Shape::Bezier && shape != Shape::Arc && shape != Shape::Polygon;
}
bool radial_shape(Shape shape) {
    return shape == Shape::Circle || shape == Shape::Oval || shape == Shape::Pentagon ||
           shape == Shape::Hexagon || shape == Shape::Octagon || shape == Shape::Star4 ||
           shape == Shape::Star5 || shape == Shape::Star6 || shape == Shape::Star8 || shape == Shape::Gear ||
           shape == Shape::Burst;
}
class ZoomTrackBar final : public gf::TrackBar {
  public:
    explicit ZoomTrackBar(gf::StableId id) : TrackBar(std::move(id)) {}
    gf::SemanticDescriptor semantic_descriptor() const override {
        gf::SemanticDescriptor descriptor = TrackBar::semantic_descriptor();
        descriptor.numeric_value = std::exp2(value()) * 100;
        descriptor.minimum_value = 6.25;
        descriptor.maximum_value = 3200;
        descriptor.value = std::to_string(static_cast<int>(std::lround(std::exp2(value()) * 100))) + "%";
        return descriptor;
    }
    bool on_semantic_action(gf::SemanticAction action, std::string_view text) override {
        if (action == gf::SemanticAction::set_value) {
            try {
                std::size_t consumed = 0;
                double percentage = std::stod(std::string(text), &consumed);
                if (consumed != text.size() || !std::isfinite(percentage) || percentage < 6.25 ||
                    percentage > 3200) {
                    return false;
                }
                set_value(std::log2(percentage / 100));
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }
        return TrackBar::on_semantic_action(action, text);
    }
};
Rect rectangle(Point start, Point end) {
    return {static_cast<int>(std::floor(std::min(start.x, end.x))),
            static_cast<int>(std::floor(std::min(start.y, end.y))),
            std::max(1, static_cast<int>(std::ceil(std::abs(end.x - start.x)))),
            std::max(1, static_cast<int>(std::ceil(std::abs(end.y - start.y))))};
}
bool point_inside(Rect rectangle, Point point) {
    return point.x >= rectangle.x && point.y >= rectangle.y && point.x < rectangle.x + rectangle.w &&
           point.y < rectangle.y + rectangle.h;
}
Image validated_clipboard_image(const gf::HostImage& source) {
    const std::uint64_t pixels = static_cast<std::uint64_t>(source.width) * source.height;
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(source.width) * sizeof(Color);
    if (source.width == 0 || source.height == 0 || source.width > 16384 || source.height > 16384 ||
        pixels > 64000000 || source.row_bytes < row_bytes ||
        source.row_bytes > std::numeric_limits<std::size_t>::max() ||
        (source.height > 0 && source.row_bytes > source.pixels.size() / source.height)) {
        throw std::runtime_error(tr("The clipboard image has invalid or unsafe pixel geometry."));
    }
    Image image;
    image.reset(static_cast<int>(source.width), static_cast<int>(source.height));
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(y) * image.width,
                    source.pixels.data() + static_cast<std::size_t>(y) * source.row_bytes,
                    static_cast<std::size_t>(image.width) * sizeof(Color));
    }
    return image;
}
} // namespace
PaintCanvas::PaintCanvas(gf::StableId id, std::weak_ptr<Editor> editor)
    : RasterCanvas(std::move(id)), editor_(std::move(editor)) {
    set_style(gf::ControlStyles::opaque, true);
}
void PaintCanvas::on_pointer(gf::PointerEvent& event) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (!editor) {
        return;
    }
    (*editor).pointer(event);
    if (event.action == gf::PointerAction::up) {
        settle_view();
    }
    prepare_display();
    (*editor).prepare_stamp_view();
    event.handled = event.action == gf::PointerAction::down || event.action == gf::PointerAction::up ||
                    event.action == gf::PointerAction::move || event.action == gf::PointerAction::wheel;
}
void PaintCanvas::on_paint_overlay(gf::Painter& painter, gf::Rect damage) {
    std::shared_ptr<Editor> editor = editor_.lock();
    if (editor) {
        (*editor).paint_canvas_overlay(painter, damage);
    }
}
Editor::Editor(gf::StableId id) : Control(std::move(id)) {
    set_theme_override(ribbon_theme());
}
void Editor::initialize_control_tree() {
    set_allow_drop(true);
    initialize_warp();
    canvas_ = gf::make_control<PaintCanvas>(gf::StableId("canvas"),
                                            std::static_pointer_cast<Editor>(shared_from_this()));
    (*canvas_).set_focusable(true);
    (*canvas_).set_tab_stop(true);
    (*canvas_).set_view(1.0, {-16, -16});
    (*canvas_).set_transparency_colors(gf::Color::rgba(255, 255, 255), gf::Color::rgba(240, 240, 240));
    (*canvas_).set_transparency_cell_size(12);
    (*canvas_).set_canvas_background(gf::Color::rgba(0, 0, 0, 0));
    add_child(canvas_);
    ribbon_ = gf::make_control<Ribbon>(gf::StableId("ribbon"),
                                       std::static_pointer_cast<Editor>(shared_from_this()));
    add_child(ribbon_);
    help_ = gf::make_control<HelpBook>(gf::StableId("help-book"));
    (*help_).set_visible(false);
    add_child(help_);
    subscriptions_.push_back((*help_).close_clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Editor, &Editor::close_help>(*this)));
    menu_ = gf::make_control<gf::MenuStrip>(gf::StableId("menus"));
    gf::ThemeDefinition file_theme = gf::windows_professional_theme_definition();
    file_theme.id = "rainstar-file-tab";
    file_theme.compatibility.border = gf::Color::rgba(45, 105, 172);
    for (std::size_t state = 0; state < gf::control_surface_state_count; ++state) {
        gf::SurfaceMaterial surface;
        surface.fills = {gf::MaterialFillLayer::linear(
            {0, 0}, {0, 1}, {{0, gf::Color::rgba(82, 139, 224)}, {1, gf::Color::rgba(23, 63, 136)}})};
        file_theme.roles[static_cast<std::size_t>(gf::ControlVisualRole::panel)].ordinary[state].material =
            surface;
        gf::ControlRoleRecipes& item =
            file_theme.roles[static_cast<std::size_t>(gf::ControlVisualRole::menu_item)];
        item.ordinary[state].material = surface;
        item.ordinary[state].text = gf::Color::rgba(255, 255, 255);
        item.selected[state].material = surface;
        item.selected[state].text = gf::Color::rgba(255, 255, 255);
    }
    (*menu_).set_theme_override(gf::Theme::create(std::move(file_theme)));
    (*menu_).set_item_padding(17);
    rebuild_file_menu();
    add_child(menu_);
    file_notice_label_ = gf::make_control<gf::Label>(gf::StableId("file-change-notice"));
    (*file_notice_label_).set_font({gf::FontRole::control, 13, 600, false});
    add_child(file_notice_label_);
    file_reload_ = gf::make_control<gf::Button>(gf::StableId("file-reload"), tr("Reload from disk…"));
    subscriptions_.push_back((*file_reload_).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Editor, &Editor::reload_clicked>(*this)));
    add_child(file_reload_);
    status_ = gf::make_control<gf::Label>(gf::StableId("status"));
    cursor_status_ = gf::make_control<gf::Label>(gf::StableId("cursor-status"));
    dimensions_status_ = gf::make_control<gf::Label>(gf::StableId("dimensions-status"));
    selection_status_ = gf::make_control<gf::Label>(gf::StableId("selection-status"));
    for (const std::shared_ptr<gf::Label>& label :
         {status_, cursor_status_, dimensions_status_, selection_status_}) {
        (*label).set_font({gf::FontRole::control, 12, 400, false, 0.08});
        add_child(label);
    }
    tool_size_status_ = gf::make_control<gf::Button>(gf::StableId("status-tool-size"), tr("Size: 3 px"));
    (*tool_size_status_).set_accessible_name(tr("Current tool size; choose custom size"));
    zoom_out_ = gf::make_control<gf::Button>(gf::StableId("status-zoom-out"), "−");
    zoom_in_ = gf::make_control<gf::Button>(gf::StableId("status-zoom-in"), "+");
    zoom_reset_ = gf::make_control<gf::Button>(gf::StableId("status-zoom-reset"), "100%");
    for (const std::shared_ptr<gf::Button>& button : {zoom_out_, zoom_in_, zoom_reset_, tool_size_status_}) {
        (*button).set_theme_override(ribbon_theme());
        (*button).set_content_padding({2, 1, 2, 1});
        (*button).set_font({gf::FontRole::control,
                            button == zoom_reset_ || button == tool_size_status_ ? 12.0 : 18.0, 400, false});
        subscriptions_.push_back((*button).clicked().subscribe(
            *this, gf::Delegate<gf::ButtonBase&>::bind<Editor, &Editor::status_clicked>(*this)));
        add_child(button);
    }
    (*zoom_out_).set_accessible_name(tr("Zoom out"));
    (*zoom_in_).set_accessible_name(tr("Zoom in"));
    (*zoom_reset_).set_accessible_name(tr("Zoom percentage; reset to 100%"));
    zoom_slider_ = gf::make_control<ZoomTrackBar>(gf::StableId("zoom-slider"));
    (*zoom_slider_).set_range(-4, 5);
    (*zoom_slider_).set_value(0);
    (*zoom_slider_).set_small_change(0.25);
    (*zoom_slider_).set_large_change(1);
    (*zoom_slider_).set_show_ticks(false);
    (*zoom_slider_).set_visual_style(gf::TrackBarVisualStyle::classic);
    (*zoom_slider_).set_accessible_name(tr("Zoom percentage"));
    subscriptions_.push_back(
        (*zoom_slider_)
            .value_changed()
            .subscribe(*this, gf::Delegate<double>::bind<Editor, &Editor::zoom_slider_changed>(*this)));
    add_child(zoom_slider_);
    initialize_canvas_controls();
    refresh();
}
gf::MenuItemSpec Editor::menu_item(const std::string& id, const std::string& text) {
    std::shared_ptr<gf::Command> command = std::make_shared<gf::Command>(id, text);
    menu_subscriptions_.push_back((*command).invoked().subscribe(
        *this, gf::Delegate<const gf::CommandInvocation&>::bind<Editor, &Editor::command_invoked>(*this)));
    commands_.push_back(command);
    return {id, gf::MenuItemKind::command, command, text};
}
void Editor::rebuild_file_menu() {
    menu_subscriptions_.clear();
    commands_.clear();
    std::vector<gf::MenuItemSpec> items = {menu_item("new", tr("New")), menu_item("open", tr("Open…"))};
    items.push_back(menu_item("browse-assets", tr("Browse assets…")));
    items.push_back(menu_item("icon-batch", tr("Batch production…")));
    if (!recent.paths.empty()) {
        gf::MenuItemSpec recent_menu;
        recent_menu.stable_id = "recent-files";
        recent_menu.kind = gf::MenuItemKind::submenu;
        recent_menu.text = tr("Recent pictures");
        for (std::size_t i = 0; i < recent.paths.size(); ++i) {
            recent_menu.children.push_back(menu_item("recent-" + std::to_string(i), recent.paths[i]));
        }
        items.push_back(std::move(recent_menu));
    }
    items.push_back(menu_item("reload", tr("Reload from disk…")));
    items.push_back(menu_item("save", tr("Save")));
    items.push_back(menu_item("save-as", tr("Save as…")));
    items.push_back(menu_item("print-preview", tr("Print preview…")));
#if RAINSTAR_FORMS_NATIVE_PRINT
    items.push_back(menu_item("print", tr("Print…")));
    items.push_back(menu_item("page-setup", tr("Page setup…")));
    items.push_back(menu_item("acquire", tr("From scanner or camera…")));
    gf::MenuItemSpec wallpaper;
    wallpaper.stable_id = "wallpaper";
    wallpaper.kind = gf::MenuItemKind::submenu;
    wallpaper.text = tr("Set as desktop background");
    wallpaper.children = {menu_item("wallpaper-fill", tr("Fill")), menu_item("wallpaper-tile", tr("Tile")),
                          menu_item("wallpaper-center", tr("Center"))};
    items.push_back(std::move(wallpaper));
#endif
    items.push_back(menu_item("properties", tr("Properties…")));
    items.push_back(menu_item("recover", tr("Recover unfinished artwork…")));
    items.push_back(menu_item("settings", tr("Settings…")));
    items.push_back(menu_item("about", tr("About Plan Paint")));
    items.push_back(menu_item("quit", tr("Exit")));
    (*menu_).set_items({{"file", tr("File"), std::move(items)}});
}
void Editor::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    if (pattern_canvas_) { set_child_layout(pattern_canvas_, {0, 0, bounds.width, bounds.height}); }
    double ribbon_height = (*ribbon_).ribbon_height();
    const double previous_ribbon_height = (*ribbon_).committed_arranged_bounds().height;
    if (previous_ribbon_height > 0 && previous_ribbon_height != ribbon_height) {
        gui_drawing::PointF origin = (*canvas_).view_origin();
        origin.y += (ribbon_height - previous_ribbon_height) / (*canvas_).zoom();
        (*canvas_).set_view_origin(origin);
    }
    set_child_layout(ribbon_, {0, 0, bounds.width, ribbon_height});
    set_child_layout(menu_, {0, 0, 56, 27});
    const bool file_notice_visible = !file_notice_.empty() && !pattern_editing;
    (*file_notice_label_).set_visible(file_notice_visible);
    (*file_reload_).set_visible(file_notice_visible);
    (*file_reload_).set_enabled(disk_stamp_.availability == FileAvailability::Available);
    (*file_notice_label_).set_text(file_notice_);
    if (file_notice_visible) {
        set_child_layout(file_notice_label_, {12, ribbon_height + 2, std::max(1.0, bounds.width - 200), 26});
        set_child_layout(file_reload_, {bounds.width - 180, ribbon_height + 2, 168, 26});
        ribbon_height += 30;
    }
    double ruler = show_rulers ? 20 : 0;
    double footer = show_status ? 30 : 0;
    const double controls_height = settings.canvas_controls && !pattern_editing ? 132 : 0;
    arrange_canvas_controls({0, bounds.height - footer - controls_height, bounds.width, controls_height});
    footer += controls_height;
    double sidebar = show_help ? std::min(370.0, bounds.width * 0.38) : 0;
    (*help_).set_visible(show_help && !pattern_editing);
    set_child_layout(
        help_, {bounds.width - sidebar, ribbon_height, sidebar, bounds.height - ribbon_height - footer});
    set_child_layout(canvas_, {ruler, ribbon_height + ruler, bounds.width - ruler - sidebar,
                               std::max(1.0, bounds.height - ribbon_height - ruler - footer)});
    for (const std::shared_ptr<gf::Control>& control : std::vector<std::shared_ptr<gf::Control>>{
             status_, cursor_status_, selection_status_, dimensions_status_, zoom_reset_, zoom_out_,
             zoom_slider_, zoom_in_, tool_size_status_}) {
        (*control).set_visible(show_status && !pattern_editing);
    }
    double y = bounds.height - 28;
    const double scale = bounds.width / 1280;
    set_child_layout(status_, {12 * scale, y, 230 * scale, 26});
    set_child_layout(cursor_status_, {260 * scale, y, 165 * scale, 26});
    set_child_layout(tool_size_status_, {442 * scale, y + 1, 120 * scale, 25});
    set_child_layout(selection_status_, {574 * scale, y, 154 * scale, 26});
    set_child_layout(dimensions_status_, {740 * scale, y, 147 * scale, 26});
    set_child_layout(zoom_reset_, {898 * scale, y + 1, 65 * scale, 25});
    set_child_layout(zoom_out_, {969 * scale, y + 1, 26 * scale, 25});
    set_child_layout(zoom_slider_, {1001 * scale, y + 1, 205 * scale, 25});
    set_child_layout(zoom_in_, {1213 * scale, y + 1, 26 * scale, 25});
    for (const std::shared_ptr<gf::Label>& label :
         {status_, cursor_status_, selection_status_, dimensions_status_}) {
        (*label).set_font({gf::FontRole::control, std::clamp(12 * scale, 10.0, 12.0), 400, false});
    }
}
void Editor::on_paint(gf::Painter& painter, gf::Rect) {
    gf::Rect bounds = committed_arranged_bounds();
    painter.fill_rect({0, 0, bounds.width, bounds.height}, interface_color(*this, gf::Color::rgba(232, 240, 249)));
    if (show_rulers) {
        gf::Rect area = (*canvas_).committed_arranged_bounds();
        const double ribbon_height = area.y - 20;
        gui_drawing::PointF origin = (*canvas_).view_origin();
        double scale = (*canvas_).zoom();
        double step = 1;
        while (step * scale < 50) {
            step *= 2;
        }
        const gf::FontSpec font{gf::FontRole::control, 10, 400, false};
        const gf::Color color = interface_color(*this, gf::Color::rgba(81, 103, 127));
        painter.fill_rect({20, ribbon_height, area.width, 20}, interface_color(*this, gf::Color::rgba(245, 248, 252)));
        painter.fill_rect({0, (ribbon_height + 20), 20, area.height}, interface_color(*this, gf::Color::rgba(245, 248, 252)));
        for (int axis = 0; axis < 2; ++axis) {
            double start = axis == 0 ? origin.x : origin.y;
            double length = axis == 0 ? area.width : area.height;
            double end = start + length / scale;
            for (double value = std::ceil(start / (step / 5)) * (step / 5); value <= end; value += step / 5) {
                double position = (value - start) * scale;
                bool major = std::abs(value / step - std::round(value / step)) < 0.001;
                if (axis == 0) {
                    painter.draw_line({20 + position, (ribbon_height + 20)},
                                      {20 + position, major ? (ribbon_height + 12) : (ribbon_height + 16)},
                                      color, 1);
                    if (major) {
                        painter.draw_text_utf8({23 + position, (ribbon_height + 10)},
                                               std::to_string(static_cast<int>(std::round(value))), font,
                                               color);
                    }
                } else {
                    painter.draw_line({20, (ribbon_height + 20) + position},
                                      {major ? 12.0 : 16.0, (ribbon_height + 20) + position}, color, 1);
                    if (major) {
                        painter.draw_text_utf8({1, ribbon_height + 17 + position},
                                               std::to_string(static_cast<int>(std::round(value))), font,
                                               color);
                    }
                }
            }
        }
        painter.draw_line({20, ribbon_height}, {20, (ribbon_height + 20) + area.height}, color, 1);
        painter.draw_line({0, (ribbon_height + 20)}, {20 + area.width, (ribbon_height + 20)}, color, 1);
    }
    if (!show_status) {
        return;
    }
    double y = bounds.height - 30;
    painter.draw_line({0, y}, {bounds.width, y}, interface_color(*this, gf::Color::rgba(172, 193, 214)), 1);
    for (double position : {249.0, 434.0, 565.0, 731.0, 887.0}) {
        const double x = position * bounds.width / 1280;
        painter.draw_line({x, y + 5}, {x, bounds.height - 5}, interface_color(*this, gf::Color::rgba(193, 208, 224)), 1);
    }
}
gf::Point Editor::screen(Point point) const {
    const gui_drawing::PointF mapped = (*canvas_).view_point({point.x, point.y});
    gui_drawing::PointF origin = (*canvas_).view_origin();
    double zoom = (*canvas_).zoom();
    return {(mapped.x - origin.x) * zoom, (mapped.y - origin.y) * zoom};
}
void Editor::paint_canvas_overlay(gf::Painter& painter, gf::Rect) {
    painter.save();
    painter.clip_rect(
        {0, 0, (*canvas_).committed_arranged_bounds().width, (*canvas_).committed_arranged_bounds().height});
    if (transform_image_.value) {
        painter.draw_image(transform_image_, transform_destination_);
    }
    if (settings.canvas_controls) {
        const gf::Point point = screen({canvas_position_.x + 0.5, canvas_position_.y + 0.5});
        painter.draw_line({point.x - 7, point.y}, {point.x + 7, point.y}, gf::Color::rgba(255, 255, 255), 3);
        painter.draw_line({point.x, point.y - 7}, {point.x, point.y + 7}, gf::Color::rgba(255, 255, 255), 3);
        painter.draw_line({point.x - 7, point.y}, {point.x + 7, point.y}, gf::Color::rgba(0, 0, 0), 1);
        painter.draw_line({point.x, point.y - 7}, {point.x, point.y + 7}, gf::Color::rgba(0, 0, 0), 1);
    }
    paint_atlas_overlay(painter);
    paint_guide_overlay(painter);
    paint_spiro_overlay(painter);
    if ((show_hotspot || pick_hotspot) && document.atlas.kind == AtlasKind::Cursor &&
        document.atlas.active >= 0) {
        const IconFrame& frame = document.atlas.icons[document.atlas.active];
        gf::Point point = screen({frame.hotspot_x + 0.5, frame.hotspot_y + 0.5});
        painter.draw_line({point.x - 9, point.y}, {point.x + 9, point.y}, interface_color(*this, gf::Color::rgba(255, 255, 255)), 3);
        painter.draw_line({point.x, point.y - 9}, {point.x, point.y + 9}, interface_color(*this, gf::Color::rgba(255, 255, 255)), 3);
        painter.draw_line({point.x - 9, point.y}, {point.x + 9, point.y}, interface_color(*this, gf::Color::rgba(180, 25, 45)), 1);
        painter.draw_line({point.x, point.y - 9}, {point.x, point.y + 9}, interface_color(*this, gf::Color::rgba(180, 25, 45)), 1);
    }
    if (show_grid && (*canvas_).zoom() >= 4) {
        const gf::Rect area = (*canvas_).client_rectangle();
        double left_bound = 1e30, top_bound = 1e30, right_bound = -1e30, bottom_bound = -1e30;
        for (int i = 0; i < 4; ++i) {
            const gui_drawing::PointF point =
                canvas().client_to_bitmap({i & 1 ? area.width : 0, i & 2 ? area.height : 0});
            left_bound = std::min(left_bound, point.x);
            right_bound = std::max(right_bound, point.x);
            top_bound = std::min(top_bound, point.y);
            bottom_bound = std::max(bottom_bound, point.y);
        }
        const gui_drawing::PointF origin{left_bound, top_bound};
        int left = std::max(0, static_cast<int>(std::floor(origin.x)));
        int top = std::max(0, static_cast<int>(std::floor(origin.y)));
        int right = std::min(document.image.width, static_cast<int>(std::ceil(right_bound)));
        int bottom = std::min(document.image.height, static_cast<int>(std::ceil(bottom_bound)));
        gf::Color color = interface_color(*this, gf::Color::rgba(90, 110, 135, 85));
        for (int x = left; x <= right; ++x) {
            painter.draw_line(screen({static_cast<double>(x), static_cast<double>(top)}),
                              screen({static_cast<double>(x), static_cast<double>(bottom)}), color, 1);
        }
        for (int y = top; y <= bottom; ++y) {
            painter.draw_line(screen({static_cast<double>(left), static_cast<double>(y)}),
                              screen({static_cast<double>(right), static_cast<double>(y)}), color, 1);
        }
    }
    if ((document.selection.active && resize_handle_ < 0 && warp_mode_ != WarpMode::rotation) ||
        (dragging_ && document.tool == Tool::Select)) {
        Rect bounds = document.selection.active
                          ? Rect{document.selection.x, document.selection.y, document.selection.image.width,
                                 document.selection.image.height}
                          : rectangle(start_, current_);
        canvas().stroke_outline(painter, {bounds.x, bounds.y, bounds.w, bounds.h},
                                interface_color(*this, gf::Color::rgba(30, 100, 190)), 1);
    }
    paint_selection_contours(painter);
    if (dragging_ && (document.tool == Tool::Lasso || document.tool == Tool::Freehand) &&
        !moving_selection_) {
        for (std::size_t index = 1; index < lasso_.size(); ++index) {
            painter.draw_line(screen(lasso_[index - 1]), screen(lasso_[index]), interface_color(*this, gf::Color::rgba(30, 100, 190)),
                              1);
        }
    }
    if (document.curve.line_set) {
        const int hovered = hovered_curve_handle(document.curve.geometry, 11);
        for (int index = 0; index < document.curve.geometry.handle_count(); ++index) {
            gf::Point point = screen(document.curve.geometry.handle(index));
            if (index == curve_handle_) {
                painter.fill_rect({point.x - 9, point.y - 9, 18, 18},
                                  interface_color(*this, gf::Color::rgba(255, 190, 40)));
            } else if (index == hovered) {
                painter.fill_rect({point.x - 8, point.y - 8, 16, 16},
                                  interface_color(*this, gf::Color::rgba(255, 220, 130)));
            }
            if (document.curve.geometry.kind == CurveKind::Bezier) {
                painter.draw_line(
                    screen(index == 0 ? document.curve.geometry.start : document.curve.geometry.end), point,
                    interface_color(*this, gf::Color::rgba(50, 110, 180)), 1);
            }
            painter.fill_rect({point.x - 5, point.y - 5, 10, 10}, interface_color(*this, gf::Color::rgba(255, 255, 255)));
            painter.stroke_rect({point.x - 5, point.y - 5, 10, 10}, interface_color(*this, gf::Color::rgba(30, 100, 190)), 2);
        }
    }
    const int hovered_node = hovered_path_node();
    for (std::size_t index = 0; index < document.path.nodes.size(); ++index) {
        gf::Point point = screen(document.path.nodes[index]);
        if (static_cast<int>(index) == path_node_) {
            painter.fill_rounded_rect({point.x - 9, point.y - 9, 18, 18}, 9,
                                      interface_color(*this, gf::Color::rgba(255, 190, 40)));
        } else if (static_cast<int>(index) == hovered_node) {
            painter.fill_rounded_rect({point.x - 8, point.y - 8, 16, 16}, 8,
                                      interface_color(*this, gf::Color::rgba(255, 220, 130)));
        }
        painter.fill_rounded_rect({point.x - 6, point.y - 6, 12, 12}, 6, interface_color(*this, gf::Color::rgba(255, 255, 255)));
        painter.fill_rounded_rect({point.x - 5, point.y - 5, 10, 10}, 5, interface_color(*this, gf::Color::rgba(0, 120, 215)));
    }
    paint_path_swap(painter);
    paint_resize_overlay(painter);
    paint_warp_overlay(painter);
    paint_text_overlay(painter);
    paint_tool_preview(painter);
    if (settings.rotate_view) {
        const gf::Point point = canvas().rotation_handle();
        painter.fill_rounded_rect({point.x - 12, point.y - 12, 24, 24}, 12,
                                  interface_color(*this, gf::Color::rgba(247, 250, 252, 245)));
        painter.stroke_rounded_rect({point.x - 12, point.y - 12, 24, 24}, 12, interface_color(*this, gf::Color::rgba(79, 102, 125)),
                                    1);
        const gf::Color ink = interface_color(*this, gf::Color::rgba(39, 93, 141));
        painter.draw_line({point.x - 6, point.y + 3}, {point.x - 6, point.y - 5}, ink, 1.5);
        painter.draw_line({point.x - 6, point.y - 5}, {point.x + 5, point.y - 5}, ink, 1.5);
        painter.draw_line({point.x + 2, point.y - 8}, {point.x + 5, point.y - 5}, ink, 1.5);
        painter.draw_line({point.x + 2, point.y - 2}, {point.x + 5, point.y - 5}, ink, 1.5);
        painter.draw_line({point.x - 9, point.y}, {point.x - 6, point.y + 3}, ink, 1.5);
        painter.draw_line({point.x - 3, point.y}, {point.x - 6, point.y + 3}, ink, 1.5);
    }
    painter.restore();
}
bool Editor::help_shortcut() {
    if ((*menu_).is_open() || editor_dialog_) {
        return false;
    }
    execute("help");
    return true;
}
void Editor::on_attached_to_window() {
    file_timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::seconds(1));
    file_subscription_ = (*file_timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<Editor, &Editor::file_watch_tick>(*this));
    (*file_timer_).start();
    help_accelerator_ = (*attached_window())
                            .register_accelerator(*this, {gf::PhysicalKey::f1, gf::Modifier::none},
                                                  std::bind(&Editor::help_shortcut, this));
}
void Editor::on_detaching_from_window(gf::Window& former_window) noexcept {
    file_subscription_.disconnect();
    file_timer_.reset();
    recovery_subscription.disconnect();
    recovery_timer.reset();
    clear_transform_preview();
    help_accelerator_.disconnect();
    selection_frame_.disconnect();
    text_caret_frame_.disconnect();
    Control::on_detaching_from_window(former_window);
}
void Editor::ready(gf::Window&, gf::ApplicationWindowHandle handle, const std::string& initial_path) {
    handle_ = handle;
    load_custom_pattern();
    rebuild_file_menu();
    refresh();
    if (!initial_path.empty()) {
        open_file(initial_path);
    }
    start_recovery(initial_path);
}
void Editor::closing(gf::HostCloseRequest& request) {
    request.cancel = !can_replace();
    if (!request.cancel) { reset_recovery(true); }
    if (request.cancel && !pending_save_path.empty()) {
        deferred_command = "quit";
    }
}
PaintCanvas& Editor::canvas() {
    return *canvas_;
}
void Editor::close_help(gf::ButtonBase&) {
    if (show_help) {
        execute("help");
    }
}
int Editor::hit_path_node(Point point) const {
    double nearest = 12;
    int result = -1;
    for (std::size_t index = 0; index < document.path.nodes.size(); ++index) {
        Point node = document.path.nodes[index];
        double distance = std::hypot(node.x - point.x, node.y - point.y) * (*canvas_).zoom();
        if (distance < nearest) {
            nearest = distance;
            result = static_cast<int>(index);
        }
    }
    return result;
}
int Editor::hit_curve_handle(const CurveGeometry& geometry, Point point, double radius) const {
    int result = -1;
    for (int index = 0; index < geometry.handle_count(); ++index) {
        const Point handle = geometry.handle(index);
        const double distance = std::hypot(handle.x - point.x, handle.y - point.y) * (*canvas_).zoom();
        if (distance < radius) {
            radius = distance;
            result = index;
        }
    }
    return result;
}
int Editor::hovered_curve_handle(const CurveGeometry& geometry, double radius) const {
    if (!cursor_client_ || (*canvas_).has_pointer_capture()) {
        return -1;
    }
    const gui_drawing::PointF point = (*canvas_).client_to_bitmap(*cursor_client_);
    return hit_curve_handle(geometry, {point.x, point.y}, radius);
}
int Editor::hovered_path_node() const {
    if (!edit_path_nodes || document.tool != Tool::Path || !cursor_client_ || (*canvas_).has_pointer_capture()) {
        return -1;
    }
    if (path_swap_segment_ >= 0 && static_cast<std::size_t>(path_swap_segment_) < document.path.segments.size() &&
        hovered_curve_handle(document.path.segments[path_swap_segment_].geometry, 10) >= 0) {
        return -1;
    }
    const gui_drawing::PointF point = (*canvas_).client_to_bitmap(*cursor_client_);
    return hit_path_node({point.x, point.y});
}
Point Editor::snap_path_point(Point point) const {
    int index = hit_path_node(point);
    return index < 0 ? point : document.path.nodes[static_cast<std::size_t>(index)];
}
bool Editor::path_preview_point(Point& point) const {
    if (edit_path_nodes || path_swap_kind_ || document.tool != Tool::Path || !document.path.extending || path_node_ >= 0 ||
        !cursor_client_) {
        return false;
    }
    gui_drawing::PointF mapped = (*canvas_).client_to_bitmap(*cursor_client_);
    if (!document.image.contains(static_cast<int>(std::floor(mapped.x)),
                                 static_cast<int>(std::floor(mapped.y)))) {
        return false;
    }
    point = {mapped.x, mapped.y};
    point = snap_path_point(point);
    return true;
}
void Editor::publish_path_preview() {
    Point point;
    Image composed = path_preview_point(point) ? document.path_image(&point) : document.image;
    if (document.selection.active) {
        document.selection.composite_onto(composed);
    }
    publish_image(composed, *canvas_);
}
void Editor::refresh() {
    if (stamp_placement_ && (document.revision != stamp_placement_revision_ ||
                             document.atlas_epoch != stamp_placement_epoch_)) {
        reset_stamp();
    }
    if (document.atlas.kind != AtlasKind::Icon && document.atlas.kind != AtlasKind::Cursor) {
        icon_index_paint = false;
    }
    interface_themes.apply(*this, preview_interface_hue >= 0 ? preview_interface_hue : settings.interface_hue);
    if ((*custom_pattern).pixels.empty()) { (*custom_pattern).reset(8, 8); }
    document.ink.custom_pattern_revision = custom_pattern_revision;
    document.alt_ink.custom_pattern_revision = custom_pattern_revision;
    document.ink.custom_pattern = custom_pattern;
    document.alt_ink.custom_pattern = custom_pattern;
    for (SpiroPeg& peg : spiro.pegs) { peg.effect.custom_pattern = custom_pattern; }
    ++canvas_revision;
    update_selection_contours();
    const Color alpha = settings.transparency_color;
    canvas().set_transparency_colors(settings.solid_transparency ? gf::Color::rgba(alpha.r, alpha.g, alpha.b)
                                                                 : gf::Color::rgba(246, 247, 249),
                                     settings.solid_transparency ? gf::Color::rgba(alpha.r, alpha.g, alpha.b)
                                                                 : gf::Color::rgba(211, 215, 220));
    if (text.active) {
        text.refresh(document.ink.primary, document.ink.secondary);
        Image composed = document.image;
        composite(composed, text.preview, text.bounds.x, text.bounds.y);
        document.constrain_selection(composed, document.image);
        if (document.selection.active) {
            document.selection.composite_onto(composed);
        }
        publish_image(composed, *canvas_);
    } else if (document.tool == Tool::Path && document.path.extending) {
        publish_path_preview();
    } else if (stamp_placement_) {
        publish_image(stamp_placement_image(), *canvas_);
    } else if (preview_active_) {
        Image composed = preview_;
        if (document.selection.active) {
            document.selection.composite_onto(composed);
        }
        publish_image(composed, *canvas_);
    } else if ((resize_handle_ >= 0 && resize_selection_) || warp_mode_ == WarpMode::rotation) {
        publish_image(document.image, *canvas_);
    } else if (document.selection.active) {
        publish_image(document.visible_image(), *canvas_);
    } else {
        publish_image(document.image, *canvas_);
    }
    if (warp_mode_ == WarpMode::rotation || (resize_handle_ >= 0 && resize_selection_)) {
        update_transform_preview();
    }
    if (ribbon_) {
        (*ribbon_).synchronize();
    }
    if (window() && editor_dialog_) {
        std::shared_ptr<AtlasPanel> gallery =
            std::dynamic_pointer_cast<AtlasPanel>((*window()).find("atlas-gallery-panel"));
        if (gallery) {
            (*gallery).synchronize();
        }
    }
    prepare_stamp_view();
    update_status();
    invalidate(gf::Dirty::paint);
    (*canvas_).invalidate(gf::Dirty::paint);
}
void Editor::update_cursor_status() {
    if (!cursor_status_) {
        return;
    }
    std::string text = tr("X: —   Y: —");
    if (cursor_client_) {
        gui_drawing::PointF point = (*canvas_).client_to_bitmap(*cursor_client_);
        text = tr("X: ") + std::to_string(static_cast<int>(std::floor(point.x))) +
               tr("   Y: ") + std::to_string(static_cast<int>(std::floor(point.y))) + tr(" px");
    }
    (*cursor_status_).set_text(text);
}
void Editor::update_status() {
    if (!status_) {
        return;
    }
    (*status_).set_text((document.filename.empty()
                             ? tr("Untitled")
                             : std::filesystem::path(document.filename).filename().string()) +
                        (document.dirty() ? " *" : "") +
                        (icon_index_paint ? " · " + tr("Index paint") + " #" + std::to_string(icon_primary_index) +
                                                " / #" + std::to_string(icon_alt_index) : "") +
                        (background_busy() ? tr(" · Rendering…") : ""));
    (*dimensions_status_)
        .set_text(std::to_string(document.image.width) + " × " + std::to_string(document.image.height) +
                  tr(" px"));
    std::string selection;
    if (resize_handle_ >= 0 && resize_selection_) {
        selection =
            std::to_string(resize_preview_.w) + " × " + std::to_string(resize_preview_.h) + tr(" px selected");
    } else if (document.selection.active) {
        selection = std::to_string(document.selection.image.width) + " × " +
                    std::to_string(document.selection.image.height) + tr(" px selected");
    } else if (dragging_ && (document.tool == Tool::Select || document.tool == Tool::Lasso)) {
        Rect bounds = rectangle(start_, current_);
        selection = std::to_string(bounds.w) + " × " + std::to_string(bounds.h) + tr(" px selected");
    }
    (*selection_status_).set_text(selection);
    (*tool_size_status_)
        .set_text(tr("Size: ") + std::to_string(document.tool == Tool::Pencil ? 1 : document.ink.size) + tr(" px"));
    double percent = (*canvas_).zoom() * 100;
    std::ostringstream label;
    label << std::fixed << std::setprecision(percent < 10 ? 2 : 0) << percent << "%";
    (*zoom_reset_).set_text(label.str());
    synchronizing_zoom_ = true;
    (*zoom_slider_).set_value(std::log2((*canvas_).zoom()));
    synchronizing_zoom_ = false;
    update_cursor_status();
}
void Editor::rotate_view(double radians) {
    if (!std::isfinite(radians)) {
        return;
    }
    const gf::Rect viewport = canvas().client_rectangle();
    const gui_drawing::PointF center = canvas().client_to_bitmap({viewport.width / 2, viewport.height / 2});
    canvas().view_angle = std::remainder(radians, 2 * std::numbers::pi);
    const gui_drawing::PointF mapped = canvas().view_point(center);
    canvas().set_view_origin({mapped.x - viewport.width / (2 * canvas().zoom()),
                              mapped.y - viewport.height / (2 * canvas().zoom())});
    refresh();
    if (warp_active() || resize_handle_ >= 0) {
        update_transform_preview();
    }
}
void Editor::zoom_slider_changed(double value) {
    if (synchronizing_zoom_) {
        return;
    }
    gf::Rect bounds = (*canvas_).committed_arranged_bounds();
    zoom(std::exp2(value) / (*canvas_).zoom(), {bounds.width / 2, bounds.height / 2});
}
void Editor::status_clicked(gf::ButtonBase& button) {
    std::string_view id = button.stable_id().value();
    if (id == "status-tool-size") {
        open_editor_dialog(EditorDialogKind::tool_size);
        return;
    }
    if (id == "status-zoom-reset") {
        execute("actual-size");
        return;
    }
    gf::Rect bounds = (*canvas_).committed_arranged_bounds();
    zoom(id == "status-zoom-in" ? 2 : 0.5, {bounds.width / 2, bounds.height / 2});
}
void Editor::command_invoked(const gf::CommandInvocation& invocation) {
    execute(invocation.command_id);
}
void Editor::release_gesture() {
    canvas_control_stroke_ = false;
    if (canvas_actions_.size() > 3) { (*canvas_actions_[3]).set_selected(false); }
    cancel_spiro_drag();
    if (!transform_preview_stamp_) {
        end_transform_preview();
    }
    if (placing_shape_ && document.curve.base && !document.curve.line_set) {
        document.curve = {};
    }
    clear_transform_preview();
    rotating_view_ = false;
    dragging_ = false;
    indexed_gesture_ = false;
    indexed_checkpoint_ = false;
    indexed_allowed_.clear();
    placing_shape_ = false;
    panning_ = false;
    picker_pending_ = false;
    moving_selection_ = false;
    preview_active_ = false;
    curve_handle_ = -1;
    path_swap_handle_ = -1;
    path_swap_checkpoint_ = false;
    path_node_ = -1;
    resize_handle_ = -1;
    lasso_.clear();
    eraser_.clear();
    material_.clear();
    dynamic_brush_.clear();
    dither_brush_.clear();
    dither_brush_mask_.clear();
    carpet_stroke_.clear();
    (*canvas_).set_pointer_capture(false);
}
void Editor::finish_controls(bool deselect) {
    if (stamp_placement_) {
        reset_stamp();
    }
    finish_text(true);
    finish_warp(false);
    release_gesture();
    document.commit_path();
    document.commit_curve();
    if (deselect) {
        document.commit_selection();
    } else {
        document.settle_selection();
    }
}
void Editor::choose_shape(Shape shape) {
    unset_guide();
    finish_controls(false);
    document.shape = shape;
    document.tool = Tool::Shape;
    refresh();
}
void Editor::choose_tool(Tool tool) {
    if (tool == Tool::Guide && !document.selection.active &&
        (guide.active() || !guide.nodes.empty() || document.tool == Tool::Guide)) {
        unset_guide();
        refresh();
        return;
    }
    if (tool == Tool::Guide) {
        guide_previous_tool_ = document.tool == Tool::Brush || document.tool == Tool::Stamp ||
                                       document.tool == Tool::Fill || document.tool == Tool::Eraser
                                   ? document.tool
                                   : Tool::Pencil;
    }

    if (tool == Tool::Guide && document.selection.active) {
        const FloatingSelection& selected = document.selection;
        guide.clear();
        guide.selection.bounds = {selected.x, selected.y, selected.image.width, selected.image.height};
        guide.selection.coverage.resize(selected.image.pixels.size());
        for (std::size_t index = 0; index < selected.image.pixels.size(); ++index) {
            guide.selection.coverage[index] = selected.image.pixels[index].a;
        }
        guide.seal_selection_body();
        guide.nodes = mask_outline(guide.selection.coverage, selected.image.width, selected.image.height);
        for (std::size_t index = 0; index < guide.nodes.size(); ++index) {
            guide.nodes[index].x += selected.x;
            guide.nodes[index].y += selected.y;
        }
        guide.closed = true;
        guide.fill = true;
        document.commit_selection();
    } else if (tool == Tool::Guide && guide.nodes.empty()) {
        guide.fill = document.shape_fill;
    } else if (tool == Tool::Select || tool == Tool::Lasso || tool == Tool::Path || tool == Tool::Shape ||
               tool == Tool::Text || tool == Tool::Reshape) {
        unset_guide();
    }
    if (document.tool != tool) {
        if (tool != Tool::Stamp && transform_preview_stamp_) {
            end_transform_preview();
        }
        path_swap_kind_.reset();
        path_swap_segment_ = -1;
        path_swap_handle_ = -1;
        finish_controls(false);
    }
    document.tool = tool;
    if (tool == Tool::Path) {
        edit_path_nodes = false;
        path_swap_kind_.reset();
    }
    apply_tool_cursor(canvas(), tool);
    refresh();
}
void Editor::pointer(const gf::PointerEvent& event) {
    if (canvas_control_stroke_ && !canvas_control_dispatch_) { return; }
    try {
        if (panning_ && event.action != gf::PointerAction::up) {
            canvas().set_cursor(gf::CursorKind::hand);
        } else {
            apply_tool_cursor(canvas(), document.tool);
        }
        shift_ = gf::has_modifier(event.modifiers, gf::Modifier::shift);
        control_ = gf::has_modifier(event.modifiers, gf::Modifier::control);
        alt_ = gf::has_modifier(event.modifiers, gf::Modifier::alt);
        gf::Point client = (*canvas_).point_from_window(event.position);
        if (event.action == gf::PointerAction::leave && !(*canvas_).has_pointer_capture()) {
            cursor_client_.reset();
        } else {
            cursor_client_ = client;
        }
        if (settings.rotate_view) {
            const gf::Point handle = canvas().rotation_handle();
            const bool hit = std::hypot(client.x - handle.x, client.y - handle.y) <= 14;
            if (!rotating_view_ && hit && event.action == gf::PointerAction::move &&
                !canvas().has_pointer_capture()) {
                canvas().set_cursor(gf::CursorKind::hand);
                canvas().invalidate(gf::Dirty::paint);
                return;
            }
            if (!rotating_view_ && hit && event.action == gf::PointerAction::down) {
                if (event.button == gf::PointerButton::secondary) {
                    rotate_view(0);
                    refresh();
                    return;
                }
                if (event.button == gf::PointerButton::primary) {
                    finish_controls(true);
                    rotation_document_pivot_ = {document.image.width / 2.0, document.image.height / 2.0};
                    rotation_pivot_ = screen({rotation_document_pivot_.x, rotation_document_pivot_.y});
                    rotation_grab_angle_ =
                        std::atan2(client.y - rotation_pivot_.y, client.x - rotation_pivot_.x);
                    rotation_start_angle_ = canvas().view_angle;
                    rotating_view_ = true;
                    canvas().set_pointer_capture(true);
                    refresh();
                    return;
                }
            }
            if (rotating_view_) {
                if (event.action == gf::PointerAction::move || event.action == gf::PointerAction::up) {
                    if (std::hypot(client.x - rotation_pivot_.x, client.y - rotation_pivot_.y) > 2) {
                        const bool first_rotation = std::abs(canvas().view_angle) < 1e-10;
                        canvas().view_angle = std::remainder(
                            rotation_start_angle_ +
                                std::atan2(client.y - rotation_pivot_.y, client.x - rotation_pivot_.x) -
                                rotation_grab_angle_,
                            2 * std::numbers::pi);
                        const gui_drawing::PointF point = canvas().view_point(rotation_document_pivot_);
                        canvas().set_view_origin({point.x - rotation_pivot_.x / canvas().zoom(),
                                                  point.y - rotation_pivot_.y / canvas().zoom()});
                        if (first_rotation) {
                            canvas().publish_source(stamp_placement_ ? stamp_placement_image()
                                                                    : document.visible_image(), {});
                        }
                        canvas().invalidate(gf::Dirty::paint);
                    }
                    if (event.action == gf::PointerAction::up) {
                        release_gesture();
                    }
                }
                return;
            }
        }
        gui_drawing::PointF mapped = (*canvas_).client_to_bitmap(client);
        Point point{mapped.x, mapped.y};
        if (!panning_ && (document.tool == Tool::Magnifier || document.tool == Tool::Stamp) &&
            (!document.image.contains(static_cast<int>(std::floor(point.x)),
                                      static_cast<int>(std::floor(point.y))) ||
             (document.tool == Tool::Stamp && document.stamp.pixels.empty()))) {
            canvas().set_cursor(gf::CursorKind::crosshair);
        }
        update_cursor_status();
        if (!panning_ && (curve_handle_ >= 0 || path_node_ >= 0 || path_swap_handle_ >= 0 ||
            hovered_path_node() >= 0 ||
            (document.curve.line_set && hovered_curve_handle(document.curve.geometry, 11) >= 0) ||
            (document.tool == Tool::Path && (edit_path_nodes || path_swap_kind_) && path_swap_segment_ >= 0 &&
             static_cast<std::size_t>(path_swap_segment_) < document.path.segments.size() &&
             hovered_curve_handle(document.path.segments[path_swap_segment_].geometry, 10) >= 0))) {
            canvas().set_cursor(gf::CursorKind::hand);
        }
        if (document.tool == Tool::Stamp || document.tool == Tool::Pencil || document.tool == Tool::Eraser ||
            document.tool == Tool::Magnifier || document.tool == Tool::Picker ||
            document.tool == Tool::Brush || document.tool == Tool::Path || document.curve.line_set) {
            (*canvas_).invalidate(gf::Dirty::paint);
        }
        if (document.tool == Tool::Path &&
            (event.action == gf::PointerAction::move || event.action == gf::PointerAction::leave)) {
            publish_path_preview();
        }
        if (event.action == gf::PointerAction::wheel) {
            if (gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                gf::has_modifier(event.modifiers, gf::Modifier::meta)) {
                zoom(event.wheel_delta.y >= 0 ? 1.25 : 0.8, client);
            } else {
                gui_drawing::PointF origin = (*canvas_).view_origin();
                const double scale = (*canvas_).zoom();
                const gf::Rect viewport = (*canvas_).committed_arranged_bounds();
                const double half_width = viewport.width / (2 * scale);
                const double half_height = viewport.height / (2 * scale);
                const gf::Rect view = canvas().view_bounds();
                origin.x = std::clamp(origin.x - event.wheel_delta.x * settings.scroll_distance / scale,
                                      view.x - half_width, view.x + view.width - half_width);
                origin.y = std::clamp(origin.y - event.wheel_delta.y * settings.scroll_distance / scale,
                                      view.y - half_height, view.y + view.height - half_height);
                (*canvas_).set_view_origin(origin);
                if (warp_mode_ == WarpMode::rotation || (resize_handle_ >= 0 && resize_selection_)) {
                    update_transform_preview();
                }
                update_cursor_status();
                invalidate(gf::Dirty::paint);
            }
            return;
        }
        if (pick_hotspot && event.action == gf::PointerAction::down &&
            event.button == gf::PointerButton::primary) {
            document.set_hotspot(static_cast<int>(std::floor(point.x)),
                                 static_cast<int>(std::floor(point.y)));
            pick_hotspot = false;
            refresh();
            return;
        }
        if (document.tool == Tool::Picker && event.button != gf::PointerButton::middle &&
            (picker_pending_ || panning_ || event.action == gf::PointerAction::down)) {
            if (event.action == gf::PointerAction::down && !picker_pending_ && !panning_) {
                picker_pending_ = true;
                picker_secondary_ = event.button == gf::PointerButton::secondary;
                pan_start_ = client;
                pan_origin_ = (*canvas_).view_origin();
                (*canvas_).set_pointer_capture(true);
                if (window()) {
                    static_cast<void>((*window()).request_focus(canvas_));
                }
            } else if (event.action == gf::PointerAction::move) {
                if (std::hypot(client.x - pan_start_.x, client.y - pan_start_.y) >= 4) {
                    panning_ = true;
                    canvas().set_cursor(gf::CursorKind::hand);
                    picker_pending_ = false;
                }
                if (panning_) {
                    (*canvas_).set_view_origin(
                        {pan_origin_.x - (client.x - pan_start_.x) / (*canvas_).zoom(),
                         pan_origin_.y - (client.y - pan_start_.y) / (*canvas_).zoom()});
                    update_cursor_status();
                    invalidate(gf::Dirty::paint);
                }
            } else if (event.action == gf::PointerAction::up) {
                const bool sample = picker_pending_;
                const bool secondary = picker_secondary_;
                release_gesture();
                if (sample) {
                    begin(point, secondary);
                }
            }
            return;
        }
        // Between placement clicks the preview follows hover without retaining
        // native capture, so ribbon commands and Settings remain reachable.
        if (placing_shape_ && !panning_ && event.button != gf::PointerButton::middle) {
            if (event.action == gf::PointerAction::move) {
                move(point);
            } else if (event.action == gf::PointerAction::up) {
                (*canvas_).set_pointer_capture(false);
            } else if (event.action == gf::PointerAction::down) {
                if (event.button == placement_button_) {
                    end(point);
                } else if (event.button == gf::PointerButton::secondary) {
                    document.curve = {};
                    release_gesture();
                    refresh();
                }
            }
            return;
        }
        if (stamp_placement_ && !panning_ && event.button != gf::PointerButton::middle) {
            if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
                if (window()) { static_cast<void>((*window()).request_focus(canvas_)); }
                begin(point, false);
            } else if (event.action == gf::PointerAction::move && dragging_) {
                move(point);
            } else if (event.action == gf::PointerAction::up && dragging_) {
                end(point);
            }
            return;
        }
        if (spiro_pointer(event, point)) {
            return;
        }
        if (path_swap_pointer(event, point)) {
            return;
        }
        if (guide_pointer(event, point)) {
            return;
        }
        if (warp_pointer(event, point)) {
            return;
        }
        if (text.active && event.button != gf::PointerButton::middle && !panning_) {
            text_pointer(event, point);
            return;
        }
        if (resize_pointer(event, point)) {
            return;
        }
        if (event.action == gf::PointerAction::down) {
            if (window()) {
                static_cast<void>((*window()).request_focus(canvas_));
            }
            if (event.button == gf::PointerButton::middle) {
                panning_ = true;
                canvas().set_cursor(gf::CursorKind::hand);
                pan_start_ = client;
                pan_origin_ = (*canvas_).view_origin();
                (*canvas_).set_pointer_capture(true);
                return;
            }
            if (event.button != gf::PointerButton::primary && event.button != gf::PointerButton::secondary) {
                return;
            }
            if ((event.button == gf::PointerButton::secondary &&
                 (document.tool == Tool::Path || document.tool == Tool::Stamp)) ||
                (event.button == gf::PointerButton::primary && document.tool == Tool::Path && edit_path_nodes)) {
                if (document.tool == Tool::Path) {
                    int node =
                        !edit_path_nodes && document.path.extending && document.path.nodes.size() - document.path.start == 1
                            ? -1
                            : hit_path_node(point);
                    if (node >= 0) {
                        path_node_ = node;
                        Point anchor = document.path.nodes[static_cast<std::size_t>(node)];
                        handle_offset_ = {point.x - anchor.x, point.y - anchor.y};
                        handle_checkpoint_ = false;
                        dragging_ = true;
                        (*canvas_).set_pointer_capture(true);
                        refresh();
                        return;
                    }
                    if (edit_path_nodes && event.button == gf::PointerButton::primary) {
                        return;
                    }
                    document.end_path_geometry();
                } else {
                    reset_stamp();
                }
                release_gesture();
                refresh();
                return;
            }
            if (event.button == gf::PointerButton::secondary && document.tool == Tool::Shape &&
                ((document.curve.base && !document.curve.line_set) || dragging_)) {
                document.curve = {};
                release_gesture();
                refresh();
                return;
            }
            begin(point, event.button == gf::PointerButton::secondary);
            if (!settings.drag_shapes && document.tool == Tool::Shape && dragging_ && curve_handle_ < 0) {
                placing_shape_ = true;
                placement_button_ = event.button;
            }
        } else if (event.action == gf::PointerAction::move) {
            if (panning_) {
                (*canvas_).set_view_origin({pan_origin_.x - (client.x - pan_start_.x) / (*canvas_).zoom(),
                                            pan_origin_.y - (client.y - pan_start_.y) / (*canvas_).zoom()});
                update_cursor_status();
                invalidate(gf::Dirty::paint);
            } else if (dragging_) {
                move(point);
            }
        } else if (event.action == gf::PointerAction::up) {
            if (panning_) {
                panning_ = false;
                (*canvas_).set_pointer_capture(false);
            } else if (dragging_) {
                end(point);
            }
        }
    } catch (const std::exception& exception) {
        release_gesture();
        error(exception.what());
    }
}
void Editor::begin(Point point, bool secondary) {
    if (stamp_placement_) {
        stamp_placement_center_ = point;
        dragging_ = true;
        (*canvas_).set_pointer_capture(true);
        refresh();
        return;
    }
    start_ = last_ = current_ = point;
    gesture_ink_ = secondary ? document.alternate_ink() : document.primary_ink();
    gesture_fill_ink_ = secondary ? document.primary_ink() : document.body_ink();
    stabilizer_.reset(point);
    if (document.tool == Tool::Brush && brush_family == BrushFamily::Heal &&
        (set_heal_source || !healing_brush_.has_source())) {
        healing_brush_.capture(document.visible_image(), point);
        set_heal_source = false;
        refresh();
        return;
    }
    if (document.curve.line_set) {
        const int nearest_handle = hit_curve_handle(document.curve.geometry, point, 11);
        if (nearest_handle >= 0) {
            const Point handle = document.curve.geometry.handle(nearest_handle);
            curve_handle_ = nearest_handle;
            handle_offset_ = {point.x - handle.x, point.y - handle.y};
            handle_checkpoint_ = false;
            dragging_ = true;
            (*canvas_).set_pointer_capture(true);
        }
        return;
    }
    if (!document.image.contains(static_cast<int>(std::floor(point.x)),
                                 static_cast<int>(std::floor(point.y)))) {
        return;
    }
    if (document.tool == Tool::Text) {
        finish_controls(false);
        text.begin(point);
        text.bounds.w = std::max(80, std::min(440, document.image.width - text.bounds.x));
        (*ribbon_).show_tool_context();
        reset_text_caret();
        refresh();
        return;
    }
    if (document.tool == Tool::Magnifier) {
        zoom(secondary ? 0.5 : 2, screen(point));
        return;
    }
    if (begin_indexed_paint(point, secondary)) {
        return;
    }
    if (document.tool == Tool::Picker) {
        Color color = sample_color(document.visible_image(), point, picker_mode);
        if (secondary) {
            document.ink.secondary = color;
        } else {
            document.ink.primary = color;
        }
        Ink& material = secondary ? document.alt_ink : document.ink;
        if (material.pattern == Pattern::None) {
            select_pattern(material, Pattern::Solid);
        }
        refresh();
        return;
    }
    if (document.tool == Tool::Shape && document.shape == Shape::Polygon) {
        document.tool = Tool::Path;
        document.continuous_path = false;
    }
    if (document.tool == Tool::Lasso && lasso_mode == LassoMode::Wand) {
        document.commit_selection();
        document.select_mask(similar_colors(document.image, point, lasso_tolerance));
        (*ribbon_).show_tool_context();
        refresh();
        return;
    }
    if (document.tool == Tool::Freehand) {
        document.settle_selection();
        paint_base_ = document.image;
        lasso_ = {point};
        dragging_ = true;
        (*canvas_).set_pointer_capture(true);
        return;
    }
    if (document.tool == Tool::Path) {
        int node = hit_path_node(point);
        point = snap_path_point(point);
        const bool joining = node >= 0 && document.path.extending;
        const bool same_tip =
            joining && point.x == document.path.nodes.back().x && point.y == document.path.nodes.back().y;
        if (!same_tip) {
            document.add_path_node(point);
        }
        // Retained anchors are destinations as well as starting points. Finish
        // only after committing the segment shown by the snapped preview.
        if (joining) {
            document.end_path_geometry();
        }
        refresh();
        return;
    }
    if (document.tool == Tool::Stamp) {
        document.settle_selection();
        paint_base_ = document.image;
        bool loaded = !document.stamp.pixels.empty() && !stamp_pending_ && !stamp_preview_.pixels.empty();
        stamp_at(point);
        if (loaded) {
            dragging_ = true;
            (*canvas_).set_pointer_capture(true);
        }
        refresh();
        return;
    }
    if (document.tool == Tool::Select || document.tool == Tool::Lasso) {
        Rect bounds{document.selection.x, document.selection.y, document.selection.image.width,
                    document.selection.image.height};
        selection_edit_ = document.tool == Tool::Lasso ? (alt_ ? -1 : control_ ? 1 : 0) : 0;
        moving_selection_ = !selection_edit_ && document.selection.active && point_inside(bounds, point);
        if (moving_selection_ && !document.selection.coverage.empty()) {
            const int x = static_cast<int>(point.x) - bounds.x, y = static_cast<int>(point.y) - bounds.y;
            moving_selection_ = document.selection.coverage[static_cast<std::size_t>(y) * bounds.w + x] != 0;
        }
        if (moving_selection_) {
            document.lift_selection();
            selection_offset_ = {point.x - bounds.x, point.y - bounds.y};
        } else {
            if (!selection_edit_) {
                document.commit_selection();
            }
            lasso_ = {point};
        }
    } else if (document.tool == Tool::Shape &&
               (document.shape == Shape::Bezier || document.shape == Shape::Arc)) {
        if (!document.curve.base) {
            document.begin_curve(document.shape == Shape::Arc ? CurveKind::Arc : CurveKind::Bezier, point,
                                 secondary);
        }
    } else {
        document.settle_selection();
        paint_base_ = document.image;
        if (document.tool != Tool::Shape &&
            !(document.tool == Tool::Stamp && document.stamp.pixels.empty())) {
            document.checkpoint();
        }
        eraser_.clear();
        material_.clear();
        dynamic_brush_.clear();
    dither_brush_.clear();
    dither_brush_mask_.clear();
        carpet_stroke_.clear();
        if (document.tool == Tool::Brush && brush_family == BrushFamily::Dither) {
            dither_brush_mask_ = canvas_selection_mask();
        }
        // A fresh stochastic deposit each gesture; pattern/shape materials retain
        // their authored seed and remain independent of brush-only dynamics.
        if (document.tool == Tool::Brush) {
            gesture_ink_.noise += static_cast<std::uint32_t>(document.revision * 104729);
        }
        transform_brush_.begin(document.image, point);
        if (document.tool == Tool::Brush && brush_family == BrushFamily::Heal) {
            healing_brush_.begin(document.image, point, heal_options);
        }
        if (document.tool == Tool::Pencil) {
            gesture_ink_ = pencil_ink(gesture_ink_);
        }
    }
    dragging_ = true;
    (*canvas_).set_pointer_capture(true);
    move(point);
}
void Editor::move(Point point) {
    if (stamp_placement_) {
        stamp_placement_center_ = point;
        refresh();
        return;
    }
    if (indexed_gesture_) {
        move_indexed_paint(point);
        return;
    }
    if (stabilize && (document.tool == Tool::Pencil || document.tool == Tool::Brush)) {
        point = stabilizer_.advance(point, stabilizer_lag);
    }
    if (shift_ && document.tool == Tool::Shape && curve_handle_ < 0 &&
        !(control_ && radial_shape(document.shape))) {
        Point anchor = document.curve.base ? document.curve.geometry.start : start_;
        double dx = point.x - anchor.x, dy = point.y - anchor.y;
        if (document.shape == Shape::Line || document.shape == Shape::Bezier ||
            document.shape == Shape::Arc) {
            double angle = std::round(std::atan2(dy, dx) / (std::numbers::pi / 4)) * (std::numbers::pi / 4);
            double length = std::hypot(dx, dy);
            point = {anchor.x + length * std::cos(angle), anchor.y + length * std::sin(angle)};
        } else {
            double extent = std::max(std::abs(dx), std::abs(dy));
            point = {anchor.x + std::copysign(extent, dx), anchor.y + std::copysign(extent, dy)};
        }
    }
    current_ = point;
    if (path_node_ >= 0) {
        Point next{point.x - handle_offset_.x, point.y - handle_offset_.y};
        Point before = document.path.nodes[static_cast<std::size_t>(path_node_)];
        if (std::hypot(next.x - before.x, next.y - before.y) > 1e-9) {
            if (!handle_checkpoint_) {
                document.checkpoint();
                handle_checkpoint_ = true;
            }
            document.move_path_node(static_cast<std::size_t>(path_node_), next);
        }
    } else if (curve_handle_ >= 0) {
        Point next{point.x - handle_offset_.x, point.y - handle_offset_.y};
        Point before = document.curve.geometry.handle(curve_handle_);
        if (std::hypot(next.x - before.x, next.y - before.y) > 1e-9) {
            if (!handle_checkpoint_) {
                document.checkpoint();
                handle_checkpoint_ = true;
            }
            document.curve.geometry.move_handle(curve_handle_, next);
            document.sync_curve();
        }
    } else if (moving_selection_) {
        document.selection.x = static_cast<int>(std::round(point.x - selection_offset_.x));
        document.selection.y = static_cast<int>(std::round(point.y - selection_offset_.y));
    } else if (document.tool == Tool::Select || document.tool == Tool::Lasso) {
        if (document.tool == Tool::Lasso) {
            lasso_.push_back(point);
        }
    } else if (document.tool == Tool::Freehand) {
        if (lasso_.empty() || std::hypot(point.x - lasso_.back().x, point.y - lasso_.back().y) >= 0.5) {
            lasso_.push_back(point);
        }
        last_ = point;
        canvas().invalidate(gf::Dirty::paint);
        update_status();
        return;
    } else if (document.tool == Tool::Shape) {
        if (document.curve.base) {
            preview_ = document.curve_image(&point);
        } else {
            preview_ = document.image;
            Point first = start_, last = point;
            if (control_ && centered_shape(document.shape)) {
                double radius_x = std::abs(point.x - start_.x), radius_y = std::abs(point.y - start_.y);
                if (radial_shape(document.shape)) {
                    radius_x = radius_y = std::hypot(point.x - start_.x, point.y - start_.y);
                }
                first = {start_.x - radius_x, start_.y - radius_y};
                last = {start_.x + radius_x, start_.y + radius_y};
            }
            draw_shape(preview_, document.shape, first, last, gesture_ink_, document.shape_outline,
                       document.shape_fill, gesture_fill_ink_.brush, &gesture_fill_ink_);
        }
        document.constrain_selection(preview_, document.image);
        preview_active_ = true;
    } else if (document.tool == Tool::Stamp) {
        // Deposit at each pixel along the gesture, even between sparse pointer events.
        double distance = std::max(std::abs(point.x - last_.x), std::abs(point.y - last_.y));
        int steps = static_cast<int>(std::ceil(distance));
        for (int step = 1; step <= steps; ++step) {
            double fraction = static_cast<double>(step) / steps;
            stamp_at({last_.x + (point.x - last_.x) * fraction, last_.y + (point.y - last_.y) * fraction},
                     false);
        }
    } else if (document.tool == Tool::Pencil || document.tool == Tool::Brush ||
               document.tool == Tool::Eraser || document.tool == Tool::Fill) {
        paint_segment(last_, point);
        if (document.tool == Tool::Fill) {
            dragging_ = false;
            (*canvas_).set_pointer_capture(false);
        }
    }
    if (document.tool == Tool::Pencil || document.tool == Tool::Brush || document.tool == Tool::Fill ||
        document.tool == Tool::Stamp) {
        constrain_paint(document.image, paint_base_, guide, atlas_painting() && atlas_preserve_alpha);
    }
    if (document.tool == Tool::Eraser && atlas_painting() && atlas_preserve_alpha) {
        constrain_paint(document.image, paint_base_, Guide{}, true);
    }
    if (document.tool == Tool::Pencil || document.tool == Tool::Brush || document.tool == Tool::Fill ||
        document.tool == Tool::Stamp || document.tool == Tool::Eraser) {
        document.constrain_selection(document.image, paint_base_);
    }
    ++canvas_revision;

    if ((document.tool == Tool::Pencil || document.tool == Tool::Brush || document.tool == Tool::Eraser) &&
        curve_handle_ < 0 && !moving_selection_) {
        int margin = gesture_ink_.size + 3;
        if (document.tool == Tool::Brush && brush_family == BrushFamily::Heal) {
            margin = gesture_ink_.size * 2 + 5;
        }
        Rect damage = rectangle(last_, point);
        damage.x -= margin;
        damage.y -= margin;
        damage.w += margin * 2;
        damage.h += margin * 2;
        publish_image(document.selection.active && !document.selection.on_canvas ? document.visible_image()
                                                                                 : document.image,
                      *canvas_, atlas_painting() && atlas_wrap ? Rect{} : damage);
        last_ = point;
        update_status();
    } else {
        last_ = point;
        refresh();
    }
}
void Editor::end(Point point) {
    if (stamp_placement_) {
        stamp_placement_center_ = point;
        release_gesture();
        refresh();
        return;
    }
    move(point);
    point = current_;
    if (curve_handle_ < 0 && !moving_selection_) {
        if (document.tool == Tool::Select || document.tool == Tool::Lasso) {
            Rect bounds = rectangle(start_, point);
            if (document.tool == Tool::Lasso) {
                Point low = start_, high = start_;
                for (std::size_t index = 0; index < lasso_.size(); ++index) {
                    low.x = std::min(low.x, lasso_[index].x);
                    low.y = std::min(low.y, lasso_[index].y);
                    high.x = std::max(high.x, lasso_[index].x);
                    high.y = std::max(high.y, lasso_[index].y);
                }
                bounds = rectangle(low, high);
            }
            if (std::hypot(point.x - start_.x, point.y - start_.y) > 0.5 || lasso_.size() > 3) {
                if (document.tool == Tool::Lasso && selection_edit_) {
                    document.edit_selection(lasso_, selection_edit_ < 0);
                } else if (document.tool == Tool::Lasso && lasso_mode != LassoMode::Free) {
                    document.select_mask(tighten_lasso(document.image, lasso_,
                                                       lasso_mode == LassoMode::InnerVoid, lasso_tolerance));
                } else {
                    document.select(bounds, document.tool == Tool::Lasso ? lasso_ : std::vector<Point>{});
                }
            }
        } else if (document.tool == Tool::Freehand) {
            if (lasso_.size() >= 3 && (document.shape_outline || document.shape_fill)) {
                preview_ = paint_base_;
                polygon(preview_, lasso_, gesture_ink_, document.shape_outline, document.shape_fill, true,
                        gesture_fill_ink_.brush, &gesture_fill_ink_);
                constrain_paint(preview_, paint_base_, guide, atlas_painting() && atlas_preserve_alpha);
                document.constrain_selection(preview_, paint_base_);
                document.checkpoint();
                document.image = std::move(preview_);
            }
        } else if (document.tool == Tool::Shape) {
            if (document.curve.base) {
                static_cast<void>(document.establish_curve(point));
            } else {
                document.checkpoint();
                document.image = preview_;
            }
        }
    }
    if ((document.tool == Tool::Select || document.tool == Tool::Lasso) && document.selection.active) {
        (*ribbon_).show_tool_context();
    }
    bool stroke_tool =
        document.tool == Tool::Pencil || document.tool == Tool::Brush || document.tool == Tool::Eraser;
    release_gesture();
    if (stroke_tool) {
        update_status();
    } else {
        refresh();
    }
}
void Editor::zoom(double factor, gf::Point anchor) {
    gui_drawing::PointF before = (*canvas_).RasterCanvas::client_to_bitmap(anchor);
    double value = std::clamp((*canvas_).zoom() * factor, 0.0625, 32.0);
    gf::Point translation{anchor.x - before.x * value, anchor.y - before.y * value};
    if (factor < 1) {
        // Focal zoom followed by the recovered viewer's independent edge clamp:
        // t' = (V - Wz') / 2 when Wz' <= V, otherwise clamp(t~, V - Wz', 0).
        // There is no additional centerward interpolation.
        gf::Rect viewport = (*canvas_).client_rectangle();
        const gf::Rect view = canvas().view_bounds();
        double width = view.width * value, height = view.height * value;
        translation.x += view.x * value;
        translation.y += view.y * value;
        translation.x = width <= viewport.width ? (viewport.width - width) / 2
                                                : std::clamp(translation.x, viewport.width - width, 0.0);
        translation.y = height <= viewport.height ? (viewport.height - height) / 2
                                                  : std::clamp(translation.y, viewport.height - height, 0.0);
        translation.x -= view.x * value;
        translation.y -= view.y * value;
    }
    (*canvas_).set_view(value, {-translation.x / value, -translation.y / value});
    refresh();
}
void Editor::on_key_preview(gf::KeyEvent& event) {
    if (pattern_editing) { return; }
    if (event.action != gf::KeyAction::down || (*menu_).is_open() || editor_dialog_) {
        return;
    }
    if (event.physical_key == gf::PhysicalKey::f1) {
        execute("help");
        event.handled = true;
        return;
    }
    if (window() && (*window()).focused_control() == canvas_ && text.active && text_key(event)) {
        return;
    }
    if (stamp_placement_ && event.physical_key == gf::PhysicalKey::escape) {
        cancel_stamp_placement();
        event.handled = true;
        return;
    }
    if (window() && (*window()).focused_control() != canvas_) {
        gf::Control::Ptr focused = (*window()).focused_control();
        if (focused) {
            gf::SemanticRole role = (*focused).semantic_descriptor().role;
            if (role == gf::SemanticRole::text_box || role == gf::SemanticRole::numeric_field) {
                return;
            }
        }
    }
    if (settings.canvas_controls && window() && (*window()).focused_control() == canvas_ &&
        event.modifiers == gf::Modifier::none) {
        int action = -1;
        if (event.physical_key == gf::PhysicalKey::left) { action = 6; }
        if (event.physical_key == gf::PhysicalKey::right) { action = 7; }
        if (event.physical_key == gf::PhysicalKey::up) { action = 8; }
        if (event.physical_key == gf::PhysicalKey::down) { action = 9; }
        if (event.physical_key == gf::PhysicalKey::space) { action = 1; }
        if (action >= 0) { canvas_control_action(action); event.handled = true; return; }
    }
    bool command = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                   gf::has_modifier(event.modifiers, gf::Modifier::meta);
    bool shift = gf::has_modifier(event.modifiers, gf::Modifier::shift);
    std::string action;
    if (event.physical_key == gf::PhysicalKey::f1) {
        action = "help";
    } else if (event.physical_key == gf::PhysicalKey::f11) {
        action = "full-screen";
    } else if (event.physical_key == gf::PhysicalKey::f12) {
        action = "save-as";
    } else if (command) {
        switch (event.physical_key) {
        case gf::PhysicalKey::p:
            action = "print";
            break;
        case gf::PhysicalKey::e:
            action = "properties";
            break;
        case gf::PhysicalKey::w:
            action = "resize";
            break;
        case gf::PhysicalKey::i:
            action = "invert";
            break;
        case gf::PhysicalKey::g:
            action = "show-grid";
            break;
        case gf::PhysicalKey::n:
            action = "new";
            break;
        case gf::PhysicalKey::o:
            action = "open";
            break;
        case gf::PhysicalKey::s:
            action = shift ? "save-as" : "save";
            break;
        case gf::PhysicalKey::z:
            action = shift ? "redo" : "undo";
            break;
        case gf::PhysicalKey::y:
            action = "redo";
            break;
        case gf::PhysicalKey::c:
            action = "copy";
            break;
        case gf::PhysicalKey::x:
            action = "cut";
            break;
        case gf::PhysicalKey::v:
            action = "paste";
            break;
        case gf::PhysicalKey::a:
            action = "select-all";
            break;
        default:
            break;
        }
    } else if (window() && (*window()).focused_control() == canvas_) {
        if (event.physical_key == gf::PhysicalKey::escape) {
            pick_hotspot = false;
            action = "release";
        } else if (event.physical_key == gf::PhysicalKey::enter) {
            action = stamp_placement_ ? "stamp-apply" : "finish-path";
        } else if (event.physical_key == gf::PhysicalKey::delete_forward ||
                   event.physical_key == gf::PhysicalKey::backspace) {
            action = "delete";
        } else if (!gf::has_modifier(event.modifiers, gf::Modifier::alt) &&
                   (event.physical_key == gf::PhysicalKey::left ||
                    event.physical_key == gf::PhysicalKey::right ||
                    event.physical_key == gf::PhysicalKey::up ||
                    event.physical_key == gf::PhysicalKey::down)) {
            gui_drawing::PointF origin = canvas().view_origin();
            const double step = (shift ? 128.0 : 32.0) / canvas().zoom();
            if (event.physical_key == gf::PhysicalKey::left) {
                origin.x -= step;
            }
            if (event.physical_key == gf::PhysicalKey::right) {
                origin.x += step;
            }
            if (event.physical_key == gf::PhysicalKey::up) {
                origin.y -= step;
            }
            if (event.physical_key == gf::PhysicalKey::down) {
                origin.y += step;
            }
            canvas().set_view_origin(origin);
            update_cursor_status();
            if (warp_active() || resize_handle_ >= 0) {
                update_transform_preview();
            }
            invalidate(gf::Dirty::paint);
            event.handled = true;
            return;
        } else if (document.atlas.kind != AtlasKind::None && !document.selection.active && !warp_active() &&
                   !dragging_ &&
                   (event.physical_key == gf::PhysicalKey::left ||
                    event.physical_key == gf::PhysicalKey::right)) {
            action = event.physical_key == gf::PhysicalKey::left ? "atlas-previous" : "atlas-next";
        } else if (document.tool == Tool::Stamp && !document.stamp.pixels.empty()) {
            if (event.physical_key == gf::PhysicalKey::r) {
                stamp_angle = std::remainder(stamp_angle + (shift ? -15 : 15), 360.0);
            } else if ((event.physical_key == gf::PhysicalKey::equal ||
                        event.physical_key == gf::PhysicalKey::keypad_plus)) {
                stamp_scale = std::min(8.0, stamp_scale * 1.1);
            } else if ((event.physical_key == gf::PhysicalKey::minus ||
                        event.physical_key == gf::PhysicalKey::keypad_minus)) {
                stamp_scale = std::max(0.1, stamp_scale / 1.1);
            } else {
                return;
            }
            regenerate_stamp();
            refresh();
            event.handled = true;
            return;
        } else if (document.selection.active && !warp_active() &&
                   (event.physical_key == gf::PhysicalKey::left ||
                    event.physical_key == gf::PhysicalKey::right ||
                    event.physical_key == gf::PhysicalKey::up ||
                    event.physical_key == gf::PhysicalKey::down)) {
            document.lift_selection();
            int step = shift ? 10 : 1;
            if (event.physical_key == gf::PhysicalKey::left) {
                document.selection.x -= step;
            } else if (event.physical_key == gf::PhysicalKey::right) {
                document.selection.x += step;
            } else if (event.physical_key == gf::PhysicalKey::up) {
                document.selection.y -= step;
            } else if (event.physical_key == gf::PhysicalKey::down) {
                document.selection.y += step;
            } else {
                return;
            }
            event.handled = true;
            refresh();
            return;
        }
    }
    if (!action.empty()) {
        event.handled = true;
        execute(action);
    }
}
gf::HostServices& Editor::services() {
    if (!window() || !(*window()).host_services()) {
        throw std::runtime_error(tr("Native host services are not attached"));
    }
    return *(*window()).host_services();
}
gf::HostDialogResult Editor::dialog(gf::HostDialogRequestPayload payload) {
    gf::HostDialogRequest request;
    request.request_id = next_dialog_id_++;
    request.payload = std::move(payload);
    gf::HostDialogResult result = services().show_dialog(request);
    if (!result.status.accepted()) {
        throw std::runtime_error(tr("The native dialog service could not complete the request"));
    }
    return result;
}
void Editor::error(const std::string& message) {
    if (status_) {
        (*status_).set_text(message);
    }
    if (window() && (*window()).host_services()) {
        gf::HostMessageDialogRequest request;
        request.title = "Plan Paint";
        request.message = message;
        request.icon = gf::HostMessageIcon::error;
        try {
            static_cast<void>(dialog(request));
        } catch (const std::exception&) {
            // The status line remains usable even if native modal services fail.
        }
    }
}

bool Editor::can_replace() {
    finish_text(true);
    finish_warp(false);
    if (!document.dirty()) {
        return true;
    }
    gf::HostMessageDialogRequest request;
    request.title = tr("Save changes?");
    request.message =
        tr("Save changes to ") + (document.filename.empty() ? std::string(tr("Untitled")) : document.filename) + "?";
    request.buttons = gf::HostMessageButtons::yes_no_cancel;
    request.default_choice = gf::HostDialogChoice::cancel;
    try {
        gf::HostMessageDialogResult result = std::get<gf::HostMessageDialogResult>(dialog(request).payload);
        if (result.choice == gf::HostDialogChoice::no) {
            return true;
        }
        return result.choice == gf::HostDialogChoice::yes && save(false);
    } catch (const std::exception& exception) {
        error(exception.what());
        return false;
    }
}
void Editor::open_file(const std::string& path) {
    const FileStamp before = file_stamp(path);
    ImageContainer loaded = load_container(path);
    if (before.availability != FileAvailability::Available || file_stamp(path) != before) {
        throw std::runtime_error(tr("File changed while reading. Try again."));
    }
    open_container(std::move(loaded), path);
    watched_stamp_ = before;
    disk_stamp_ = before;
}
void Editor::open_container(ImageContainer image, const std::string& path) {
    finish_controls();
    unset_guide();
    atlas_reference = {};
    reference_frame = -1;
    healing_brush_.clear();
    document.replace_container(std::move(image), path);
    reset_file_watch();
    spiro = {};
    if (document.tool == Tool::Spirograph) {
        document.tool = Tool::Pencil;
    }
    reset_recovery(true, path);
    recent.remember(path);
    rebuild_file_menu();
    (*canvas_).set_view(1, {-16, -16});
    refresh();
    if (document.atlas.kind != AtlasKind::None) {
        (*ribbon_).show_atlas();
    } else if (sprite_sheet_filename(path)) {
        execute("atlas-grid");
    }
}
void Editor::open_generated_image(Image image) {
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height) {
        throw std::runtime_error("The generated image is empty or incomplete.");
    }
    close_editor_dialog();
    if (!can_replace()) {
        if (!pending_save_path.empty()) {
            deferred_generated_image = std::move(image);
        }
        return;
    }
    install_generated_image(std::move(image));
}
void Editor::install_generated_image(Image image) {
    ImageContainer container;
    container.frames.push_back({std::move(image), 0, 0, {}});
    open_container(std::move(container), "");
    // Generated artwork has no backing file, even before the first paint stroke.
    document.revision = document.next_revision++;
    refresh();
}
bool Editor::save(bool save_as) {
    finish_warp(false);
    finish_text(true);
    std::string path = document.filename;
    if (save_as || path.empty() || !writable_image_path(path)) {
        gf::HostSaveFileDialogRequest request;
        request.title = tr("Save image");
        request.suggested_name =
            path.empty() ? "Untitled.png" : std::filesystem::path(path).filename().string();
        request.default_extension = "png";
        request.filters = {{tr("Images"), {"png", "bmp", "jpg", "jpeg", "tif", "tiff", "tga", "ico", "cur"}}};
        gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(dialog(request).payload);
        if (result.outcome != gf::HostDialogOutcome::accepted || result.paths.empty()) {
            return false;
        }
        path = result.paths.front();
    }
    std::string extension = image_extension(path);
    if ((extension == ".ico" || extension == ".cur") && document.atlas.kind != AtlasKind::Icon &&
        document.atlas.kind != AtlasKind::Cursor) {
        open_editor_dialog(extension == ".cur" ? EditorDialogKind::cursor_sizes
                                               : EditorDialogKind::icon_sizes);
        pending_save_path = path;
        return false;
    }
    return save_path(path);
}
std::string Editor::choose_export_directory(const std::string& initial_directory) {
    const gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(
        dialog(gf::HostFolderDialogRequest{tr("Destination folder"), initial_directory}).payload);
    return result.outcome == gf::HostDialogOutcome::accepted && !result.paths.empty() ? result.paths.front()
                                                                                      : "";
}
std::string Editor::choose_asset_directory(const std::string& initial_directory) {
    const gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(
        dialog(gf::HostFolderDialogRequest{tr("Browse assets"), initial_directory}).payload);
    return result.outcome == gf::HostDialogOutcome::accepted && !result.paths.empty() ? result.paths.front()
                                                                                      : "";
}
void Editor::copy_asset_path(const std::string& path) {
    if (!services().write_clipboard_text(path).accepted()) {
        throw std::runtime_error(tr("Could not copy the asset path."));
    }
}
void Editor::open_browser_asset(const AssetPreview& preview) {
    if (!asset_unchanged(preview)) {
        throw std::runtime_error(tr("Asset changed while loading. Refresh its preview."));
    }
    ImageContainer prepared = preview.images;
    close_editor_dialog();
    if (!can_replace()) {
        if (!pending_save_path.empty()) {
            deferred_open_path = preview.path;
        }
        return;
    }
    if (!asset_unchanged(preview)) {
        error(tr("Asset changed while loading. Refresh its preview."));
        return;
    }
    try {
        open_container(std::move(prepared), preview.path);
    } catch (const std::exception& exception) {
        error(exception.what());
    }
}
bool Editor::save_export_report(const std::string& report, const std::string& suggested_name) {
    gf::HostSaveFileDialogRequest request;
    request.title = tr("Save report…");
    request.suggested_name = suggested_name;
    request.default_extension = "json";
    request.filters = {{"JSON", {"json"}}};
    const gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(dialog(request).payload);
    if (result.outcome != gf::HostDialogOutcome::accepted || result.paths.empty()) {
        return false;
    }
    std::string path = result.paths.front();
    if (image_extension(path).empty()) {
        path += ".json";
    }
    if (image_extension(path) != ".json") {
        throw std::runtime_error(tr("Choose a JSON filename for this report."));
    }
    const std::vector<std::uint8_t> bytes(report.begin(), report.end());
    save_encoded_bytes(bytes, path);
    return true;
}
bool Editor::export_toolbar_strip(const Image& image, bool bmp, const std::string& source_path) {
    const std::string extension = bmp ? "bmp" : "png";
    gf::HostSaveFileDialogRequest request;
    request.title = tr("Toolbar strip"); request.suggested_name = "toolbar-strip."+extension;
    request.default_extension = extension; request.filters = {{bmp ? "BMP" : "PNG", {extension}}};
    const gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(dialog(request).payload);
    if (result.outcome != gf::HostDialogOutcome::accepted || result.paths.empty()) { return false; }
    std::string path = result.paths.front();
    if (image_extension(path).empty()) { path += "."+extension; }
    if (image_extension(path) != "."+extension) { throw std::runtime_error("Filename does not match the reviewed output format."); }
    if (!source_path.empty()) {
        std::error_code error;
        const bool same_file = std::filesystem::equivalent(source_path,path,error);
        if (same_file || std::filesystem::absolute(source_path).lexically_normal() == std::filesystem::absolute(path).lexically_normal()) {
            throw std::runtime_error(tr("Export to a separate file; the source cannot be replaced."));
        }
    }
    save_image(image,path);
    return true;
}
bool Editor::export_png_image(const Image& image, const std::string& suggested_name) {
    gf::HostSaveFileDialogRequest request;
    request.title = tr("Export PNG");
    request.suggested_name = suggested_name;
    request.default_extension = "png";
    request.filters = {{"PNG", {"png"}}};
    const gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(dialog(request).payload);
    if (result.outcome != gf::HostDialogOutcome::accepted || result.paths.empty()) {
        return false;
    }
    std::string path = result.paths.front();
    if (image_extension(path).empty()) {
        path += ".png";
    }
    if (image_extension(path) != ".png") {
        throw std::runtime_error(tr("Choose a PNG filename for this export."));
    }
    save_image(image, path);
    return true;
}
bool Editor::save_path(const std::string& path) {
    std::error_code path_error;
    const bool same_file = path == watched_path_ || (!watched_path_.empty() &&
        std::filesystem::equivalent(path_from_utf8(path), path_from_utf8(watched_path_), path_error));
    if (same_file && !watched_path_.empty() && file_stamp(watched_path_) != watched_stamp_ &&
        !confirm_disk_action(tr("The file changed or became unavailable on disk. Overwrite it with local artwork?"))) {
        return false;
    }
    document.sync_curve();
    document.sync_path();
    // Saving a selection or retained curve must not release its editing session.
    std::string extension = image_extension(path);
    if (extension == ".ico" || extension == ".cur") {
        save_container(document.output_container(extension == ".cur"), path);
    } else {
        save_image(document.output_image(), path, jpeg_quality);
    }
    document.filename = path;
    document.saved_revision = document.revision;
    reset_file_watch();
    try {
        settle_recovery();
        if (recovery_session) { (*recovery_session).discard(); }
    } catch (const std::exception& exception) { recovery_notice = exception.what(); }
    recovery_metadata.saved_ms = recovery_time_ms();
    recovery_metadata.source_path = path;
    recovery_capture_revision = 0;
    recent.remember(path);
    rebuild_file_menu();
    refresh();
    return true;
}
void Editor::copy_alpha_image(const Image& source) {
    const Image image = alpha_image(source);
    gf::HostImageView view{static_cast<unsigned>(image.width), static_cast<unsigned>(image.height),
                           static_cast<std::uint64_t>(image.width) * 4,
                           std::as_bytes(std::span<const Color>(image.pixels))};
    if (!services().write_clipboard_image(view).accepted()) {
        throw std::runtime_error(tr("Could not copy the image to the clipboard"));
    }
}
Image Editor::read_alpha_mask() {
    const gf::HostClipboardImageResult result = services().read_clipboard_image();
    if (!result.status.accepted()) {
        throw std::runtime_error(tr("Could not read the image clipboard"));
    }
    return result.image.has_value() ? validated_clipboard_image(*result.image) : Image{};
}
void Editor::copy() {
    Image image = document.selection.active ? document.selected_image() : document.visible_image();
    gf::HostImageView view{static_cast<unsigned>(image.width), static_cast<unsigned>(image.height),
                           static_cast<std::uint64_t>(image.width) * 4,
                           std::as_bytes(std::span<const Color>(image.pixels))};
    if (!services().write_clipboard_image(view).accepted()) {
        throw std::runtime_error(tr("Could not copy the image to the clipboard"));
    }
}
void Editor::paste() {
    gf::HostClipboardFilesResult files = services().read_clipboard_files();
    if (!files.status.accepted() && files.status.error != gf::HostServiceError::unsupported) {
        throw std::runtime_error(tr("Could not read files from the clipboard"));
    }
    if (!files.paths_utf8.empty()) {
        Image image = load_image(files.paths_utf8.front());
        finish_controls();
        const Point origin = canvas().visible_paste_origin(document.image.width, document.image.height);
        document.paste(image, static_cast<int>(origin.x), static_cast<int>(origin.y));
        if (window()) {
            static_cast<void>((*window()).request_focus(canvas_));
        }
        refresh();
        return;
    }
    gf::HostClipboardImageResult result = services().read_clipboard_image();
    if (!result.status.accepted()) {
        throw std::runtime_error(tr("Could not read the image clipboard"));
    }
    if (!result.image.has_value()) {
        return;
    }
    Image image = validated_clipboard_image(*result.image);
    finish_controls();
    const Point origin = canvas().visible_paste_origin(document.image.width, document.image.height);
    document.paste(image, static_cast<int>(origin.x), static_cast<int>(origin.y));
    if (window()) {
        static_cast<void>((*window()).request_focus(canvas_));
    }
    refresh();
}
void Editor::open_editor_dialog(EditorDialogKind kind, bool secondary) {
    if (!attached_window()) {
        return;
    }
    (*ribbon_).close_popup();
    close_editor_dialog();
    editor_dialog_ = gf::make_control<EditorDialog>(
        gf::StableId("editor-dialog"), std::static_pointer_cast<Editor>(shared_from_this()), kind, secondary);
    interface_themes.apply(*editor_dialog_, settings.interface_hue);
    editor_dialog_popup_ = (*attached_window()).open_popup(shared_from_this(), editor_dialog_);
    editor_dialog_focus_ = (*attached_window()).begin_focus_scope(editor_dialog_);
    if (kind == EditorDialogKind::atlas_gallery) {
        std::shared_ptr<AtlasPanel> gallery =
            std::dynamic_pointer_cast<AtlasPanel>((*attached_window()).find("atlas-gallery-panel"));
        if (gallery) {
            (*gallery).reveal_current();
        }
    }
}
void Editor::close_editor_dialog() {
    if (preview_interface_hue >= 0) {
        preview_interface_hue = -1;
        interface_themes.apply(*this, settings.interface_hue);
        invalidate(gf::Dirty::paint);
        (*ribbon_).invalidate(gf::Dirty::paint);
    }
    pending_save_path.clear();
    deferred_command.clear();
    deferred_open_path.clear();
    deferred_generated_image = {};
    if (editor_dialog_focus_ && attached_window()) {
        static_cast<void>((*attached_window()).end_focus_scope(editor_dialog_focus_));
        editor_dialog_focus_ = {};
    }
    editor_dialog_popup_.disconnect();
    editor_dialog_.reset();
}
void Editor::complete_deferred_save() {
    std::string command = deferred_command, path = deferred_open_path;
    Image generated = std::move(deferred_generated_image);
    close_editor_dialog();
    if (!generated.pixels.empty()) {
        install_generated_image(std::move(generated));
    } else if (!path.empty()) {
        open_file(path);
    } else if (!command.empty()) {
        execute(command);
    }
}
void Editor::on_drag(gf::DragEvent& event) {
    if (pattern_editing) { return; }
    if (editor_dialog_ || !gf::has_drag_effect(event.allowed_effects, gf::DragEffect::copy)) {
        return;
    }
    for (std::size_t i = 0; i < event.items.size(); ++i) {
        const gf::DragFileListData* files = std::get_if<gf::DragFileListData>(&event.items[i]);
        if (!files || (*files).paths_utf8.empty()) {
            continue;
        }
        event.accepted_effect = gf::DragEffect::copy;
        event.handled = true;
        if (event.action == gf::DragAction::drop) {
            try {
                Image image = load_image((*files).paths_utf8.front());
                gui_drawing::PointF point =
                    canvas().client_to_bitmap(canvas().point_from_window(event.position));
                int x = std::clamp(static_cast<int>(std::floor(point.x)), 0,
                                   std::max(0, document.image.width - image.width));
                int y = std::clamp(static_cast<int>(std::floor(point.y)), 0,
                                   std::max(0, document.image.height - image.height));
                finish_controls();
                document.paste(image, x, y);
                if (window()) {
                    static_cast<void>((*window()).request_focus(canvas_));
                }
                refresh();
            } catch (const std::exception& exception) {
                error(exception.what());
                event.accepted_effect = gf::DragEffect::none;
            }
        }
        return;
    }
}
void Editor::edit_color(bool secondary) {
    open_editor_dialog(EditorDialogKind::color, secondary);
}

void Editor::execute(const std::string& command) {
    if (pattern_editing && command != "quit") {
        if (command == "undo" || command == "redo") { (*pattern_canvas_).undo(command == "redo"); }
        return;
    }
    try {
        if (command == "sticky-new-paper" && text.note) {
            ++(*text.note).seed;
            refresh();
            return;
        }
        if (command == "sticky-note") {
            document.require_rgba_transform();
            insert_sticky_note();
            return;
        }
        if (command == "stamp-apply") { apply_stamp_placement(); return; }
        if (command == "stamp-cancel") { cancel_stamp_placement(); return; }
        if (command == "reload") { reload_file(); return; }
        if (command == "guide-edit") {
            edit_guide();
            return;
        }
        if (command == "guide-clear") {
            unset_guide();
            refresh();
            return;
        }
        if (command == "guide-set") {
            guide.closed = guide.nodes.size() >= 3;
            guide.rebuild_boundary();
            refresh();
            return;
        }
        if (command == "guide-fill") {
            guide.fill = !guide.fill;
            refresh();
            return;
        }
        if (command == "atlas-wrap") {
            atlas_wrap = !atlas_wrap;
            refresh();
            return;
        }
        if (command == "atlas-alpha") {
            atlas_preserve_alpha = !atlas_preserve_alpha;
            refresh();
            return;
        }
        if (command == "atlas-reference-clear") {
            atlas_reference = {};
            reference_frame = -1;
            refresh();
            return;
        }
        if (command == "cut" && !document.selection.active &&
            (guide.active() || !guide.nodes.empty() || spiro.active)) {
            // A stencil is editing state. Dismissing it must not enter Cut's
            // no-selection fallback, which intentionally cuts the whole image.
            unset_guide();
            refresh();
            return;
        }
        if (command == "crop" || command == "cut" || command == "paste" || command == "resize" ||
            command == "text" || command == "select-all" || command == "new" || command == "open" ||
            command == "reshape") {
            unset_guide();
        }
        if (command == "numeric-transform") {
            document.require_rgba_transform();
            unset_guide();
            finish_text(true);
            finish_warp(false);
            release_gesture();
            document.commit_path();
            document.commit_curve();
            open_editor_dialog(EditorDialogKind::numeric_transform);
            return;
        }
        if (command.starts_with("atlas-")) {
            unset_guide();
            finish_controls();
            if ((command == "atlas-strip" || (command == "atlas-export" && document.atlas.kind == AtlasKind::Sheet)) && document.atlas.count() > 0) {
                open_editor_dialog(EditorDialogKind::toolbar_strip);
            } else if (command == "atlas-gallery") {
                open_editor_dialog(EditorDialogKind::atlas_gallery);
            } else if (command == "atlas-states" &&
                       (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor)) {
                open_editor_dialog(EditorDialogKind::icon_states);
            } else if (command == "atlas-palette" &&
                       (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor)) {
                document.require_rgba_transform();
                open_editor_dialog(EditorDialogKind::icon_palette);
            } else if (command == "atlas-validate" &&
                       (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor)) {
                open_editor_dialog(EditorDialogKind::icon_validation);
            } else if (command == "atlas-effects" &&
                       (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor)) {
                document.require_rgba_transform();
                open_editor_dialog(EditorDialogKind::icon_effects);
            } else if (command == "atlas-alpha-tools" &&
                       (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor)) {
                document.require_rgba_transform();
                open_editor_dialog(EditorDialogKind::alpha_tools);
            } else if (command == "atlas-compare" &&
                       (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor)) {
                open_editor_dialog(EditorDialogKind::icon_sheet);
            } else if (command == "atlas-export" &&
                       (document.atlas.kind == AtlasKind::Icon || document.atlas.kind == AtlasKind::Cursor)) {
                open_editor_dialog(EditorDialogKind::icon_export);
            } else if (command == "atlas-grid") {
                (*ribbon_).show_atlas();
                open_editor_dialog(EditorDialogKind::atlas_grid);
            } else if (command == "atlas-icons" || command == "atlas-cursors") {
                (*ribbon_).show_atlas();
                open_editor_dialog(command == "atlas-icons" ? EditorDialogKind::icon_sizes
                                                            : EditorDialogKind::cursor_sizes);
            } else if (command == "atlas-hotspot" && document.atlas.kind == AtlasKind::Cursor) {
                open_editor_dialog(EditorDialogKind::hotspot);
            } else if (command == "atlas-whole") {
                document.atlas_select(-1, false);
            } else if (command == "atlas-leave") {
                document.leave_atlas();
            } else if (command == "atlas-next" || command == "atlas-previous") {
                document.atlas_step(command == "atlas-next" ? 1 : -1);
            }
            refresh();
            return;
        }
        if (command == "reshape") {
            start_reshape();
            refresh();
            return;
        }
        if (command == "warp-cancel") {
            cancel_warp();
            refresh();
            return;
        }
        if (command == "warp-place") {
            finish_warp(true);
            refresh();
            return;
        }
        if (warp_active() && command == "undo") {
            cancel_warp();
            refresh();
            return;
        }
        if (warp_active() && command != "zoom-in" && command != "zoom-out" && command != "fit" &&
            command != "actual-size") {
            finish_warp(false);
        }
        if (text_command(command)) {
            return;
        }
        if (command == "text") {
            choose_tool(Tool::Text);
            (*ribbon_).show_tool_context();
            return;
        }
        if (command.starts_with("tool-")) {
            choose_tool(tools[std::stoul(command.substr(5))]);
            return;
        }
        if (command == "new") {
            if (can_replace()) {
                finish_controls();
                atlas_reference = {};
                reference_frame = -1;
                healing_brush_.clear();
                document.new_image();
                reset_file_watch();
                reset_recovery(true);
                spiro = {};
                if (document.tool == Tool::Spirograph) {
                    document.tool = Tool::Pencil;
                }
            } else if (!pending_save_path.empty()) {
                deferred_command = "new";
            }
        } else if (command == "icon-batch") {
            open_editor_dialog(EditorDialogKind::icon_batch);
        } else if (command == "browse-assets") {
            open_editor_dialog(EditorDialogKind::asset_browser);
        } else if (command == "open") {
            if (!can_replace()) {
                if (!pending_save_path.empty()) {
                    deferred_command = "open";
                }
                return;
            }
            gf::HostOpenFileDialogRequest request;
            request.title = tr("Open image");
            gf::HostPathDialogResult result = std::get<gf::HostPathDialogResult>(dialog(request).payload);
            if (result.outcome == gf::HostDialogOutcome::accepted && !result.paths.empty()) {
                open_file(result.paths.front());
            }
        } else if (command.starts_with("recent-")) {
            std::size_t index = std::stoul(command.substr(7));
            if (index < recent.paths.size()) {
                std::string path = recent.paths[index];
                if (can_replace()) {
                    open_file(path);
                } else if (!pending_save_path.empty()) {
                    deferred_open_path = path;
                }
            }
        } else if (command == "recover") {
            recover_document();
        } else if (command == "settings") {
            open_editor_dialog(EditorDialogKind::settings);
        } else if (command == "properties") {
            finish_controls();
            open_editor_dialog(EditorDialogKind::properties);
        } else if (command == "print-preview") {
            finish_controls();
            open_editor_dialog(EditorDialogKind::print_preview);
        } else if (command == "save" || command == "save-as") {
            static_cast<void>(save(command == "save-as"));
        }
#if RAINSTAR_FORMS_NATIVE_PRINT
        else if (command == "print") {
            finish_controls();
#if defined(__linux__)
            open_editor_dialog(EditorDialogKind::linux_print);
#else
            static_cast<void>(print_image(document.output_image()));
#endif
        } else if (command == "page-setup") {
#if defined(__linux__)
            open_editor_dialog(EditorDialogKind::linux_page_setup);
#else
            page_setup();
#endif
        } else if (command == "acquire") {
            if (can_replace()) {
                std::string path;
                if (acquire_picture(path)) {
                    open_file(path);
                }
            } else if (!pending_save_path.empty()) {
                deferred_command = "acquire";
            }
        } else if (command.starts_with("wallpaper-")) {
            finish_controls();
            gf::HostMonitorResult monitors = services().query_monitors();
            if (!monitors.status.accepted() || monitors.monitors.empty()) {
                throw std::runtime_error(tr("The desktop display size is unavailable."));
            }
            gf::HostMonitor monitor = monitors.monitors.front();
            for (std::size_t i = 0; i < monitors.monitors.size(); ++i) {
                if (monitors.monitors[i].primary) {
                    monitor = monitors.monitors[i];
                    break;
                }
            }
            WallpaperLayout layout = command == "wallpaper-fill"   ? WallpaperLayout::Fill
                                     : command == "wallpaper-tile" ? WallpaperLayout::Tile
                                                                   : WallpaperLayout::Center;
            Image image = wallpaper_image(document.output_image(),
                                          static_cast<int>(monitor.frame.width * monitor.scale),
                                          static_cast<int>(monitor.frame.height * monitor.scale), layout);
            set_wallpaper(desktop_export(image, "Wallpaper"));
        }
#endif
        else if (command == "quit") {
            static_cast<void>(handle_.request_close());
        } else if (command == "undo") {
            release_gesture();
            document.undo();
        } else if (command == "redo") {
            release_gesture();
            document.redo();
        } else if (command == "copy") {
            copy();
        } else if (command == "cut") {
            copy();
            if (!document.selection.active) {
                document.select_all();
            }
            document.delete_selection();
        } else if (command == "paste") {
            paste();
        } else if (command == "select-all") {
            finish_controls();
            document.select_all();
            document.tool = Tool::Select;
            if (window()) {
                static_cast<void>((*window()).request_focus(canvas_));
            }
        } else if (command == "selection-dither") {
            document.require_rgba_transform();
            document.settle_selection();
            open_editor_dialog(EditorDialogKind::dither);
            return;
        } else if (command == "invert-selection") {
            document.invert_selection();
        } else if (command == "delete") {
            finish_text(true);
            document.commit_path();
            document.commit_curve();
            if (document.selection.active) {
                document.delete_selection();
            } else {
                document.checkpoint();
                document.image.reset(document.image.width, document.image.height, document.ink.secondary);
            }
        } else if (command == "crop") {
            finish_text(true);
            release_gesture();
            if (document.selection.active) {
                document.crop();
            } else {
                choose_tool(Tool::Select);
                (*ribbon_).show_tool_context();
            }
        } else if (command == "resize") {
            finish_text(true);
            open_editor_dialog(EditorDialogKind::resize);
        } else if (command == "rotate-right" || command == "rotate-left" || command == "rotate-180") {
            finish_text(true);
            document.commit_curve();
            document.commit_path();
            document.rotate(command == "rotate-right" ? 1 : command == "rotate-left" ? 3 : 2);
        } else if (command == "flip-horizontal" || command == "flip-vertical") {
            finish_text(true);
            document.commit_curve();
            document.commit_path();
            document.flip(command == "flip-horizontal");
        } else if (command == "invert") {
            finish_controls();
            document.invert_colors();
        } else if (command == "release") {
            finish_controls(true);
            reset_stamp();
        } else if (command == "finish-path") {
            if (document.tool == Tool::Path) {
                document.end_path_geometry();
            } else {
                finish_controls();
            }
        } else if (command == "outline") {
            document.shape_outline = !document.shape_outline;
        } else if (command == "fill") {
            if (document.tool == Tool::Guide) {
                guide.fill = !guide.fill;
            } else {
                document.shape_fill = !document.shape_fill;
            }
        } else if (command == "render-crisp" || command == "render-smooth" || command == "render-smooth-4x") {
            document.ink.smooth = command != "render-crisp";
            document.ink.supersample = command == "render-smooth-4x";
        } else if (command == "smooth-lines") {
            document.ink.smooth = !document.ink.smooth;
        } else if (command == "alt-carries-body") {
            document.alt_carries_body = !document.alt_carries_body;
        } else if (command == "continuous-path") {
            document.continuous_path = !document.continuous_path;
        } else if (command == "edit-path-nodes") {
            release_gesture();
            edit_path_nodes = !edit_path_nodes;
            path_swap_segment_ = path_swap_handle_ = -1;
            if (edit_path_nodes) {
                path_swap_kind_.reset();
            }
        } else if (command == "transparent-pattern") {
            document.ink.transparent_pattern = !document.ink.transparent_pattern;
        } else if (command == "transparent-selection") {
            document.transparent_selection = !document.transparent_selection;
        } else if (command == "show-rulers" || command == "show-grid" || command == "show-status") {
            if (command == "show-rulers") {
                show_rulers = !show_rulers;
            }
            if (command == "show-grid") {
                show_grid = !show_grid;
            }
            if (command == "show-status") {
                show_status = !show_status;
            }
            invalidate(gf::Dirty::layout | gf::Dirty::paint);
        } else if (command == "full-screen") {
            gf::HostServiceStatus result = handle_.toggle_full_screen();
            if (!result.accepted()) {
                error(tr("Full screen is unavailable from this window host."));
            }
        } else if (command == "primary" || command == "secondary") {
            edit_color(command == "secondary");
        } else if (command == "zoom-in" || command == "zoom-out") {
            gf::Rect bounds = (*canvas_).client_rectangle();
            zoom(command == "zoom-in" ? 2 : 0.5,
                 cursor_client_.value_or(gf::Point{bounds.width / 2, bounds.height / 2}));
        } else if (command == "actual-size") {
            gf::Rect bounds = (*canvas_).client_rectangle();
            zoom(1 / (*canvas_).zoom(), {bounds.width / 2, bounds.height / 2});
        } else if (command == "fit") {
            gf::Rect bounds = (*canvas_).committed_arranged_bounds();
            const gf::Rect view = canvas().view_bounds();
            const double width = view.width, height = view.height;
            double scale = std::clamp(std::min((bounds.width - 32) / width, (bounds.height - 32) / height),
                                      0.0625, 32.0);
            (*canvas_).set_view(scale, {view.x + (width - bounds.width / scale) / 2,
                                        view.y + (height - bounds.height / scale) / 2});
        } else if (command == "help-english") {
            open_editor_dialog(EditorDialogKind::english_help);
        } else if (command == "help") {
            show_help = !show_help;
            invalidate(gf::Dirty::layout | gf::Dirty::paint);
        } else if (command == "about") {
            open_editor_dialog(EditorDialogKind::about);
        }
        document.sync_curve();
        document.sync_path();
        refresh();
    } catch (const std::exception& exception) {
        error(exception.what());
    }
}
} // namespace paint::forms

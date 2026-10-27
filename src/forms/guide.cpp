#include "forms/editor.hpp"
#include <algorithm>
#include <cmath>
namespace paint::forms {
namespace gf = gui_forms;
bool Editor::atlas_painting() const {
    return document.atlas.kind != AtlasKind::None && document.atlas.active >= 0;
}
bool Editor::guide_pointer(const gf::PointerEvent& event, Point point) {
    if (document.tool != Tool::Guide || event.button == gf::PointerButton::middle || panning_) {
        return false;
    }
    if (guide_swap_pointer(event, point)) {
        return true;
    }
    if (event.action == gf::PointerAction::down) {
        if (event.button == gf::PointerButton::secondary) {
            if (guide.nodes.size() == 1) {
                guide.clear();
            }
            guide_extending_ = false;
            guide_node_ = -1;
            guide_connecting_ = guide_moving_ = false;
            canvas().set_pointer_capture(false);
        } else if (event.button == gf::PointerButton::primary) {
            guide_node_ = -1;
            for (std::size_t index = 0; index < guide.nodes.size(); ++index) {
                if (std::hypot(point.x - guide.nodes[index].x, point.y - guide.nodes[index].y) *
                        canvas().zoom() <=
                    8) {
                    guide_node_ = static_cast<int>(index);
                    break;
                }
            }
            guide_connecting_ = guide_node_ >= 0 && !guide.closed;
            guide_moving_ =
                guide_node_ < 0 && guide.closed && inside_polygon(guide.boundary(), point.x, point.y);
            if (guide_node_ < 0 && !guide_moving_) {
                if (guide.closed) {
                    guide.clear();
                }
                guide.nodes.push_back(point);
                guide_extending_ = true;
                guide_node_ = static_cast<int>(guide.nodes.size()) - 1;
            }
            guide_last_ = point;
            canvas().set_pointer_capture(true);
        }
    } else if ((event.action == gf::PointerAction::move || event.action == gf::PointerAction::up) &&
               (guide_node_ >= 0 || guide_moving_)) {
        if (guide_moving_) {
            const Point delta{std::round(point.x - guide_last_.x), std::round(point.y - guide_last_.y)};
            guide.translate(delta);
            guide_last_.x += delta.x;
            guide_last_.y += delta.y;
        } else if (guide_node_ >= 0) {
            if (guide_connecting_ &&
                std::hypot(point.x - guide_last_.x, point.y - guide_last_.y) * canvas().zoom() > 3) {
                guide_connecting_ = false;
            }
            if (!guide_connecting_) {
                guide.move_node(static_cast<std::size_t>(guide_node_), point);
            }
        }
        if (event.action == gf::PointerAction::up) {
            if (guide_connecting_) {
                if (guide_node_ == 0 && guide.nodes.size() >= 3) {
                    guide.closed = true;
                    guide_extending_ = false;
                } else if (static_cast<std::size_t>(guide_node_) + 1 < guide.nodes.size()) {
                    guide.nodes.push_back(guide.nodes[guide_node_]);
                }
            }
            guide_connecting_ = false;
            guide_node_ = -1;
            guide_moving_ = false;
            canvas().set_pointer_capture(false);
        }
    }
    guide.rebuild_boundary();
    canvas().invalidate(gf::Dirty::paint);
    return true;
}
void Editor::paint_guide_overlay(gf::Painter& painter) {
    if (!guide.active() && guide.nodes.empty()) {
        return;
    }
    const gf::Color blue = gf::Color::rgba(26, 112, 174, 230), white = gf::Color::rgba(255, 255, 255, 230);
    const std::vector<Point>& outline = guide.boundary();
    const std::size_t edges = outline.size() < 2 ? 0 : guide.closed ? outline.size() : outline.size() - 1;
    for (std::size_t index = 0; index < edges; ++index) {
        const gf::Point a = screen(outline[index]), b = screen(outline[(index + 1) % outline.size()]);
        painter.draw_line(a, b, white, 3);
        painter.draw_line(a, b, blue, 1);
    }
    if (document.tool == Tool::Guide && guide_extending_ && !guide.closed && guide_node_ < 0 &&
        !guide.nodes.empty() && cursor_client_) {
        const gui_drawing::PointF mapped = canvas().client_to_bitmap(*cursor_client_);
        Point target{mapped.x, mapped.y};
        for (const Point& node : guide.nodes) {
            if (std::hypot(node.x - target.x, node.y - target.y) * canvas().zoom() <= 8) {
                target = node;
                break;
            }
        }
        const gf::Point a = screen(guide.nodes.back()), b = screen(target);
        painter.draw_line(a, b, white, 3);
        painter.draw_line(a, b, blue, 1);
    }
    if (guide.fill && guide.closed) {
        // Sparse cross-hatching identifies the protected body without hiding it.
        double left = document.image.width, top = document.image.height, right = 0, bottom = 0;
        for (std::size_t index = 0; index < guide.boundary().size(); ++index) {
            left = std::min(left, guide.boundary()[index].x);
            top = std::min(top, guide.boundary()[index].y);
            right = std::max(right, guide.boundary()[index].x);
            bottom = std::max(bottom, guide.boundary()[index].y);
        }
        const double step = std::max(1.0, 18 / canvas().zoom());
        for (double y = std::max(0.0, top); y < std::min(static_cast<double>(document.image.height), bottom);
             y += step) {
            for (double x = std::max(0.0, left);
                 x < std::min(static_cast<double>(document.image.width), right); x += step) {
                if (guide.blocked(static_cast<int>(x), static_cast<int>(y)) > 0.5) {
                    const gf::Point p = screen({x, y});
                    painter.draw_line({p.x - 2, p.y + 2}, {p.x + 2, p.y - 2},
                                      gf::Color::rgba(26, 112, 174, 85), 1);
                }
            }
        }
    }
    if (document.tool == Tool::Guide && guide_swap_segment_ >= 0 &&
        static_cast<std::size_t>(guide_swap_segment_) < guide.segments.size()) {
        const CurveGeometry& geometry = guide.segments[guide_swap_segment_].geometry;
        for (int index = 0; index < geometry.handle_count(); ++index) {
            const gf::Point point = screen(geometry.handle(index));
            if (geometry.kind == CurveKind::Bezier) {
                painter.draw_line(screen(index == 0 ? geometry.start : geometry.end), point, blue, 1);
            }
            painter.fill_rect({point.x - 5, point.y - 5, 10, 10}, white);
            painter.stroke_rect({point.x - 5, point.y - 5, 10, 10}, blue, 2);
        }
    }
    if (document.tool == Tool::Guide) {
        for (std::size_t index = 0; index < guide.nodes.size(); ++index) {
            const gf::Point point = screen(guide.nodes[index]);
            painter.fill_rounded_rect({point.x - 4, point.y - 4, 8, 8}, 4, blue);
            painter.stroke_rounded_rect({point.x - 4, point.y - 4, 8, 8}, 4, white, 1);
        }
    }
}
std::vector<std::uint8_t> Editor::canvas_selection_mask() const {
    const FloatingSelection& selection = document.selection;
    if (!selection.active || !selection.canvas_selection) return {};
    std::vector<std::uint8_t> mask(document.image.pixels.size());
    for (int y=0;y<document.image.height;++y) for(int x=0;x<document.image.width;++x) {
        mask[static_cast<std::size_t>(y)*document.image.width+x] = selection.contains(x,y) ? 1 : 0;
    }
    return mask;
}
void Editor::paint_segment(Point start, Point end) {
    const bool wrap = atlas_painting() && atlas_wrap;
    if (document.tool == Tool::Brush && brush_family == BrushFamily::Dither) {
        dither_brush_.segment(document.image,start,end,gesture_ink_.size,dither_brush_mode,gesture_ink_.noise,wrap,dither_brush_mask_);
        return;
    }
    if (document.tool == Tool::Brush && brush_family == BrushFamily::Carpet) {
        carpet_stroke_.segment(document.image, start, end, gesture_ink_.size, carpet_tile,
                               gesture_ink_.primary.a / 255.0, wrap);
        return;
    }
    if (document.tool == Tool::Eraser && atlas_painting() && atlas_preserve_alpha &&
        eraser_mode <= EraserMode::Soft) {
        return;
    }
    if (document.tool == Tool::Fill) {
        stencil_flood(document.image, start_, gesture_ink_, guide, wrap, &document.selection,
                      gradient_fill ? &fill_gradient : nullptr);
        return;
    }
    // Repeat only footprints which intersect this torus cell. The same material
    // coverage object owns overlap so duplicate edge passes do not add coats.
    const double radius = gesture_ink_.size * 0.5 + 2;
    const int min_x =
        wrap ? static_cast<int>(std::ceil((-std::max(start.x, end.x) - radius) / document.image.width)) : 0;
    const int max_x =
        wrap ? static_cast<int>(std::floor((document.image.width - std::min(start.x, end.x) + radius) /
                                           document.image.width))
             : 0;
    const int min_y =
        wrap ? static_cast<int>(std::ceil((-std::max(start.y, end.y) - radius) / document.image.height)) : 0;
    const int max_y =
        wrap ? static_cast<int>(std::floor((document.image.height - std::min(start.y, end.y) + radius) /
                                           document.image.height))
             : 0;
    if (document.tool == Tool::Brush && brush_family == BrushFamily::Additive &&
        gesture_ink_.brush != Brush::Round) {
        dynamic_brush_.segment(document.image, start, end, gesture_ink_, glitter, wrap);
        return;
    }
    if (document.tool == Tool::Brush && brush_family == BrushFamily::Heal) {
        healing_brush_.segment(document.image, start, end, gesture_ink_.size, heal_hardness, heal_correction,
                               wrap);
        return;
    }
    if (document.tool == Tool::Brush && brush_family == BrushFamily::Mix) {
        transform_brush_.segment(document.image, start, end, gesture_ink_.size, mix_effect, effect_strength,
                                 effect_scale, effect_phase, wrap);
        return;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const Point a{start.x + x * document.image.width, start.y + y * document.image.height};
            const Point b{end.x + x * document.image.width, end.y + y * document.image.height};
            if (document.tool == Tool::Pencil) {
                pixel_line(document.image, a, b, gesture_ink_);
            } else if (document.tool == Tool::Brush) {
                material_.segment(document.image, a, b, gesture_ink_);
            } else if (document.tool == Tool::Eraser) {
                if (eraser_mode == EraserMode::Hard || eraser_mode == EraserMode::Soft) {
                    eraser_.segment(document.image, a, b, gesture_ink_.size,
                                    eraser_soft || eraser_mode == EraserMode::Soft);
                } else {
                    const MixEffect effect = eraser_mode == EraserMode::Blur      ? MixEffect::Blur
                                             : eraser_mode == EraserMode::Sharpen ? MixEffect::Sharpen
                                                                                  : MixEffect::Smudge;
                    transform_brush_.segment(document.image, a, b, gesture_ink_.size, effect, effect_strength,
                                             effect_scale, effect_phase, wrap);
                }
            }
        }
    }
}
} // namespace paint::forms

namespace paint::forms {
void Editor::begin_path_swap(CurveKind kind) {
    unset_guide();
    edit_path_nodes = false;
    document.tool = Tool::Path;
    path_swap_kind_ = kind;
    path_swap_segment_ = -1;
    path_swap_handle_ = -1;
    refresh();
}
bool Editor::path_swap_pointer(const gf::PointerEvent& event, Point point) {
    if ((!path_swap_kind_ && !edit_path_nodes) || document.tool != Tool::Path || event.button == gf::PointerButton::middle ||
        panning_) {
        return false;
    }
    if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::secondary) {
        if (edit_path_nodes) {
            return false;
        }
        path_swap_kind_.reset();
        path_swap_segment_ = -1;
        path_swap_handle_ = -1;
        canvas().set_pointer_capture(false);
        refresh();
        return true;
    }
    if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
        if (window()) {
            static_cast<void>((*window()).request_focus(canvas_));
        }
        if (path_swap_segment_ >= 0 &&
            static_cast<std::size_t>(path_swap_segment_) < document.path.segments.size()) {
            const CurveGeometry& geometry = document.path.segments[path_swap_segment_].geometry;
            const int nearest_handle = hit_curve_handle(geometry, point, 10);
            if (nearest_handle >= 0) {
                path_swap_handle_ = nearest_handle;
                const Point handle = geometry.handle(nearest_handle);
                handle_offset_ = {point.x - handle.x, point.y - handle.y};
                path_swap_checkpoint_ = false;
                canvas().set_pointer_capture(true);
                return true;
            }
        }
        if (edit_path_nodes && hit_path_node(point) >= 0) {
            path_swap_segment_ = -1;
            return false;
        }
        const std::vector<PathEdge> edges = document.path_edges();
        double closest = 8 / canvas().zoom();
        int selected = -1;
        int selected_curve = -1;
        for (std::size_t index = 0; index < edges.size(); ++index) {
            int curve_index = -1;
            std::vector<Point> samples{document.path.nodes[edges[index].first],
                                       document.path.nodes[edges[index].last]};
            for (std::size_t curve = 0; curve < document.path.segments.size(); ++curve) {
                const PathSegment& segment = document.path.segments[curve];
                if (segment.first == edges[index].first && segment.last == edges[index].last) {
                    samples = segment.geometry.samples();
                    curve_index = static_cast<int>(curve);
                    break;
                }
            }
            if (edit_path_nodes && curve_index < 0) {
                continue;
            }
            for (std::size_t sample = 1; sample < samples.size(); ++sample) {
                const Point a = samples[sample - 1], b = samples[sample];
                const double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
                const double t =
                    length > 0 ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0, 1.0)
                               : 0;
                const double distance = std::hypot(point.x - a.x - t * dx, point.y - a.y - t * dy);
                if (distance < closest) {
                    closest = distance;
                    selected = static_cast<int>(index);
                    selected_curve = curve_index;
                }
            }
        }
        if (edit_path_nodes) {
            path_swap_segment_ = selected_curve;
            refresh();
            return selected_curve >= 0;
        } else if (selected >= 0) {
            path_swap_segment_ = document.swap_path_segment(edges[selected], *path_swap_kind_);
            refresh();
        }
    } else if ((event.action == gf::PointerAction::move || event.action == gf::PointerAction::up) &&
               path_swap_handle_ >= 0) {
        if (path_swap_segment_ < 0 ||
            static_cast<std::size_t>(path_swap_segment_) >= document.path.segments.size()) {
            path_swap_handle_ = -1;
            canvas().set_pointer_capture(false);
            return true;
        }
        CurveGeometry next = document.path.segments[path_swap_segment_].geometry;
        const Point previous = next.handle(path_swap_handle_);
        next.move_handle(path_swap_handle_, {point.x - handle_offset_.x, point.y - handle_offset_.y});
        const Point moved = next.handle(path_swap_handle_);
        if (std::hypot(moved.x - previous.x, moved.y - previous.y) > 1e-9) {
            if (!path_swap_checkpoint_) {
                document.checkpoint();
                path_swap_checkpoint_ = true;
            }
            document.path.segments[path_swap_segment_].geometry = next;
            document.sync_path();
        }
        refresh();
        if (event.action == gf::PointerAction::up) {
            path_swap_handle_ = -1;
            canvas().set_pointer_capture(false);
        }
    } else if (edit_path_nodes) {
        return false;
    }
    return true;
}
void Editor::paint_path_swap(gf::Painter& painter) {
    if ((!path_swap_kind_ && !edit_path_nodes) || path_swap_segment_ < 0 ||
        static_cast<std::size_t>(path_swap_segment_) >= document.path.segments.size()) {
        return;
    }
    const CurveGeometry& geometry = document.path.segments[path_swap_segment_].geometry;
    const int hovered = hovered_curve_handle(geometry, 10);
    const gf::Color blue = gf::Color::rgba(30, 110, 190), white = gf::Color::rgba(255, 255, 255);
    for (int index = 0; index < geometry.handle_count(); ++index) {
        const gf::Point point = screen(geometry.handle(index));
        if (index == path_swap_handle_) {
            painter.fill_rect({point.x - 9, point.y - 9, 18, 18}, gf::Color::rgba(255, 190, 40));
        } else if (index == hovered) {
            painter.fill_rect({point.x - 8, point.y - 8, 16, 16}, gf::Color::rgba(255, 220, 130));
        }
        if (geometry.kind == CurveKind::Bezier) {
            painter.draw_line(screen(index == 0 ? geometry.start : geometry.end), point, blue, 1);
        }
        painter.fill_rect({point.x - 5, point.y - 5, 10, 10}, white);
        painter.stroke_rect({point.x - 5, point.y - 5, 10, 10}, blue, 2);
    }
}
} // namespace paint::forms

namespace paint::forms {
void Editor::unset_guide() {
    // Spirograph apparatus has the same temporary lifetime as a drawing guide.
    cancel_spiro_drag();
    spiro = {};
    if (document.tool == Tool::Spirograph) {
        document.tool = Tool::Pencil;
    }
    guide.clear();
    guide_swap_kind_.reset();
    guide_swap_segment_ = guide_swap_handle_ = guide_node_ = -1;
    guide_extending_ = guide_connecting_ = guide_moving_ = false;
    canvas().set_pointer_capture(false);
    if (document.tool == Tool::Guide) {
        document.tool = guide_previous_tool_;
    }
}
void Editor::edit_guide() {
    if (document.tool != Tool::Guide) {
        guide_previous_tool_ = document.tool == Tool::Brush || document.tool == Tool::Stamp ||
                                       document.tool == Tool::Fill || document.tool == Tool::Eraser
                                   ? document.tool
                                   : Tool::Pencil;
        finish_controls();
    }
    document.tool = Tool::Guide;
    guide_swap_kind_.reset();
    guide_swap_segment_ = guide_swap_handle_ = guide_node_ = -1;
    guide_extending_ = false;
    refresh();
}
void Editor::begin_guide_swap(CurveKind kind) {
    edit_guide();
    guide_swap_kind_ = kind;
    refresh();
}
bool Editor::guide_swap_pointer(const gf::PointerEvent& event, Point point) {
    if (!guide_swap_kind_) {
        return false;
    }
    if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::secondary) {
        edit_guide();
        canvas().set_pointer_capture(false);
        return true;
    }
    if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
        if (guide_swap_segment_ >= 0 &&
            static_cast<std::size_t>(guide_swap_segment_) < guide.segments.size()) {
            const CurveGeometry& geometry = guide.segments[guide_swap_segment_].geometry;
            for (int handle = 0; handle < geometry.handle_count(); ++handle) {
                const Point location = geometry.handle(handle);
                if (std::hypot(point.x - location.x, point.y - location.y) * canvas().zoom() < 10) {
                    guide_swap_handle_ = handle;
                    canvas().set_pointer_capture(true);
                    return true;
                }
            }
        }
        const std::size_t edges = guide.nodes.size() < 2 ? 0
                                  : guide.closed         ? guide.nodes.size()
                                                         : guide.nodes.size() - 1;
        double closest = 8 / canvas().zoom();
        int selected = -1;
        for (std::size_t edge = 0; edge < edges; ++edge) {
            std::vector<Point> samples{guide.nodes[edge], guide.nodes[(edge + 1) % guide.nodes.size()]};
            for (const GuideSegment& segment : guide.segments) {
                if (segment.edge == edge) {
                    samples = segment.geometry.samples();
                    break;
                }
            }
            for (std::size_t sample = 1; sample < samples.size(); ++sample) {
                const Point a = samples[sample - 1], b = samples[sample];
                const double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
                const double t =
                    length > 0 ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0, 1.0)
                               : 0;
                const double distance = std::hypot(point.x - a.x - t * dx, point.y - a.y - t * dy);
                if (distance < closest) {
                    closest = distance;
                    selected = static_cast<int>(edge);
                }
            }
        }
        if (selected >= 0) {
            guide_swap_segment_ = guide.swap_segment(static_cast<std::size_t>(selected), *guide_swap_kind_);
        }
    } else if ((event.action == gf::PointerAction::move || event.action == gf::PointerAction::up) &&
               guide_swap_handle_ >= 0) {
        guide.move_handle(guide_swap_segment_, guide_swap_handle_, point);
        if (event.action == gf::PointerAction::up) {
            guide_swap_handle_ = -1;
            canvas().set_pointer_capture(false);
        }
    }
    canvas().invalidate(gf::Dirty::paint);
    return true;
}
} // namespace paint::forms

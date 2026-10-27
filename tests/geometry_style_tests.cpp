#include "document.hpp"
#include "geometry_style.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
bool near(paint::Point a, paint::Point b) {
    return std::hypot(a.x - b.x, a.y - b.y) < 1e-8;
}
void corner_geometry() {
    const std::vector<paint::Point> square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const std::vector<paint::Point> unchanged = paint::soften_corners(square, 0, true);
    require(unchanged.size() == square.size(), "zero softness must preserve original vertices");
    for (std::size_t i = 0; i < square.size(); ++i) {
        require(near(square[i], unchanged[i]), "zero radius changed a point");
    }
    const std::vector<paint::Point> rounded = paint::soften_corners(square, 10, true);
    require(rounded.size() > 12 && near(rounded.front(), {0, 10}),
            "fillet must start at the incoming tangent");
    bool found_tangent = false;
    for (paint::Point point : rounded) {
        require(point.x >= -1e-8 && point.y >= -1e-8 && point.x <= 100 + 1e-8 && point.y <= 100 + 1e-8,
                "rounded polygon escaped its original bounds");
        if (point.x <= 10 && point.y <= 10) {
            require(std::abs(std::hypot(point.x - 10, point.y - 10) - 10) < 1e-8,
                    "corner is not a circular arc of the requested radius");
        }
        found_tangent = found_tangent || near(point, {10, 0});
    }
    require(found_tangent, "outgoing fillet tangent was omitted");
    const std::vector<paint::Point> concave{{0, 0}, {100, 0}, {100, 50}, {50, 50}, {50, 100}, {0, 100}};
    const std::vector<paint::Point> softened = paint::soften_corners(concave, 10, true);
    bool found_concave_arc = false;
    for (paint::Point point : softened) {
        if (point.x > 50 && point.x < 60 && point.y > 50 && point.y < 60) {
            require(std::abs(std::hypot(point.x - 60, point.y - 60) - 10) < 1e-8,
                    "concave corner must follow the opposite circular arc");
            found_concave_arc = true;
        }
    }
    require(found_concave_arc, "concave corner remained sharp");
    const std::vector<paint::Point> short_path{{0, 0}, {0, 2}, {4, 2}};
    const std::vector<paint::Point> limited = paint::soften_corners(short_path, 4096, false);
    require(near(limited.front(), short_path.front()) && near(limited.back(), short_path.back()),
            "softening must preserve open path endpoints");
    for (paint::Point point : limited) {
        require(point.x >= -1e-8 && point.x <= 4 && point.y >= -1e-8 && point.y <= 2,
                "large radius reversed a short edge");
    }
    const std::vector<paint::Point> duplicates{{0, 0}, {0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 0}};
    require(paint::soften_corners(duplicates, 10, true).size() == rounded.size(),
            "duplicate vertices changed the fillets");
    require(paint::soften_corners({{0, 0}, {10, std::numeric_limits<double>::infinity()}, {20, 20}}, 10, true)
                .empty(),
            "nonfinite corner geometry must be rejected before rasterization");
}
void filled_geometry() {
    for (bool supersample : {false, true}) {
        paint::Image image;
        image.reset(100, 100, {0, 0, 0, 0});
        paint::Ink ink;
        ink.geometry.corner_radius = 20;
        ink.supersample = supersample;
        ink.secondary = {30, 80, 190, 128};
        paint::draw_shape(image, paint::Shape::Rectangle, {10, 10}, {90, 90}, ink, false, true);
        require(image.get(11, 11).a == 0 && image.get(40, 40).a == 128,
                "rounded fill must omit corners and preserve interior opacity");
        require(image.get(10, 45).a > 0, "softening must retain straight edge tangency");
    }
    paint::Document document;
    document.ink.geometry.corner_radius = 17;
    require(document.alternate_ink().geometry.corner_radius == 17,
            "alternate-color geometry must use the same corner setting");
}
void path_history() {
    paint::Document document;
    document.new_image(100, 100);
    document.ink.geometry.corner_radius = 15;
    document.ink.geometry.organic = 1.5;
    document.ink.geometry.stroke = paint::StrokeStyle::Dashed;
    document.shape_fill = true;
    document.continuous_path = false;
    document.add_path_node({10, 10});
    document.add_path_node({90, 10});
    document.add_path_node({90, 90});
    document.add_path_node({10, 90});
    document.end_path_geometry();
    const paint::Image original = document.image;
    document.ink.geometry.corner_radius = 0;
    document.sync_path();
    require(std::equal(original.pixels.begin(), original.pixels.end(), document.image.pixels.begin(),
                       paint::equal),
            "later corner settings must not change retained path runs");
    document.checkpoint();
    document.move_path_node(0, {25, 25});
    const paint::Image moved = document.image;
    require(!std::equal(original.pixels.begin(), original.pixels.end(), moved.pixels.begin(), paint::equal),
            "moving retained rounded geometry must redraw its pixels");
    document.undo();
    require(document.path.runs.front().ink.geometry.corner_radius == 15 &&
                document.path.runs.front().ink.geometry.organic == 1.5 &&
                document.path.runs.front().ink.geometry.stroke == paint::StrokeStyle::Dashed &&
                std::equal(original.pixels.begin(), original.pixels.end(), document.image.pixels.begin(),
                           paint::equal),
            "undo must restore rounded path style and its original pixels");
    document.redo();
    require(std::equal(moved.pixels.begin(), moved.pixels.end(), document.image.pixels.begin(), paint::equal),
            "redo must restore the rounded node edit exactly");
}
void outline_styles() {
    const std::vector<paint::Point> bent{{0, 0}, {10, 0}, {10, 30}};
    const std::vector<paint::StrokeSegment> dashed =
        paint::styled_outline(bent, paint::StrokeStyle::Dashed, 4, false);
    require(dashed.size() == 3 && near(dashed[1].first, {10, 0}) && near(dashed[1].last, {10, 6}) &&
                near(dashed[2].first, {10, 18}),
            "dash phase must continue across a corner");
    const std::vector<paint::StrokeSegment> dotted =
        paint::styled_outline(bent, paint::StrokeStyle::Dotted, 2, false);
    require(dotted.size() == 7 && near(dotted[2].first, {10, 2}),
            "dot spacing must continue across a corner");
    for (const paint::StrokeSegment& segment : dotted) {
        require(near(segment.first, segment.last), "a dot must use one brush footprint");
    }
    for (bool supersample : {false, true}) {
        paint::Ink ink;
        ink.size = 4;
        ink.primary = {30, 80, 190, 128};
        ink.supersample = supersample;
        for (paint::StrokeStyle style : {paint::StrokeStyle::Dashed, paint::StrokeStyle::Dotted}) {
            ink.geometry.stroke = style;
            paint::Image image, subdivided;
            image.reset(140, 40, {0, 0, 0, 0});
            subdivided = image;
            paint::polygon(image, {{10, 20}, {122, 20}}, ink, true, false, false);
            paint::polygon(subdivided, {{10, 20}, {23, 20}, {53, 20}, {122, 20}}, ink, true, false, false);
            require(
                std::equal(image.pixels.begin(), image.pixels.end(), subdivided.pixels.begin(), paint::equal),
                "adding collinear path nodes changed stroke spacing or opacity");
            require(image.get(10, 20).a == 128 &&
                        image.get(style == paint::StrokeStyle::Dashed ? 32 : 16, 20).a == 0,
                    "styled outline lost its mark or its gap");
            for (paint::Color pixel : image.pixels) {
                require(pixel.a <= 128, "styled outline compounded opacity at joins");
            }
        }
    }
    paint::Ink ink;
    ink.geometry.stroke = paint::StrokeStyle::Dotted;
    ink.size = 4;
    ink.secondary = {30, 80, 190, 128};
    paint::Image image;
    image.reset(100, 100, {0, 0, 0, 0});
    paint::draw_shape(image, paint::Shape::Rectangle, {10, 10}, {90, 90}, ink, true, true);
    require(image.get(50, 50).a == 128, "outline style must not dash the interior fill");
}
void organic_geometry() {
    const std::vector<paint::Point> line{{10, 20}, {210, 20}};
    const std::vector<paint::Point> straight = paint::organic_contour(line, 0, 1, false);
    require(straight.size() == line.size() && near(straight.front(), line.front()),
            "zero Organic must preserve the original contour exactly");
    const std::vector<paint::Point> first = paint::organic_contour(line, 1.5, 1, false);
    const std::vector<paint::Point> repeated = paint::organic_contour(line, 1.5, 1, false);
    const std::vector<paint::Point> different = paint::organic_contour(line, 1.5, 2, false);
    const std::vector<paint::Point> shifted = paint::organic_contour({{60, 90}, {260, 90}}, 1.5, 1, false);
    bool changed = false, varied = false;
    require(near(first.front(), line.front()) && near(first.back(), line.back()),
            "Organic must preserve open endpoints");
    for (std::size_t index = 0; index < first.size(); ++index) {
        require(near(first[index], repeated[index]), "Organic must be deterministic for a retained seed");
        require(near({first[index].x + 50, first[index].y + 70}, shifted[index]),
                "moving geometry must not change its Organic appearance");
        require(std::abs(first[index].y - 20) <= 3, "Organic exceeded its bounded variation");
        changed = changed || std::abs(first[index].y - 20) > 0.1;
        varied = varied || !near(first[index], different[index]);
    }
    require(changed && varied, "Organic amount and variation must affect the contour");
    const std::vector<paint::Point> square{{20, 20}, {100, 20}, {100, 100}, {20, 100}};
    const std::vector<paint::Point> closed = paint::organic_contour(square, 3, 1, true);
    require(std::hypot(closed.front().x - closed.back().x, closed.front().y - closed.back().y) < 6,
            "closed Organic contour has a discontinuous seam");
    paint::Ink ink;
    ink.geometry.organic = 3;
    ink.secondary = {30, 80, 190, 128};
    ink.supersample = true;
    paint::Image image;
    image.reset(120, 120, {0, 0, 0, 0});
    paint::polygon(image, square, ink, false, true, true);
    require(image.get(60, 60).a == 128, "Organic must preserve the fill's interior opacity");
}
void endpoint_markers() {
    const std::vector<paint::Point> line{{20, 50}, {120, 50}};
    for (int index = 0; index < paint::endpoint_marker_count; ++index) {
        paint::GeometryStyle style;
        style.start = style.end = static_cast<paint::EndpointMarker>(index);
        const std::vector<paint::StrokeSegment> segments = paint::geometry_outline(line, style, 4, false);
        const std::vector<paint::StrokeSegment> rotated =
            paint::geometry_outline({{-50, 20}, {-50, 120}}, style, 4, false);
        require(!segments.empty() && segments.size() == rotated.size(),
                "endpoint marker geometry is missing");
        for (std::size_t i = 0; i < segments.size(); ++i) {
            require(near({-segments[i].first.y, segments[i].first.x}, rotated[i].first) &&
                        near({-segments[i].last.y, segments[i].last.x}, rotated[i].last),
                    "endpoint markers must rotate with the path tangent");
        }
        for (bool supersample : {false, true}) {
            paint::Image image;
            image.reset(150, 100, {0, 0, 0, 0});
            paint::Ink ink;
            ink.size = 4;
            ink.primary = {30, 80, 190, 128};
            ink.geometry = style;
            ink.supersample = supersample;
            paint::polygon(image, line, ink, true, false, false);
            require(image.get(70, 50).a == 128, "endpoint markers lost the shaft");
            for (paint::Color pixel : image.pixels) {
                require(pixel.a <= 128, "marker and shaft overlaps compounded opacity");
            }
            if (style.start == paint::EndpointMarker::Circle) {
                require(image.get(26, 50).a == 0, "shaft must not cross a hollow circle marker");
            }
            if (style.end == paint::EndpointMarker::Arrow) {
                require(image.get(108, 57).a > 0, "arrow wing was clipped outside the shaft bounds");
            }
        }
    }
    paint::GeometryStyle style;
    style.start = paint::EndpointMarker::Circle;
    style.end = paint::EndpointMarker::ZeroMany;
    const std::vector<paint::Point> closed{{10, 10}, {80, 10}, {80, 80}};
    const std::vector<paint::StrokeSegment> plain =
        paint::styled_outline(closed, paint::StrokeStyle::Solid, 4, true);
    const std::vector<paint::StrokeSegment> marked = paint::geometry_outline(closed, style, 4, true);
    require(plain.size() == marked.size(), "closed geometry must not acquire endpoint markers");
    for (std::size_t i = 0; i < plain.size(); ++i) {
        require(near(plain[i].first, marked[i].first) && near(plain[i].last, marked[i].last),
                "endpoint settings changed a closed outline");
    }
    require(paint::geometry_outline({{10, 10}, {10, 10}}, style, 4, false).empty(),
            "zero-length geometry must not invent a marker direction");
    const std::vector<paint::StrokeSegment> short_line =
        paint::geometry_outline({{10, 10}, {13, 10}}, style, 16, false);
    for (const paint::StrokeSegment& segment : short_line) {
        for (paint::Point point : {segment.first, segment.last}) {
            require(std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 10 - 1e-8 &&
                        point.x <= 13 + 1e-8,
                    "short-path markers must scale down without reversing the shaft");
        }
    }
    paint::Document document;
    document.new_image(150, 100);
    document.ink.geometry = style;
    document.add_path_node({20, 50});
    document.add_path_node({120, 50});
    document.end_path_geometry();
    const paint::Image original = document.image;
    document.checkpoint();
    document.move_path_node(1, {100, 70});
    const paint::Image moved = document.image;
    document.undo();
    require(document.path.runs.front().ink.geometry.end == paint::EndpointMarker::ZeroMany &&
                std::equal(original.pixels.begin(), original.pixels.end(), document.image.pixels.begin(),
                           paint::equal),
            "undo must retain endpoint marker style and pixels");
    document.redo();
    require(std::equal(moved.pixels.begin(), moved.pixels.end(), document.image.pixels.begin(), paint::equal),
            "redo must restore marker direction after a node edit");
}
} // namespace
int main() {
    try {
        corner_geometry();
        filled_geometry();
        path_history();
        outline_styles();
        organic_geometry();
        endpoint_markers();
        std::cout << "Corner geometry and raster contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
